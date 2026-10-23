/*=============================================================================
	NSDLInputTest.cpp: the input test channel (NSDLInputTest.h).
=============================================================================*/

#include "NSDLDrv.h"
#include "NSDLInputTest.h"

// Every SDL controller button and axis, in SDL's own order: with these masks SDL's mapping for a virtual
// controller makes joystick button/axis i controller button/axis i.
enum
{
	VPAD_BUTTONS	= SDL_CONTROLLER_BUTTON_DPAD_RIGHT + 1,
	VPAD_AXES		= SDL_CONTROLLER_AXIS_MAX,
};

/*-----------------------------------------------------------------------------
	Reflection helpers: the console and UWindow are script classes of other packages.
-----------------------------------------------------------------------------*/

static UObject* GetObjectProp( UObject* Obj, const TCHAR* Name )
{
	UObjectProperty* P = Obj ? FindField<UObjectProperty>( Obj->GetClass(), Name ) : NULL;
	UObject* Value = NULL;
	if( P )
		appMemcpy( &Value, (BYTE*)Obj + P->Offset, sizeof(Value) );
	return Value;
}

static FLOAT GetFloatProp( UObject* Obj, const TCHAR* Name )
{
	UFloatProperty* P = Obj ? FindField<UFloatProperty>( Obj->GetClass(), Name ) : NULL;
	return P ? *(FLOAT*)((BYTE*)Obj + P->Offset) : 0.f;
}

static const TCHAR* ClassNameOf( UObject* Obj )
{
	return Obj ? Obj->GetClass()->GetName() : TEXT("None");
}

/*-----------------------------------------------------------------------------
	FNSDLInputTest.
-----------------------------------------------------------------------------*/

FNSDLInputTest::FNSDLInputTest( const TCHAR* InPath )
:	File( NULL )
,	PartialLen( 0 )
,	Virtual( NULL )
,	VirtualID( -1 )
,	MouseX( 0 )
,	MouseY( 0 )
,	WatchLeft( 0 )
,	WatchIndex( 0 )
{
	File = fopen( TCHAR_TO_ANSI(InPath), "r" );
	debugf( NAME_Log, TEXT("NSDLInputTest: channel %s: %s"), InPath, File ? TEXT("open") : TEXT("cannot open") );
}

FNSDLInputTest::~FNSDLInputTest()
{
	Detach();
	if( File )
		fclose( File );
}

void FNSDLInputTest::Detach()
{
	if( !Virtual )
		return;
	SDL_JoystickClose( Virtual );
	Virtual = NULL;
	for( INT i=0; i<SDL_NumJoysticks(); i++ )
		if( SDL_JoystickGetDeviceInstanceID( i )==VirtualID )
		{
			SDL_JoystickDetachVirtual( i );
			break;
		}
	VirtualID = -1;
}

void FNSDLInputTest::Tick( UNSDLViewport* Viewport )
{
	guard(FNSDLInputTest::Tick);
	if( !File )
		return;
	if( WatchLeft > 0 )
	{
		APlayerPawn* Actor = Viewport->Actor;
		debugf( NAME_Log, TEXT("NSDLInputTest: watch %s %i %.4f %i %i %.1f"), *WatchTag, WatchIndex++, appSeconds(),
			Actor ? Actor->ViewRotation.Yaw : 0, Actor ? Actor->ViewRotation.Pitch : 0, Actor ? Actor->Velocity.Size2D() : 0.f );
		WatchLeft--;
	}
	for( ;; )
	{
		int C = fgetc( File );
		if( C==EOF )
		{
			// Lines are appended while the file is read: forget the end of file and look again next tick.
			clearerr( File );
			fseek( File, 0, SEEK_CUR );
			return;
		}
		if( C=='\r' )
			continue;
		if( C!='\n' )
		{
			if( PartialLen < (INT)sizeof(Partial)-1 )
				Partial[PartialLen++] = (ANSICHAR)C;
			continue;
		}
		Partial[PartialLen] = 0;
		PartialLen = 0;
		if( !Partial[0] || Partial[0]=='#' )
			continue;
		if( Execute( Viewport, appFromAnsi(Partial) ) )
			return;
	}
	unguard;
}

