/*=============================================================================
	Launch.cpp: Game launcher.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Brandon Reinhart.
=============================================================================*/

#ifdef __EMSCRIPTEN__
// Before the engine headers: html5.h pulls in <time.h>, and UnFile.h #defines clock(). The page's visibility,
// for the frame wait.
#include <emscripten/html5.h>
#endif
#include "LaunchPrivate.h"
#ifdef PLATFORM_SDL
#include <SDL2/SDL.h>
#endif
#if defined(__ANDROID__)
#include <android/log.h>
#include <pthread.h>
#include <sys/system_properties.h>
#include <unistd.h>

// stdout and stderr on logcat (tag UT99-stdio): both are a pipe that a thread reads line by line. GLog's own
// lines go to logcat directly (FOutputDeviceFile); this catches printf output, appThrowf's among it.
static void* AndroidStdioReader( void* Arg )
{
	const int Fd = (int)(intptr_t)Arg;
	char Buf[1024];
	size_t Len = 0;
	for( ;; )
	{
		const ssize_t N = read( Fd, Buf + Len, sizeof(Buf) - 1 - Len );
		if( N <= 0 )
			break;
		Len += (size_t)N;
		Buf[Len] = 0;
		char* Start = Buf;
		while( char* End = strchr( Start, '\n' ) )
		{
			*End = 0;
			if( End > Start )
				__android_log_write( ANDROID_LOG_INFO, "UT99-stdio", Start );
			Start = End + 1;
		}
		Len = strlen( Start );
		memmove( Buf, Start, Len + 1 );
		if( Len == sizeof(Buf) - 1 )
		{
			__android_log_write( ANDROID_LOG_INFO, "UT99-stdio", Buf );
			Len = 0;
		}
	}
	return NULL;
}
static void AndroidRedirectStdio()
{
	int Fds[2];
	if( pipe( Fds ) != 0 )
		return;
	setvbuf( stdout, NULL, _IOLBF, 0 );
	setvbuf( stderr, NULL, _IONBF, 0 );
	dup2( Fds[1], STDOUT_FILENO );
	dup2( Fds[1], STDERR_FILENO );
	close( Fds[1] );
	pthread_t Thread;
	pthread_attr_t Attr;
	pthread_attr_init( &Attr );
	pthread_attr_setdetachstate( &Attr, PTHREAD_CREATE_DETACHED );
	if( pthread_create( &Thread, &Attr, AndroidStdioReader, (void*)(intptr_t)Fds[0] ) != 0 )
		close( Fds[0] );
	pthread_attr_destroy( &Attr );
}
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/heap.h> // emscripten_get_heap_size, for the pacing ring
#include <unistd.h> // chdir

// Whether the page is hidden, for the frame wait (the worker has no document of its own).
EM_JS(void, ufront_set_hidden, (int hidden), {
    globalThis.ufrontHidden = !!hidden;
});

// A hidden page has no display frames: its frames run from timers.
static EM_BOOL UfrontVisibilityChange( int EventType, const EmscriptenVisibilityChangeEvent* Event, void* UserData )
{
	ufront_set_hidden( Event->hidden );
	return EM_TRUE;
}

// The per-frame wait. The engine thread returns to the worker's event loop every frame: that is when the worker's
// OffscreenCanvas is composited (the frame's present point) and when input and console commands arrive. It resumes
// at the display's next frame (the worker's requestAnimationFrame), or at the first display frame that reaches
// waitMs from now: frames that start on the display's frames take the browser's mouse motion (one coalesced event a
// display frame) one batch a frame, and are shown at an even pace. Where the worker has no requestAnimationFrame,
// while the page is hidden and with -TIMERFRAMES, a timer resumes it instead (waitMs, or 0 ms); a display frame
// that does not come within 100 ms (the page hidden meanwhile) gives way to a timer too. EM_ASYNC_JS imports match
// Asyncify's default `__asyncjs__*` list, so no ASYNCIFY_IMPORTS entry is needed.
EM_ASYNC_JS(void, ufront_wait_frame, (int onDisplayFrame, double waitMs), {
    const end = performance.now() + Math.max(0, waitMs);
    for (;;) {
        if (!onDisplayFrame || typeof requestAnimationFrame !== 'function' || globalThis.ufrontHidden) {
            const left = end - performance.now();
            await new Promise((resolve) => { setTimeout(resolve, Math.max(0, left)); });
            return;
        }
        await new Promise((resolve) => {
            const t = setTimeout(resolve, 100);
            requestAnimationFrame(() => { clearTimeout(t); resolve(); });
        });
        if (performance.now() >= end - 1)
            return;
    }
});

