#include "SDL2/SDL.h"
#ifndef __EMSCRIPTEN__
#include "glad.h"
#endif
#include "glm/matrix.hpp"
#include "glm/ext/matrix_clip_space.hpp"

#include "NOpenGLESDrvPrivate.h"

/*-----------------------------------------------------------------------------
	GLSL shaders.
-----------------------------------------------------------------------------*/

static const char *FragShaderGLSL {
#include "FragmentShader.glsl.inc"
};

static const char *VertShaderGLSL {
#include "VertexShader.glsl.inc"
};

/*-----------------------------------------------------------------------------
	Global implementation.
-----------------------------------------------------------------------------*/

IMPLEMENT_PACKAGE(NOpenGLESDrv);
IMPLEMENT_CLASS(UNOpenGLESRenderDevice);

/*-----------------------------------------------------------------------------
	UNOpenGLESRenderDevice implementation.
-----------------------------------------------------------------------------*/

// MV matrix that puts the coordinate system in order
static constexpr glm::mat4 MtxModelView {
	+1.f, +0.f, +0.f, +0.f,
	+0.f, -1.f, +0.f, +0.f,
	+0.f, +0.f, -1.f, +0.f,
	+0.f, +0.f, +0.f, +1.f,
};

// in floats
static constexpr DWORD AttribSizes[AT_Count] = {
	3, 2, 2, 2, 2, 4, 4
};

// from XOpenGLDrv:
// PF_Masked requires index 0 to be transparent, but is set on the polygon instead of the texture,
// so we potentially need two copies of any palettized texture in the cache
// unlike in newer unreal versions the low cache bits are actually used, so we have use one of the
// actually unused higher bits for this purpose, thereby breaking 64-bit compatibility for now
#define MASKED_TEXTURE_TAG (1ULL << 60ULL)

// FColor is adjusted for endianness
#define ALPHA_MASK 0xff000000

// lightmaps are 0-127
#define LIGHTMAP_SCALE 2

// and it also would be nice to overbright them
#define LIGHTMAP_OVERBRIGHT 1.4f

// max vertices in a single draw call
#define MAX_VERTS 32768

void UNOpenGLESRenderDevice::StaticConstructor()
{
	guardSlow(UNOpenGLESRenderDevice::StaticConstructor);
	new(GetClass(), "NoFiltering",    RF_Public)UBoolProperty( CPP_PROPERTY(NoFiltering),    "Options", CPF_Config );
	new(GetClass(), "Overbright",     RF_Public)UBoolProperty( CPP_PROPERTY(Overbright),     "Options", CPF_Config );
	new(GetClass(), "DetailTextures", RF_Public)UBoolProperty( CPP_PROPERTY(DetailTextures), "Options", CPF_Config );
	new(GetClass(), "UseVAO",         RF_Public)UBoolProperty( CPP_PROPERTY(UseVAO),         "Options", CPF_Config );
	new(GetClass(), "UseBGRA",        RF_Public)UBoolProperty( CPP_PROPERTY(UseBGRA),        "Options", CPF_Config );
	new(GetClass(), "MaxAnisotropy",  RF_Public)UFloatProperty( CPP_PROPERTY(MaxAnisotropy),  "Options", CPF_Config );
	new(GetClass(), "NumAASamples",   RF_Public)UIntProperty( CPP_PROPERTY(NumAASamples),     "Options", CPF_Config );
	new(GetClass(), "SwapInterval",   RF_Public)UIntProperty( CPP_PROPERTY(SwapInterval),     "Options", CPF_Config );
	// Compiled defaults, on the class default object, where the ini overrides them (the constructor runs on an
	// object the ini has already filled). They match the web ini.
	DetailTextures = true;
	Overbright = true;
	NoFiltering = false;
	UseVAO = false;
	UseBGRA = true;
	MaxAnisotropy = 16.f;   // clamped at Init to the driver's maximum
	NumAASamples = 4;
	SwapInterval = 1;	// 0 off, 1 vsync, -1 adaptive
	unguardSlow;
}

UNOpenGLESRenderDevice::UNOpenGLESRenderDevice()
{
	// Nothing configurable here (see StaticConstructor).
	CurrentBrightness = -1.f;
}

/*-----------------------------------------------------------------------------
	BGRA capability.
-----------------------------------------------------------------------------*/

// Extension tests ask the live context's GL_EXTENSIONS, on every platform (the web compiles no glad, and the
// vendored glad_es has no EXT_texture_filter_anisotropic flag).
#include <string.h>
#ifdef __EMSCRIPTEN__
extern "C" int ufront_backbuffer_samples( int Samples ); // msaa-backbuffer.js
#endif
static UBOOL NOpenGLESHasExtension( const char* Name )
{
	const char* Exts = (const char*)glGetString( GL_EXTENSIONS );
	if( !Exts || !Name || !*Name )
		return 0;
	// Whole-token match: "GL_EXT_texture_format_BGRA8888" must not be satisfied by a longer neighbour.
	const size_t Len = strlen( Name );
	for( const char* P = Exts; ( P = strstr( P, Name ) ) != NULL; P += Len )
	{
		const char Before = ( P == Exts ) ? ' ' : P[-1];
		const char After  = P[Len];
		if( Before == ' ' && ( After == ' ' || After == '\0' ) )
			return 1;
	}
	return 0;
}
// WebGL spells extensions without the "GL_" prefix (EXT_texture_format_BGRA8888), so try both.
static UBOOL NOpenGLESHasExtensionEither( const char* GLName )
{
	if( NOpenGLESHasExtension( GLName ) )
		return 1;
	return ( strncmp( GLName, "GL_", 3 ) == 0 ) ? NOpenGLESHasExtension( GLName + 3 ) : 0;
}
#define UFRONT_GLES_BGRA_SUPPORTED()                                  \
	( NOpenGLESHasExtensionEither( "GL_APPLE_texture_format_BGRA8888" ) || \
	  NOpenGLESHasExtensionEither( "GL_EXT_texture_format_BGRA8888" )   || \
	  NOpenGLESHasExtensionEither( "GL_MESA_bgra" ) )

