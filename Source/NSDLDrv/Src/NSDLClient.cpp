#include <stdlib.h>
#include <ctype.h>

#include "NSDLDrv.h"
#ifdef __EMSCRIPTEN__
#include <emscripten/em_asm.h>
#endif
#include "UnRender.h"

IMPLEMENT_CLASS( UNSDLClient );

/*-----------------------------------------------------------------------------
	UNSDLClient implementation.
-----------------------------------------------------------------------------*/

//
// Static init.
//
void UNSDLClient::StaticConstructor()
{
	guard(UNSDLClient::StaticConstructor);
	// ufront 2.19: was InternalClassInitializer, the UE1 *v200* class hook — which this v400 engine NEVER
	// invokes (v400's IMPLEMENT_CLASS calls StaticConstructor). See NSDLViewport.cpp, where the identical
	// dead hook cost UT99 its keyboard, and NOpenGLESDrv, fixed the same way in this step. Consequence
	// here: NONE of the nine options below were registered, so [NSDLDrv.NSDLClient] in the ini was inert
	// and joystick / fullscreen / dead-zone settings silently did nothing. The class-name guard the v200
	// hook needed is gone with it — StaticConstructor already runs per concrete class.
	new(GetClass(), "DefaultDisplay",    RF_Public)UIntProperty(CPP_PROPERTY(DefaultDisplay),     "Display",  CPF_Config );
	new(GetClass(), "StartupFullscreen", RF_Public)UBoolProperty(CPP_PROPERTY(StartupFullscreen), "Display",  CPF_Config );
	new(GetClass(), "UseJoystick",       RF_Public)UBoolProperty(CPP_PROPERTY(UseJoystick),       "Joystick", CPF_Config );
	new(GetClass(), "DeadZoneXYZ",       RF_Public)UFloatProperty(CPP_PROPERTY(DeadZoneXYZ),      "Joystick", CPF_Config );
	new(GetClass(), "DeadZoneRUV",       RF_Public)UFloatProperty(CPP_PROPERTY(DeadZoneRUV),      "Joystick", CPF_Config );
	new(GetClass(), "ScaleXYZ",          RF_Public)UFloatProperty(CPP_PROPERTY(ScaleXYZ),         "Joystick", CPF_Config );
	new(GetClass(), "ScaleRUV",          RF_Public)UFloatProperty(CPP_PROPERTY(ScaleRUV),         "Joystick", CPF_Config );
	new(GetClass(), "InvertY",           RF_Public)UBoolProperty(CPP_PROPERTY(InvertY),           "Joystick", CPF_Config );
	new(GetClass(), "InvertV",           RF_Public)UBoolProperty(CPP_PROPERTY(InvertV),           "Joystick", CPF_Config );

	// ★ COMPILED DEFAULTS (ufront 2.35). Registration above only makes an option ADDRESSABLE by the ini;
	// it assigns no value. UClient::StaticConstructor (Engine/Src/UnCamMgr.cpp) is the same — it registers
	// its ~15 display options and assigns none of them — so with no [NSDLDrv.NSDLClient] section EVERY
	// setting below fell back to a zero-initialised default, and the zeroes are wrong:
	//
	//   Decals = 0            -> ADecal::execAttachDecal (UnScript.cpp) early-returns on
	//                            `!Engine->Client->Decals`, so no decal is ever placed. Silently.
	//   Brightness = 0        -> a black picture.
	//   WindowedViewportX/Y=0 -> a 0x0 window (measured: 320x240 under Xvfb during the 2.34 Windows pass).
	//   CurvedSurfaces = 0    -> flat-shaded curved geometry.
	//   MinDesiredFrameRate=0 -> the dynamic-detail governor has no target to hold.
	//
	// ⚠ THE NATIVE CONFIG NEVER SUPPLIED THEM. Config/System/web/Default.ini carries a full section, so the
	// WEB build has always been fine; Config/System/UnrealTournament.ini and Default.ini carry NONE, which
	// is why a native install writes `Decals=False` back into its own ini and then honours it forever. That
	// is the "no decals" the owner kept hitting. Fixed in both places: the values here, and a matching
	// section added to the native ini so the knob is documented rather than merely implied.
	//
	// Values mirror Config/System/web/Default.ini's section exactly, so the compiled default and the shipped
	// ini now AGREE. Every line stays overridable by ini, like any other config option.
	CaptureMouse        = 1;
	CurvedSurfaces      = 1;
	ScreenFlashes       = 1;
	NoLighting          = 0;
	Decals              = 1;   // ← the one the owner asked for; off-by-default was never a decision
	NoDynamicLights     = 0;

	WindowedViewportX   = 1280;
	WindowedViewportY   = 720;
	WindowedColorBits   = 32;
	FullscreenViewportX = 1280;
	FullscreenViewportY = 720;
	FullscreenColorBits = 32;

	Brightness          = 0.5f;
	MipFactor           = 1.0f;
	MinDesiredFrameRate = 20.0f;

	// TextureDetail / SkinDetail index the `Details` enum (High, Medium, Low, Ultra Low), so 0 is already
	// High. Written out so it is a decision rather than a coincidence.
	TextureLODSet[1]    = 0;   // TextureDetail = High
	TextureLODSet[2]    = 0;   // SkinDetail    = High

	// This driver's own nine, likewise unassigned until now.
	DefaultDisplay      = 0;
	StartupFullscreen   = 0;
	UseJoystick         = 1;
	DeadZoneXYZ         = 0.1f;
	DeadZoneRUV         = 0.1f;
	ScaleXYZ            = 200.0f;
	ScaleRUV            = 500.0f;
	InvertY             = 0;
	InvertV             = 0;

	unguard;
}

