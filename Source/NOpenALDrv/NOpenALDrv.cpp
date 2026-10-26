#include <stdlib.h>

// One code path on every target: the web links the openal-soft fork too (cmake/OpenALSoft.cmake: mixed on a
// pthread, played by an AudioWorklet). Music is pumped from Update(); there is no decoder thread.

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"
// EFX is resolved at run time, never linked (no AL_ALEXT_PROTOTYPES), so the driver links against an OpenAL
// without EFX and plays everything but reverb there. The pointers live in `Efx` below.
#if defined(__has_include)
#  if __has_include("AL/efx.h")
#    define NOPENAL_HAVE_EFX_HEADERS 1
#  endif
#else
#  define NOPENAL_HAVE_EFX_HEADERS 1   // a compiler with no __has_include: assume the usual SDK layout
#endif
#if NOPENAL_HAVE_EFX_HEADERS
#include "AL/efx.h"
#include "AL/efx-presets.h"
#else
// A core-only SDK (no <AL/efx.h>): spell out the handful of types and enums this driver uses, so it still
// compiles. The EAX reverb preset table is efx-presets.h's and is not reproduced — without those headers the
// EAX path compiles out and there is no reverb.
#define NOPENAL_NO_EAX 1
typedef void (AL_APIENTRY *LPALGENEFFECTS)( ALsizei, ALuint* );
typedef void (AL_APIENTRY *LPALDELETEEFFECTS)( ALsizei, const ALuint* );
typedef void (AL_APIENTRY *LPALEFFECTI)( ALuint, ALenum, ALint );
typedef void (AL_APIENTRY *LPALEFFECTF)( ALuint, ALenum, ALfloat );
typedef void (AL_APIENTRY *LPALEFFECTFV)( ALuint, ALenum, const ALfloat* );
typedef void (AL_APIENTRY *LPALGENAUXILIARYEFFECTSLOTS)( ALsizei, ALuint* );
typedef void (AL_APIENTRY *LPALDELETEAUXILIARYEFFECTSLOTS)( ALsizei, const ALuint* );
typedef void (AL_APIENTRY *LPALAUXILIARYEFFECTSLOTI)( ALuint, ALenum, ALint );
#define AL_EFFECT_TYPE                           0x8001
#define AL_EFFECT_NULL                           0x0000
#define AL_EFFECTSLOT_EFFECT                     0x0001
#define AL_FILTER_NULL                           0x0000
#define AL_AUXILIARY_SEND_FILTER                 0x20006
#define AL_METERS_PER_UNIT                       0x20004
#endif
// openal-soft's output-mode extension (1.23+); spelled out for older alext.h headers a native build may find.
#ifndef ALC_OUTPUT_MODE_SOFT
#define ALC_OUTPUT_MODE_SOFT 0x19AC
#endif
#ifndef ALC_ANY_SOFT
#define ALC_ANY_SOFT 0x19AD
#endif
#ifndef ALC_STEREO_HRTF_SOFT
#define ALC_STEREO_HRTF_SOFT 0x19B2
#endif
#include "xmp.h"
#if defined(__ANDROID__)
#include "SDL2/SDL.h"
#endif

#include "NOpenALDrvPrivate.h"
#include "UnRender.h"

/*-----------------------------------------------------------------------------
	Global implementation.
-----------------------------------------------------------------------------*/

IMPLEMENT_PACKAGE(NOpenALDrv);
IMPLEMENT_CLASS(UNOpenALAudioSubsystem);

// EFX's entry points, resolved once in Init. `Present` is the test the rest of the driver makes: the device
// advertises ALC_EXT_EFX and every pointer resolved. File-static, so the native class's script-visible layout
// gains no member.
static struct FNOpenALEfx
{
	LPALGENEFFECTS                 GenEffects;
	LPALDELETEEFFECTS              DeleteEffects;
	LPALEFFECTI                    Effecti;
	LPALEFFECTF                    Effectf;
	LPALEFFECTFV                   Effectfv;
	LPALGENAUXILIARYEFFECTSLOTS    GenAuxiliaryEffectSlots;
	LPALDELETEAUXILIARYEFFECTSLOTS DeleteAuxiliaryEffectSlots;
	LPALAUXILIARYEFFECTSLOTI       AuxiliaryEffectSloti;
	UBOOL                          Present;
} Efx;

// Resolve them. Call with the context current: alGetProcAddress is context-dependent.
static UBOOL ResolveEfx( ALCdevice* InDevice )
{
	appMemzero( &Efx, sizeof(Efx) );
	if( !alcIsExtensionPresent( InDevice, "ALC_EXT_EFX" ) )
		return 0;
	Efx.GenEffects                 = (LPALGENEFFECTS)                 alGetProcAddress( "alGenEffects" );
	Efx.DeleteEffects              = (LPALDELETEEFFECTS)              alGetProcAddress( "alDeleteEffects" );
	Efx.Effecti                    = (LPALEFFECTI)                    alGetProcAddress( "alEffecti" );
	Efx.Effectf                    = (LPALEFFECTF)                    alGetProcAddress( "alEffectf" );
	Efx.Effectfv                   = (LPALEFFECTFV)                   alGetProcAddress( "alEffectfv" );
	Efx.GenAuxiliaryEffectSlots    = (LPALGENAUXILIARYEFFECTSLOTS)    alGetProcAddress( "alGenAuxiliaryEffectSlots" );
	Efx.DeleteAuxiliaryEffectSlots = (LPALDELETEAUXILIARYEFFECTSLOTS) alGetProcAddress( "alDeleteAuxiliaryEffectSlots" );
	Efx.AuxiliaryEffectSloti       = (LPALAUXILIARYEFFECTSLOTI)       alGetProcAddress( "alAuxiliaryEffectSloti" );
	// All or nothing: a partial table would fault on the first missing one, at a random later moment.
	Efx.Present = Efx.GenEffects && Efx.DeleteEffects && Efx.Effecti && Efx.Effectf && Efx.Effectfv
		&& Efx.GenAuxiliaryEffectSlots && Efx.DeleteAuxiliaryEffectSlots && Efx.AuxiliaryEffectSloti;
	return Efx.Present;
}

