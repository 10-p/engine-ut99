/*=============================================================================
	WebRTCLan.cpp — make the STOCK UE1 server browser work over WebRTC.

	★ THE PROBLEM (ufront 2.38). The owner reported that the server browser had
	never worked on any UE1 target, and that we had assumed UT2004's networking
	work covered them. It did not: this engine had no discovery bridge at all —
	`webrtc_discover_servers` and friends are exported by the baked glue shim and
	nothing in the UE1 engines had ever called them.

	★ WHY THIS IS A UDP SIMULATOR AND NOT A BROWSER PATCH. The browser is script,
	and the script packages are FROZEN retail `.u` files we cannot recompile. So the
	fix has to meet the stock code where it already is, and where it is is UDP:

	  Unreal 1  — `UnrealListenMenu` spawns `IpDrv.ClientBeaconReceiver`, which binds
	              the beacon port and LISTENS. Servers broadcast
	              "<Product> <address> <text>"; the menu lists the <text> and travels
	              to the <address>. The client never sends anything.
	  UT99/436  — `UBrowser.UBrowserLocalLink` broadcasts "REPORTQUERY" to
	              ServerBeaconPort+0..9 and expects "<Product> <QueryPort>" back; it
	              takes the SOURCE ADDRESS of that reply as the server's IP. Then
	              `UBrowserServerPing` (also a UdpLink) sends "\info\" to that
	              address and parses "\key\value\" pairs.

	Both bottom out in `AUdpLink`, so ONE hook — send + poll — serves every UE1
	target, and the browsers, list boxes, sorting, ping columns and join buttons all
	keep working untouched.

	★ HOW A JOIN GETS BACK TO A ROOM. We hand out synthetic addresses in 10.199.0.0/16
	and keep a stable index -> room table. Both browsers put that address in the travel
	URL, `FURL` parses a dotted token as a HOST (UnURL.cpp), and
	`UWebRTCNetDriver::InitConnect` asks us to translate it back. So the room name
	survives a round trip through code that only understands IPv4.

	Layering: this file lives in WebRTCDrv and uses NO IpDrv types; IpDrv calls in
	through the plain-C-shaped declarations in WebRTCLan.h. One-way, link-time, no
	module cycle — the same arrangement engine-ut2004 uses for WebRTCDiscovery.cpp.
=============================================================================*/

#include "Engine.h"
#include "UnNet.h"
#include "WebRTCLan.h"

#ifdef __EMSCRIPTEN__

// Discovery bridge (webrtc-net.js --js-library exports; resolved at WASM link).
extern "C"
{
	void webrtc_discover_servers( const char* room );
	int  webrtc_get_server_count();
	int  webrtc_is_discovery_done();
	int  webrtc_get_server_info( int index, char* name, int nameMax, char* joinKey, int joinKeyMax );
	int  webrtc_get_server_map( int index, char* buf, int maxLen );
	int  webrtc_get_server_gametype( int index, char* buf, int maxLen );
	int  webrtc_get_server_players( int index );
	int  webrtc_get_server_maxplayers( int index );
	int  webrtc_get_server_ping( int index );
}

/*-----------------------------------------------------------------------------
	Tunables + state.
-----------------------------------------------------------------------------*/

// 10.199.0.0/16 — private, and far away from anything a real LAN hands out. The low 16 bits are the
// index into GRooms, which is why the table must be append-only (see RoomIndex).
#define LAN_ADDR_BASE     0x0AC70000u
#define LAN_ADDR_MASK     0xFFFF0000u
// The "query port" we advertise. UT99's browser sends its \info\ there; nothing binds it for real.
#define LAN_QUERY_PORT    7778
// The game port that goes in the row / the join URL. Cosmetic — the room is what actually routes.
#define LAN_GAME_PORT     7777
// Re-run discovery at most this often; the browser re-queries on a timer and on REFRESH.
#define LAN_DISCOVERY_MIN_INTERVAL  3.0
// Unreal 1's ClientBeaconReceiver ages entries out after BeaconTimeout (5 s default), so a listening
// client needs a fresh beacon well inside that or rows blink out.
#define LAN_BEACON_REPEAT_INTERVAL  2.0

