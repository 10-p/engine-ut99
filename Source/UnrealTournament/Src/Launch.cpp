/*=============================================================================
	Launch.cpp: Game launcher.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Brandon Reinhart.
=============================================================================*/

#include "LaunchPrivate.h"
#ifdef PLATFORM_SDL
#include <SDL2/SDL.h>
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <unistd.h> // chdir (ufront 2.7b — main-loop inversion + base-dir)

// ufront 2.7d: the running engine, for WebRTCDrv's listen-status push + ue_exec_command (set in InitEngine
// after Engine->Init()). WebRTCDrv.cpp declares `extern UEngine* GWebRTCEngine`.
UEngine* GWebRTCEngine = NULL;

// ADR-10: run a console command and RETURN its captured output (not fire-and-forget). The frontend/E2E call
// this via ccall for deterministic assertions + live debugging. FStringOutputDevice is a string-capturing
// FOutputDevice (UnTemplate.h); the text is held in a static FString so the returned pointer stays valid
// until the next call (the JS side reads it synchronously during the ccall). v400 Exec takes FOutputDevice&.
extern "C" char* EMSCRIPTEN_KEEPALIVE ue_exec_command( const char* Cmd )
{
	static FString Captured;
	Captured = TEXT("");
	if( GWebRTCEngine && Cmd )
	{
		FStringOutputDevice Out;
		GWebRTCEngine->Exec( Cmd, Out );
		Captured = Out;
	}
	return (char*)(*Captured);
}
#endif
/*-----------------------------------------------------------------------------
	Global variables.
-----------------------------------------------------------------------------*/

extern "C" {TCHAR THIS_PACKAGE[64]=TEXT("Launch");}

// Memory allocator.
#include "FMallocAnsi.h"
FMallocAnsi Malloc;

// Log file.
#include "FOutputDeviceFile.h"
FOutputDeviceFile Log;

// Error handler.
#include "FOutputDeviceAnsiError.h"
FOutputDeviceAnsiError Error;

// Feedback.
#include "FFeedbackContextAnsi.h"
FFeedbackContextAnsi Warn;

// File manager.
#include "FFileManagerLinux.h"
FFileManagerLinux FileManager;

// Config.
#include "FConfigCacheIni.h"

#ifdef PLATFORM_DREAMCAST
#include <kos.h>
#include <malloc.h>
#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <kos/thread.h>
#define MAIN_STACK_SIZE (32 * 1024)  
#ifdef DREAMCAST_USE_FATFS
extern "C" {
#include <fatfs.h>
}
#endif
KOS_INIT_FLAGS( INIT_DEFAULT | INIT_CDROM | INIT_CONTROLLER | INIT_KEYBOARD | INIT_MOUSE | INIT_VMU | INIT_NET );
#endif

#ifdef PLATFORM_DREAMCAST
// fix thread stack underrun
static void init_thread_stack(void) {
    kthread_t *current = thd_get_current();
    if (current) {
        void *new_stack = malloc(MAIN_STACK_SIZE);
        if (new_stack) {
            current->stack = new_stack;
            current->stack_size = MAIN_STACK_SIZE;
            current->flags |= THD_OWNS_STACK;
        }
    }
}

// What dbgio device was active at startup
DLL_EXPORT const char* GStartupDbgDev = nullptr;

//
// Display error and lock up.
//
void FatalError( const char* Fmt, ... ) __attribute__((noreturn));
void FatalError( const char* Fmt, ... )
{
	char Msg[2048];

	va_list Args;
	va_start( Args, Fmt );
	vsnprintf( Msg, sizeof( Msg ), Fmt, Args );
	va_end( Args );


	printf( "%s\n\n", Msg );

	arch_stk_trace( 2 );

	while (true)
		thd_sleep( 100 );
}

//
// Handle assertion failure.
//
void HandleAssertFail( const char* File, int Line, const char* Expr, const char* Msg, const char* Func )
{
	FatalError( "ASSERTION FAILED:\nLoc: %s:%d (%s)\nExpr: %s\n%s", File, Line, Func, Expr, Msg);
}