// EXT_texture_filter_anisotropic tokens, for headers that lack them.
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

#define UFRONT_GLES_HAS_ANISO() NOpenGLESHasExtensionEither( "GL_EXT_texture_filter_anisotropic" )

UBOOL UNOpenGLESRenderDevice::Init( UViewport* InViewport, INT NewX, INT NewY, INT NewColorBytes, UBOOL Fullscreen )
{
	guard(UNOpenGLESRenderDevice::Init)
	debugf( NAME_Log, TEXT("NOpenGLESDrv::Init: NewX=%d, NewY=%d, ColorBytes=%d, Fullscreen=%d"), NewX, NewY, NewColorBytes, Fullscreen );

	// The viewport takes the mode: its window size and, through the blit flags, whether the absolute mouse path
	// is available to UWindow (a windowed viewport), which a tap on a touch screen needs.
	if( !InViewport->ResizeViewport( Fullscreen ? (BLIT_Fullscreen | BLIT_OpenGL) : (BLIT_HardwarePaint | BLIT_OpenGL), NewX, NewY, NewColorBytes ) )
	{
		debugf( NAME_Warning, TEXT("NOpenGLESDrv: could not resize the viewport to %ix%i"), NewX, NewY );
		return false;
	}

#ifndef __EMSCRIPTEN__
	// Resolve the GLES2 entry points through SDL.
	if( !gladLoadGLES2Loader( &SDL_GL_GetProcAddress ) )
	{
		debugf( NAME_Warning, "Could not load GLES2: %s", SDL_GetError() );
		return false;
	}
#endif

	debugf( NAME_Log, "Got OpenGL %s", glGetString( GL_VERSION ) );
	{
		// The depth the default framebuffer really has (on the web, Emscripten's emulated framebuffer, which
		// offscreen-depth24.sh makes 24-bit).
		GLint DepthBits = 0, StencilBits = 0;
		glGetIntegerv( GL_DEPTH_BITS, &DepthBits );
		glGetIntegerv( GL_STENCIL_BITS, &StencilBits );
		debugf( NAME_Log, "NOpenGLES: default framebuffer depth %d bits, stencil %d bits", DepthBits, StencilBits );
	}

	SupportsFogMaps = true;
	SupportsDistanceFog = true;

	UpdateSwapInterval();

	ComposeSize = 256 * 256 * 4;
	Compose = (BYTE*)appMalloc( ComposeSize, "GLComposeBuf" );
	verify( Compose );

	VtxDataSize = 18 * MAX_VERTS; // should be enough for all attributes
	VtxData = (FLOAT*)appMalloc( VtxDataSize * sizeof(FLOAT), "GLVtxDataBuf" );
	verify( VtxData );
	VtxDataEnd = VtxData + VtxDataSize;
	VtxDataPtr = VtxData;

	IdxDataSize = MAX_VERTS;
	IdxData = (GLushort*)appMalloc( IdxDataSize * sizeof(GLushort), "GLIdxDataBuf" );
	verify( IdxData );
	IdxDataEnd = IdxData + IdxDataSize;
	IdxDataPtr = IdxData;
	IdxCount = 0;

	if( UseVAO )
	{
		glGenBuffers( 1, &GLBuf );
		glBindBuffer( GL_ARRAY_BUFFER, GLBuf );
		glBufferData( GL_ARRAY_BUFFER, VtxDataSize * sizeof(FLOAT), (void*)VtxData, GL_DYNAMIC_DRAW ); // a size in bytes
	}

	// Anisotropic filtering: clamp the requested level to the context's maximum, 1 (isotropic) without the
	// extension.
	if( MaxAnisotropy > 1.f )
	{
		if( UFRONT_GLES_HAS_ANISO() )
		{
			GLfloat DriverMax = 1.f;
			glGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &DriverMax );
			if( DriverMax < 1.f )
				DriverMax = 1.f;                       // some drivers answer 0 when the query is unsupported
			if( MaxAnisotropy > DriverMax )
				MaxAnisotropy = DriverMax;
			debugf( NAME_Log, "GLES2: anisotropic filtering %.0fx (driver max %.0fx)", MaxAnisotropy, DriverMax );
		}
		else
		{
			debugf( NAME_Log, "GLES2: anisotropic filtering requested but unsupported; disabling" );
			MaxAnisotropy = 1.f;
		}
	}

#ifdef __EMSCRIPTEN__
	// Antialiasing on the web is a multisampled emulated back buffer (msaa-backbuffer.js), which needs WebGL 2;
	// asked for or given back on every Init, since a context outlives a device.
	{
		const INT Got = ufront_backbuffer_samples( NumAASamples > 1 ? NumAASamples : 0 );
		if( Got > 1 )
			debugf( NAME_Log, "GLES2: antialiasing %dx (multisampled back buffer)", Got );
		else if( NumAASamples > 1 )
			debugf( NAME_Log, "GLES2: antialiasing (%d samples) unavailable on this context (WebGL 1)", NumAASamples );
		else
			debugf( NAME_Log, "GLES2: antialiasing off" );
	}
#else
	// Natively the context is made before this driver runs, so NumAASamples is only reported.
	if( NumAASamples > 0 )
		debugf( NAME_Log, "GLES2: AA requested (%d samples nominal)", NumAASamples );
