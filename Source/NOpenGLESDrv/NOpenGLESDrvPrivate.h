/*------------------------------------------------------------------------------------
	Dependencies.
------------------------------------------------------------------------------------*/

#ifdef __EMSCRIPTEN__
// On the web Emscripten provides the GLES2 API as link-time symbols (-sFULL_ES2), so no loader is compiled;
// that also keeps this driver from clashing with XOpenGLDrv's glad in the static binary.
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>   // GL_BGRA_EXT and the other extension tokens
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
	// Image quality.
	//   MaxAnisotropy: EXT_texture_filter_anisotropic level, clamped at Init to the driver's maximum.
	//   NumAASamples: antialiasing samples (0 = off). On the web Init asks ufront_backbuffer_samples for a
	//     multisampled back buffer; natively the viewport made the context first, so it is only logged.
	FLOAT MaxAnisotropy;
	INT NumAASamples;
	INT SwapInterval;
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
	// Registers the config options and their compiled defaults (v400 calls StaticConstructor once per class).
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
	void UpdateSwapInterval();

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

	// Flush before a polygon that may not fit: the batch buffers are fixed (MAX_VERTS entries) and every
	// Attrib*/EndPoly write is unchecked. A close-up mesh, subdivided in proportion to the screen width, can
	// exceed the index buffer in one batch. The bound is generous: UE1 polys carry at most 16 vertices and
	// AttribSizes sum to 19 floats.
	enum { MAX_POLY_VERTS = 64, MAX_POLY_FLOATS = MAX_POLY_VERTS * 20, MAX_POLY_INDICES = ( MAX_POLY_VERTS - 2 ) * 3 };
	inline void BeginPoly()
	{
		if( IdxDataPtr + MAX_POLY_INDICES > IdxDataEnd || VtxDataPtr + MAX_POLY_FLOATS > VtxDataEnd )
			FlushTriangles();
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