void HandleIrqException( irq_t Code, irq_context_t* Context, void* Data )
{
	bfont_draw_str_vram_fmt( 8, 8, true, "UNHANDLED EXCEPTION 0x%08x", Code );
	bfont_draw_str_vram_fmt( 8, 32, true, "PC: %p PR: %p", (void*)Context->pc, (void*)Context->pr );
	bfont_draw_str_vram_fmt( 8, 56, true, "SR: %p R0: %p", (void*)Context->sr, (void*)Context->r[0] );
	bfont_draw_str_vram_fmt( 8, 80, true, "PROBABLY OUT OF MEMORY");

	arch_stk_trace_at( Context->r[14], 0 );

	volatile INT Dummy = 1;
	while (Dummy);
}

#endif

/*-----------------------------------------------------------------------------
	Initialization
-----------------------------------------------------------------------------*/

//
// ufront 2.9 / 2.12 — unified render-device selection, SHARED between native and web (the
// #ifdef __EMSCRIPTEN__ guard was removed 2026-07-16 so `./UnrealTournament -SOFTWARERENDERER` /
// -XOPENGLRENDERER / … work identically on native — the run-tree launch scripts are only a testing
// convenience; the shipped binary is run as-is with a switch).
//
// Each -*RENDERER switch names a render device; NSDLViewport picks the matching GL context version from
// the device name (XOpenGL -> WebGL2, else WebGL1 on web). Precedence in SelectRenderDevice(): a switch,
// else the persisted ini choice (a hardware default OR the user's explicit pick — incl. SoftDrv — flagged
// by the ufrontUserRenderDevice marker), else the preference order below. A switch whose driver ISN'T
// compiled/available no-ops (the game still boots on the default — the web canary). A switch that actually
// initializes is persisted to the ini by PersistRenderChoiceIfSwitched() (after Engine->Init()), so the
// pick sticks next boot on both platforms.
//
struct FRenderSwitch { const TCHAR* Param; const TCHAR* Device; };
static const FRenderSwitch GRenderSwitches[] =
{
	{ TEXT("XOPENGLRENDERER"),   TEXT("XOpenGLDrv.XOpenGLRenderDevice") },
	{ TEXT("UTGLRRENDERER"),     TEXT("OpenGLDrv.OpenGLRenderDevice") },
	{ TEXT("NOPENGLESRENDERER"), TEXT("NOpenGLESDrv.NOpenGLESRenderDevice") },
	// -SOFTWARERENDERER selects SoftDrv.SoftwareRenderDevice (the CPU rasterizer), ported into ut99 from the
	// original UT99 v400 SoftDrv source + engine-unreal1's gcc/wasm portability changes (ASM=0 -> the portable
	// #if !ASM paths; empty MMX stubs + #if ASM body wraps; Line.h VC++ macros -> no 2D lines on gcc/wasm).
	// Pure software (no glad), so it coexists with XOpenGL/UTGLR on web. Renders in-browser + native.
	{ TEXT("SOFTWARERENDERER"),  TEXT("SoftDrv.SoftwareRenderDevice") },
	// -NULLRENDERER selects NullDrv.NullRenderDevice (a no-op render device — no GL context), ported into
	// ut99 alongside SoftDrv. GPU-free: a headless / "poor-man's dedicated server" run (host listens, empty
	// tab). Not in GRenderPreference, so IsHardwareDefaultEligible excludes it -> opt-in only, never a default.
	{ TEXT("NULLRENDERER"),      TEXT("NullDrv.NullRenderDevice") },
};
// Default when nothing is chosen: best HW first.
static const TCHAR* GRenderPreference[] =
{
	TEXT("XOpenGLDrv.XOpenGLRenderDevice"),
	TEXT("OpenGLDrv.OpenGLRenderDevice"),
	TEXT("NOpenGLESDrv.NOpenGLESRenderDevice"),
};
// The class a -*RENDERER switch requested (empty if none) — persisted after a successful Init().
static FString GRequestedRenderDevice;

