/*=============================================================================
	UnMesh.h: Unreal mesh objects.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

/*-----------------------------------------------------------------------------
	FMeshVert.
-----------------------------------------------------------------------------*/

// Packed mesh vertex point for skinned meshes.
#define GET_MESHVERT_DWORD(mv) (*(DWORD*)&(mv))
struct FMeshVert
{
	// Variables.
#if __INTEL_BYTE_ORDER__
	INT X:11; INT Y:11; INT Z:10;
#else
	INT Z:10; INT Y:11; INT X:11;
#endif

	// Constructor.
	FMeshVert()
	{}
	FMeshVert( const FVector& In )
	: X((INT)In.X), Y((INT)In.Y), Z((INT)In.Z)
	{}

	// Functions.
	FVector Vector() const
	{
		return FVector( X, Y, Z );
	}

	// Serializer.
	friend FArchive& operator<<( FArchive& Ar, FMeshVert& V )
	{
		return Ar << GET_MESHVERT_DWORD(V);
	}
};

/*-----------------------------------------------------------------------------
	FMeshTri.
-----------------------------------------------------------------------------*/

// Texture coordinates associated with a vertex and one or more mesh triangles.
// All triangles sharing a vertex do not necessarily have the same texture
// coordinates at the vertex.
struct FMeshUV
{
	BYTE U;
	BYTE V;
	friend FArchive &operator<<( FArchive& Ar, FMeshUV& M )
		{return Ar << M.U << M.V;}
};

// One triangular polygon in a mesh, which references three vertices,
// and various drawing/texturing information.
struct FMeshTri
{
	_WORD		iVertex[3];		// Vertex indices.
	FMeshUV		Tex[3];			// Texture UV coordinates.
	DWORD		PolyFlags;		// Surface flags.
	INT			TextureIndex;	// Source texture index.
	friend FArchive &operator<<( FArchive& Ar, FMeshTri& T )
	{
		Ar << T.iVertex[0] << T.iVertex[1] << T.iVertex[2];
		Ar << T.Tex[0] << T.Tex[1] << T.Tex[2];
		Ar << T.PolyFlags << T.TextureIndex;
		return Ar;
	}
};

/*-----------------------------------------------------------------------------
	LOD Mesh structs.
-----------------------------------------------------------------------------*/

// Compact structure to send processing info to mesh digestion at import time.
struct ULODProcessInfo
{
	UBOOL LevelOfDetail;
	UBOOL NoUVData;
	UBOOL OldAnimFormat;
	INT   SampleFrame;
	INT   MinVerts;
	INT   Style;
};

// LOD-style Textured vertex struct. references one vertex, and 
// contains texture U,V information. 4 bytes.
// One triangular polygon in a mesh, which references three vertices,
// and various drawing/texturing information. 
struct FMeshWedge
{
	_WORD		iVertex;		// Vertex index.
	FMeshUV		TexUV;			// Texture UV coordinates. ( 2 bytes)
	friend FArchive &operator<<( FArchive& Ar, FMeshWedge& T )
	{
		Ar << T.iVertex << T.TexUV;
		return Ar;
	}

	FMeshWedge& operator=( const FMeshWedge& Other )
	{
		// Superfast copy by considering it as a DWORD.
		*(DWORD*)this = *(DWORD*)&Other;
		return *this;
	}	
};


// LOD-style triangular polygon in a mesh, which references three textured vertices.  8 bytes.
struct FMeshFace
{
	_WORD		iWedge[3];		// Textured Vertex indices.
	_WORD		MaterialIndex;	// Source Material (= texture plus unique flags) index.

	friend FArchive &operator<<( FArchive& Ar, FMeshFace& F )
	{
		Ar << F.iWedge[0] << F.iWedge[1] << F.iWedge[2];
		Ar << F.MaterialIndex;
		return Ar;
	}

	FMeshFace& operator=( const FMeshFace& Other )
	{
		guardSlow(FMeshFace::operator=);
		this->iWedge[0] = Other.iWedge[0];
		this->iWedge[1] = Other.iWedge[1];
		this->iWedge[2] = Other.iWedge[2];
		this->MaterialIndex = Other.MaterialIndex;
		return *this;
		unguardSlow;
	}	
};

