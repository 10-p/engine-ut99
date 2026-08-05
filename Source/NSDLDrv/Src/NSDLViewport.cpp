#include <string.h>
#include <ctype.h>

#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
// ufront 2.13.2 — the worker creates and presents its own WebGL context (hatch (c)); see OpenWindow.
#include <stdint.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>
#endif

#include "NSDLDrv.h"
#include "UnRender.h"

IMPLEMENT_CLASS( UNSDLViewport );

/*-----------------------------------------------------------------------------
	UNSDLViewport implementation.
-----------------------------------------------------------------------------*/

//
// SDL_BUTTON_ -> EInputKey translation map.
//
const BYTE UNSDLViewport::MouseButtonMap[6] =
{
	/* invalid           */ IK_None,
	/* SDL_BUTTON_LEFT   */ IK_LeftMouse,
	/* SDL_BUTTON_MIDDLE */ IK_MiddleMouse,
	/* SDL_BUTTON_RIGHT  */ IK_RightMouse,
	/* SDL_BUTTON_X1     */ IK_None,
	/* SDL_BUTTON_X2     */ IK_None
};

//
// SDL_CONTROLLER_BUTTON_ -> EInputKey translation map.
//
const BYTE UNSDLViewport::JoyButtonMap[SDL_CONTROLLER_BUTTON_MAX] =
{
	/* BUTTON_A             */ IK_Joy1,
	/* BUTTON_B             */ IK_Joy2,
	/* BUTTON_X             */ IK_Joy3,
	/* BUTTON_Y             */ IK_Joy4,
	/* BUTTON_BACK          */ IK_Joy5,
	/* BUTTON_GUIDE         */ IK_Joy6,
	/* BUTTON_START         */ IK_Joy7,
	/* BUTTON_LEFTSTICK     */ IK_Joy8,
	/* BUTTON_RIGHTSTICK    */ IK_Joy9,
	/* BUTTON_LEFTSHOULDER  */ IK_Joy10,
	/* BUTTON_RIGHTSHOULDER */ IK_Joy11,
	/* BUTTON_DPAD_UP       */ IK_JoyPovUp,
	/* BUTTON_DPAD_DOWN     */ IK_JoyPovDown,
	/* BUTTON_DPAD_LEFT     */ IK_JoyPovLeft,
	/* BUTTON_DPAD_RIGHT    */ IK_JoyPovRight,
};

//
// SDL_CONTROLLER_BUTTON_ -> EInputKey translation map for UI controls.
//
const BYTE UNSDLViewport::JoyButtonMapUI[SDL_CONTROLLER_BUTTON_MAX] =
{
	/* BUTTON_A             */ IK_Enter,
	/* BUTTON_B             */ IK_Escape,
	/* BUTTON_X             */ IK_N,
	/* BUTTON_Y             */ IK_Y,
	/* BUTTON_BACK          */ IK_Escape,
	/* BUTTON_GUIDE         */ IK_Escape,
	/* BUTTON_START         */ IK_Escape,
	/* BUTTON_LEFTSTICK     */ IK_Joy8,
	/* BUTTON_RIGHTSTICK    */ IK_Joy9,
	/* BUTTON_LEFTSHOULDER  */ IK_Joy10,
	/* BUTTON_RIGHTSHOULDER */ IK_Joy11,
	/* BUTTON_DPAD_UP       */ IK_Up,
	/* BUTTON_DPAD_DOWN     */ IK_Down,
	/* BUTTON_DPAD_LEFT     */ IK_Left,
	/* BUTTON_DPAD_RIGHT    */ IK_Right,
};

//
// SDL_CONTROLLER_BUTTON_ -> EInputKey translation map.
//
const BYTE UNSDLViewport::JoyAxisMap[SDL_CONTROLLER_AXIS_MAX] =
{
	/* AXIS_LEFT_X          */ IK_JoyX,
	/* AXIS_LEFT_Y          */ IK_JoyY,
	/* AXIS_RIGHT_X         */ IK_JoyU,
	/* AXIS_RIGHT_Y         */ IK_JoyV,
	/* AXIS_LTRIGGER        */ IK_Joy12,
	/* AXIS_RTRIGGER        */ IK_Joy13,
};

//
// Additional scale to apply per SDL axis.
//
const FLOAT UNSDLViewport::JoyAxisDefaultScale[SDL_CONTROLLER_AXIS_MAX] =
{
	/* AXIS_LEFT_X          */ +60.f,
	/* AXIS_LEFT_Y          */ -60.f,
	/* AXIS_RIGHT_X         */ +60.f,
	/* AXIS_RIGHT_Y         */ +60.f,
	/* AXIS_LTRIGGER        */ +60.f,
	/* AXIS_RTRIGGER        */ +60.f,
};

//
// SDL_Scancode -> EInputKey translation map.
//
BYTE UNSDLViewport::KeyMap[512];
void UNSDLViewport::InitKeyMap()
{
	#define INIT_KEY_RANGE( AStart, AEnd, BStart, BEnd ) \
		for( DWORD Key = AStart; Key <= AEnd; ++Key ) KeyMap[Key] = BStart + ( Key - AStart )

	appMemset( KeyMap, 0, sizeof( KeyMap ) );

	// TODO: IK_LControl, IK_LShift, etc exist, what are they for?
	KeyMap[SDL_SCANCODE_LSHIFT] = IK_Shift;
	KeyMap[SDL_SCANCODE_RSHIFT] = IK_Shift;
	KeyMap[SDL_SCANCODE_LCTRL] = IK_Ctrl;
	KeyMap[SDL_SCANCODE_RCTRL] = IK_Ctrl;
	KeyMap[SDL_SCANCODE_LALT] = IK_Alt;
	KeyMap[SDL_SCANCODE_RALT] = IK_Alt;
	KeyMap[SDL_SCANCODE_GRAVE] = IK_Tilde;
	KeyMap[SDL_SCANCODE_ESCAPE] = IK_Escape;
	KeyMap[SDL_SCANCODE_SPACE] = IK_Space;
	KeyMap[SDL_SCANCODE_RETURN] = IK_Enter;
	KeyMap[SDL_SCANCODE_BACKSPACE] = IK_Backspace;
	KeyMap[SDL_SCANCODE_CAPSLOCK] = IK_CapsLock;
	KeyMap[SDL_SCANCODE_TAB] = IK_Tab;
	KeyMap[SDL_SCANCODE_DELETE] = IK_Delete;
	KeyMap[SDL_SCANCODE_INSERT] = IK_Insert;
	KeyMap[SDL_SCANCODE_HOME] = IK_Home;
	KeyMap[SDL_SCANCODE_END] = IK_End;
	KeyMap[SDL_SCANCODE_PAGEUP] = IK_PageUp;
	KeyMap[SDL_SCANCODE_PAGEDOWN] = IK_PageDown;
	KeyMap[SDL_SCANCODE_PRINTSCREEN] = IK_PrintScrn;
	KeyMap[SDL_SCANCODE_EQUALS] = IK_Equals;
	KeyMap[SDL_SCANCODE_SEMICOLON] = IK_Semicolon;
	KeyMap[SDL_SCANCODE_BACKSLASH] = IK_Backslash;
	KeyMap[SDL_SCANCODE_SLASH] = IK_Slash;
	KeyMap[SDL_SCANCODE_LEFTBRACKET] = IK_LeftBracket;
	KeyMap[SDL_SCANCODE_RIGHTBRACKET] = IK_RightBracket;
	KeyMap[SDL_SCANCODE_COMMA] = IK_Comma;
	KeyMap[SDL_SCANCODE_PERIOD] = IK_Period;
	KeyMap[SDL_SCANCODE_LEFT] = IK_Left;
	KeyMap[SDL_SCANCODE_UP] = IK_Up;
	KeyMap[SDL_SCANCODE_RIGHT] = IK_Right;
	KeyMap[SDL_SCANCODE_DOWN] = IK_Down;
	KeyMap[SDL_SCANCODE_0] = IK_0;
	KeyMap[SDL_SCANCODE_KP_0] = IK_NumPad0;
	KeyMap[SDL_SCANCODE_KP_PERIOD] = IK_NumPadPeriod;

	INIT_KEY_RANGE( SDL_SCANCODE_1,    SDL_SCANCODE_9,    IK_1,       IK_9 );
	INIT_KEY_RANGE( SDL_SCANCODE_A,    SDL_SCANCODE_Z,    IK_A,       IK_Z );
	INIT_KEY_RANGE( SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_9, IK_NumPad1, IK_NumPad9 );
	INIT_KEY_RANGE( SDL_SCANCODE_F1,   SDL_SCANCODE_F12,  IK_F1,      IK_F12 );
	INIT_KEY_RANGE( SDL_SCANCODE_F13,  SDL_SCANCODE_F24,  IK_F13,     IK_F24 );

	#undef INIT_KEY_RANGE
}