#endif

	if( UseBGRA )
	{
		// check if BGRA is actually supported
		if( !UFRONT_GLES_BGRA_SUPPORTED() )
		{
			debugf( "GLES2: BGRA8888 enabled, but not supported; disabling" );
			UseBGRA = false;
		}
		else
		{
			debugf( "GLES2: BGRA8888 supported" );
		}
	}

	// Set permanent state.
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_TRUE );
	glBlendFunc( GL_ONE, GL_ZERO );
	glEnable( GL_BLEND );
	glEnableVertexAttribArray( 0 );

	// Precache some common shaders
	static const DWORD PrecacheShaders[] = {
		SF_VtxColor,
		SF_Texture0,
		SF_Texture0 | SF_VtxColor,
		SF_Texture0 | SF_AlphaTest,
		SF_Texture0 | SF_VtxColor | SF_AlphaTest,
		SF_Texture0 | SF_VtxColor | SF_VtxFog,
		SF_Texture0 | SF_VtxColor | SF_VtxFog | SF_AlphaTest,
		SF_Texture0 | SF_Texture1 | SF_Lightmap,
		SF_Texture0 | SF_Texture1 | SF_Lightmap | SF_AlphaTest,
		SF_Texture0 | SF_Texture1 | SF_Texture2 | SF_Lightmap | SF_Fogmap,
		SF_Texture0 | SF_Texture1 | SF_Texture2 | SF_Lightmap | SF_Fogmap | SF_AlphaTest,
	};
	for( DWORD i = 0; i < ARRAY_COUNT( PrecacheShaders ); ++i )
		CreateShader( PrecacheShaders[i] );

	CurrentPolyFlags = PF_Occlude;
	CurrentShaderFlags = 0;
	CurrentBrightness = -1.f;
	Viewport = InViewport;

	// Set initial viewport size
	glViewport( 0, 0, NewX, NewY );
	debugf( NAME_Log, TEXT("NOpenGLESDrv::Init complete, viewport set to %dx%d"), NewX, NewY );

	return true;
	unguard;
}

UBOOL UNOpenGLESRenderDevice::SetRes( INT NewX, INT NewY, INT NewColorBytes, UBOOL Fullscreen )
{
	guard(UNOpenGLESRenderDevice::SetRes);
	debugf( NAME_Log, TEXT("NOpenGLESDrv::SetRes: NewX=%d, NewY=%d"), NewX, NewY );
	if( !Viewport->ResizeViewport( Fullscreen ? (BLIT_Fullscreen | BLIT_OpenGL) : (BLIT_HardwarePaint | BLIT_OpenGL), NewX, NewY, NewColorBytes ) )
	{
		debugf( NAME_Warning, TEXT("NOpenGLESDrv: could not resize the viewport to %ix%i"), NewX, NewY );
		return false;
	}
	// Update GL viewport when resolution changes
	glViewport( 0, 0, NewX, NewY );
	return true;
	unguard;
}

void UNOpenGLESRenderDevice::Exit()
{
	guard(UNOpenGLESRenderDevice::Exit);

	debugf( NAME_Log, "Shutting down OpenGL ES2 renderer" );

	Flush( 0 );

	if( Compose )
	{
		appFree( Compose );
		Compose = NULL;
	}
	ComposeSize = 0;

	unguard;
}

void UNOpenGLESRenderDevice::PostEditChange()
{
	guard(UNOpenGLESRenderDevice::PostEditChange)

	Super::PostEditChange();

	UpdateSwapInterval();

	unguard;
}

void UNOpenGLESRenderDevice::Flush( UBOOL AllowPrecache )
{
	guard(UNOpenGLESRenderDevice::Flush);

	if( TexAlloc.Num() )
	{
		debugf( NAME_Log, "Flushing %d textures", TexAlloc.Num() );
		ResetTexture( 0 );
		ResetTexture( 1 );
		ResetTexture( 2 );
		ResetTexture( 3 );
		glFinish();
		glDeleteTextures( TexAlloc.Num(), &TexAlloc(0) );
		TexAlloc.Empty();
		BindMap.Empty();
	}

	unguard;
}

UBOOL UNOpenGLESRenderDevice::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	return false;
}

void UNOpenGLESRenderDevice::Lock( FPlane FlashScale, FPlane FlashFog, FPlane ScreenClear, DWORD RenderLockFlags, BYTE* InHitData, INT* InHitSize )
{
	guard(UNOpenGLESRenderDevice::Lock);


	glClearColor( ScreenClear.X, ScreenClear.Y, ScreenClear.Z, ScreenClear.W );
	glClearDepthf( 1.f );
	glDepthFunc( GL_LEQUAL );

	// The client's Brightness (0..1; the shader scales by 0.5 + Brightness).
	FLOAT TargetBrightness = CurrentBrightness;
	if( Viewport && Viewport->GetOuterUClient() )
		TargetBrightness = Viewport->GetOuterUClient()->Brightness;
	else if( CurrentBrightness < 0.f )
		TargetBrightness = 0.5f;

	if( CurrentBrightness != TargetBrightness )
	{
		CurrentBrightness = TargetBrightness;
		UniformsChanged[UF_Brightness] = true;
	}

	SetBlend( PF_Occlude );
	SetShader( CurrentShaderFlags );

	GLbitfield ClearBits = GL_DEPTH_BUFFER_BIT;
	if( RenderLockFlags & LOCKR_ClearScreen )
		ClearBits |= GL_COLOR_BUFFER_BIT;
	glClear( ClearBits );

	if( FlashScale != FPlane(0.5f, 0.5f, 0.5f, 0.0f) || FlashFog != FPlane(0.0f, 0.0f, 0.0f, 0.0f) )
		ColorMod = FPlane( FlashFog.X, FlashFog.Y, FlashFog.Z, 1.f - Min( FlashScale.X * 2.f, 1.f ) );
	else
		ColorMod = FPlane( 0.f, 0.f, 0.f, 0.f );

	unguard;
}

void UNOpenGLESRenderDevice::Unlock( UBOOL Blit )
{
	guard(UNOpenGLESRenderDevice::Unlock);

	FlushTriangles();

	glFlush();

	unguard;
}

