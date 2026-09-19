/*=============================================================================
	UnAnim.cpp: Unreal animation object (v436-compat port).
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	v436-compat port: the native Animation and SkeletalMesh classes that v436 engine.u
	references but the stock v400 engine lacks (see UnMesh.h). Serialization is byte-accurate;
	USkeletalMesh::GetFrame skins and animates as the retail v436 Engine.dll does.
=============================================================================*/

#include "EnginePrivate.h"

IMPLEMENT_CLASS(UAnimation);

// v436-compat port: SkeletalMesh native class (OldSkool/ONP, Infiltration and other v436 mods).
IMPLEMENT_CLASS(USkeletalMesh);

//
// v436-compat port: USkeletalMesh's constructor. As in v436, the weapon adjustment starts as the
// identity; everything else starts zeroed. A loaded mesh then reads WeaponAdjust from its package.
//
USkeletalMesh::USkeletalMesh()
{
	guard(USkeletalMesh::USkeletalMesh);
	WeaponAdjust = GMath.UnitCoords;
	unguard;
}

//
// v436-compat port: as ULodMesh::SetScale, but a skeletal mesh gauges LOD on 1/25.6 of its
// bounding-sphere radius instead of 1/128 (retail v436 constant 0.0390625). Called by the editor.
//
void USkeletalMesh::SetScale( FVector NewScale )
{
	guard(USkeletalMesh::SetScale);
	Scale = NewScale;
	MeshScaleMax = (1.f/25.6f) * BoundingSphere.W * Max(Abs(Scale.X), Max(Abs(Scale.Y), Abs(Scale.Z)));
	unguardobj;
}

//
// v436-compat port: reverse the winding of every face (swap each face's first two wedges), as the
// retail v436 USkeletalMesh::FlipFaces does. Editor-only; nothing in the engine calls it at run time.
//
void USkeletalMesh::FlipFaces()
{
	guard(USkeletalMesh::FlipFaces);
	for( INT i=0; i<Faces.Num(); i++ )
		Exchange( Faces(i).iWedge[0], Faces(i).iWedge[1] );
	debugf( TEXT("Flipping all faces for model %s"), GetName() );
	unguardobj;
}

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
	unguard;
}

//
// v436-compat port: render bounding box of a skeletal mesh (retail v436
// USkeletalMesh::GetRenderBoundingBox). It is UMesh's non-animating case, always: the authored
// BoundingBox, scaled about Origin, expanded by one unit and moved into the actor's frame. A
// skeletal mesh has no per-frame BoundingBoxes, so UMesh's animating case must not be used.
//
FBox USkeletalMesh::GetRenderBoundingBox( const AActor* Owner, UBOOL Exact )
{
	guard(USkeletalMesh::GetRenderBoundingBox);
	FBox Bound = BoundingBox;
	FLOAT DrawScale = Owner->bParticles ? 1.5f : Owner->DrawScale;
	Bound = FBox( Scale*DrawScale*(Bound.Min - Origin), Scale*DrawScale*(Bound.Max - Origin) ).ExpandBy(1.0f);
	FCoords Coords = GMath.UnitCoords / RotOrigin / Owner->Rotation;
	Coords.Origin  = Owner->Location + Owner->PrePivot;
	return Bound.TransformBy( Coords.Transpose() );
	unguardobj;
}

/*-----------------------------------------------------------------------------
	USkeletalMesh::GetFrame (v436-compat port of the retail v436 Engine.dll function).
-----------------------------------------------------------------------------*/

