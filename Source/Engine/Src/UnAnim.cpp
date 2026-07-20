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
// Forward declarations of the CCoords-style helpers (defined below) so GetFrame can use them.
static FVector SkelUnTransformVector( const FSkelCoords& C, const FVector& s );

void USkeletalMesh::GetFrame( FVector* ResultVerts, INT Size, FCoords Coords, AActor* Owner, INT& LODRequest )
{
	guard(USkeletalMesh::GetFrame);
	// Link the animation sequences into the mesh so the engine's GetAnimSeq/GetAnimGroup find them
	// (they live in DefaultAnimation, not the mesh). Without this the script's LoopAnim/PlayAnim
	// calls fail ("Sequence not found in Mesh") and the actor's AnimSequence/AnimFrame never update,
	// so the mesh is stuck in one pose. This is the LinkSkelAnim step the stock engine does.
	if( AnimSeqs.Num()==0 && DefaultAnimation && DefaultAnimation->AnimSeqs.Num() )
		AnimSeqs = DefaultAnimation->AnimSeqs;
	// Compose the incoming (camera/world->view) Coords with the actor's world transform + the mesh
	// scale/orientation, exactly as ULodMesh::GetFrame does, so mesh-local Points map to view space
	// at the actor's position. Without this the local Points were fed to the raw camera transform and
	// every skeletal actor drew clustered at the same spot.
	FLOAT DrawScale = Owner->bParticles ? 1.0f : Owner->DrawScale;
	// Skeletal meshes are mirrored in the data. Un-mirror with a NEGATIVE-Y scale reflection: this
	// reverses the winding to show the exterior, keeps the soldier right-handed (left/right animations
	// correct), AND faces him along his movement direction. (Negate-X instead gave right-handed but
	// backward-facing "moonwalking"; negate-Y == a 180-deg yaw composed with that reflection.)
	Coords = Coords * (Owner->Location + Owner->PrePivot) * Owner->Rotation * RotOrigin
		* FScale( FVector(Scale.X, -Scale.Y, Scale.Z) * DrawScale, 0.0f, SHEER_None );

	// Skin Points into the current animation pose if one applies; else use the static bind pose.
	TArray<FVector> Skinned;
	const FVector* Src = ( Points.Num() && ComputeSkinnedPose( Owner, Skinned ) && Skinned.Num()==Points.Num() )
		? &Skinned(0) : ( Points.Num() ? &Points(0) : NULL );

	// Resolve the weapon attachment bone (set by ComputeSkinnedPose) into a view-space frame that
	// DrawLodMesh publishes as SpecialCoords, so the third-person weapon attaches to the hand.
	bWeaponAttach = 0;
	if( bWeaponBoneM )
	{
		// Compose the weapon-bone frame with the mesh's WeaponAdjust (the grip offset+orientation of
		// the weapon relative to the bone). WeaponAdjust is native FCoords; mirror it into the bone's
		// mirrored space (M*C*M: negate Origin.X; XAxis negate Y,Z; YAxis/ZAxis negate X), compose, then
		// un-mirror + transform to view space.
		FVector wao(  -WeaponAdjust.Origin.X,  WeaponAdjust.Origin.Y,  WeaponAdjust.Origin.Z );
		FVector waX(   WeaponAdjust.XAxis.X,  -WeaponAdjust.XAxis.Y,  -WeaponAdjust.XAxis.Z );
		FVector waY(  -WeaponAdjust.YAxis.X,   WeaponAdjust.YAxis.Y,   WeaponAdjust.YAxis.Z );
		FVector waZ(  -WeaponAdjust.ZAxis.X,   WeaponAdjust.ZAxis.Y,   WeaponAdjust.ZAxis.Z );
		FVector wfO = WeaponBoneM.O + SkelUnTransformVector( WeaponBoneM, wao );		// weapon origin, mirrored ml
		FVector wfX = SkelUnTransformVector( WeaponBoneM, waX );						// weapon axes, mirrored ml
		FVector wfY = SkelUnTransformVector( WeaponBoneM, waY );
		FVector wfZ = SkelUnTransformVector( WeaponBoneM, waZ );
		FVector O  = wfO;         O.X  = -O.X;			// un-mirror to native
		FVector eX = wfO + wfX;   eX.X = -eX.X;
		FVector eY = wfO + wfY;   eY.X = -eY.X;
		FVector eZ = wfO + wfZ;   eZ.X = -eZ.X;
		FVector Ov = (O - Origin).TransformPointBy( Coords );
		WeaponAttachOrigin = Ov;
		WeaponAttachX = ( (eX - Origin).TransformPointBy(Coords) - Ov ).SafeNormal();
		WeaponAttachY = ( (eY - Origin).TransformPointBy(Coords) - Ov ).SafeNormal();
		WeaponAttachZ = ( (eZ - Origin).TransformPointBy(Coords) - Ov ).SafeNormal();
		bWeaponAttach = 1;
	}

	INT Requested = LODRequest + SpecialVerts;			// DrawLodMesh allocated this many samples.
	INT Count     = Min( Requested, Points.Num() );
	BYTE* Out     = (BYTE*)ResultVerts;
	for( INT i=0; i<Count; i++ )
	{
		*(FVector*)Out = (Src[i] - Origin).TransformPointBy( Coords );
		Out += Size;
	}
	// Defensive: never leave requested samples uninitialized (only hit if a mesh had SpecialVerts>0
	// or fewer Points than ModelVerts, which none of the tested content does). Collapse to origin.
	FVector Safe = Origin.TransformPointBy( Coords );
	for( INT i=Count; i<Requested; i++ ) { *(FVector*)Out = Safe; Out += Size; }
	LODRequest = Max( 0, Count - SpecialVerts );
	unguard;
}