// LOD-style mesh material.
struct FMeshMaterial
{
	DWORD		PolyFlags;		// Surface flags.
	INT			TextureIndex;	// Source texture index.
	friend FArchive &operator<<( FArchive& Ar, FMeshMaterial& M )
	{
		return Ar << M.PolyFlags << M.TextureIndex;
	}
};

/*-----------------------------------------------------------------------------
	FMeshAnimNotify.
-----------------------------------------------------------------------------*/

// An actor notification event associated with an animation sequence.
struct FMeshAnimNotify
{
	FLOAT	Time;			// Time to occur, 0.0-1.0.
	FName	Function;		// Name of the actor function to call.
	friend FArchive &operator<<( FArchive& Ar, FMeshAnimNotify& N )
		{return Ar << N.Time << N.Function;}
	FMeshAnimNotify()
		: Time(0.0), Function(NAME_None) {}
};

/*-----------------------------------------------------------------------------
	FMeshAnimSeq.
-----------------------------------------------------------------------------*/

// Information about one animation sequence associated with a mesh,
// a group of contiguous frames.
struct FMeshAnimSeq
{
	FName					Name;		// Sequence's name.
	FName					Group;		// Group.
	INT						StartFrame;	// Starting frame number.
	INT						NumFrames;	// Number of frames in sequence.
	FLOAT					Rate;		// Playback rate in frames per second.
	TArray<FMeshAnimNotify> Notifys;	// Notifications.
	friend FArchive &operator<<( FArchive& Ar, FMeshAnimSeq& A )
		{return Ar << A.Name << A.Group << A.StartFrame << A.NumFrames << A.Notifys << A.Rate;}
	FMeshAnimSeq()
		: Name(NAME_None), Group(NAME_None), StartFrame(0), NumFrames(0), Rate(30.0), Notifys() {}
};

/*-----------------------------------------------------------------------------
	FMeshVertConnect.
-----------------------------------------------------------------------------*/

// Says which triangles a particular mesh vertex is associated with.
// Precomputed so that mesh triangles can be shaded with Gouraud-style
// shared, interpolated normal shading.
struct FMeshVertConnect
{
	INT	NumVertTriangles;
	INT	TriangleListOffset;
	friend FArchive &operator<<( FArchive& Ar, FMeshVertConnect& C )
		{return Ar << C.NumVertTriangles << C.TriangleListOffset;}
};

/*-----------------------------------------------------------------------------
	UMesh.
-----------------------------------------------------------------------------*/

//
// A mesh, completely describing a 3D object (creature, weapon, etc) and
// its animation sequences.  Does not reference textures.
//
class ENGINE_API UMesh : public UPrimitive
{
	DECLARE_CLASS(UMesh,UPrimitive,0)

	// Objects.
	TLazyArray<FMeshVert>			Verts;
	TLazyArray<FMeshTri>			Tris;
	TArray<FMeshAnimSeq>			AnimSeqs;
	TLazyArray<FMeshVertConnect>	Connects;
	TArray<FBox>					BoundingBoxes;
	TArray<FSphere>					BoundingSpheres;//!!currently broken
	TLazyArray<INT>					VertLinks;
	TArray<UTexture*>				Textures;
	TArray<FLOAT>					TextureLOD;

	// Counts.
	INT						FrameVerts;
	INT						AnimFrames;

	// Render info.
	DWORD					AndFlags;
	DWORD					OrFlags;

	// Scaling.
	FVector					Scale;		// Mesh scaling.
	FVector 				Origin;		// Origin in original coordinate system.
	FRotator				RotOrigin;	// Amount to rotate when importing (mostly for yawing).

	// Editing info.
	INT						CurPoly;	// Index of selected polygon.
	INT						CurVertex;	// Index of selected vertex.

	// UObject interface.
	UMesh();
	void Serialize( FArchive& Ar );

	// UPrimitive interface.
	FBox GetRenderBoundingBox( const AActor* Owner, UBOOL Exact );
	FSphere GetRenderBoundingSphere( const AActor* Owner, UBOOL Exact );
	UBOOL LineCheck
	(
		FCheckResult&	Result,
		AActor*			Owner,
		FVector			End,
		FVector			Start,
		FVector			Size,
		DWORD           ExtraNodeFlags
	);

