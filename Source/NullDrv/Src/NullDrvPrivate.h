/*=============================================================================
	NullDrvPrivate.h: Null render device (GPU-free) private header.
	A URenderDevice that draws nothing, for a headless run.
=============================================================================*/

#include "RenderPrivate.h"

/*------------------------------------------------------------------------------------
	UNullRenderDevice.
------------------------------------------------------------------------------------*/

//
// The v400 URenderDevice interface with no-op draw calls, selected with -NULLRENDERER. UNSDLViewport::OpenWindow
// makes a GL context only for a device name containing "OPENGL", so this one gets a software surface and the
// game runs without a GPU (a listen server with an empty tab).
//
class DLL_EXPORT UNullRenderDevice : public URenderDevice
{
	DECLARE_CLASS(UNullRenderDevice,URenderDevice,CLASS_Config)

	// Constructors.
	void StaticConstructor();

	// URenderDevice interface. Only Init/SetRes do anything (claim a software surface); every draw is a no-op.
	UBOOL Init( UViewport* InViewport, INT NewX, INT NewY, INT NewColorBytes, UBOOL Fullscreen );
	UBOOL SetRes( INT NewX, INT NewY, INT NewColorBytes, UBOOL Fullscreen );
	void Exit();
	void Flush( UBOOL AllowPrecache );
	void Lock( FPlane FlashScale, FPlane FlashFog, FPlane ScreenClear, DWORD RenderLockFlags, BYTE* HitData, INT* HitSize );
	void Unlock( UBOOL Blit );
	void DrawComplexSurface( FSceneNode* Frame, FSurfaceInfo& Surface, FSurfaceFacet& Facet );
	void DrawGouraudPolygon( FSceneNode* Frame, FTextureInfo& Info, FTransTexture** Pts, INT NumPts, DWORD PolyFlags, FSpanBuffer* Span );
	void DrawTile( FSceneNode* Frame, FTextureInfo& Info, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL, FLOAT U, FLOAT V, FLOAT UL, FLOAT VL, FSpanBuffer* Span, FLOAT Z, FPlane Light, FPlane Fog, DWORD PolyFlags );
	void Draw2DLine( FSceneNode* Frame, FPlane Color, DWORD LineFlags, FVector P1, FVector P2 );
	void Draw2DPoint( FSceneNode* Frame, FPlane Color, DWORD LineFlags, FLOAT X1, FLOAT Y1, FLOAT X2, FLOAT Y2, FLOAT Z );
	void ClearZ( FSceneNode* Frame );
	void PushHit( const BYTE* Data, INT Count );
	void PopHit( INT Count, UBOOL bForce );
	void GetStats( TCHAR* Result );
	void ReadPixels( FColor* Pixels );
};

/*------------------------------------------------------------------------------------
	The End.
------------------------------------------------------------------------------------*/