//
// v436-compat: build the reference (bind) pose bone world transforms from RefSkeleton, once.
// Convention ported from UEViewer (Gildor's umodel): compose local (Position,Orientation) up the
// hierarchy; the root bone's orientation is conjugated. worldRot = parentRot * localRot;
// worldPos = parentPos + parentRot.Rotate(localPos).
// ---- Exact port of UEViewer's CAxis/CCoords math (rigid frames as origin + 3 axis rows) --------
static void SkelToAxis( const FSkelQuat& q, FSkelCoords& C )
{
	FLOAT x2=q.X*2, y2=q.Y*2, z2=q.Z*2;
	FLOAT xx=q.X*x2, xy=q.X*y2, xz=q.X*z2;
	FLOAT yy=q.Y*y2, yz=q.Y*z2, zz=q.Z*z2;
	FLOAT wx=q.W*x2, wy=q.W*y2, wz=q.W*z2;
	C.AX = FVector( 1.0f-(yy+zz), xy-wz,        xz+wy );
	C.AY = FVector( xy+wz,        1.0f-(xx+zz), yz-wx );
	C.AZ = FVector( xz-wy,        yz+wx,        1.0f-(xx+yy) );
}
static FVector SkelTransformVector( const FSkelCoords& C, const FVector& s )   // R*s
{
	return FVector( s|C.AX, s|C.AY, s|C.AZ );
}
static FVector SkelUnTransformVector( const FSkelCoords& C, const FVector& s ) // R^T*s
{
	return C.AX*s.X + C.AY*s.Y + C.AZ*s.Z;
}
static FVector SkelUnTransformPoint( const FSkelCoords& C, const FVector& s )  // local point -> world
{
	return C.O + SkelUnTransformVector( C, s );
}
static void SkelUnTransformCoords( const FSkelCoords& P, const FSkelCoords& L, FSkelCoords& Out ) // P applied to L
{
	FSkelCoords T;
	T.O  = SkelUnTransformPoint( P, L.O );
	T.AX = SkelUnTransformVector( P, L.AX );
	T.AY = SkelUnTransformVector( P, L.AY );
	T.AZ = SkelUnTransformVector( P, L.AZ );
	Out = T;
}
static void SkelInvertCoords( const FSkelCoords& S, FSkelCoords& D )
{
	FSkelCoords T;
	T.O  = SkelTransformVector( S, S.O ) * -1.0f;
	T.AX = FVector( S.AX.X, S.AY.X, S.AZ.X );
	T.AY = FVector( S.AX.Y, S.AY.Y, S.AZ.Y );
	T.AZ = FVector( S.AX.Z, S.AY.Z, S.AZ.Z );
	D = T;
}