#if defined(__ANDROID__)
// The app in the background: SDL's event pump blocks the game thread until it returns, and the output device is
// paused (ALC_SOFT_pause_device), so nothing plays and openal-soft's mixer thread stops. SDL sends both events
// from that pump, on the game thread: the first just before it blocks, the second right after it wakes.
static LPALCDEVICEPAUSESOFT  AlcDevicePause  = NULL;
static LPALCDEVICERESUMESOFT AlcDeviceResume = NULL;

static int SDLCALL PauseInBackground( void* Device, SDL_Event* Event )
{
	if( Event->type == SDL_APP_DIDENTERBACKGROUND )
	{
		AlcDevicePause( (ALCdevice*)Device );
		debugf( TEXT("NOpenALDrv: output paused (the app is in the background)") );
	}
	else if( Event->type == SDL_APP_WILLENTERFOREGROUND )
	{
		AlcDeviceResume( (ALCdevice*)Device );
		debugf( TEXT("NOpenALDrv: output resumed") );
	}
	return 1;
}

static void WatchAppBackground( ALCdevice* InDevice, UBOOL On )
{
	if( !On )
	{
		SDL_DelEventWatch( PauseInBackground, InDevice );
		return;
	}
	AlcDevicePause  = alcIsExtensionPresent( InDevice, "ALC_SOFT_pause_device" ) ? (LPALCDEVICEPAUSESOFT)alcGetProcAddress( InDevice, "alcDevicePauseSOFT" ) : NULL;
	AlcDeviceResume = AlcDevicePause ? (LPALCDEVICERESUMESOFT)alcGetProcAddress( InDevice, "alcDeviceResumeSOFT" ) : NULL;
	if( AlcDevicePause && AlcDeviceResume )
		SDL_AddEventWatch( PauseInBackground, InDevice );
	else
		debugf( NAME_Warning, TEXT("NOpenALDrv: this OpenAL cannot pause its device (no ALC_SOFT_pause_device): sound plays on in the background") );
}
#else
static void WatchAppBackground( ALCdevice* InDevice, UBOOL On ) {}
#endif


/*-----------------------------------------------------------------------------
	UNOpenALAudioSubsystem implementation.
-----------------------------------------------------------------------------*/

void UNOpenALAudioSubsystem::StaticConstructor()
{
	guardSlow(UNOpenALAudioSubsystem::StaticConstructor);
	// The config properties (v400 calls StaticConstructor at class registration); the UWindow audio menu reads
	// and writes them with `get/set ini:Engine.Engine.AudioDevice`. DeviceName is not registered (a fixed char[]):
	// it stays "", the default AL device.
	new(GetClass(), TEXT("OutputRate"),         RF_Public)UIntProperty   ( CPP_PROPERTY( OutputRate         ), TEXT("Audio"), CPF_Config );
	new(GetClass(), TEXT("MusicVolume"),        RF_Public)UByteProperty  ( CPP_PROPERTY( MusicVolume        ), TEXT("Audio"), CPF_Config );
	new(GetClass(), TEXT("SoundVolume"),        RF_Public)UByteProperty  ( CPP_PROPERTY( SoundVolume        ), TEXT("Audio"), CPF_Config );
	new(GetClass(), TEXT("MasterVolume"),       RF_Public)UByteProperty  ( CPP_PROPERTY( MasterVolume       ), TEXT("Audio"), CPF_Config );
	new(GetClass(), TEXT("AmbientFactor"),      RF_Public)UFloatProperty ( CPP_PROPERTY( AmbientFactor      ), TEXT("Audio"), CPF_Config );
	new(GetClass(), TEXT("DopplerFactor"),      RF_Public)UFloatProperty ( CPP_PROPERTY( DopplerFactor      ), TEXT("Audio"), CPF_Config );
	new(GetClass(), TEXT("UseReverb"),          RF_Public)UBoolProperty  ( CPP_PROPERTY( UseReverb          ), TEXT("Audio"), CPF_Config );
	new(GetClass(), TEXT("UseHRTF"),            RF_Public)UBoolProperty  ( CPP_PROPERTY( UseHRTF            ), TEXT("Audio"), CPF_Config );
	new(GetClass(), TEXT("MusicInterpolation"), RF_Public)UByteProperty  ( CPP_PROPERTY( MusicInterpolation ), TEXT("Audio"), CPF_Config );
	unguardSlow;
}

UNOpenALAudioSubsystem::UNOpenALAudioSubsystem()
{
	OutputRate = DEFAULT_OUTPUT_RATE;
	MasterVolume = 255;
	SoundVolume = 127;
	MusicVolume = 63;
	AmbientFactor = 0.6f;
	DopplerFactor = 0.01f;
	UseHRTF = false;	// the player's choice (see Init)
	UseReverb = true;
	ReverbKind = REVERB_EAX;
	MusicInterpolation = XMP_INTERP_LINEAR;
}