// True iff the render-device class is compiled into this build. The web build disables exception
// CATCHING, so StaticLoadClass on an absent package appThrowf's ("Can't find file for package X") ->
// Aborted() (uncatchable — ut99 lacks unreal1's 2.6g GetPackageLinker-returns-NULL fix). StaticFindObject
// only inspects ALREADY-REGISTERED objects (a statically-linked native render device registers at
// startup) — no file system, no throw. Match the class' short name (after the last '.') vs ANY_PACKAGE.
static UBOOL IsRenderDeviceAvailable( const TCHAR* Device )
{
#ifdef __EMSCRIPTEN__
	// web (single static binary, exception CATCHING disabled): StaticFindObject inspects ALREADY-registered
	// classes — a statically-linked render device registers at startup, so no file load + no throw.
	FString Path = Device;
	INT Dot = Path.InStr( TEXT("."), 1 );
	const TCHAR* ClassName = ( Dot != INDEX_NONE ) ? ((*Path) + Dot + 1) : (*Path);
	return UObject::StaticFindObject( UClass::StaticClass(), ANY_PACKAGE, ClassName ) != NULL;
#else
	// native (shared .so packages): render-device classes are NOT registered until their package is loaded,
	// and SelectRenderDevice() runs before Engine->Init(), so StaticFindObject would see nothing. Load the
	// class by name instead (LOAD_NoWarn) — a compiled-in driver resolves + registers, an absent one returns
	// NULL. Every native preset builds the full render set, so this resolves for all GRenderSwitches/Preference.
	return UObject::StaticLoadClass( URenderDevice::StaticClass(), NULL, Device, NULL, LOAD_NoWarn, NULL ) != NULL;
#endif
}

static void SetRenderDeviceConfig( const TCHAR* Device )
{
	GConfig->SetString( TEXT("Engine.Engine"), TEXT("GameRenderDevice"),     Device );
	GConfig->SetString( TEXT("Engine.Engine"), TEXT("WindowedRenderDevice"), Device );
	GConfig->SetString( TEXT("Engine.Engine"), TEXT("RenderDevice"),         Device );
}

// True iff Device is one of the hardware render devices eligible as an IMPLICIT web default (i.e. it is
// in GRenderPreference). SoftDrv (the CPU rasterizer) is deliberately NOT in that set: it is opt-in ONLY,
// selectable via -SOFTWARERENDERER (which wins at step 1 of SelectRenderDevice), but never auto-adopted as
// the default. The retail UT99 ini ships GameRenderDevice=SoftDrv.SoftwareRenderDevice (a Glide-era CPU
// default from before hardware T&L); now that SoftDrv is compiled into the web build, honoring that
// persisted value verbatim would silently default the browser to the slow software rasterizer instead of
// the GPU. So a persisted SOFTWARE device is not honored as a default — only a persisted HARDWARE choice
// (a prior -*RENDERER pick, written back by PersistRenderChoiceIfSwitched) sticks next boot.
static UBOOL IsHardwareDefaultEligible( const TCHAR* Device )
{
	for( INT i=0; i<ARRAY_COUNT(GRenderPreference); i++ )
		if( appStricmp( Device, GRenderPreference[i] )==0 )
			return 1;
	return 0;
}