//
void USkeletalMesh::ComputeBindPose()
{
	guard(USkeletalMesh::ComputeBindPose);
	if( bBindComputed )
		return;
	INT NB = RefSkeleton.Num();
	TArray<FSkelCoords> Ref; Ref.Add(NB);
	BindRefInv.Empty(); BindRefInv.Add(NB);
	for( INT b=0; b<NB; b++ )
	{
		FSkelQuat bo = RefSkeleton(b).BonePos.Orientation;
		FVector   bp = RefSkeleton(b).BonePos.Position;
		// UEViewer's mirrored bone space (negate Position.X, Orientation.X/W); un-mirrored on output.
		bo.X = -bo.X; bo.W = -bo.W; bp.X = -bp.X;
		if( b==0 ) bo = bo.Conj();				// only the root orientation is conjugated
		FSkelCoords L; L.O = bp; SkelToAxis( bo, L );
		if( b==0 )
			Ref(b) = L;
		else
		{
			INT p = RefSkeleton(b).ParentIndex;
			if( p<0 || p>=NB ) p = 0;
			SkelUnTransformCoords( Ref(p), L, Ref(b) );	// world = parent applied to local
		}
		SkelInvertCoords( Ref(b), BindRefInv(b) );		// cache inverse (ref->bone-local) for skinning
	}
	bBindComputed = 1;
	unguard;
}

// Sample an analog track at Frame (keys evenly spaced; UE1 empties KeyTime). Defaults left intact
// when the track has no keys.
static void SkelSampleTrack( const FSkelAnalogTrack& T, FLOAT Frame, INT NumFrames, FVector& OutPos, FSkelQuat& OutRot )
{
	INT NP = T.KeyPos.Num(), NR = T.KeyQuat.Num();
	if( NumFrames < 1 ) NumFrames = 1;
	if( NP==1 ) OutPos = T.KeyPos(0);
	else if( NP>1 )
	{
		FLOAT pos = Frame / NumFrames * NP;
		INT x = appFloor(pos); FLOAT f = pos - x;
		if( x<0 ) { x=0; f=0; } if( x>=NP ) x=NP-1;
		INT y = x+1; if( y>=NP ) { y=NP-1; }
		OutPos = T.KeyPos(x) + (T.KeyPos(y) - T.KeyPos(x)) * f;
	}
	if( NR==1 ) OutRot = T.KeyQuat(0);
	else if( NR>1 )
	{
		FLOAT pos = Frame / NumFrames * NR;
		INT x = appFloor(pos); FLOAT f = pos - x;
		if( x<0 ) { x=0; f=0; } if( x>=NR ) x=NR-1;
		INT y = x+1; if( y>=NR ) { y=NR-1; }
		OutRot = SkelSlerp( T.KeyQuat(x), T.KeyQuat(y), f );
	}
}