struct FLanServer
{
	FString Room, Name, Map, GameType;
	INT     Players, MaxPlayers, Ping;
	FLanServer() : Players(0), MaxPlayers(0), Ping(0) {}
};

// Append-only: an index handed out in a synthetic address must keep meaning the same room for as long
// as the session lives, because the player may join minutes after the row was drawn.
static TArray<FString>    GRooms;
static TArray<FLanServer> GServers;
static UBOOL              GDiscoveryPending = 0;
static DOUBLE             GLastDiscoveryAt  = 0.0;
// Set by the net driver when WE are the listen server, so we never advertise ourselves to ourselves.
static FString            GLocalRoom;
// Set from a context where proxying is unsafe (see WebRTCLan_Bind); acted on from the tick.
static UBOOL              GWantKick = 0;

// A link that wants synthetic traffic. `BeaconPort` is set for a passive Unreal 1 listener.
struct FLanLink
{
	const void* Id;
	INT     BoundPort;
	UBOOL   WantsBeacons;      // Unreal 1: emit unsolicited beacons on a timer
	DOUBLE  NextBeaconAt;
	UBOOL   bLoggedFirstPoll;
	// ★ A sweep this link asked for that we could not answer yet. See WebRTCLan_Send.
	UBOOL   bSweepPending;
	DOUBLE  SweepDeadline;
	DOUBLE  LastPendingLog;
};
static TArray<FLanLink> GLinks;

struct FLanReply
{
	const void* LinkId;
	DWORD   Addr;
	INT     Port;
	FString Text;
	FLanReply() : LinkId(NULL), Addr(0), Port(0) {}
	FLanReply( const void* InLinkId, DWORD InAddr, INT InPort, const FString& InText )
	: LinkId(InLinkId), Addr(InAddr), Port(InPort), Text(InText) {}
};
static TArray<FLanReply> GReplies;

/*-----------------------------------------------------------------------------
	Helpers.
-----------------------------------------------------------------------------*/

static FLanLink* FindLink( const void* Id )
{
	for( INT i=0; i<GLinks.Num(); i++ )
		if( GLinks(i).Id == Id )
			return &GLinks(i);
	return NULL;
}

static FLanLink& TouchLink( const void* Id )
{
	FLanLink* L = FindLink( Id );
	if( L )
		return *L;
	FLanLink& N = GLinks( GLinks.AddZeroed() );
	N.Id = Id;
	return N;
}

/**
 * Stable index for a room. Append-only — see GRooms.
 *
 * ⚠ `new( Array )T( Item )`, NOT `Array.AddItem( Item )`. See the note above PumpDiscovery: UE1's
 * `AddItem` ASSIGNS into an uninitialised slot, and assigning an FString reallocs whatever garbage
 * pointer was sitting there.
 */
static INT RoomIndex( const FString& Room )
{
	for( INT i=0; i<GRooms.Num(); i++ )
		if( appStricmp( *GRooms(i), *Room )==0 )
			return i;
	new( GRooms )FString( Room );
	return GRooms.Num() - 1;
}

static DWORD AddrForIndex( INT Index )
{
	return LAN_ADDR_BASE | (DWORD)(Index & 0xFFFF);
}

static UBOOL IsSyntheticAddr( DWORD Addr )
{
	return (Addr & LAN_ADDR_MASK) == LAN_ADDR_BASE;
}

/** The beacon product string the stock script will compare against. Read the ini, never guess: Unreal 1
 *  compares with `==` (CASE-SENSITIVE), so a hardcoded "unreal" would silently match nothing. */
static FString BeaconProduct()
{
	FString P;
	if( GConfig->GetString( TEXT("UBrowser.UBrowserLocalFact"), TEXT("BeaconProduct"), P ) && P.Len() )
		return P;
	if( GConfig->GetString( TEXT("IpDrv.UdpBeacon"), TEXT("BeaconProduct"), P ) && P.Len() )
		return P;
	return FString(TEXT("unreal"));
}

/*-----------------------------------------------------------------------------
	Discovery.
-----------------------------------------------------------------------------*/