// Choose the web render device BEFORE Engine->Init() (mirrors the old hardcoded block, now data-driven).
static void SelectRenderDevice()
{
	// 1) A command-line -*RENDERER switch (first present wins).
	for( INT i=0; i<ARRAY_COUNT(GRenderSwitches); i++ )
	{
		if( ParseParam( appCmdLine(), GRenderSwitches[i].Param ) )
		{
			const TCHAR* Device = GRenderSwitches[i].Device;
			if( IsRenderDeviceAvailable( Device ) )
			{
				debugf( TEXT("ufront 2.9: -%s selects render device %s"), GRenderSwitches[i].Param, Device );
				SetRenderDeviceConfig( Device );
				GRequestedRenderDevice = Device; // persist iff it initializes (PersistRenderChoiceIfSwitched)
				return;
			}
			debugf( NAME_Warning, TEXT("ufront 2.9: render device %s not available (not compiled in this build) -> keeping the default; -%s ignored"), Device, GRenderSwitches[i].Param );
			break; // fall through to the default selection
		}
	}

	// 2) The persisted ini choice, if its driver is compiled in AND is a hardware default (see
	//    IsHardwareDefaultEligible). This is what makes a prior -*RENDERER pick "stick" next boot (the
	//    frontend renderer dropdown is session-only; the engine persists the choice via Flush). It
	//    deliberately does NOT adopt a persisted SoftDrv: the retail ini ships GameRenderDevice=SoftDrv,
	//    so honoring it verbatim would default the web build to the CPU rasterizer — SoftDrv is opt-in
	//    only, via -SOFTWARERENDERER (handled at step 1 above).
	FString Persisted, UserSel;
	if( GConfig->GetString( TEXT("Engine.Engine"), TEXT("GameRenderDevice"), Persisted ) && Persisted.Len()>0 )
	{
		// A persisted device sticks if it is a hardware default OR was EXPLICITLY user-selected: the marker
		// ufrontUserRenderDevice is written by PersistRenderChoiceIfSwitched ONLY after a -*RENDERER switch
		// actually initialized, so it distinguishes a user's SoftDrv pick (sticky, per owner 2.12) from the
		// retail ini's GameRenderDevice=SoftDrv (no marker -> still overridden to a hardware default first boot).
		const UBOOL bUserSelected =
			GConfig->GetString( TEXT("Engine.Engine"), TEXT("ufrontUserRenderDevice"), UserSel )
			&& UserSel.Len()>0 && appStricmp( *Persisted, *UserSel )==0;
		if( IsRenderDeviceAvailable( *Persisted ) && ( IsHardwareDefaultEligible( *Persisted ) || bUserSelected ) )
		{
			debugf( TEXT("ufront 2.12: using the persisted %s render device %s"), bUserSelected ? TEXT("user-selected") : TEXT("hardware"), *Persisted );
			SetRenderDeviceConfig( *Persisted );
			return;
		}
		debugf( NAME_Warning, TEXT("ufront 2.9: persisted render device %s is not an eligible hardware default (unavailable or software opt-in) -> preference order"), *Persisted );
	}

	// 3) Preference order — first compiled-in device wins.
	for( INT i=0; i<ARRAY_COUNT(GRenderPreference); i++ )
	{
		if( IsRenderDeviceAvailable( GRenderPreference[i] ) )
		{
			debugf( TEXT("ufront 2.9: default render device %s (preference order)"), GRenderPreference[i] );
			SetRenderDeviceConfig( GRenderPreference[i] );
			return;
		}
	}
	debugf( NAME_Warning, TEXT("ufront 2.9: no known render device is compiled into this build!") );
}

// After Engine->Init(): persist a -*RENDERER switch choice so it sticks next boot. The render device
// inits lazily (in the main loop at level bring-up), not within Engine->Init(), so we can't inspect the
// active RenDev here. Instead confirm the engine didn't rewrite GameRenderDevice to a fallback during
// Init: if it still equals our (compiled, selected) request, persist it. Flush writes the user ini,
// which survives reload via the user-data IDB layer.
static void PersistRenderChoiceIfSwitched()
{
	if( GRequestedRenderDevice.Len()==0 )
		return;
	FString Active;
	if( GConfig->GetString( TEXT("Engine.Engine"), TEXT("GameRenderDevice"), Active ) && appStricmp( *Active, *GRequestedRenderDevice )==0 )
	{
		// Mark this as an EXPLICIT user selection so SelectRenderDevice honors it next boot even when it is
		// not a hardware default (i.e. a user-chosen SoftDrv). Distinguishes it from the retail ini SoftDrv.
		GConfig->SetString( TEXT("Engine.Engine"), TEXT("ufrontUserRenderDevice"), *GRequestedRenderDevice );
		GConfig->Flush( 0 );
		debugf( TEXT("ufront 2.12: persisted render device %s as a user selection (sticks next boot)"), *GRequestedRenderDevice );
	}
	else
	{
		debugf( NAME_Warning, TEXT("ufront 2.9: render device changed from %s during init -> not persisted"), *GRequestedRenderDevice );
	}
}

// (render-device selection above is compiled + used on both native and web — see the header note.)

