/*=============================================================================
	WebRTCDrv.cpp: WebRTC network driver (browser multiplayer, Emscripten only).

	IpDrv's UTcpNetDriver/UTcpipConnection shape with the socket transport replaced
	by the glue C ABI (vendor/glue/net-shim.js forwards it to the host's
	Module.UE.net): one UNetConnection per WebRTC data channel (connId).
=============================================================================*/

#include "Engine.h"
#include "UnNet.h"
#include "WebRTCLan.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include "ue_abi.h"	// the glue ABI: ue_net_* imports, UE_NET_STATE_* (vendor/glue)
#endif

// Package.
IMPLEMENT_PACKAGE(WebRTCDrv);

// The peer-ID namespace prefix; it must match the frontend's targets/ut99/target.json network.peerPrefix
// (the glue registers the listen peer as "<prefix>-<room>-srv").
#define WEBRTC_PEER_PREFIX      "ut99"

// Packet sizing (parity with UTcpipConnection's UDP model).
#define WEBRTC_MAX_PACKET       (512)
#define WEBRTC_PACKET_OVERHEAD  (28)      // ~UDP header, for the rate limiter
#define WEBRTC_RECV_BUFFER      (4096)

// The running engine, for the listen-server status push (set in Launch.cpp after InitEngine).
extern UEngine* GEngineForExec;

// TCHAR -> a caller-owned ANSI buffer. appToAnsi() hands back one shared static string, so anything that
// needs two converted strings alive at the same time (the listen-status push needs three) must copy.
static void ToAnsiBuf( const TCHAR* Src, ANSICHAR* Dst, INT Max )
{
	const ANSICHAR* A = Src ? appToAnsi( Src ) : NULL;
	INT i = 0;
	if( A )
		for( ; i < Max-1 && A[i]; i++ )
			Dst[i] = A[i];
	Dst[i] = 0;
}

/*-----------------------------------------------------------------------------
	UWebRTCConnection — a UNetConnection over one WebRTC data channel (by connId).
-----------------------------------------------------------------------------*/
class DLL_EXPORT_CLASS UWebRTCConnection : public UNetConnection
{
	DECLARE_CLASS(UWebRTCConnection,UNetConnection,CLASS_Config|CLASS_Transient)
	NO_DEFAULT_CONSTRUCTOR(UWebRTCConnection)

	// Variables.
	INT		WebRTCConnId;
	UBOOL	OpenedLocally;

	// Constructor.
	UWebRTCConnection( INT InWebRTCConnId, UNetDriver* InDriver, EConnectionState InState, UBOOL InOpenedLocally, const FURL& InURL )
	:	UNetConnection	( InDriver, InURL )
	,	WebRTCConnId	( InWebRTCConnId )
	,	OpenedLocally	( InOpenedLocally )
	{
		guard(UWebRTCConnection::UWebRTCConnection);
		State          = InState;
		MaxPacket      = WEBRTC_MAX_PACKET;
		PacketOverhead = WEBRTC_PACKET_OVERHEAD;
		InitOut();
		unguard;
	}
	void Destroy()
	{
		guard(UWebRTCConnection::Destroy);
		if( Channels[0] )
		{
			Channels[0]->Close();
			FlushNet();
		}
		if( WebRTCConnId >= 0 )
			ue_net_close( WebRTCConnId );
		Super::Destroy();
		unguard;
	}

	// UNetConnection interface (v400: send one packet via LowLevelSend; base packs OutData -> here).
	void LowLevelSend( void* Data, INT Count )
	{
		guard(UWebRTCConnection::LowLevelSend);
		// connId 0 is valid: it is the client's own outbound pipe (server-side accepted connections start at 1).
		if( WebRTCConnId >= 0 && Count > 0 )
			ue_net_send( WebRTCConnId, Data, Count );
		unguard;
	}
	FString LowLevelGetRemoteAddress()
	{
		guard(UWebRTCConnection::LowLevelGetRemoteAddress);
		return FString::Printf( TEXT("webrtc:%i"), WebRTCConnId );
		unguard;
	}
	FString LowLevelDescribe()
	{
		guard(UWebRTCConnection::LowLevelDescribe);
		return FString::Printf
		(
			TEXT("webrtc:%i state: %s"),
			WebRTCConnId,
				State==USOCK_Pending	?	TEXT("Pending")
			:	State==USOCK_Open		?	TEXT("Open")
			:	State==USOCK_Closed		?	TEXT("Closed")
			:								TEXT("Invalid")
		);
		unguard;
	}
};
IMPLEMENT_CLASS(UWebRTCConnection);