//
// UNSDLClient constructor.
//
UNSDLClient::UNSDLClient()
{
	guard(UNSDLClient::UWindowsClient);
	// ufront 2.91: nothing configurable is assigned here. This v400 engine runs the C++ constructor on an object
	// whose config the ini has ALREADY filled (StaticAllocateObject copies the class defaults, which LoadConfig set),
	// so the assignments this held (UseJoystick=true, ScaleXYZ/RUV=100, the dead zones, DefaultDisplay) overwrote the
	// ini every time: `UseJoystick=False` connected the pad anyway (measured 2026-10-14) and the Scale keys never
	// applied. The compiled defaults live in StaticConstructor, where the ini can override them.
	Controller = NULL;
	unguard;
}

//
// Initialize the platform-specific viewport manager subsystem.
// Must be called after the Unreal object manager has been initialized.
// Must be called before any viewports are created.
//
void UNSDLClient::Init( UEngine* InEngine )
{
	guard(UNSDLClient::Init);

	// Init base.
	UClient::Init( InEngine );

	// ★ SAY WHAT THE CLIENT ACTUALLY RESOLVED TO (ufront 2.35). These options are read off this object by
	// the renderer and by ADecal::execAttachDecal, and until now the resolved values were invisible -- so
	// "why are there no decals" had no answer short of reading the ini and knowing which of the four client
	// sections this build even uses. Same lesson as 2.30's "GL4ES: Vertex buffers:" line: a silent decision
	// is indistinguishable from a broken one. One line, at Init, naming the values that matter.
	debugf( NAME_Init, TEXT("NSDLClient: %ix%i, Brightness=%.2f, Decals=%i, CurvedSurfaces=%i, ScreenFlashes=%i, NoDynamicLights=%i, MinDesiredFrameRate=%.0f"),
		WindowedViewportX, WindowedViewportY, Brightness, (INT)Decals, (INT)CurvedSurfaces,
		(INT)ScreenFlashes, (INT)NoDynamicLights, MinDesiredFrameRate );

	Controller = NULL;

	// ufront 2.91: the game-controller subsystem is always up; whether a pad is USED is UseJoystick, which the
	// viewport reads every tick (UNSDLViewport::TickInput), so the game's own option takes effect at once.
	if ( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER ) < 0 )
	{
		appErrorf( "SDL_Init failed: %s", SDL_GetError() );
		appExit();
	}

	atexit( SDL_Quit );

	// A pad already connected (on the web: one pressed on the launcher page). Later ones arrive through
	// SDL_CONTROLLERDEVICEADDED (UNSDLViewport::UpdateInput): there was no hot-plug before, so on the web, where a
	// browser lists a pad only once a button is pressed on it, a pad practically never worked.
	SDL_GameControllerEventState( SDL_ENABLE );
	if( UseJoystick )
	{
		for( INT i=0; i<SDL_NumJoysticks() && !Controller; i++ )
			OpenPad( i, NULL );
	}
	else debugf( NAME_Init, TEXT("NSDLClient: UseJoystick=False: no game pads") );

	// Not calling SDL_StartTextInput because that pops up on-screen keyboards sometimes.
	SDL_EventState( SDL_TEXTINPUT, SDL_ENABLE );

	SDL_GetDesktopDisplayMode( DefaultDisplay, &DefaultDisplayMode );

	unguard;
}