//
// Per-actor skeletal state, kept in the global cache under MakeCacheID(CID_SkeletalData, Owner)
// exactly as v436 keeps it: this header, then per bone the track link, the last local position
// and the last local orientation. It is what a tween blends FROM and what links the mesh's bones
// to the current animation's tracks.
//
struct FSkelDataHeader
{
	UMesh*		Mesh;			// Mesh the payload was built for.
	UAnimation*	LinkedAnim;		// Animation Links was built for.
	FLOAT		TweenFrame;		// AnimFrame at the previous tween step.
	FName		TweenSeq;		// Sequence being tweened into.
	INT			Unused;			// v436 never reads or writes this slot.
	UBOOL		bHavePose;		// Pos/Rot hold a sampled pose.
	INT*		Links;			// Per bone: track index in LinkedAnim, or -1.
	FVector*	Pos;			// Per bone: last local position.
	FSkelQuat*	Rot;			// Per bone: last local orientation.
};

//
// Skin the mesh into its owner's animation pose.
//
// The owner's animation is its SkelAnim, which is set to the mesh's DefaultAnimation the first
// time the actor draws without one. Each bone is sampled from its track at the owner's AnimFrame
// (a key-time search, slerp and lerp), or holds its reference pose if the animation has no
// track for it. While the owner tweens (AnimFrame < 0), the pose cached from the last frame
// eases toward the first key of the new sequence instead. Bones are then composed down the
// hierarchy. Each vertex is the weighted sum of its influences' bone-local points (LocalPoints)
// carried by their bones. The weapon bone yields ClassicWeaponCoords, which DrawLodMesh uses for
// the third-person weapon.
//
void USkeletalMesh::GetFrame( FVector* ResultVerts, INT Size, FCoords Coords, AActor* Owner, INT& LODRequest )
{
	guard(USkeletalMesh::GetFrame);

	// Attachments (bAnimByOwner) take their animation from their owner.
	AActor* AnimOwner = ( Owner->bAnimByOwner && Owner->Owner ) ? Owner->Owner : Owner;

	// Find or create this actor's skeletal cache entry.
	INT NumBones = RefSkeleton.Num();
	FCacheItem* Item = NULL;
	QWORD CacheID = MakeCacheID( CID_SkeletalData, Owner );
	FSkelDataHeader* Cache = (FSkelDataHeader*)GCache.Get( CacheID, Item, 8 );
	if( Cache && Cache->Mesh != this )
	{
		Item->Unlock();
		GCache.Flush( CacheID );
		Cache = NULL;
	}
	if( !Cache )
	{
		Cache = (FSkelDataHeader*)GCache.Create( CacheID, Item, sizeof(FSkelDataHeader) + NumBones*(sizeof(INT)+sizeof(FVector)+sizeof(FSkelQuat)), 8 );
		Cache->Mesh       = this;
		Cache->Links      = (INT*)(Cache + 1);
		Cache->Pos        = (FVector*)(Cache->Links + NumBones);
		Cache->Rot        = (FSkelQuat*)(Cache->Pos + NumBones);
		Cache->bHavePose  = 0;
		Cache->TweenFrame = 0.0f;
		Cache->LinkedAnim = NULL;
		Cache->TweenSeq   = NAME_None;	// v436 leaves this uninitialized.
		Cache->Unused     = 0;
	}

	INT   LodVerts  = Min( LODRequest, FrameVerts );
	FLOAT DrawScale = AnimOwner->bParticles ? 1.0f : Owner->DrawScale;

	// Mesh-to-view transform. The skeletal data is mirrored: the negated Y scale un-mirrors it.
	Coords = Coords * (Owner->Location + Owner->PrePivot) * Owner->Rotation * RotOrigin
		* FScale( FVector(Scale.X, -Scale.Y, Scale.Z) * DrawScale, 0.0f, SHEER_None );

	// The owner's animation, linked to the mesh's default the first time it is needed.
	if( !AnimOwner->SkelAnim && DefaultAnimation )
		AnimOwner->SkelAnim = DefaultAnimation;
	UAnimation*       Anim     = AnimOwner->SkelAnim;
	FMeshAnimSeq*     Seq      = NULL;
	FSkelMotionChunk* Move     = NULL;
	FLOAT             AnimTime = 0.0f;
	if( Anim )
	{
		Seq  = Anim->GetAnimSeq( AnimOwner->AnimSequence );
		Move = Anim->GetMovement( AnimOwner->AnimSequence );
		if( Seq )
			AnimTime = Clamp( AnimOwner->AnimFrame, 0.0f, 1.0f ) * Move->TrackTime;
	}
	UBOOL bTween = ( AnimOwner->AnimFrame < 0.0f && Cache->bHavePose );

	// Link the mesh's bones to the animation's tracks, by name, whenever the animation changes.
	TArray<FCoords> BoneCoords;
	BoneCoords.AddZeroed( NumBones );	// v436 leaves these uninitialized (read only if no track path runs).
	if( Anim && Move && Anim != Cache->LinkedAnim )
	{
		guard(SkLinkup);
		for( INT b=0; b<NumBones; b++ )
		{
			Cache->Links[b] = -1;
			for( INT t=0; t<Move->AnimTracks.Num() && t<Anim->RefBones.Num(); t++ )
			{
				if( Anim->RefBones(t).Name == RefSkeleton(b).Name )
				{
					Cache->Links[b] = t;
					break;
				}
			}
			if( Cache->Links[b] == -1 )
				debugf( TEXT("Warning: missing bone/animation link for %s  index %i"), *RefSkeleton(b).Name, RefSkeleton(b).Name.GetIndex() );
		}
		Cache->LinkedAnim = Anim;
		unguard;
	}

	if( Anim && !Seq && bTween )
	{
		// Tweening into a sequence this animation lacks: hold the last pose.
		for( INT b=0; b<NumBones; b++ )
			BoneCoords(b) = Cache->Rot[b].ToCoords( Cache->Pos[b] );
		Cache->TweenFrame = AnimOwner->AnimFrame;
	}
	else if( Anim && Seq && !bTween )
	{
		// Play the sequence: sample every linked bone's track at AnimTime.
		if( Move->AnimTracks.Num() )
		{
			for( INT b=0; b<NumBones; b++ )
			{
				FSkelQuat Q;
				FVector   P;
				INT       t = Cache->Links[b];
				if( t < 0 )
				{
					// No track for this bone: reference pose.
					Q = RefSkeleton(b).BonePos.Orientation;
					P = RefSkeleton(b).BonePos.Position;
				}
				else
				{
					FSkelAnalogTrack& Track = Move->AnimTracks(t);
					INT NumKeys = Track.KeyTime.Num();

					// Key X is the last one at or before AnimTime; Y follows it, wrapping to key 0.
					INT   X     = -1;
					UBOOL bWrap = 0;
					for( INT k=1; k<NumKeys; k++ )
					{
						if( AnimTime < Track.KeyTime(k) )
						{
							X = k-1;
							break;
						}
					}
					if( X == -1 && AnimTime < Move->TrackTime )
						X = NumKeys-1;
					INT Y;
					if( X >= NumKeys-1 )
					{
						bWrap = 1;
						Y     = 0;
					}
					else Y = X+1;
					if( NumKeys == 1 )
						X = Y = 0;

					// v436 reads key -1 when AnimTime reaches TrackTime with no key beyond it, or
					// when a track has no key times. Use key 0 there instead of reading out of range.
					if( X < 0 )
						X = 0;
					FLOAT KeyX  = NumKeys ? Track.KeyTime(X) : 0.0f;
					FLOAT Delta = bWrap ? Move->TrackTime - KeyX : Abs( (NumKeys ? Track.KeyTime(Y) : 0.0f) - KeyX );
					FLOAT Alpha = ( Delta > 0.000001f ) ? ( AnimTime - KeyX ) / Delta : 0.0f;

					// Orientation: slerp X->Y (over the shorter arc when wrapping), then normalize.
					// Key indices are clamped to each key array; on well-formed data (one key, or one
					// per key time) that changes nothing, and it avoids reading past a short array.
					INT NumQuats = Track.KeyQuat.Num();
					if( NumQuats )
					{
						FSkelQuat A = Track.KeyQuat( Min(X,NumQuats-1) );
						FSkelQuat B = Track.KeyQuat( Min(Y,NumQuats-1) );
						if( bWrap )
							SkelAlignQuatWith( B, A );
						Q = SkelSlerpQuat( A, B, Alpha );
						Q.Normalize();
					}
					else Q = RefSkeleton(b).BonePos.Orientation;

					// Position: lerp X->Y when the track moves, else its single key.
					INT NumPos = Track.KeyPos.Num();
					if( NumPos > 1 )
					{
						const FVector& PX = Track.KeyPos( Min(X,NumPos-1) );
						const FVector& PY = Track.KeyPos( Min(Y,NumPos-1) );
						P = PX + (PY - PX) * Alpha;
					}
					else if( NumPos == 1 )
						P = Track.KeyPos(0);
					else
						P = RefSkeleton(b).BonePos.Position;
				}
				Cache->Rot[b] = Q;
				Cache->Pos[b] = P;
				BoneCoords(b) = Q.ToCoords( P );
			}
			Cache->bHavePose = 1;
		}
	}
	else if( Anim && Seq && bTween )
	{
		// Tween: close the fraction Alpha of the remaining gap between the cached pose and the
		// first key of the new sequence. A new tween (other sequence, or Alpha out of range)
		// starts with Alpha 0.
		FLOAT Alpha = 1.0f - AnimOwner->AnimFrame / Cache->TweenFrame;
		if( Cache->TweenSeq != AnimOwner->AnimSequence || !(Alpha >= 0.0f) || Alpha > 1.0f )
		{
			Cache->TweenFrame = -1.0f / Seq->NumFrames;
			Alpha             = 0.0f;
			Cache->TweenSeq   = AnimOwner->AnimSequence;
		}
		for( INT b=0; b<NumBones; b++ )
		{
			INT t = Cache->Links[b];
			if( t >= 0 )
			{
				FSkelAnalogTrack& Track = Move->AnimTracks(t);
				if( Track.KeyQuat.Num() )
				{
					FSkelQuat Target = Track.KeyQuat(0);
					SkelAlignQuatWith( Target, Cache->Rot[b] );
					FSkelQuat Q = SkelSlerpQuat( Cache->Rot[b], Target, Alpha );
					Q.Normalize();
					Cache->Rot[b] = Q;
				}
				if( Track.KeyPos.Num() )
					Cache->Pos[b] = Cache->Pos[b] + (Track.KeyPos(0) - Cache->Pos[b]) * Alpha;
			}
			BoneCoords(b) = Cache->Rot[b].ToCoords( Cache->Pos[b] );
		}
		Cache->TweenFrame = AnimOwner->AnimFrame;
	}
	else
	{
		// No animation, or not tweening into a sequence it lacks: reference pose.
		for( INT b=0; b<NumBones; b++ )
			BoneCoords(b) = RefSkeleton(b).BonePos.Orientation.ToCoords( RefSkeleton(b).BonePos.Position );
	}

	// Compose each bone with its parent (parents precede children in RefSkeleton).
	for( INT b=1; b<BoneCoords.Num(); b++ )
	{
		INT Parent = RefSkeleton(b).ParentIndex;
		check(Parent != b);
		BoneCoords(b) = SkelApplyPivot( BoneCoords(b), BoneCoords(Parent) );
	}

	// Debugging: bone pivots (view space) and per-vertex influence counts.
	if( DisplayBones )
	{
		DebugPivots.Empty();
		DebugPivots.Add( BoneCoords.Num() );
		DebugParents.Empty();
		DebugParents.Add( BoneCoords.Num() );
		for( INT b=0; b<BoneCoords.Num(); b++ )
		{
			DebugParents(b) = RefSkeleton(b).ParentIndex;
			DebugPivots(b)  = (BoneCoords(b).Origin - Origin).TransformPointBy( Coords );
		}
	}
	if( DisplayInfluences && DebugVerts.Num()==0 )
	{
		DebugVerts.Add( FrameVerts );
		TArray<INT> Counts;
		Counts.AddZeroed( FrameVerts );
		for( INT b=0; b<BoneWeightIdx.Num(); b++ )
		{
			INT First = BoneWeightIdx(b).WeightIndex;
			for( INT i=First; i<First+BoneWeightIdx(b).Number; i++ )
				if( BoneWeights(i).PointIndex < FrameVerts && BoneWeights(i).BoneWeight != 0 )
					Counts( BoneWeights(i).PointIndex )++;
		}
		for( INT v=0; v<FrameVerts; v++ )
			DebugVerts(v) = Min( Counts(v), 255 );
	}

	// Skin: each vertex is the weighted sum of its influences' local points carried by their bones.
	TArray<FVector> Verts;
	Verts.AddZeroed( LodVerts );
	INT NumSkinBones = Min( BoneWeightIdx.Num(), BoneCoords.Num() );
	for( INT b=0; b<NumSkinBones; b++ )
	{
		const FCoords& C = BoneCoords(b);
		INT First = BoneWeightIdx(b).WeightIndex;
		for( INT i=First; i<First+BoneWeightIdx(b).Number; i++ )
		{
			INT PointIndex = BoneWeights(i).PointIndex;
			if( PointIndex < LodVerts )
			{
				FLOAT Weight = BoneWeights(i).BoneWeight * (1.0f/65535.0f);
				Verts(PointIndex) += (C.Origin + LocalPoints(i).TransformVectorBy( C )) * Weight;
			}
		}
	}

	// Transform to view space into the caller's buffer.
	FCoords VertCoords = Coords;
	VertCoords.Origin += Origin;
	BYTE* Out = (BYTE*)ResultVerts;
	for( INT v=0; v<Verts.Num(); v++ )
	{
		*(FVector*)Out = Verts(v).TransformPointBy( VertCoords );
		Out += Size;
	}
	LODRequest = LodVerts;

	// Weapon: carry the weapon bone by WeaponAdjust, bring it to view space and express it in the
	// classic special-face form (origin + X/Y/Z axes) that DrawLodMesh expects in SpecialCoords.
	if( WeaponBoneIndex > -1 )
	{
		FCoords WC = SkelApplyPivot( WeaponAdjust, BoneCoords(WeaponBoneIndex) );
		FCoords WV;
		WV.Origin      = (WC.Origin - Origin).TransformPointBy( Coords );
		FVector XEnd   = (WC.Origin + FVector(1,0,0).TransformVectorBy(WC) - Origin).TransformPointBy( Coords );
		WV.XAxis       = (XEnd - WV.Origin).SafeNormal();
		FVector ZEnd   = (WC.Origin + FVector(0,0,1).TransformVectorBy(WC) - Origin).TransformPointBy( Coords );
		FVector YAxis  = (WV.XAxis ^ (ZEnd - WV.Origin)).SafeNormal();
		WV.ZAxis       = WV.XAxis ^ YAxis;
		WV.YAxis       = -YAxis;
		FCoords CWC    = GMath.UnitCoords;
		CWC.Origin     = CWC.Origin.TransformPointBy( WV );
		CWC.XAxis      = CWC.XAxis.TransformVectorBy( WV );
		CWC.YAxis      = CWC.YAxis.TransformVectorBy( WV );
		CWC.ZAxis      = CWC.ZAxis.TransformVectorBy( WV );
		ClassicWeaponCoords = CWC;
	}

	Item->Unlock();
	unguard;
}

/*----------------------------------------------------------------------------
	The End.
----------------------------------------------------------------------------*/
