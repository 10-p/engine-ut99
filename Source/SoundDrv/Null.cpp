/*=============================================================================
	Null.cpp: A do-nothing audio subsystem (silent).

	The silent audio subsystem (SoundDrv.NullAudioSubsystem) on v400's
	UAudioSubsystem interface; NOpenALDrv is the audio driver of every target.
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

	// v200-era class hook; v400 never calls it. No config properties.
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
