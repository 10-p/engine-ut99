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
	// The driver's ten config options (v400's IMPLEMENT_CLASS calls StaticConstructor once per class).
	new(GetClass(), "DefaultDisplay",    RF_Public)UIntProperty(CPP_PROPERTY(DefaultDisplay),     "Display",  CPF_Config );
	new(GetClass(), "StartupFullscreen", RF_Public)UBoolProperty(CPP_PROPERTY(StartupFullscreen), "Display",  CPF_Config );
	new(GetClass(), "UseJoystick",       RF_Public)UBoolProperty(CPP_PROPERTY(UseJoystick),       "Joystick", CPF_Config );
	new(GetClass(), "DeadZoneXYZ",       RF_Public)UFloatProperty(CPP_PROPERTY(DeadZoneXYZ),      "Joystick", CPF_Config );
	new(GetClass(), "DeadZoneRUV",       RF_Public)UFloatProperty(CPP_PROPERTY(DeadZoneRUV),      "Joystick", CPF_Config );
	new(GetClass(), "ScaleXYZ",          RF_Public)UFloatProperty(CPP_PROPERTY(ScaleXYZ),         "Joystick", CPF_Config );
	new(GetClass(), "ScaleRUV",          RF_Public)UFloatProperty(CPP_PROPERTY(ScaleRUV),         "Joystick", CPF_Config );
	new(GetClass(), "InvertY",           RF_Public)UBoolProperty(CPP_PROPERTY(InvertY),           "Joystick", CPF_Config );
	new(GetClass(), "InvertV",           RF_Public)UBoolProperty(CPP_PROPERTY(InvertV),           "Joystick", CPF_Config );
	new(GetClass(), "PadCursorSpeed",    RF_Public)UFloatProperty(CPP_PROPERTY(PadCursorSpeed),   "Joystick", CPF_Config );

	// Compiled defaults, for these options and the ones UClient registers without a value (UnCamMgr.cpp): with no
	// [NSDLDrv.NSDLClient] section a zero would mean no decals, a black picture and a 0x0 window. They match the
	// section in Config/System/web/Default.ini but for the stick scales; the ini overrides any of them.
	CaptureMouse        = 1;
	CurvedSurfaces      = 1;
	ScreenFlashes       = 1;
	NoLighting          = 0;
	Decals              = 1;
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

	// TextureDetail / SkinDetail index the `Details` enum (High, Medium, Low, Ultra Low).
	TextureLODSet[1]    = 0;   // TextureDetail = High
	TextureLODSet[2]    = 0;   // SkinDetail    = High

	// This driver's own ten. The stick scales are Unreal 1's: an ini without this section is a retail one, and
	// its User.ini's pad bindings (speed 2 and 5.9) turn at Unreal 1's rate with them (web: 200/500 with speed 1).
	DefaultDisplay      = 0;
	StartupFullscreen   = 0;
	UseJoystick         = 1;
	DeadZoneXYZ         = 0.1f;
	DeadZoneRUV         = 0.1f;
	ScaleXYZ            = 100.0f;
	ScaleRUV            = 100.0f;
	InvertY             = 0;
	InvertV             = 0;
	PadCursorSpeed      = 1.0f / 3.0f;	// a viewport's width in three seconds at full tilt

	unguard;
}

//
// UNSDLClient constructor.
//
UNSDLClient::UNSDLClient()
{
	guard(UNSDLClient::UWindowsClient);
	// Nothing configurable is assigned here: the constructor runs on an object whose config the ini has already
	// filled (StaticAllocateObject copies the class defaults), so an assignment would override the ini.
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

	// Log the resolved display options (read off this object by the renderer and by ADecal::execAttachDecal).
	debugf( NAME_Init, TEXT("NSDLClient: %ix%i, Brightness=%.2f, Decals=%i, CurvedSurfaces=%i, ScreenFlashes=%i, NoDynamicLights=%i, MinDesiredFrameRate=%.0f"),
		WindowedViewportX, WindowedViewportY, Brightness, (INT)Decals, (INT)CurvedSurfaces,
		(INT)ScreenFlashes, (INT)NoDynamicLights, MinDesiredFrameRate );

	Controller = NULL;

#if defined(__ANDROID__)
	// The system's Back is a key (SDL_SCANCODE_AC_BACK, the game's Escape) instead of closing the activity.
	// Touches stay mouse events (SDL's default): the menus are driven by tapping, and in play the on-screen
	// overlay takes the touches before SDL sees them.
	SDL_SetHint( SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1" );
#endif

	// The game-controller subsystem is always up; whether a pad is used is UseJoystick, which the viewport reads
	// every tick (UNSDLViewport::TickInput).
	if ( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER ) < 0 )
	{
		appErrorf( "SDL_Init failed: %s", SDL_GetError() );
		appExit();
	}

	atexit( SDL_Quit );

	// A pad already connected (on the web: one pressed on the launcher page). Later ones arrive through
	// SDL_CONTROLLERDEVICEADDED (UNSDLViewport::UpdateInput); a browser lists a pad only once a button is pressed.
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
	Game pads (one pad: the first connected).
-----------------------------------------------------------------------------*/

#ifdef __EMSCRIPTEN__
//
// The buttons pressed right now on the browser's first listed pad named Id (bit i = Gamepad.buttons[i].pressed,
// the W3C "standard" layout), 0 if none. A browser lists a pad only at its first press, and that press is already
// down in the snapshot SDL's Emscripten driver keeps as the pad's starting state (it reports changes only), so SDL
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
