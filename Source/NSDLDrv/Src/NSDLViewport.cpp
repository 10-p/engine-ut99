#include <string.h>
#include <ctype.h>

#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
// The worker creates and presents its own WebGL context; see OpenWindow.
#include <stdint.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>
#endif

#include "NSDLDrv.h"
#include "NSDLInputTest.h"
#include "UnRender.h"

#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
#include <math.h>
// The canvas's device-pixel size, so the backing store matches the panel (SDL2's Emscripten backend sizes it
// to the CSS size, which the browser then resamples). Passed to SDL_SetWindowSize, it makes SDL set the backing
// store to exactly that (CSS untouched) and rescale menu mouse coordinates into it. Both calls are sync-proxied
// to the main thread. Returns false when the box reads 0x0 (display:none), so the caller keeps SDL's numbers.
static UBOOL ufrontCanvasDevicePixelSize( INT& OutW, INT& OutH )
{
	double CssW = 0.0, CssH = 0.0;
	if( emscripten_get_element_css_size( "#canvas", &CssW, &CssH ) != EMSCRIPTEN_RESULT_SUCCESS )
		return 0;
	const double Dpr = emscripten_get_device_pixel_ratio();
	// lround, not floor: a 1000.8 css box at 1.25 is 1251 device pixels.
	const INT W = (INT)lround( CssW * Dpr ), H = (INT)lround( CssH * Dpr );
	if( W <= 0 || H <= 0 )
		return 0;
	OutW = W; OutH = H;
	return 1;
}
#endif

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
// SDL_CONTROLLER_BUTTON_ -> EInputKey translation map: the pad's Joy keys, which the bindings give their actions.
// While the pad has the menus' cursor, PadMenuKey stands the menus' keys in for some of them.
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
#if defined(__ANDROID__)
	// The system's Back key (UNSDLClient::Init traps it) is the game's Escape.
	KeyMap[SDL_SCANCODE_AC_BACK] = IK_Escape;
#endif
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

	// Fill the scancode -> EInputKey map (a static array; idempotent) before any TickInput.
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

	appMemzero( PadKeySent, sizeof(PadKeySent) );
	PadCursor = 0;
	PadCursorX = PadCursorY = 0.f;
	PadStickOut[0] = PadStickOut[1] = 0;
	InputTest = NULL;
	FString InputTestFile;
	if( Parse( appCmdLine(), TEXT("INPUTTEST="), InputTestFile ) )
		InputTest = new FNSDLInputTest( *InputTestFile );

	unguard;
}

// UObject interface.
void UNSDLViewport::Destroy()
{
	guard(UNSDLViewport::Destroy);
	// Note: FullscreenViewport tracking removed - Unreal 1 specific
	delete InputTest;
	InputTest = NULL;
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
void UNSDLViewport::OpenWindow( PTRINT InParentWindow, UBOOL Temporary, INT NewX, INT NewY, INT OpenX, INT OpenY )
{
	guard(UNSDLViewport::OpenWindow);
	check(Actor);
	check(HoldCount == 0);
	UBOOL DoRepaint=0, DoSetActive=0;
	UBOOL DoOpenGL=0;
	UBOOL NoHard=ParseParam( appCmdLine(), "nohard" );
	SDL_GLprofile GLProfile = SDL_GL_CONTEXT_PROFILE_COMPATIBILITY;
	// The web and Android context's GLES major (2 = WebGL 1 / GLES 2, 3 = WebGL 2 / GLES 3): on the web every GL
	// device gets WebGL 2; NOpenGLESDrv and OpenGLDrv/UTGLR take WebGL 1 with -WEBGL=1 or as a fallback,
	// XOpenGLDrv never. On Android XOpenGLDrv gets GLES 3 and the others GLES 2. Set from the device name below.
	INT WebGLESMajor = 2;
	UBOOL XOpenGLDevice = 0; // needs WebGL 2, so it never falls back to WebGL 1

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

			// Then the client object (its compiled default), before the 320x240 floor: a retail ini has no
			// [NSDLDrv.NSDLClient] section.
			if( Client )
			{
				if( NewX <= 0 && Client->WindowedViewportX > 0 ) NewX = Client->WindowedViewportX;
				if( NewY <= 0 && Client->WindowedViewportY > 0 ) NewY = Client->WindowedViewportY;
			}
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
		// XOpenGLDrv needs a GLES3 / WebGL 2 context.
		if( DoOpenGL && Temp.InStr(TEXT("XOPENGL")) != INDEX_NONE )
			WebGLESMajor = 3;
#ifdef __EMSCRIPTEN__
		// WebGL 2 for every GL device: NOpenGLESDrv's and gl4es's (OpenGLDrv/UTGLR) GLES2 calls run on it unchanged,
		// and their antialiasing needs it (msaa-backbuffer.js). -WEBGL=1 asks for WebGL 1 for those two, and a browser
		// without WebGL 2 gets it (below).
		INT AskWebGL = 2;
		XOpenGLDevice = DoOpenGL && Temp.InStr(TEXT("XOPENGL")) != INDEX_NONE;
		if( DoOpenGL )
			WebGLESMajor = ( !XOpenGLDevice && Parse( appCmdLine(), TEXT("WEBGL="), AskWebGL ) && AskWebGL == 1 ) ? 2 : 3;
#endif
	}

	// User window of launcher if no parent window was specified.
	if( !InParentWindow )
	{
		QWORD ParentPtr;
		Parse( appCmdLine(), TEXT("HWND="), ParentPtr );
		InParentWindow = (PTRINT)ParentPtr;
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
			// Resizable: the viewport follows the OS window or the browser canvas (the WINDOWEVENT_RESIZED handler
			// in TickInput()).
			Flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE;
		}
		if( DoOpenGL )
		{
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
			// No SDL GL window on a worker: SDL's EGL path is proxied to the main thread, which can no longer see
			// the transferred canvas (Emscripten #24792). The WebGL context is created further down; SDL keeps the
			// window and input.
#else
			Flags |= SDL_WINDOW_OPENGL;
#endif
		}
#if defined(__ANDROID__)
		// The whole screen, landscape either way up, with the system bars hidden: SDLActivity reads the
		// orientation hint when the window is created. The ini's StartupFullscreen stays off, so the viewport
		// keeps the absolute mouse path the menus use with touch (ResizeViewport).
		SDL_SetHint( SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight" );
		Flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif

		// Set OpenGL attributes if needed.
		if( DoOpenGL )
		{
#ifdef __EMSCRIPTEN__
			// WebGL takes an ES profile at WebGLESMajor (Emscripten rejects the COMPATIBILITY profile).
			SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, WebGLESMajor );
			SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 0 );
			SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES );
