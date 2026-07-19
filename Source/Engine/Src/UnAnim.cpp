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
	// Skeletal animation data (v436 format). Consumed for byte-accurate loading; not played back.
	Ar << RefBones;
	Ar << Moves;
	Ar << AnimSeqs;
	unguard;
}

void USkeletalMesh::Serialize( FArchive& Ar )
{
	guard(USkeletalMesh::Serialize);
	Super::Serialize( Ar );			// ULodMesh: base mesh + LOD data.
	// Skeletal-specific blob (v436 format). Order per Ut99PubSrc/SurrealEngine.
	Ar << ExtWedges;
	Ar << Points;
	Ar << RefSkeleton;
	Ar << BoneWeightIdx;
	Ar << BoneWeights;
	Ar << LocalPoints;
	Ar << SkeletalDepth;
	Ar << DefaultAnimation;
	Ar << WeaponBoneIndex;
	Ar << WeaponAdjust.Origin << WeaponAdjust.XAxis << WeaponAdjust.YAxis << WeaponAdjust.ZAxis;
	// v436-compat diagnostic: report skeletal meshes as they load. This is how we know which
	// packages/actors actually carry skeletal (vs vertex) meshes — a real USkeletalMesh reports a
	// non-empty reference skeleton here.
	if( Ar.IsLoading() )
		debugf( NAME_Log, TEXT("[skelmesh] %s: %i bones, %i points, %i weights"),
			GetFullName(), RefSkeleton.Num(), Points.Num(), BoneWeights.Num() );
	unguard;
}

//
// v436-compat: draw a skeletal mesh in its reference (bind) pose.
//
// v400 predates skeletal skinning, so the renderer routes a USkeletalMesh (IsA ULodMesh) through
// DrawLodMesh, which calls this GetFrame to fill the transformed-vertex buffer and then draws
// triangles from the inherited ULodMesh Wedges/Faces. The default ULodMesh::GetFrame reads the
// vertex-mesh Verts array — empty for a skeletal mesh (FrameVerts>0 but Verts.Num()==0) — and reads
// out of bounds, which is the segfault seen when a skeletal actor first draws.
//
// For every skeletal mesh these mods ship, ModelVerts == FrameVerts == Points.Num() and
// SpecialVerts == 0, and the ULodMesh Wedges index into [0,ModelVerts). Points holds the skin
// vertices in mesh space, so emitting Points transformed by Coords (mesh Scale/rotation/world are
// baked into Coords exactly as the vertex-mesh path does) draws the mesh in its bind pose. LOD is
// already disabled for skeletal meshes (empty CollapsePointThus -> DoLOD false in DrawLodMesh), so
// LODRequest arrives == ModelVerts and no vertex-collapse indexing occurs. Animation playback is a
// non-goal, so the pose is static (bones/weights/animation are loaded but not applied).
//
void USkeletalMesh::GetFrame( FVector* ResultVerts, INT Size, FCoords Coords, AActor* Owner, INT& LODRequest )
{
	guard(USkeletalMesh::GetFrame);
	INT Requested = LODRequest + SpecialVerts;			// DrawLodMesh allocated this many samples.
	INT Count     = Min( Requested, Points.Num() );
	BYTE* Out     = (BYTE*)ResultVerts;
	for( INT i=0; i<Count; i++ )
	{
		*(FVector*)Out = (Points(i) - Origin).TransformPointBy( Coords );
		Out += Size;
	}
	// Defensive: never leave requested samples uninitialized (only hit if a mesh had SpecialVerts>0
	// or fewer Points than ModelVerts, which none of the tested content does). Collapse to origin.
	FVector Safe = Origin.TransformPointBy( Coords );
	for( INT i=Count; i<Requested; i++ ) { *(FVector*)Out = Safe; Out += Size; }
	LODRequest = Max( 0, Count - SpecialVerts );
	unguard;
}

/*----------------------------------------------------------------------------
	The End.
----------------------------------------------------------------------------*/
