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
// v436: 436, which script reads as LevelInfo.EngineVersion; appInit logs it ("Version: 436").
// #ifndef-guarded so one target can override it: XOpenGLDrv/CMakeLists.txt compiles that driver with 400,
// because its v400 paths key on ENGINE_VERSION==400 (the render structs it uses are not version-gated).
#ifndef ENGINE_VERSION
#define ENGINE_VERSION 436
#endif

// Base protocol version to negotiate in network play.
#define ENGINE_NEGOTIATION_VERSION 400

// Prevents incorrect files from being loaded.
#define PACKAGE_FILE_TAG 0x9E2A83C1

// The current Unrealfile version.
// v436: 69, the version v436 writes. The only Ver() test at 69 is UFont's char-remap table;
// loading accepts PACKAGE_MIN_VERSION (60) upward.
#define PACKAGE_FILE_VERSION 69

// v436: licensee version, the upper 16 bits of a package's file version (FArchive::LicenseeVer).
// Epic packages carry 0 there.
#define PACKAGE_FILE_VERSION_LICENSEE	0x00

// The earliest file version which we can load with complete
// backwards compatibility. Must be at least PACKAGE_FILE_VERSION.
#define PACKAGE_MIN_VERSION 60

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