void UNOpenGLESRenderDevice::DrawComplexSurface( FSceneNode* Frame, FSurfaceInfo& Surface, FSurfaceFacet& Facet )
{
	guard(UNOpenGLESRenderDevice::DrawComplexSurface);

	check(Surface.Texture);

	SetSceneNode( Frame );
	SetBlend( Surface.PolyFlags );
	SetTexture( 0, *Surface.Texture, ( Surface.PolyFlags & PF_Masked ), 0.f );
	if( Surface.LightMap )
	{
		SetTexture( 1, *Surface.LightMap, 0, -0.5f );
		CurrentShaderFlags |= SF_Lightmap;
	}
	if( Surface.FogMap )
	{
		SetTexture( 2, *Surface.FogMap, 0, -0.5f );
		CurrentShaderFlags |= SF_Fogmap;
	}
	if( Surface.DetailTexture && DetailTextures )
	{
		SetTexture( 3, *Surface.DetailTexture, 0, 0.f );
		CurrentShaderFlags |= SF_Detail;
	}
	SetShader( CurrentShaderFlags );

	FLOAT UDot = Facet.MapCoords.XAxis | Facet.MapCoords.Origin;
	FLOAT VDot = Facet.MapCoords.YAxis | Facet.MapCoords.Origin;
	for( FSavedPoly* Poly = Facet.Polys; Poly; Poly = Poly->Next )
	{
		BeginPoly();
		for( INT i = 0; i < Poly->NumPts; i++ )
		{
			FLOAT U = Facet.MapCoords.XAxis | Poly->Pts[i]->Point;
			FLOAT V = Facet.MapCoords.YAxis | Poly->Pts[i]->Point;
			AttribFloat3( &Poly->Pts[i]->Point.X );
			AttribFloat2( (U-UDot-TexInfo[0].UPan)*TexInfo[0].UMult, (V-VDot-TexInfo[0].VPan)*TexInfo[0].VMult );
			if( Surface.LightMap )
				AttribFloat2( (U-UDot-TexInfo[1].UPan)*TexInfo[1].UMult, (V-VDot-TexInfo[1].VPan)*TexInfo[1].VMult );
			if( Surface.FogMap )
				AttribFloat2( (U-UDot-TexInfo[2].UPan)*TexInfo[2].UMult, (V-VDot-TexInfo[2].VPan)*TexInfo[2].VMult );
			if( Surface.DetailTexture && DetailTextures )
				AttribFloat2( (U-UDot-TexInfo[3].UPan)*TexInfo[3].UMult, (V-VDot-TexInfo[3].VPan)*TexInfo[3].VMult );
			PolyVertex();
		}
		EndPoly();
	}

	CurrentShaderFlags &= ~( SF_Lightmap|SF_Fogmap|SF_Detail );

	ResetTexture( 1 );
	ResetTexture( 2 );
	ResetTexture( 3 );

	unguard;
}

void UNOpenGLESRenderDevice::DrawGouraudPolygon( FSceneNode* Frame, FTextureInfo& Texture, FTransTexture** Pts, INT NumPts, DWORD PolyFlags, FSpanBuffer* SpanBuffer )
{
	guard(UNOpenGLESRenderDevice::DrawGouraudPolygon);

	const UBOOL IsFog = ( ( PolyFlags & ( PF_RenderFog|PF_Translucent|PF_Modulated ) ) == PF_RenderFog );
	const UBOOL IsModulated = ( PolyFlags & PF_Modulated );
	if( !IsModulated )
		CurrentShaderFlags |= SF_VtxColor;
	if( IsFog )
		CurrentShaderFlags |= SF_VtxFog;

	SetSceneNode( Frame );
	SetBlend( PolyFlags );
	SetTexture( 0, Texture, ( PolyFlags & PF_Masked ), 0 );
	SetShader( CurrentShaderFlags );

	BeginPoly();
	for( INT i=0; i<NumPts; i++ )
	{
		FTransTexture* P = Pts[i];
		AttribFloat3( &P->Point.X );
		AttribFloat2( P->U*TexInfo[0].UMult, P->V*TexInfo[0].VMult );
		if( !IsModulated )
			AttribFloat4( P->Light.X, P->Light.Y, P->Light.Z, 1.f );
		if( IsFog )
			AttribFloat4( &P->Fog.X );
		PolyVertex();
	}
	EndPoly();

	CurrentShaderFlags &= ~( SF_VtxColor|SF_VtxFog );

	unguard;
}

void UNOpenGLESRenderDevice::DrawTile( FSceneNode* Frame, FTextureInfo& Texture, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL, FLOAT U, FLOAT V, FLOAT UL, FLOAT VL, FSpanBuffer* Span, FLOAT Z, FPlane Light, FPlane Fog, DWORD PolyFlags )
{
	guard(UNOpenGLESRenderDevice::DrawTile);

	FPlane VtxColor;
	if( !( PolyFlags & PF_Modulated ) )
	{
		VtxColor.X = Light.X;
		VtxColor.Y = Light.Y;
		VtxColor.Z = Light.Z;
		VtxColor.W = 1.f;
	}
	else
	{
		VtxColor.X = 1.f;
		VtxColor.Y = 1.f;
		VtxColor.Z = 1.f;
		VtxColor.W = 1.f;
	}

	CurrentShaderFlags |= SF_VtxColor;

	SetSceneNode( Frame );
	SetBlend( PolyFlags );
	SetTexture( 0, Texture, ( PolyFlags & PF_Masked ), 0.f );
	SetShader( CurrentShaderFlags );

	BeginPoly();
		AttribFloat3( RFX2 * Z * (X - Frame->FX2), RFY2 * Z * (Y - Frame->FY2), Z );
		AttribFloat2( U * TexInfo[0].UMult, V * TexInfo[0].VMult );
		AttribFloat4( &VtxColor.X );
		PolyVertex();
		AttribFloat3( RFX2 * Z * (X + XL - Frame->FX2), RFY2 * Z * (Y - Frame->FY2), Z );
		AttribFloat2( (U + UL) * TexInfo[0].UMult, V * TexInfo[0].VMult );
		AttribFloat4( &VtxColor.X );
		PolyVertex();
		AttribFloat3( RFX2 * Z * (X + XL - Frame->FX2), RFY2 * Z * (Y + YL - Frame->FY2), Z );
		AttribFloat2( (U + UL) * TexInfo[0].UMult, (V + VL) *TexInfo[0].VMult );
		AttribFloat4( &VtxColor.X );
		PolyVertex();
		AttribFloat3( RFX2 * Z * (X - Frame->FX2), RFY2 * Z * (Y + YL - Frame->FY2), Z );
		AttribFloat2( U * TexInfo[0].UMult, (V + VL) * TexInfo[0].VMult );
		AttribFloat4( &VtxColor.X );
		PolyVertex();
	EndPoly();

	CurrentShaderFlags &= ~SF_VtxColor;

	unguard;
}