//
// Shut down the platform-specific viewport manager subsystem.
//
void UNSDLClient::Destroy()
{
	guard(UNSDLClient::Destroy);

	Controller = NULL;

	SDL_QuitSubSystem( SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER );

	UClient::Destroy();

	unguard;
}

//
// Failsafe routine to shut down viewport manager subsystem
// after an error has occured. Not guarded.
//
void UNSDLClient::ShutdownAfterError()
{
	debugf( NAME_Exit, "Executing UNSDLClient::ShutdownAfterError" );

	if( Engine && Engine->Audio )
	{
		Engine->Audio->ConditionalShutdownAfterError();
	}

	for( INT i=Viewports.Num()-1; i>=0; i-- )
	{
		UNSDLViewport* Viewport = (UNSDLViewport*)Viewports(i);
	}

	Super::ShutdownAfterError();
}

//
// Enable or disable all viewport windows that have ShowFlags set (or all if ShowFlags=0).
//
void UNSDLClient::EnableViewportWindows( DWORD ShowFlags, int DoEnable )
{
	guard(UNSDLClient::EnableViewportWindows);
	unguard;
}

//
// Show or hide all viewport windows that have ShowFlags set (or all if ShowFlags=0).
//
void UNSDLClient::ShowViewportWindows( DWORD ShowFlags, int DoShow )
{
	guard(UNSDLClient::ShowViewportWindows);
	unguard;
}

//
// Configuration change.
//
void UNSDLClient::PostEditChange()
{
	guard(UNSDLClient::PostEditChange);
	Super::PostEditChange();
	unguard;
}

// Perform background processing.  Should be called 100 times
// per second or more for best results.
//
void UNSDLClient::Poll()
{
	guard(UNSDLClient::Poll);
	unguard;
}

//
// Perform timer-tick processing on all visible viewports.  This causes
// all realtime viewports, and all non-realtime viewports which have been
// updated, to be blitted.
//
void UNSDLClient::Tick()
{
	guard(UNSDLClient::Tick);

	// Process input and blit any viewports that need blitting.
	UNSDLViewport* BestViewport = NULL;
	for( INT i=0; i<Viewports.Num(); i++ )
	{
		UNSDLViewport* Viewport = CastChecked<UNSDLViewport>(Viewports(i));
		if( !Viewport->GetWindow() )
		{
			// Window was closed via close button.
			delete Viewport;
			return;
		}
		else if (	Viewport->IsRealtime()
			&&	Viewport->SizeX && Viewport->SizeY && Viewport->HoldCount == 0
			&&	(!BestViewport || Viewport->LastUpdateTime<BestViewport->LastUpdateTime) )
		{
			BestViewport = Viewport;
		}
		// Tick input for this viewport and see if it wants to die.
		if( Viewport->TickInput() )
		{
			delete Viewport;
			return;
		}
	}

	if( BestViewport )
		BestViewport->Repaint( 1 );

	unguard;
}

//
// Make this viewport the current one.
// If Viewport=0, makes no viewport the current one.
//
void UNSDLClient::MakeCurrent( UViewport* InViewport )
{
	guard(UNSDLClient::MakeCurrent);
	for( INT i=0; i<Viewports.Num(); i++ )
	{
		UViewport* OldViewport = Viewports(i);
		if( OldViewport->Current && OldViewport!=InViewport )
		{
			OldViewport->Current = 0;
			OldViewport->UpdateWindowFrame();
		}
	}
	if( InViewport )
	{
		InViewport->Current = 1;
		InViewport->UpdateWindowFrame();
	}
	unguard;
}

//
// Create a new viewport.
//
UViewport* UNSDLClient::NewViewport( const FName Name )
{
	guard(UNSDLClient::NewViewport);
	return new( this, Name )UNSDLViewport( NULL, this );
	unguard;
}