UBOOL UNOpenALAudioSubsystem::Init()
{
	guard(UNOpenALAudioSubsystem::Init)

	// The constructor's defaults overwrite the ini values the class defaults carried, so read the
	// [NOpenALDrv.NOpenALAudioSubsystem] section onto this instance again (only CPF_Config properties).
	LoadConfig();

	Viewport = NULL;
	Device = NULL;
	if( DeviceName[0] )
		Device = alcOpenDevice( DeviceName );
	if( !Device )
		Device = alcOpenDevice( NULL );
	if( !Device )
	{
		debugf( NAME_Warning, "Could not open AL device: %04x", alcGetError( NULL ) );
		return false;
	}

	if( OutputRate <= 0 )
		OutputRate = DEFAULT_OUTPUT_RATE;
	
	if( DopplerFactor < 0.f )
		DopplerFactor = 0.f;

	AmbientFactor = Clamp( AmbientFactor, 0.f, 1.f );

	if( MusicInterpolation > XMP_INTERP_SPLINE )
		MusicInterpolation = XMP_INTERP_SPLINE;

	// HRTF is a stereo rendering: a surround device asked for HRTF alone renders surround and reports
	// ALC_HRTF_UNSUPPORTED_FORMAT_SOFT, so UseHRTF also asks for the stereo-HRTF output mode. Off by default: no
	// browser can tell headphones from speakers, so it is the player's choice.
	const ALint AttrList[] = {
		ALC_FREQUENCY, OutputRate,
		ALC_HRTF_SOFT, UseHRTF ? ALC_TRUE : ALC_FALSE,
		ALC_OUTPUT_MODE_SOFT, UseHRTF ? ALC_STEREO_HRTF_SOFT : ALC_ANY_SOFT,
		0
	};

	Ctx = alcCreateContext( Device, AttrList );
	if( !Ctx )
	{
		debugf( NAME_Warning, "Could not create AL context: %04x", alcGetError( Device ) );
		alcCloseDevice( Device );
		Device = NULL;
		return false;
	}

	alcMakeContextCurrent( Ctx );
	WatchAppBackground( Device, 1 );

	// With EFX, reverb is EFX's EAX reverb; without it everything except reverb plays (the context is current,
	// so the line below can report it).
	ResolveEfx( Device );
	ReverbKind = REVERB_EAX;
	if( UseReverb && !Efx.Present )
	{
		ReverbKind = REVERB_NONE;
		debugf( TEXT("NOpenALDrv: this OpenAL has no EFX (ALC_EXT_EFX) - reverb is off; everything else plays") );
	}
#if NOPENAL_NO_EAX
	if( ReverbKind == REVERB_EAX && UseReverb )
	{
		ReverbKind = REVERB_NONE;
		debugf( TEXT("NOpenALDrv: built without the EFX headers - reverb is off") );
	}
#endif

	{
		// Read back what the library did: a request is not a result.
		ALCint HRTFActive = 0;
		alcGetIntegerv( Device, ALC_HRTF_SOFT, 1, &HRTFActive );
		debugf( TEXT("NOpenALDrv: HRTF %s (UseHRTF=%s), reverb %s"), HRTFActive ? TEXT("ACTIVE") : TEXT("inactive"),
			UseHRTF ? TEXT("True") : TEXT("False"),
			!UseReverb ? TEXT("off (UseReverb=False)") : ReverbKind == REVERB_NONE ? TEXT("off (no EFX in this OpenAL)") : TEXT("on (EAX)") );
	}

	alDistanceModel( AL_LINEAR_DISTANCE_CLAMPED );
	alDopplerFactor( Max( 0.f, DopplerFactor ) );
	if( Efx.Present )
		alListenerf( AL_METERS_PER_UNIT, DISTANCE_SCALE );   // an EFX listener property: AL_INVALID_ENUM without it
	alListenerf( AL_GAIN, MasterVolume / 255.f );

	alGenSources( MAX_SOURCES, Sources );

	alGenSources( 1, &MusicSource	);
	alSourcei( MusicSource, AL_SOURCE_RELATIVE, AL_TRUE );
	alSource3f( MusicSource, AL_POSITION, 0.f, 0.f, 0.f );
	alSourcef( MusicSource, AL_ROLLOFF_FACTOR, 0.f );
	alSourcef( MusicSource, AL_GAIN, MusicVolume / 255.f );

	alGenBuffers( ARRAY_COUNT( MusicBuffers ), MusicBuffers );
	for( INT i = 0; i < ARRAY_COUNT( MusicBuffers ); ++i )
	{
		alBufferData( MusicBuffers[i], AL_FORMAT_STEREO16, MusicBufferData, sizeof( MusicBufferData ), OutputRate );
		FreeMusicBuffers[i] = MusicBuffers[i];
	}
	NumFreeMusicBuffers = NUM_MUSIC_BUFFERS;

	if( UseReverb && ReverbKind != REVERB_NONE )
	{
		Efx.GenEffects( 1, &ReverbEffect );
		InitReverbEffect();
		Efx.GenAuxiliaryEffectSlots( 1, &ReverbSlot );
		Efx.AuxiliaryEffectSloti( ReverbSlot, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL );
	}
	ReverbOn = false;

	for( INT i = 0; i < MAX_SOURCES; ++i )
		Voices[i].Buffer = INVALID_BUFFER;

	MusicCtx = xmp_create_context();
	xmp_set_player( MusicCtx, XMP_PLAYER_INTERP, MusicInterpolation );

	// Set ourselves up as the audio subsystem.
	USound::Audio = this;
	UMusic::Audio = this;

	// Music is pumped from Update(); there is no decoder thread.

	return true;

	unguard;
}

void UNOpenALAudioSubsystem::Destroy()
{
	guard(UNOpenALAudioSubsystem::Destroy)

	USound::Audio = NULL;
	UMusic::Audio = NULL;

	if( MusicCtx )
	{
		xmp_end_player( MusicCtx );
		xmp_free_context( MusicCtx );
		MusicCtx = NULL;
	}

	if( UseReverb && ReverbKind != REVERB_NONE && Efx.Present )
	{
		Efx.DeleteAuxiliaryEffectSlots(1, &ReverbSlot);
		Efx.DeleteEffects(1, &ReverbEffect);
	}

	if( Ctx )
	{
		// If we have a context, we probably have everything else. Kill it.
		SetViewport( NULL ); // This will also stop all sounds.
		alDeleteBuffers( Buffers.Num(), &Buffers(0) );
		alDeleteBuffers( ARRAY_COUNT( MusicBuffers ), MusicBuffers );
		alDeleteSources( MAX_SOURCES, Sources );
		alDeleteSources( 1, &MusicSource );
		alcMakeContextCurrent( NULL );
		alcDestroyContext( Ctx );
		Ctx = NULL;
		Buffers.Empty();
	}

	if( Device )
	{
		// We might still have a device even if we haven't created a context.
		WatchAppBackground( Device, 0 );
		alcCloseDevice( Device );
		Device = NULL;
	}

	Super::Destroy();

	unguard;
}