static void KickDiscovery()
{
	DOUBLE Now = appSeconds();
	if( GDiscoveryPending || Now - GLastDiscoveryAt < LAN_DISCOVERY_MIN_INTERVAL )
		return;
	GLastDiscoveryAt  = Now;
	GDiscoveryPending = 1;
	webrtc_discover_servers( "" );   // "" = every room on this signalling server
	debugf( NAME_DevNet, TEXT("[WebRTC-LAN] discovery started") );
}

/**
 * Drain a finished discovery into GServers. No-op while one is still in flight.
 *
 * ★★ THROTTLED, AND THE THROTTLE IS NOT AN OPTIMISATION — IT IS THE DIFFERENCE BETWEEN RUNNING AND
 * ABORTING. Every webrtc_* call here is `__proxy: 'sync'`: a CROSS-THREAD round trip from the engine
 * pthread to the main thread. This function is reached from AUdpLink::Tick, i.e. once per link per
 * FRAME, and while a sweep was outstanding it asked "is discovery done?" every single one of them.
 *
 * ⚠ Under load that overflows Emscripten's proxy queue and the module dies:
 *     Aborted(Assertion failed: false && "emscripten_proxy_async failed",
 *             at: system/lib/html5/callback.c,42,_emscripten_run_callback_on_thread)
 * — measured 2.38. It survived a standalone run and aborted inside a suite, which made it look like a
 * dead tick, a stale artifact and an empty server list in turn. Ask four times a second instead.
 */
static DOUBLE GLastPumpAt = 0.0;

static void PumpDiscovery()
{
	if( !GDiscoveryPending )
		return;
	const DOUBLE PumpNow = appSeconds();
	if( PumpNow - GLastPumpAt < 0.25 )
		return;
	GLastPumpAt = PumpNow;
	if( !webrtc_is_discovery_done() )
		return;
	GDiscoveryPending = 0;

	// ⚠⚠ COPY-CONSTRUCTED IN PLACE (`new( Array )T( Item )`) — AND NOTHING ELSE WORKS.
	//
	// ★ THIS FILE HAS NOW BEEN BITTEN TWICE BY THE SAME UE1 FACT, ONCE PER FLAVOUR, SO IT IS WRITTEN OUT
	// IN FULL. `TArray<T>` IS A POD CONTAINER. It has no idea T might own memory, and both of its obvious
	// "add an element" spellings hand you a slot that is not a T yet:
	//
	//     INT AddItem( const T& Item ) { INT Index=Add(); (*this)(Index)=Item; }   // UnTemplate.h
	//     INT FArray::Add( INT n, INT Size ) { ... Realloc(Size); return Index; }  // NO zeroing, NO ctor
	//
	// so `AddItem` ASSIGNS into raw memory, and FString's assignment operator is:
	//
	//     FString& operator=( const FString& Other ) { ArrayNum=ArrayMax=Other.Num(); Realloc(...); }
	//
	// i.e. `appRealloc( Data, … )` on whatever bit pattern the allocator happened to leave in that slot.
	// `FLanServer` holds four FStrings, so one AddItem is four reallocs of four garbage pointers.
	//
	// ★★ AND IT IS HEAP LUCK, WHICH IS WHY IT READ AS FLAKINESS. Fresh dlmalloc pages come back zeroed, and
	// a zeroed FString is a valid empty one — so on a quiet heap this "works". Measured 2.38: ut99 died on
	// it 3 runs out of 3 (`memory access out of bounds`, and inside a suite a CRASH-PANEL), while unreal1 —
	// same source, lighter heap — drained 4 for 4 and looked perfectly healthy. The tell is in the log:
	// the per-server line below prints and "discovery done" never does, because the module is gone in
	// between. Downstream that is a browser window with no rows in it, then a page that stops painting.
	//
	// ⚠ The FIRST version of this comment claimed "a local + AddItem is safe: the copy constructor runs".
	// It does not run. There is no construction anywhere in AddItem. Placement new is the only spelling
	// that copy-constructs, and it is what `TArray::operator=` itself uses.
	//
	// (Removal and Empty() are fine as they are: TTypeInfo<T>::NeedsDestructor() defaults to 1, so TArray
	// does destruct elements it drops — it just never constructs the ones it adds.)
	GServers.Empty();
	const INT Count = webrtc_get_server_count();
	char nameBuf[256], roomBuf[256], mapBuf[128], gtBuf[128];
	for( INT i=0; i<Count; i++ )
	{
		appMemzero( nameBuf, sizeof(nameBuf) );
		appMemzero( roomBuf, sizeof(roomBuf) );
		appMemzero( mapBuf,  sizeof(mapBuf)  );
		appMemzero( gtBuf,   sizeof(gtBuf)   );
		if( !webrtc_get_server_info( i, nameBuf, sizeof(nameBuf), roomBuf, sizeof(roomBuf) ) )
			continue;
		FString Room = ANSI_TO_TCHAR(roomBuf);
		// Skip our own listen server: a host browsing its own LAN list should not see itself, and
		// joining it would be a peer connecting to itself.
		if( GLocalRoom.Len() && appStricmp( *Room, *GLocalRoom )==0 )
			continue;
		webrtc_get_server_map( i, mapBuf, sizeof(mapBuf) );
		webrtc_get_server_gametype( i, gtBuf, sizeof(gtBuf) );

		FLanServer S;
		S.Room       = Room;
		S.Name       = ANSI_TO_TCHAR(nameBuf);
		S.Map        = mapBuf[0] ? FString(ANSI_TO_TCHAR(mapBuf)) : FString(TEXT("?"));
		S.GameType   = ANSI_TO_TCHAR(gtBuf);
		S.Players    = webrtc_get_server_players( i );
		S.MaxPlayers = webrtc_get_server_maxplayers( i );
		S.Ping       = webrtc_get_server_ping( i );
		if( S.MaxPlayers <= 0 ) S.MaxPlayers = 16;
		if( S.Ping      <= 0 )  S.Ping = 1;
		if( !S.Name.Len() )     S.Name = Room;
		const FString Host = WebRTCLan_HostForRoom( *S.Room );   // also reserves the room's index
		debugf( NAME_DevNet, TEXT("[WebRTC-LAN] '%s' room=%s map=%s %i/%i %ims -> %s"),
			*S.Name, *S.Room, *S.Map, S.Players, S.MaxPlayers, S.Ping, *Host );
		new( GServers )FLanServer( S );
	}
	debugf( NAME_DevNet, TEXT("[WebRTC-LAN] discovery done: %i server(s)"), GServers.Num() );
}

