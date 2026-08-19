/*=============================================================================
	WebRTCDrv.cpp: WebRTC network driver for UT99 (Emscripten/browser multiplayer).
	ufront step 2.7d.

	Engine-LOCAL (engines are independent — no shared code). Modeled on this repo's
	IpDrv `UTcpNetDriver`/`UTcpipConnection` (the v400 UNetDriver/UNetConnection
	reference) with the socket transport replaced by the glue `webrtc_*` C-ABI
	(satisfied by the baked vendor/glue/webrtc-shim.js -> the host-injected
	Module.UE.net). Ported from engine-unreal1's WebRTCDrv (UE1 v200) and adapted to
	UT99 v400: v400 splits Init into InitConnect/InitListen, ticks via TickDispatch,
	sends via the connection's LowLevelSend(Data,Count) (base packs OutData), and
	receives via the driver calling Connection->ReceivedRawPacket(Data,Size); the
	client list is ClientConnections (not v200's Connections).
=============================================================================*/

#include "Engine.h"
#include "UnNet.h"
#include "WebRTCLan.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// Package.
IMPLEMENT_PACKAGE(WebRTCDrv);

// WebRTC connection state (matches the glue ABI + webrtc_get_connection_state).
#define WEBRTC_STATE_INVALID    0
#define WEBRTC_STATE_CONNECTING 1
#define WEBRTC_STATE_OPEN       2
#define WEBRTC_STATE_CLOSED     3

// UT99's peer-ID namespace prefix. MUST match targets/ut99/target.json network.peerPrefix (the glue
// registers the listen peer as "<prefix>-<room>-srv"). Engine-specific by design — engine-ut99 only ever
// hosts UnrealTournament, exactly as engine-ut2004 hardcodes "ut2k4" (DATA-not-code governs the generic
// frontend/glue, not the per-game engine repos).
#define WEBRTC_PEER_PREFIX      "ut99"

// Packet sizing (parity with UTcpipConnection's UDP model).
#define WEBRTC_MAX_PACKET       (512)
#define WEBRTC_PACKET_OVERHEAD  (28)      // ~UDP header, for the rate limiter
#define WEBRTC_RECV_BUFFER      (4096)

/*-----------------------------------------------------------------------------
	extern "C" transport bridge (WASM <-> JS).
	On Emscripten these resolve to the baked vendor/glue/webrtc-shim.js, which
	forwards to Module.UE.net.*. On native they are inline no-op stubs.
-----------------------------------------------------------------------------*/
#ifdef __EMSCRIPTEN__
extern "C"
{
	void webrtc_init_listen( const char* roomName );
	void webrtc_connect( const char* peerId );
	int  webrtc_send( int connId, const void* data, int len );
	int  webrtc_recv( int connId, void* buffer, int maxLen );
	int  webrtc_accept_connection();
	void webrtc_close( int connId );
	void webrtc_destroy();
	int  webrtc_get_connection_state( int connId );
	void webrtc_set_listen_status( const char* name, const char* map, int players, int maxPlayers, const char* gametype );
}
#else
inline void webrtc_init_listen( const char* ) {}
inline void webrtc_connect( const char* ) {}
inline int  webrtc_send( int, const void*, int ) { return 0; }
inline int  webrtc_recv( int, void*, int ) { return 0; }
inline int  webrtc_accept_connection() { return 0; }
inline void webrtc_close( int ) {}
inline void webrtc_destroy() {}
inline int  webrtc_get_connection_state( int ) { return WEBRTC_STATE_INVALID; }
inline void webrtc_set_listen_status( const char*, const char*, int, int, const char* ) {}
#endif