//
// Static init.
//
void UNSDLViewport::InternalClassInitializer( UClass* Class )
{
	guard(UNSDLViewport::InternalClassInitializer);
	// Fill in keymap.
	InitKeyMap();
	unguard;
}

//
// Constructor.
//
UNSDLViewport::UNSDLViewport( ULevel* InLevel, UNSDLClient* InClient )
:	UViewport()
,	Client( InClient )
{
	guard(UNSDLViewport::UNSDLViewport);

#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
	SoftFB = NULL;
	SoftFBBytes = 0;
#endif

	// ufront 2.12 (web+native keyboard): populate the scancode->EInputKey KeyMap here.
	// KeyMap is a runtime-filled static (unlike the const-initialized MouseButtonMap), and its only
	// filler, InitKeyMap(), was wired to UNSDLViewport::InternalClassInitializer -- a UE1 *v200* class
	// hook that this v400 engine NEVER invokes (v400's IMPLEMENT_CLASS calls StaticConstructor instead;
	// see Core/Inc/UnObjBas.h). So InitKeyMap() never ran, KeyMap stayed all-zero, every SDL_KEYDOWN
	// mapped to 0 and CauseInputEvent() early-returned -> keyboard was dead on BOTH web and native since
	// the DC-derived port began, while the const MouseButtonMap kept the mouse working. Calling it from
	// the ctor (idempotent memset+fill of a static array) guarantees it runs before any TickInput. The
	// dead InternalClassInitializer is left in place; converting it (and UNSDLClient's) to a proper v400
	// StaticConstructor is a separate cleanup. (engine-unreal1 is v200, so its hook fires -> it was fine.)
	InitKeyMap();

	// Set color bytes based on screen resolution.
	SDL_DisplayMode Mode;
	SDL_GetDesktopDisplayMode( InClient->DefaultDisplay, &Mode );
	ColorBytes = SDL_BYTESPERPIXEL( Mode.format );
	Caps = 0;
	if( ColorBytes == 2 && SDL_PIXELLAYOUT( Mode.format ) == SDL_PACKEDLAYOUT_565 )
	{
		Caps |= CC_RGB565;
	}

	// Inherit default display until we have a window.
	DisplayIndex = InClient->DefaultDisplay;
	DisplaySize.w = InClient->GetDefaultDisplayMode().w;
	DisplaySize.h = InClient->GetDefaultDisplayMode().h;

	// Init input.
	if( GIsEditor )
		Input->Init( this );

	Destroyed = false;
	QuitRequested = false;
	HoldCount = 0;

	unguard;
}

// UObject interface.
void UNSDLViewport::Destroy()
{
	guard(UNSDLViewport::Destroy);
	// Note: FullscreenViewport tracking removed - Unreal 1 specific
	UViewport::Destroy();
	unguard;
}

//
// Set the mouse cursor according to Unreal or UnrealEd's mode, or to
// an hourglass if a slow task is active. Not implemented.
//
void UNSDLViewport::SetModeCursor()
{
	guard(UNSDLViewport::SetModeCursor);
	unguard;
}

//
// Update user viewport interface.
//
void UNSDLViewport::UpdateWindowFrame()
{
	guard(UNSDLViewport::UpdateWindowFrame);

	// If not a window, exit.
	if( hWnd==NULL || HoldCount > 0 )
		return;

	// Set viewport window's name to show resolution.
	char WindowName[80];
	if( !GIsEditor || (Actor->ShowFlags&SHOW_PlayerCtrl) )
	{
		appSprintf( WindowName, LocalizeGeneral("Product","Core") );
	}
	else switch( Actor->RendMap )
	{
		case REN_Wire:		strcpy(WindowName,LocalizeGeneral("ViewPersp")); break;
		case REN_OrthXY:	strcpy(WindowName,LocalizeGeneral("ViewXY")); break;
		case REN_OrthXZ:	strcpy(WindowName,LocalizeGeneral("ViewXZ")); break;
		case REN_OrthYZ:	strcpy(WindowName,LocalizeGeneral("ViewYZ")); break;
		default:			strcpy(WindowName,LocalizeGeneral("ViewOther")); break;
	}

	// Set window title.
	if( SizeX && SizeY )
	{
		appSprintf(WindowName+strlen(WindowName)," (%i x %i)",SizeX,SizeY);
		if( this == Client->CurrentViewport() )
			strcat( WindowName, " *" );
	}
	SDL_SetWindowTitle( hWnd, WindowName );

	unguard;
}