/*-----------------------------------------------------------------------------
	Reply builders.
-----------------------------------------------------------------------------*/

// ⚠ Placement new, like every other append in this file — see the note above PumpDiscovery. (AddZeroed +
// assign would in fact survive here, because a zeroed FString is a valid empty one, but "survives because
// zero happens to be a legal value" is exactly the reasoning that cost this file two crashes.)
static void Queue( const void* LinkId, DWORD Addr, INT Port, const FString& Text )
{
	new( GReplies )FLanReply( LinkId, Addr, Port, Text );
}

/** Unreal 1: an unsolicited server beacon. "<Product> <address> <what the menu shows>". */
static void QueueBeacons( const void* LinkId )
{
	const FString Product = BeaconProduct();
	for( INT i=0; i<GServers.Num(); i++ )
	{
		const FLanServer& S = GServers(i);
		const DWORD Addr = AddrForIndex( RoomIndex( S.Room ) );
		// The menu splits on the FIRST space: everything before it is the join address, the rest is the
		// row label. So the label must not be empty, and the address must not contain a space.
		Queue( LinkId, Addr, LAN_GAME_PORT,
			FString::Printf( TEXT("%s %s %s (%s) %i/%i"),
				*Product, *WebRTCLan_HostForRoom( *S.Room ), *S.Name, *S.Map, S.Players, S.MaxPlayers ) );
	}
}

/** UT99: the reply to REPORTQUERY — "<Product> <QueryPort>", from the server's synthetic address. */
static void QueueBeaconQueries( const void* LinkId )
{
	const FString Product = BeaconProduct();
	for( INT i=0; i<GServers.Num(); i++ )
	{
		const DWORD Addr = AddrForIndex( RoomIndex( GServers(i).Room ) );
		Queue( LinkId, Addr, LAN_QUERY_PORT, FString::Printf( TEXT("%s %i"), *Product, LAN_QUERY_PORT ) );
	}
}

