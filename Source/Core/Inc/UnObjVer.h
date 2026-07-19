/*=============================================================================
	UnObjVer.h: Unreal object version.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

/*-----------------------------------------------------------------------------
	Version coding.
-----------------------------------------------------------------------------*/

// Earliest engine build that is network compatible with this one.
#define ENGINE_MIN_NET_VERSION 400

// Engine build number, for displaying to end users.
// v436-compat port: report 436 so version-gating 436-era script/mods behave, and as a build
// marker (paired with the startup banner in appInit). Net-compat floor stays at 400 via
// ENGINE_MIN_NET_VERSION above. PACKAGE_FILE_VERSION is intentionally left at 68.
#define ENGINE_VERSION 436

// Base protocol version to negotiate in network play.
#define ENGINE_NEGOTIATION_VERSION 400

// Prevents incorrect files from being loaded.
#define PACKAGE_FILE_TAG 0x9E2A83C1

// The current Unrealfile version.
#define PACKAGE_FILE_VERSION 68

// The earliest file version which we can load with complete
// backwards compatibility. Must be at least PACKAGE_FILE_VERSION.
#define PACKAGE_MIN_VERSION 60

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