#elif defined(__ANDROID__)
			// Android has GLES only: GLES 3.0 for XOpenGLDrv, GLES 2.0 for NOpenGLESDrv (WebGLESMajor, from the
			// device name above), with a 24-bit depth buffer (SDL's default asks for 16).
			SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, WebGLESMajor );
			SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 0 );
			SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES );
			SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 24 );
			SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, 8 );
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
				// Create the WebGL context directly on the worker: SDL2's GL path is EGL, which Emscripten proxies to
				// the main thread, where the transferred canvas is out of reach (Emscripten #24792). The context is the
				// worker's own, so no GL call is proxied. SDL2 still owns the window and all input.
				EmscriptenWebGLContextAttributes Attrs;
				emscripten_webgl_init_context_attributes( &Attrs );
				// WebGLESMajor is a GLES major; Emscripten counts WebGL versions (WebGL 1.0 == GLES2, 2.0 == GLES3).
				Attrs.majorVersion              = WebGLESMajor - 1;
				Attrs.minorVersion              = 0;
				Attrs.alpha                     = 0;
				Attrs.depth                     = 1;
				Attrs.stencil                   = 1;
				Attrs.antialias                 = 0;
				// explicitSwapControl + renderViaOffscreenBackBuffer: the frame is presented by
				// emscripten_webgl_commit_frame() in Unlock.
				Attrs.explicitSwapControl       = 1;
				Attrs.renderViaOffscreenBackBuffer = 1;
				EMSCRIPTEN_WEBGL_CONTEXT_HANDLE WebGLCtx = emscripten_webgl_create_context( "#canvas", &Attrs );
				if( WebGLCtx <= 0 && Attrs.majorVersion == 2 && !XOpenGLDevice )
				{
					// A browser without WebGL 2 still runs NOpenGLESDrv and UTGLR, without antialiasing.
					debugf( NAME_Init, "No WebGL 2 context (handle %i): WebGL 1, without antialiasing", (INT)WebGLCtx );
					Attrs.majorVersion = 1;
					WebGLESMajor = 2;
					WebGLCtx = emscripten_webgl_create_context( "#canvas", &Attrs );
				}
				if( WebGLCtx <= 0 )
				{
					appErrorf( "Could not create WebGL context on the worker (handle %i)", (INT)WebGLCtx );
				}
				if( emscripten_webgl_make_context_current( WebGLCtx ) != EMSCRIPTEN_RESULT_SUCCESS )
				{
					appErrorf( "Could not make the worker WebGL context current" );
				}
				// Kept in GLCtx, which the rest of the viewport tests for GL. XOpenGL adopts it through
				// emscripten_webgl_get_current_context(), as it adopts the SDL context on other builds.
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
			// No SDL renderer on a worker: it is GLES-backed, and its proxied eglCreateContext would block the
			// engine thread for good. SoftDrv renders into SoftFB and Unlock blits it to the transferred
			// OffscreenCanvas through a 2D context; a device that never locks pays nothing.
			SDLTexFormat = SDL_PIXELFORMAT_ARGB8888;
			ColorBytes   = SDL_BYTESPERPIXEL( SDLTexFormat );
			Caps         = 0;
			// SoftFB is allocated in Lock(), where SizeX/SizeY are final.
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

		// Adopt the real drawable size instead of the ini size (on the web, the canvas); WINDOWEVENT_RESIZED
		// (TickInput) keeps it in sync.
		int RealW = 0, RealH = 0;
		SDL_GetWindowSize( hWnd, &RealW, &RealH );
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
		// The canvas's device-pixel size, unaligned: any other width is a fractional rescale in the browser.
		INT DevW = 0, DevH = 0;
		if( ufrontCanvasDevicePixelSize( DevW, DevH ) )
		{
			RealW = DevW;
			RealH = DevH;
		}
		const INT WantX = RealW;
#else
		const INT WantX = Align( RealW, 4 );
#endif
		if( RealW > 0 && RealH > 0 && ( WantX != NewX || RealH != NewY ) )
		{
			NewX = WantX;
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
			// GLCtx is an Emscripten WebGL handle here, not an SDL context.
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
		// No SDL texture to lock on a worker: the engine draws straight into SoftFB, sized here, where SizeX/SizeY
		// are final (a resize changes them without revisiting OpenWindow). It shrinks as well as grows, and a
		// device that never locks allocates nothing.
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

	// The absolute Windows-mouse path on windowed viewports, as the stock WinViewport sets it: UWindow's
	// WindowConsole.RenderUWindow then puts its cursor at WindowsMouseX/Y, which MousePosition fills from
	// absolute SDL motion. UGameEngine::Tick releases relative mode while a menu is up, so absolute motion
	// arrives there.
	bWindowsMouseAvailable = !(NewBlitFlags & BLIT_Fullscreen);

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
			// The context was created with explicitSwapControl, so this is the present (SDL_GL_SwapWindow
			// would go through the EGL emulation, which cannot see the worker's context).
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

#if defined(__ANDROID__)
	// The window is the screen already (OpenWindow): the surface's size stays, whatever the ini asks for.
	SDL_SetWindowFullscreen( hWnd, SDL_WINDOW_FULLSCREEN_DESKTOP );
	(void)NewX; (void)NewY;
#else
	// Fullscreen rendering. For now no borderless.
	// Note: FullscreenViewport tracking removed - Unreal 1 specific
	SetClientSize( NewX, NewY, false );
	SDL_SetWindowFullscreen( hWnd, SDL_WINDOW_FULLSCREEN );
#endif

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

	// Restore the saved size only when leaving fullscreen with a valid saved size: ResizeViewport()'s windowed
	// branch calls this right after SetClientSize(), and SetClientSize(0,0) would clamp the viewport to 320x240.
#if defined(__ANDROID__)
	// The window stays the screen.
#else
	const UBOOL WasFullscreen = hWnd && ( SDL_GetWindowFlags( hWnd ) & SDL_WINDOW_FULLSCREEN );
	SDL_SetWindowFullscreen( hWnd, 0 );
	if( WasFullscreen && SavedX > 0 && SavedY > 0 )
		SetClientSize( SavedX, SavedY, false );
#endif

	unguard;
}