//
// v436-compat: skin Points from the reference pose into the current animation pose for Owner.
// Returns 0 (falls back to bind pose) when there is no linked animation or no matching sequence.
//
UBOOL USkeletalMesh::ComputeSkinnedPose( AActor* Owner, TArray<FVector>& Out )
{
	guard(USkeletalMesh::ComputeSkinnedPose);
	bWeaponBoneM = 0;
	UAnimation* Anim = DefaultAnimation;
	INT NB = RefSkeleton.Num();
	if( !Anim || NB==0 || Points.Num()==0 || BoneWeightIdx.Num()==0 )
		return 0;

	// Honor bAnimByOwner: attachment meshes (holstered pistol on the hip, sheathed knife on the
	// thigh -- INFc_PlayerAttachment) are separate skeletal actors that inherit the pawn's animation.
	// Take AnimSequence/AnimFrame from Owner->Owner in that case, exactly as ULodMesh::GetFrame does;
	// otherwise the gear stays in bind pose while the pawn animates and floats around the belt.
	AActor* AnimOwner = ( Owner->bAnimByOwner && Owner->Owner ) ? Owner->Owner : Owner;

	// Find the sequence matching the (anim) actor's current AnimSequence.
	INT SeqIdx = INDEX_NONE;
	for( INT i=0; i<Anim->AnimSeqs.Num(); i++ )
		if( Anim->AnimSeqs(i).Name == AnimOwner->AnimSequence ) { SeqIdx = i; break; }
	if( SeqIdx==INDEX_NONE || SeqIdx>=Anim->Moves.Num() )
		return 0;
	FSkelMotionChunk& Move = Anim->Moves(SeqIdx);
	INT NumFrames = Max( 1, Anim->AnimSeqs(SeqIdx).NumFrames );
	FLOAT Frame = Clamp( AnimOwner->AnimFrame, 0.f, 1.f ) * NumFrames;

	ComputeBindPose();

	// Animated bone world coords + per-bone skinning transform (anim ∘ bindInverse).
	TArray<FSkelCoords> AC; AC.Add(NB);		// animated world coords
	TArray<FSkelCoords> Xf; Xf.Add(NB);		// skinning transform (ref pose -> anim pose)
	for( INT b=0; b<NB; b++ )
	{
		FVector   lp = RefSkeleton(b).BonePos.Position;
		FSkelQuat lr = RefSkeleton(b).BonePos.Orientation;
		// Map mesh bone -> animation track by bone name.
		INT ab = INDEX_NONE;
		for( INT j=0; j<Anim->RefBones.Num(); j++ )
			if( Anim->RefBones(j).Name == RefSkeleton(b).Name ) { ab = j; break; }
		if( ab!=INDEX_NONE && ab<Move.AnimTracks.Num() )
			SkelSampleTrack( Move.AnimTracks(ab), Frame, NumFrames, lp, lr );
		// Mirror into the same space as the bind pose (see ComputeBindPose).
		lr.X = -lr.X; lr.W = -lr.W; lp.X = -lp.X;
		if( b==0 ) lr = lr.Conj();
		FSkelCoords L; L.O = lp; SkelToAxis( lr, L );
		if( b==0 )
			AC(b) = L;
		else
		{
			INT p = RefSkeleton(b).ParentIndex;
			if( p<0 || p>=NB ) p = 0;
			SkelUnTransformCoords( AC(p), L, AC(b) );
		}
		SkelUnTransformCoords( AC(b), BindRefInv(b), Xf(b) );	// Transform = anim.UnTransformCoords(refInv)
	}

	// Stash the animated weapon-bone frame (still in mirrored space) for GetFrame to turn into the
	// view-space SpecialCoords used to attach the third-person weapon to the hand.
	bWeaponBoneM = 0;
	if( WeaponBoneIndex>=0 && WeaponBoneIndex<NB )
	{
		WeaponBoneM = AC(WeaponBoneIndex);
		bWeaponBoneM = 1;
	}

	// Linear-blend skin: transform each ref-pose point by its bones' skinning transforms, weighted.
	Out.Empty(); Out.AddZeroed( Points.Num() );
	TArray<FLOAT> WSum; WSum.AddZeroed( Points.Num() );
	INT NBW = Min( BoneWeightIdx.Num(), NB );
	for( INT b=0; b<NBW; b++ )
	{
		const FSkelBoneInfIndex& BI = BoneWeightIdx(b);
		for( INT j=0; j<BI.Number; j++ )
		{
			INT wi = BI.WeightIndex + j;
			if( wi<0 || wi>=BoneWeights.Num() ) continue;
			const FSkelBoneInfluence& Inf = BoneWeights(wi);
			INT pi = Inf.PointIndex;
			if( pi<0 || pi>=Points.Num() ) continue;
			FLOAT w = Inf.BoneWeight / 65535.0f;
			FVector Pm = Points(pi); Pm.X = -Pm.X;		// into mirrored bone space
			FVector ap = SkelUnTransformPoint( Xf(b), Pm );
			Out(pi) += ap * w;
			WSum(pi) += w;
		}
	}
	for( INT i=0; i<Out.Num(); i++ )
	{
		if( WSum(i) > 0.001f )
		{
			Out(i) /= WSum(i);
			Out(i).X = -Out(i).X;						// un-mirror back to native space
		}
		else Out(i) = Points(i);
	}
	return 1;
	unguard;
}

/*----------------------------------------------------------------------------
	The End.
----------------------------------------------------------------------------*/