//
// Creates a UEngine object.
//
static UEngine* InitEngine()
{
	guard(InitEngine);
	DOUBLE LoadTime = appSeconds();

	// Set exec hook.
	GExec = NULL;

#ifdef __EMSCRIPTEN__
	// ufront 2.7c: the shipped Default.ini is Dreamcast-configured (PVRDrv/KOSDrv/AICADrv). Force the web
	// driver set BEFORE Engine->Init() loads the subsystems — mirrors engine-unreal1's SDLLaunch override.
	// SDL2 windowing (NSDLDrv) + NOpenGLESDrv (GLES2 -> WebGL) + the null (silent) audio subsystem. The net
	// driver auto-switches IpDrv.TcpNetDriver -> WebRTCDrv in step 2.7d. Keeping this in code (not the asset
	// inis) leaves the game data pristine — the override travels with the engine.
	GConfig->SetString( TEXT("Engine.Engine"), TEXT("ViewportManager"),      TEXT("NSDLDrv.NSDLClient") );
	// ufront 2.12 (web audio): route audio to the real OpenAL subsystem (NOpenALDrv) instead of the silent
	// null one — SFX via Emscripten's built-in OpenAL + .umx tracker music via a FetchContent'd libxmp,
	// pumped from Update() (the web build is single-threaded; EFX/HRTF compiled out under NOPENAL_WEB). The
	// driver's constructor defaults are audible (MasterVolume=255) so no ini is required. Native uses the
	// same driver (openal-soft, full EFX). Mirrors engine-unreal1.
	GConfig->SetString( TEXT("Engine.Engine"), TEXT("AudioDevice"),          TEXT("NOpenALDrv.NOpenALAudioSubsystem") );
	// ufront 2.7d: BSD sockets don't work in a browser — auto-switch the net driver to the engine-local
	// WebRTC driver (its transport is the glue webrtc_* ABI). Mirrors engine-unreal1/engine-ut2004.
	GConfig->SetString( TEXT("Engine.Engine"), TEXT("NetworkDevice"),        TEXT("WebRTCDrv.WebRTCNetDriver") );
	// ufront 2.12 (web mouse): the SDL client ships CaptureMouse=False (classic click-drag-to-look), but the
	// web wants UT2004-style dynamic pointer lock — captured while playing, released in a menu. UnGame's
	// Tick/mouse handlers already drive SetMouseCapture from bShowMenu + Client->CaptureMouse; enabling the flag
	// + NSDLViewport::SetMouseCapture's web focus-skip (-> SDL_SetRelativeMouseMode) yields click-to-lock /
	// ESC-to-unlock (SDL2's Emscripten port defers the lock to the next click + re-grabs after ESC). Mirrors
	// engine-unreal1; native keeps the ini's CaptureMouse (revisit in the native pass).
	GConfig->SetString( TEXT("NSDLDrv.NSDLClient"),  TEXT("CaptureMouse"),    TEXT("True") );
#endif

	// ufront 2.12: unified render-device selection — SHARED native+web, run BEFORE Engine->Init() (which loads
	// the subsystems). A -*RENDERER switch wins, else a persisted hardware default OR the user's marked pick,
	// else the preference order. (Was #ifdef __EMSCRIPTEN__; now also honors `./UnrealTournament -SOFTWARERENDERER`
	// etc. on native.)
	SelectRenderDevice();

	// Update first-run.
	INT FirstRun=0;
	if (FirstRun<ENGINE_VERSION)
		FirstRun = ENGINE_VERSION;
	GConfig->SetInt( TEXT("FirstRun"), TEXT("FirstRun"), FirstRun );

	// Create the global engine object.
	UClass* EngineClass;
	EngineClass = UObject::StaticLoadClass(
		UGameEngine::StaticClass(), NULL, 
		TEXT("ini:Engine.Engine.GameEngine"), 
		NULL, LOAD_NoFail, NULL 
	);
	UEngine* Engine = ConstructObject<UEngine>( EngineClass );
#ifdef __EMSCRIPTEN__
	// ufront 2.7d: drop UT99's retail ServerActors before Engine->Init() -> Browse() spins up a listen server
	// and spawns them. The game ini's [Engine.GameEngine] lists six — IpDrv.UdpBeacon, IpServer.UdpServerQuery,
	// 3x IpServer.UdpServerUplink (master-server uplinks) and UWeb.WebServer (web-admin) — every one a UDP /
	// master-server / web-admin actor that is dead in a browser (no UDP sockets, no master server; UWeb's
	// native class isn't even compiled -> "Can't bind to native class UWeb.WebRequest" aborts the listen
	// server). A WebRTC listen game needs none of them; the game link is the glue webrtc_* ABI. Cleared on the
	// engine object here in the LAUNCHER (mirrors the driver overrides above) — NOT in core UnLevel.cpp and NOT
	// by mutating the pristine game-asset ini. Placement is load-bearing: after ConstructObject (the array is
	// copied from the class-default object, which loads config at package-bind, so a GConfig clear would be too
	// late — UGameEngine isn't CLASS_PerObjectConfig) and before Init() (which runs the listen path). Only
	// UT99 configures these actors (unreal1/ut2004 need no equivalent). ServerPackages (skins) is left intact.
	if( Engine->IsA(UGameEngine::StaticClass()) )
		((UGameEngine*)Engine)->ServerActors.Empty();
#endif
	Engine->Init();
	// ufront 2.12: persist a -*RENDERER switch choice so it sticks next boot — SHARED native+web (writes the
	// ufrontUserRenderDevice marker if Init didn't rewrite GameRenderDevice to a fallback).
	PersistRenderChoiceIfSwitched();
#ifdef PLATFORM_DREAMCAST
	malloc_stats();
#endif
#ifdef __EMSCRIPTEN__
	// ufront 2.7d: expose the running engine to WebRTCDrv (listen-status push) + ue_exec_command (ADR-10).
	GWebRTCEngine = Engine;
#endif
	debugf( TEXT("Startup time: %f seconds."), appSeconds()-LoadTime );

	return Engine;
	unguard;
}

