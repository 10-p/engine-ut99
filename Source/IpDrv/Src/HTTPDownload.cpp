/*=============================================================================
	HTTPDownload.cpp: Unreal HTTP file download.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Jack Porter.
=============================================================================*/

#include "IpDrvPrivate.h"
#include "HTTPDownload.h"

/*-----------------------------------------------------------------------------
	UHTTPDownload implementation.
-----------------------------------------------------------------------------*/

//
// Put a socket in non-blocking mode. Same body as ATcpLink's, which is where this package keeps it;
// WinSock has no fcntl, so the switch is the socket API, not the compiler.
//
static UBOOL HTTPSetNonBlocking( SOCKET Socket )
{
	#if __BSD_SOCKETS__
	INT pd_flags = fcntl( Socket, F_GETFL, 0 );
	pd_flags |= O_NONBLOCK;
	return fcntl( Socket, F_SETFL, pd_flags ) == 0;
	#else
	DWORD NoBlock = 1;
	return ioctlsocket( Socket, FIONBIO, &NoBlock ) == 0;
	#endif
}

UHTTPDownload::UHTTPDownload()
{
	guard(UHTTPDownload::UHTTPDownload);
	ServerSocket = INVALID_SOCKET;
	ResolveInfo  = NULL;
	HTTPState    = HTTP_Initialized;
	ConnectStartTime = 0.0;
	unguard;
}

void UHTTPDownload::StaticConstructor()
{
	guard(UHTTPDownload::StaticConstructor);
	// The last two alias UDownload's members: 436 registers `RedirectToURL` on DownloadParams and
	// `UseCompression` on UseCompression, so the wire's `DLMGR PARAMS=`/`COMPRESSION=` and the ini's
	// `[IpDrv.HTTPDownload]` block are the same two values.
	new(GetClass(),TEXT("ProxyServerHost"),RF_Public)UStrProperty ( CPP_PROPERTY(ProxyServerHost), TEXT("Settings"), CPF_Config );
	new(GetClass(),TEXT("ProxyServerPort"),RF_Public)UIntProperty ( CPP_PROPERTY(ProxyServerPort), TEXT("Settings"), CPF_Config );
	new(GetClass(),TEXT("RedirectToURL"),  RF_Public)UStrProperty ( CPP_PROPERTY(DownloadParams),  TEXT("Settings"), CPF_Config );
	new(GetClass(),TEXT("UseCompression"), RF_Public)UBoolProperty( CPP_PROPERTY(UseCompression),  TEXT("Settings"), CPF_Config );
	unguard;
}

void UHTTPDownload::Serialize( FArchive& Ar )
{
	guard(UHTTPDownload::Serialize);
	Super::Serialize( Ar );
	Ar << ReceivedData << Headers << DownloadURL;
	unguard;
}

void UHTTPDownload::Destroy()
{
	guard(UHTTPDownload::Destroy);
	if( ServerSocket != INVALID_SOCKET )
	{
		closesocket( ServerSocket );
		ServerSocket = INVALID_SOCKET;
	}
	Super::Destroy();
	unguard;
}

void UHTTPDownload::ReceiveFile( UNetConnection* InConnection, INT InPackageIndex, const TCHAR* Params, UBOOL InCompression )
{
	guard(UHTTPDownload::ReceiveFile);
	UDownload::ReceiveFile( InConnection, InPackageIndex, Params, InCompression );

	if( !Params || !*Params )
		return;
	IsCompressed = InCompression;

	FPackageInfo& PkgInfo = Connection->PackageMap->List( PackageIndex );
	FURL Base( NULL, TEXT(""), TRAVEL_Absolute );
	Base.Port = 80;

	// 436 simply appends the server's FNAME= to RedirectToURL. The %guid%/%ext%/%file% substitutions
	// and the URL escaping are later (UT200x) additions and are deliberately absent here.
	FString File   = *PkgInfo.URL;
	FString StrURL = Params;
	if( IsCompressed )
		DownloadURL = FURL( &Base, *(StrURL + File + TEXT(".uz")), TRAVEL_Relative );
	else
		DownloadURL = FURL( &Base, *(StrURL + File), TRAVEL_Relative );

	const TCHAR* h = (*(*ProxyServerHost)) ? *ProxyServerHost : *DownloadURL.Host;
	DWORD addr = inet_addr( appToAnsi(h) );
	if( addr==INADDR_NONE )
	{
		ResolveInfo = new FResolveInfo( h );
		HTTPState = HTTP_Resolving;
	}
	else
	{
		ServerAddr.sin_addr = *((in_addr*)&addr);
		debugf( TEXT("Decoded IP %s (%s)"), h, *IpString(ServerAddr.sin_addr) );
		HTTPState = HTTP_Resolved;
	}

	// Show progress while connecting.
	ReceiveData( NULL, 0 );
	unguard;
}