//
// Open a viewport window.
//
void UNSDLViewport::OpenWindow( DWORD InParentWindow, UBOOL Temporary, INT NewX, INT NewY, INT OpenX, INT OpenY )
{
	guard(UNSDLViewport::OpenWindow);
	check(Actor);
	check(HoldCount == 0);
	UBOOL DoRepaint=0, DoSetActive=0;
	UBOOL DoOpenGL=0;
	UBOOL NoHard=ParseParam( appCmdLine(), "nohard" );
	SDL_GLprofile GLProfile = SDL_GL_CONTEXT_PROFILE_COMPATIBILITY;
	// ufront 2.9: on Emscripten the GL context version is chosen by the selected render-device NAME —
	// XOpenGLDrv targets WebGL2/GLES3 (major 3); every other device (NOpenGLESDrv, OpenGLDrv/UTGLR via
	// gl4es) targets WebGL1/GLES2 (major 2). Set from the resolved device name below; used in the
	// __EMSCRIPTEN__ context block. One viewport hosts all devices — no second NSDL package needed.
	INT WebGLESMajor = 2;

	// Clamp invalid sizes – SDL textures can't be 0x0
	// If no size specified, try to read from config
	if( NewX <= 0 || NewY <= 0 )
	{
		if( !Temporary && !GIsEditor )
		{
			// Try to get windowed viewport size from config
			INT ConfigX = 0, ConfigY = 0;
			if( GConfig->GetInt( TEXT("NSDLDrv.NSDLClient"), TEXT("WindowedViewportX"), ConfigX ) && ConfigX > 0 )
				NewX = ConfigX;
			if( GConfig->GetInt( TEXT("NSDLDrv.NSDLClient"), TEXT("WindowedViewportY"), ConfigY ) && ConfigY > 0 )
				NewY = ConfigY;
		}
		// Final fallback
		if( NewX <= 0 ) NewX = 320;
		if( NewY <= 0 ) NewY = 240;
	}
	NewX = Align(NewX,4);
	debugf( NAME_Log, TEXT("OpenWindow: NewX=%d, NewY=%d"), NewX, NewY );

	if( !Temporary && !GIsEditor && !NoHard )
	{
		// HACK: Just check if we're about to load OpenGLDrv. Not sure how else you would know to add the GL flag.
		FString Temp;
		Parse( appCmdLine(), TEXT("GAMERENDERDEVICE="), Temp );
		if( Temp.Len() == 0 )
			GConfig->GetString( TEXT("Engine.Engine"), TEXT("GameRenderDevice"), Temp );
		appStrupr( (TCHAR*)*Temp );
		if( Temp.InStr(TEXT("OPENGL")) == INDEX_NONE )
		{
			Parse( appCmdLine(), TEXT("WINDOWEDRENDERDEVICE="), Temp );
			if( Temp.Len() == 0 )
				GConfig->GetString( TEXT("Engine.Engine"), TEXT("WindowedRenderDevice"), Temp );
			appStrupr( (TCHAR*)*Temp );
			if( Temp.InStr(TEXT("OPENGL")) != INDEX_NONE )
				DoOpenGL = 1;
		}
		else
		{
			DoOpenGL = 1;
		}
		if( DoOpenGL && Temp.InStr(TEXT("GLES")) != INDEX_NONE )
			GLProfile = SDL_GL_CONTEXT_PROFILE_COMPATIBILITY;
		// ufront 2.9: XOpenGLDrv wants a WebGL2/GLES3 context (its name has no "GLES", so the compat line
		// above skips it). Every other OPENGL device stays WebGL1/GLES2 (major 2).
		if( DoOpenGL && Temp.InStr(TEXT("XOPENGL")) != INDEX_NONE )
			WebGLESMajor = 3;
	}

	// User window of launcher if no parent window was specified.
	if( !InParentWindow )
	{
		QWORD ParentPtr;
		Parse( appCmdLine(), TEXT("HWND="), ParentPtr );
		InParentWindow = (DWORD)ParentPtr;
	}

	if( Temporary )
	{
		// Create in-memory data.
		ColorBytes = 2;
		ScreenPointer = (BYTE*)appMalloc( 2 * NewX * NewY, "TemporaryViewportData" );	
		hWnd = NULL;
		debugf( NAME_Log, "Opened temporary viewport" );
	}
	else
	{
		// Get flags.
		DWORD Flags = 0;
		if( InParentWindow && (Actor->ShowFlags & SHOW_ChildWindow) )
		{
			Flags = SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS;
		}
		else
		{
			// ufront 2.12 (dynamic resolution): resizable so the OS window / browser canvas can be resized
			// and the viewport follows. The WINDOWEVENT_RESIZED handler in TickInput() syncs the engine
			// viewport + framebuffer; on the web SDL2's Emscripten backend maps the window to the CSS
			// 100vw/100vh canvas and emits those events automatically. Mirrors engine-ut2004 SDL2Drv.
			Flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE;
		}
		if( DoOpenGL )
		{
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
			// ufront 2.13.2 hatch (c): do NOT ask SDL for a GL window. SDL_WINDOW_OPENGL makes SDL bring
			// up its EGL path, Emscripten proxies EGL to the browser main thread, and the canvas has been
			// transferred to this worker — so the main thread's getContext() throws InvalidStateError
			// (Emscripten #24792). We create the WebGL context ourselves further down; SDL is here for the
			// window and input only, which need no GL flag.
#else
			Flags |= SDL_WINDOW_OPENGL;
#endif
		}

		// Set OpenGL attributes if needed.
		if( DoOpenGL )
		{
#ifdef __EMSCRIPTEN__
			// ufront 2.7c: WebGL is GLES2 — request an ES 2.0 context. ut99dc's default GLProfile for the
			// "NOpenGLES" device is COMPATIBILITY (desktop-GL compat), which emscripten's WebGL rejects
			// ("Could not create EGL context (context attributes are not supported)"). Force ES 2.0 here.
			SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, WebGLESMajor );
			SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 0 );
			SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES );
#else
			if( GLProfile == SDL_GL_CONTEXT_PROFILE_ES )
			{
				// Request GLES2.
				SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 2 );
				SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 0 );
			}
			SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, GLProfile );
