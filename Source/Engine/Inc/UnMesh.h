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
	Skeletal mesh / animation structs.

	The data of the USkeletalMesh/UAnimation exports v436-era mods ship, in the v432 public header
	layout and read byte for byte. v436's FQuat lives in Core; here it is FSkelQuat, with the
	quaternion math USkeletalMesh::GetFrame uses, as the retail v436 DLL has it.
-----------------------------------------------------------------------------*/

// A quaternion: 4 floats, as on disk.
struct FSkelQuat
{
	FLOAT X, Y, Z, W;
	FSkelQuat() {}
	FSkelQuat( FLOAT InX, FLOAT InY, FLOAT InZ, FLOAT InW ) : X(InX), Y(InY), Z(InZ), W(InW) {}
	friend FArchive& operator<<( FArchive& Ar, FSkelQuat& Q )
	{
		return Ar << Q.X << Q.Y << Q.Z << Q.W;
	}
	// v436 FQuat::Normalize: below a square length of DELTA (0.00001) it becomes (0,0,0.1,0).
	UBOOL Normalize()
	{
		FLOAT SquareSum = X*X + Y*Y + Z*Z + W*W;
		if( SquareSum >= 0.00001f )
		{
			FLOAT Scale = 1.0f/appSqrt(SquareSum);
			X *= Scale; Y *= Scale; Z *= Scale; W *= Scale;
			return 1;
		}
		X = 0.0f; Y = 0.0f; Z = 0.1f; W = 0.0f;
		return 0;
	}
	// v436 FQuat::FQuatToFMatrix, as the rows (axes) of an FCoords with the given origin.
	FCoords ToCoords( const FVector& Origin ) const
	{
		FLOAT x2 = X+X, y2 = Y+Y, z2 = Z+Z;
		FLOAT xx = X*x2, xy = X*y2, xz = X*z2;
		FLOAT yy = Y*y2, yz = Y*z2, zz = Z*z2;
		FLOAT wx = W*x2, wy = W*y2, wz = W*z2;
		return FCoords
		(
			Origin,
			FVector( 1.0f-(yy+zz), xy-wz,        xz+wy        ),
			FVector( xy+wz,        1.0f-(xx+zz), yz-wx        ),
			FVector( xz-wy,        yz+wx,        1.0f-(xx+yy) )
		);
	}
};

// v436 SlerpQuat: spherical interpolation, output not normalized; Q1 itself once cos reaches 1.
inline FSkelQuat SkelSlerpQuat( const FSkelQuat& Q1, const FSkelQuat& Q2, FLOAT Alpha )
{
	FLOAT CosOm = Q1.X*Q2.X + Q1.Y*Q2.Y + Q1.Z*Q2.Z + Q1.W*Q2.W;
	if( CosOm < 1.0f )
	{
		FLOAT Omega  = acos( CosOm );		// v436 appAcos
		FLOAT SinInv = 1.f/appSin( Omega );
		FLOAT Scale0 = appSin( (1.0f - Alpha) * Omega ) * SinInv;
		FLOAT Scale1 = appSin( Alpha * Omega ) * SinInv;
		return FSkelQuat
		(
			Scale0*Q1.X + Scale1*Q2.X,
			Scale0*Q1.Y + Scale1*Q2.Y,
			Scale0*Q1.Z + Scale1*Q2.Z,
			Scale0*Q1.W + Scale1*Q2.W
		);
	}
	return Q1;
}

// v436: flip Q1 onto Q2's hemisphere when that brings it closer (shortest-arc interpolation).
inline void SkelAlignQuatWith( FSkelQuat& Q1, const FSkelQuat& Q2 )
{
	FLOAT Minus = Square(Q1.X-Q2.X) + Square(Q1.Y-Q2.Y) + Square(Q1.Z-Q2.Z) + Square(Q1.W-Q2.W);
	FLOAT Plus  = Square(Q1.X+Q2.X) + Square(Q1.Y+Q2.Y) + Square(Q1.Z+Q2.Z) + Square(Q1.W+Q2.W);
	if( Minus > Plus )
	{
		Q1.X = -Q1.X; Q1.Y = -Q1.Y; Q1.Z = -Q1.Z; Q1.W = -Q1.W;
	}
}

// v436 FCoords::ApplyPivot: A expressed in B's frame (child bone A under parent B).
inline FCoords SkelApplyPivot( const FCoords& A, const FCoords& B )
{
	FCoords Temp;
	Temp.Origin = B.Origin + A.Origin.TransformVectorBy( B );
	Temp.XAxis  = B.XAxis.TransformVectorBy( A );
	Temp.YAxis  = B.YAxis.TransformVectorBy( A );
	Temp.ZAxis  = B.ZAxis.TransformVectorBy( A );
	return Temp;
}

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

// Reference-skeleton joint position (44 bytes). Epic's serializer writes XSize twice and
// never YSize; this one does the same, so the byte offsets line up.
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

// Reference-skeleton bone (package form): NumChildren is serialized before ParentIndex, as
// Epic's operator<< does.
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
// Native Animation class, which v436 engine.u references. Serialize reads the reference bones and
// motion data byte for byte; USkeletalMesh::GetFrame plays it and AActor::GetAnim resolves its sequences.
//
class ENGINE_API UAnimation : public UObject
{
	DECLARE_CLASS(UAnimation,UObject,0)

