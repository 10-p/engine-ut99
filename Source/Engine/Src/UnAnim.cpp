/*=============================================================================
	UnAnim.cpp: Unreal animation object (v436-compat port).
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	v436-compat port: registers the native class 'Engine.Animation' that v436 engine.u
	references but the stock v400 engine lacks. See UnMesh.h for the rationale. Kept minimal
	on purpose — UT99 base content carries no skeletal-animation data, so this only needs to
	exist so the linker resolves the import. If a package ever does contain an Animation
	export, Serialize below leaves the object's tagged properties (none) to the base and does
	not attempt to decode the skeletal blob.
=============================================================================*/

#include "EnginePrivate.h"

IMPLEMENT_CLASS(UAnimation);

// v436-compat port: minimal SkeletalMesh native class so OldSkool/ONP and other v436 mods that
// import Engine.SkeletalMesh can load. Full skeletal rendering is a non-goal (see UnMesh.h).
IMPLEMENT_CLASS(USkeletalMesh);

void UAnimation::Serialize( FArchive& Ar )
{
	guard(UAnimation::Serialize);
	Super::Serialize( Ar );
	unguard;
}

/*----------------------------------------------------------------------------
	The End.
----------------------------------------------------------------------------*/
