/*------------------------------------------------------------------------------------
	Dependencies.
------------------------------------------------------------------------------------*/

#include "AL/al.h"
#include "AL/alc.h"
#include "xmp.h"
#include "Engine.h"

/*------------------------------------------------------------------------------------
	OpenAL audio subsystem private definitions.
------------------------------------------------------------------------------------*/

#define MAX_SOURCES 64

#define NUM_MUSIC_BUFFERS 4

#define INVALID_SOURCE ((ALuint)-1)
#define INVALID_BUFFER ((ALuint)-1)

#define SOUND_SLOT_IS( Id, Slot ) ( ( (Id) & 14 ) == (Slot) * 2 )
#define AMBIENT_SOUND_ID( ActorIndex ) ( (ActorIndex) * 16 + SLOT_Ambient * 2 )

#define DEFAULT_OUTPUT_RATE 44100

#define STREAM_BUFSIZE 32768

// World scale related constants, same as in ALAudio 2.4.7.
#define DISTANCE_SCALE 0.023255814f
#define ROLLOFF_FACTOR 1.1f
#define DESPATIALIZE_FACTOR 0.1f

// ufront 2.12 (web + native): this driver pumps the libxmp music decoder from Update() on BOTH platforms
// (the web build is single-threaded, so it must; native follows the same path for one code path), so there
// is no background decoder thread and no cross-thread music state. UT99's Core (v400) has no FMutex/
// FScopedLock (those were v200 additions in engine-unreal1), so define trivial no-op equivalents here to
// keep the music-locking call sites unchanged and correct — all music access is on the main (game) thread.
struct FMutex
{
	FMutex( const TCHAR* = NULL ) {}
	void Lock() {}
	void Unlock() {}
};
struct FScopedLock
{
	FScopedLock( FMutex& ) {}
};

class DLL_EXPORT UNOpenALAudioSubsystem : public UAudioSubsystem
{
	DECLARE_CLASS(UNOpenALAudioSubsystem, UAudioSubsystem, CLASS_Config)

	// Options
	char DeviceName[256];
	INT OutputRate;
	BYTE MasterVolume;
	BYTE SoundVolume;
	BYTE MusicVolume;
	BYTE MusicInterpolation;
	FLOAT AmbientFactor;
	FLOAT DopplerFactor;
	UBOOL UseReverb;
	UBOOL UseHRTF;
	UBOOL UseEAX;			// ufront 2.70: which reverb when UseReverb — EFX's EAX reverb (True) or the classic Galaxy one (False)
	// ufront 2.76: REVERB_NONE is the third tier — an OpenAL with no EFX at all (a core 1.1 library). Decided
	// at Init from what the LINKED library offers, never at build time.
	enum EReverbKind { REVERB_EAX=0, REVERB_GALAXY=1, REVERB_NONE=2 };
	INT ReverbKind;			// resolved at Init: Galaxy only if the library has AL_UFRONT_galaxy_reverb

	// Constructors.
	void StaticConstructor();
	UNOpenALAudioSubsystem();

	// UObject interface.
	virtual void Destroy();
	virtual void PostEditChange();
	virtual void ShutdownAfterError();

	// UAudioSubsystem interface (UT99 v400, Engine/Inc/UnAudio.h).
	virtual UBOOL Init();
	virtual void SetViewport( UViewport* Viewport );
	virtual UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar=*GLog );
	virtual void Update( FPointRegion Region, FCoords& Listener );
	virtual void RegisterMusic( UMusic* Music );
	virtual void RegisterSound( USound* Music );
	virtual void UnregisterSound( USound* Sound );
	virtual void UnregisterMusic( UMusic* Music );
	virtual UBOOL PlaySound( AActor* Actor, INT Id, USound* Sound, FVector Location, FLOAT Volume, FLOAT Radius, FLOAT Pitch );
	virtual void NoteDestroy( AActor* Actor );
	virtual UBOOL GetLowQualitySetting() { return false; };
	virtual UViewport* GetViewport() { return Viewport; }
	virtual void RenderAudioGeometry( FSceneNode* Frame ) {}
	virtual void PostRender( FSceneNode* Frame ) {}

	// Internals.
private:
	UViewport* Viewport;
	ALCdevice* Device;
	ALCcontext* Ctx;
	ALuint Sources[MAX_SOURCES];
	TArray<ALuint> Buffers;
	INT NextId;
	FCoords ListenerCoords;

	ALuint ReverbEffect;
	ALuint ReverbSlot;
	UBOOL ReverbOn;
	AZoneInfo* ReverbZone;

	xmp_context MusicCtx;
	UMusic* Music;
	FLOAT MusicFade;
	DOUBLE MusicTime;
	BYTE MusicSection;
	ALuint MusicSource;
	UBOOL MusicIsPlaying = false;
	UBOOL MusicIsLoaded = false;

	BYTE MusicBufferData[STREAM_BUFSIZE];
	ALuint MusicBuffers[NUM_MUSIC_BUFFERS];
	ALuint FreeMusicBuffers[NUM_MUSIC_BUFFERS];
	INT NumFreeMusicBuffers;

	// Single-threaded music: pumped from Update() (no background thread). See the FMutex note above.
	FMutex MusicMutex { "MusicMutex" };

	enum ENVoiceOp
	{
		NVOP_None,
		NVOP_Play,
		NVOP_Stop,
		NVOP_Pause,
	};

	struct FNVoice
	{
		ALuint Buffer = INVALID_BUFFER;
		AActor* Actor;
		INT Id;
		USound* Sound;
		FVector Location;
		FVector Velocity;
		FLOAT Volume;
		FLOAT Radius;
		FLOAT Pitch;
		FLOAT Priority;
		UBOOL Looping;
		UBOOL BufferChanged = false;
	} Voices[MAX_SOURCES];

	void InitReverbEffect();
	void UpdateReverb( FPointRegion& Region );
	void UpdateVoice( INT Num, const ENVoiceOp Op = NVOP_None );
	void StopVoice( INT Num );
	void PlayMusic();
	void StopMusic();

	void UpdateMusicBuffers();
	void ClearMusicBuffers();

	inline FLOAT GetVoicePriority( const FVector& Location, FLOAT Volume, FLOAT Radius )
	{
		if( Radius && Viewport->Actor )
			return Volume * ( 1.f - (Location - Viewport->Actor->Location).Size() / Radius );
		else
			return Volume;
	}
};