//
// Handle an error.
//
void HandleError( const char* Exception )
{
	GIsGuarded=0;
	GIsCriticalError=1;
	debugf( NAME_Exit, "Shutting down after catching exception" );
	debugf( NAME_Exit, "Exiting due to exception" );
	GErrorHist[ARRAY_COUNT(GErrorHist)-1]=0;
#ifdef PLATFORM_SDL
	SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, LocalizeError("Critical"), GErrorHist, SDL_GetKeyboardFocus() );
#elif defined(PLATFORM_DREAMCAST)
	if( Exception )
		FatalError( "FATAL ERROR:\n%s\n\n%s", Exception, GErrorHist );
	else
		FatalError( "FATAL ERROR:\n%s", GErrorHist );
#endif
}

/*-----------------------------------------------------------------------------
	Main Loop
-----------------------------------------------------------------------------*/

//
// X game message loop.
//
static void MainLoop( UEngine* Engine )
{
	guard(MainLoop);
	check(Engine);

	// Loop while running.
	GIsRunning = 1;
	DOUBLE OldTime = appSeconds();
	DOUBLE SecondStartTime = OldTime;
	INT TickCount = 0;
	while( GIsRunning && !GIsRequestingExit )
	{
		// Update the world.
		guard(UpdateWorld);
		DOUBLE NewTime   = appSeconds();
		FLOAT  DeltaTime = NewTime - OldTime;
		Engine->Tick( DeltaTime );
		if( GWindowManager )
			GWindowManager->Tick( DeltaTime );
		OldTime = NewTime;
		TickCount++;
		if( OldTime > SecondStartTime + 1 )
		{
			Engine->CurrentTickRate = (FLOAT)TickCount / (OldTime - SecondStartTime);
			SecondStartTime = OldTime;
			TickCount = 0;
		}
		unguard;

		// Enforce optional maximum tick rate.
		guard(EnforceTickRate);
		FLOAT MaxTickRate = Engine->GetMaxTickRate();
#ifdef __EMSCRIPTEN__
		// ufront 2.7b — main-loop inversion (docs/10 §2), matching engine-unreal1/engine-ut2004: a browser
		// cannot block its main thread, so each frame we yield via emscripten_sleep, which ASYNCIFY-suspends
		// the wasm and lets the browser event loop run (rendering + rAF) before resuming. NOT
		// emscripten_set_main_loop (its simulate_infinite_loop unwind collides with main()'s C++ try/catch).
		// Always yield >= 1 ms so the browser makes progress even when a frame overruns / no cap is set.
		{
			DOUBLE Budget = MaxTickRate>0.0 ? (1.0/MaxTickRate) : 0.0;
			INT SleepMs = (INT)((Budget - (appSeconds()-OldTime)) * 1000.0);
			emscripten_sleep( SleepMs > 1 ? SleepMs : 1 );
		}
#else
		// ufront 2.12 (native): enforce a client frame-rate cap. UGameEngine::GetMaxTickRate() returns 0 for
		// standalone play (no net driver), so without a cap this loop runs at hundreds/thousands of FPS on
		// modern hardware. ULevel::Tick then clamps DeltaSeconds to a 0.005s (200 fps) MINIMUM
		// (UnLevTic.cpp) -> above 200 fps the world advances MORE time than really elapsed, so the whole game
		// runs faster the higher the frame rate (owner-reported: fast staring at a wall, slower in open areas,
		// always too fast). The web build never hits this because the browser's rAF/vsync caps it ~60 fps.
		// Cap here so DeltaSeconds stays in ULevel::Tick's honest (unclamped) 2.5..200 fps range. A net/demo
		// cap (GetMaxTickRate>0) still wins; otherwise a sane default, overridable via [Engine.Engine]
		// FrameRateCap in the ini (0 or negative => uncapped, restoring the old behavior if ever wanted).
		FLOAT Cap = MaxTickRate;
		if( Cap<=0.0 )
		{
			static FLOAT ClientCap = -1.f;
			if( ClientCap < -0.5f )
			{
				FLOAT V=0.f;
				ClientCap = ( GConfig->GetFloat(TEXT("Engine.Engine"),TEXT("FrameRateCap"),V) ) ? V : 120.f;
			}
			Cap = ClientCap;
		}
		if( Cap>0.0 )
		{
			FLOAT Delta = (1.0/Cap) - (appSeconds()-OldTime);
			appSleep( Max(0.f,Delta) );
		}
#endif
		unguard;
	}
	GIsRunning = 0;

	unguard;
}