void UNOpenGLESRenderDevice::Draw2DLine( FSceneNode* Frame, FPlane Color, DWORD LineFlags, FVector P1, FVector P2 )
{

}

void UNOpenGLESRenderDevice::Draw2DPoint( FSceneNode* Frame, FPlane Color, DWORD LineFlags, FLOAT X1, FLOAT Y1, FLOAT X2, FLOAT Y2, FLOAT Z )
{

}

void UNOpenGLESRenderDevice::EndFlash( )
{
	guard(UNOpenGLESRenderDevice::EndFlash);

	if( ColorMod == FPlane( 0.f, 0.f, 0.f, 0.f ) )
		return;

	CurrentShaderFlags = SF_VtxColor;
	ResetTexture( 0 );
	ResetTexture( 1 );
	ResetTexture( 2 );
	ResetTexture( 3 );
	SetBlend( PF_Highlighted );
	SetShader( CurrentShaderFlags );

	const FLOAT Z = 1.f;
	const FLOAT RFX2 = RProjZ;
	const FLOAT RFY2 = RProjZ * Aspect;

	glDisable( GL_DEPTH_TEST );

	BeginPoly();
		AttribFloat3( RFX2 * -Z, RFY2 * -Z, Z );
		AttribFloat4( &ColorMod.X );
		PolyVertex();
		AttribFloat3( RFX2 * +Z, RFY2 * -Z, Z );
		AttribFloat4( &ColorMod.X );
		PolyVertex();
		AttribFloat3( RFX2 * +Z, RFY2 * +Z, Z );
		AttribFloat4( &ColorMod.X );
		PolyVertex();
		AttribFloat3( RFX2 * -Z, RFY2 * +Z, Z );
		AttribFloat4( &ColorMod.X );
		PolyVertex();
	EndPoly();

	glEnable( GL_DEPTH_TEST );

	CurrentShaderFlags &= ~SF_VtxColor;

	unguard;
}

void UNOpenGLESRenderDevice::PushHit( const BYTE* Data, INT Count )
{

}

void UNOpenGLESRenderDevice::PopHit( INT Count, UBOOL bForce )
{

}

void UNOpenGLESRenderDevice::GetStats( TCHAR* Result )
{
	guard(UNOpenGLESRenderDevice::GetStats)

	if( Result ) *Result = '\0';

	unguard;
}

void UNOpenGLESRenderDevice::ReadPixels( FColor* Pixels )
{
	guard(UNOpenGLESRenderDevice::ReadPixels);

	// The frame drawn so far, batched triangles included.
	FlushTriangles();

	// A read packs rows: PACK alignment, 1 (RGBA rows are 4-byte aligned anyway).
	glPixelStorei( GL_PACK_ALIGNMENT, 1 );
	glReadPixels( 0, 0, Viewport->SizeX, Viewport->SizeY, GL_RGBA, GL_UNSIGNED_BYTE, (void*)Pixels );

	// GL reads rows bottom-up; SHOT takes them top-down, in BGRA. Flip the rows and swap RGBA -> BGRA in one pass.
	const INT SizeX = Viewport->SizeX, SizeY = Viewport->SizeY;
	for( INT Y = 0; Y < ( SizeY + 1 ) / 2; ++Y )
	{
		FColor* Top    = Pixels + Y * SizeX;
		FColor* Bottom = Pixels + ( SizeY - 1 - Y ) * SizeX;
		for( INT X = 0; X < SizeX; ++X )
		{
			const FColor T = Top[X], B = Bottom[X];
			Top[X].R    = B.B; Top[X].G    = B.G; Top[X].B    = B.R; Top[X].A    = B.A;
			Bottom[X].R = T.B; Bottom[X].G = T.G; Bottom[X].B = T.R; Bottom[X].A = T.A;
		}
	}

	unguard;
}

void UNOpenGLESRenderDevice::ClearZ( FSceneNode* Frame )
{
	guard(UNOpenGLESRenderDevice::ClearZ);

	FlushTriangles();
	SetBlend( PF_Occlude );
	SetShader( CurrentShaderFlags );

	glClear( GL_DEPTH_BUFFER_BIT );

	unguard;
}

void UNOpenGLESRenderDevice::UpdateUniforms()
{
	guard(UNOpenGLESRenderDevice::UpdateUniforms);

	if( UniformsChanged[UF_Mtx] )
	{
		FlushTriangles();
		glUniformMatrix4fv( ShaderInfo->Uniforms[UF_Mtx], 1, GL_FALSE, &MtxMVP[0][0] );
		UniformsChanged[UF_Mtx] = false;
	}

	if( UniformsChanged[UF_Brightness] )
	{
		FlushTriangles();
		// A modulated poly multiplies the framebuffer by 2*texel, so its neutral grey must stay exactly 0.5: send the
		// neutral brightness while PF_Modulated is the current blend.
		glUniform1f( ShaderInfo->Uniforms[UF_Brightness], ( CurrentPolyFlags & PF_Modulated ) ? 0.5f : CurrentBrightness );
		UniformsChanged[UF_Brightness] = false;
	}

	for( INT i = UF_Texture0; i <= UF_Texture3; ++i )
	{
		if( UniformsChanged[i] && ShaderInfo->Uniforms[i] >= 0 )
		{
			glUniform1i( ShaderInfo->Uniforms[i], i - UF_Texture0 );
			UniformsChanged[i] = false;
		}
	}

	unguard;
}