//
// Return the current viewport.  Returns NULL if no viewport has focus.
//
UViewport* UNSDLClient::CurrentViewport()
{
	guard(UNSDLClient::CurrentViewport);

	// Note: FullscreenViewport tracking removed - Unreal 1 specific
	SDL_Window *MouseWin = SDL_GetMouseFocus();
	UNSDLViewport* TestViewport = NULL;
	for( int i=0; i<Viewports.Num(); i++ )
	{
		TestViewport = (UNSDLViewport*)Viewports(i);
		if( TestViewport->Current || (SDL_Window*)TestViewport->GetWindow() == MouseWin )
			return TestViewport;
	}

	return NULL;

	unguard;
}

//
// Assemble display mode array.
//
static INT Compare( const SDL_Rect& A, const SDL_Rect& B )
{
	if( A.w == B.w )
		return A.h - B.h;
	return A.w - B.w;
}
static UBOOL operator==( const SDL_Rect& A, const SDL_Rect& B )
{
	return A.w == B.w && A.h == B.h;
}
const TArray<SDL_Rect>& UNSDLClient::GetDisplayResolutions()
{
	guard(UNSDLClient::GetDisplayResolutions)

	DisplayResolutions.Empty();

	SDL_DisplayMode Mode;
	SDL_Rect Rect = { 0, 0, 0, 0 };
	if( SDL_GetDesktopDisplayMode( DefaultDisplay, &Mode ) == 0 )
	{
		// we're only interested in size
		Rect.w = Mode.w;
		Rect.h = Mode.h;
		DisplayResolutions.AddItem( Rect );
	}

	const INT NumModes = SDL_GetNumDisplayModes( DefaultDisplay );
	
	for( INT i=0; i<NumModes; ++i )
	{
		if( SDL_GetDisplayMode( DefaultDisplay, i, &Mode ) == 0 )
		{
			// we're only interested in size
			Rect.w = Mode.w;
			Rect.h = Mode.h;
			DisplayResolutions.AddUniqueItem( Rect );
		}
	}

	// Sort display resolutions
	if( DisplayResolutions.Num() > 0 )
		appQsort( &DisplayResolutions(0), DisplayResolutions.Num(), sizeof(SDL_Rect), (int(CDECL *)(const void*, const void*))Compare );

	return DisplayResolutions;

	unguard;
}

//
// Command line.
//
UBOOL UNSDLClient::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(UNSDLClient::Exec);

	if( ParseCommand( &Cmd, TEXT("EndFullscreen") ) )
	{
		EndFullscreen();
		return 1;
	}
	else if( ParseCommand( &Cmd, TEXT("GetRes") ) )
	{
		FString Result;
		GetDisplayResolutions();
		for( INT i=0; i<DisplayResolutions.Num(); ++i )
		{
			const SDL_Rect& Mode = DisplayResolutions(i);
			if( i > 0 )
				Result += TEXT(" ");
			Result += FString::Printf( TEXT("%ix%i"), Mode.w, Mode.h );
		}
		Ar.Log( *Result );
		return 1;
	}
	else if( UClient::Exec( Cmd, Ar ) )
	{
		return 1;
	}

	return 0;

	unguard;
}

void UNSDLClient::EndFullscreen()
{
	// End fullscreen for all viewports
	for( INT i=0; i<Viewports.Num(); i++ )
	{
		UNSDLViewport* Viewport = Cast<UNSDLViewport>(Viewports(i));
		if( Viewport && Viewport->IsFullscreen() )
		{
			Viewport->EndFullscreen();
		}
	}
}