void UHTTPDownload::Tick()
{
	guard(UHTTPDownload::Tick);
	switch( HTTPState )
	{
	case HTTP_Resolving:
	{
		check(ResolveInfo);
		if( !ResolveInfo->Resolved() )
		{
			// Host name still resolving.
			break;
		}
		else if( ResolveInfo->GetError() )
		{
			// Host name resolution just now failed.
			DownloadError( *FString::Printf( LocalizeError(TEXT("InvalidUrl"),TEXT("Engine")), (*(*ProxyServerHost)) ? *ProxyServerHost : *DownloadURL.Host ) );
			HTTPState = HTTP_Closed;
			delete ResolveInfo;
			ResolveInfo = NULL;
			return;
		}
		else
		{
			// Host name resolution just now succeeded.
			debugf( TEXT("Resolved %s (%s)"), ResolveInfo->GetHostName(), *IpString(ResolveInfo->GetAddr()) );
			ServerAddr.sin_addr = ResolveInfo->GetAddr();
			delete ResolveInfo;
			ResolveInfo = NULL;
			HTTPState = HTTP_Resolved;
		}
		break;
	}
	case HTTP_Resolved:
	{
		ServerAddr.sin_family = AF_INET;
		ServerAddr.sin_port   = htons( (*(*ProxyServerHost)) ? ProxyServerPort : DownloadURL.Port );

		// Connect.
		ServerSocket = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
		INT optval = 1;
		setsockopt( ServerSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(INT) );
		if( ServerSocket == INVALID_SOCKET )
		{
			DownloadError( LocalizeError(TEXT("ConnectionFailed"),TEXT("Engine")) );
			HTTPState = HTTP_Closed;
			return;
		}
		LINGER ling;
		ling.l_onoff  = 1;
		ling.l_linger = 0;
		setsockopt( ServerSocket, SOL_SOCKET, SO_LINGER, (LPSTR)&ling, sizeof(ling) );

		LocalAddr.sin_family = AF_INET;
		LocalAddr.sin_addr   = getlocalbindaddr( *GLog );
		LocalAddr.sin_port   = 0;
		if( !bindnextport( ServerSocket, &LocalAddr, 20, 1 ) )
		{
			debugf( NAME_DevNet, TEXT("HTTPDownload: bind() failed") );
			DownloadError( LocalizeError(TEXT("ConnectionFailed"),TEXT("Engine")) );
			HTTPState = HTTP_Closed;
			return;
		}
		HTTPSetNonBlocking( ServerSocket );
		if( connect( ServerSocket, (SOCKADDR*)&ServerAddr, sizeof(ServerAddr) ) == SOCKET_ERROR )
		{
			// 436 tests WSAEWOULDBLOCK alone, which is what a non-blocking WinSock connect() reports.
			// A BSD socket reports EINPROGRESS instead, so the shim adds it -- the same platform
			// difference the rest of this package routes through UnSocket.h.
			INT Error = WSAGetLastError();
			#if __BSD_SOCKETS__
			UBOOL Pending = ( Error==WSAEWOULDBLOCK || Error==EINPROGRESS );
			#else
			UBOOL Pending = ( Error==WSAEWOULDBLOCK );
			#endif
			if( !Pending )
			{
				debugf( NAME_DevNet, TEXT("HTTPDownload: connect() failed") );
				DownloadError( LocalizeError(TEXT("ConnectionFailed"),TEXT("Engine")) );
				HTTPState = HTTP_Closed;
				return;
			}
		}
		ConnectStartTime = appSeconds();
		HTTPState = HTTP_Connecting;
		break;
	}
	case HTTP_Connecting:
	{
		// Check for writability: a writable socket means the connection attempt succeeded.
		fd_set SocketSet;
		TIMEVAL SelectTime = {0,0};
		FD_ZERO( &SocketSet );
		FD_SET( ServerSocket, &SocketSet );
		INT Ready = select( ServerSocket+1, NULL, &SocketSet, NULL, &SelectTime );
		if( Ready == SOCKET_ERROR )
		{
			debugf( NAME_DevNet, TEXT("HTTPDownload: select() failed") );
			DownloadError( LocalizeError(TEXT("ConnectionFailed"),TEXT("Engine")) );
			HTTPState = HTTP_Closed;
			return;
		}
		else if( Ready == 0 )
		{
			if( appSeconds() - ConnectStartTime > 30 )
			{
				debugf( NAME_DevNet, TEXT("HTTPDownload: connection timed out") );
				DownloadError( LocalizeError(TEXT("ConnectionFailed"),TEXT("Engine")) );
				HTTPState = HTTP_Closed;
			}
			return;
		}

		// Connected: send the HTTP request.
		FString Request = FString::Printf
		(
			TEXT("GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: Unreal\r\nConnection: close\r\n\r\n"),
			(*(*ProxyServerHost)) ? *DownloadURL.String() : *(FString(TEXT("/")) + DownloadURL.Map),
			(*(*ProxyServerHost)) ? *ProxyServerHost      : *DownloadURL.Host
		);
		send( ServerSocket, appToAnsi(*Request), Request.Len(), 0 );
		HTTPState = HTTP_ReceivingHeader;
		break;
	}
	case HTTP_ReceivingHeader:
	{
		FetchData();
		for( INT i=0; i<ReceivedData.Num(); i++ )
		{
			if( ReceivedData(i)==13 || ReceivedData(i)==10 )
			{
				UBOOL EOH = i==0;
				ReceivedData(i) = 0;
				Headers(Headers.AddZeroed()) = appFromAnsi( (ANSICHAR*)&ReceivedData(0) );
				if( i<ReceivedData.Num()-1 && (ReceivedData(i+1)==13 || ReceivedData(i+1)==10) )
					i++;
				ReceivedData.Remove( 0, i+1 );
				if( EOH )
				{
					// Check we got a 200 OK.
					if( Headers(0).Mid(Headers(0).InStr(TEXT(" ")),5) != TEXT(" 200 ") )
					{
						DownloadError( *FString::Printf( LocalizeError(TEXT("InvalidUrl"),TEXT("Engine")), *DownloadURL.String() ) );
						HTTPState = HTTP_Closed;
						return;
					}
					// A compressed file is not the size the package map knows, so 436 only checks
					// Content-Length against FileSize for the uncompressed case.
					if( !IsCompressed )
					{
						for( i=0; i<Headers.Num(); i++ )
						{
							if( Headers(i).Caps().Left(16) == TEXT("CONTENT-LENGTH: ") )
							{
								if( appAtoi( *Headers(i).Mid(16) ) != Info->FileSize )
								{
									DownloadError( LocalizeError(TEXT("NetSize"),TEXT("Engine")) );
									HTTPState = HTTP_Closed;
									return;
								}
							}
						}
					}
					HTTPState = HTTP_ReceivingData;
					break;
				}
				FetchData();
				i = -1;
			}
		}
		break;
	}
	case HTTP_ReceivingData:
	{
		while( ReceivedData.Num() || FetchData() )
		{
			if( ReceivedData.Num() > 0 )
			{
				INT Count = Transfered + ReceivedData.Num() > Info->FileSize ? Info->FileSize - Transfered : ReceivedData.Num();
				if( Count > 0 )
					ReceiveData( &ReceivedData(0), Count );
				else
					HTTPState = HTTP_Closed;
				ReceivedData.Empty();
			}
		}
		break;
	}
	case HTTP_Closed:
	{
		DownloadDone();
		return;
	}
	}
	unguard;
}