// -TIMERFRAMES (e.g. a benchmark): frames resume from timers alone. The first call, on the engine's thread, also
// starts following the page's visibility.
static INT UfrontOnDisplayFrame()
{
	static INT OnDisplayFrame = -1;
	if( OnDisplayFrame < 0 )
	{
		OnDisplayFrame = !ParseParam( appCmdLine(), TEXT("TIMERFRAMES") );
		debugf( NAME_Init, TEXT("ufront: frames start %s"), OnDisplayFrame ? TEXT("on the display's frames") : TEXT("from timers") );
		EmscriptenVisibilityChangeEvent Status;
		if( emscripten_get_visibility_status( &Status ) == EMSCRIPTEN_RESULT_SUCCESS )
			ufront_set_hidden( Status.hidden );
		emscripten_set_visibilitychange_callback( NULL, EM_TRUE, UfrontVisibilityChange );
	}
	return OnDisplayFrame;
}

// Wait until the next frame may start: the display's next frame (Period 0), or with a frame-rate cap a display
// frame, at most one a Period on average. Each frame's due time is the one before plus Period, so a cap the
// display's rate does not divide still averages the cap (some display frames are skipped); a frame later than its
// due time moves the schedule to itself. Returns the wait the cap asked for in ms, INDEX_NONE without a cap.
static INT UfrontWaitFrame( DOUBLE Period )
{
	static DOUBLE Due = 0.0;
	if( Period <= 0.0 )
	{
		ufront_wait_frame( UfrontOnDisplayFrame(), 0.0 );
		return INDEX_NONE;
	}
	const DOUBLE Now = appSeconds();
	const DOUBLE WaitMs = Max( 0.0, Due - Now ) * 1000.0;
	ufront_wait_frame( UfrontOnDisplayFrame(), WaitMs );
	Due = Max( Due + Period, appSeconds() );
	return (INT)WaitMs;
}


// The running engine, for WebRTCDrv's listen-status push and ue_exec_command (set in InitEngine after
// Engine->Init()).
UEngine* GEngineForExec = NULL;

// The frame-pacing ring: one sample per frame, so a reader sees the interval distribution a mean frame rate
// hides. Dumped by `ufront_pacing` (ue_exec_command).
//   TickMs   — Engine->Tick + GWindowManager->Tick: simulation and the whole render/submit path.
//   YieldMs  — the present point: the frame wait (UfrontWaitFrame).
//   SleepReq — what the cap asked for.
#define UFRONT_PACE_MAX 4096
struct FUfrontPaceSample
{
	DOUBLE Start;      // appSeconds() at the top of the frame
	FLOAT  TickMs;
	FLOAT  YieldMs;
	INT    SleepReqMs; // the wait the cap asked for; INDEX_NONE without a cap
	// wasm heap size: a memory.grow under -pthread rebuilds every thread's views, so a stall frame can be
	// matched with a heap growth
	INT    HeapMB;
};
static FUfrontPaceSample GUfrontPace[UFRONT_PACE_MAX];
static INT GUfrontPaceHead  = 0; // next write slot
static INT GUfrontPaceCount = 0; // total frames ever recorded (so the ring's fill level is known)

