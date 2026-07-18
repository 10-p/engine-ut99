/*=============================================================================
	Null.cpp: A do-nothing audio subsystem (silent).

	ufront step 2.7a (engine-ut99). ut99dc's shipped inis select
	`SoundDrv.NullAudioSubsystem`, but the `SoundDrv` package was never included
	in the ut99dc tree (only the Dreamcast `AICADrv` and the OSS-`/dev/dsp`
	`Audio` subsystem, neither usable on native-Linux/WASM). This provides the
	null subsystem the config already references — silent audio that works on
	every platform. Mirrors engine-unreal1's `SoundDrv/Null.cpp`, adapted to
	UT99 v400's `UAudioSubsystem` interface (Engine/Inc/UnAudio.h): the v400 ABI
	adds GetViewport/RenderAudioGeometry/PostRender and takes an `FOutputDevice&`
	in Exec. Real audio (SDL2/OpenAL) is a documented future enhancement.
=============================================================================*/

#include "Engine.h"
#include "UnRender.h"

/*-----------------------------------------------------------------------------
	UNullAudioSubsystem.
-----------------------------------------------------------------------------*/

//
// The null (silent) audio subsystem. Every method is a no-op; Init() reports
// success so the engine proceeds with a valid-but-silent audio device.
//
class DLL_EXPORT UNullAudioSubsystem : public UAudioSubsystem
{
	DECLARE_CLASS(UNullAudioSubsystem, UAudioSubsystem, CLASS_Config)

	// Class registration hook (the ut99dc driver convention). No config
	// properties — silent audio needs none.
	static void InternalClassInitializer( UClass* Class ) {}

	// UAudioSubsystem interface (UT99 v400, Engine/Inc/UnAudio.h).
	virtual UBOOL Init() { return 1; }
	virtual void SetViewport( UViewport* Viewport ) { AudioViewport = Viewport; }
	virtual UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar=*GLog ) { return 0; }
	virtual void Update( FPointRegion Region, FCoords& Listener ) {}
	virtual void RegisterMusic( UMusic* Music ) {}
	virtual void RegisterSound( USound* Sound ) {}
	virtual void UnregisterSound( USound* Sound ) {}
	virtual void UnregisterMusic( UMusic* Music ) {}
	virtual UBOOL PlaySound( AActor* Actor, INT Id, USound* Sound, FVector Location, FLOAT Volume, FLOAT Radius, FLOAT Pitch ) { return 0; }
	virtual void NoteDestroy( AActor* Actor ) {}
	virtual UBOOL GetLowQualitySetting() { return 1; }
	virtual UViewport* GetViewport() { return AudioViewport; }
	virtual void RenderAudioGeometry( FSceneNode* Frame ) {}
	virtual void PostRender( FSceneNode* Frame ) {}

private:
	UViewport* AudioViewport = NULL;
};

IMPLEMENT_CLASS(UNullAudioSubsystem);

/*-----------------------------------------------------------------------------
	Package.
-----------------------------------------------------------------------------*/

IMPLEMENT_PACKAGE(SoundDrv);

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
