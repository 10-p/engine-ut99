/*=============================================================================
	NSDLAndroid.cpp: the Android touch overlay's side of NSDLDrv.

	The on-screen overlay (Android/app/src/main/java/com/ut99/android/GameActivity.java, a View above SDL's
	surface) drives play in landscape: a move stick on the left half, a drag-look on the right half, and the
	FIRE, ALTFIRE, JUMP, CROUCH, NEXTWEAPON and MENU buttons. While a menu is up (bShowWindowsMouse, published
	below) the overlay lets the touches through to SDL, which turns them into mouse events for UWindow.

	The overlay runs on Android's UI thread, so it writes its intent here through JNI and
	UNSDLViewport::TickAndroidTouch (the engine thread) turns it into the CauseInputEvent calls a mouse and a
	pad would make, on keys DefUser.ini binds:
	  move stick    -> IK_JoyX / IK_JoyY       (aStrafe / aBaseY)
	  drag-look     -> IK_MouseX / IK_MouseY   (the mouse's own axes)
	  FIRE/ALTFIRE  -> IK_LeftMouse / IK_RightMouse
	  JUMP/CROUCH   -> IK_Space / IK_C
	  NEXTWEAPON    -> IK_MouseWheelDown (a press and release)
	  MENU          -> IK_Escape
=============================================================================*/

#include <jni.h>
#include <atomic>
#include <mutex>
#include <vector>

#include "NSDLDrv.h"

// The button ids GameActivity.Ut99TouchOverlayView sends.
enum EAndroidTouchButton
{
	ATB_Fire       = 0,
	ATB_AltFire    = 1,
	ATB_Jump       = 2,
	ATB_Crouch     = 3,
	ATB_NextWeapon = 4,
	ATB_Menu       = 5,
};

// Menu or play, for the overlay: the engine thread writes it each tick, the UI thread reads it.
static std::atomic<bool> GAndroidMenuVisible( false );

// The move stick, held-absolute in [-1,1]; the UI thread writes, the engine thread reads.
static std::atomic<float> GAndroidMoveX( 0.f );
static std::atomic<float> GAndroidMoveY( 0.f );
static float GAndroidLastMoveX = 0.f;
static float GAndroidLastMoveY = 0.f;

// The drag-look, in pixels: the UI thread adds, the engine thread takes and clears each tick.
static std::atomic<float> GAndroidLookX( 0.f );
static std::atomic<float> GAndroidLookY( 0.f );

// Button edges, a queue so a tap pressed and released between two ticks still fires.
struct FAndroidButtonEdge { INT Key; BYTE Action; };
static std::mutex GAndroidButtonMutex;
static std::vector<FAndroidButtonEdge> GAndroidButtonQueue;

static void AndroidQueueButton( INT Key, EInputAction Action )
{
	std::lock_guard<std::mutex> Lock( GAndroidButtonMutex );
	FAndroidButtonEdge Edge; Edge.Key = Key; Edge.Action = (BYTE)Action;
	GAndroidButtonQueue.push_back( Edge );
}