// Engine pointer for the listen-server status push (set in Launch.cpp after InitEngine).
extern UEngine* GWebRTCEngine;

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
			webrtc_close( WebRTCConnId );
		Super::Destroy();
		unguard;
	}

	// UNetConnection interface (v400: send one packet via LowLevelSend; base packs OutData -> here).
	void LowLevelSend( void* Data, INT Count )
	{
		guard(UWebRTCConnection::LowLevelSend);
		// connId 0 is VALID — it's the client's own outbound pipe (the server-side accepted connections start
		// at 1). Guard on Count only (like unreal1's FlushNet guards on OutNum). A `> 0` test here silently
		// muted every client->server packet (the client's Hello never left), so the server never WELCOMEd and
		// the guest never joined — while server->client (connId >= 1) worked, giving a one-way link.
		if( WebRTCConnId >= 0 && Count > 0 )
			webrtc_send( WebRTCConnId, Data, Count );
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

// ★ THE CLIENT HAS EXACTLY ONE TRANSPORT SLOT. The glue hands every client connect the same conn id (0),
// so only one client driver can own the wire at a time. Tracked here so a second InitConnect can take the
// slot cleanly instead of silently sharing it. Server-side ids are per peer and unaffected.
static UWebRTCNetDriver* GClientDriver = NULL;

class DLL_EXPORT_CLASS UWebRTCNetDriver : public UNetDriver
{
	DECLARE_CLASS(UWebRTCNetDriver,UNetDriver,CLASS_Transient|CLASS_Config)

	// Variables.
	FString	RoomName;
	FString	ServerName;      // ?ServerName= for browser discovery
	UBOOL	bIsListenServer;
	// Set when a NEWER client driver has taken the one client transport slot from us.
	UBOOL	bDetached;
	// Pipes the level told us to IGNORE (not refuse) — re-offered on a later tick. See TickDispatch().
	TArray<INT>	ParkedAccepts;

	// Constructor.
	UWebRTCNetDriver()
	{}

	// Generous timeouts: the WebRTC handshake (PeerJS signaling + ICE) and Emscripten sync-XHR map downloads
	// take far longer than a UDP connect, and the engine's sleeps stop while a package streams, so
	// LastReceiveTime goes stale. Prevent an immediate disconnect on the first post-download tick (ICE
	// monitors the real link underneath). Mirrors engine-ut2004's WebRTC timeouts.
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
		// ★ ufront 2.38 — a join FROM THE SERVER BROWSER arrives as a dotted address, not a room: both
		// stock UE1 browsers can only carry an IPv4 string through to the travel URL. The synthetic LAN
		// handed that address out, so it can hand the room back. A real-looking address that is not one
		// of ours translates to "" and is left alone.
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
		// ★ TAKE THE ONE CLIENT SLOT. A server map change calls InitConnect again while the OLD level is
		// still ticking and flushing — the engine keeps you playing until the new level has loaded. On UDP
		// the two connections have different local ports and coexist; here they would share glue conn 0, so
		// the departing level's driver goes on writing into the NEW session.
		bDetached = 0;
		if( GClientDriver && GClientDriver!=this && GClientDriver->GetServerConnection() )
		{
			debugf( NAME_DevNet, TEXT("[WebRTC] InitConnect: detaching the previous client connection (one transport slot)") );
			// ⚠ DETACHED, NOT CLOSED — marking it USOCK_Closed reads as "connection lost" and aborts the travel.
			GClientDriver->GetServerConnection()->WebRTCConnId = -1;
			// …and its LowLevelDestroy must NOT call webrtc_destroy(): that transport is now OURS.
			GClientDriver->bDetached = 1;
		}
		GClientDriver = this;
		webrtc_connect( appToAnsi(*ServerPeerId) );

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
		debugf( NAME_DevNet, TEXT("[WebRTC] InitListen: room='%s' name='%s'"), *RoomName, *ServerName );
		// ufront 2.38 — tell the synthetic LAN which room is OURS, so a host browsing its own server
		// list does not find itself and try to connect a peer to itself.
		WebRTCLan_SetLocalRoom( *RoomName );
		webrtc_init_listen( appToAnsi(*RoomName) );

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
		// ★ ACCEPTC_Ignore IS NOT A REFUSAL. ULevel::NotifyAcceptingConnection (UnLevel.cpp) returns it
		// for every incoming connection while a server travel is pending (`NextURL` set) — i.e. for the
		// whole ServerTravelPause window. On UDP that means "drop this connect packet on the floor":
		// nothing is torn down, the client just keeps retrying and gets in once the new level listens.
		//
		// Our transport is a PIPE, and closing it is a loud, visible event: the client's glue sees the
		// data channel die, tears the peer down, reconnects, is ignored again, and the two livelock.
		// Measured on the sibling v200 engine during a coop travel: 306 connect/close rounds inside a
		// ~6s pause, after which the guest never rejoined at all.
		//
		// So park the pipe and re-offer it on a later tick, leaving the transport untouched — the
		// pipe-oriented equivalent of dropping the packet. ⚠ Parked ids are re-offered BEFORE new
		// arrivals, or a client that has been waiting loses its place to a newer one.
		TArray<INT> Incoming = ParkedAccepts;
		ParkedAccepts.Empty();
		INT NewConnId;
		while( (NewConnId = webrtc_accept_connection()) > 0 )
			Incoming.AddItem( NewConnId );

		for( INT i=0; i<Incoming.Num(); i++ )
		{
			INT ConnId = Incoming(i);
			// The client may have given up (or the page closed) while we were ignoring it.
			if( webrtc_get_connection_state( ConnId ) == WEBRTC_STATE_CLOSED )
				continue;
			EAcceptConnection Acc = Notify->NotifyAcceptingConnection();
			if( Acc == ACCEPTC_Ignore )
			{
				ParkedAccepts.AddItem( ConnId );
				continue;
			}
			if( Acc != ACCEPTC_Accept )
			{
				webrtc_close( ConnId );
				continue;
			}
			UWebRTCConnection* Conn = new UWebRTCConnection( ConnId, this, USOCK_Open, 0, FURL() );
			Conn->URL.Host = FString::Printf( TEXT("webrtc:%i"), ConnId );
			Notify->NotifyAcceptedConnection( Conn );
			ClientConnections.AddItem( Conn );
			debugf( NAME_DevNet, TEXT("[WebRTC] Accepted connection connId=%i (clients=%i)"), ConnId, ClientConnections.Num() );
		}

		// Poll receive on the client's server connection + all server-side client connections.
		// A DETACHED connection (WebRTCConnId < 0) no longer owns the slot — asking the glue about a connId
		// we gave away would report CLOSED and the departing level would be called "connection lost".
		if( GetServerConnection() && GetServerConnection()->WebRTCConnId >= 0 )
		{
			UWebRTCConnection* SC = GetServerConnection();
			if( webrtc_get_connection_state( SC->WebRTCConnId ) == WEBRTC_STATE_CLOSED && SC->State != USOCK_Closed )
			{
				debugf( NAME_DevNet, TEXT("[WebRTC] Server connection transport closed") );
				SC->State = USOCK_Closed;
			}
			if( SC->State == USOCK_Open || SC->State == USOCK_Pending )
			{
				INT Size;
				while( (Size = webrtc_recv( SC->WebRTCConnId, Data, sizeof(Data) )) > 0 )
					SC->ReceivedRawPacket( Data, Size );
			}
		}
		for( INT i=0; i<ClientConnections.Num(); i++ )
		{
			UWebRTCConnection* Conn = (UWebRTCConnection*)ClientConnections(i);
			// ★ ufront 2.38 — SAY SO. Dropping a peer used to be entirely silent on this engine (only
			// Unreal 1 v200 logs "Destroying … because connection closed"), so "the host noticed the
			// guest left" had no evidence anywhere in a log — the glue's reap line was the last word on
			// the subject and it only proves the TRANSPORT gave up, not that the engine acted. Matches
			// what engine-ut2004's driver already prints, so one milestone covers every target.
			if( webrtc_get_connection_state( Conn->WebRTCConnId ) == WEBRTC_STATE_CLOSED && Conn->State != USOCK_Closed )
			{
				debugf( NAME_DevNet, TEXT("[WebRTC] TickDispatch: Client connId=%i connection CLOSED by remote"), Conn->WebRTCConnId );
				Conn->State = USOCK_Closed;
			}
			if( Conn->State == USOCK_Open || Conn->State == USOCK_Pending )
			{
				INT Size;
				while( (Size = webrtc_recv( Conn->WebRTCConnId, Data, sizeof(Data) )) > 0 )
					Conn->ReceivedRawPacket( Data, Size );
			}
		}

#ifdef __EMSCRIPTEN__
		// Listen server: publish current status for browser discovery (~1s throttle).
		//
		// ★ ufront 2.38 — this used to push `(Name, RoomName, 0, 0, "")`: it advertised the ROOM as the
		// MAP and hard-coded zero players, with the original author's own comment admitting "Minimal for
		// now". Nothing ever noticed, because until 2.38 no UE1 client could see a server list at all —
		// the payload had no reader. Now it has one (the synthetic LAN responder feeds these fields
		// straight into the stock beacon + \status\ replies), so a wrong map here is a wrong map in the
		// player's server browser.
		//
		// ⚠ Player count is derived from the TRANSPORT, not from the GameInfo: UE1's native AGameInfo
		// does not export NumPlayers on every engine in this family (Unreal 1 v200 keeps it script-side),
		// and ClientConnections + the local listen player is both universally available and the number a
		// browser actually wants — how many people are in there right now.
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
				UGameEngine* GE = Cast<UGameEngine>( GWebRTCEngine );
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
				// ⚠ appToAnsi() returns ONE shared static buffer, so three calls in a single expression
				// would clobber each other the moment this engine is built UNICODE. Copy each into its
				// own frame buffer.
				ANSICHAR NameBuf[128], MapBuf[128], GTypeBuf[128];
				ToAnsiBuf( *SName, NameBuf, ARRAY_COUNT(NameBuf) );
				ToAnsiBuf( *MapName, MapBuf, ARRAY_COUNT(MapBuf) );
				ToAnsiBuf( *GType, GTypeBuf, ARRAY_COUNT(GTypeBuf) );
				webrtc_set_listen_status( NameBuf, MapBuf, Players, MaxPl, GTypeBuf );
			}
		}

		// Yield to the browser event loop while the WebRTC handshake completes (PeerJS signaling + ICE need
		// several event-loop turns). Only sleep while waiting to establish — never during the data phase, so
		// asset streaming isn't throttled.
		if( bIsListenServer && ClientConnections.Num()==0 )
			emscripten_sleep( 50 );
		else if( GetServerConnection() && webrtc_get_connection_state( GetServerConnection()->WebRTCConnId ) != WEBRTC_STATE_OPEN )
			emscripten_sleep( 50 );
#endif

		unguard;
	}
	void LowLevelDestroy()
	{
		guard(UWebRTCNetDriver::LowLevelDestroy);
		debugf( NAME_Exit, TEXT("[WebRTC] LowLevelDestroy: room='%s'%s"), *RoomName, bDetached ? TEXT(" (detached — transport left alone)") : TEXT("") );
		// The parked pipes are about to be closed wholesale by webrtc_destroy(); just drop our list.
		ParkedAccepts.Empty();
		if( GClientDriver==this )
			GClientDriver = NULL;
		// ★★ A DEPARTING DRIVER MUST NOT DESTROY THE ARRIVING DRIVER'S TRANSPORT. webrtc_destroy() is
		// GLOBAL, and on a CLIENT the two drivers overlap: InitConnect for the pending level runs FIRST and
		// the old level's driver is destroyed LATER, inside LoadMap. Measured on the sibling v200 engine:
		// the guest loaded the new map and then sat on "CONNECTING…" forever, and only a page reload
		// cleared it. The host is safe — there the old driver dies BEFORE InitListen.
		if( !bDetached )
			webrtc_destroy();
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