UBOOL FNSDLInputTest::Execute( UNSDLViewport* Viewport, const TCHAR* Line )
{
	guard(FNSDLInputTest::Execute);
	debugf( NAME_Log, TEXT("NSDLInputTest: run %s"), Line );
	const TCHAR* Cmd = Line;
	if( ParseCommand(&Cmd,TEXT("PAD")) )
	{
		if( ParseCommand(&Cmd,TEXT("ATTACH")) )
		{
			if( Virtual )
				return 0;
			SDL_VirtualJoystickDesc Desc;
			SDL_zero( Desc );
			Desc.version     = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
			Desc.type        = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
			Desc.naxes       = VPAD_AXES;
			Desc.nbuttons    = VPAD_BUTTONS;
			Desc.button_mask = (1u << VPAD_BUTTONS) - 1;
			Desc.axis_mask   = (1u << VPAD_AXES) - 1;
			Desc.name        = "NSDLInputTest virtual pad";
			INT Index = SDL_JoystickAttachVirtualEx( &Desc );
			if( Index < 0 )
			{
				debugf( NAME_Log, TEXT("NSDLInputTest: virtual pad: attach failed: %s"), appFromAnsi(SDL_GetError()) );
				return 0;
			}
			Virtual   = SDL_JoystickOpen( Index );
			VirtualID = Virtual ? SDL_JoystickInstanceID( Virtual ) : -1;
			// A trigger rests at the bottom of its raw range, which the controller reads as 0.
			if( Virtual )
			{
				SDL_JoystickSetVirtualAxis( Virtual, SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768 );
				SDL_JoystickSetVirtualAxis( Virtual, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768 );
			}
			// The virtual pad takes the client's one pad place from any pad the machine has.
			UNSDLClient* Client = Viewport->Client;
			if( SDL_GameController* GC = Client->GetController() )
				if( Client->ClosePad( SDL_JoystickInstanceID( SDL_GameControllerGetJoystick( GC ) ) ) )
					Viewport->ReleasePad();
			Client->OpenPad( Index, NULL );
			debugf( NAME_Log, TEXT("NSDLInputTest: virtual pad: joystick %i, game controller %i"), Index, (INT)SDL_IsGameController(Index) );
			return 1;
		}
		if( ParseCommand(&Cmd,TEXT("DETACH")) )
		{
			Detach();
			debugf( NAME_Log, TEXT("NSDLInputTest: virtual pad: detached") );
			return 1;
		}
		const UBOOL IsButton = ParseCommand(&Cmd,TEXT("BUTTON"));
		if( IsButton || ParseCommand(&Cmd,TEXT("AXIS")) )
		{
			FString Name;
			ParseToken( Cmd, Name, 0 );
			INT Value = appAtoi( Cmd );
			if( IsButton )
			{
				SDL_GameControllerButton B = SDL_GameControllerGetButtonFromString( TCHAR_TO_ANSI(*Name) );
				if( !Virtual || B==SDL_CONTROLLER_BUTTON_INVALID || (INT)B>=(INT)VPAD_BUTTONS )
				{
					debugf( NAME_Log, TEXT("NSDLInputTest: virtual pad: bad button command") );
					return 0;
				}
				SDL_JoystickSetVirtualButton( Virtual, B, Value ? SDL_PRESSED : SDL_RELEASED );
			}
			else
			{
				SDL_GameControllerAxis A = SDL_GameControllerGetAxisFromString( TCHAR_TO_ANSI(*Name) );
				if( !Virtual || A==SDL_CONTROLLER_AXIS_INVALID )
				{
					debugf( NAME_Log, TEXT("NSDLInputTest: virtual pad: bad axis command") );
					return 0;
				}
				// SDL maps a trigger's whole raw range onto the controller's 0..32767.
				if( A==SDL_CONTROLLER_AXIS_TRIGGERLEFT || A==SDL_CONTROLLER_AXIS_TRIGGERRIGHT )
					Value = appRound( -32768.f + Clamp( Value, 0, 32767 ) * (65535.f / 32767.f) );
				SDL_JoystickSetVirtualAxis( Virtual, A, (Sint16)Clamp( Value, -32768, 32767 ) );
			}
			return 1;
		}
		debugf( NAME_Log, TEXT("NSDLInputTest: virtual pad: unknown command") );
		return 0;
	}
	else if( ParseCommand(&Cmd,TEXT("MOUSE")) )
	{
		FString SX, SY;
		ParseToken( Cmd, SX, 0 );
		ParseToken( Cmd, SY, 0 );
		const INT X = appAtoi( *SX ), Y = appAtoi( *SY );
		SDL_Event Ev;
		SDL_zero( Ev );
		Ev.type             = SDL_MOUSEMOTION;
		Ev.motion.timestamp = SDL_GetTicks();
		Ev.motion.windowID  = Viewport->hWnd ? SDL_GetWindowID( Viewport->hWnd ) : 0;
		Ev.motion.x         = X;
		Ev.motion.y         = Y;
		Ev.motion.xrel      = X - MouseX;
		Ev.motion.yrel      = Y - MouseY;
		MouseX = X;
		MouseY = Y;
		if( SDL_PushEvent( &Ev ) < 0 )
			debugf( NAME_Log, TEXT("NSDLInputTest: mouse: SDL_PushEvent failed: %s"), appFromAnsi(SDL_GetError()) );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("KEY")) )
	{
		FString Name;
		ParseToken( Cmd, Name, 0 );
		const SDL_Scancode Code = SDL_GetScancodeFromName( TCHAR_TO_ANSI(*Name) );
		if( Code==SDL_SCANCODE_UNKNOWN )
		{
			debugf( NAME_Log, TEXT("NSDLInputTest: key: no SDL key named %s"), *Name );
			return 0;
		}
		SDL_Event Ev;
		SDL_zero( Ev );
		Ev.type                = appAtoi( Cmd ) ? SDL_KEYDOWN : SDL_KEYUP;
		Ev.key.timestamp       = SDL_GetTicks();
		Ev.key.windowID        = Viewport->hWnd ? SDL_GetWindowID( Viewport->hWnd ) : 0;
		Ev.key.state           = appAtoi( Cmd ) ? SDL_PRESSED : SDL_RELEASED;
		Ev.key.keysym.scancode = Code;
		Ev.key.keysym.sym      = SDL_GetKeyFromScancode( Code );
		if( SDL_PushEvent( &Ev ) < 0 )
			debugf( NAME_Log, TEXT("NSDLInputTest: key: SDL_PushEvent failed: %s"), appFromAnsi(SDL_GetError()) );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("EXEC")) )
	{
		Viewport->Exec( Cmd, *GLog );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("PROBE")) )
	{
		Probe( Viewport, Cmd );
		return 0;
	}
	else if( ParseCommand(&Cmd,TEXT("WATCH")) )
	{
		FString Ticks;
		ParseToken( Cmd, Ticks, 0 );
		ParseToken( Cmd, WatchTag, 0 );
		WatchLeft  = appAtoi( *Ticks );
		WatchIndex = 0;
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("LOG")) )
	{
		return 0;
	}
	debugf( NAME_Log, TEXT("NSDLInputTest: unknown command") );
	return 0;
	unguard;
}