/*-----------------------------------------------------------------------------
	UWebRTCNetDriver.
-----------------------------------------------------------------------------*/
class UWebRTCNetDriver;

// The client has exactly one transport slot: the glue hands every client connect conn id 0, so one client
// driver owns the wire at a time and a second InitConnect takes the slot over. Server-side ids are per peer.
static UWebRTCNetDriver* GClientDriver = NULL;

class DLL_EXPORT_CLASS UWebRTCNetDriver : public UNetDriver
{
	DECLARE_CLASS(UWebRTCNetDriver,UNetDriver,CLASS_Transient|CLASS_Config)

	// Variables.
	FString	RoomName;
	FString	ServerName;      // ?ServerName= for browser discovery
	UBOOL	bIsListenServer;
	// Set when a newer client driver has taken the client transport slot.
	UBOOL	bDetached;
	// Pipes the level said to ignore (not refuse), re-offered on a later tick. See TickDispatch().
	TArray<INT>	ParkedAccepts;

	// Constructor.
	UWebRTCNetDriver()
	{}

	// Long timeouts: the WebRTC handshake (signaling + ICE) and map downloads take far longer than a UDP connect,
	// and LastReceiveTime goes stale while a package streams. ICE monitors the real link underneath.
	void SetWebRTCTimeouts()
	{
		ConnectionTimeout     = 300.f;
		InitialConnectTimeout = 600.f;
	}