	// UMesh interface.
	UMesh( INT NumPolys, INT NumVerts, INT NumFrames );
	virtual const FMeshAnimSeq* GetAnimSeq( FName SeqName ) const
	{
		guardSlow(UMesh::GetAnimSeq);
		for( INT i=0; i<AnimSeqs.Num(); i++ )
			if( SeqName == AnimSeqs(i).Name )
				return &AnimSeqs(i);
		return NULL;
		unguardSlow;
	}
	virtual FMeshAnimSeq* GetAnimSeq( FName SeqName )
	{
		guardSlow(UMesh::GetAnimSeq);
		for( INT i=0; i<AnimSeqs.Num(); i++ )
			if( SeqName == AnimSeqs(i).Name )
				return &AnimSeqs(i);
		return NULL;
		unguardSlow;
	}
	virtual void GetFrame( FVector* Verts, INT Size, FCoords Coords, AActor* Owner );
	void AMD3DGetFrame( FVector* Verts, INT Size, FCoords Coords, AActor* Owner );
	virtual UTexture* GetTexture( INT Count, AActor* Owner )
	{
		guardSlow(UMesh::GetTexture);
		if( Owner && Owner->GetSkin( Count ) )
			return Owner->GetSkin( Count );
		else if( Count!=0 && Textures(Count) )
			return Textures(Count);
		else if( Owner && Owner->Skin )
			return Owner->Skin;
		else
			return Textures(Count);
		unguardSlow;
	}
	virtual void SetScale( FVector NewScale );
};

/*-----------------------------------------------------------------------------
	ULodMesh.
-----------------------------------------------------------------------------*/

/*-----------------------------------------------------------------------------
	ULodMesh.
-----------------------------------------------------------------------------*/

//
// A LodMesh, completely describing a 3D object (creature, weapon, etc) and
// its animation sequences.  Does not reference textures.
//
class ENGINE_API ULodMesh : public UMesh
{
	DECLARE_CLASS(ULodMesh,UMesh,0)

	// LOD-specific objects.
	// Make lazy arrays where useful.
	TArray<_WORD>			CollapsePointThus;  // Lod-collapse single-linked list for points.
	TArray<_WORD>           FaceLevel;          // Minimum lod-level indicator for each face.
	TArray<FMeshFace>       Faces;              // Faces 
	TArray<_WORD>			CollapseWedgeThus;  // Lod-collapse single-linked list for the wedges.
	TArray<FMeshWedge>		Wedges;             // 'Hoppe-style' textured vertices.
	TArray<FMeshMaterial>   Materials;          // Materials
	TArray<FMeshFace>       SpecialFaces;       // Invisible special-coordinate faces.

	// Misc Internal.
	INT    ModelVerts;     // Number of 'visible' vertices.
	INT	   SpecialVerts;   // Number of 'invisible' (special attachment) vertices.

	// Max of x/y/z mesh scale for LOD gauging (works on top of drawscale).
	FLOAT  MeshScaleMax;

	// Script-settable LOD controlling parameters.
	FLOAT  LODStrength;    // Scales the (not necessarily linear) falloff of vertices with distance.
	INT    LODMinVerts;    // Minimum number of vertices with which to draw a model.
	FLOAT  LODMorph;       // >0.0 = allow morphing ; 0.0-1.0 = range of vertices to morph.
	FLOAT  LODZDisplace;   // Z displacement for LOD distance-dependency tweaking.
	FLOAT  LODHysteresis;  // Controls LOD-level change delay and morphing.

	// Remapping of animation vertices.
	TArray<_WORD> RemapAnimVerts;
	INT    OldFrameVerts;  // Possibly different old per-frame vertex count.
	
	//  UObject interface.
	ULodMesh(){};
	void Serialize( FArchive& Ar );

	//  UMesh interface.
	void SetScale( FVector NewScale );
	ULodMesh( INT NumPolys, INT NumVerts, INT NumFrames );
	