UBOOL UHTTPDownload::FetchData()
{
	guard(UHTTPDownload::FetchData);
	BYTE Buf[1024];
	INT Bytes = recv( ServerSocket, (char*)Buf, 1024, 0 );
	if( Bytes == 0 )
	{
		// Close received.
		HTTPState = HTTP_Closed;
		return 0;
	}
	if( Bytes != SOCKET_ERROR )
	{
		appMemcpy( &ReceivedData(ReceivedData.Add(Bytes)), Buf, Bytes );
		return 1;
	}
	if( WSAGetLastError() == WSAEWOULDBLOCK )
		return 0;	// No data.

	debugf( NAME_DevNet, TEXT("HTTPDownload: recv() failed") );
	DownloadError( LocalizeError(TEXT("ConnectionFailed"),TEXT("Engine")) );
	HTTPState = HTTP_Closed;
	return 0;
	unguard;
}

UBOOL UHTTPDownload::TrySkipFile()
{
	guard(UHTTPDownload::TrySkipFile);
	if( Super::TrySkipFile() )
	{
		HTTPState = HTTP_Closed;
		Connection->Logf( TEXT("SKIP GUID=%s"), Info->Guid.String() );
		return 1;
	}
	return 0;
	unguard;
}

IMPLEMENT_CLASS(UHTTPDownload);

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