void UNOpenALAudioSubsystem::ShutdownAfterError()
{
	guard(UNOpenALAudioSubsystem::Destroy)

	USound::Audio = NULL;
	UMusic::Audio = NULL;

	// Shutdown contexts without touching anything else.
	if( MusicCtx )
	{
		xmp_free_context( MusicCtx );
		MusicCtx = NULL;
		Music = NULL;
	}
	if( UseReverb && ReverbKind != REVERB_NONE && Efx.Present )
	{
		Efx.DeleteAuxiliaryEffectSlots(1, &ReverbSlot);
		Efx.DeleteEffects(1, &ReverbEffect);
	}
	if( Ctx )
	{
		alcMakeContextCurrent( NULL );
		alcDestroyContext( Ctx );
		Ctx = NULL;
	}
	if( Device )
	{
		WatchAppBackground( Device, 0 );
		alcCloseDevice( Device );
		Device = NULL;
	}

	Super::ShutdownAfterError();

	unguard;
}

void UNOpenALAudioSubsystem::PostEditChange()
{
	guard(UNOpenALAudioSubsystem::PostEditChange)

	Super::PostEditChange();

	FScopedLock Lock( MusicMutex );

	if( DopplerFactor < 0.f )
		DopplerFactor = 0.f;
	AmbientFactor = Clamp( AmbientFactor, 0.f, 1.f );
	MusicInterpolation = Clamp( MusicInterpolation, (BYTE)0, (BYTE)XMP_INTERP_SPLINE );

	if( Ctx )
	{
		alListenerf( AL_GAIN, MasterVolume / 255.f );
		alDopplerFactor( DopplerFactor );
		alSourcef( MusicSource, AL_GAIN, Max(MusicFade, 0.f) * MusicVolume / 255.f );
		// Voice volumes will be updated in Update().
	}

	if( MusicCtx )
	{
		xmp_set_player( MusicCtx, XMP_PLAYER_INTERP, MusicInterpolation );
	}

	unguard;
}

void UNOpenALAudioSubsystem::SetViewport( UViewport* InViewport )
{
	guard(UNOpenALAudioSubsystem::SetViewport)

	// Stop all sounds before viewport change.
	for( INT i = 0; i < MAX_SOURCES; ++i )
		StopVoice( i );

	// Stop and free music if the viewport has changed.
	if( InViewport != Viewport )
	{
		if( Music )
		{
			UnregisterMusic( Music );
			Music = NULL;
		}
	}

	Viewport = InViewport;

	unguard;
}

void UNOpenALAudioSubsystem::RegisterMusic( UMusic* Music )
{
	guard(UNOpenALAudioSubsystem::RegisterMusic)

	FScopedLock Lock( MusicMutex );

	if( Music->Handle || !Music->Data.Num() )
		return;

	INT Err = xmp_load_module_from_memory( MusicCtx, &Music->Data(0), Music->Data.Num() );
	if( Err < 0 )
	{
		debugf( NAME_Warning, "Couldn't load music `%s`: %d", Music->GetName(), Err );
		return;
	}

	Err = xmp_start_player( MusicCtx, OutputRate, 0 );
	if( Err < 0 )
	{
		xmp_release_module( MusicCtx );
		debugf( NAME_Warning, "Couldn't start player on `%s`: %d", Music->GetName(), Err );
		return;
	}

	Music->Handle = (void*)1;
	MusicIsLoaded = true;

	unguard;
}

void UNOpenALAudioSubsystem::UnregisterMusic( UMusic* Music )
{
	guard(UNOpenALAudioSubsystem::UnregisterMusic)

	FScopedLock Lock( MusicMutex );

	StopMusic();
	ClearMusicBuffers();
	if( MusicCtx )
	{
		xmp_end_player( MusicCtx );
		if( MusicIsLoaded )
			xmp_release_module( MusicCtx );
	}

	MusicIsLoaded = false;

	unguard;
}

void UNOpenALAudioSubsystem::RegisterSound( USound* Sound )
{
	guard(UNOpenALAudioSubsystem::RegisterSound)

	if( Sound->Handle )
		return;

	check( Sound->Data.Num() );

	FWaveModInfo WaveInfo;
	if( !WaveInfo.ReadWaveInfo( Sound->Data ) )
	{
		debugf( NAME_Warning, "Sound %s is not a valid WAV file", Sound->GetName() );
		return;
	}

	ALuint Buf = 0;
	alGenBuffers( 1, &Buf );
	Buffers.AddItem( Buf );

	ALenum Format = AL_FORMAT_MONO8;
	if( *WaveInfo.pChannels == 2 )
	{
		if( *WaveInfo.pBitsPerSample == 16 )
			Format = AL_FORMAT_STEREO16;
		else
			Format = AL_FORMAT_STEREO8;
	}
	else
	{
		if( *WaveInfo.pBitsPerSample == 16 )
			Format = AL_FORMAT_MONO16;
		else
			Format = AL_FORMAT_MONO8;
	}

	alBufferData( Buf, Format, (const void*)WaveInfo.SampleDataStart, WaveInfo.SampleDataSize, *WaveInfo.pSamplesPerSec );

	Sound->Handle = (void*)(UPTRINT)Buf; // An AL buffer id parked in the engine's pointer-sized handle.
	// v400 USound has no Looping field (v200 did). Looping is decided per-voice by sound slot at PlaySound
	// time (ambient sounds loop), matching UT99's UGenericAudioSubsystem — see PlaySound below.

	if( !GIsEditor )
		Sound->Data.Empty();

	unguard;
}

void UNOpenALAudioSubsystem::UnregisterSound( USound* Sound )
{
	guard(UNOpenALAudioSubsystem::UnregisterSound)

	check( Sound );

	if( Sound->Handle )
	{
		ALuint Buf = (ALuint)(UPTRINT)Sound->Handle;
		check( alIsBuffer( Buf ) );

		for( INT i = 0; i < MAX_SOURCES; ++i )
		{
			if( Voices[i].Sound == Sound )
				StopVoice( i );
		}

		Buffers.RemoveItem( Buf );
		alDeleteBuffers( 1, &Buf );

		Sound->Handle = NULL;
	}

	unguard;
}