/** UT99: the GameSpy-style \key\value\ answer UBrowserServerPing parses (see its GetInfo state). */
static void QueueServerInfo( const void* LinkId, DWORD Addr )
{
	const INT Index = (INT)(Addr & 0xFFFF);
	if( Index < 0 || Index >= GRooms.Num() )
		return;
	const FString Room = GRooms( Index );
	for( INT i=0; i<GServers.Num(); i++ )
	{
		const FLanServer& S = GServers(i);
		if( appStricmp( *S.Room, *Room )!=0 )
			continue;
		// `maptitle` doubles as the row's display name, `gamever` keeps the row out of the
		// version-mismatch filter, and `worldlog\false` keeps the ngWorldStats column honest.
		Queue( LinkId, Addr, LAN_QUERY_PORT, FString::Printf(
			TEXT("\\hostname\\%s\\hostport\\%i\\mapname\\%s\\maptitle\\%s\\gametype\\%s")
			TEXT("\\numplayers\\%i\\maxplayers\\%i\\gamemode\\openplaying\\gamever\\%i")
			TEXT("\\minnetver\\%i\\worldlog\\false\\wantworldlog\\false\\final\\"),
			*S.Name, LAN_GAME_PORT, *S.Map, *S.Map, *S.GameType,
			S.Players, S.MaxPlayers, ENGINE_VERSION, ENGINE_MIN_NET_VERSION ) );
		return;
	}
}

/*-----------------------------------------------------------------------------
	The IpDrv-facing hooks.
-----------------------------------------------------------------------------*/

void WebRTCLan_Bind( const void* LinkId, INT Port )
{
	FLanLink& L = TouchLink( LinkId );
	L.BoundPort = Port;

	// ★ WARM THE CACHE ON ANY BIND, not just the beacon port — but ⚠ NOT FROM HERE.
	//
	// The stock browser's link is SHORT-LIVED — it binds, broadcasts its sweep once, and is destroyed a
	// few seconds later. Discovery is asynchronous (a peer-list XHR plus a status handshake per server),
	// so a sweep issued microseconds after the bind routinely outlives its own answer, and the list shows
	// nothing at all. Kicking here buys those few seconds and, more importantly, leaves GServers populated
	// for the NEXT query — which is why a REFRESH always works even when the first look did not.
	//
	// ⚠⚠ DEFERRED, NOT CALLED. BindPort can be reached from INSIDE a script event that our own delivery
	// triggered (a beacon reply -> FoundServer -> spawn UBrowserServerPing -> BindPort), i.e. from inside
	// the engine's tick, inside script, inside an Asyncify-unwound stack. Every webrtc_* bridge is a
	// SYNC CROSS-THREAD call, and making one from there hangs the engine — the drain completes, the log
	// stops mid-flow, and nothing ever ticks again. So record the wish and grant it from the tick's own
	// context (WebRTCLan_NextReply), which is a safe place to proxy from.
	GWantKick = 1;

	// Unreal 1's ClientBeaconReceiver is PASSIVE: it binds the beacon port and waits for servers to
	// shout. Nothing will ever arrive to trigger us, so binding that port IS the trigger.
	INT BeaconPort = 7776;
	GConfig->GetInt( TEXT("IpDrv.UdpBeacon"), TEXT("BeaconPort"), BeaconPort );
	if( Port == BeaconPort )
	{
		L.WantsBeacons = 1;
		L.NextBeaconAt = 0.0;
		KickDiscovery();
		debugf( NAME_DevNet, TEXT("[WebRTC-LAN] beacon listener bound on port %i"), Port );
	}
}

void WebRTCLan_Unbind( const void* LinkId )
{
	for( INT i=GLinks.Num()-1; i>=0; i-- )
		if( GLinks(i).Id == LinkId )
			GLinks.Remove( i );
	for( INT i=GReplies.Num()-1; i>=0; i-- )
		if( GReplies(i).LinkId == LinkId )
			GReplies.Remove( i );
}