GLuint UNOpenGLESRenderDevice::CompileShader( GLenum Type, const char* Text )
{
	guard(UNOpenGLESRenderDevice::CompileShader);

	GLuint Id = glCreateShader( Type );

	const char *Src[] = { Text, NULL };
	glShaderSource( Id, 1, Src, NULL );

	glCompileShader( Id );

	GLint Status = 0;
	glGetShaderiv( Id, GL_COMPILE_STATUS, &Status );
	if( !Status )
	{
		char Tmp[2048];
		glGetShaderInfoLog( Id, sizeof(Tmp), NULL, Tmp );
		appErrorf( "%s shader compilation failed:\n%s", ( Type == GL_FRAGMENT_SHADER ) ? "Fragment" : "Vertex", Tmp );
	}

	return Id;

	unguard;
}

UNOpenGLESRenderDevice::FCachedShader* UNOpenGLESRenderDevice::CreateShader( DWORD ShaderFlags )
{
	guard(UNOpenGLESRenderDevice::CreateShader);

	static const char* FlagNames[SF_Count] = {
		"SF_Texture0", "SF_Texture1", "SF_Texture2", "SF_Texture3",
		"SF_VtxColor", "SF_AlphaTest", "SF_Lightmap", "SF_Fogmap",
		"SF_Detail", "SF_VtxFog"
	};

	static const char* UniformNames[UF_Count] = {
		"uMtx", "uBrightness", "uTexture0", "uTexture1", "uTexture2", "uTexture3"
	};

	static const char* AttribNames[AT_Count] = {
		"aPosition", "aTexCoord0", "aTexCoord1", "aTexCoord2",
		"aTexCoord3", "aVtxColor", "aVtxFog"
	};

	static const DWORD AttribFlags[AT_Count] = {
		0, SF_Texture0, SF_Texture1, SF_Texture2, SF_Texture3, SF_VtxColor, SF_VtxFog
	};

	static const char* ShaderVersion = "#version 100\n";

	FCachedShader NewShaderValue;
	NewShaderValue.Flags = ShaderFlags;
	FCachedShader& NewShaderRef = ShaderMap.Set( ShaderFlags, NewShaderValue );
	FCachedShader* NewShader = &NewShaderRef;

	FString VSText;
	FString FSText;

	VSText += ShaderVersion;
	FSText += ShaderVersion;

	for( DWORD Flag = 1, FlagNum = 0; Flag <= SF_Max; Flag <<= 1, ++FlagNum )
	{
		if( ShaderFlags & Flag )
		{
			TCHAR Buf[256];
			appSprintf( Buf, TEXT("#define %s %u\n"), FlagNames[FlagNum], Flag );
			VSText += Buf;
			FSText += Buf;
		}
	}

	if( Overbright )
	{
		TCHAR Buf[256];
		appSprintf( Buf, TEXT("#define LIGHTMAP_OVERBRIGHT %f\n"), LIGHTMAP_OVERBRIGHT );
		FSText += Buf;
	}

	VSText += VertShaderGLSL;
	FSText += FragShaderGLSL;

	GLuint VS = CompileShader( GL_VERTEX_SHADER, *VSText );
	GLuint FS = CompileShader( GL_FRAGMENT_SHADER, *FSText );

	GLuint Prog = glCreateProgram();
	glAttachShader( Prog, VS );
	glAttachShader( Prog, FS );

	NewShader->NumFloats = 0;
	for( INT i = 0; i < AT_Count; ++i )
	{
		if( i == 0 || ( ShaderFlags & AttribFlags[i] ) )
		{
			glBindAttribLocation( Prog, i, AttribNames[i] );
			NewShader->Attribs[i] = true;
			NewShader->NumFloats += AttribSizes[i];
		}
		else
		{
			NewShader->Attribs[i] = false;
		}
	}

	glLinkProgram( Prog );

	GLint Status = 0;
	glGetProgramiv( Prog, GL_LINK_STATUS, &Status );
	if( !Status )
	{
		char Tmp[2048];
		glGetProgramInfoLog( Prog, sizeof(Tmp), NULL, Tmp );
		appErrorf( "Failed to link shader %08x:\n%s", ShaderFlags, Tmp );
	}

	glDeleteShader( VS );
	glDeleteShader( FS );

	NewShader->Prog = Prog;

	for( INT i = 0; i < UF_Count; ++i )
		NewShader->Uniforms[i] = glGetUniformLocation( NewShader->Prog, UniformNames[i] );

	return NewShader;
	unguard;
}

void UNOpenGLESRenderDevice::SetShader( DWORD ShaderFlags )
{
	guard(UNOpenGLESRenderDevice::SetShader);

	if( !ShaderInfo || ShaderInfo->Flags != ShaderFlags )
	{
		FlushTriangles();

		ShaderInfo = ShaderMap.Find( ShaderFlags );
		if( !ShaderInfo )
			ShaderInfo = CreateShader( ShaderFlags );
		verify( ShaderInfo );

		// TODO: probably don't do this every program change
		for( INT i = 0; i < UF_Count; ++i )
			UniformsChanged[i] = true;

		BYTE* Ptr = UseVAO ? nullptr : (BYTE*)VtxData;
		for( INT i = 0; i < AT_Count; ++i )
		{
			if( ShaderInfo->Attribs[i] )
			{
				glEnableVertexAttribArray( i );
				glVertexAttribPointer( i, AttribSizes[i], GL_FLOAT, GL_FALSE, ShaderInfo->NumFloats * sizeof(FLOAT), (void*)Ptr );
				Ptr += AttribSizes[i] * sizeof(FLOAT);
			}
			else
			{
				glDisableVertexAttribArray( i );
			}
		}

		glUseProgram( ShaderInfo->Prog );
	}

	UpdateUniforms();

	unguard;
}