	// GetFrame for LOD.
	virtual void GetFrame( FVector* Verts, INT Size, FCoords Coords, AActor* Owner, INT& LODRequest );
};

/*-----------------------------------------------------------------------------
	Skeletal mesh / animation serialization structs (v436-compat port).

	v400 predates UE1 skeletal meshes, so the engine has no FQuat and none of the
	skeletal serialization structs. v436-era mods (Infiltration, NeoCairo, TacticalOps,
	Onslaught, OldSkool/ONP, ...) ship real USkeletalMesh/UAnimation exports; loading
	them on the minimal (empty) classes left the archive short and tripped the fatal
	"Serial size mismatch". We now deserialize the skeletal blob byte-for-byte so those
	packages load. Skeletal *rendering* stays a non-goal — the data is consumed and kept
	but the meshes are not skinned/drawn (skeletal actors show no mesh).

	Byte format ported from the v432 public headers (Ut99PubSrc/Engine/Inc/UnSkeletalMesh.h)
	and cross-checked against SurrealEngine's USkeletalMesh::Load / UAnimation::Load. FQuat
	is represented as FSkelQuat (4 raw floats) since we never do quaternion math on it.
-----------------------------------------------------------------------------*/

// A quaternion on disk: 4 floats. (v400 has no FQuat; we only need byte-accurate I/O.)
struct FSkelQuat
{
	FLOAT X, Y, Z, W;
	FSkelQuat() {}
	FSkelQuat( FLOAT InX, FLOAT InY, FLOAT InZ, FLOAT InW ) : X(InX), Y(InY), Z(InZ), W(InW) {}
	friend FArchive& operator<<( FArchive& Ar, FSkelQuat& Q )
	{
		return Ar << Q.X << Q.Y << Q.Z << Q.W;
	}
	// Conjugate (inverse rotation for unit quats).
	FSkelQuat Conj() const { return FSkelQuat(-X,-Y,-Z,W); }
	// Hamilton product: (a*b) applies rotation b, then a.
	FSkelQuat operator*( const FSkelQuat& b ) const
	{
		return FSkelQuat(
			W*b.X + X*b.W + Y*b.Z - Z*b.Y,
			W*b.Y - X*b.Z + Y*b.W + Z*b.X,
			W*b.Z + X*b.Y - Y*b.X + Z*b.W,
			W*b.W - X*b.X - Y*b.Y - Z*b.Z );
	}
	// Rotate a vector by this quaternion (matches CQuat::ToAxis matrix).
	FVector Rotate( const FVector& v ) const
	{
		FVector u( X, Y, Z );
		FVector uv  = u ^ v;
		FVector uuv = u ^ uv;
		return v + ( uv * W + uuv ) * 2.0f;
	}
	void Normalize()
	{
		FLOAT m = appSqrt( X*X + Y*Y + Z*Z + W*W );
		if( m > 0.0001f ) { FLOAT inv = 1.0f/m; X*=inv; Y*=inv; Z*=inv; W*=inv; }
	}
};

// Spherical-ish interpolation (normalized lerp along the shorter arc; good enough for playback).
inline FSkelQuat SkelSlerp( FSkelQuat a, FSkelQuat b, FLOAT t )
{
	FLOAT dot = a.X*b.X + a.Y*b.Y + a.Z*b.Z + a.W*b.W;
	if( dot < 0.0f ) { b.X=-b.X; b.Y=-b.Y; b.Z=-b.Z; b.W=-b.W; }
	FSkelQuat r( a.X+(b.X-a.X)*t, a.Y+(b.Y-a.Y)*t, a.Z+(b.Z-a.Z)*t, a.W+(b.W-a.W)*t );
	r.Normalize();
	return r;
}

// A rigid coordinate frame (origin + 3 axis ROWS), matching UEViewer's CCoords/CAxis convention.
// Vector ops (implemented in UnAnim.cpp): TransformVector(v)=R*v (rotate by the frame's quat),
// UnTransformVector(v)=R^T*v (inverse), and the coord composition UnTransformCoords. Skeletal
// bone transforms are composed with these to exactly match umodel's bind + animation math.
struct FSkelCoords
{
	FVector O;			// origin
	FVector AX, AY, AZ;	// axis rows
};

