/*------------------------------------------------------------------------------------
	Dependencies.
------------------------------------------------------------------------------------*/

#ifdef __EMSCRIPTEN__
// ufront 2.19: on the web there is nothing to load. Emscripten provides the whole GLES2 API as real
// link-time symbols under -sFULL_ES2, and this driver uses 45 entry points, every one of them core GLES2.
// Taking the headers directly is not just tidier — it is what lets this driver co-link with XOpenGLDrv,
// which vendors its OWN glad with byte-identical symbol names (glad_glActiveTexture, and even
// gladLoadGLES2Loader). Two glads in one static binary under --whole-archive is a duplicate-symbol link
// failure, which is why the web build could previously ship only one of the two.
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>   // GL_BGRA_EXT and friends (the ext tokens glad used to supply)
#else
#include "glad.h"
#endif
#include "glm/matrix.hpp"
#include "RenderPrivate.h"

/*------------------------------------------------------------------------------------
	OpenGL rendering private definitions.
------------------------------------------------------------------------------------*/

enum EUniformIndex
{
	UF_Mtx,
	UF_Brightness,
	UF_Texture0,
	UF_Texture1,
	UF_Texture2,
	UF_Texture3,
	UF_Count
};

enum EShaderFlags : DWORD
{
	SF_Texture0  = 1 << 0,
	SF_Texture1  = 1 << 1,
	SF_Texture2  = 1 << 2,
	SF_Texture3  = 1 << 3,
	SF_VtxColor  = 1 << 4,
	SF_AlphaTest = 1 << 5,
	SF_Lightmap  = 1 << 6,
	SF_Fogmap    = 1 << 7,
	SF_Detail    = 1 << 8,
	SF_VtxFog    = 1 << 9,
	SF_Max       = SF_VtxFog,
	SF_Count     = 10,
};

enum EShaderAttribs
{
	AT_Position,
	AT_TexCoord0,
	AT_TexCoord1,
	AT_TexCoord2,
	AT_TexCoord3,
	AT_VtxColor,
	AT_VtxFog,
	AT_Count
};

//
// GLES2 renderer based on NOpenGLDrv.
//
class DLL_EXPORT UNOpenGLESRenderDevice : public URenderDevice
{
	DECLARE_CLASS(UNOpenGLESRenderDevice, URenderDevice, CLASS_Config)

	static constexpr INT MaxTexUnits = 4;

	// Options.
	UBOOL NoFiltering;
	// ufront 2.19 — image-quality knobs.
	//   MaxAnisotropy is a REAL numeric control on the web: EXT_texture_filter_anisotropic exposes a
	//     driver maximum (16 on this box) and takes a float per texture. Clamped to that max at Init.
	//   NumAASamples is honest-but-nominal: WebGL picks MSAA with a BOOLEAN `antialias` context attribute
	//     at creation time, so the count cannot be chosen and the context is already made by the viewport
	//     before this driver runs. It is kept as a number for parity with XOpenGL/UTGLR (and for native,
	//     where it is meaningful); >0 simply means "AA requested".
	FLOAT MaxAnisotropy;
	INT NumAASamples;
	UBOOL UseBGRA;
	UBOOL Overbright;
	UBOOL DetailTextures;
	UBOOL UseVAO;

	// All currently cached textures.
	struct FCachedTexture
	{
		GLuint Id;
		INT BaseMip;
		INT MaxLevel;
	};
	TMap<QWORD, FCachedTexture> BindMap;
	TArray<GLuint> TexAlloc;

	// Currently bound textures.
	struct FTexInfo
	{
		QWORD CurrentCacheID;
		FLOAT UMult;
		FLOAT VMult;
		FLOAT UPan;
		FLOAT VPan;
	} TexInfo[MaxTexUnits];

	// All currently compiled shaders.
	struct FCachedShader
	{
		DWORD Flags;
		GLuint Prog;
		GLint Uniforms[UF_Count];
		UBOOL Attribs[AT_Count];
		DWORD NumFloats;
	};
	TMap<DWORD, FCachedShader> ShaderMap;

	BYTE UniformsChanged[UF_Count];

	// Currently bound shader.
	FCachedShader* ShaderInfo;

	// Texture upload buffer.
	BYTE* Compose;
	DWORD ComposeSize;

	// Vertex buffer.
	GLuint GLBuf;
	GLfloat* VtxData;
	GLfloat* VtxDataEnd;
	GLfloat* VtxDataPtr;
	DWORD VtxDataSize;
	DWORD VtxPolyVerts;
	UBOOL InPoly;

	// Index buffer.
	GLushort* IdxData;
	GLushort* IdxDataEnd;
	GLushort* IdxDataPtr;
	DWORD IdxDataSize;
	GLushort IdxCount;
	GLushort IdxBase;

	// Current state.
	DWORD CurrentShaderFlags;
	DWORD CurrentPolyFlags;
	FLOAT CurrentBrightness;
	FLOAT RProjZ, Aspect;
	FLOAT RFX2, RFY2;
	glm::mat4 MtxProj;
	glm::mat4 MtxMVP;
	FPlane ColorMod;

	struct FCachedSceneNode
	{
		FLOAT FovAngle;
		FLOAT FX, FY;
		INT X, Y;
		INT XB, YB;
		INT SizeX, SizeY;
	} CurrentSceneNode;

	// Constructors.
	UNOpenGLESRenderDevice();
	// ufront 2.19: v400 registers config properties from StaticConstructor. The v200-era
	// InternalClassInitializer this driver shipped with has ZERO call sites in this engine (see
	// NSDLDrv/Src/NSDLViewport.cpp:187, which documents the same dead hook costing UT99 its keyboard), so
	// none of the five Options below were ever registered and [NOpenGLESDrv.NOpenGLESRenderDevice] in the
	// ini was silently inert — the device always ran on the C++ constructor defaults.
	void StaticConstructor();

