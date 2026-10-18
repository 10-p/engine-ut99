/*=============================================================================
	HTTPDownload.h: Unreal HTTP file download.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Jack Porter.
=============================================================================*/

//
// v436: the download manager a server names with `DLMGR CLASS=IpDrv.HTTPDownload PARAMS=<RedirectToURL>`.
// It is C++-only (v436's IpDrv.u has no script class for it), so there is no script layout to match.
// Natively it drives its own non-blocking TCP socket with this package's socket helpers (FResolveInfo,
// getlocalbindaddr, bindnextport). A web build fetches through the GWebHttp hook table (WebHttp.h)
// instead; with no hook to take the download it fails at once, and the next download manager takes over.
//

enum EHTTPState
{
	HTTP_Initialized		=0,
	HTTP_Resolving			=1,
	HTTP_Resolved			=2,
	HTTP_Connecting			=3,
	HTTP_ReceivingHeader	=4,
	HTTP_ReceivingData		=5,
	HTTP_Closed				=6,
};

class UHTTPDownload : public UDownload
{
	DECLARE_CLASS(UHTTPDownload,UDownload,CLASS_Transient|CLASS_Config)

	// Config.
	FStringNoInit	ProxyServerHost;
	INT				ProxyServerPort;
	// Also config: `RedirectToURL` and `UseCompression`, registered on UDownload's DownloadParams and
	// UseCompression.

	// Variables.
	BYTE			HTTPState;
	SOCKADDR_IN		LocalAddr;
	SOCKADDR_IN		ServerAddr;
	SOCKET			ServerSocket;
	FResolveInfo*	ResolveInfo;
	FURL			DownloadURL;
	TArray<BYTE>	ReceivedData;
	TArray<FString>	Headers;
	DOUBLE			ConnectStartTime;	// appSeconds() at connect.

	// Constructors.
	UHTTPDownload();
	void StaticConstructor();

	// UObject interface.
	void Destroy();
	void Serialize( FArchive& Ar );

	// UDownload interface.
	void ReceiveFile( UNetConnection* InConnection, INT PackageIndex, const TCHAR* Params=NULL, UBOOL InCompression=0 );
	UBOOL TrySkipFile();
	void Tick();

	// UHTTPDownload interface.
	UBOOL FetchData();

#ifdef __EMSCRIPTEN__
	// The web fetch through GWebHttp. These follow the v436 members; the class is C++-only, so
	// nothing depends on its size.
	INT WebHandle;			// from GWebHttp->Open(); -1 = none
	DOUBLE WebActivityTime;	// appSeconds() at the fetch's start or its last data (the stall timeout's base)
	void WebReceiveFile();
	void WebTick();
	void WebClose();
#endif
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