/*-----------------------------------------------------------------------------
	Main.
-----------------------------------------------------------------------------*/

//
// Simple copy.
// 

void SimpleCopy(TCHAR* fromfile, TCHAR* tofile)
{
	INT c;
	FILE* from;
	FILE* to;
	from = fopen(fromfile, "r");
	if (from == NULL)
		return;
	to = fopen(tofile, "w");
	if (to == NULL)
	{
		printf("Can't open or create %s", tofile);
		return;
	}
	while ((c = getc(from)) != EOF)
		putc(c, to);
	fclose(from);
	fclose(to);
}

//
// Exit wound.
//
int CleanUpOnExit(int ErrorLevel)
{
	GFileManager->Delete(TEXT("Running.ini"),0,0);
	debugf( NAME_Title, LocalizeGeneral("Exit") );
	appPreExit();
	GIsGuarded = 0;

	// Shutdown.
	appExit();
	GIsStarted = 0;

	// Restore the user's configuration.
	TCHAR baseconfig[PATH_MAX] = TEXT("");
	if( getcwd(baseconfig, sizeof(baseconfig)) == NULL )
	{
		appStrcpy( baseconfig, TEXT("./User.ini") );
	}
	else
	{
		appStrcat(baseconfig, "/User.ini");
	}

	TCHAR userconfig[PATH_MAX] = TEXT("");
	sprintf(userconfig, "~/.utconf");

	TCHAR exec[PATH_MAX] = TEXT("");
	sprintf(exec, "cp -f %s %s", baseconfig, userconfig);
	//system( exec );

	return ErrorLevel;
}