	// URenderDevice interface.
	virtual UBOOL Init( UViewport* InViewport, INT NewX, INT NewY, INT NewColorBytes, UBOOL Fullscreen ) override;
	virtual UBOOL SetRes( INT NewX, INT NewY, INT NewColorBytes, UBOOL Fullscreen ) override;
	virtual void Exit() override;
	virtual void Flush( UBOOL AllowPrecache ) override;
	virtual void PostEditChange() override;
	virtual UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar ) override;
	virtual void Lock( FPlane FlashScale, FPlane FlashFog, FPlane ScreenClear, DWORD RenderLockFlags, BYTE* InHitData, INT* InHitSize ) override;
	virtual void Unlock( UBOOL Blit ) override;
	virtual void DrawComplexSurface( FSceneNode* Frame, FSurfaceInfo& Surface, FSurfaceFacet& Facet ) override;
	virtual void DrawGouraudPolygon( FSceneNode* Frame, FTextureInfo& Texture, FTransTexture** Pts, INT NumPts, DWORD PolyFlags, FSpanBuffer* SpanBuffer ) override;
	virtual void DrawTile( FSceneNode* Frame, FTextureInfo& Texture, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL, FLOAT U, FLOAT V, FLOAT UL, FLOAT VL, FSpanBuffer* Span, FLOAT Z, FPlane Light, FPlane Fog, DWORD PolyFlags ) override;
	virtual void EndFlash() override;
	virtual void GetStats( TCHAR* Result ) override;
	virtual void Draw2DLine( FSceneNode* Frame, FPlane Color, DWORD LineFlags, FVector P1, FVector P2 ) override;
	virtual void Draw2DPoint( FSceneNode* Frame, FPlane Color, DWORD LineFlags, FLOAT X1, FLOAT Y1, FLOAT X2, FLOAT Y2, FLOAT Z ) override;
	virtual void PushHit( const BYTE* Data, INT Count ) override;
	virtual void PopHit( INT Count, UBOOL bForce ) override;
	virtual void ReadPixels( FColor* Pixels ) override;
	virtual void ClearZ( FSceneNode* Frame ) override;

	// UNOpenGLESRenderDevice interface.
	void UpdateUniforms();
	GLuint CompileShader( GLenum Type, const char* Text );
	FCachedShader* CreateShader( DWORD ShaderFlags );
	void SetShader( DWORD ShaderFlags );
	void SetSceneNode( FSceneNode* Frame );
	void SetBlend( DWORD PolyFlags, UBOOL InverseOrder = false );
	void SetTexture( INT TMU, FTextureInfo& Info, DWORD PolyFlags, FLOAT PanBias );
	void ResetTexture( INT TMU );
	void UploadTexture( FTextureInfo& Info, UBOOL Masked, UBOOL NewTexture );
	void UpdateTextureFilter( const FTextureInfo& Info, DWORD PolyFlags );

private:
	// Fixed function mode emulation.
	inline void FlushTriangles()
	{
		if( IdxCount )
		{
			check( IdxDataPtr <= IdxDataEnd );
			check( VtxDataPtr <= VtxDataEnd );
			if ( UseVAO )
				glBufferSubData( GL_ARRAY_BUFFER, 0, ( (BYTE*)VtxDataPtr - (BYTE*)VtxData ), VtxData );
			glDrawElements( GL_TRIANGLES, IdxDataPtr - IdxData, GL_UNSIGNED_SHORT, IdxData );
			IdxCount = 0;
		}
		VtxDataPtr = VtxData;
		IdxDataPtr = IdxData;
	}

	inline void BeginPoly()
	{
		VtxPolyVerts = 0;
		IdxBase = IdxCount;
	}

	inline void AttribFloat2( FLOAT X, FLOAT Y )
	{
		*VtxDataPtr++ = X;
		*VtxDataPtr++ = Y;
	}

	inline void AttribFloat2( const FLOAT *V )
	{
		*VtxDataPtr++ = V[0];
		*VtxDataPtr++ = V[1];
	}

	inline void AttribFloat3( FLOAT X, FLOAT Y, FLOAT Z )
	{
		*VtxDataPtr++ = X;
		*VtxDataPtr++ = Y;
		*VtxDataPtr++ = Z;
	}

	inline void AttribFloat3( const FLOAT* V )
	{
		*VtxDataPtr++ = V[0];
		*VtxDataPtr++ = V[1];
		*VtxDataPtr++ = V[2];
	}

	inline void AttribFloat4( FLOAT X, FLOAT Y, FLOAT Z, FLOAT W )
	{
		*VtxDataPtr++ = X;
		*VtxDataPtr++ = Y;
		*VtxDataPtr++ = Z;
		*VtxDataPtr++ = W;
	}

	inline void AttribFloat4( const FLOAT* V )
	{
		*VtxDataPtr++ = V[0];
		*VtxDataPtr++ = V[1];
		*VtxDataPtr++ = V[2];
		*VtxDataPtr++ = V[3];
	}

	inline void PolyVertex()
	{
		++VtxPolyVerts;
	}

	inline void EndPoly()
	{
		*IdxDataPtr++ = IdxCount++;
		*IdxDataPtr++ = IdxCount++;
		*IdxDataPtr++ = IdxCount++;
		for( DWORD i = 3; i < VtxPolyVerts; ++i )
		{
			*IdxDataPtr++ = IdxBase;
			*IdxDataPtr++ = IdxCount - 1;
			*IdxDataPtr++ = IdxCount++;
		}
	}
};