#endif
		}

		// Set position and size.
		if( OpenX==-1 )
			OpenX = SDL_WINDOWPOS_UNDEFINED;
		if( OpenY==-1 )
			OpenY = SDL_WINDOWPOS_UNDEFINED;

		// If switching renderers, destroy the old window.
		if( hWnd && ( DoOpenGL != !!( SDL_GetWindowFlags( hWnd ) & SDL_WINDOW_OPENGL ) ) )
		{
			CloseWindow();
		}

		// Create or update the window.
		if( !hWnd )
		{
			// Creating new viewport.
			hWnd = SDL_CreateWindow( "", OpenX, OpenY, NewX, NewY, Flags );
			if( !hWnd && DoOpenGL )
			{
				// Try without GL.
				debugf( NAME_Warning, "Could not create OpenGL window: %s. Trying without OpenGL.", SDL_GetError() );
				Flags &= ~SDL_WINDOW_OPENGL;
				DoOpenGL = 0;
				hWnd = SDL_CreateWindow( "", OpenX, OpenY, NewX, NewY, Flags );
			}
			if( !hWnd )
			{
				appErrorf( "Could not create SDL window: %s", SDL_GetError() );
			}

			// Set parent window.
			if( InParentWindow && (Actor->ShowFlags & SHOW_ChildWindow) )
			{
				SDL_SetWindowModalFor( hWnd, (SDL_Window*)InParentWindow );
			}

			debugf( NAME_Log, "Opened viewport" );
			DoSetActive = DoRepaint = 1;
		}
		else
		{
			// Resizing existing viewport.
			SetClientSize( NewX, NewY, false );
		}

		// Create GL context or SDL renderer if needed.
		if( DoOpenGL )
		{
			if( !GLCtx )
			{
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
				// ufront 2.13.2 — hatch (c): create the WebGL context DIRECTLY, not through SDL2.
				//
				// SDL2's emscripten GL path is 100% EGL (SDL_EGL_CreateContext_impl), and Emscripten's EGL
				// emulation always proxies to the browser main thread. Under -sPROXY_TO_PTHREAD the canvas
				// has been transferred to this worker, so the main thread can no longer getContext() on it
				// and SDL_GL_CreateContext fails with InvalidStateError (Emscripten #24792). The 2.13.0
				// spike measured all three escape hatches; this is the one that works, and it renders on
				// the worker's OWN context — no per-GL-call proxying, so no per-call penalty.
				//
				// SDL2 still owns the window and ALL input; the only SDL call replaced is context creation.
				EmscriptenWebGLContextAttributes Attrs;
				emscripten_webgl_init_context_attributes( &Attrs );
				// WebGLESMajor is the GLES major SDL was asked for (2 = GLES2, 3 = GLES3 for XOpenGL);
				// emscripten counts WEBGL versions instead, and WebGL 1.0 == GLES2, WebGL 2.0 == GLES3.
				Attrs.majorVersion              = WebGLESMajor - 1;
				Attrs.minorVersion              = 0;
				Attrs.alpha                     = 0;
				Attrs.depth                     = 1;
				Attrs.stencil                   = 1;
				Attrs.antialias                 = 0;
				// explicitSwapControl + renderViaOffscreenBackBuffer are what make the frame OURS to
				// present: without them the browser composites at its own cadence and commit_frame() is
				// meaningless on a worker.
				Attrs.explicitSwapControl       = 1;
				Attrs.renderViaOffscreenBackBuffer = 1;
				EMSCRIPTEN_WEBGL_CONTEXT_HANDLE WebGLCtx = emscripten_webgl_create_context( "#canvas", &Attrs );
				if( WebGLCtx <= 0 )
				{
					appErrorf( "Could not create WebGL context on the worker (handle %i)", (INT)WebGLCtx );
				}
				if( emscripten_webgl_make_context_current( WebGLCtx ) != EMSCRIPTEN_RESULT_SUCCESS )
				{
					appErrorf( "Could not make the worker WebGL context current" );
				}
				// Stash it where the rest of the viewport already tests for "do we have GL". XOpenGL then
				// adopts it through emscripten_webgl_get_current_context(), exactly as it adopts the SDL
				// context on every other build.
				GLCtx = (SDL_GLContext)(intptr_t)WebGLCtx;
				debugf( NAME_Init, "Created WebGL %i.0 context on the worker (handle %i)", (INT)(WebGLESMajor-1), (INT)WebGLCtx );
#else
				GLCtx = SDL_GL_CreateContext( hWnd );
				if( !GLCtx )
				{
					appErrorf( "Could not create GL context: %s", SDL_GetError() );
				}
#endif
			}
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
			emscripten_webgl_make_context_current( (EMSCRIPTEN_WEBGL_CONTEXT_HANDLE)(intptr_t)GLCtx );
#else
			SDL_GL_MakeCurrent( hWnd, GLCtx );
#endif
		}
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
		else
		{
			// ufront 2.13.2: SDL's Emscripten renderer is GLES-backed, so SDL_CreateRenderer goes through
			// eglCreateContext, which Emscripten proxies to the browser main thread — where the canvas has
			// been transferred to this worker and getContext() throws. The proxied call never completes and
			// THE ENGINE THREAD BLOCKS FOREVER (measured on the pilot: the engine stopped logging right
			// after "Opened viewport" and stopped servicing even a posted `exit`).
			//
			// So SoftDrv renders into OUR framebuffer and Unlock blits it to the transferred
			// OffscreenCanvas through a 2D context. A null device never locks, so it pays nothing for this.
			SDLTexFormat = SDL_PIXELFORMAT_ARGB8888;
			ColorBytes   = SDL_BYTESPERPIXEL( SDLTexFormat );
			Caps         = 0;
			// ufront 2.19f: deliberately NO allocation here. This runs inside `if( !hWnd )`, i.e. only at
			// window creation, and from the ini resolution — which the adopt-drawable-size block below
			// then discards. Sizing it here bought a buffer for a resolution we were about to stop using.
			// Lock() sizes it instead, where SizeX/SizeY are both final and known to be about to be drawn.
		}
#else
		else
		{
			SDL_SetHint( SDL_HINT_RENDER_SCALE_QUALITY, "nearest" );
			SDLRen = SDL_CreateRenderer( hWnd, -1, 0 );
			if( !SDLRen )
			{
				// Fallback to software.
				debugf( NAME_Warning, "Could not create SDL renderer: %s. Trying software.", SDL_GetError() );
				SDLRen = SDL_CreateRenderer( hWnd, -1, SDL_RENDERER_SOFTWARE );
				if( !SDLRen )
				{
					appErrorf( "Could not create SDL renderer: %s", SDL_GetError() );
				}
			}
			// Create framebuffer texture.
			SDLTexFormat = SDL_PIXELFORMAT_ARGB8888;
			ColorBytes = SDL_BYTESPERPIXEL( SDLTexFormat );
			Caps = ( SDL_PIXELLAYOUT( SDLTexFormat ) == SDL_PACKEDLAYOUT_565 ) ? CC_RGB565 : 0;
			SDLTex = SDL_CreateTexture( SDLRen, SDLTexFormat, SDL_TEXTUREACCESS_STREAMING, NewX, NewY );
			if( !SDLTex )
			{
				appErrorf( "Could not create framebuffer texture: %s", SDL_GetError() );
			}
		}