void UNOpenGLESRenderDevice::SetSceneNode( FSceneNode* Frame )
{
	guard(UNOpenGLESRenderDevice::SetSceneNode);

	check(Viewport);

	if( !Frame )
	{
		// invalidate current saved data
		CurrentSceneNode.X = -1;
		CurrentSceneNode.FX = -1.f;
		CurrentSceneNode.SizeX = -1;
		return;
	}

	if( Frame->X != CurrentSceneNode.X || Frame->Y != CurrentSceneNode.Y ||
			Frame->XB != CurrentSceneNode.XB || Frame->YB != CurrentSceneNode.YB ||
			Viewport->SizeX != CurrentSceneNode.SizeX || Viewport->SizeY != CurrentSceneNode.SizeY )
	{
		FlushTriangles();
		glViewport( Frame->XB, Viewport->SizeY - Frame->Y - Frame->YB, Frame->X, Frame->Y );
		CurrentSceneNode.X = Frame->X;
		CurrentSceneNode.Y = Frame->Y;
		CurrentSceneNode.XB = Frame->XB;
		CurrentSceneNode.YB = Frame->YB;
		CurrentSceneNode.SizeX = Viewport->SizeX;
		CurrentSceneNode.SizeY = Viewport->SizeY;
	}

	if( Frame->FX != CurrentSceneNode.FX || Frame->FY != CurrentSceneNode.FY ||
			Viewport->Actor->FovAngle != CurrentSceneNode.FovAngle )
	{
		RProjZ = appTan( Viewport->Actor->FovAngle * PI / 360.0 );
		Aspect = Frame->FY / Frame->FX;
		RFX2 = 2.0f * RProjZ / Frame->FX;
		RFY2 = 2.0f * RProjZ * Aspect / Frame->FY;
		MtxProj = glm::frustum( -RProjZ, +RProjZ, -Aspect * RProjZ, +Aspect * RProjZ, 1.f, 32768.f );
		MtxMVP = MtxProj * MtxModelView;
		CurrentSceneNode.FX = Frame->FX;
		CurrentSceneNode.FY = Frame->FY;
		CurrentSceneNode.FovAngle = Viewport->Actor->FovAngle;
		UniformsChanged[UF_Mtx] = true;
	}

	unguard;
}

void UNOpenGLESRenderDevice::SetBlend( DWORD PolyFlags, UBOOL InverseOrder )
{
	guard(UNOpenGLESRenderDevice::SetBlend);

	// Adjust PolyFlags according to Unreal's precedence rules.
	if( !(PolyFlags & (PF_Translucent|PF_Modulated)) )
		PolyFlags |= PF_Occlude;
	else if( PolyFlags & PF_Translucent )
		PolyFlags &= ~PF_Masked;

	// Detect changes in the blending modes.
	DWORD Xor = CurrentPolyFlags ^ PolyFlags;
	if( Xor & (PF_Translucent|PF_Modulated|PF_Invisible|PF_Occlude|PF_Masked|PF_Highlighted) )
	{
		FlushTriangles();
		if( Xor & (PF_Translucent|PF_Modulated|PF_Highlighted) )
		{
			glEnable( GL_BLEND );
			if( PolyFlags & PF_Translucent )
			{
				glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_COLOR );
			}
			else if( PolyFlags & PF_Modulated )
			{
				glBlendFunc( GL_DST_COLOR, GL_SRC_COLOR );
			}
			else if( PolyFlags & PF_Highlighted )
			{
				glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
			}
			else
			{
				glDisable( GL_BLEND );
				glBlendFunc( GL_ONE, GL_ZERO );
			}
		}
		if( Xor & PF_Invisible )
		{
			UBOOL Show = !( PolyFlags & PF_Invisible );
			glColorMask( Show, Show, Show, Show );
		}
		if( Xor & PF_Modulated )
			UniformsChanged[UF_Brightness] = true; // brightness is neutral for modulated polys (UpdateUniforms)
		if( Xor & PF_Occlude )
			glDepthMask( (PolyFlags & PF_Occlude) != 0 );
		if( Xor & PF_Masked )
		{
			if( PolyFlags & PF_Masked )
				CurrentShaderFlags |= SF_AlphaTest;
			else
				CurrentShaderFlags &= ~SF_AlphaTest;
		}
	}

	CurrentPolyFlags = PolyFlags;

	unguard;
}

void UNOpenGLESRenderDevice::UpdateTextureFilter( const FTextureInfo& Info, DWORD PolyFlags )
{
	guard(UNOpenGLESRenderDevice::UpdateTextureFilter);

	// Set mip filtering if there are mips.
	if( ( PolyFlags & PF_NoSmooth ) || ( NoFiltering && Info.Palette ) ) // TODO: This is set per poly, not per texture.
	{
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, ( Info.NumMips > 1 ) ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	}
	else
	{
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, ( Info.NumMips > 1 ) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		// Anisotropy on the smooth path, with mips to sample across; MaxAnisotropy is clamped at Init.
		if( MaxAnisotropy > 1.f && Info.NumMips > 1 )
			glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, MaxAnisotropy );
	}

	// This is a light/fog map.
	if( !Info.Palette )
	{
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	}

	unguard;
}

void UNOpenGLESRenderDevice::ResetTexture( INT TMU )
{
	guard(UNOpenGLESRenderDevice::ResetTexture);

	CurrentShaderFlags &= ~(1 << TMU);

	if( TexInfo[TMU].CurrentCacheID != 0 )
	{
		FlushTriangles();
		glActiveTexture( GL_TEXTURE0 + TMU );
		glBindTexture( GL_TEXTURE_2D, 0 );
		TexInfo[TMU].CurrentCacheID = 0;
	}

	unguard;
}

