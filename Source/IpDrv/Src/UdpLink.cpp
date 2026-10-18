/*=============================================================================
	IpDrv.cpp: Unreal TCP/IP driver.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Tim Sweeney.
	* Additions by Brandon Reinhart.
=============================================================================*/

#include "IpDrvPrivate.h"
// The synthetic LAN (WebRTCDrv). Header only: the call is resolved at the final --whole-archive link, so
// IpDrv does not link WebRTCDrv.
#include "WebRTCLan.h"

/*-----------------------------------------------------------------------------
	AUdpLink implementation.
-----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(AUdpLink);

#define MAXRECVDATASIZE 4096

//
// Constructor.
//
AUdpLink::AUdpLink()
{
	guard(AUdpLink::AUdpLink);
	unguard;
}

//
// Destroy.
//
void AUdpLink::Destroy()
{
	guard(AUdpLink::Destroy);
#ifdef __EMSCRIPTEN__
	WebRTCLan_Unbind( this );   // drop any queued synthetic packets for this link
#endif
	if( HasSocket() )
		closesocket(GetSocket());
	Super::Destroy();
	unguard;
}

//
// BindPort: Binds a free port or optional port specified in argument one.
//
void AUdpLink::execBindPort( FFrame& Stack, RESULT_DECL )
{
	guard(AUdpLink::execBindPort);
	P_GET_INT_OPTX(InPort,0);
	P_GET_UBOOL_OPTX(bUseNextAvailable,0);
	P_FINISH;
#ifdef __EMSCRIPTEN__
	// A browser has no UDP: the synthetic LAN answers the bind (the port is reported as taken and the link
	// registered), and SendText/Tick are served from WebRTCLan. It replaces the bind outright, because the stock
	// callers stop on a zero return (UBrowserLocalLink, ClientBeaconReceiver).
	{
		static INT GNextSyntheticPort = 40000;
		const INT Bound = InPort ? InPort : GNextSyntheticPort++;
		Port = Bound;
		WebRTCLan_Bind( this, Bound );
		*(INT*)Result = Bound;
		return;
	}
#endif
	if( GInitialized )
	{
		if( GetSocket()==INVALID_SOCKET )
		{
			Socket = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
			if( GetSocket() != INVALID_SOCKET )
			{
				UBOOL TrueBuffer=1;
#ifdef PLATFORM_DREAMCAST
				if( true )
#else
				if( setsockopt( GetSocket(), SOL_SOCKET, SO_BROADCAST, (char*)&TrueBuffer, sizeof(TrueBuffer) )==0 )
#endif
				{
					sockaddr_in Addr;
					Addr.sin_family      = AF_INET;
					Addr.sin_addr        = getlocalbindaddr( Stack );
					Addr.sin_port        = htons(InPort);
					INT boundport = bindnextport( Socket, &Addr, bUseNextAvailable ? 20 : 1, 1 );
					if( boundport )
					{
						// WinSock has no fcntl: switch on the socket API, not the compiler.
						#if __BSD_SOCKETS__
						INT pd_flags;
						pd_flags = fcntl( Socket, F_GETFL, 0 );
						pd_flags |= O_NONBLOCK;
						if( fcntl( Socket, F_SETFL, pd_flags ) == 0 )
						#else
						DWORD NoBlock = 1;
						if( ioctlsocket( Socket, FIONBIO, &NoBlock )==0 )
						#endif
						{
							// Success.
							*(INT*)Result = boundport;
							Port = ntohs( Addr.sin_port );
							return;
						}
						else Stack.Logf( TEXT("BindPort: ioctlsocket failed") );
					}
					else Stack.Logf( TEXT("BindPort: bind failed") );
				}
				else Stack.Logf( TEXT("BindPort: setsockopt failed") );
			}
			else Stack.Logf( TEXT("BindPort: socket failed") );
			if( HasSocket() )
				closesocket(GetSocket());
			GetSocket()=INVALID_SOCKET;
		}
		else Stack.Logf( TEXT("BindPort: already bound") );
	}
	else Stack.Logf( TEXT("BindPort: winsock failed") );
	*(INT*)Result = 0;
	unguard;
}

//
// Send text in a UDP packet.
//
void AUdpLink::execSendText( FFrame& Stack, RESULT_DECL )
{
	guard(AUdpLink::execSendText);
	P_GET_STRUCT(FIpAddr,IpAddr);
	P_GET_STR(Str);
	P_FINISH;
#ifdef __EMSCRIPTEN__
	// Hand the datagram to the synthetic LAN: it answers the beacon sweep and the GameSpy \info\ query and
	// swallows anything else (there is no socket to send on).
	WebRTCLan_Send( this, IpAddr.Addr, IpAddr.Port, *Str );
	*(DWORD*)Result = 1;
	return;
#endif
	if( HasSocket() )
	{
		sockaddr_in Addr;
		Addr.sin_family      = AF_INET;
		Addr.sin_port        = htons(IpAddr.Port);
		Addr.sin_addr.s_addr = htonl(IpAddr.Addr);
		INT SentBytes = sendto( Socket, (char*)appToAnsi(*Str), sizeof(ANSICHAR)*Str.Len(), 0, (sockaddr*)&Addr, sizeof(Addr) );
		if ( SentBytes == 0 )
		{
			Stack.Logf( TEXT("SentText: sendto failed") );
			*(DWORD*)Result = 0;
			return;
		}
		else
		{
			//debugf("Sent %i bytes.", SentBytes);
		}
	}
	*(DWORD*)Result = 1;
	unguard;
}

//
// Send binary data.
//
void AUdpLink::execSendBinary( FFrame& Stack, RESULT_DECL )
{
	guard(AUdpLink::execSendBinary);
	P_GET_STRUCT(FIpAddr,IpAddr);
	P_GET_INT(Count);
	P_GET_ARRAY_REF(BYTE,B);
	P_FINISH;
#ifdef __EMSCRIPTEN__
	// No UDP in a browser; the synthetic LAN is text-only (both stock browsers query in text mode).
	*(DWORD*)Result = 0;
	return;
#endif
	if( HasSocket() )
	{
		sockaddr_in Addr;
		Addr.sin_family      = AF_INET;
		Addr.sin_port        = htons(IpAddr.Port);
		Addr.sin_addr.s_addr = htonl(IpAddr.Addr);
		if( sendto( Socket, (char*)B, Count, 0, (sockaddr*)&Addr, sizeof(Addr) )==0 )
		{
			Stack.Logf( TEXT("SendBinary: sendto failed") );
			*(DWORD*)Result = 1;
			return;
		}
	}
	*(DWORD*)Result = 0;
	unguard;
}

//
// Time passes...
//
UBOOL AUdpLink::Tick( FLOAT DeltaTime, enum ELevelTick TickType )
{
	guard(AUdpLink::Tick);
	UBOOL Result = Super::Tick( DeltaTime, TickType );
#ifdef __EMSCRIPTEN__
	// Deliver synthetic LAN packets through the stock receive path, so the browser's ReceivedText handlers,
	// list building, ping timing and sorting run unmodified. Outside the socket guard below: on the web there
	// is no socket (see execBindPort).
	{
		FIpAddr From;
		FString Text;
		DWORD Addr = 0;
		INT   Port = 0;
		while( WebRTCLan_NextReply( this, &Addr, &Port, Text ) )
		{
			From.Addr = Addr;
			From.Port = Port;
			if( LinkMode == MODE_Line )
				eventReceivedLine( From, Text );
			else if( LinkMode == MODE_Binary )
				eventReceivedBinary( From, Text.Len(), (BYTE*)appToAnsi(*Text) );
			else
				eventReceivedText( From, Text );
		}
	}
	// RMODE_Manual (ReadText/ReadBinary polling) is not fed: nothing in either stock browser uses it.
	return Result;
#endif
	if( HasSocket() )
	{
		if( ReceiveMode == RMODE_Event )
		{
			BYTE Buffer[MAXRECVDATASIZE];
			sockaddr_in FromAddr;
			socklen_t FromSize = sizeof(FromAddr);
			INT Count = recvfrom( GetSocket(), (char*)Buffer, ARRAY_COUNT(Buffer)-1, 0, (sockaddr*)&FromAddr, &FromSize );
			if( Count!=SOCKET_ERROR )
			{
				FIpAddr Addr;
				Addr.Addr = ntohl( FromAddr.sin_addr.s_addr );
				Addr.Port = ntohs( FromAddr.sin_port );
				if( LinkMode == MODE_Text )
				{
					Buffer[Count]=0;
					eventReceivedText( Addr, appFromAnsi((ANSICHAR*)Buffer) );
				}
				else if ( LinkMode == MODE_Line )
				{
					Buffer[Count]=0;
					eventReceivedLine( Addr, appFromAnsi((ANSICHAR*)Buffer) );
				}
				else if( LinkMode == MODE_Binary )
				{
					eventReceivedBinary( Addr, Count, (BYTE*)Buffer );
				}
			}
		}
		else if( ReceiveMode == RMODE_Manual )
		{
			fd_set SocketSet;
			TIMEVAL SelectTime = {0, 0};
			INT Error;

			FD_ZERO( &SocketSet );
			FD_SET( Socket, &SocketSet );
			Error = select( Socket + 1, &SocketSet, 0, 0, &SelectTime);
			if( Error==0 || Error==SOCKET_ERROR )
			{
				DataPending = 0;
			}
			else
			{
				DataPending = 1;
			}
		}
	}

	return Result;
	unguard;
}

//
// Read text.
//
void AUdpLink::execReadText( FFrame& Stack, RESULT_DECL )
{
	guard(AUdpLink::execReadText);
	P_GET_STRUCT_REF( FIpAddr, Addr );
	P_GET_STR_REF( Str );
	P_FINISH;
	*Str = TEXT("");
	if( HasSocket() )
	{
		BYTE Buffer[MAXRECVDATASIZE];
		sockaddr_in FromAddr;
		socklen_t FromSize = sizeof(FromAddr);
		INT BytesReceived = recvfrom( (SOCKET)Socket, (char*)Buffer, sizeof(Buffer), 0, (sockaddr*)&FromAddr, &FromSize );
		if( BytesReceived != SOCKET_ERROR )
		{
			Addr->Addr = ntohl( FromAddr.sin_addr.s_addr );
			Addr->Port = ntohs( FromAddr.sin_port );
			*Str = appFromAnsi((ANSICHAR*)Buffer);
			*(DWORD*)Result = BytesReceived;
		}
		else
		{
			*(DWORD*) Result = 0;
			if ( WSAGetLastError() != WSAEWOULDBLOCK )
				debugf( NAME_Log, TEXT("ReadText: Error reading text.") );
			return;
		}
		return;
	}
	*(DWORD*)Result = 0;

	unguardexec;
}

//
// Read Binary.
//
void AUdpLink::execReadBinary( FFrame& Stack, RESULT_DECL )
{
	guard(AUdpLink::execReadBinary);
	P_GET_STRUCT_REF(FIpAddr, Addr);
	P_GET_INT(Count);
	P_GET_ARRAY_REF(BYTE,B);
	P_FINISH;
	if( HasSocket() )
	{
		sockaddr_in FromAddr;
		socklen_t FromSize = sizeof(FromAddr);
		INT BytesReceived = recvfrom( (SOCKET) Socket, (char*)B, Min<INT>(Count,255), 0, (sockaddr*)&FromAddr, &FromSize );
		if( BytesReceived != SOCKET_ERROR )
		{
			Addr->Addr = ntohl( FromAddr.sin_addr.s_addr );
			Addr->Port = ntohs( FromAddr.sin_port );
			*(DWORD*) Result = BytesReceived;
		}
		else
		{
			*(DWORD*)Result = 0;
			if( WSAGetLastError() != WSAEWOULDBLOCK )
				debugf( NAME_Log, TEXT("ReadBinary: Error reading text.") );
			return;
		}
		return;
	}
	*(DWORD*)Result = 0;

	unguardexec;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