void UNOpenALAudioSubsystem::UpdateVoice( INT Num, const ENVoiceOp Op )
{
	guard(UNOpenALAudioSubsystem::UpdateVoice)

	FNVoice& Voice = Voices[Num];

	// Swap position and velocity into AL space.
	FVector ALVelocity;
	ALVelocity.X = Voice.Velocity.X;
	ALVelocity.Y = Voice.Velocity.Y;
	ALVelocity.Z = -Voice.Velocity.Z;

	// If the source is close enough to the listener, don't spatialize it.
	DWORD SourceRelative;
	FVector ALLocation;
	if( FDistSquared( ListenerCoords.Origin, Voice.Location ) < Square( Voice.Radius * DESPATIALIZE_FACTOR ) )
	{
		SourceRelative = AL_TRUE;
		ALLocation.X = 0.f;
		ALLocation.Y = 0.f;
		ALLocation.Z = 0.f;
		// If the source is ON the listener, also reset the velocity so we don't doppler our own voice.
		if( Viewport && Viewport->Actor && Voice.Actor == Viewport->Actor )
		{
			ALVelocity.X = 0.f;
			ALVelocity.Y = 0.f;
			ALVelocity.Z = 0.f;
		}
	}
	else
	{
		SourceRelative = AL_FALSE;
		ALLocation.X = Voice.Location.X;
		ALLocation.Y = Voice.Location.Y;
		ALLocation.Z = -Voice.Location.Z;
	}

	// Set up AL source.
	ALuint Source = Sources[Num];
	alSourcei( Source, AL_SOURCE_RELATIVE, SourceRelative );
	alSourcef( Source, AL_GAIN, Voice.Volume * ( SoundVolume / 255.f ) );
	alSourcef( Source, AL_PITCH, Voice.Pitch );
	alSourcef( Source, AL_MAX_DISTANCE, Voice.Radius );
	alSourcef( Source, AL_REFERENCE_DISTANCE, Voice.Radius * DESPATIALIZE_FACTOR );
	alSourcef( Source, AL_ROLLOFF_FACTOR, ROLLOFF_FACTOR );
	alSourcefv( Source, AL_POSITION, &ALLocation.X );
	alSourcefv( Source, AL_VELOCITY, &ALVelocity.X );
	alSourcei( Source, AL_LOOPING, Voice.Looping );

	if( Voice.BufferChanged )
	{
		Voice.BufferChanged = false;
		INT State = AL_STOPPED;
		alGetSourcei( Source, AL_SOURCE_STATE, &State );
		if( State == AL_PLAYING || State == AL_PAUSED )
			alSourceStop( Source );
		alSourcei( Source, AL_BUFFER, Voice.Buffer );
		if( State == AL_PLAYING || State == AL_PAUSED )
		{
			alSourcePlay( Source );
			if( State == AL_PAUSED )
				alSourcePause( Source );
		}
	}

	if( UseReverb && ReverbKind != REVERB_NONE && Op == NVOP_Play )
		alSource3i( Source, AL_AUXILIARY_SEND_FILTER, (ALint)ReverbSlot, 0, AL_FILTER_NULL );

	// Play or stop if needed.
	switch( Op )
	{
		case ENVoiceOp::NVOP_Play:  alSourcePlay( Source ); break;
		case ENVoiceOp::NVOP_Pause: alSourcePause( Source ); break;
		case ENVoiceOp::NVOP_Stop:  alSourceStop( Source ); break;
		default: break;
	}

	unguard;
}

UBOOL UNOpenALAudioSubsystem::PlaySound( AActor* Actor, INT Id, USound* Sound, FVector Location, FLOAT Volume, FLOAT Radius, FLOAT Pitch )
{
	guard(UNOpenALAudioSubsystem::PlaySound)

	if( !Viewport )
		return false;

	// Register the sound the first time it is played, as UGenericAudioSubsystem::GetSound() does: packages can load
	// before the audio subsystem exists, so the load-time RegisterSound hook (UnAudio.cpp) may not have run.
	if( Sound && !Sound->Handle && Sound->Data.Num() )
		RegisterSound( Sound );

	// Allocate a new slot if requested.
	if( SOUND_SLOT_IS( Id, SLOT_None ) )
		Id = 16 * --NextId;

	FLOAT Priority = GetVoicePriority( Location, Volume, Radius );
	FLOAT MaxPriority = Priority;
	FNVoice* Voice = NULL;
	for( INT i = 0; i < MAX_SOURCES; ++i )
	{
		FNVoice* V = &Voices[i];
		if( ( V->Id & ~1 ) == ( Id & ~1 ) )
		{
			// Skip if not interruptable.
			if( Id & 1 )
				return false;
			StopVoice( i );
			Voice = V;
			break;
		}
		else if( V->Priority <= MaxPriority )
		{
			MaxPriority = V->Priority;
			Voice = V;
		}
	}

	// If we ran out of voices or the sound is too low priority, bail.
	if( !Voice || !Sound || !Sound->Handle )
		return false;

	ALuint Buf = (ALuint)(UPTRINT)Sound->Handle;
	check( alIsBuffer( Buf ) );

	Voice->Id = Id;
	Voice->BufferChanged = ( Buf != Voice->Buffer );
	Voice->Buffer = Buf;
	Voice->Location = Location;
	Voice->Velocity = Actor ? Actor->Velocity : FVector();
	Voice->Volume = Clamp( Volume, 0.f, 1.f );
	Voice->Radius = Radius;
	Voice->Pitch = Pitch;
	Voice->Priority = Priority;
	Voice->Actor = Actor;
	Voice->Looping = SOUND_SLOT_IS( Id, SLOT_Ambient ); // v400: ambient sounds loop (matches UGenericAudioSubsystem)
	Voice->Sound = Sound;

	// Start the voice.
	UpdateVoice( Voice - Voices, NVOP_Play );

	return true;

	unguard;
}

void UNOpenALAudioSubsystem::NoteDestroy( AActor* Actor )
{
	guard(UNOpenALAudioSubsystem::NoteDestroy)

	check(Actor);
	check(Actor->IsValid());
	for( INT i = 0; i < MAX_SOURCES; ++i)
	{
		if( Voices[i].Actor == Actor )
		{
			if( SOUND_SLOT_IS( Voices[i].Id, SLOT_Ambient ) )
			{
				// Stop ambient sound when actor dies.
				StopVoice( i );
			}
			else
			{
				// Unbind regular sounds from actors.
				Voices[i].Actor = NULL;
			}
		}
	}

	unguard;
}

