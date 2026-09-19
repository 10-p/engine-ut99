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
// Guarded with #ifndef so a single translation unit can be compiled against a different value:
// the fetched XOpenGLDrv (a "ut99-v400-compat" fork) gates its render-header selection and its
// v400 adaptation paths on ENGINE_VERSION==400, and our render layer *is* still v400 (the render
// structs it uses are not version-gated), so XOpenGLDrv/CMakeLists.txt forces ENGINE_VERSION=400
// for that driver only. The rest of the engine keeps 436.
#ifndef ENGINE_VERSION
#define ENGINE_VERSION 436
#endif

// Base protocol version to negotiate in network play.
#define ENGINE_NEGOTIATION_VERSION 400

// Prevents incorrect files from being loaded.
#define PACKAGE_FILE_TAG 0x9E2A83C1

// The current Unrealfile version.
#define PACKAGE_FILE_VERSION 68

// v436-compat port: licensee version, the upper 16 bits of a package's file version (436 reads
// it into FArchive::LicenseeVer). Epic packages carry 0 there.
#define PACKAGE_FILE_VERSION_LICENSEE	0x00

// The earliest file version which we can load with complete
// backwards compatibility. Must be at least PACKAGE_FILE_VERSION.
#define PACKAGE_MIN_VERSION 60

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
