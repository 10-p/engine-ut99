/*=============================================================================
	IpDrv.cpp: Unreal TCP/IP driver.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Tim Sweeney.
	* Additions by Brandon Reinhart.
=============================================================================*/

#include "IpDrvPrivate.h"
// ufront 2.38 — the synthetic LAN (engine-local WebRTCDrv). Header only: the call is resolved at the
// final --whole-archive link, so IpDrv does not link WebRTCDrv and there is no module cycle.
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
	WebRTCLan_Unbind( this );   // ufront 2.38 — drop any queued synthetic packets for this link
#endif
	if( GetSocket() )
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
	// ★ ufront 2.38 — a browser has no UDP, so there is no socket to bind and no point pretending
	// otherwise halfway. Bind is answered by the synthetic LAN instead: report the port as taken and
	// register the link, and everything downstream (SendText/Tick) is served from WebRTCLan.
	//
	// ⚠ This has to REPLACE the bind rather than fall back after it, because the stock callers treat a
	// zero return as fatal and stop: UBrowserLocalLink gives up with "Could not bind to a free port"
	// and Unreal 1's ClientBeaconReceiver logs "beacon port in use" and never starts its timer. Under
	// Emscripten's virtual socket layer the result is not even reliably one or the other.
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
						// ufront 2.33: WinSock has no fcntl — switch on the socket API, not the compiler.
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
			closesocket(GetSocket());
			GetSocket()=0;
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
	// ufront 2.38 — hand the datagram to the synthetic LAN. It answers the beacon sweep and the
	// GameSpy \info\ query; anything else is swallowed, because the alternative is a sendto() on a
	// socket that does not exist and a script warning that means nothing.
	WebRTCLan_Send( this, IpAddr.Addr, IpAddr.Port, *Str );
	*(DWORD*)Result = 1;
	return;
#endif
	if( GetSocket() )
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
	if( GetSocket() )
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
	// ufront 2.38 — deliver synthetic LAN packets through the STOCK receive path, so the browser's own
	// ReceivedText handlers, list building, ping timing and sorting all run unmodified. Outside the
	// GetSocket() guard below on purpose: on the web there is no socket at all (see execBindPort).
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
	if( GetSocket() )
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
	if( GetSocket() )
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
	if( GetSocket() )
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