	// Serialized skeletal animation data, played by USkeletalMesh::GetFrame.
	TArray<FSkelNamedBone>		RefBones;	// Reference bone names.
	TArray<FSkelMotionChunk>	Moves;		// One motion chunk per animation.
	TArray<FMeshAnimSeq>		AnimSeqs;	// Classic per-animation name/frame info.

	// UObject interface.
	UAnimation(){}
	void Serialize( FArchive& Ar );

	// UAnimation's virtuals, as the retail v436 DLL exports them. A skeletal mesh's sequences live
	// here, not in the mesh: AActor::GetAnim and USkeletalMesh::GetFrame look them up through these.
	virtual const FMeshAnimSeq* GetAnimSeq( FName SeqName ) const
	{
		guardSlow(UAnimation::GetAnimSeq);
		for( INT i=0; i<AnimSeqs.Num(); i++ )
			if( SeqName == AnimSeqs(i).Name )
				return &AnimSeqs(i);
		return NULL;
		unguardSlow;
	}
	virtual FMeshAnimSeq* GetAnimSeq( FName SeqName )
	{
		guardSlow(UAnimation::GetAnimSeq);
		for( INT i=0; i<AnimSeqs.Num(); i++ )
			if( SeqName == AnimSeqs(i).Name )
				return &AnimSeqs(i);
		return NULL;
		unguardSlow;
	}
	// The motion chunk of a sequence: Moves is indexed in step with AnimSeqs.
	virtual const FSkelMotionChunk* GetMovement( FName SeqName ) const
	{
		guardSlow(UAnimation::GetMovement);
		for( INT i=0; i<AnimSeqs.Num(); i++ )
			if( SeqName == AnimSeqs(i).Name )
				return &Moves(i);
		return NULL;
		unguardSlow;
	}
	virtual FSkelMotionChunk* GetMovement( FName SeqName )
	{
		guardSlow(UAnimation::GetMovement);
		for( INT i=0; i<AnimSeqs.Num(); i++ )
			if( SeqName == AnimSeqs(i).Name )
				return &Moves(i);
		return NULL;
		unguardSlow;
	}
	// Estimated memory footprint of the digested key data, in bytes.
	virtual INT MemFootprint()
	{
		guard(UAnimation::MemFootprint);
		INT TotalMem = 0;
		for( INT i=0; i<Moves.Num(); i++ )
		{
			for( INT j=0; j<Moves(i).AnimTracks.Num(); j++ )
			{
				TotalMem += sizeof(DWORD); // Flags
				TotalMem += sizeof(FSkelQuat) * Moves(i).AnimTracks(j).KeyQuat.Num();
				TotalMem += sizeof(FVector)   * Moves(i).AnimTracks(j).KeyPos.Num();
				TotalMem += sizeof(FLOAT)     * Moves(i).AnimTracks(j).KeyTime.Num();
			}
			TotalMem += sizeof(FSkelQuat) * Moves(i).RootTrack.KeyQuat.Num();
			TotalMem += sizeof(FVector)   * Moves(i).RootTrack.KeyPos.Num();
			TotalMem += sizeof(FLOAT)     * Moves(i).RootTrack.KeyTime.Num();
		}
		return TotalMem;
		unguard;
	}
};

//
// Native SkeletalMesh class (a ULodMesh subclass), as in the retail v436 DLL; OldSkool/ONP,
// Infiltration and other v436-era mods ship its exports. Serialize reads the skeletal blob byte for
// byte; GetFrame skins it into the owner's animation pose, and DrawLodMesh draws its ULodMesh faces.
//
class ENGINE_API USkeletalMesh : public ULodMesh
{
	DECLARE_CLASS(USkeletalMesh,ULodMesh,0)

	// Skeletal data (serialized).
	TArray<FMeshExtWedge>		ExtWedges;
	TArray<FVector>				Points;
	TArray<FSkelMeshBone>		RefSkeleton;
	TArray<FSkelBoneInfIndex>	BoneWeightIdx;
	TArray<FSkelBoneInfluence>	BoneWeights;
	TArray<FVector>				LocalPoints;	// Each weighted point in its bone's local space (per BoneWeights entry).
	INT							SkeletalDepth;
	UAnimation*					DefaultAnimation;
	INT							WeaponBoneIndex;	// -1 means it hasn't been assigned.
	FCoords						WeaponAdjust;		// Weapon adjustment relative to the weapon bone.

	// Debugging only - not serialized (v436: filled by GetFrame when DisplayBones/DisplayInfluences).
	TArray<FVector>				DebugPivots;
	TArray<INT>					DebugParents;
	TArray<BYTE>				DebugVerts;
	UBOOL						DisplayBones;
	UBOOL						DisplayInfluences;

	// Runtime weapon coords - not serialized. GetFrame sets them from the weapon bone, in the same
	// form as the classic special-face coords; DrawLodMesh uses them as the weapon's SpecialCoords.
	FCoords						ClassicWeaponCoords;

	// UObject interface.
	USkeletalMesh();
	void Serialize( FArchive& Ar );

	// UPrimitive interface.
	FBox GetRenderBoundingBox( const AActor* Owner, UBOOL Exact );

	// UMesh interface (USkeletalMesh's overrides in the retail v436 DLL).
	void SetScale( FVector NewScale );
	virtual void FlipFaces();

	// Skin the mesh into the owner's current (or tweening) animation pose.
	void GetFrame( FVector* Verts, INT Size, FCoords Coords, AActor* Owner, INT& LODRequest );
};

/*----------------------------------------------------------------------------
	The End.
----------------------------------------------------------------------------*/