#endif

		SDL_ShowWindow( hWnd );

		// Get this window's display parameters.
		SDL_DisplayMode DisplayMode;
		DisplayIndex = SDL_GetWindowDisplayIndex( hWnd );
		if( SDL_GetWindowDisplayMode( hWnd, &DisplayMode ) == 0 )
		{
			DisplaySize.w = DisplayMode.w;
			DisplaySize.h = DisplayMode.h;
		}

		// ufront 2.12 (dynamic resolution): adopt the real drawable size instead of the ini size. On the
		// web SDL2's Emscripten backend maps the window to the CSS-sized (100vw/100vh) canvas, so this is
		// the browser viewport; natively it is the created size. Match the drawing buffer (canvas
		// width/height) + software framebuffer to it; WINDOWEVENT_RESIZED (TickInput) keeps it in sync.
		int RealW = 0, RealH = 0;
		SDL_GetWindowSize( hWnd, &RealW, &RealH );
		if( RealW > 0 && RealH > 0 && ( Align( RealW, 4 ) != NewX || RealH != NewY ) )
		{
			NewX = Align( RealW, 4 );
			NewY = RealH;
			SDL_SetWindowSize( hWnd, NewX, NewY );
			if( SDLRen && SDLTex )
			{
				SDL_DestroyTexture( SDLTex );
				SDLTex = SDL_CreateTexture( SDLRen, SDLTexFormat, SDL_TEXTUREACCESS_STREAMING, NewX, NewY );
				if( !SDLTex )
					appErrorf( "Could not create framebuffer texture: %s", SDL_GetError() );
			}
		}
	}

	SizeX = NewX;
	SizeY = NewY;

	if( !RenDev && Temporary )
		Client->TryRenderDevice( this, "SoftDrv.SoftwareRenderDevice", 0 );
	if( !RenDev && !GIsEditor && !NoHard )
		Client->TryRenderDevice( this, "ini:Engine.Engine.GameRenderDevice", Client->StartupFullscreen );
	if( !RenDev )
		Client->TryRenderDevice( this, "ini:Engine.Engine.WindowedRenderDevice", 0 );
	check(RenDev);

	if( !Temporary )
		UpdateWindowFrame();
	if( DoRepaint )
		Repaint( 1 );

	unguard;
}

//
// Close a viewport window.  Assumes that the viewport has been opened with
// OpenViewportWindow.  Does not affect the viewport's object, only the
// platform-specific information associated with it.
//
void UNSDLViewport::CloseWindow()
{
	guard(UNSDLViewport::CloseWindow);

	if( hWnd )
	{
		if( SDLTex )
		{
			SDL_DestroyTexture( SDLTex );
			SDLTex = NULL;
		}
		if( SDLRen )
		{
			SDL_DestroyRenderer( SDLRen );
			SDLRen = NULL;
		}
		if( GLCtx )
		{
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
			// GLCtx is an Emscripten WebGL handle here, not an SDL context (hatch (c)) — SDL never made it
			// and must not be asked to destroy it.
			emscripten_webgl_destroy_context( (EMSCRIPTEN_WEBGL_CONTEXT_HANDLE)(intptr_t)GLCtx );
#else
			SDL_GL_DeleteContext( GLCtx );
#endif
			GLCtx = NULL;
		}
		SDL_DestroyWindow( hWnd );
		hWnd = NULL;
	}

	unguard;
}

//
// Lock the viewport window and set the approprite Screen and RealScreen fields
// of Viewport.  Returns 1 if locked successfully, 0 if failed.  Note that a
// lock failing is not a critical error; it's a sign that a DirectDraw mode
// has ended or the user has closed a viewport window.
//
UBOOL UNSDLViewport::Lock( FPlane FlashScale, FPlane FlashFog, FPlane ScreenClear, DWORD RenderLockFlags, BYTE* HitData, INT* HitSize )
{
	guard(UNSDLViewport::LockWindow);
	clock(Client->DrawCycles);

	// Make sure window is lockable.
	if( !hWnd )
	{
		return 0;
	}

	if( HoldCount > 0 || !SizeX || !SizeY )
	{
		appErrorf( "Failed locking viewport" );
		return 0;
	}

#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
	if( !GLCtx )
	{
		// No SDL texture to lock on a worker — the engine draws straight into our framebuffer.
		//
		// ufront 2.19f: size it HERE, lazily and exactly. This is the only point where the buffer is
		// about to be written and SizeX/SizeY are both final, so it cannot go stale: ResizeViewport
		// adopts the real canvas size only at window creation, and the WINDOWEVENT_RESIZED path changes
		// SizeX/SizeY without revisiting the creation block at all. Sizing at either of those sites left
		// SoftDrv rasterising SizeX*SizeY*ColorBytes bytes into a buffer sized for a resolution the
		// engine had already discarded — a heap overrun every frame, which is what crashed every
		// viewport above ~4 MB (see docs/migration/steps/2.19f).
		//
		// != rather than <, so it SHRINKS as well as grows: this is the single largest allocation the
		// software path makes (33 MB at 4K, 8 MB at 1080p), and a grow-only buffer would pin the
		// high-water mark of a session's largest window for the lifetime of the viewport. Lazy because
		// a device that never locks — the null renderer, and every GL device — must not pay for it at
		// all. Both matter on a 64 MB target.
		const INT Needed = SizeX * SizeY * ColorBytes;
		if( SoftFBBytes != Needed )
		{
			if( SoftFB )
				appFree( SoftFB );
			SoftFB      = (BYTE*)appMalloc( Needed, "ufrontSoftFB" );
			SoftFBBytes = Needed;
		}
		ScreenPointer = SoftFB;
		Stride = SizeX;
	}
#else
	if( SDLRen && SDLTex )
	{
		// Obtain pointer to screen.
		Stride = SizeX;
		ScreenPointer = NULL;
		SDL_LockTexture( SDLTex, NULL, (void **)&ScreenPointer, &Stride );
		Stride /= ColorBytes;
		check(ScreenPointer);
	}
#endif

	// Success.
	unclock(Client->DrawCycles);

	return UViewport::Lock( FlashScale, FlashFog, ScreenClear, RenderLockFlags, HitData, HitSize );

	unguard;
}