UBOOL WebRTCLan_Send( const void* LinkId, DWORD DestAddr, INT DestPort, const TCHAR* Text )
{
	if( !Text )
		return 0;
	TouchLink( LinkId );

	// UT99's LAN sweep. It fires ten of these (ServerBeaconPort+0..9).
	//
	// ★★ THE ANSWER USUALLY CANNOT BE IMMEDIATE, AND PRETENDING OTHERWISE LOSES THE LIST.
	// Discovery is a peer-list XHR plus a status handshake per server — hundreds of milliseconds at best.
	// The stock browser broadcasts ONCE when its window opens and then waits; it does not ask again until
	// the player hits REFRESH. So answering synchronously with whatever is cached means an empty list
	// whenever the window opens before discovery has finished, which is most of the time.
	//
	// ⚠ Measured, and it cost several suite runs to see: standalone the cache happened to be warm and the
	// list filled; inside a suite the guest reached its menu in 20 s, opened the browser first, and the
	// list stayed empty — with the window demonstrably open and discovery demonstrably succeeding a
	// second later. It read as "the server browser is broken".
	//
	// So the sweep is remembered and answered when the answer exists. That is also what a real LAN sweep
	// looks like from the client's side: replies trickle in after the broadcast, not with it.
	if( appStrnicmp( Text, TEXT("REPORTQUERY"), 11 )==0 )
	{
		KickDiscovery();
		FLanLink& L = TouchLink( LinkId );
		L.WantsBeacons  = 0;
		L.bSweepPending = 1;
		L.SweepDeadline = appSeconds() + 30.0;   // stop re-kicking eventually; the browser has REFRESH
		PumpDiscovery();
		if( GServers.Num() > 0 )
		{
			QueueBeaconQueries( LinkId );
			L.bSweepPending = 0;
		}
		return 1;
	}
	// "REPORT" (the plain beacon request) — same answer shape as an unsolicited beacon.
	if( appStrnicmp( Text, TEXT("REPORT"), 6 )==0 )
	{
		KickDiscovery();
		PumpDiscovery();
		QueueBeacons( LinkId );
		return 1;
	}
	// A GameSpy query aimed at one of our synthetic servers.
	if( IsSyntheticAddr( DestAddr ) && Text[0]=='\\' )
	{
		PumpDiscovery();
		QueueServerInfo( LinkId, DestAddr );
		return 1;
	}
	// Anything else addressed at a synthetic server is still ours to swallow: there is no socket that
	// could carry it, and letting sendto() fail would only produce a misleading script warning.
	if( IsSyntheticAddr( DestAddr ) )
		return 1;

	return 0;
}