void UNOpenGLESRenderDevice::SetTexture( INT TMU, FTextureInfo& Info, DWORD PolyFlags, FLOAT PanBias )
{
	guard(UNOpenGLESRenderDevice::SetTexture);

	CurrentShaderFlags |= 1 << TMU;

	// Set panning.
	FTexInfo& Tex = TexInfo[TMU];
	Tex.UPan      = Info.Pan.X + PanBias*Info.UScale;
	Tex.VPan      = Info.Pan.Y + PanBias*Info.VScale;

	// Account for all the impact on scale normalization.
	Tex.UMult = 1.f / (Info.UScale * static_cast<FLOAT>(Info.USize));
	Tex.VMult = 1.f / (Info.VScale * static_cast<FLOAT>(Info.VSize));

	// Find in cache.
	QWORD NewCacheID = Info.CacheID;
	if( ( PolyFlags & PF_Masked ) && Info.Palette )
		NewCacheID |= MASKED_TEXTURE_TAG;
	UBOOL RealtimeChanged = Info.bRealtimeChanged;
	if( NewCacheID == Tex.CurrentCacheID && !RealtimeChanged )
		return;

	FlushTriangles();

	// Make current.
	Tex.CurrentCacheID = NewCacheID;
	FCachedTexture* Bind = BindMap.Find( NewCacheID );
	FCachedTexture* OldBind = Bind;
	if( !Bind )
	{
		// New texture.
		FCachedTexture NewTexture;
		FCachedTexture& TexRef = BindMap.Set( NewCacheID, NewTexture );
		Bind = &TexRef;
		glGenTextures( 1, &Bind->Id );
		TexAlloc.AddItem( Bind->Id );
	}

	glActiveTexture( GL_TEXTURE0 + TMU );
	glBindTexture( GL_TEXTURE_2D, Bind->Id );

	if( !OldBind || RealtimeChanged )
	{
		// New texture or it has changed, upload it.
		Info.bRealtimeChanged = 0;
		UploadTexture( Info, ( PolyFlags & PF_Masked ), !OldBind );
		// TODO: This depends on PolyFlags, not Info.
		UpdateTextureFilter( Info, PolyFlags );
	}

	unguard;
}

void UNOpenGLESRenderDevice::UploadTexture( FTextureInfo& Info, UBOOL Masked, UBOOL NewTexture )
{
	guard(UNOpenGLESRenderDevice::UploadTexture);

	if( !Info.Mips[0] )
	{
		debugf( NAME_Warning, "Encountered texture with invalid mips!" );
		return;
	}

	// We're gonna be using the compose buffer, so expand it to fit.
	INT NewComposeSize = Info.Mips[0]->USize * Info.Mips[0]->VSize * 4;
	if( NewComposeSize > ComposeSize )
	{
		Compose = (BYTE*)appRealloc( Compose, NewComposeSize, "GLComposeBuf" );
		verify( Compose );
	}

	// Upload all mips.
	for( INT MipIndex = 0; MipIndex < Info.NumMips; ++MipIndex )
	{
		FMipmapBase* Mip = Info.Mips[MipIndex];
		if( !Mip || !Mip->DataPtr ) break;
		BYTE* UploadBuf;
		GLenum UploadFormat;
		// Convert texture if needed.
		if( Info.Palette )
		{
			// 8-bit indexed. We have to fix the alpha component since it's mostly garbage in non-detailmaps.
			UploadBuf = Compose;
			UploadFormat = GL_RGBA;
			DWORD* Dst = (DWORD*)Compose;
			const BYTE* Src = (const BYTE*)Mip->DataPtr;
			const DWORD* Pal = (const DWORD*)Info.Palette;
			const DWORD Count = Mip->USize * Mip->VSize;
			if( Masked )
			{
				// index 0 is transparent
				for( DWORD i = 0; i < Count; ++i, ++Src )
					*Dst++ = *Src ? ( Pal[*Src] | ALPHA_MASK ) : 0;
			}
			else
			{
				// index 0 is whatever
				for( DWORD i = 0; i < Count; ++i )
					*Dst++ = ( Pal[*Src++] | ALPHA_MASK );
			}
		}
		else if( UseBGRA )
		{
			// BGRA8888 (or 7777) and we can upload it as-is.
			UploadBuf = Mip->DataPtr;
			UploadFormat = GL_BGRA_EXT;
		}
		else
		{
			// BGRA8888 (or 7777), but we must swap it because it's not supported natively.
			UploadBuf = Compose;
			UploadFormat = GL_RGBA;
			BYTE* Dst = (BYTE*)Compose;
			const BYTE* Src = (const BYTE*)Mip->DataPtr;
			const DWORD Count = Mip->USize * Mip->VSize;
			for( DWORD i = 0; i < Count; ++i, Src += 4 )
			{
				*Dst++ = Src[2];
				*Dst++ = Src[1];
				*Dst++ = Src[0];
				*Dst++ = Src[3];
			}
		}
		// Upload to GL.
		if( NewTexture )
			glTexImage2D( GL_TEXTURE_2D, MipIndex, UploadFormat, Mip->USize, Mip->VSize, 0, UploadFormat, GL_UNSIGNED_BYTE, (void*)UploadBuf );
		else
			glTexSubImage2D( GL_TEXTURE_2D, MipIndex, 0, 0, Mip->USize, Mip->VSize, UploadFormat, GL_UNSIGNED_BYTE, (void*)UploadBuf );
	}

	unguard;
}

void UNOpenGLESRenderDevice::UpdateSwapInterval()
{
	guard(UNOpenGLESRenderDevice::UpdateSwapInterval);

	if( SwapInterval < -1 )
	{
		SwapInterval = -1;
	}

#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
	// Presentation is ours (explicitSwapControl + emscripten_webgl_commit_frame), so SDL's swap interval
	// is meaningless here and would only log a failure against a context SDL does not own.
	if( 0 )
#else
	if( SDL_GL_SetSwapInterval( SwapInterval ) < 0 )
#endif
	{
		debugf( NAME_Warning, "Failed to set swap interval %d: %s", SwapInterval, SDL_GetError() );
		if( SwapInterval < 0 )
		{
			// Adaptive VSync not supported, try normal VSync.
			SwapInterval = 1;
			UpdateSwapInterval();
		}
	}

	unguard;
}