//
// Update input for viewport.
//
void UNSDLViewport::UpdateInput( UBOOL Reset )
{
	guard(UNSDLViewport::UpdateInput);

	if( Reset )
	{
		appMemset( (void*)JoyAxis, 0, sizeof(JoyAxis) );
		MoveStick[0] = MoveStick[1] = 0;
	}

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
	// No focus gate on the web: the canvas fills the window, SDL's Emscripten port takes the lock on the next
	// click, and SDL_GetMouseFocus() can be stale on the menu -> game frame.
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

//
// One stick or trigger value (SDL's -32768..32767; a trigger 0..32767). A trigger is a button with a threshold, a
// stick has its dead zone applied and is sent every tick while it is off centre (TickInput).
//
void UNSDLViewport::UpdatePadAxis( INT Axis, INT NewValue )
{
	const BYTE Key = JoyAxisMap[Axis];
	const INT PrevValue = JoyAxis[Axis];
	INT DeadZone = 0;
	if( Axis == SDL_CONTROLLER_AXIS_LEFTX || Axis == SDL_CONTROLLER_AXIS_LEFTY )
		MoveStick[Axis - SDL_CONTROLLER_AXIS_LEFTX] = (SWORD)Clamp( NewValue, -32767, 32767 );
	if ( Key < IK_JoyX )
	{
		// Treat the axis like a trigger.
		if ( PrevValue < JoyAxisPressThreshold && NewValue >= JoyAxisPressThreshold )
			PadKey( Key, IST_Press );
		else if ( PrevValue >= JoyAxisPressThreshold && NewValue < JoyAxisPressThreshold )
			PadKey( Key, IST_Release );
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
	JoyAxis[Axis] = NewValue;
}

// The keys a pad's held-at-connect press gave the game. SDL never reports their release (it never saw them go
// down), so they are a tap: released at the next tick.
static BYTE GHeldTap[32];
static INT GHeldTapNum = 0;
#if defined(__ANDROID__)
// Mouse button events held back for later ticks, and the one-pixel move after a press (TickInput).
struct FAndroidClick { BYTE Key; BYTE Action; };
static FAndroidClick GAndroidClick[16];
static INT GAndroidClickNum = 0;
#endif
static INT GPadsOn = -1;	// UseJoystick as last seen (-1: not yet; the client's Init said it)

//
// Nothing stays held when the pad goes (removed, or UseJoystick off): every pad key is released and the axes the
// frame loop re-sends are zeroed.
//
void UNSDLViewport::ReleasePad()
{
	guard(UNSDLViewport::ReleasePad);
	for( INT k=0; k<IK_MAX; k++ )
		PadKey( k, IST_Release );
	for( INT a=0; a<SDL_CONTROLLER_AXIS_MAX; a++ )
		JoyAxis[a] = 0;
	MoveStick[0] = MoveStick[1] = 0;
	unguard;
}

//
// A pad key's press or release. While UWindow shows its cursor a press takes the cursor for the pad and raises the
// menus' key the pad key stands for there; anywhere else, and while the mouse has the cursor, the pad key is
// itself. A press already held is ignored, and a release sends the engine key the press sent.
//
void UNSDLViewport::PadKey( INT Key, EInputAction Action )
{
	guard(UNSDLViewport::PadKey);
	if( Key <= 0 || Key >= IK_MAX )
		return;
	INT Sent = PadKeySent[Key];
	if( Action == IST_Press )
	{
		if( Sent )
			return;
		const UBOOL HadCursor = PadCursor;
		if( bShowWindowsMouse )
			SetPadCursor( 1 );
		Sent = PadCursor ? PadMenuKey( Key, HadCursor ) : Key;
		PadKeySent[Key] = Sent;
		if( !Sent )
			return;
	}
	else
	{
		if( !Sent )
			return;
		PadKeySent[Key] = 0;
	}
	if( InputTest )
		debugf( NAME_Log, TEXT("NSDLInputTest: key %s %s"), Input->GetKeyName( (EInputKey)Sent ), Action == IST_Press ? TEXT("press") : TEXT("release") );
	CauseInputEvent( Sent, Action );
	// A press that brought UWindow up gives the cursor to the pad too.
	if( Action == IST_Press && bShowWindowsMouse )
		SetPadCursor( 1 );
	unguard;
}

/*-----------------------------------------------------------------------------
	The menu cursor on the pad.
-----------------------------------------------------------------------------*/

//
// The menus' keys on the pad while it has their cursor: Back and B go back, Start and A confirm (A clicks instead:
// under the cursor shown when that is on a control, else the focused control when that takes a click), the D-pad
// is the arrow keys (an arrow the focused control does not use moves the focus as Tab does: down and right to the
// next control, up and left to the previous one). A message box takes A, Start and the way back as
// MessageBoxAnswer says, an open drop-down list the others as ComboListKey says.
//
static const struct { BYTE PadKey; BYTE MenuKey; } GPadMenuKeys[] =
{
	{ IK_Joy5,        IK_Escape },	// Back
	{ IK_Joy7,        IK_Enter  },	// Start
	{ IK_Joy1,        IK_Enter  },	// A
	{ IK_Joy2,        IK_Escape },	// B
	{ IK_JoyPovUp,    IK_Up     },
	{ IK_JoyPovDown,  IK_Down   },
	{ IK_JoyPovLeft,  IK_Left   },
	{ IK_JoyPovRight, IK_Right  },
};

//
// The mouse buttons the menu cursor clicks with: a pad key bound to one of these commands clicks its button while
// UWindow shows its cursor.
//
static const struct { const TCHAR* Command; BYTE Button; } GPadCursorClicks[] =
{
	{ TEXT("Fire"),    IK_LeftMouse  },
	{ TEXT("AltFire"), IK_RightMouse },
};

//
// UWindow's windows are script objects of other packages, read here by property and class name.
//
static UObject* ObjectProp( UObject* Obj, const TCHAR* Name )
{
	UObjectProperty* P = Obj ? FindField<UObjectProperty>( Obj->GetClass(), Name ) : NULL;
	UObject* Value = NULL;
	if( P )
		appMemcpy( &Value, (BYTE*)Obj + P->Offset, sizeof(Value) );
	return Value;
}

static UBOOL BoolProp( UObject* Obj, const TCHAR* Name )
{
	UBoolProperty* P = Obj ? FindField<UBoolProperty>( Obj->GetClass(), Name ) : NULL;
	return P && ( *(DWORD*)((BYTE*)Obj + P->Offset) & P->BitMask );
}

static UBOOL IsAClassNamed( UObject* Obj, const TCHAR* Name )
{
	for( UClass* C = Obj ? Obj->GetClass() : NULL; C; C = C->GetSuperClass() )
		if( appStricmp( C->GetName(), Name ) == 0 )
			return 1;
	return 0;
}

static FLOAT* FloatProp( UObject* Obj, const TCHAR* Name )
{
	UFloatProperty* P = Obj ? FindField<UFloatProperty>( Obj->GetClass(), Name ) : NULL;
	return P ? (FLOAT*)((BYTE*)Obj + P->Offset) : NULL;
}

//
// A call of a script function of a UWindow window, its parameters set by name (the others zero, None or False).
//
struct FUWindowCall
{
	UObject* Obj;
	UFunction* Function;
	TArray<BYTE> Parms;
	FUWindowCall( UObject* InObj, const TCHAR* Name )
	:	Obj( InObj )
	,	Function( InObj ? InObj->FindFunction( FName( Name, FNAME_Find ) ) : NULL )
	{
		if( Function )
			Parms.AddZeroed( Function->ParmsSize );
	}
	void Set( const TCHAR* Name, const void* Value, INT Size )
	{
		UProperty* P = Function ? FindField<UProperty>( Function, Name ) : NULL;
		if( P && P->ElementSize >= Size )
			appMemcpy( &Parms(P->Offset), Value, Size );
	}
	UBOOL Call()
	{
		if( !Function )
			return 0;
		Obj->ProcessEvent( Function, Parms.Num() ? &Parms(0) : NULL );
		return 1;
	}
};

//
// Whether a UWindow window and every window it sits in are shown.
//
static UBOOL WindowShown( UObject* Window )
{
	for( INT Depth = 0; Window && Depth < 64; Depth++ )
	{
		if( !BoolProp( Window, TEXT("bWindowVisible") ) )
			return 0;
		Window = ObjectProp( Window, TEXT("ParentWindow") );
	}
	return 1;
}

//
// Whether the window under UWindow's cursor is a control a click acts on (labels are not).
//
static UBOOL ControlUnderCursor( UObject* Root )
{
	UObject* Window = ObjectProp( Root, TEXT("MouseWindow") );
	if( IsAClassNamed( Window, TEXT("UWindowDialogControl") ) )
		return !IsAClassNamed( Window, TEXT("UWindowLabelControl") );
	return IsAClassNamed( Window, TEXT("UWindowTabControlTabArea") ) || IsAClassNamed( Window, TEXT("UWindowGrid") )
		|| IsAClassNamed( Window, TEXT("UWindowVScrollBar") ) || IsAClassNamed( Window, TEXT("UWindowHScrollBar") );
}

//
// Whether the window with the keyboard focus acts on arrow key Key (its KeyDown uses it).
//
static UBOOL FocusUsesArrow( UObject* Root, INT Key )
{
	UObject* Focus = ObjectProp( Root, TEXT("KeyFocusWindow") );
	const UBOOL Across = Key == IK_Left || Key == IK_Right;
	if( IsAClassNamed( Focus, TEXT("UWindowPulldownMenu") ) )
		return 1;
	if( IsAClassNamed( Focus, TEXT("UWindowMenuBar") ) || IsAClassNamed( Focus, TEXT("UWindowHSliderControl") ) )
		return Across;
	if( IsAClassNamed( Focus, TEXT("UWindowGrid") ) )
		return !Across;
	if( IsAClassNamed( Focus, TEXT("UWindowEditBox") ) )
		return BoolProp( Focus, TEXT("bCanEdit") ) && ( Across || BoolProp( Focus, TEXT("bHistory") ) );
	return 0;
}

//
// Moves the keyboard focus to the next or the previous control of the focused control's tab ring that is shown and
// takes the focus (a label does not), as UWindowDialogControl's Tab does (ActivateWindow); a dialog with no focused
// control starts at its first control. Returns the control focused, NULL when there is none to move to.
//
static UObject* MoveFocus( UObject* Root, UBOOL Next )
{
	const TCHAR* Link = Next ? TEXT("TabNext") : TEXT("TabPrev");
	UObject* Focus = ObjectProp( Root, TEXT("KeyFocusWindow") );
	UObject* From = Focus;
	while( From && !ObjectProp( From, Link ) )
		From = ObjectProp( From, TEXT("ParentWindow") );
	UObject* To = NULL;
	if( From )
		To = ObjectProp( From, Link );
	else
		for( UObject* Client = Focus; Client && !To; Client = ObjectProp( Client, TEXT("ParentWindow") ) )
			if( UObject* Last = ObjectProp( Client, TEXT("TabLast") ) )
				To = Next ? ObjectProp( Last, TEXT("TabNext") ) : Last;
	// Past the controls hidden or without the focus: no further than the control it starts from, or once round.
	UObject* Start = To;
	for( INT i = 0; To && To != From && i < 256; i++ )
	{
		if( BoolProp( To, TEXT("bWindowVisible") ) && BoolProp( To, TEXT("bAcceptsFocus") ) )
			break;
		To = ObjectProp( To, Link );
		if( To == Start )
			To = NULL;
	}
	if( To == From )
		return NULL;
	FUWindowCall Activate( To, TEXT("ActivateWindow") );
	return Activate.Call() ? To : NULL;
}

//
// The keys of an open drop-down list (UWindowComboList, the focus while it is open), whose mouse-only items take
// the pad's here: Up and Down select the item before or after (scrolled into view), Enter picks the selected one (or
// closes the list when none is), Escape closes it. Returns whether MenuKey was one of them.
//
static UBOOL ComboListKey( UObject* List, INT MenuKey )
{
	UObject* Items = ObjectProp( List, TEXT("Items") );
	UObjectProperty* SelectedProp = FindField<UObjectProperty>( List->GetClass(), TEXT("Selected") );
	UObject* Selected = ObjectProp( List, TEXT("Selected") );
	if( MenuKey == IK_Up || MenuKey == IK_Down )
	{
		UObject* To = Selected ? ObjectProp( Selected, MenuKey == IK_Down ? TEXT("Next") : TEXT("Prev") )
			: ObjectProp( Items, TEXT("Next") );
		if( To && To != Items && SelectedProp )
		{
			appMemcpy( (BYTE*)List + SelectedProp->Offset, &To, sizeof(To) );
			// The selected row in view, as the scroll bar shows a row (UWindowVScrollBar.Show).
			FLOAT Row = 0.f;
			for( UObject* I = ObjectProp( Items, TEXT("Next") ); I && I != To; I = ObjectProp( I, TEXT("Next") ) )
				Row += 1.f;
			UObject* ScrollBar = ObjectProp( List, TEXT("VertSB") );
			if( BoolProp( ScrollBar, TEXT("bWindowVisible") ) )
			{
				FUWindowCall Show( ScrollBar, TEXT("Show") );
				Show.Set( TEXT("P"), &Row, sizeof(Row) );
				Show.Call();
			}
		}
		return 1;
	}
	if( MenuKey == IK_Enter && Selected )
	{
		FUWindowCall Execute( List, TEXT("ExecuteItem") );
		Execute.Set( TEXT("I"), &Selected, sizeof(Selected) );
		Execute.Call();
		return 1;
	}
	if( MenuKey == IK_Enter || MenuKey == IK_Escape )
	{
		FUWindowCall Close( List, TEXT("CloseUp") );
		Close.Call();
		return 1;
	}
	return 0;
}

//
// Whether A gives the focused control a click (a button presses, a checkbox toggles, a drop-down list opens) rather
// than Enter, which the controls that take it keep (pull-down menus, editable boxes, message boxes) and which the
// controls a click at the centre would set at random do too (lists, the menu bar, sliders; labels take neither).
//
static UBOOL FocusTakesClick( UObject* Focus )
{
	return IsAClassNamed( Focus, TEXT("UWindowDialogControl") )
		&& !IsAClassNamed( Focus, TEXT("UWindowListControl") )
		&& !IsAClassNamed( Focus, TEXT("UWindowLabelControl") )
		&& !IsAClassNamed( Focus, TEXT("UWindowHSliderControl") )
		&& !( IsAClassNamed( Focus, TEXT("UWindowEditBox") ) && BoolProp( Focus, TEXT("bCanEdit") ) );
}

//
// A left click at the centre of a window, as the mouse's would be there: UWindow's mouse down and up at that point,
// with the root's mouse position there meanwhile (the captures read it); the cursor stays where it is.
//
static UBOOL ClickWindow( UObject* Root, UObject* Window )
{
	FLOAT* RootX = FloatProp( Root, TEXT("MouseX") );
	FLOAT* RootY = FloatProp( Root, TEXT("MouseY") );
	FLOAT* Width = FloatProp( Window, TEXT("WinWidth") );
	FLOAT* Height = FloatProp( Window, TEXT("WinHeight") );
	if( !RootX || !RootY || !Width || !Height )
		return 0;
	FLOAT X = *Width / 2.f, Y = *Height / 2.f;
	for( UObject* W = Window; W && W != Root; W = ObjectProp( W, TEXT("ParentWindow") ) )
	{
		FLOAT* Left = FloatProp( W, TEXT("WinLeft") );
		FLOAT* Top = FloatProp( W, TEXT("WinTop") );
		X += Left ? *Left : 0.f;
		Y += Top ? *Top : 0.f;
	}
	const FLOAT SavedX = *RootX, SavedY = *RootY;
	*RootX = X;
	*RootY = Y;
	const INT LeftMouse = IK_LeftMouse;
	for( BYTE Message = 0; Message < 2; Message++ )	// WM_LMouseDown, WM_LMouseUp
	{
		FUWindowCall Event( Root, TEXT("WindowEvent") );
		Event.Set( TEXT("Msg"), &Message, sizeof(Message) );
		Event.Set( TEXT("X"), &X, sizeof(X) );
		Event.Set( TEXT("Y"), &Y, sizeof(Y) );
		Event.Set( TEXT("Key"), &LeftMouse, sizeof(LeftMouse) );
		Event.Call();
	}
	*RootX = SavedX;
	*RootY = SavedY;
	return 1;
}

//
// A click on the focused control when it takes one (FocusTakesClick). Returns the control, or NULL.
//
static UObject* ClickFocus( UObject* Root )
{
	UObject* Focus = ObjectProp( Root, TEXT("KeyFocusWindow") );
	return FocusTakesClick( Focus ) && ClickWindow( Root, Focus ) ? Focus : NULL;
}

//
// A message box on top (UWindowMessageBox, modal) answers the pad's A and Start with its affirmative button (Yes,
// else OK) and the pad's way back with its negative one (Cancel, else No; a box with OK alone keeps Escape's answer).
// Returns the button clicked, or NULL.
//
static UObject* MessageBoxAnswer( UObject* Root, UBOOL Affirmative )
{
	UObject* Box = ObjectProp( Root, TEXT("ActiveWindow") );
	if( !IsAClassNamed( Box, TEXT("UWindowMessageBox") ) )
		return NULL;
	UObject* Client = ObjectProp( Box, TEXT("ClientArea") );
	UObject* Button = Affirmative ? ObjectProp( Client, TEXT("YesButton") ) : ObjectProp( Client, TEXT("CancelButton") );
	if( !Button )
		Button = Affirmative ? ObjectProp( Client, TEXT("OKButton") ) : ObjectProp( Client, TEXT("NoButton") );
	return Button && ClickWindow( Root, Button ) ? Button : NULL;
}

//
// Whether a key-binding page of the menus is on screen and waits for a key (its bPolling): the pad's keys then
// reach it as themselves, so that they can be bound.
//
static UBOOL KeyBinderPolling()
{
	static const TCHAR* Binders[] = { TEXT("UMenuCustomizeClientWindow"), TEXT("SpeechBinderCW") };
	for( INT i = 0; i < ARRAY_COUNT(Binders); i++ )
	{
		UClass* Class = FindObject<UClass>( ANY_PACKAGE, Binders[i] );
		if( !Class )
			continue;
		for( TObjectIterator<UObject> It; It; ++It )
			if( It->IsA( Class ) && BoolProp( *It, TEXT("bPolling") ) && WindowShown( *It ) )
				return 1;
	}
	return 0;
}

//
// The key a pad key raises while the pad has the menus' cursor: the menus' key of GPadMenuKeys, else the mouse
// button of GPadCursorClicks its binding names, else itself; 0 when it clicked or moved the focus instead. A key-binding page
// waiting for a key gets the pad's own keys, but for Back, which closes it as the keyboard's Escape does.
// HadCursor: the cursor was the pad's, so shown, before this press.
//
INT UNSDLViewport::PadMenuKey( INT Key, UBOOL HadCursor )
{
	guard(UNSDLViewport::PadMenuKey);
	if( !Input || Key <= 0 || Key >= IK_MAX )
		return Key;
	INT MenuKey = 0;
	for( INT i = 0; i < ARRAY_COUNT(GPadMenuKeys) && !MenuKey; i++ )
		if( GPadMenuKeys[i].PadKey == Key )
			MenuKey = GPadMenuKeys[i].MenuKey;
	for( INT i = 0; i < ARRAY_COUNT(GPadCursorClicks) && !MenuKey; i++ )
		if( appStricmp( *Input->Bindings[Key], GPadCursorClicks[i].Command ) == 0 )
			MenuKey = GPadCursorClicks[i].Button;
	if( !MenuKey || ( Key != IK_Joy5 && KeyBinderPolling() ) )
		return Key;
	UObject* Root = ObjectProp( (UObject*)Console, TEXT("Root") );
	if( Key == IK_Joy1 && HadCursor && ControlUnderCursor( Root ) )
		return IK_LeftMouse;
	const UBOOL Affirmative = Key == IK_Joy1 || Key == IK_Joy7;	// A and Start
	if( Affirmative || MenuKey == IK_Escape )
		if( UObject* Answer = MessageBoxAnswer( Root, Affirmative ) )
		{
			if( InputTest )
				debugf( NAME_Log, TEXT("NSDLInputTest: key %s answers the message box with %s"), Input->GetKeyName( (EInputKey)Key ), Answer->GetName() );
			return 0;
		}
	UObject* Focus = ObjectProp( Root, TEXT("KeyFocusWindow") );
	if( IsAClassNamed( Focus, TEXT("UWindowComboList") ) && ComboListKey( Focus, MenuKey ) )
	{
		if( InputTest )
			debugf( NAME_Log, TEXT("NSDLInputTest: key %s works the open list %s"), Input->GetKeyName( (EInputKey)Key ), Focus->GetName() );
		return 0;
	}
	if( Key == IK_Joy1 )
	{
		if( UObject* Clicked = ClickFocus( Root ) )
		{
			if( InputTest )
				debugf( NAME_Log, TEXT("NSDLInputTest: key %s clicks %s"), Input->GetKeyName( (EInputKey)Key ), Clicked->GetName() );
			return 0;
		}
	}
	if( ( MenuKey == IK_Up || MenuKey == IK_Down || MenuKey == IK_Left || MenuKey == IK_Right ) && !FocusUsesArrow( Root, MenuKey ) )
		if( UObject* Focused = MoveFocus( Root, MenuKey == IK_Down || MenuKey == IK_Right ) )
		{
			if( InputTest )
				debugf( NAME_Log, TEXT("NSDLInputTest: key %s moves the focus to %s"), Input->GetKeyName( (EInputKey)Key ), Focused->GetName() );
			return 0;
		}
	return MenuKey;
	unguard;
}

//
// The pad takes the menu cursor where the mouse left it, and gives it back to the mouse. The system's pointer,
// which stays where the mouse left it, is hidden meanwhile.
//
void UNSDLViewport::SetPadCursor( UBOOL On )
{
	guard(UNSDLViewport::SetPadCursor);
	if( !On == !PadCursor )
		return;
	PadCursor = On;
	if( On )
	{
		PadCursorX = WindowsMouseX;
		PadCursorY = WindowsMouseY;
	}
	SDL_ShowCursor( On ? SDL_DISABLE : SDL_ENABLE );
	debugf( NAME_Log, TEXT("NSDLClient: the menu cursor follows the %s"), On ? TEXT("pad") : TEXT("mouse") );
	unguard;
}

//
// While UWindow shows its cursor, a stick pushed out of its dead zone gives the cursor to the pad (a stick held out
// since does not take it back from the mouse), and while the pad has it the left stick moves it: linear from the
// dead zone's edge to full tilt, across the viewport's width in PadCursorSeconds, with no acceleration. It reaches
// UWindow as the mouse's absolute motion does: the position, and the deltas UWindow counts on a viewport without
// a system pointer.
//
void UNSDLViewport::TickPadCursor( FLOAT DeltaTime )
{
	guard(UNSDLViewport::TickPadCursor);
	UBOOL Pushed = 0;
	for( INT s = 0; s < 2; s++ )
	{
		const UBOOL Out = s == 0
			? ( JoyAxis[SDL_CONTROLLER_AXIS_LEFTX] || JoyAxis[SDL_CONTROLLER_AXIS_LEFTY] )
			: ( JoyAxis[SDL_CONTROLLER_AXIS_RIGHTX] || JoyAxis[SDL_CONTROLLER_AXIS_RIGHTY] );
		if( Out && !PadStickOut[s] )
			Pushed = 1;
		PadStickOut[s] = Out;
	}
	if( !bShowWindowsMouse )
	{
		SetPadCursor( 0 );
		return;
	}
	if( Pushed )
		SetPadCursor( 1 );
	if( !PadCursor )
		return;
	const FLOAT DeadZone = Clamp( Client->DeadZoneXYZ, 0.f, 0.99f );
	const INT Axes[2] = { SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY };
	FLOAT Tilt[2];
	for( INT i = 0; i < 2; i++ )
	{
		const FLOAT Value = Clamp( JoyAxis[Axes[i]] / 32767.f, -1.f, 1.f );
		Tilt[i] = Abs( Value ) > DeadZone ? ( Value - ( Value > 0.f ? DeadZone : -DeadZone ) ) / ( 1.f - DeadZone ) : 0.f;
	}
	if( Tilt[0] == 0.f && Tilt[1] == 0.f )
		return;
	const FLOAT Step = SizeX * DeltaTime / Max( Client->PadCursorSeconds, 0.01f );
	const FLOAT X = Clamp( PadCursorX + Tilt[0] * Step, 0.f, (FLOAT)Max( SizeX - 1, 0 ) );
	const FLOAT Y = Clamp( PadCursorY + Tilt[1] * Step, 0.f, (FLOAT)Max( SizeY - 1, 0 ) );
	const FLOAT DX = X - PadCursorX, DY = Y - PadCursorY;
	PadCursorX = X;
	PadCursorY = Y;
	Client->Engine->MousePosition( this, 0, X, Y );
	if( DX != 0.f )
		CauseInputEvent( IK_MouseX, IST_Axis, DX );
	if( DY != 0.f )
		CauseInputEvent( IK_MouseY, IST_Axis, -DY );
	unguard;
}

// The move stick's walk is one bit of the player's walk button (bRun), apart from the 1 the Walking key sets: that
// key's press and release overwrite the byte, and the stick sets its bit again on the next tick.
static const BYTE PadWalkBit = 0x40;

//
// The left stick in play (the menu cursor has it while the pad has the cursor). Its dead zone is radial, so a direction
// between the axes keeps both parts; out of it, X and Y go to their bindings, and a tilt below PadWalkTilt walks.
//
void UNSDLViewport::TickMoveStick( FLOAT DeltaTime )
{
	guard(UNSDLViewport::TickMoveStick);
	UBOOL Walk = 0;
	if( !PadCursor )
	{
		const FLOAT X = MoveStick[0] / 32767.f, Y = MoveStick[1] / 32767.f;
		const FLOAT Tilt = Min( (FLOAT)appSqrt( X*X + Y*Y ), 1.f );
		if( Tilt > 0.f && Tilt >= Client->DeadZoneXYZ )
		{
			const FLOAT Scale = Client->ScaleXYZ * DeltaTime;
			if( X != 0.f )
				CauseInputEvent( IK_JoyX, IST_Axis, X * Scale * JoyAxisDefaultScale[SDL_CONTROLLER_AXIS_LEFTX] );
			if( Y != 0.f )
				CauseInputEvent( IK_JoyY, IST_Axis, Y * Scale * JoyAxisDefaultScale[SDL_CONTROLLER_AXIS_LEFTY] * ( Client->InvertY ? -1.f : 1.f ) );
			Walk = Tilt < Client->PadWalkTilt;
		}
	}
	if( Actor )
	{
		if( Walk )
			Actor->bRun |= PadWalkBit;
		else
			Actor->bRun &= ~PadWalkBit;
	}
	unguard;
}

UBOOL UNSDLViewport::TickInput()
{
	guard(UNSDLViewport::TickInput);

	SDL_Event Ev;
	INT Tmp;
	// DOUBLE: on the threaded web build appSeconds() counts from the epoch, which a FLOAT cannot resolve to a frame.
	// The first call has no previous time, and a long hitch is clamped to 0.25 s.
	const DOUBLE CurTime = appSeconds();
	const FLOAT DeltaTime = InputUpdateTime > 0.0 ? (FLOAT)Clamp( CurTime - InputUpdateTime, 0.0, 0.25 ) : 0.f;
	for( INT k = 0; k < GHeldTapNum; k++ )
		PadKey( GHeldTap[k], IST_Release );
	GHeldTapNum = 0;

	if( InputTest )
		InputTest->Tick( this );

#if defined(__ANDROID__)
	// A touch's tap arrives in one poll: the motion, the press and the release. UWindow acts on the cursor it
	// drew last frame, so a press sent with its motion lands where the finger was before. In absolute mode each
	// button event waits for a later tick, one per tick, after a frame has put the cursor under the finger. A
	// press is followed by a one-pixel move: UWindow's menu bar takes a pulldown's clicks only once the mouse
	// has moved while its button was down, which a finger held still never does.
	if( GAndroidClickNum )
	{
		if( GAndroidClick[0].Action == IST_Axis )
		{
			WindowsMouseX += 1.f;
			Client->Engine->MousePosition( this, 0, WindowsMouseX, WindowsMouseY );
			CauseInputEvent( IK_MouseX, IST_Axis, 1.f );
		}
		else
			CauseInputEvent( GAndroidClick[0].Key, (EInputAction)GAndroidClick[0].Action );
		for( INT k = 1; k < GAndroidClickNum; k++ )
			GAndroidClick[k-1] = GAndroidClick[k];
		GAndroidClickNum--;
	}
#endif

	// UseJoystick, every tick. The game's "Use Joystick" option (`set ini:Engine.Engine.ViewportManager UseJoystick ...`)
	// sets it on this live client: off closes the pad (keys released), on opens the first one present.
	{
		UNSDLClient* PadClient = Client;
		const INT On = PadClient->UseJoystick ? 1 : 0;
		if( GPadsOn >= 0 && On != GPadsOn )
		{
			debugf( NAME_Init, TEXT("NSDLClient: game pads %s (UseJoystick)"), On ? TEXT("on") : TEXT("off") );
			if( !On )
			{
				if( SDL_GameController* GC = PadClient->GetController() )
					if( PadClient->ClosePad( SDL_JoystickInstanceID( SDL_GameControllerGetJoystick( GC ) ) ) )
						ReleasePad();
			}
			else
				for( INT i=0; i<SDL_NumJoysticks() && !PadClient->GetController(); i++ )
					PadClient->OpenPad( i, NULL );
		}
		GPadsOn = On;
	}

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
#if defined(__ANDROID__)
				if( !SDL_GetRelativeMouseMode() && GAndroidClickNum < ARRAY_COUNT(GAndroidClick) - 1 )
				{
					GAndroidClick[GAndroidClickNum].Key = MouseButtonMap[Ev.button.button];
					GAndroidClick[GAndroidClickNum].Action = ( Ev.type == SDL_MOUSEBUTTONDOWN ) ? IST_Press : IST_Release;
					GAndroidClickNum++;
					if( Ev.type == SDL_MOUSEBUTTONDOWN )
					{
						GAndroidClick[GAndroidClickNum].Key = 0;
						GAndroidClick[GAndroidClickNum].Action = IST_Axis;
						GAndroidClickNum++;
					}
					break;
				}
#endif
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
			// Hot-plug: a pad connected after start opens here (the first one; UseJoystick gates it, UNSDLClient::OpenPad),
			// and the one in use closes when it goes.
			case SDL_CONTROLLERDEVICEADDED:
				{
					DWORD Held = 0;
					if( Client->OpenPad( Ev.cdevice.which, &Held ) && Held )
					{
						// Web: the press that made the browser list the pad is already down in SDL's snapshot and is
						// never reported; give it to the game now (W3C "standard" button order -> SDL's).
						static const INT W3CToSDL[17] =
						{
							SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
							SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, -1, -1,
							SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START,
							SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK,
							SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
							SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_GUIDE,
						};
						for( INT b=0; b<17; b++ )
						{
							if( !(Held & (1u<<b)) )
								continue;
							if( b==6 || b==7 )
							{
								// The triggers are buttons in the browser's layout and axes in SDL's.
								const INT Axis = b==6 ? SDL_CONTROLLER_AXIS_TRIGGERLEFT : SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
								if( JoyAxisMap[Axis] )
								{
									PadKey( JoyAxisMap[Axis], IST_Press );
									GHeldTap[GHeldTapNum++] = JoyAxisMap[Axis];	// released next tick (a tap)
								}
							}
							else if( JoyButtonMap[W3CToSDL[b]] )
							{
								PadKey( JoyButtonMap[W3CToSDL[b]], IST_Press );
								GHeldTap[GHeldTapNum++] = JoyButtonMap[W3CToSDL[b]];	// released next tick (a tap)
							}
						}
						debugf( NAME_Init, TEXT("NSDLClient: buttons held at the connect given to the game: 0x%x"), Held );
					}
				}
				break;
			case SDL_CONTROLLERDEVICEREMOVED:
				if( Client->ClosePad( Ev.cdevice.which ) )
					ReleasePad();	// nothing may stay held
				break;
			case SDL_CONTROLLERBUTTONDOWN:
			case SDL_CONTROLLERBUTTONUP:
				PadKey( JoyButtonMap[Ev.cbutton.button], ( Ev.type == SDL_CONTROLLERBUTTONDOWN ) ? IST_Press : IST_Release );
				break;
			case SDL_CONTROLLERAXISMOTION:
				// The sticks and triggers are read from the pad's state every tick (below), not from these.
				break;
			case SDL_MOUSEMOTION:
				if( InputTest )
					debugf( NAME_Log, TEXT("NSDLInputTest: motion %i,%i rel %i,%i"), Ev.motion.x, Ev.motion.y, Ev.motion.xrel, Ev.motion.yrel );
				// The mouse's own motion takes the menu cursor back from the pad, a touch's too; the motion SDL
				// reports in place when it releases the capture moves nothing and leaves it.
				if( Ev.motion.xrel || Ev.motion.yrel || Ev.motion.which == SDL_TOUCH_MOUSEID )
					SetPadCursor( 0 );
				if( !SDL_GetRelativeMouseMode() )
				{
					// Absolute (menu / cursor-visible) mode. MousePosition feeds WindowsMouseX/Y, which UWindow reads on
					// a windowed viewport (bWindowsMouseAvailable); the axis deltas move its cursor where that is off
					// (fullscreen). In game the relative branch runs (capture is armed while a level is up and no menu shows).
#if defined(__ANDROID__)
					// A touch's tap arrives with no deltas (SDL zeroes them for a finger's first position); UWindow
					// moves its cursor on the mouse axis events, as a mouse's, so they come from the cursor's last
					// position (the position itself it takes from WindowsMouseX/Y each frame).
					if( !Ev.motion.xrel && !Ev.motion.yrel )
					{
						const INT XRel = Ev.motion.x - (INT)WindowsMouseX, YRel = Ev.motion.y - (INT)WindowsMouseY;
						Client->Engine->MousePosition( this, 0, Ev.motion.x, Ev.motion.y );
						if( XRel ) CauseInputEvent( IK_MouseX, IST_Axis, XRel );
						if( YRel ) CauseInputEvent( IK_MouseY, IST_Axis, -YRel );
						break;
					}
#endif
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
				// Follow OS window and browser canvas resizes. SetClientSize re-syncs the SDL window, the
				// software framebuffer and SizeX/SizeY; a GL device re-issues glViewport on the next frame.
				// The no-change guard keeps the SDL_SetWindowSize inside SetClientSize from recursing.
				if( ( Ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || Ev.window.event == SDL_WINDOWEVENT_RESIZED )
					&& Ev.window.data1 > 0 && Ev.window.data2 > 0 )
				{
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
					// SDL reports the CSS size and has just reset the backing store to it, so re-apply the
					// device size whenever SDL's differs from it; the echo from SetClientSize's
					// SDL_SetWindowSize carries the device size and is a no-op. Unaligned (ufrontCanvasDevicePixelSize).
					INT NewX = Ev.window.data1, NewY = Ev.window.data2;
					ufrontCanvasDevicePixelSize( NewX, NewY );
					if( NewX != Ev.window.data1 || NewY != Ev.window.data2 || NewX != SizeX || NewY != SizeY )
						SetClientSize( NewX, NewY, false );
#else
					const INT NewX = Align( Ev.window.data1, 4 ), NewY = Ev.window.data2;
					if( NewX != SizeX || NewY != SizeY )
						SetClientSize( NewX, NewY, false );
#endif
				}
				break;
			default:
				break;
		}
	}

	// The sticks and triggers come from the pad's state every tick, not from SDL_CONTROLLERAXISMOTION: SDL's
	// Emscripten driver reports no axis values at the open, so SDL takes an axis's first value for its resting
	// point and posts no event for it.
	if( SDL_GameController* GC = Client->GetController() )
		for( INT a = 0; a < SDL_CONTROLLER_AXIS_MAX; a++ )
			UpdatePadAxis( a, SDL_GameControllerGetAxis( GC, (SDL_GameControllerAxis)a ) );

	TickPadCursor( DeltaTime );

	// Constantly hammer the input system with axis events for axes that are not zero; the left stick is TickMoveStick's.
	TickMoveStick( DeltaTime );
	for ( INT i = 0; i < SDL_CONTROLLER_AXIS_MAX; ++i )
	{
		const BYTE Key = JoyAxisMap[i];
		const SWORD Value = JoyAxis[i];
		if ( i == SDL_CONTROLLER_AXIS_LEFTX || i == SDL_CONTROLLER_AXIS_LEFTY )
			continue;
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

#if defined(__ANDROID__)
	TickAndroidTouch();
#endif

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