//
// Entry point.
//
int main( int argc, char* argv[] )
{
#ifdef PLATFORM_DREAMCAST
	// fix thread stack underrun
	init_thread_stack();
	// Redirect dbgio to the framebuffer if we're not already using dcload.
	GStartupDbgDev = dbgio_dev_get();
	if( !GStartupDbgDev || !appStrstr( GStartupDbgDev, "dcl" ) )
		dbgio_dev_select( "fb" );
	assert_set_handler( HandleAssertFail );
	irq_set_handler( EXC_UNHANDLED_EXC, HandleIrqException, nullptr );
#ifdef DREAMCAST_USE_FATFS
	if( fs_fat_mount_sd() == 0 )
	{
		printf( "SD card found, will try to load data from there\n" );
	}
	else
	{
		// failed
		printf( "SD card not found, will default to CD\n" );
		sd_shutdown();
		fs_fat_shutdown();
	}
#endif
#endif
	try
	{
	guard(main);
	
	INT ErrorLevel = 0;
	GIsStarted	   = 1;
#ifndef PLATFORM_DREAMCAST
	// Set module name.
	appStrcpy( GModule, argv[0] );

	// Set the package name.
	appStrcpy( THIS_PACKAGE, appPackage() );	
#endif
	// Get the command line.
	TCHAR CmdLine[1024], *CmdLinePtr=CmdLine;
	*CmdLinePtr = 0;
	for( INT i=1; i<argc; i++ )
	{
		if( i>1 )
			appStrcat( CmdLine, " " );
		appStrcat( CmdLine, argv[i] );
	}
#ifndef PLATFORM_DREAMCAST
	// Take care of .ini swapping.
	TCHAR userconfig[PATH_MAX] = TEXT("");
	sprintf(userconfig, "~/.utconf");

	TCHAR baseconfig[PATH_MAX] = TEXT("");
	if( getcwd(baseconfig, sizeof(baseconfig)) == NULL )
	{
		appStrcpy( baseconfig, TEXT("./User.ini") );
	}
	else
	{
		appStrcat(baseconfig, "/User.ini");
	}

	TCHAR exec[PATH_MAX] = TEXT("");
	sprintf(exec, "cp -f %s %s", userconfig, baseconfig);
	//system( exec );
#endif
	//SimpleCopy( userconfig, baseconfig );

	// Init core.
	GIsClient = 1;
	GIsGuarded = 0;
#ifdef __EMSCRIPTEN__
	// ufront 2.7b (web port): the MEMFS root is "/", but UT99 runs from its System/ dir like a native
	// install — Default.ini is preloaded at /System and the package-search Paths (../System, ../Maps, ...)
	// resolve relative to appBaseDir(). FetchFS has created /System (from the manifest) in preRun. chdir
	// here, BEFORE appInit() caches appBaseDir()=getcwd(), so the base dir becomes "/System/".
	chdir( "/System" );
#endif
	appInit( TEXT("UnrealTournament"), CmdLine, &Malloc, &Log, &Error, &Warn, &FileManager, FConfigCacheIni::Factory, 1 );

	// Init mode.
	GIsServer		= 1;
	GIsClient		= !ParseParam(appCmdLine(), TEXT("SERVER"));
	GIsEditor		= 0;
	GIsScriptable	= 1;
#ifdef PLATFORM_DREAMCAST
	GLazyLoad  		= 1;
#else
	GLazyLoad		= !GIsClient || ParseParam(appCmdLine(), TEXT("LAZY"));
#endif
#ifdef PLATFORM_DREAMCAST
		Warn.AuxOut = GLog;
		GLog		= &Warn;
#else
		// Init console log.
	if (ParseParam(CmdLine, TEXT("LOG")))
	{
			Warn.AuxOut = GLog;
			GLog		= &Warn;
	}
#endif

	// Init engine.
	UEngine* Engine = InitEngine();
	if( Engine )
	{
		debugf( NAME_Title, LocalizeGeneral("Run") );

		// Optionally Exec and exec file.
		FString Temp;
		if( Parse(CmdLine, TEXT("EXEC="), Temp) )
		{
			Temp = FString(TEXT("exec ")) + Temp;
			if( Engine->Client && Engine->Client->Viewports.Num() && Engine->Client->Viewports(0) )
				Engine->Client->Viewports(0)->Exec( *Temp, *GLog );
		}

		// Start main engine loop.
		debugf( TEXT("Entering main loop.") );
		if ( !GIsRequestingExit )
			MainLoop( Engine );
	}

	// Finish up.
	return CleanUpOnExit(ErrorLevel);

	unguard;
	}
	catch (...)
	{
		// Chained abort.  Do cleanup.
		HandleError(NULL);
		return CleanUpOnExit(1);
	}
}