// Extended LOD wedge with floating-point UVs.
struct FMeshExtWedge
{
	_WORD	iVertex;
	_WORD	Flags;
	FLOAT	U, V;
	friend FArchive& operator<<( FArchive& Ar, FMeshExtWedge& W )
	{
		return Ar << W.iVertex << W.Flags << W.U << W.V;
	}
};

// Reference-skeleton joint position. NOTE: the original Epic serializer writes XSize
// twice and never YSize; replicate that exactly so byte offsets line up (44 bytes).
struct FSkelJointPos
{
	FSkelQuat	Orientation;
	FVector		Position;
	FLOAT		Length;
	FLOAT		XSize, YSize, ZSize;
	friend FArchive& operator<<( FArchive& Ar, FSkelJointPos& V )
	{
		return Ar << V.Orientation << V.Position << V.Length << V.XSize << V.XSize << V.ZSize;
	}
};

// Reference-skeleton bone (package form). Serializes NumChildren before ParentIndex and
// omits Depth, matching the original Epic operator<<.
struct FSkelMeshBone
{
	FName			Name;
	DWORD			Flags;
	FSkelJointPos	BonePos;
	INT				ParentIndex;
	INT				NumChildren;
	friend FArchive& operator<<( FArchive& Ar, FSkelMeshBone& F )
	{
		return Ar << F.Name << F.Flags << F.BonePos << F.NumChildren << F.ParentIndex;
	}
};

// Bone-influence index block (per reference bone).
struct FSkelBoneInfIndex
{
	_WORD	WeightIndex;
	_WORD	Number;
	_WORD	DetailA;
	_WORD	DetailB;
	friend FArchive& operator<<( FArchive& Ar, FSkelBoneInfIndex& V )
	{
		return Ar << V.WeightIndex << V.Number << V.DetailA << V.DetailB;
	}
};

// Single bone influence (weight + vertex).
struct FSkelBoneInfluence
{
	_WORD	PointIndex;
	_WORD	BoneWeight;
	friend FArchive& operator<<( FArchive& Ar, FSkelBoneInfluence& V )
	{
		return Ar << V.PointIndex << V.BoneWeight;
	}
};

// Named bone for the animating skeleton.
struct FSkelNamedBone
{
	FName	Name;
	DWORD	Flags;
	INT		ParentIndex;
	friend FArchive& operator<<( FArchive& Ar, FSkelNamedBone& F )
	{
		return Ar << F.Name << F.Flags << F.ParentIndex;
	}
};

// 'Analog' animation key track for a single bone.
struct FSkelAnalogTrack
{
	DWORD				Flags;
	TArray<FSkelQuat>	KeyQuat;
	TArray<FVector>		KeyPos;
	TArray<FLOAT>		KeyTime;
	friend FArchive& operator<<( FArchive& Ar, FSkelAnalogTrack& A )
	{
		return Ar << A.Flags << A.KeyQuat << A.KeyPos << A.KeyTime;
	}
};

// One animation's motion chunk (per-bone compressed tracks + a root track).
struct FSkelMotionChunk
{
	FVector						RootSpeed3D;
	FLOAT						TrackTime;
	INT							StartBone;
	DWORD						Flags;
	TArray<INT>					BoneIndices;
	TArray<FSkelAnalogTrack>	AnimTracks;
	FSkelAnalogTrack			RootTrack;
	friend FArchive& operator<<( FArchive& Ar, FSkelMotionChunk& M )
	{
		return Ar << M.RootSpeed3D << M.TrackTime << M.StartBone << M.Flags << M.BoneIndices << M.AnimTracks << M.RootTrack;
	}
};

/*-----------------------------------------------------------------------------
	UAnimation.
-----------------------------------------------------------------------------*/

//
// v436-compat port: native Animation class.
//
// v436 engine.u references the native class 'Engine.Animation' (added to UT after v400 for
// skeletal-mesh animation). Registering this class resolves the import; Serialize consumes the
// reference-skeleton + motion data so packages that actually contain Animation exports load
// without a serial-size mismatch. Animation *playback* is a non-goal — the data is kept, unused.
//
class ENGINE_API UAnimation : public UObject
{
	DECLARE_CLASS(UAnimation,UObject,0)

