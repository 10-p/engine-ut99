/*=============================================================================
	HTTPDownload.h: Unreal HTTP file download.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Jack Porter.
=============================================================================*/

//
// v436-compat port: v400's IpDrv has no download manager at all -- UHTTPDownload is new in 436, and it
// is the manager a server advertises with `DLMGR CLASS=IpDrv.HTTPDownload PARAMS=<RedirectToURL>`.
// The class is C++-only: 436's IpDrv.u carries no `HTTPDownload` name, so there is no script layout to
// match. Shape and bodies come from the retail v436 IpDrv.dll (class registrant 10703c50: sizeof 0x4f4,
// CLASS_Transient|CLASS_Config, Super=UDownload).
//
// It drives its own non-blocking TCP socket, so it lives on the IpDrv side and uses this package's
// existing socket machinery (FResolveInfo, getlocalbindaddr, bindnextport) exactly as ATcpLink and
// UTcpNetDriver do.
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
	FStringNoInit	ProxyServerHost;	// 436 @0x454; left uninitialized by the constructor.
	INT				ProxyServerPort;	// 436 @0x460.
	// `RedirectToURL` and `UseCompression` are registered here too, but they alias UDownload's
	// DownloadParams (@0x34) and UseCompression (@0x40) -- see StaticConstructor.

	// Variables.
	BYTE			HTTPState;			// 436 @0x464.
	SOCKADDR_IN		LocalAddr;			// 436 @0x468.
	SOCKADDR_IN		ServerAddr;			// 436 @0x478.
	SOCKET			ServerSocket;		// 436 @0x488.
	FResolveInfo*	ResolveInfo;		// 436 @0x48c.
	FURL			DownloadURL;		// 436 @0x490.
	TArray<BYTE>	ReceivedData;		// 436 @0x4d4.
	TArray<FString>	Headers;			// 436 @0x4e0.
	DOUBLE			ConnectStartTime;	// 436 @0x4ec, an FTime there; DOUBLE is our appSeconds.

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
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
