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

// -skelanimlog / -skelnoblend (defined near ComputeSkinnedPose, used from Serialize/GetFrame above it).
static UBOOL SkelAnimLogging();

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
	if( SkelAnimLogging() )
		debugf( NAME_Log, TEXT("[skelload] %s bones=%i points=%i wedges=%i anim=%s"),
			GetName(), RefSkeleton.Num(), Points.Num(), ExtWedges.Num(),
			DefaultAnimation ? DefaultAnimation->GetName() : TEXT("none") );
	unguard;
}

//
// v436-compat: render-visibility bounding box for a skeletal mesh.
//
// Without this override a skeletal mesh inherits UMesh::GetRenderBoundingBox. Once our LinkSkelAnim
// step (in GetFrame) copies DefaultAnimation->AnimSeqs into the mesh, that base method's
// GetAnimSeq(Owner->AnimSequence) lookup succeeds and it takes the "animating" branch, reading
// BoundingBoxes(iFrame) — a PER-FRAME array that is a vertex-mesh import feature and is empty for a
// skeletal mesh. The result is a degenerate box, the actor fails the frustum test, and the mesh
// blinks invisible for a frame or two during animation or at glancing camera angles. Real UT (v436)
// overrides this and the models never flash.
//
// The P266-era engine used precomputed bounds, not a per-frame recompute, so we do too: resolve a
// mesh-space box ONCE (the authored singular BoundingBox if valid, else an AABB over the skin
// vertices Points), cache it, and transform it into world space exactly like UMesh's static
// (non-animating) branch. This is render culling only — hit detection uses the actor's
// CollisionRadius/CollisionHeight, so aiming/accuracy is unaffected.
//
FBox USkeletalMesh::GetRenderBoundingBox( const AActor* Owner, UBOOL Exact )
{
	guard(USkeletalMesh::GetRenderBoundingBox);

	// Resolve the mesh-space box once and cache it.
	if( !bBoundComputed )
	{
		bBoundComputed = 1;
		if( BoundingBox.IsValid && !(BoundingBox.Min==BoundingBox.Max) )
			LocalBound = BoundingBox;						// authored box (spans all anim poses)
		else
		{
			// Authored box missing/degenerate: fall back to an AABB over the skin vertices.
			FBox PointsBound(0);
			for( INT i=0; i<Points.Num(); i++ )
				PointsBound += Points(i);
			LocalBound = PointsBound;
		}
	}
	if( !LocalBound.IsValid )
		return Super::GetRenderBoundingBox( Owner, Exact );	// no geometry to bound; defer to base.

	// Transform the mesh-space box into world space exactly like UMesh::GetRenderBoundingBox's static
	// (non-animating) branch: scale about Origin, expand a hair, then rotate/translate by the actor.
	FLOAT DrawScale = Owner->bParticles ? 1.5f : Owner->DrawScale;
	FBox Bound = FBox( Scale*DrawScale*(LocalBound.Min - Origin), Scale*DrawScale*(LocalBound.Max - Origin) ).ExpandBy(1.0f);
	FCoords Coords = GMath.UnitCoords / RotOrigin / Owner->Rotation;
	Coords.Origin  = Owner->Location + Owner->PrePivot;
	return Bound.TransformBy( Coords.Transpose() );
	unguardobj;
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
	UBOOL bSkinned = ( Points.Num() && ComputeSkinnedPose( Owner, Skinned ) && Skinned.Num()==Points.Num() );
	const FVector* Src = bSkinned ? &Skinned(0) : ( Points.Num() ? &Points(0) : NULL );
	if( SkelAnimLogging() )
		debugf( NAME_Log, TEXT("[skeldraw] %s mesh=%s skinned=%i seq=%s af=%f anim=%s bones=%i"),
			Owner->GetName(), GetName(), bSkinned?1:0, *Owner->AnimSequence, Owner->AnimFrame,
			DefaultAnimation ? DefaultAnimation->GetName() : TEXT("none"), RefSkeleton.Num() );

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
		// The weapon seats at the right spot but rotated 180 deg (upside down, barrel toward the
		// pawn). Rotate the frame 180 deg about its side (Y) axis -- pivot stays at the grip origin --
		// which flips the forward (X) and up (Z) axes.
		WeaponAttachX = WeaponAttachX * -1.0f;
		WeaponAttachZ = WeaponAttachZ * -1.0f;
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

// Largest key index X with KeyTime[X] <= t (linear scan; tracks are short, <=~121 keys). Matches
// UEViewer's FindTimeKey.
static INT SkelFindTimeKey( const TArray<FLOAT>& KeyTime, FLOAT t )
{
	INT n = KeyTime.Num(), X = 0;
	for( INT i=0; i<n; i++ )
	{
		if( KeyTime(i) <= t ) X = i;
		else break;
	}
	return X;
}

// Sample an analog track for the owner's current position in the sequence.
//
// UE1 tracks are KEY-REDUCED at IRREGULAR times: a bone may have (e.g.) 4 rotation keys at KeyTime
// 0.0, 0.333, 3.533, 4.0 seconds out of a 4.03s sequence. The keys carry an explicit KeyTime (in
// seconds, 0..TrackTime). Sampling them as evenly spaced (the old behavior, also what UEViewer's
// UMeshAnimation::Upgrade does after emptying KeyTime) time-warps each reduced bone differently, so
// bones desync and each "plays slow then snaps" at its own phase -- the per-cycle jerkiness seen at
// slomo. Sample by KeyTime instead (bracket the current time, interpolate by the real time fraction),
// which is what the stock engine does. Logic mirrors UEViewer's CAnimTrack::GetBonePosition +
// GetKeyParams, in seconds (t = AnimFrame01 * TrackTime, loop endpoint = TrackTime).
//
// Only when a track has no timing data (KeyTime empty) do we fall back to even spacing. KeyPos and
// KeyQuat are each either static (1 key) or share KeyTime's count; Min() guards keep indexing safe.
static void SkelSampleTrack( const FSkelAnalogTrack& T, FLOAT Frame, INT NumFrames, FLOAT TrackTime, UBOOL Loop, FVector& OutPos, FSkelQuat& OutRot )
{
	INT NP = T.KeyPos.Num(), NR = T.KeyQuat.Num(), NT = T.KeyTime.Num();
	if( NumFrames < 1 ) NumFrames = 1;
	FLOAT AnimFrame01 = Clamp( Frame / NumFrames, 0.f, 1.f );

	if( NT>1 && TrackTime>0.0001f )
	{
		// Time-based sampling (correct for reduced/irregular tracks).
		FLOAT t = AnimFrame01 * TrackTime;
		INT X = SkelFindTimeKey( T.KeyTime, t );
		INT Y = X + 1;
		FLOAT F;
		if( Y >= NT )
		{
			if( !Loop ) { Y = X; F = 0.0f; }							// clamp: hold last key
			else { FLOAT d = TrackTime - T.KeyTime(X); Y = 0; F = d>0.0001f ? (t - T.KeyTime(X))/d : 0.0f; }	// blend last->first
		}
		else
		{
			FLOAT d = T.KeyTime(Y) - T.KeyTime(X);
			F = d>0.0001f ? (t - T.KeyTime(X))/d : 0.0f;
		}
		if( NP==1 ) OutPos = T.KeyPos(0);
		else if( NP>1 ) { INT px=Min(X,NP-1), py=Min(Y,NP-1); OutPos = T.KeyPos(px) + (T.KeyPos(py)-T.KeyPos(px))*F; }
		if( NR==1 ) OutRot = T.KeyQuat(0);
		else if( NR>1 ) OutRot = SkelSlerp( T.KeyQuat(Min(X,NR-1)), T.KeyQuat(Min(Y,NR-1)), F );
		return;
	}

	// Fallback: no per-key timing -> evenly spaced across the sequence (loop wraps last->first key).
	FLOAT FrameC = AnimFrame01 * NumFrames;
	if( NP==1 ) OutPos = T.KeyPos(0);
	else if( NP>1 )
	{
		FLOAT pos = FrameC / NumFrames * NP;
		INT x = appFloor(pos); FLOAT f = pos - x;
		if( x<0 ) { x=0; f=0; } if( x>=NP ) { x=NP-1; f=0; }
		INT y = x+1; if( y>=NP ) y = Loop ? 0 : x;
		OutPos = T.KeyPos(x) + (T.KeyPos(y) - T.KeyPos(x)) * f;
	}
	if( NR==1 ) OutRot = T.KeyQuat(0);
	else if( NR>1 )
	{
		FLOAT pos = FrameC / NumFrames * NR;
		INT x = appFloor(pos); FLOAT f = pos - x;
		if( x<0 ) { x=0; f=0; } if( x>=NR ) { x=NR-1; f=0; }
		INT y = x+1; if( y>=NR ) y = Loop ? 0 : x;
		OutRot = SkelSlerp( T.KeyQuat(x), T.KeyQuat(y), f );
	}
}

//
// v436-compat animation blending: per-actor tween state, held in the engine's GCache exactly as
// ULodMesh::GetFrame holds its vertex-tween state. The payload following this header is
// CachedBones x FSkelBonePose -- the LOCAL pose currently being displayed, which is both what the
// next frame blends FROM and the reason an interrupted blend resumes instead of snapping.
//
// CachedMesh/CachedBones guard against reading a payload written for a different mesh (the cache is
// keyed on the actor AND the mesh, so this is belt and braces, and it also sizes the payload).
//
struct FSkelTweenHeader
{
	UMesh*	CachedMesh;
	INT		CachedBones;
	FName	CachedSeq;
	FLOAT	CachedFrame;
	FLOAT	TweenIndicator;
};

// Angle between two unit quaternions, in degrees. Instrumentation only. There is no appAcos in this
// engine's math family, and atan2(sin,cos) is better conditioned near 0 anyway: |dot| is cos(a/2).
static FLOAT SkelQuatAngleDeg( const FSkelQuat& a, const FSkelQuat& b )
{
	FLOAT d = Clamp( Abs( a.X*b.X + a.Y*b.Y + a.Z*b.Z + a.W*b.W ), 0.f, 1.f );
	return (FLOAT)( 2.0 * appAtan2( appSqrt(1.0-(DOUBLE)d*(DOUBLE)d), (DOUBLE)d ) * 180.0 / PI );
}

// -skelanimlog on the command line turns on the per-frame bone-delta trace below. Resolved once.
static UBOOL SkelAnimLogging()
{
	static INT Enabled = -1;
	if( Enabled < 0 )
		Enabled = ParseParam( appCmdLine(), TEXT("skelanimlog") ) ? 1 : 0;
	return Enabled != 0;
}

// -skelnoblend restores the pre-blend behaviour (a tweening AnimFrame clamps to 0, so the pose snaps
// to the new sequence's first frame). It exists so the blending test can prove its own instrument:
// a measurement that only ever reports "smooth" is worth nothing until it has been shown to report
// "snap" for the code that actually snapped. Not a user-facing option.
static UBOOL SkelBlendDisabled()
{
	static INT Disabled = -1;
	if( Disabled < 0 )
		Disabled = ParseParam( appCmdLine(), TEXT("skelnoblend") ) ? 1 : 0;
	return Disabled != 0;
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

	// ---- Sequence blending (tweening) --------------------------------------------------------
	//
	// A NEGATIVE AnimFrame is UE1's "tweening" state. PlayAnim/LoopAnim/TweenAnim (UnScript.cpp) set
	// AnimSequence to the NEW sequence immediately and AnimFrame to -1/NumFrames, then UnLevTic.cpp
	// advances it up to 0 at TweenRate. So while tweening, AnimSequence already names the sequence we
	// are heading INTO, its first frame is the target pose, and AnimFrame measures the remaining gap.
	//
	// This used to sample at Clamp(AnimFrame,0,1), which clamps a tweening (negative) AnimFrame flat
	// to 0 -- the pose jumped to the new sequence's frame 0 the instant PlayAnim was called. Walking
	// -> jumping snapped; a fast crouch/stand toggle teleported between poses.
	//
	// The fix is ULodMesh::GetFrame's tween, moved from vertices to bones: keep the CURRENT pose in a
	// per-actor cache and, each frame, close a fraction Alpha of the REMAINING gap toward the target
	// (Cached += (Target-Cached)*Alpha). Because every step starts from where the model actually is
	// rather than from a remembered start pose, interrupting a blend simply retargets it and the model
	// resumes from its current pose -- it never jumps. Alpha = 1 - now/before makes the accumulated
	// blend exactly linear in AnimFrame while staying expressible as that incremental step.
	//
	// Bones are blended in LOCAL space (position lerp + quaternion slerp) and composed up the
	// hierarchy afterwards, so each bone stays rigid through the blend -- which blending composed
	// world frames, or the vertex path's blending of skinned points, would not guarantee.
	//
	UBOOL bTween = ( AnimOwner->AnimFrame < 0.0f ) && !SkelBlendDisabled();
	FLOAT Frame  = bTween ? 0.0f : Clamp( AnimOwner->AnimFrame, 0.f, 1.f ) * NumFrames;

	// Sample the sequence at Frame: the pose we are heading TOWARD (and the pose itself when not
	// tweening). Bones with no matching track keep their reference-skeleton local pose.
	TArray<FSkelBonePose> Target; Target.Add(NB);
	for( INT b=0; b<NB; b++ )
	{
		Target(b).Pos = RefSkeleton(b).BonePos.Position;
		Target(b).Rot = RefSkeleton(b).BonePos.Orientation;
		// Map mesh bone -> animation track by bone name.
		INT ab = INDEX_NONE;
		for( INT j=0; j<Anim->RefBones.Num(); j++ )
			if( Anim->RefBones(j).Name == RefSkeleton(b).Name ) { ab = j; break; }
		if( ab!=INDEX_NONE && ab<Move.AnimTracks.Num() )
			SkelSampleTrack( Move.AnimTracks(ab), Frame, NumFrames, Move.TrackTime, AnimOwner->bAnimLoop, Target(b).Pos, Target(b).Rot );
	}

	// Resolve the pose actually drawn, blending through the per-actor cache.
	TArray<FSkelBonePose> Pose; Pose.Add(NB);
	FLOAT MaxBoneDelta = -1.0f;		// instrumentation only; see SkelQuatAngleDeg.
	{
		// Keyed on (actor, mesh): distinct from ULodMesh::GetFrame's vertex tween cache, which keys on
		// (actor, NULL), so the two can never read each other's payload.
		FCacheItem* Item    = NULL;
		QWORD       CacheID = MakeCacheID( CID_TweenAnim, Owner, this );
		BYTE*       Mem     = GCache.Get( CacheID, Item );
		FSkelTweenHeader* Hdr = (FSkelTweenHeader*)Mem;
		UBOOL WasCached = ( Mem!=NULL && Hdr->CachedMesh==this && Hdr->CachedBones==NB );
		if( Mem && !WasCached )
		{
			// Stale entry (the actor's mesh or its bone count changed): drop it and start clean.
			Item->Unlock();
			GCache.Flush( CacheID );
			Mem = NULL;
		}
		if( !Mem )
		{
			Mem = GCache.Create( CacheID, Item, sizeof(FSkelTweenHeader) + NB*sizeof(FSkelBonePose) );
			Hdr = (FSkelTweenHeader*)Mem;
			Hdr->CachedMesh     = this;
			Hdr->CachedBones    = NB;
			Hdr->CachedSeq      = NAME_None;
			Hdr->CachedFrame    = 0.0f;
			Hdr->TweenIndicator = 1.0f;
		}
		FSkelBonePose* Cached = (FSkelBonePose*)( Mem + sizeof(FSkelTweenHeader) );

		if( !bTween || !WasCached )
		{
			// Normal playback -- or this actor's first draw, where there is no previous pose to blend
			// from and the sampled pose is the only honest answer. Record it either way: this is what a
			// later tween blends FROM, and keeping it current is the whole reason an interruption
			// resumes smoothly instead of snapping.
			for( INT b=0; b<NB; b++ )
			{
				if( WasCached )
					MaxBoneDelta = Max( MaxBoneDelta, SkelQuatAngleDeg( Cached[b].Rot, Target(b).Rot ) );
				Cached[b] = Target(b);
			}
			Hdr->CachedSeq      = AnimOwner->AnimSequence;
			Hdr->CachedFrame    = AnimOwner->AnimFrame;
			Hdr->TweenIndicator = 1.0f;		// the cache IS the target pose: nothing left to blend.
			// ⚠ That invariant is also what makes the OTHER way into this branch correct. A first draw
			// that happens mid-tween stores a NEGATIVE CachedFrame, so the next frame computes a real
			// Alpha rather than tripping the divisor guard below — but TweenIndicator is 1, the 0.97
			// cutoff fires, and the pose holds. Which is right: the cache already holds the target
			// (Frame is 0 while tweening), so there was never anything to blend from.
		}
		else
		{
			// Alpha = the fraction of the remaining gap to close this frame. During a tween both frames
			// are negative and rising toward 0, so a tween in progress yields Alpha in [0,1]. Anything
			// else means a NEW tween just began -- including the normal-playback exit above, which
			// leaves CachedFrame >= 0 and so trips the divisor guard on the tween's very first frame.
			FLOAT Alpha = ( Hdr->CachedFrame < 0.0f ) ? ( 1.0f - AnimOwner->AnimFrame / Hdr->CachedFrame ) : -1.0f;
			if( Hdr->CachedSeq != AnimOwner->AnimSequence || Alpha < 0.0f || Alpha > 1.0f )
			{
				// A tween just started. Adopt it and blend NOTHING this frame (Alpha=0) so the pose
				// stays exactly where it is; the following frames ease away from there.
				//
				// Deviation from ULodMesh::GetFrame, deliberately: it clears TweenIndicator only on a
				// sequence CHANGE. Re-tweening into the sequence already playing (PlayAnim on the
				// current sequence -- crouch, stand, crouch again) therefore inherited the finished
				// tween's indicator of ~1, which forces Alpha to 0 below for the entire tween: the pose
				// freezes and then jumps when normal playback resumes. Clearing it on every restart is
				// what makes a fast toggle blend instead of teleport.
				Hdr->CachedSeq      = AnimOwner->AnimSequence;
				Hdr->TweenIndicator = 0.0f;
				Alpha               = 0.0f;
			}

			// Progress tracker, same exponential form: 1-TweenIndicator is the fraction of the original
			// gap still to cover. Past 0.97 the remainder is not worth another pass over the bones.
			Hdr->TweenIndicator += ( 1.0f - Hdr->TweenIndicator ) * Alpha;
			if( Hdr->TweenIndicator > 0.97f )
				Alpha = 0.0f;

			MaxBoneDelta = 0.0f;
			if( Alpha > 0.0f )
				for( INT b=0; b<NB; b++ )
				{
					FSkelQuat Blended = SkelSlerp( Cached[b].Rot, Target(b).Rot, Alpha );
					MaxBoneDelta   = Max( MaxBoneDelta, SkelQuatAngleDeg( Cached[b].Rot, Blended ) );
					Cached[b].Pos += ( Target(b).Pos - Cached[b].Pos ) * Alpha;
					Cached[b].Rot  = Blended;
				}

			// Drawing the same actor twice in one frame must not advance the blend twice: with
			// CachedFrame == AnimFrame the next call computes Alpha = 0 and holds.
			Hdr->CachedFrame = AnimOwner->AnimFrame;
		}

		for( INT b=0; b<NB; b++ )
			Pose(b) = Cached[b];
		Item->Unlock();
	}

	// Instrumentation for the blending E2E (-skelanimlog): smoothness is not OCR-able, but a snap is,
	// by definition, the whole pose change delivered in ONE frame. Report the largest per-frame bone
	// rotation step so a spec can assert that a sequence switch produces a bounded ramp rather than a
	// single spike. Off unless asked for -- this is one line per skeletal actor per drawn frame.
	if( MaxBoneDelta >= 0.0f && SkelAnimLogging() )
		debugf( NAME_Log, TEXT("[skelanim] %s seq=%s af=%f tween=%i maxdeg=%f"),
			Owner->GetName(), *AnimOwner->AnimSequence, AnimOwner->AnimFrame, bTween?1:0, MaxBoneDelta );

	ComputeBindPose();

	// Animated bone world coords + per-bone skinning transform (anim∘bindInverse).
	TArray<FSkelCoords> AC; AC.Add(NB);		// animated world coords
	TArray<FSkelCoords> Xf; Xf.Add(NB);		// skinning transform (ref pose -> anim pose)
	for( INT b=0; b<NB; b++ )
	{
		FVector   lp = Pose(b).Pos;
		FSkelQuat lr = Pose(b).Rot;
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