	// UNetDriver interface.
	UBOOL InitConnect( FNetworkNotify* InNotify, FURL& ConnectURL, FString& Error )
	{
		guard(UWebRTCNetDriver::InitConnect);
		if( !Super::InitConnect( InNotify, ConnectURL, Error ) )
			return 0;
		SetWebRTCTimeouts();

		// Client. ConnectURL.Host carries the room (HostID); the server registered as "<prefix>-<room>-srv".
		bIsListenServer = 0;
		RoomName = ConnectURL.Host;
		// A join from the server browser arrives as a dotted address (the stock browsers carry only an IPv4
		// string to the travel URL); the synthetic LAN handed that address out, so it hands the room back. An
		// address that is not one of ours translates to "" and is left alone.
		{
			FString FromBrowser = WebRTCLan_RoomForHost( *RoomName );
			if( FromBrowser.Len() )
			{
				debugf( NAME_DevNet, TEXT("[WebRTC] InitConnect: browser address %s -> room '%s'"),
					*RoomName, *FromBrowser );
				RoomName = FromBrowser;
			}
		}
		if( RoomName.Len() <= 0 )
			RoomName = TEXT("default");
		FString ServerPeerId = FString::Printf( TEXT("%s-%s-srv"), TEXT(WEBRTC_PEER_PREFIX), *RoomName );
		debugf( NAME_DevNet, TEXT("[WebRTC] InitConnect: room='%s' -> peer '%s'"), *RoomName, *ServerPeerId );
		// Take the client slot: a server map change calls InitConnect while the old level still ticks and
		// flushes, and the two drivers would otherwise share glue conn 0.
		bDetached = 0;
		if( GClientDriver && GClientDriver!=this && GClientDriver->GetServerConnection() )
		{
			debugf( NAME_DevNet, TEXT("[WebRTC] InitConnect: detaching the previous client connection (one transport slot)") );
			// Detached, not closed: USOCK_Closed would read as "connection lost" and abort the travel.
			GClientDriver->GetServerConnection()->WebRTCConnId = -1;
			// …and its LowLevelDestroy leaves the transport, which is now this driver's, alone.
			GClientDriver->bDetached = 1;
		}
		GClientDriver = this;
		ue_net_connect( appToAnsi(*ServerPeerId) );

		// Pending server connection + control channel (channel 0) — like UTcpNetDriver::InitConnect.
		ServerConnection = new UWebRTCConnection( 0, this, USOCK_Pending, 1, ConnectURL );
		GetServerConnection()->CreateChannel( CHTYPE_Control, 1, 0 );

		return 1;
		unguard;
	}
	UBOOL InitListen( FNetworkNotify* InNotify, FURL& ListenURL, FString& Error )
	{
		guard(UWebRTCNetDriver::InitListen);
		if( !Super::InitListen( InNotify, ListenURL, Error ) )
			return 0;
		SetWebRTCTimeouts();

		// Listen server. Room + display name come from the travel URL options.
		bIsListenServer = 1;
		RoomName   = ListenURL.GetOption( TEXT("room="), TEXT("default") );
		ServerName = ListenURL.GetOption( TEXT("ServerName="), TEXT("") );

		// Without ?ServerName= (the in-game LISTEN button sends none), take [Engine.GameReplicationInfo] ServerName
		// from the config: a room holds several servers and the transport addresses one as
		// `<room>_<slug of the server name>`. The GameReplicationInfo object does not exist yet (the level is not up).
		if( !ServerName.Len() )
			GConfig->GetString( TEXT("Engine.GameReplicationInfo"), TEXT("ServerName"), ServerName );

		debugf( NAME_DevNet, TEXT("[WebRTC] InitListen: room='%s' name='%s'"), *RoomName, *ServerName );
		// The synthetic LAN skips this room, so a host browsing its own server list does not find itself.
		WebRTCLan_SetLocalRoom( *RoomName );
		// `room/serverName` — the transport slugs the name and builds `<prefix>-<room>_<slug>-srv`. The
		// separator is a `/` because a server name may contain anything, including the `_` the peer id uses.
		FString ListenKey = ServerName.Len() ? (RoomName + TEXT("/") + ServerName) : RoomName;
		ue_net_init_listen( appToAnsi(*ListenKey) );

		return 1;
		unguard;
	}
	void TickDispatch( FLOAT DeltaTime )
	{
		guard(UWebRTCNetDriver::TickDispatch);
		Super::TickDispatch( DeltaTime );

		BYTE Data[WEBRTC_RECV_BUFFER];

		// Server: accept newly-opened peer connections.
		//
		// ACCEPTC_Ignore is not a refusal: ULevel::NotifyAcceptingConnection returns it while a server travel is
		// pending, and on UDP the client simply retries. Closing a pipe would make the client's glue reconnect in a
		// loop, so the pipe is parked and re-offered on a later tick, parked ids before new arrivals.
		TArray<INT> Incoming = ParkedAccepts;
		ParkedAccepts.Empty();
		INT NewConnId;
		while( (NewConnId = ue_net_accept_connection()) > 0 )
			Incoming.AddItem( NewConnId );

		for( INT i=0; i<Incoming.Num(); i++ )
		{
			INT ConnId = Incoming(i);
			// The client may have given up (or the page closed) while we were ignoring it.
			if( ue_net_get_connection_state( ConnId ) == UE_NET_STATE_CLOSED )
				continue;
			EAcceptConnection Acc = Notify->NotifyAcceptingConnection();
			if( Acc == ACCEPTC_Ignore )
			{
				ParkedAccepts.AddItem( ConnId );
				continue;
			}
			if( Acc != ACCEPTC_Accept )
			{
				ue_net_close( ConnId );
				continue;
			}
			UWebRTCConnection* Conn = new UWebRTCConnection( ConnId, this, USOCK_Open, 0, FURL() );
			Conn->URL.Host = FString::Printf( TEXT("webrtc:%i"), ConnId );
			Notify->NotifyAcceptedConnection( Conn );
			ClientConnections.AddItem( Conn );
			debugf( NAME_DevNet, TEXT("[WebRTC] Accepted connection connId=%i (clients=%i)"), ConnId, ClientConnections.Num() );
		}

		// Poll receive on the client's server connection + all server-side client connections.
		// A detached connection (WebRTCConnId < 0) no longer owns the slot: the glue would report its old connId
		// CLOSED and the departing level would read "connection lost".
		if( GetServerConnection() && GetServerConnection()->WebRTCConnId >= 0 )
		{
			UWebRTCConnection* SC = GetServerConnection();
			if( ue_net_get_connection_state( SC->WebRTCConnId ) == UE_NET_STATE_CLOSED && SC->State != USOCK_Closed )
			{
				debugf( NAME_DevNet, TEXT("[WebRTC] Server connection transport closed") );
				SC->State = USOCK_Closed;
			}
			if( SC->State == USOCK_Open || SC->State == USOCK_Pending )
			{
				INT Size;
				while( (Size = ue_net_recv( SC->WebRTCConnId, Data, sizeof(Data) )) > 0 )
					SC->ReceivedRawPacket( Data, Size );
			}
		}
		for( INT i=0; i<ClientConnections.Num(); i++ )
		{
			UWebRTCConnection* Conn = (UWebRTCConnection*)ClientConnections(i);
			// Log a peer's transport closing, so the engine's own reaction is visible.
			if( ue_net_get_connection_state( Conn->WebRTCConnId ) == UE_NET_STATE_CLOSED && Conn->State != USOCK_Closed )
			{
				debugf( NAME_DevNet, TEXT("[WebRTC] TickDispatch: Client connId=%i connection CLOSED by remote"), Conn->WebRTCConnId );
				Conn->State = USOCK_Closed;
			}
			if( Conn->State == USOCK_Open || Conn->State == USOCK_Pending )
			{
				INT Size;
				while( (Size = ue_net_recv( Conn->WebRTCConnId, Data, sizeof(Data) )) > 0 )
					Conn->ReceivedRawPacket( Data, Size );
			}
		}

#ifdef __EMSCRIPTEN__
		// Listen server: publish current status for browser discovery (~1s throttle).
		//
		// The synthetic LAN feeds these fields into the stock beacon and \info\ replies. The player count is
		// the transport's (the client connections plus the listen host), not the GameInfo's.
		if( bIsListenServer )
		{
			static DOUBLE LastPush = 0.0;
			DOUBLE Now = appSeconds();
			if( Now - LastPush > 1.0 )
			{
				LastPush = Now;
				FString SName = ServerName, MapName = TEXT("?"), GType = TEXT("");
				INT Players = 1 + ClientConnections.Num();   // the listen host counts
				INT MaxPl   = 16;
				UGameEngine* GE = Cast<UGameEngine>( GEngineForExec );
				if( GE && GE->GLevel )
				{
					if( GE->GLevel->URL.Map.Len() )
						MapName = GE->GLevel->URL.Map;
					ALevelInfo* LI = GE->GLevel->GetLevelInfo();
					AGameInfo* GI = LI ? LI->Game : NULL;
					if( GI )
					{
						if( GI->MaxPlayers > 0 )
							MaxPl = GI->MaxPlayers;
						// GameName is the human label ("DeathMatch"); the class name is the fallback so the
						// browser row is never blank.
						if( GI->GameName.Len() )
							GType = GI->GameName;
						else
							GType = FString( GI->GetClass()->GetName() );
						if( !SName.Len() && GI->GameReplicationInfo )
							SName = GI->GameReplicationInfo->ServerName;
					}
				}
				if( !SName.Len() )
					SName = TEXT("UT Server");
				// appToAnsi() returns one shared static buffer: copy each string into its own buffer.
				ANSICHAR NameBuf[128], MapBuf[128], GTypeBuf[128];
				ToAnsiBuf( *SName, NameBuf, ARRAY_COUNT(NameBuf) );
				ToAnsiBuf( *MapName, MapBuf, ARRAY_COUNT(MapBuf) );
				ToAnsiBuf( *GType, GTypeBuf, ARRAY_COUNT(GTypeBuf) );
				ue_net_set_listen_status( NameBuf, MapBuf, Players, MaxPl, GTypeBuf );
			}
		}

		// Yield to the browser event loop while the WebRTC handshake completes (signaling and ICE need several
		// event-loop turns); never during the data phase, so asset streaming is not throttled.
		if( bIsListenServer && ClientConnections.Num()==0 )
			emscripten_sleep( 50 );
		else if( GetServerConnection() && ue_net_get_connection_state( GetServerConnection()->WebRTCConnId ) != UE_NET_STATE_OPEN )
			emscripten_sleep( 50 );
#endif

		unguard;
	}
	void LowLevelDestroy()
	{
		guard(UWebRTCNetDriver::LowLevelDestroy);
		debugf( NAME_Exit, TEXT("[WebRTC] LowLevelDestroy: room='%s'%s"), *RoomName, bDetached ? TEXT(" (detached — transport left alone)") : TEXT("") );
		// The parked pipes are about to be closed wholesale by ue_net_destroy(); just drop our list.
		ParkedAccepts.Empty();
		if( GClientDriver==this )
			GClientDriver = NULL;
		// A departing driver must not destroy the arriving driver's transport: ue_net_destroy() is global, and on a
		// client the pending level's InitConnect runs before the old driver is destroyed inside LoadMap. (On a host
		// the old driver dies before InitListen.)
		if( !bDetached )
			ue_net_destroy();
		unguard;
	}
	FString LowLevelGetNetworkNumber()
	{
		guard(UWebRTCNetDriver::LowLevelGetNetworkNumber);
		return FString::Printf( TEXT("webrtc:%s"), *RoomName );
		unguard;
	}

	UWebRTCConnection* GetServerConnection() { return (UWebRTCConnection*)ServerConnection; }
};
IMPLEMENT_CLASS(UWebRTCNetDriver);

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