static inline void AndroidAtomicAdd( std::atomic<float>& Value, float Delta )
{
	float Old = Value.load( std::memory_order_relaxed );
	while( !Value.compare_exchange_weak( Old, Old + Delta, std::memory_order_relaxed ) ) {}
}

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_ut99_android_GameActivity_nativeIsMenuVisible( JNIEnv*, jclass )
{
	return GAndroidMenuVisible.load( std::memory_order_relaxed ) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_ut99_android_GameActivity_nativeTouchAxis( JNIEnv*, jclass, jint Axis, jfloat Value )
{
	// Axis 0 is strafe, 1 forward.
	if( Axis == 0 )
		GAndroidMoveX.store( Value, std::memory_order_relaxed );
	else if( Axis == 1 )
		GAndroidMoveY.store( Value, std::memory_order_relaxed );
}

JNIEXPORT void JNICALL
Java_com_ut99_android_GameActivity_nativeTouchLook( JNIEnv*, jclass, jfloat DX, jfloat DY )
{
	AndroidAtomicAdd( GAndroidLookX, DX );
	AndroidAtomicAdd( GAndroidLookY, DY );
}

JNIEXPORT void JNICALL
Java_com_ut99_android_GameActivity_nativeTouchButton( JNIEnv*, jclass, jint Button, jboolean Down )
{
	INT Key = IK_None;
	switch( Button )
	{
	case ATB_Fire:       Key = IK_LeftMouse;      break;
	case ATB_AltFire:    Key = IK_RightMouse;     break;
	case ATB_Jump:       Key = IK_Space;          break;
	case ATB_Crouch:     Key = IK_C;              break;
	case ATB_NextWeapon: Key = IK_MouseWheelDown; break;
	case ATB_Menu:       Key = IK_Escape;         break;
	default: return;
	}
	if( Key == IK_MouseWheelDown )
	{
		// A wheel notch is a press and a release.
		if( Down )
		{
			AndroidQueueButton( Key, IST_Press );
			AndroidQueueButton( Key, IST_Release );
		}
		return;
	}
	AndroidQueueButton( Key, Down ? IST_Press : IST_Release );
}

} // extern "C"

//
// Fold the overlay's input into the engine's, once per tick on the engine thread.
//
void UNSDLViewport::TickAndroidTouch()
{
	guard(UNSDLViewport::TickAndroidTouch);

	GAndroidMenuVisible.store( bShowWindowsMouse ? true : false, std::memory_order_relaxed );

	// The button edges, taken out under the lock and sent without it.
	{
		std::vector<FAndroidButtonEdge> Edges;
		{
			std::lock_guard<std::mutex> Lock( GAndroidButtonMutex );
			Edges.swap( GAndroidButtonQueue );
		}
		for( size_t i = 0; i < Edges.size(); ++i )
			CauseInputEvent( Edges[i].Key, (EInputAction)Edges[i].Action );
	}

	// The drag-look as the mouse's axes (the SDL_MOUSEMOTION handler's sign convention).
	{
		const FLOAT LookX = GAndroidLookX.exchange( 0.f, std::memory_order_relaxed );
		const FLOAT LookY = GAndroidLookY.exchange( 0.f, std::memory_order_relaxed );
		if( LookX != 0.f )
			CauseInputEvent( IK_MouseX, IST_Axis, +LookX );
		if( LookY != 0.f )
			CauseInputEvent( IK_MouseY, IST_Axis, -LookY );
	}

	// The move stick as the pad's left stick: the value every tick while it is held, and one 0 when it is
	// released. The scale and signs are the pad path's (TickInput: UNSDLClient::ScaleXYZ, JoyAxisDefaultScale,
	// InvertY, per second); the overlay's Y is SDL's, down positive.
	{
		const DOUBLE CurTime = appSeconds();
		const FLOAT DeltaTime = InputUpdateTime > 0.0 ? (FLOAT)Clamp( CurTime - InputUpdateTime, 0.0, 0.25 ) : 0.f;
		const FLOAT MoveX = GAndroidMoveX.load( std::memory_order_relaxed );
		const FLOAT MoveY = GAndroidMoveY.load( std::memory_order_relaxed );
		const FLOAT Scale = Client->ScaleXYZ * 60.f * DeltaTime;
		const FLOAT ScaleY = Client->InvertY ? Scale : -Scale;
		if( MoveX != 0.f || GAndroidLastMoveX != 0.f )
			CauseInputEvent( IK_JoyX, IST_Axis, MoveX * Scale );
		if( MoveY != 0.f || GAndroidLastMoveY != 0.f )
			CauseInputEvent( IK_JoyY, IST_Axis, MoveY * ScaleY );
		GAndroidLastMoveX = MoveX;
		GAndroidLastMoveY = MoveY;
	}

	unguard;
}