	// Serialized skeletal animation data (not played back).
	TArray<FSkelNamedBone>		RefBones;	// Reference bone names.
	TArray<FSkelMotionChunk>	Moves;		// One motion chunk per animation.
	TArray<FMeshAnimSeq>		AnimSeqs;	// Classic per-animation name/frame info.

	// UObject interface.
	UAnimation(){}
	void Serialize( FArchive& Ar );
};

//
// v436-compat port: native SkeletalMesh class.
//
// OldSkool/ONP, Infiltration, NeoCairo, TacticalOps and other v436-era mods ship real
// USkeletalMesh exports (a ULodMesh subclass) that stock v400 lacks. Serialize decodes the
// skeletal blob (ext wedges, points, reference skeleton, bone weights, weapon attachment) so
// those packages load. Skeletal *rendering* is an accepted non-goal — the data is consumed and
// stored but the meshes are not skinned/drawn.
//
class ENGINE_API USkeletalMesh : public ULodMesh
{
	DECLARE_CLASS(USkeletalMesh,ULodMesh,0)

	// Skeletal data (serialized; not rendered).
	TArray<FMeshExtWedge>		ExtWedges;
	TArray<FVector>				Points;
	TArray<FSkelMeshBone>		RefSkeleton;
	TArray<FSkelBoneInfIndex>	BoneWeightIdx;
	TArray<FSkelBoneInfluence>	BoneWeights;
	TArray<FVector>				LocalPoints;
	INT							SkeletalDepth;
	UAnimation*					DefaultAnimation;
	INT							WeaponBoneIndex;
	FCoords						WeaponAdjust;

	// v436-compat: cached inverse bind-pose (reference skeleton) bone coords, computed lazily; used to
	// skin Points from the reference pose to the current animation pose. Not serialized.
	TArray<FSkelCoords>	BindRefInv;
	UBOOL				bBindComputed;

	// v436-compat weapon attachment: the animated WeaponBoneIndex bone frame (mirrored bone space,
	// filled by ComputeSkinnedPose) and its resolved view-space frame (filled by GetFrame). DrawLodMesh
	// publishes the view-space frame as the engine's SpecialCoords so the third-person weapon attaches
	// to the hand bone instead of floating beside the pawn. Not serialized.
	FSkelCoords	WeaponBoneM;		// animated weapon bone, mirrored space
	UBOOL		bWeaponBoneM;
	FVector		WeaponAttachOrigin, WeaponAttachX, WeaponAttachY, WeaponAttachZ;	// view space
	UBOOL		bWeaponAttach;

	// v436-compat render bound: the mesh-space box used for frustum culling, resolved once (authored
	// BoundingBox if valid, else an AABB over Points) and cached. Not serialized. See
	// GetRenderBoundingBox for why skeletal meshes need their own override.
	FBox		LocalBound;
	UBOOL		bBoundComputed;

	// UObject interface.
	USkeletalMesh(){}
	void Serialize( FArchive& Ar );

	// UPrimitive interface.
	FBox GetRenderBoundingBox( const AActor* Owner, UBOOL Exact );

	// v436-compat: render the reference (bind) pose. v400 has no skeletal skinning; without this
	// override a skeletal mesh falls through to ULodMesh::GetFrame, which reads the empty vertex-mesh
	// Verts array (FrameVerts>0, Verts.Num()==0) and crashes. Emits Points (the skin vertices, in
	// mesh space) so the inherited ULodMesh Wedges/Faces draw the mesh. Animated when possible.
	void GetFrame( FVector* Verts, INT Size, FCoords Coords, AActor* Owner, INT& LODRequest );

	// v436-compat skeletal animation: build bind-pose bone transforms (once); skin Points into the
	// current animation pose for the owner. ComputeSkinnedPose returns 0 if no animation applies
	// (falls back to the static bind pose).
	void ComputeBindPose();
	UBOOL ComputeSkinnedPose( AActor* Owner, TArray<FVector>& OutPoints );
};

/*----------------------------------------------------------------------------
	The End.
----------------------------------------------------------------------------*/