UBOOL WebRTCLan_NextReply( const void* LinkId, DWORD* OutAddr, INT* OutPort, FString& OutText )
{
	// Grant any deferred discovery kick here: this is tick context, which is safe to proxy from.
	if( GWantKick )
	{
		GWantKick = 0;
		KickDiscovery();
	}
	// One line per link, the first time it polls. Whether the stock link TICKS AT ALL is the load-bearing
	// assumption of this whole file — if it does not, nothing can ever be delivered — so it is worth
	// knowing from the log rather than by inference.
	{
		FLanLink* L0 = FindLink( LinkId );
		if( L0 && !L0->bLoggedFirstPoll )
		{
			L0->bLoggedFirstPoll = 1;
			debugf( NAME_DevNet, TEXT("[WebRTC-LAN] link %p is polling (port %i)"), LinkId, L0->BoundPort );
		}
	}
	PumpDiscovery();

	// Passive Unreal 1 listeners get a fresh beacon every couple of seconds, because the receiver ages
	// rows out on BeaconTimeout and a stale list looks exactly like a broken one.
	FLanLink* L = FindLink( LinkId );
	if( L && L->WantsBeacons )
	{
		DOUBLE Now = appSeconds();
		if( Now >= L->NextBeaconAt )
		{
			L->NextBeaconAt = Now + LAN_BEACON_REPEAT_INTERVAL;
			KickDiscovery();
			QueueBeacons( LinkId );
		}
	}

	// A sweep we owed an answer to: deliver it as soon as discovery has one, and keep asking until it
	// does (or until the deadline — after that the player's REFRESH is the retry).
	if( L && L->bSweepPending )
	{
		// ⚠ While a sweep is outstanding, say so every few seconds. "The link stopped ticking" and
		// "discovery found nothing" produce the same silence otherwise, and telling them apart by
		// inference has already been wrong once in both directions.
		DOUBLE Now2 = appSeconds();
		if( Now2 - L->LastPendingLog > 5.0 )
		{
			L->LastPendingLog = Now2;
			debugf( NAME_DevNet, TEXT("[WebRTC-LAN] sweep pending: discovery %s, %i server(s) known"),
				GDiscoveryPending ? TEXT("in flight") : TEXT("idle"), GServers.Num() );
		}
		if( GServers.Num() > 0 )
		{
			QueueBeaconQueries( LinkId );
			L->bSweepPending = 0;
			debugf( NAME_DevNet, TEXT("[WebRTC-LAN] answered a pending LAN sweep with %i server(s)"), GServers.Num() );
		}
		else if( appSeconds() > L->SweepDeadline )
		{
			L->bSweepPending = 0;
			debugf( NAME_DevNet, TEXT("[WebRTC-LAN] LAN sweep found nothing before its deadline") );
		}
		else
		{
			KickDiscovery();
		}
	}

	for( INT i=0; i<GReplies.Num(); i++ )
	{
		if( GReplies(i).LinkId != LinkId )
			continue;
		*OutAddr = GReplies(i).Addr;
		*OutPort = GReplies(i).Port;
		OutText  = GReplies(i).Text;
		GReplies.Remove( i );
		return 1;
	}
	return 0;
}

FString WebRTCLan_HostForRoom( const TCHAR* Room )
{
	const DWORD A = AddrForIndex( RoomIndex( FString(Room) ) );
	return FString::Printf( TEXT("%i.%i.%i.%i"), (A>>24)&0xFF, (A>>16)&0xFF, (A>>8)&0xFF, A&0xFF );
}

FString WebRTCLan_RoomForHost( const TCHAR* Host )
{
	if( !Host )
		return FString(TEXT(""));
	// Parse a.b.c.d without pulling in a socket header (this file must not depend on IpDrv).
	INT Oct[4] = {0,0,0,0}, N = 0;
	const TCHAR* P = Host;
	while( N < 4 )
	{
		if( *P < '0' || *P > '9' )
			return FString(TEXT(""));
		INT V = 0;
		while( *P >= '0' && *P <= '9' )
			V = V*10 + (*P++ - '0');
		if( V > 255 )
			return FString(TEXT(""));
		Oct[N++] = V;
		if( N < 4 )
		{
			if( *P != '.' )
				return FString(TEXT(""));
			P++;
		}
	}
	if( *P != 0 )
		return FString(TEXT(""));
	const DWORD Addr = ((DWORD)Oct[0]<<24) | ((DWORD)Oct[1]<<16) | ((DWORD)Oct[2]<<8) | (DWORD)Oct[3];
	if( !IsSyntheticAddr( Addr ) )
		return FString(TEXT(""));
	const INT Index = (INT)(Addr & 0xFFFF);
	if( Index < 0 || Index >= GRooms.Num() )
		return FString(TEXT(""));
	return GRooms( Index );
}

void WebRTCLan_SetLocalRoom( const TCHAR* Room )
{
	GLocalRoom = Room ? Room : TEXT("");
}

#else  // !__EMSCRIPTEN__

// Native builds have real UDP and a real LAN; the stock browser needs no help.
void    WebRTCLan_Bind( const void*, INT ) {}
void    WebRTCLan_Unbind( const void* ) {}
UBOOL   WebRTCLan_Send( const void*, DWORD, INT, const TCHAR* ) { return 0; }
UBOOL   WebRTCLan_NextReply( const void*, DWORD*, INT*, FString& ) { return 0; }
FString WebRTCLan_HostForRoom( const TCHAR* ) { return FString(TEXT("")); }
FString WebRTCLan_RoomForHost( const TCHAR* ) { return FString(TEXT("")); }
void    WebRTCLan_SetLocalRoom( const TCHAR* ) {}

#endif // __EMSCRIPTEN__

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