static void UfrontPaceRecord( DOUBLE Start, FLOAT TickMs, FLOAT YieldMs, INT SleepReqMs )
{
	FUfrontPaceSample& S = GUfrontPace[GUfrontPaceHead];
	S.Start      = Start;
	S.TickMs     = TickMs;
	S.YieldMs    = YieldMs;
	S.SleepReqMs = SleepReqMs;
	S.HeapMB     = (INT)(emscripten_get_heap_size() / (1024*1024));
	GUfrontPaceHead = (GUfrontPaceHead+1) % UFRONT_PACE_MAX;
	GUfrontPaceCount++;
}

// Dump the ring as raw CSV, one header line and one line per frame; the frontend's pacing probe computes
// percentiles, judder and the sleep/yield split from the series.
static FString UfrontPaceDump()
{
	const INT N = Min(GUfrontPaceCount, (INT)UFRONT_PACE_MAX);
	const INT First = (GUfrontPaceHead - N + UFRONT_PACE_MAX) % UFRONT_PACE_MAX;
	FString Out = FString::Printf( TEXT("ufront_pacing v3 frames=%i total=%i now=%f\n"), N, GUfrontPaceCount, appSeconds() );
	Out += TEXT("start_s,tick_ms,yield_ms,sleep_req_ms,heap_mb\n");
	for( INT i=0; i<N; i++ )
	{
		const FUfrontPaceSample& S = GUfrontPace[(First+i) % UFRONT_PACE_MAX];
		Out += FString::Printf( TEXT("%f,%.3f,%.3f,%i,%i\n"), S.Start, S.TickMs, S.YieldMs, S.SleepReqMs, S.HeapMB );
	}
	return Out;
}