void UNOpenALAudioSubsystem::StopVoice( INT Num )
{
	guard(UNOpenALAudioSubsystem::StopVoice)

	FNVoice& Voice = Voices[Num];

	alSourcei( Sources[Num], AL_LOOPING, AL_FALSE );
	alSourceStop( Sources[Num] );

	if( Voice.Buffer != INVALID_BUFFER )
	{
		Voice.Buffer = INVALID_BUFFER;
		alSourcei( Sources[Num], AL_BUFFER, 0 );
	}

	Voice.Priority = 0.f;
	Voice.Sound = NULL;
	Voice.Actor = NULL;
	Voice.Looping = false;
	Voice.Id = 0;

	unguard;
}

void UNOpenALAudioSubsystem::PlayMusic()
{
	guard(UNOpenALAudioSubsystem::PlayMusic)

	FScopedLock Lock( MusicMutex );

	alSourceStop(MusicSource);
	ClearMusicBuffers();
	xmp_set_position( MusicCtx, MusicSection );

	ALint State = 0;
	alGetSourcei( MusicSource, AL_SOURCE_STATE, &State );
	if( State != AL_PLAYING )
		alSourcePlay( MusicSource );

	MusicIsPlaying = true;

	unguard;
}

void UNOpenALAudioSubsystem::StopMusic()
{
	guard(UNOpenALAudioSubsystem::StopMusic)

	FScopedLock Lock( MusicMutex );

	MusicIsPlaying = false;
	alSourceStop( MusicSource );

	unguard;
}

void UNOpenALAudioSubsystem::Update( FPointRegion Region, FCoords& Listener )
{
	guard(UNOpenALAudioSubsystem::Update)

	// Pump the music stream every frame, before the realtime early-return, so menu and title music keeps
	// streaming without a live viewport.
	UpdateMusicBuffers();

	if( !Viewport || !Viewport->IsRealtime() )
		return;

	// Update AL listener position, velocity and orientation.
	FVector ALPosition;
	ALPosition.X = Listener.Origin.X;
	ALPosition.Y = Listener.Origin.Y;
	ALPosition.Z = -Listener.Origin.Z;
	FVector ALVelocity;
	if( Viewport->Actor )
	{
		ALVelocity.X = Viewport->Actor->Velocity.X;
		ALVelocity.Y = Viewport->Actor->Velocity.Y;
		ALVelocity.Z = -Viewport->Actor->Velocity.Z;
	}
	FLOAT ALOrientation[] = {
		+Listener.ZAxis.X,
		+Listener.ZAxis.Y,
		-Listener.ZAxis.Z,
		-Listener.YAxis.X,
		-Listener.YAxis.Y,
		+Listener.YAxis.Z,
	};
	alListenerfv( AL_POSITION, &ALPosition.X );
	alListenerfv( AL_VELOCITY, &ALVelocity.X );
	alListenerfv( AL_ORIENTATION, ALOrientation );
	ListenerCoords = Listener;

	if( UseReverb )
		UpdateReverb( Region );

	// Start new ambient sounds if needed.
	if( Viewport->Actor && Viewport->Actor->XLevel )
	{
		for( INT i = 0; i < Viewport->Actor->XLevel->Actors.Num(); i++ )
		{
			AActor* Actor = Viewport->Actor->XLevel->Actors(i);
			if( !Actor || !Actor->IsValid() )
				continue;

			const FLOAT DistSq = FDistSquared( Viewport->Actor->Location, Actor->Location );
			const FLOAT AmbRad = Square( Actor->WorldSoundRadius() );
			if( !Actor->AmbientSound || DistSq > AmbRad )
				continue;

			// See if it's already playing.
			INT Id = AMBIENT_SOUND_ID( Actor->GetIndex() );
			INT AmbientNum;
			for( AmbientNum = 0; AmbientNum < MAX_SOURCES; ++AmbientNum )
			{
				if( Voices[AmbientNum].Id == Id )
					break;
			}

			// If not, start it.
			if( AmbientNum == MAX_SOURCES )
			{
				FLOAT Vol = AmbientFactor * Actor->SoundVolume / 255.f;
				FLOAT Rad = Actor->WorldSoundRadius();
				FLOAT Pitch = Actor->SoundPitch / 64.f;
				PlaySound( Actor, Id, Actor->AmbientSound, Actor->Location, Vol, Rad, Pitch );
			}
		}
	}

	// Update active ambient sounds.
	for( INT VoiceNum = 0; VoiceNum < MAX_SOURCES; ++VoiceNum )
	{
		FNVoice& Voice = Voices[VoiceNum];
		if( !Voice.Id || !Voice.Sound || Voice.Buffer == INVALID_BUFFER || !SOUND_SLOT_IS( Voice.Id, SLOT_Ambient ) )
			continue;

		check( Voice.Actor );

		const FLOAT DistSq = FDistSquared( Viewport->Actor->Location, Voice.Actor->Location );
		const FLOAT AmbRad = Square( Voice.Actor->WorldSoundRadius() );
		if( Voice.Sound != Voice.Actor->AmbientSound || DistSq > AmbRad )
		{
			// Sound changed or went out of range.
			StopVoice( VoiceNum );
		}
		else
		{
			// Update parameters. These will be applied in the loop below.
			Voice.Radius = Voice.Actor->WorldSoundRadius();
			Voice.Pitch = Voice.Actor->SoundPitch / 64.f;
			Voice.Volume = AmbientFactor * Voice.Actor->SoundVolume / 255.f;
			if( Voice.Actor->LightType != LT_None )
				Voice.Volume *= Voice.Actor->LightBrightness / 255.f;
		}
	}

	// Update all active voices.
	for( INT VoiceNum = 0; VoiceNum < MAX_SOURCES; ++VoiceNum )
	{
		FNVoice& Voice = Voices[VoiceNum];
		if( !Voice.Id || !Voice.Sound || Voice.Buffer == INVALID_BUFFER )
			continue;

		ALuint Source = Sources[VoiceNum];
		ALint SourceState;
		alGetSourcei( Source, AL_SOURCE_STATE, &SourceState );
		if( SourceState == AL_STOPPED )
		{
			// Voice has finished playing.
			StopVoice( VoiceNum );
		}
		else if( SourceState == AL_PLAYING )
		{
			// Voice is playing, update its location and priority.
			if( Voice.Actor && Voice.Actor->IsValid() )
			{
				Voice.Location = Voice.Actor->Location;
				Voice.Velocity = Voice.Actor->Velocity;
			}
			Voice.Priority = GetVoicePriority( Voice.Location, Voice.Volume, Voice.Radius );
			// Update AL source.
			UpdateVoice( VoiceNum );
		}
	}

	// Update music.
	DOUBLE DeltaTime = appSeconds() - MusicTime;
	MusicTime += DeltaTime;
	DeltaTime = Clamp( DeltaTime, 0.0, 1.0 );
	if( Viewport->Actor && Viewport->Actor->Transition != MTRAN_None )
	{
		// Track is changing.
		UBOOL MusicChanged = Music != Viewport->Actor->Song;
		if( Music )
		{
			// Already playing something, figure out if we're ready to change.
			UBOOL MusicDone = false;
			if( MusicSection == 255 )
			{
				MusicDone = true;
			}
			else if( Viewport->Actor->Transition == MTRAN_Fade )
			{
				MusicFade -= DeltaTime;
				MusicDone = ( MusicFade < -2.f / 1000.f );
			}
			else if( Viewport->Actor->Transition == MTRAN_SlowFade )
			{
				MusicFade -= DeltaTime * 0.2;
				MusicDone = ( MusicFade < 0.2f * -2.f / 1000.f );
			}
			else if( Viewport->Actor->Transition == MTRAN_FastFade )
			{
				MusicFade -= DeltaTime * 3.0;
				MusicDone = ( MusicFade < 3.0f * -2.f / 1000.f );
			}
			else
			{
				MusicDone = true;
			}

			MusicMutex.Lock();

			if( MusicDone )
			{
				if( Music && MusicChanged )
					UnregisterMusic( Music );
				Music = NULL;
			}
			else
			{
				alSourcef( MusicSource, AL_GAIN, Max(MusicFade, 0.f) * MusicVolume / 255.f );
			}

			MusicMutex.Unlock();
		}

		if( Music == NULL )
		{
			FScopedLock Lock( MusicMutex );
			MusicFade = 1.f;
			alSourcef( MusicSource, AL_GAIN, Max(MusicFade, 0.f) * MusicVolume / 255.f );
			Music = Viewport->Actor->Song;
			MusicSection = Viewport->Actor->SongSection;
			if( Music )
			{
				if( MusicChanged )
					RegisterMusic( Music );
				if( MusicSection != 255 )
					PlayMusic();
				else
					StopMusic();
			}
			Viewport->Actor->Transition = MTRAN_None;
		}
	}

	unguard;
}

