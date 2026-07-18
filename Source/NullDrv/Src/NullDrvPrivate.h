/*=============================================================================
	NullDrvPrivate.h: Null render device (GPU-free) private header.
	ufront: a no-op URenderDevice for a headless / "poor-man's dedicated server" run.
=============================================================================*/

#include "RenderPrivate.h"

/*------------------------------------------------------------------------------------
	UNullRenderDevice.
------------------------------------------------------------------------------------*/

//
// ufront: a null render device — implements the v400 URenderDevice interface with no-op draw calls and no
// GL context. Selected via -NULLRENDERER. The NSDL viewport decides GL-vs-software purely from the render
// device NAME containing "OPENGL" (UNSDLViewport::OpenWindow, before Init runs; it ignores the BLIT_* type
// and the device class). "NullDrv.NullRenderDevice" lacks "OPENGL", so the viewport creates an SDL_Texture
// software surface and NO WebGL context — the game runs GPU-free. This mirrors, for the older UE1
// immediate-mode interface, what UT2004's Engine.NullRenderDevice (UE2.5 / FRenderInterface) does: a valid
// render device that draws nothing, so a listen server can run with an empty tab.
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