//
// Try switching to a new rendering device.
//
void UNSDLClient::TryRenderDevice( UViewport* Viewport, const char* ClassName, UBOOL Fullscreen )
{
	guard(UNSDLClient::TryRenderDevice);

	UNSDLViewport* SDLViewport = Cast<UNSDLViewport>(Viewport);
	if( !SDLViewport )
		return;

	// Shut down current rendering device.
	if( Viewport->RenDev )
	{
		Viewport->RenDev->Exit();
		delete Viewport->RenDev;
		Viewport->RenDev = NULL;
	}

	// Find device driver.
	FString ClassNameStr = appFromAnsi(ClassName);
	UClass* RenderClass = UObject::StaticLoadClass( URenderDevice::StaticClass(), NULL, *ClassNameStr, NULL, LOAD_NoWarn, NULL );
	if( RenderClass )
	{
		Viewport->RenDev = ConstructObject<URenderDevice>( RenderClass );
		// (On web the device's inherited CPF_Config feature flags -- Coronas, VolumetricLighting,
		// ShinySurfaces, DetailTextures, HighDetailActors -- are loaded centrally in
		// UObject::StaticConstructObject, which repairs the value-init ClassConstructor wipe; see UnObj.cpp.)
		if( Engine->Audio && !GIsEditor )
			Engine->Audio->SetViewport( NULL );
		if( Viewport->RenDev->Init( Viewport, Viewport->SizeX, Viewport->SizeY, Viewport->ColorBytes, Fullscreen ) )
		{
			Viewport->Actor->GetLevel()->DetailChange( Viewport->RenDev->HighDetailActors );
			if( Fullscreen )
				SDLViewport->MakeFullscreen( FullscreenViewportX, FullscreenViewportY, 1 );
		}
		else
		{
			debugf( NAME_Log, LocalizeError("Failed3D") );
			delete Viewport->RenDev;
			Viewport->RenDev = NULL;
		}
		if( Engine->Audio && !GIsEditor )
			Engine->Audio->SetViewport( Viewport );
	}

	unguard;
}

/*-----------------------------------------------------------------------------
	ufront: game pads (from the UC2 port's SDL2Drv/Src/SDL2Pads.cpp, one pad here).
-----------------------------------------------------------------------------*/

#ifdef __EMSCRIPTEN__
//
// H-3: the buttons pressed right now on the browser's first listed pad named Id (bit i = Gamepad.buttons[i].pressed,
// the W3C "standard" layout), 0 if none. A browser lists a pad only at its first press, and that press is already
// down in the snapshot SDL's Emscripten driver keeps as the pad's starting state (it reports CHANGES only), so SDL
// never reports it: read it once here. Runs on the page's main thread (the engine is on a worker).
// (In the EM_ASM block: no top-level commas and no // comments.)
//
static DWORD WebPadPressed( const char* Id )
{
	return (DWORD)MAIN_THREAD_EM_ASM_INT({
		if (typeof navigator === 'undefined' || !navigator.getGamepads) return 0;
		var id = UTF8ToString($0);
		var pads = navigator.getGamepads();
		for (var i = 0; i < pads.length; i++) {
			var g = pads[i];
			if (!g || g.id !== id || !g.buttons) continue;
			var m = 0;
			for (var b = 0; b < g.buttons.length && b < 31; b++)
				if (g.buttons[b] && g.buttons[b].pressed) m = m | (1 << b);
			return m;
		}
		return 0;
	}, Id );
}
#endif

UBOOL UNSDLClient::OpenPad( INT DeviceIndex, DWORD* Held )
{
	guard(UNSDLClient::OpenPad);
	if( Held )
		*Held = 0;
	if( !UseJoystick || Controller || !SDL_IsGameController( DeviceIndex ) )
		return 0;
	Controller = SDL_GameControllerOpen( DeviceIndex );
	if( !Controller )
	{
		debugf( NAME_Init, TEXT("NSDLClient: game pad %i could not be opened: %s"), DeviceIndex, ANSI_TO_TCHAR(SDL_GetError()) );
		return 0;
	}
	const char* Name = SDL_GameControllerName( Controller );
	debugf( NAME_Init, TEXT("NSDLClient: game pad connected: %s"), Name ? ANSI_TO_TCHAR(Name) : TEXT("(no name)") );
#ifdef __EMSCRIPTEN__
	// SDL names the joystick by the browser's Gamepad.id.
	const char* Id = SDL_JoystickName( SDL_GameControllerGetJoystick( Controller ) );
	if( Held && Id )
		*Held = WebPadPressed( Id );
#endif
	return 1;
	unguard;
}

UBOOL UNSDLClient::ClosePad( INT InstanceId )
{
	guard(UNSDLClient::ClosePad);
	if( !Controller || SDL_JoystickInstanceID( SDL_GameControllerGetJoystick( Controller ) ) != InstanceId )
		return 0;
	SDL_GameControllerClose( Controller );
	Controller = NULL;
	debugf( NAME_Init, TEXT("NSDLClient: game pad disconnected") );
	return 1;
	unguard;
}