// Run a console command and return its captured output (the frontend and the E2E call it through ccall). The
// text lives in a static FString, so the returned pointer stays valid until the next call.
extern "C" char* EMSCRIPTEN_KEEPALIVE ue_exec_command( const char* Cmd )
{
	static FString Captured;
	Captured = TEXT("");
	// The pacing ring belongs to the launcher's main loop, so it answers here rather than through UEngine::Exec;
	// `ufront_pacing reset` empties it.
	if( Cmd && appStrnicmp( Cmd, TEXT("ufront_pacing"), 13 )==0 )
	{
		if( appStrfind( (TCHAR*)Cmd, TEXT("reset") ) )
		{
			GUfrontPaceHead = GUfrontPaceCount = 0;
			Captured = TEXT("ufront_pacing reset\n");
		}
		else Captured = UfrontPaceDump();
		return (char*)(*Captured);
	}
	if( GEngineForExec && Cmd )
	{
		FStringOutputDevice Out;
		// What the engine itself does not take goes to the first viewport, as a line typed on the console would
		// (the render device's commands, the player's exec functions); a viewport runs commands once it has a player.
		UClient* Client = GEngineForExec->Client;
		if( !GEngineForExec->Exec( Cmd, Out ) && Client && Client->Viewports.Num() && Client->Viewports(0)->Actor )
			Client->Viewports(0)->Exec( Cmd, Out );
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
// Render-device selection, on every target. Each -*RENDERER switch names a render device. SelectRenderDevice()
// takes a switch; else the persisted ini choice when it is a hardware default or the user's marked pick
// (ufrontUserRenderDevice); else the preference order below. A switch whose driver is absent is ignored; one
// whose device survives Init is persisted by PersistRenderChoiceIfSwitched().
//
struct FRenderSwitch { const TCHAR* Param; const TCHAR* Device; };
static const FRenderSwitch GRenderSwitches[] =
{
	{ TEXT("XOPENGLRENDERER"),   TEXT("XOpenGLDrv.XOpenGLRenderDevice") },
	{ TEXT("UTGLRRENDERER"),     TEXT("OpenGLDrv.OpenGLRenderDevice") },
	{ TEXT("NOPENGLESRENDERER"), TEXT("NOpenGLESDrv.NOpenGLESRenderDevice") },
	// -SOFTWARERENDERER: Epic's CPU rasterizer through its portable C paths.
	{ TEXT("SOFTWARERENDERER"),  TEXT("SoftDrv.SoftwareRenderDevice") },
	// -NULLRENDERER: a render device that draws nothing and needs no GL context (headless runs). It is not in
	// GRenderPreference, so it is never a default.
	{ TEXT("NULLRENDERER"),      TEXT("NullDrv.NullRenderDevice") },
};
// Default when nothing is chosen: best HW first. On Android NOpenGLESDrv leads: on a mobile GPU XOpenGLDrv's
// ES path runs a match at a fraction of its frame rate.
static const TCHAR* GRenderPreference[] =
{
#if defined(__ANDROID__)
	TEXT("NOpenGLESDrv.NOpenGLESRenderDevice"),
	TEXT("XOpenGLDrv.XOpenGLRenderDevice"),
#else
	TEXT("XOpenGLDrv.XOpenGLRenderDevice"),
	TEXT("OpenGLDrv.OpenGLRenderDevice"),
	TEXT("NOpenGLESDrv.NOpenGLESRenderDevice"),
#endif
};
// The class a -*RENDERER switch requested (empty if none) — persisted after a successful Init().
static FString GRequestedRenderDevice;

// True iff the named "Package.Class" (a render device, viewport client, audio subsystem or net driver) is in
// this build.
static UBOOL IsPackageClassAvailable( const TCHAR* Device )
{
#ifdef __EMSCRIPTEN__
	// Every driver of the static web binary registers at startup, so look the class up by its short name
	// without loading anything.
	FString Path = Device;
	INT Dot = Path.InStr( TEXT("."), 1 );
	const TCHAR* ClassName = ( Dot != INDEX_NONE ) ? ((*Path) + Dot + 1) : (*Path);
	return UObject::StaticFindObject( UClass::StaticClass(), ANY_PACKAGE, ClassName ) != NULL;
#else
	// A driver class is registered only once its package is loaded, so load it by name (LOAD_NoWarn): a present
	// driver resolves, an absent one returns NULL. UObject is the base because the probe also answers for
	// viewport clients, audio subsystems and net drivers.
	return UObject::StaticLoadClass( UObject::StaticClass(), NULL, Device, NULL, LOAD_NoWarn, NULL ) != NULL;
#endif
}

// Replace a subsystem the ini names but this build lacks with the first present driver of Preference, and log
// the substitution. The retail ini names WinDrv.WindowsClient and Galaxy.GalaxyAudioSubsystem, which the engine
// loads with LOAD_NoFail: a fatal error before the first frame. A present choice is left alone.
static void NormalizeSubsystem( const TCHAR* Section, const TCHAR* Key, const TCHAR* const* Preference, INT Num )
{
	FString Configured;
	if( GConfig->GetString( Section, Key, Configured ) && Configured.Len() && IsPackageClassAvailable( *Configured ) )
		return;	// the configured driver is present

	for( INT i=0; i<Num; i++ )
	{
		if( IsPackageClassAvailable( Preference[i] ) )
		{
			debugf( TEXT("ufront: [%s] %s=%s is not in this build -> %s"),
				Section, Key, Configured.Len() ? *Configured : TEXT("(unset)"), Preference[i] );
			GConfig->SetString( Section, Key, Preference[i] );
			return;
		}
	}
	debugf( NAME_Warning, TEXT("ufront: [%s] %s=%s is not in this build and no replacement is either"),
		Section, Key, Configured.Len() ? *Configured : TEXT("(unset)") );
}

static void SetRenderDeviceConfig( const TCHAR* Device )
{
	GConfig->SetString( TEXT("Engine.Engine"), TEXT("GameRenderDevice"),     Device );
	GConfig->SetString( TEXT("Engine.Engine"), TEXT("WindowedRenderDevice"), Device );
	GConfig->SetString( TEXT("Engine.Engine"), TEXT("RenderDevice"),         Device );
}

// True iff Device is in GRenderPreference, i.e. may be taken from the ini without the user marker. SoftDrv is
// not: the retail ini ships GameRenderDevice=SoftDrv.SoftwareRenderDevice, and the CPU rasterizer is never an
// implicit default.
static UBOOL IsHardwareDefaultEligible( const TCHAR* Device )
{
	for( INT i=0; i<ARRAY_COUNT(GRenderPreference); i++ )
		if( appStricmp( Device, GRenderPreference[i] )==0 )
			return 1;
	return 0;
}

// Choose the render device before Engine->Init() loads it.
static void SelectRenderDevice()
{
	// 1) A command-line -*RENDERER switch (first present wins).
	for( INT i=0; i<ARRAY_COUNT(GRenderSwitches); i++ )
	{
		if( ParseParam( appCmdLine(), GRenderSwitches[i].Param ) )
		{
			const TCHAR* Device = GRenderSwitches[i].Device;
			if( IsPackageClassAvailable( Device ) )
			{
				debugf( TEXT("ufront: -%s selects render device %s"), GRenderSwitches[i].Param, Device );
				SetRenderDeviceConfig( Device );
				GRequestedRenderDevice = Device; // persist iff it initializes (PersistRenderChoiceIfSwitched)
				return;
			}
			debugf( NAME_Warning, TEXT("ufront: render device %s not available (not compiled in this build) -> keeping the default; -%s ignored"), Device, GRenderSwitches[i].Param );
			break; // fall through to the default selection
		}
	}

	// 2) The persisted ini choice, if its driver is present and it is a hardware default or the marked user pick.
	FString Persisted, UserSel;
	if( GConfig->GetString( TEXT("Engine.Engine"), TEXT("GameRenderDevice"), Persisted ) && Persisted.Len()>0 )
	{
		// ufrontUserRenderDevice is written only after a -*RENDERER switch initialized, so it tells a user's SoftDrv
		// pick from the retail ini's GameRenderDevice=SoftDrv.
		const UBOOL bUserSelected =
			GConfig->GetString( TEXT("Engine.Engine"), TEXT("ufrontUserRenderDevice"), UserSel )
			&& UserSel.Len()>0 && appStricmp( *Persisted, *UserSel )==0;
		if( IsPackageClassAvailable( *Persisted ) && ( IsHardwareDefaultEligible( *Persisted ) || bUserSelected ) )
		{
			debugf( TEXT("ufront: using the persisted %s render device %s"), bUserSelected ? TEXT("user-selected") : TEXT("hardware"), *Persisted );
			SetRenderDeviceConfig( *Persisted );
			return;
		}
		debugf( NAME_Warning, TEXT("ufront: persisted render device %s is not an eligible hardware default (unavailable or software opt-in) -> preference order"), *Persisted );
	}

	// 3) Preference order — first compiled-in device wins.
	for( INT i=0; i<ARRAY_COUNT(GRenderPreference); i++ )
	{
		if( IsPackageClassAvailable( GRenderPreference[i] ) )
		{
			debugf( TEXT("ufront: default render device %s (preference order)"), GRenderPreference[i] );
			SetRenderDeviceConfig( GRenderPreference[i] );
			return;
		}
	}
	debugf( NAME_Warning, TEXT("ufront: no known render device is compiled into this build!") );
}

// After Engine->Init(): persist a -*RENDERER choice so it sticks next boot. The render device initializes later
// (at level bring-up), so this checks that Init did not rewrite GameRenderDevice to a fallback.
static void PersistRenderChoiceIfSwitched()
{
	if( GRequestedRenderDevice.Len()==0 )
		return;
	FString Active;
	if( GConfig->GetString( TEXT("Engine.Engine"), TEXT("GameRenderDevice"), Active ) && appStricmp( *Active, *GRequestedRenderDevice )==0 )
	{
		// The marker lets SelectRenderDevice keep a pick that is not a hardware default (SoftDrv).
		GConfig->SetString( TEXT("Engine.Engine"), TEXT("ufrontUserRenderDevice"), *GRequestedRenderDevice );
		GConfig->Flush( 0 );
		debugf( TEXT("ufront: persisted render device %s as a user selection (sticks next boot)"), *GRequestedRenderDevice );
	}
	else
	{
		debugf( NAME_Warning, TEXT("ufront: render device changed from %s during init -> not persisted"), *GRequestedRenderDevice );
	}
}

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
	// A browser has no BSD sockets: the stock TcpNetDriver becomes WebRTCDrv, over the glue ABI.
	{
		FString Net;
		if( GConfig->GetString( TEXT("Engine.Engine"), TEXT("NetworkDevice"), Net ) && appStricmp( *Net, TEXT("IpDrv.TcpNetDriver") )==0 )
		{
			debugf( TEXT("ufront: [Engine.Engine] NetworkDevice=IpDrv.TcpNetDriver -> WebRTCDrv.WebRTCNetDriver (no BSD sockets in a browser)") );
			GConfig->SetString( TEXT("Engine.Engine"), TEXT("NetworkDevice"), TEXT("WebRTCDrv.WebRTCNetDriver") );
		}
	}
	// The pointer is locked while a level is up and released while a menu shows (UGameEngine::Tick drives
	// SetMouseCapture; SDL's Emscripten port takes the lock on the next click).
	GConfig->SetString( TEXT("NSDLDrv.NSDLClient"),  TEXT("CaptureMouse"),    TEXT("True") );
	// The multiplayer Server tab's internet services start off: world and local stats logging (the batcher path
	// ends in appCreateProc, a fork/exec a browser cannot do) and the master-server uplink (UDP). The checkboxes
	// still work.
	GConfig->SetString( TEXT("Engine.GameInfo"),           TEXT("bWorldLog"), TEXT("False") );
	GConfig->SetString( TEXT("Engine.GameInfo"),           TEXT("bLocalLog"), TEXT("False") );
	GConfig->SetString( TEXT("IpServer.UdpServerUplink"),  TEXT("DoUplink"),  TEXT("False") );
#endif
#if defined(__ANDROID__)
	// The stats logging ends in appCreateProc, a fork/exec of a batcher an app does not carry.
	GConfig->SetString( TEXT("Engine.GameInfo"),           TEXT("bWorldLog"), TEXT("False") );
	GConfig->SetString( TEXT("Engine.GameInfo"),           TEXT("bLocalLog"), TEXT("False") );
	// gl4es, OpenGLDrv/UTGLR's GL layer, reads these when the device starts it: no precompiled-shader archive in the
	// home folder, and on the emulator, whose GLES translator corrupts GL_BGRA uploads, BGRA swizzled by gl4es.
	setenv( "LIBGL_NOPSA", "1", 1 );
	{
		char Qemu[PROP_VALUE_MAX] = {0}, Hardware[PROP_VALUE_MAX] = {0};
		__system_property_get( "ro.kernel.qemu", Qemu );
		__system_property_get( "ro.hardware", Hardware );
		if( Qemu[0]=='1' || strstr( Hardware, "goldfish" ) || strstr( Hardware, "ranchu" ) )
		{
			debugf( TEXT("OpenGLDrv: Android emulator detected -> LIBGL_NOBGRA=1 (gl4es swizzles BGRA uploads)") );
			setenv( "LIBGL_NOBGRA", "1", 1 );
		}
	}
#endif

	// Replace the subsystems the ini names but this build lacks (the retail WinDrv.WindowsClient and
	// Galaxy.GalaxyAudioSubsystem), before Engine->Init() loads them. Each list is best real driver first.
	{
		static const TCHAR* const ViewportPreference[] = { TEXT("NSDLDrv.NSDLClient") };
		static const TCHAR* const AudioPreference[]    = { TEXT("NOpenALDrv.NOpenALAudioSubsystem"),
		                                                   TEXT("SoundDrv.NullAudioSubsystem") };
#ifdef __EMSCRIPTEN__
		static const TCHAR* const NetPreference[]      = { TEXT("WebRTCDrv.WebRTCNetDriver") };
#else
		static const TCHAR* const NetPreference[]      = { TEXT("IpDrv.TcpNetDriver") };
#endif
		NormalizeSubsystem( TEXT("Engine.Engine"), TEXT("ViewportManager"), ViewportPreference, ARRAY_COUNT(ViewportPreference) );
		NormalizeSubsystem( TEXT("Engine.Engine"), TEXT("AudioDevice"),     AudioPreference,    ARRAY_COUNT(AudioPreference) );
		NormalizeSubsystem( TEXT("Engine.Engine"), TEXT("NetworkDevice"),   NetPreference,      ARRAY_COUNT(NetPreference) );
	}

	// A -*RENDERER switch, else the persisted choice, else the preference order.
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
	// Drop the game ini's ServerActors (UdpBeacon, UdpServerQuery, the master-server uplinks, UWeb.WebServer):
	// UDP and web-admin actors a browser listen server cannot run. After ConstructObject, because the array is
	// copied from the class default object, and before Init(), which starts the listen path.
	if( Engine->IsA(UGameEngine::StaticClass()) )
		((UGameEngine*)Engine)->ServerActors.Empty();
#endif
	Engine->Init();
	// Persist a -*RENDERER choice that survived Init.
	PersistRenderChoiceIfSwitched();
#ifdef PLATFORM_DREAMCAST
	malloc_stats();
#endif
#ifdef __EMSCRIPTEN__
	// The running engine, for WebRTCDrv's listen-status push and ue_exec_command.
	GEngineForExec = Engine;
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
#ifdef __EMSCRIPTEN__
		// Declared outside the guard() blocks, each of which opens its own scope.
		DOUBLE PaceFrameStart = 0.0, PaceTickEnd = 0.0;
#endif
		// Update the world.
		guard(UpdateWorld);
		DOUBLE NewTime   = appSeconds();
		FLOAT  DeltaTime = NewTime - OldTime;
		Engine->Tick( DeltaTime );
		if( GWindowManager )
			GWindowManager->Tick( DeltaTime );
#ifdef __EMSCRIPTEN__
		PaceFrameStart = NewTime;
		PaceTickEnd    = appSeconds(); // end of simulate+render, start of the yield
#endif
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

		// Client frame-rate cap. GetMaxTickRate() is 0 in standalone play; then [Engine.Engine] FrameRateCap applies
		// (120 by default, 0 or negative = uncapped). A net or demo max tick rate wins.
		FLOAT Cap = MaxTickRate;
		if( Cap<=0.0 )
		{
			static FLOAT ClientCap = -1.f;
			if( ClientCap < -0.5f )
			{
				FLOAT V=0.f;
				// Read once; from outside it is set with -ini:System:[Engine.Engine]:FrameRateCap=<n>.
				ClientCap = ( GConfig->GetFloat(TEXT("Engine.Engine"),TEXT("FrameRateCap"),V) ) ? V : 120.f;
				debugf( NAME_Init, TEXT("ufront: client frame rate cap %.0f fps%s"), ClientCap, ClientCap>0.f ? TEXT("") : TEXT(" (uncapped)") );
			}
			Cap = ClientCap;
		}
#ifdef __EMSCRIPTEN__
		// The browser's event loop must run every frame, so the loop waits through Asyncify until the next frame
		// may start (UfrontWaitFrame). (emscripten_set_main_loop's unwind would collide with main()'s try/catch.)
		{
			const INT SleepReqMs = UfrontWaitFrame( Cap>0.0 ? (1.0/Cap) : 0.0 );
			// One pacing sample per frame, after the wait so YieldMs covers the present point.
			const DOUBLE PaceEnd = appSeconds();
			UfrontPaceRecord( PaceFrameStart, (FLOAT)((PaceTickEnd-PaceFrameStart)*1000.0), (FLOAT)((PaceEnd-PaceTickEnd)*1000.0), SleepReqMs );
		}
#else
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
// Entry point. On Android SDL_main (below) runs this on a thread of its own.
//
#if defined(__ANDROID__)
static int UT99Main( int argc, char* argv[] )
#else
int main( int argc, char* argv[] )
#endif
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
#if defined(__ANDROID__)
	// argv[0] is the host process's name (app_process), not the game's: the log file and appPackage() take
	// the game's own.
	appStrcpy( GModule, "UnrealTournament" );
#elif !defined(PLATFORM_DREAMCAST)
	// Set module name: the file name of argv[0] without ".exe", bounded (GModule holds 32 characters), so the log is
	// UnrealTournament.log however the game is started, by a relative, an absolute or a Windows path.
	{
		const char* Base = argv[0];
		for( const char* P=argv[0]; *P; P++ )
			if( *P=='/' || *P=='\\' || *P==':' )
				Base = P+1;
		appStrncpy( GModule, Base, ARRAY_COUNT(GModule) );
		INT Len = appStrlen( GModule );
		if( Len>4 && appStricmp( GModule+Len-4, TEXT(".exe") )==0 )
			GModule[Len-4] = 0;
	}

	// Set the package name.
	appStrcpy( THIS_PACKAGE, appPackage() );	
#endif
	// Get the command line: up to 4096 characters, the size of Core's GCmdLine. A token that does not fit is
	// dropped whole, with a warning once the log is up.
	TCHAR CmdLine[4096], *CmdLinePtr=CmdLine;
	*CmdLinePtr = 0;
	UBOOL CmdLineCut = 0;
	for( INT i=1; i<argc; i++ )
	{
		if( appStrlen(CmdLine) + appStrlen(argv[i]) + 2 >= ARRAY_COUNT(CmdLine) )
		{
			CmdLineCut = 1;
			break;
		}
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
	// The game runs from System/ like a native install: the inis are preloaded at /System and the package Paths
	// (../System, ../Maps, ...) resolve from appBaseDir(), which appInit() takes from getcwd().
	chdir( "/System" );
#endif
	appInit( TEXT("UnrealTournament"), CmdLine, &Malloc, &Log, &Error, &Warn, &FileManager, FConfigCacheIni::Factory, 1 );
	if( CmdLineCut )
		debugf( NAME_Warning, TEXT("The command line is longer than %i characters: the arguments after '%s' were dropped"), ARRAY_COUNT(CmdLine), CmdLine + Max(0, appStrlen(CmdLine) - 40) );

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

#if defined(__ANDROID__)
//
// Android: SDLActivity loads libUT99.so and calls SDL_main on its SDL thread, whose stack is Android's default
// 1 MB. The engine runs on a thread of its own with the 8 MB a Linux main thread has (the script VM and the
// level code recurse deeply); SDL_main waits for it. SDL's Android calls attach whichever thread makes them.
//
struct FUT99MainArgs { int argc; char** argv; int Result; };
static void* UT99MainThread( void* Arg )
{
	FUT99MainArgs* Args = (FUT99MainArgs*)Arg;
	Args->Result = UT99Main( Args->argc, Args->argv );
	return NULL;
}
extern "C" __attribute__((visibility("default"))) int SDL_main( int argc, char** argv )
{
	AndroidRedirectStdio();
	FUT99MainArgs Args = { argc, argv, 1 };
	pthread_attr_t Attr;
	pthread_attr_init( &Attr );
	pthread_attr_setstacksize( &Attr, 8 * 1024 * 1024 );
	pthread_t Thread;
	if( pthread_create( &Thread, &Attr, UT99MainThread, &Args ) != 0 )
	{
		__android_log_write( ANDROID_LOG_ERROR, "UT99", "could not start the engine thread: running it on the SDL thread" );
		Args.Result = UT99Main( argc, argv );
	}
	else
		pthread_join( Thread, NULL );
	pthread_attr_destroy( &Attr );
	return Args.Result;
}
#endif