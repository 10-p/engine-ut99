#include "Engine.h"
#include "SDL2/SDL.h"

/*-----------------------------------------------------------------------------
	Defines.
-----------------------------------------------------------------------------*/

#ifdef NSDLDRV_EXPORTS
#define NSDLDRV_API DLL_EXPORT
#else
#define NSDLDRV_API DLL_IMPORT
#endif


/*-----------------------------------------------------------------------------
	UNSDLViewport.
-----------------------------------------------------------------------------*/

extern NSDLDRV_API UBOOL GTickDue;

//
// A SDL2 viewport.
//
class NSDLDRV_API UNSDLViewport : public UViewport
{
	DECLARE_CLASS( UNSDLViewport, UViewport, CLASS_Transient )
	NO_DEFAULT_CONSTRUCTOR( UNSDLViewport )

	// Constructors.
	UNSDLViewport( ULevel* InLevel, class UNSDLClient* InClient );
	static void InternalClassInitializer( UClass* Class );

	// UObject interface.
	virtual void Destroy() override;

	// UViewport interface.
	virtual UBOOL Lock( FPlane FlashScale, FPlane FlashFog, FPlane ScreenClear, DWORD RenderLockFlags, BYTE* HitData=NULL, INT* HitSize=0 );
	virtual void Unlock( UBOOL Blit );
	virtual UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar );
	virtual void Repaint( UBOOL Blit );
	virtual void SetModeCursor();
	virtual void UpdateWindowFrame();
	virtual void OpenWindow( PTRINT ParentWindow, UBOOL Temporary, INT NewX, INT NewY, INT OpenX, INT OpenY );
	virtual void CloseWindow();
	virtual void UpdateInput( UBOOL Reset );
	virtual void MakeCurrent();
	virtual void MakeFullscreen( INT NewX, INT NewY, UBOOL UpdateProfile );
	virtual void* GetWindow();
	virtual void SetMouseCapture( UBOOL Capture, UBOOL Clip, UBOOL FocusOnly );
	virtual UBOOL IsFullscreen();
	virtual UBOOL ResizeViewport( DWORD BlitType, INT X=INDEX_NONE, INT Y=INDEX_NONE, INT ColorBytes=INDEX_NONE );

	// UNSDLViewport interface.
	void SetClientSize( INT NewX, INT NewY, UBOOL UpdateProfile );
	void EndFullscreen();
	UBOOL TickInput(); // returns true if the viewport has requested death

private:
	// Static variables.
	static BYTE KeyMap[SDL_NUM_SCANCODES]; // SDL_Scancode -> EInputKey map
	static const BYTE MouseButtonMap[6]; // SDL_BUTTON_ -> EInputKey map
	static const BYTE JoyButtonMap[SDL_CONTROLLER_BUTTON_MAX]; // SDL_GameControllerButton -> EInputKey map
	static const BYTE JoyButtonMapUI[SDL_CONTROLLER_BUTTON_MAX];  // SDL_GameControllerButton -> EInputKey map for UI
	static const BYTE JoyAxisMap[SDL_CONTROLLER_AXIS_MAX]; // SDL_GameControllerAxis -> EInputKey map
	static const FLOAT JoyAxisDefaultScale[SDL_CONTROLLER_AXIS_MAX];
	static const SWORD JoyAxisPressThreshold = 8192;

	// Variables.
	class UNSDLClient* Client;
	SDL_Window* hWnd;
	SDL_Renderer* SDLRen; // for accelerated SoftDrv
	SDL_Texture* SDLTex; // for use with the above renderer
	DWORD SDLTexFormat;
	SDL_GLContext GLCtx; // for OpenGLDrv
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
	// ufront 2.13.2: on a worker there is no SDL renderer (SDL's is GLES-backed and its proxied
	// eglCreateContext deadlocks the engine thread), so SoftDrv renders into OUR buffer and we blit it to
	// the transferred OffscreenCanvas through a 2D context.
	BYTE* SoftFB;
	INT SoftFBBytes;
#endif
	UBOOL Destroyed;
	INT DisplayIndex;
	SDL_Rect DisplaySize;
	SWORD JoyAxis[SDL_CONTROLLER_AXIS_MAX];
	void UpdatePadAxis( INT Axis, INT NewValue );	// ufront: one stick or trigger value, read from the pad's state
	void ReleasePad();	// ufront 2.91: every pad key up and the axes zeroed (the pad went, or UseJoystick went off)
	UBOOL QuitRequested;
	DOUBLE InputUpdateTime;	// ufront: DOUBLE, see TickInput
public:
	INT HoldCount;
private:

	// Info saved during captures and fullscreen sessions.
	INT SavedX, SavedY;

	// UNSDLViewport private methods.
	static void InitKeyMap();
	UBOOL CauseInputEvent( INT iKey, EInputAction Action, FLOAT Delta=0.0 );
};

/*-----------------------------------------------------------------------------
	UNSDLClient.
-----------------------------------------------------------------------------*/

//
// SDL2 implementation of the client.
//
class NSDLDRV_API UNSDLClient : public UClient, public FNotifyHook
{
	DECLARE_CLASS( UNSDLClient, UClient, CLASS_Transient|CLASS_Config )

	// Configuration.
	INT DefaultDisplay;
	UBOOL StartupFullscreen;
	UBOOL UseJoystick;
	UBOOL InvertY;
	UBOOL InvertV;
	FLOAT ScaleXYZ;
	FLOAT ScaleRUV;
	FLOAT DeadZoneXYZ;
	FLOAT DeadZoneRUV;

	// Constructors.
	UNSDLClient();
	// ufront 2.19: v400 registers config properties from StaticConstructor; the v200-era
	// InternalClassInitializer has zero call sites here, so these Options were never registered.
	void StaticConstructor();

	// UObject interface.
	virtual void Destroy() override;
	virtual void PostEditChange() override;
	virtual void ShutdownAfterError() override;

	// UClient interface.
	virtual void Init( UEngine* InEngine );
	virtual void ShowViewportWindows( DWORD ShowFlags, INT DoShow );
	virtual void EnableViewportWindows( DWORD ShowFlags, INT DoEnable );
	virtual void Poll();
	virtual UViewport* CurrentViewport();
	virtual UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar=*GLog );
	virtual void Tick();
	virtual void MakeCurrent( UViewport* InViewport );
	virtual UViewport* NewViewport( const FName Name );
	virtual void EndFullscreen();

	// UNSDLClient interface.
	void TryRenderDevice( UViewport* Viewport, const char* ClassName, UBOOL Fullscreen );
	const TArray<SDL_Rect>& GetDisplayResolutions();
	inline SDL_GameController* GetController() { return Controller; }
	// ufront (the UC2 port's pads, one pad here): open the pad at SDL device index DeviceIndex when none is open and
	// UseJoystick is on; Held gets the buttons already down at the connect (W3C indices, the web's H-3: the press
	// that made the browser list the pad, which SDL never reports). ClosePad closes it when InstanceId is ours.
	UBOOL OpenPad( INT DeviceIndex, DWORD* Held );
	UBOOL ClosePad( INT InstanceId );
	inline const SDL_DisplayMode& GetDefaultDisplayMode() const { return DefaultDisplayMode; }

private:
	// Variables.
	SDL_GameController* Controller;
	SDL_DisplayMode DefaultDisplayMode;
	TArray<SDL_Rect> DisplayResolutions;
};