//
// The view, the console's state and UWindow's cursor, in one line.
//
void FNSDLInputTest::Probe( UNSDLViewport* Viewport, const TCHAR* Tag )
{
	guard(FNSDLInputTest::Probe);
	APlayerPawn* Actor = Viewport->Actor;
	UObject* Console = (UObject*)Viewport->Console;
	FStateFrame* Frame = Console ? Console->GetStateFrame() : NULL;
	UObject* Root = GetObjectProp( Console, TEXT("Root") );
	SDL_GameController* Pad = Viewport->Client->GetController();
	debugf
	(
		NAME_Log,
		TEXT("NSDLInputTest: probe %s yaw=%i pitch=%i state=%s paused=%i winmouse=%i at=%.1f,%.1f uwin=%.1f,%.1f over=%s active=%s relative=%i oscursor=%i pad=%s"),
		Tag,
		Actor ? Actor->ViewRotation.Yaw : 0,
		Actor ? Actor->ViewRotation.Pitch : 0,
		Frame && Frame->StateNode ? Frame->StateNode->GetName() : TEXT("None"),
		Actor && Actor->Level && Actor->Level->Pauser.Len() ? 1 : 0,
		(INT)Viewport->bShowWindowsMouse,
		Viewport->WindowsMouseX, Viewport->WindowsMouseY,
		GetFloatProp( Console, TEXT("MouseX") ), GetFloatProp( Console, TEXT("MouseY") ),
		ClassNameOf( GetObjectProp( Root, TEXT("MouseWindow") ) ),
		ClassNameOf( GetObjectProp( Root, TEXT("ActiveWindow") ) ),
		(INT)SDL_GetRelativeMouseMode(),
		SDL_ShowCursor( SDL_QUERY ),
		Pad && SDL_GameControllerName( Pad ) ? appFromAnsi( SDL_GameControllerName( Pad ) ) : TEXT("None")
	);
	unguard;
}