void UNOpenALAudioSubsystem::UpdateMusicBuffers()
{
	guard(UNOpenALAudioSubsystem::UpdateMusicBuffers)

	FScopedLock Lock( MusicMutex );

	// First dequeue the buffers that are done playing and put them into the buffer pool
	ALint BuffersProcessed = 0;
	ALint BuffersQueued = 0;
	ALint State = AL_STOPPED;
	alGetSourcei( MusicSource, AL_BUFFERS_PROCESSED, &BuffersProcessed );
	alGetSourcei( MusicSource, AL_BUFFERS_QUEUED, &BuffersQueued );
	alGetSourcei( MusicSource, AL_SOURCE_STATE, &State );
	if( BuffersProcessed > 0 && NumFreeMusicBuffers < NUM_MUSIC_BUFFERS )
	{
		const INT NumToUnqueue = Min( NUM_MUSIC_BUFFERS - NumFreeMusicBuffers, BuffersProcessed );
		alSourceUnqueueBuffers( MusicSource, NumToUnqueue, &FreeMusicBuffers[NumFreeMusicBuffers] );
		NumFreeMusicBuffers += NumToUnqueue;
	}

	if( !Music || !MusicIsPlaying || MusicSection == 255 || !MusicCtx )
		return;

	// If music is playing, render and queue more buffers if available
	while( BuffersQueued < NUM_MUSIC_BUFFERS && NumFreeMusicBuffers )
	{
		if( xmp_play_buffer( MusicCtx, MusicBufferData, sizeof( MusicBufferData ), 0 ) < 0 )
			break;
		alBufferData( FreeMusicBuffers[NumFreeMusicBuffers - 1], AL_FORMAT_STEREO16, MusicBufferData, sizeof( MusicBufferData ), OutputRate );
		alSourceQueueBuffers( MusicSource, 1, &FreeMusicBuffers[NumFreeMusicBuffers - 1] );
		--NumFreeMusicBuffers;
		++BuffersQueued;
	}

	// If it stopped because it ran out of buffers, restart it
	if( BuffersQueued > 0 && ( State == AL_INITIAL || State == AL_STOPPED ) )
		alSourcePlay( MusicSource );

	unguard;
}

void UNOpenALAudioSubsystem::ClearMusicBuffers()
{
	guard(UNOpenALAudioSubsystem::ClearMusicBuffers)

	FScopedLock Lock( MusicMutex );

	appMemset( (void*)MusicBufferData, 0, sizeof(MusicBufferData) );

	ALint BuffersProcessed = 0;
	alGetSourcei( MusicSource, AL_BUFFERS_PROCESSED, &BuffersProcessed );
	if( BuffersProcessed > 0 && NumFreeMusicBuffers < NUM_MUSIC_BUFFERS )
	{
		const INT NumToUnqueue = Min( NUM_MUSIC_BUFFERS - NumFreeMusicBuffers, BuffersProcessed );
		alSourceUnqueueBuffers( MusicSource, NumToUnqueue, &FreeMusicBuffers[NumFreeMusicBuffers] );
		NumFreeMusicBuffers += NumToUnqueue;
	}

	for( INT i = 0; i < NumFreeMusicBuffers; ++i )
		alBufferData( FreeMusicBuffers[i], AL_FORMAT_STEREO16, MusicBufferData, sizeof( MusicBufferData ), OutputRate );

	unguard;
}

