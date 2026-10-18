/*=============================================================================
	NullDrv.cpp: Null (GPU-free) render device.
	A URenderDevice that draws nothing, selected with -NULLRENDERER for a headless run.
=============================================================================*/

#include "NullDrvPrivate.h"

/*-----------------------------------------------------------------------------
	Global implementation.
-----------------------------------------------------------------------------*/

IMPLEMENT_PACKAGE(NullDrv);
IMPLEMENT_CLASS(UNullRenderDevice);

/*-----------------------------------------------------------------------------
	UNullRenderDevice implementation.
-----------------------------------------------------------------------------*/

void UNullRenderDevice::StaticConstructor()
{
	guard(UNullRenderDevice::StaticConstructor);
	// The null device has nothing to configure (no options / no GL state).
	unguard;
}

UBOOL UNullRenderDevice::Init( UViewport* InViewport, INT NewX, INT NewY, INT NewColorBytes, UBOOL Fullscreen )
{
	guard(UNullRenderDevice::Init);
	Viewport            = InViewport;
	SpanBased           = 0;
	FullscreenOnly      = 0;
	SupportsFogMaps     = 0;
	SupportsDistanceFog = 0;
	return SetRes( NewX, NewY, NewColorBytes, Fullscreen );
	unguard;
}

UBOOL UNullRenderDevice::SetRes( INT NewX, INT NewY, INT NewColorBytes, UBOOL Fullscreen )
{
	guard(UNullRenderDevice::SetRes);
	if( NewColorBytes!=2 && NewColorBytes!=4 )
		NewColorBytes = 4;
	// Claim a software surface: the SDL viewport makes a GL context only for a device name containing "OPENGL".
	// Nothing is ever written to it (every Draw* below is a no-op).
	return Viewport->ResizeViewport( BLIT_DibSection, Align(NewX,2), NewY, NewColorBytes );
	unguard;
}

void UNullRenderDevice::Exit() {}
void UNullRenderDevice::Flush( UBOOL /*AllowPrecache*/ ) {}
void UNullRenderDevice::Lock( FPlane, FPlane, FPlane, DWORD, BYTE*, INT* ) {}
void UNullRenderDevice::Unlock( UBOOL /*Blit*/ ) {}
void UNullRenderDevice::DrawComplexSurface( FSceneNode*, FSurfaceInfo&, FSurfaceFacet& ) {}
void UNullRenderDevice::DrawGouraudPolygon( FSceneNode*, FTextureInfo&, FTransTexture**, INT, DWORD, FSpanBuffer* ) {}
void UNullRenderDevice::DrawTile( FSceneNode*, FTextureInfo&, FLOAT, FLOAT, FLOAT, FLOAT, FLOAT, FLOAT, FLOAT, FLOAT, FSpanBuffer*, FLOAT, FPlane, FPlane, DWORD ) {}
void UNullRenderDevice::Draw2DLine( FSceneNode*, FPlane, DWORD, FVector, FVector ) {}
void UNullRenderDevice::Draw2DPoint( FSceneNode*, FPlane, DWORD, FLOAT, FLOAT, FLOAT, FLOAT, FLOAT ) {}
void UNullRenderDevice::ClearZ( FSceneNode* ) {}
void UNullRenderDevice::PushHit( const BYTE*, INT ) {}
void UNullRenderDevice::PopHit( INT, UBOOL ) {}
void UNullRenderDevice::GetStats( TCHAR* Result ) { if( Result ) Result[0] = 0; }
void UNullRenderDevice::ReadPixels( FColor* ) {}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