//
// Return whether fullscreen.
//
UBOOL UNSDLViewport::IsFullscreen()
{
	guard(UNSDLViewport::IsFullscreen);
	if( !hWnd )
		return 0;
	Uint32 Flags = SDL_GetWindowFlags( hWnd );
	return (Flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
	unguard;
}

//
// Resize the viewport.
//
UBOOL UNSDLViewport::ResizeViewport( DWORD NewBlitFlags, INT InNewX, INT InNewY, INT InNewColorBytes )
{
	guard(UNSDLViewport::ResizeViewport);

	// Remember viewport for audio (mimic X/Win behaviour).
	UViewport* SavedViewport = NULL;
	if( Client->Engine->Audio && !GIsEditor && !(GetFlags() & RF_Destroyed) )
		SavedViewport = Client->Engine->Audio->GetViewport();

	// Accept default parameters.
	INT NewX          = InNewX         == INDEX_NONE ? SizeX      : InNewX;
	INT NewY          = InNewY         == INDEX_NONE ? SizeY      : InNewY;
	INT NewColorBytes = InNewColorBytes== INDEX_NONE ? ColorBytes : InNewColorBytes;

	// Default resolution handling: use client defaults when not explicitly specified.
	if( InNewX == INDEX_NONE || InNewY == INDEX_NONE )
	{
		const SDL_DisplayMode& DefaultMode = Client->GetDefaultDisplayMode();
		if( NewBlitFlags & BLIT_Fullscreen )
		{
			// In fullscreen, prefer desktop resolution if nothing is specified.
			NewX = InNewX != INDEX_NONE ? InNewX : DefaultMode.w;
			NewY = InNewY != INDEX_NONE ? InNewY : DefaultMode.h;
		}
	}

	// Apply new size / mode.
	if( NewBlitFlags & BLIT_Fullscreen )
	{
		// Switching to fullscreen.
		MakeFullscreen( NewX, NewY, 1 );
	}
	else
	{
		// Windowed resize.
		SetClientSize( NewX, NewY, 1 );
		EndFullscreen();
	}

	// Update color depth if requested (render devices read this).
	ColorBytes = NewColorBytes;

	// Update audio.
	if( SavedViewport && SavedViewport!=Client->Engine->Audio->GetViewport() )
		Client->Engine->Audio->SetViewport( SavedViewport );

	// Update window frame.
	UpdateWindowFrame();

	return 1;

	unguard;
}

//
// Unlock the viewport window.  If Blit=1, blits the viewport's frame buffer.
//
void UNSDLViewport::Unlock( UBOOL Blit )
{
	guard(UNSDLViewport::Unlock);

	Client->DrawCycles=0;
	clock(Client->DrawCycles);

	// Unlock base.
	UViewport::Unlock( Blit );

	// Blit, if desired.
	if( Blit && hWnd && HoldCount == 0 )
	{
		if( GLCtx )
		{
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
			// The context was created with explicitSwapControl, so THIS is the present (2.13.2 hatch (c)).
			// SDL_GL_SwapWindow would go through the EGL emulation that cannot see our worker context.
			emscripten_webgl_commit_frame();
#else

			SDL_GL_SwapWindow( hWnd );
#endif
		}
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
		else if( SoftFB )
		{
			// Present the software framebuffer on the worker's own OffscreenCanvas. Emscripten sets
			// Module.canvas to the transferred canvas on the thread it was given to, and a 2D context is
			// available there because SoftDrv never creates a WebGL one (a canvas has only one context).
			// SDL_PIXELFORMAT_ARGB8888 is B,G,R,A in memory; ImageData wants R,G,B,A — hence the swizzle,
			// done 32 bits at a time rather than per channel.
			EM_ASM({
				var cv = Module['canvas'];
				if( !cv ) return;
				var ctx = Module.__ufrontSoftCtx;
				if( !ctx )
				{
					ctx = Module.__ufrontSoftCtx = cv.getContext('2d');
					if( !ctx ) return;
				}
				// One declaration per statement: EM_ASM is a macro and the preprocessor splits on
				// top-level commas — braces do not protect them, only parentheses do.
				var w = $1;
				var h = $2;
				var n = w * h;
				if( !Module.__ufrontSoftImg || Module.__ufrontSoftImg.width !== w || Module.__ufrontSoftImg.height !== h )
				{
					if( cv.width !== w ) cv.width = w;
					if( cv.height !== h ) cv.height = h;
					Module.__ufrontSoftImg = ctx.createImageData(w, h);
				}
				var src = new Uint32Array(HEAPU8.buffer, $0, n);
				var dst = new Uint32Array(Module.__ufrontSoftImg.data.buffer);
				for( var i = 0; i < n; i++ )
				{
					var v = src[i];
					dst[i] = (v & 0xff00ff00) | ((v & 0xff) << 16) | ((v >> 16) & 0xff) | 0xff000000;
				}
				ctx.putImageData(Module.__ufrontSoftImg, 0, 0);
			}, SoftFB, SizeX, SizeY);
		}
#else
		else if( SDLRen && SDLTex )
		{
			// Blitting with SDLRenderer.
			SDL_UnlockTexture( SDLTex );
			SDL_RenderCopy( SDLRen, SDLTex, NULL, NULL );
			SDL_RenderPresent( SDLRen );
		}
#endif
	}

	unclock(Client->DrawCycles);

	unguard;
}

//
// Make this viewport the current one.
// If Viewport=0, makes no viewport the current one.
//
void UNSDLViewport::MakeCurrent()
{
	guard(UNSDLViewport::MakeCurrent);
	Current = 1;
	for( INT i=0; i<Client->Viewports.Num(); i++ )
	{
		UViewport* OldViewport = Client->Viewports(i);
		if( OldViewport->Current && OldViewport != this )
		{
			OldViewport->Current = 0;
			OldViewport->UpdateWindowFrame();
		}
	}
	if( GLCtx )
	{
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
		emscripten_webgl_make_context_current( (EMSCRIPTEN_WEBGL_CONTEXT_HANDLE)(intptr_t)GLCtx );
#else
		SDL_GL_MakeCurrent( hWnd, GLCtx );
#endif
	}
	UpdateWindowFrame();
	unguard;
}

//
// Repaint the viewport.
//
void UNSDLViewport::Repaint( UBOOL Blit )
{
	guard(UNSDLViewport::Repaint);
	if( HoldCount == 0 && RenDev && SizeX && SizeY )
		Client->Engine->Draw( this, Blit );
	unguard;
}

//
// Set the client size (viewport view size) of a viewport.
//
void UNSDLViewport::SetClientSize( INT NewX, INT NewY, UBOOL UpdateProfile )
{
	guard(UNSDLViewport::SetClientSize);

	// Guard against zero/negative sizes.
	if( NewX <= 0 ) NewX = 320;
	if( NewY <= 0 ) NewY = 240;

	if( hWnd )
	{
		SDL_SetWindowSize( hWnd, NewX, NewY );
		// Resize output texture if required.
		if( SDLRen && SDLTex )
		{
			SDL_DestroyTexture( SDLTex );
			SDLTex = SDL_CreateTexture( SDLRen, SDLTexFormat, SDL_TEXTUREACCESS_STREAMING, NewX, NewY );
			if( !SDLTex )
			{
				appErrorf( "Could not create framebuffer texture: %s", SDL_GetError() );
			}
		}
	}

	SizeX = NewX;
	SizeY = NewY;

	// Optionally save this size in the profile.
	if( UpdateProfile )
	{
		Client->FullscreenViewportX = NewX;
		Client->FullscreenViewportY = NewY;
		Client->SaveConfig();
	}

	unguard;
}

//
// Return the viewport's window.
//
void* UNSDLViewport::GetWindow()
{
	return (void*)hWnd;
}

//
// Try to make this viewport fullscreen, matching the fullscreen
// mode of the nearest x-size to the current window. If already in
// fullscreen, returns to non-fullscreen.
//
void UNSDLViewport::MakeFullscreen( INT NewX, INT NewY, UBOOL UpdateProfile )
{
	guard(UNSDLViewport::MakeFullscreen);

	// If someone else is fullscreen, stop them.
	// Note: FullscreenViewport tracking removed - Unreal 1 specific
	Client->EndFullscreen();

	// Save this window.
	SavedX = SizeX;
	SavedY = SizeY;

	// Fullscreen rendering. For now no borderless.
	// Note: FullscreenViewport tracking removed - Unreal 1 specific
	SetClientSize( NewX, NewY, false );
	SDL_SetWindowFullscreen( hWnd, SDL_WINDOW_FULLSCREEN );

	if( UpdateProfile )
	{
		Client->FullscreenViewportX = NewX;
		Client->FullscreenViewportY = NewY;
		Client->SaveConfig();
	}

	unguard;
}

//
//
//
void UNSDLViewport::EndFullscreen()
{
	guard(UNSDLViewport::EndFullscreen);

	// ufront 2.12 (web resolution): only restore the saved size when actually LEAVING fullscreen with a
	// valid saved size. ResizeViewport()'s windowed branch calls EndFullscreen() unconditionally right after
	// a SetClientSize(NewX,NewY); if we were never fullscreen, SavedX/SavedY are 0 and SetClientSize(0,0)
	// hits its "<=0 -> 320x240" clamp -> it CLOBBERS the just-adopted CSS-canvas size back to 320x240. On the
	// web that 320x240 is the WebGL drawing buffer (canvas.width/height), so the game rendered at 320x240 and
	// the browser upscaled it -> blurry on every renderer; only a real browser resize re-synced it (masking
	// the cause). Guarding on the fullscreen flag makes EndFullscreen a no-op for a plain windowed resize.
	// (Native fullscreen toggle unaffected: MakeFullscreen sets the flag + a valid SavedX/SavedY first.)
	const UBOOL WasFullscreen = hWnd && ( SDL_GetWindowFlags( hWnd ) & SDL_WINDOW_FULLSCREEN );
	SDL_SetWindowFullscreen( hWnd, 0 );
	if( WasFullscreen && SavedX > 0 && SavedY > 0 )
		SetClientSize( SavedX, SavedY, false );

	unguard;
}

//
// Update input for viewport.
//
void UNSDLViewport::UpdateInput( UBOOL Reset )
{
	guard(UNSDLViewport::UpdateInput);

	if( Reset )
		appMemset( (void*)JoyAxis, 0, sizeof(JoyAxis) );

	unguard;
}

//
// If the cursor is currently being captured, stop capturing, clipping, and 
// hiding it, and move its position back to where it was when it was initially
// captured.
//
void UNSDLViewport::SetMouseCapture( UBOOL Capture, UBOOL Clip, UBOOL OnlyFocus )
{
	guard(UNSDLViewport::SetMouseCapture);

	// If only focus, reject.
#ifndef __EMSCRIPTEN__
	if( OnlyFocus )
		if( hWnd != SDL_GetMouseFocus() )
			return;
#else
	// ufront 2.12 (web mouse): skip the focus gate on the web. The canvas fills the window and SDL2's
	// Emscripten port defers the pointer lock to the next click anyway, so gating the menu->game capture
	// transition on SDL_GetMouseFocus() (which can be stale on the transition frame) would drop the lock
	// intent. UT2004's SetMouseCapture is likewise focus-agnostic. Mirrors engine-unreal1.
	(void)OnlyFocus;
#endif

	// If capturing, windows requires clipping in order to keep focus.
	Clip |= Capture;

	// Handle capturing.
	SDL_SetRelativeMouseMode( (SDL_bool)Capture );

	unguard;
}

UBOOL UNSDLViewport::CauseInputEvent( INT iKey, EInputAction Action, FLOAT Delta )
{
	guard(UWindowsViewport::CauseInputEvent);

	// Route to engine if a valid key
	if( iKey > 0 )
		return Client->Engine->InputEvent( this, (EInputKey)iKey, Action, Delta );
	else
		return 0;

	unguard;
}

UBOOL UNSDLViewport::TickInput()
{
	guard(UNSDLViewport::TickInput);

	SDL_Event Ev;
	INT Tmp;
	const FLOAT CurTime = appSeconds();
	const FLOAT DeltaTime = CurTime - InputUpdateTime;

	while( SDL_PollEvent( &Ev ) )
	{
		switch( Ev.type )
		{
			case SDL_QUIT:
				// signal to client and remember set a flag just in case
				QuitRequested = true;
				return true;
			case SDL_TEXTINPUT:
				for( const char *p = Ev.text.text; *p && p < Ev.text.text + sizeof( Ev.text.text ); ++p )
				{
					if( *p < 0 )
						break;
					if( isprint( *p ) || *p == '\r' )
						Client->Engine->Key( this, (EInputKey)*p );
				}
				break;
			case SDL_KEYDOWN:
			case SDL_KEYUP:
				CauseInputEvent( KeyMap[Ev.key.keysym.scancode], ( Ev.type == SDL_KEYDOWN ) ? IST_Press : IST_Release );
				break;
			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
				CauseInputEvent( MouseButtonMap[Ev.button.button], ( Ev.type == SDL_MOUSEBUTTONDOWN ) ? IST_Press : IST_Release );
				break;
			case SDL_MOUSEWHEEL:
				if( Ev.wheel.y )
				{
					CauseInputEvent( IK_MouseW, IST_Axis, Ev.wheel.y );
					if( Ev.wheel.y < 0 )
					{
						CauseInputEvent( IK_MouseWheelDown, IST_Press );
						CauseInputEvent( IK_MouseWheelDown, IST_Release );
					}
					else if( Ev.wheel.y > 0 )
					{
						CauseInputEvent( IK_MouseWheelUp, IST_Press );
						CauseInputEvent( IK_MouseWheelUp, IST_Release );
					}
				}
				break;
			case SDL_CONTROLLERBUTTONDOWN:
			case SDL_CONTROLLERBUTTONUP:
				{
					// HACK: Swap to alternate bindings when in menus, but not when waiting for keypress in the keybind menu.
					// Note: GetMainFrame() is Unreal 1 specific, disabled for UT99
					const UBOOL bIsInUI = 0; // Console && ((UObject*)Console)->GetMainFrame() && ...
					const BYTE* JoyMap = bIsInUI ? JoyButtonMapUI : JoyButtonMap;
					CauseInputEvent( JoyMap[Ev.cbutton.button], ( Ev.type == SDL_CONTROLLERBUTTONDOWN ) ? IST_Press : IST_Release );
				}
				break;
			case SDL_CONTROLLERAXISMOTION:
				{
					const BYTE Key = JoyAxisMap[Ev.caxis.axis];
					const INT PrevValue = JoyAxis[Ev.caxis.axis];
					INT NewValue = Ev.caxis.value;
					INT DeadZone = 0;
					if ( Key < IK_JoyX )
					{
						// Treat the axis like a trigger.
						if ( PrevValue < JoyAxisPressThreshold && NewValue >= JoyAxisPressThreshold )
							CauseInputEvent( Key, IST_Press );
						else if ( PrevValue >= JoyAxisPressThreshold && NewValue < JoyAxisPressThreshold )
							CauseInputEvent( Key, IST_Release );
					}
					else
					{
						// Apply deadzone.
						if ( Key >= IK_JoyX && Key <= IK_JoyZ )
							DeadZone = Client->DeadZoneXYZ * 32767.f;
						else if ( Key == IK_JoyR || Key == IK_JoyU || Key == IK_JoyV )
							DeadZone = Client->DeadZoneRUV * 32767.f;
						if ( Abs(NewValue) < DeadZone )
							NewValue = 0;
					}
					JoyAxis[Ev.caxis.axis] = NewValue;
				}
				break;
			case SDL_MOUSEMOTION:
				if( !SDL_GetRelativeMouseMode() )
				{
					// Absolute (menu / cursor-visible) mode. MousePosition feeds WindowsMouseX/Y (used by the
					// Dreamcast KOSViewport path, not the PC UWindow menu). ufront 2.12 (bug M): UT99's UWindow
					// menu moves its cursor from the IK_MouseX/IK_MouseY *axis* input (relayed to aMouseX/aMouseY),
					// which was only emitted in the relative branch below -- so in a menu (where the pointer-lock
					// Tick driver releases relative capture) the menu cursor stayed frozen at 0,0 and no button was
					// reachable, even though the correct absolute coords WERE arriving. Emit the axis deltas here
					// too (SDL provides xrel/yrel in absolute mode) so the menu cursor tracks the mouse. In-game is
					// always the relative branch (capture is armed whenever a level is up), so this only affects
					// menus -- no double-feed to the player pawn. Mirrors how the captured/relative case drives it.
					Client->Engine->MousePosition( this, 0, Ev.motion.x, Ev.motion.y );
					if( Ev.motion.xrel ) CauseInputEvent( IK_MouseX, IST_Axis, Ev.motion.xrel );
					if( Ev.motion.yrel ) CauseInputEvent( IK_MouseY, IST_Axis, -Ev.motion.yrel );
				}
				else
				{
					DWORD ViewportButtonFlags = 0;
					if( Ev.motion.state & SDL_BUTTON_LMASK ) ViewportButtonFlags |= MOUSE_Left;
					if( Ev.motion.state & SDL_BUTTON_RMASK ) ViewportButtonFlags |= MOUSE_Right;
					if( Ev.motion.state & SDL_BUTTON_MMASK ) ViewportButtonFlags |= MOUSE_Middle;
					if( Ev.motion.xrel || Ev.motion.yrel )
					{
						Client->Engine->MouseDelta( this, ViewportButtonFlags, Ev.motion.xrel, -Ev.motion.yrel );
						if( Ev.motion.xrel ) CauseInputEvent( IK_MouseX, IST_Axis, Ev.motion.xrel );
						if( Ev.motion.yrel ) CauseInputEvent( IK_MouseY, IST_Axis, -Ev.motion.yrel );
					}
				}
				break;
			case SDL_WINDOWEVENT:
				// ufront 2.12 (dynamic resolution): follow OS window / browser canvas resizes. SDL2's
				// Emscripten backend fires this when the CSS 100vw/100vh canvas changes size (and once
				// after creation, correcting the initial size); native window managers fire it on user
				// resize. SetClientSize re-syncs the SDL window + software framebuffer + SizeX/SizeY; the
				// GL render device re-issues glViewport from Viewport->SizeX/SizeY on the next frame. The
				// no-op guard makes the SDL_SetWindowSize inside SetClientSize non-recursive. (engine-ut2004
				// SDL2Drv parity.)
				if( ( Ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || Ev.window.event == SDL_WINDOWEVENT_RESIZED )
					&& Ev.window.data1 > 0 && Ev.window.data2 > 0 )
				{
					const INT NewX = Align( Ev.window.data1, 4 ), NewY = Ev.window.data2;
					if( NewX != SizeX || NewY != SizeY )
						SetClientSize( NewX, NewY, false );
				}
				break;
			default:
				break;
		}
	}

	// Constantly hammer the input system with axis events for axes that are not zero.
	for ( INT i = 0; i < SDL_CONTROLLER_AXIS_MAX; ++i )
	{
		const BYTE Key = JoyAxisMap[i];
		const SWORD Value = JoyAxis[i];
		if ( Value && Key && Key >= IK_JoyX )
		{
			const FLOAT FltValue = Clamp( Value / 32767.f, -1.f, 1.f );
			FLOAT Scale = ( Key >= IK_JoyX && Key <= IK_JoyZ ) ? Client->ScaleXYZ : Client->ScaleRUV;
			Scale *= JoyAxisDefaultScale[i] * DeltaTime;
			if ( ( Client->InvertV && Key == IK_JoyV ) || ( Client->InvertY && Key == IK_JoyY ) )
				Scale = -Scale;
			CauseInputEvent( Key, IST_Axis, FltValue * Scale );
		}
	}

	InputUpdateTime = CurTime;

	return QuitRequested;

	unguard;
}

/*-----------------------------------------------------------------------------
	Command line.
-----------------------------------------------------------------------------*/

UBOOL UNSDLViewport::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(UNSDLViewport::Exec);
	if( UViewport::Exec( Cmd, Ar ) )
	{
		return 1;
	}
	else if( ParseCommand(&Cmd, TEXT("ToggleFullscreen")) )
	{
		// Toggle fullscreen.
		// Note: FullscreenViewport tracking removed - Unreal 1 specific
		Client->EndFullscreen();
		if( !(Actor->ShowFlags & SHOW_ChildWindow) )
			Client->TryRenderDevice( this, "ini:Engine.Engine.GameRenderDevice", 1 );
		return 1;
	}
	else if( ParseCommand(&Cmd, TEXT("GetCurrentRes")) )
	{
		Ar.Logf( TEXT("%ix%i"), SizeX, SizeY );
		return 1;
	}
	else if( ParseCommand(&Cmd, TEXT("SetRes")) )
	{
		INT X=appAtoi(Cmd), Y=appAtoi(appStrchr(Cmd,TEXT('x')) ? appStrchr(Cmd,TEXT('x'))+1 : appStrchr(Cmd,TEXT('X')) ? appStrchr(Cmd,TEXT('X'))+1 : TEXT(""));
		if( X && Y )
		{
			// Note: FullscreenViewport tracking removed - Unreal 1 specific
			MakeFullscreen( X, Y, 1 );
		}
		return 1;
	}
	else if( ParseCommand(&Cmd, TEXT("Preferences")) )
	{
		// Note: FullscreenViewport tracking removed - Unreal 1 specific
		Client->EndFullscreen();
		return 1;
	}
	else return 0;
	unguard;
}