void UNOpenALAudioSubsystem::UpdateReverb( FPointRegion& Region )
{
	guard(UNOpenALAudioSubsystem::UpdateReverb)

	if( ReverbKind == REVERB_NONE )
		return;   // no EFX: there is no slot to point at a zone

	const UBOOL bNewReverb = ( Viewport->Actor && Viewport->Actor->Region.Zone && Viewport->Actor->Region.Zone->bReverbZone );

	if( !bNewReverb && ReverbOn )
	{
		// Just turn it off.
		ReverbOn = false;
		ReverbZone = NULL;
		// TODO: is there a better way to toggle this?
		Efx.AuxiliaryEffectSloti( ReverbSlot, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL );
		return;
	}

	ReverbOn = bNewReverb;

	if( ReverbOn )
	{
		AZoneInfo* NewReverbZone = Viewport->Actor->Region.Zone;
		if( NewReverbZone != ReverbZone )
		{
			// Reverb zone changed.
			ReverbZone = NewReverbZone;
			// Unbind effect, change parameters, then bind again.
			Efx.AuxiliaryEffectSloti( ReverbSlot, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL );
			Efx.Effectf( ReverbEffect, AL_EAXREVERB_GAIN, ReverbZone->MasterGain / 255.f );
			Efx.AuxiliaryEffectSloti( ReverbSlot, AL_EFFECTSLOT_EFFECT, ReverbEffect );
			// Say once that reverb engages.
			static UBOOL LoggedReverb = 0;
			if( !LoggedReverb )
			{
				if( GUfrontLog )
					debugf( TEXT("NOpenALDrv: EAX reverb applied for zone %s (MasterGain %d)"), ReverbZone->GetName(), ReverbZone->MasterGain );
				LoggedReverb = 1;
			}
		}
	}

	unguard;
}

void UNOpenALAudioSubsystem::InitReverbEffect()
{
	guard(UNOpenALAudioSubsystem::InitReverbEffect)

	if( ReverbKind == REVERB_NONE )
		return;   // no EFX: no effect object was created

#if !NOPENAL_NO_EAX
	EFXEAXREVERBPROPERTIES Reverb = EFX_REVERB_PRESET_GENERIC;
	Efx.Effecti( ReverbEffect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB );
	Efx.Effectf( ReverbEffect, AL_EAXREVERB_DENSITY, Reverb.flDensity );
	Efx.Effectf( ReverbEffect, AL_EAXREVERB_DIFFUSION, Reverb.flDiffusion );
	Efx.Effectf( ReverbEffect, AL_EAXREVERB_GAIN, Reverb.flGain );
	Efx.Effectf( ReverbEffect, AL_EAXREVERB_GAINHF, Reverb.flGainHF );
	Efx.Effectf( ReverbEffect, AL_EAXREVERB_GAINLF, Reverb.flGainLF );
	Efx.Effectf( ReverbEffect, AL_EAXREVERB_DECAY_TIME, Reverb.flDecayTime );
	Efx.Effectf( ReverbEffect, AL_EAXREVERB_DECAY_HFRATIO, Reverb.flDecayHFRatio );
	Efx.Effectf( ReverbEffect, AL_EAXREVERB_DECAY_LFRATIO, Reverb.flDecayLFRatio );
	Efx.Effectf( ReverbEffect, AL_EAXREVERB_REFLECTIONS_GAIN, Reverb.flReflectionsGain);
	Efx.Effectf( ReverbEffect, AL_EAXREVERB_REFLECTIONS_DELAY, Reverb.flReflectionsDelay );
	Efx.Effectfv( ReverbEffect, AL_EAXREVERB_REFLECTIONS_PAN, Reverb.flReflectionsPan );
	Efx.Effectf( ReverbEffect, AL_EAXREVERB_LATE_REVERB_GAIN, Reverb.flLateReverbGain );
	Efx.Effectf( ReverbEffect, AL_EAXREVERB_LATE_REVERB_DELAY, Reverb.flLateReverbDelay );
	Efx.Effectfv( ReverbEffect, AL_EAXREVERB_LATE_REVERB_PAN, Reverb.flLateReverbPan );
	Efx.Effectf( ReverbEffect, AL_EAXREVERB_ECHO_TIME, Reverb.flEchoTime );
	Efx.Effectf( ReverbEffect, AL_EAXREVERB_ECHO_DEPTH, Reverb.flEchoDepth );
	Efx.Effectf( ReverbEffect, AL_EAXREVERB_MODULATION_TIME, Reverb.flModulationTime );
	Efx.Effectf( ReverbEffect, AL_EAXREVERB_MODULATION_DEPTH, Reverb.flModulationDepth );
	Efx.Effectf( ReverbEffect, AL_EAXREVERB_AIR_ABSORPTION_GAINHF, Reverb.flAirAbsorptionGainHF );
	Efx.Effectf( ReverbEffect, AL_EAXREVERB_HFREFERENCE, Reverb.flHFReference );
	Efx.Effectf( ReverbEffect, AL_EAXREVERB_LFREFERENCE, Reverb.flLFReference );
	Efx.Effectf( ReverbEffect, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, Reverb.flRoomRolloffFactor );
	Efx.Effecti( ReverbEffect, AL_EAXREVERB_DECAY_HFLIMIT, Reverb.iDecayHFLimit );
#endif

	unguard;
}

UBOOL UNOpenALAudioSubsystem::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(UNOpenALAudioSubsystem::Exec)

	if( ParseCommand( &Cmd, TEXT("MusicOrder") ) )
	{
		if( Music && MusicCtx )
		{
			FScopedLock Lock( MusicMutex );
			INT Pos = appAtoi( Cmd );
			Ar.Logf( TEXT("Set music position to %d"), Pos );
			xmp_set_position( MusicCtx, Pos );
			MusicSection = Pos;
			return true;
		}
	}
	else if( ParseCommand( &Cmd, TEXT("MusicInterp") ) )
	{
		FScopedLock Lock( MusicMutex );
		MusicInterpolation = Clamp( appAtoi( Cmd ), 0, XMP_INTERP_SPLINE );
		if( MusicCtx )
			xmp_set_player( MusicCtx, XMP_PLAYER_INTERP, MusicInterpolation );
		return true;
	}

	return false;

	unguard;
}

// Music is decoded incrementally in UpdateMusicBuffers(), pumped from Update() each frame.

