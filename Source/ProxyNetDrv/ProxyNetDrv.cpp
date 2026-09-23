/*=============================================================================
	ProxyNetDrv.cpp: "Proxy networking" — a client-only UE1 net driver that reaches an
	ordinary NATIVE game server through a WSS<->UDP game proxy.  ufront step 2.79.

	Modelled function for function on IpDrv/Src/TcpNetDriver.cpp, deliberately: it takes
	exactly what the TcpIp driver takes — `open host:port`, the same URL forms, an `ip:port`
	remote address, and an ini section ([ProxyNetDrv.ProxyNetDriver]) with the same keys as
	[IpDrv.TcpNetDriver]. The only thing swapped is the socket: sendto()/recvfrom() become the
	`ue_proxynet_*` imports of the OPTIONAL proxynet ABI module (vendor/glue/ue_proxynet_abi.h,
	satisfied by the baked vendor/glue/proxynet-shim.js -> the host-injected Module.UE.proxynet).
	What TcpNetDriver does that a CLIENT never needs is simply absent: no listen socket, no
	accept path, no ICMP handling, no recently-closed peer list.

	DETACHED BY DECISION (ufrontend docs/12 ADR-16). This package shares nothing with WebRTCDrv
	and edits none of its files. It is selected like any other driver —
	    [Engine.Engine] NetworkDevice=ProxyNetDrv.ProxyNetDriver
	— and -DUT_WITH_PROXYNET=OFF removes it entirely.

	436-ONLY. Native servers run 436 and later, so this lives on feat/v436-compat and never on
	base main (the v400 baseline).

	CLIENT ONLY. A browser cannot accept UDP; hosting is what P2P networking is for.
=============================================================================*/

#include "Engine.h"
#include "UnNet.h"
#include "WebHttp.h"

#ifdef __EMSCRIPTEN__
#include "ue_proxynet_abi.h"
#include "WebLinks.h"
#include <stdlib.h>	// atoi (2.79o's list parser)
#include <string.h>	// strstr
#else
// A native build has real sockets and never links this package; these keep a stray build compiling.
#define PROXYNET_STATE_INVALID    0
#define PROXYNET_STATE_CONNECTING 1
#define PROXYNET_STATE_OPEN       2
#define PROXYNET_STATE_CLOSED     3
#define PROXYNET_HTTP_PENDING     0
#define PROXYNET_HTTP_RECEIVING   1
#define PROXYNET_HTTP_DONE        2
#define PROXYNET_HTTP_FAILED      3
inline int  ue_proxynet_connect( const char*, int ) { return -1; }
inline int  ue_proxynet_state( int ) { return PROXYNET_STATE_CLOSED; }
inline int  ue_proxynet_send( int, const void*, int ) { return 0; }
inline int  ue_proxynet_recv( int, void*, int ) { return 0; }
inline void ue_proxynet_close( int ) {}
inline int  ue_proxynet_http_open( int, const char* ) { return -1; }
inline int  ue_proxynet_http_poll( int, int*, int*, int* ) { return -1; }
inline int  ue_proxynet_http_read( int, void*, int ) { return 0; }
inline void ue_proxynet_http_close( int ) {}
inline int  ue_proxynet_list_open() { return -1; }
inline int  ue_proxynet_list_poll( int, int*, int* ) { return -1; }
inline int  ue_proxynet_list_read( int, void*, int ) { return 0; }
inline void ue_proxynet_list_close( int ) {}
#endif

// Package.
IMPLEMENT_PACKAGE(ProxyNetDrv);

// The same figures TcpNetDriver.cpp uses, because the far end IS a TcpNetDriver: the packet size the
// server will send, and the per-packet overhead the rate limiter must charge so NETSPEED means to us what
// it means to a native client.
#define PROXYNET_IP_HEADER_SIZE     (20)
#define PROXYNET_UDP_HEADER_SIZE    (PROXYNET_IP_HEADER_SIZE+8)
#define PROXYNET_SLIP_HEADER_SIZE   (PROXYNET_UDP_HEADER_SIZE+4)
#define PROXYNET_MAX_PACKET         (512)
#define PROXYNET_RECV_BUFFER        (576)

/*-----------------------------------------------------------------------------
	UProxyNetConnection — one connection to a native server, by the glue's connection id.
-----------------------------------------------------------------------------*/
class DLL_EXPORT_CLASS UProxyNetConnection : public UNetConnection
{
	DECLARE_CLASS(UProxyNetConnection,UNetConnection,CLASS_Config|CLASS_Transient)
	NO_DEFAULT_CONSTRUCTOR(UProxyNetConnection)

	// Variables.
	INT		ConnId;			// from ue_proxynet_connect(); -1 once closed
	FString	RemoteHost;		// as the player typed it — the PROXY resolves names, not the engine
	INT		RemotePort;

	// Constructor.
	UProxyNetConnection( INT InConnId, UNetDriver* InDriver, EConnectionState InState, const FURL& InURL )
	:	UNetConnection	( InDriver, InURL )
	,	ConnId			( InConnId )
	,	RemoteHost		( InURL.Host )
	,	RemotePort		( InURL.Port )
	{
		guard(UProxyNetConnection::UProxyNetConnection);
		// Init the connection — as UTcpipConnection does. No address resolution: the host string goes to
		// the game proxy as typed, and the PROXY resolves it (and refuses private address space).
		State          = InState;
		MaxPacket      = PROXYNET_MAX_PACKET;
		PacketOverhead = PROXYNET_SLIP_HEADER_SIZE;
		InitOut();
		unguard;
	}

	// Give the glue connection back (idempotent).
	void CloseTransport()
	{
		guard(UProxyNetConnection::CloseTransport);
		if( ConnId >= 0 )
		{
			ue_proxynet_close( ConnId );
			ConnId = -1;
		}
		unguard;
	}
	void Destroy()
	{
		guard(UProxyNetConnection::Destroy);
		// ★ THE CONNECTION OWNS ITS TRANSPORT, like a socket. ue_proxynet_connect() hands out a NEW id per
		// call, so at a server map change the departing level's connection and the arriving one are two ids,
		// exactly as they are two UDP sockets natively — closing this one cannot touch the other.
		CloseTransport();
		Super::Destroy();
		unguard;
	}

	// UNetConnection interface.
	void LowLevelSend( void* Data, INT Count )
	{
		guard(UProxyNetConnection::LowLevelSend);
		// Non-blocking, and allowed to drop: before the proxy says `ready` there is nowhere to send, and
		// under back-pressure a datagram transport loses packets. The engine's own reliability layer
		// retransmits what matters — which is also what carries the first HELLO to a server still loading.
		if( ConnId >= 0 && Count > 0 )
			ue_proxynet_send( ConnId, Data, Count );
		unguard;
	}
	FString LowLevelGetRemoteAddress()
	{
		guard(UProxyNetConnection::LowLevelGetRemoteAddress);
		return FString::Printf( TEXT("%s:%i"), *RemoteHost, RemotePort );
		unguard;
	}
	FString LowLevelDescribe()
	{
		guard(UProxyNetConnection::LowLevelDescribe);
		return FString::Printf
		(
			TEXT("%s:%i (proxy conn %i) state: %s"),
			*RemoteHost,
			RemotePort,
			ConnId,
				State==USOCK_Pending	?	TEXT("Pending")
			:	State==USOCK_Open		?	TEXT("Open")
			:	State==USOCK_Closed		?	TEXT("Closed")
			:								TEXT("Invalid")
		);
		unguard;
	}
};
IMPLEMENT_CLASS(UProxyNetConnection);

/*-----------------------------------------------------------------------------
	Redirect downloads.

	A native server names an HTTP redirect (`DLMGR CLASS=IpDrv.HTTPDownload`). In a browser
	IpDrv's UHTTPDownload cannot open a socket, so it asks the table in IpDrv/Inc/WebHttp.h —
	and this package, which owns the `ue_proxynet_http_*` imports, is what fills it. The game
	proxy does the fetch for a LIVE session and pays for it from that session's download
	allowance; any refusal is simply FAILED and the engine falls back to the in-band download.
-----------------------------------------------------------------------------*/

// The hook's status values ARE the ABI's: they cross unconverted.
static_assert( WEBHTTP_Pending==PROXYNET_HTTP_PENDING && WEBHTTP_Receiving==PROXYNET_HTTP_RECEIVING
	&& WEBHTTP_Done==PROXYNET_HTTP_DONE && WEBHTTP_Failed==PROXYNET_HTTP_FAILED, "WebHttp.h and ue_proxynet_abi.h disagree" );

static INT ProxyNetHttpOpen( UNetConnection* Connection, const ANSICHAR* Url )
{
	// Only for a download that belongs to one of OUR connections: its session is what the proxy
	// charges. Anything else (a P2P connection in the same build) gets -1 and falls back at once.
	UProxyNetConnection* Conn = Cast<UProxyNetConnection>( Connection );
	if( !Conn || Conn->ConnId < 0 )
		return -1;
	return ue_proxynet_http_open( Conn->ConnId, Url );
}
static INT ProxyNetHttpPoll( INT Handle, INT* Status, INT* Received, INT* Total )
{
	return ue_proxynet_http_poll( Handle, Status, Received, Total );
}
static INT ProxyNetHttpRead( INT Handle, BYTE* Buffer, INT MaxLen )
{
	return ue_proxynet_http_read( Handle, Buffer, MaxLen );
}
static void ProxyNetHttpClose( INT Handle )
{
	ue_proxynet_http_close( Handle );
}
static FWebHttpHooks GProxyNetHttpHooks = { ProxyNetHttpOpen, ProxyNetHttpPoll, ProxyNetHttpRead, ProxyNetHttpClose };

#ifdef __EMSCRIPTEN__
/*-----------------------------------------------------------------------------
	The stock server browser, from the game proxy's list (2.79o, IpDrv's WebLinks.h).

	The web build has no sockets, so UBrowser (UnrealScript, unchanged) can neither reach a GameSpy
	master nor ping a server. In Proxy mode the game proxy already holds the public list, fetched here
	through the four `ue_proxynet_list_*` imports, and both ends of both conversations are played from
	it: a TcpLink to a master's list port gets the challenge and then "\ip\a.b.c.d:queryport\...\final\";
	a UdpLink query to a listed server gets that server's "\info\", from the address that was asked
	(436's UBrowserServerPing checks the source), after the proxy's own round trip -- so the ping the
	browser shows is the player -> proxy leg: a floor, not the whole path.

	Registered when the program starts (the browser wants its list before any connection exists);
	Active() keeps it out of the way unless THIS launch is in Proxy mode.
-----------------------------------------------------------------------------*/

#define PROXYNET_GSPY_LIST_PORT 28900	// a GameSpy master's list port: every [UBrowserAll] factory's

static UBOOL ProxyNetLinksActive()
{
	// Proxy networking is this launch's mode exactly when the engine's network device is this driver
	// (Launch.cpp honours that key). Fixed for the launch -> cached.
	static INT Active = -1;
	if( Active < 0 && GConfig )
		Active = appStricmp( GConfig->GetStr( TEXT("Engine.Engine"), TEXT("NetworkDevice") ),
			TEXT("ProxyNetDrv.ProxyNetDriver") ) == 0;
	return Active == 1;
}

// The list: dotted-IPv4 rows only (a host NAME would be "resolved" by the web build to an address no
// ping can reach, and UBrowser drops an unanswered server).
struct FProxyRow
{
	DWORD   Addr;	// host order
	INT     Port;	// the game port; the query port is Port+1
	FString Info;	// this server's "\info\" answer, ready to send
};
static TArray<FProxyRow> GProxyRows;
static INT    GProxyListHandle = -1;
static INT    GProxyListState  = 0;	// 0 = not asked, 1 = asking, 2 = ready, 3 = not available
static DOUBLE GProxyListAt     = 0.0;
static DOUBLE GProxyNextPoll   = 0.0;
static INT    GProxyPingMs     = -1;	// the player -> proxy round trip; -1 = unknown
static UBOOL  GProxyPingsLogged = 0;	// one line per list: the first ping the list answered

static UBOOL ProxyParseIp( const ANSICHAR* S, DWORD& Out )
{
	DWORD A = 0;
	for( INT Part = 0; Part < 4; Part++ )
	{
		if( *S < '0' || *S > '9' )
			return 0;
		INT V = 0;
		while( *S >= '0' && *S <= '9' )
			if( (V = V*10 + (*S++ - '0')) > 255 )
				return 0;
		A = (A << 8) | V;
		if( Part < 3 && *S++ != '.' )
			return 0;
	}
	if( *S )
		return 0;
	Out = A;
	return 1;
}

// The list is UTF-8 (it is the page's own text); a GameSpy answer is 8-bit text. So each character is DECODED
// to its Latin-1 byte, in place (never longer), rather than copied byte by byte -- which would turn a name's
// "»" into two characters. A code point above 0xFF becomes '?', and so does a broken sequence.
static void ProxyUtf8ToLatin1( ANSICHAR* S )
{
	BYTE* In = (BYTE*)S;
	BYTE* Out = In;
	while( *In )
	{
		DWORD C = *In++;
		INT More = 0;
		if( C >= 0x80 )
		{
			if( C >= 0xC0 && C < 0xE0 )			{ C &= 0x1F; More = 1; }
			else if( C >= 0xE0 && C < 0xF0 )	{ C &= 0x0F; More = 2; }
			else if( C >= 0xF0 && C < 0xF8 )	{ C &= 0x07; More = 3; }
			else								C = '?';	// a stray continuation byte, or no UTF-8 lead at all
		}
		for( ; More > 0; More-- )
		{
			if( (*In & 0xC0) != 0x80 )
			{
				C = '?';	// cut short: the next byte starts a character of its own
				break;
			}
			C = (C << 6) | (*In++ & 0x3F);
		}
		*Out++ = (BYTE)(C == 0 || C > 0xFF ? '?' : C);
	}
	*Out = 0;
}

// "#proxynet-list 1 ping=<ms>", then host TAB port TAB name TAB map TAB gametype TAB players TAB
// maxplayers TAB version TAB minversion TAB password -- one row per line.
static void ProxyParseList( ANSICHAR* Text )
{
	GProxyRows.Empty();
	GProxyPingMs = -1;
	for( ANSICHAR* Line = Text; *Line; )
	{
		ANSICHAR* End = Line;
		while( *End && *End != '\n' )
			End++;
		UBOOL Last = (*End == 0);
		*End = 0;
		if( Line[0] == '#' )
		{
			const ANSICHAR* P = strstr( Line, "ping=" );
			if( P )
				GProxyPingMs = atoi( P + 5 );
		}
		else if( Line[0] )
		{
			ANSICHAR* F[10];
			INT N = 0;
			F[N++] = Line;
			for( ANSICHAR* C = Line; *C && N < 10; C++ )
				if( *C == '\t' )
				{
					*C = 0;
					F[N++] = C + 1;
				}
			DWORD Addr = 0;
			const INT Port = N == 10 ? atoi( F[1] ) : 0;
			if( Port > 0 && Port < 65535 && ProxyParseIp( F[0], Addr ) )
			{
				// A backslash would split a GameSpy field: somebody else's data, so it goes (after the
				// decode: only an ASCII byte can become one).
				for( INT f = 2; f <= 4; f++ )
					ProxyUtf8ToLatin1( F[f] );
				for( INT f = 2; f <= 4; f++ )
					for( ANSICHAR* C = F[f]; *C; C++ )
						if( *C == '\\' )
							*C = '/';
				// The version fields only when the list KNOWS them (the proxy asks each server its \info\, 2.79p).
				// Never a stand-in: our own 436 made EVERY server look compatible, and a 469-only one then turned
				// the player away at the door. With no gamever UBrowser's "compatible" tabs leave the row out --
				// as they do natively for a server that never answered its ping.
				const INT Version = atoi( F[7] ), MinVersion = atoi( F[8] );
				FString Versions;
				if( Version > 0 )
					Versions += FString::Printf( TEXT("\\gamever\\%i"), Version );
				if( MinVersion > 0 )
					Versions += FString::Printf( TEXT("\\minnetver\\%i"), MinVersion );
				INT i = GProxyRows.AddZeroed();
				GProxyRows(i).Addr = Addr;
				GProxyRows(i).Port = Port;
				GProxyRows(i).Info = FString::Printf(
					TEXT("\\hostname\\%s\\hostport\\%i\\maptitle\\%s\\mapname\\%s\\gametype\\%s\\numplayers\\%i\\maxplayers\\%i\\gamemode\\openplaying%s\\final\\"),
					F[2][0] ? appFromAnsi(F[2]) : appFromAnsi(F[0]), Port, appFromAnsi(F[3]), appFromAnsi(F[3]),
					appFromAnsi(F[4]), Max( atoi(F[5]), 0 ), Max( atoi(F[6]), 0 ), *Versions );
			}
		}
		if( Last )
			break;
		Line = End + 1;
	}
}

// 2 = the list is here, 3 = it cannot be had (no proxy chosen, or unreachable), 1 = still asking.
// At most four sync calls a second: a per-frame proxied import once overflowed Emscripten's proxy
// queue (WebRTCLan.cpp). Only ever called from a link's Tick, never from inside a script call.
static INT ProxyListPoll()
{
	if( GProxyListState == 2 && appSeconds() - GProxyListAt > 60.0 )
		GProxyListState = 0;	// a new query after a minute asks again (the proxy refreshes far less often)
	if( GProxyListState >= 2 )
		return GProxyListState;
	if( appSeconds() < GProxyNextPoll )
		return 1;
	GProxyNextPoll = appSeconds() + 0.25;
	if( GProxyListHandle < 0 )
	{
		GProxyListHandle = ue_proxynet_list_open();
		GProxyListState = GProxyListHandle >= 0 ? 1 : 3;
		if( GProxyListState == 3 )
			GLog->Logf( TEXT("ProxyNet: the in-game server browser's list from the game proxy: not available") );
		return GProxyListState;
	}
	INT Status = PROXYNET_HTTP_PENDING, Count = 0;
	if( ue_proxynet_list_poll( GProxyListHandle, &Status, &Count ) < 0 )
		Status = PROXYNET_HTTP_FAILED;
	if( Status == PROXYNET_HTTP_PENDING || Status == PROXYNET_HTTP_RECEIVING )
		return 1;
	UBOOL Ok = 0;
	if( Status == PROXYNET_HTTP_DONE )
	{
		const INT Need = ue_proxynet_list_read( GProxyListHandle, NULL, 0 );
		if( Need > 0 )
		{
			TArray<ANSICHAR> Blob;
			Blob.Add( Need + 1 );
			if( ue_proxynet_list_read( GProxyListHandle, &Blob(0), Need ) == Need )
			{
				Blob(Need) = 0;
				ProxyParseList( &Blob(0) );
				GProxyPingsLogged = 0;
				Ok = 1;
			}
		}
	}
	ue_proxynet_list_close( GProxyListHandle );
	GProxyListHandle = -1;
	GProxyListState  = Ok ? 2 : 3;
	GProxyListAt     = appSeconds();
	GLog->Logf( TEXT("ProxyNet: the in-game server browser's list from the game proxy: %s, %i servers"),
		Ok ? TEXT("ok") : TEXT("not available"), Ok ? GProxyRows.Num() : 0 );
	return GProxyListState;
}

// --- the master: TcpLink ---
struct FProxyMaster
{
	INT     Id;
	INT     Phase;		// 0 = connecting, 1 = challenge sent, 2 = list sent (hang up once read), 3 = gone
	UBOOL   WantList;
	FString Out;		// what the script has yet to receive
};
static TArray<FProxyMaster> GProxyMasters;
static INT GProxyNextMaster = 1;

static FProxyMaster* ProxyMasterFind( INT Id )
{
	for( INT i = 0; i < GProxyMasters.Num(); i++ )
		if( GProxyMasters(i).Id == Id )
			return &GProxyMasters(i);
	return NULL;
}
static INT ProxyNetTcpOpen( DWORD Addr, INT Port )
{
	if( Port != PROXYNET_GSPY_LIST_PORT )
		return -1;			// the news (HTTP), IRC, anything else: fails as before
	if( GProxyListState == 3 )
		GProxyListState = 0;	// a new query asks again
	INT i = GProxyMasters.AddZeroed();
	GProxyMasters(i).Id = GProxyNextMaster++;
	return GProxyMasters(i).Id;
}
static void ProxyNetTcpSend( INT Handle, const ANSICHAR* Text )
{
	FProxyMaster* M = ProxyMasterFind( Handle );
	if( M && strstr( Text, "\\list\\" ) )
		M->WantList = 1;
}
static INT ProxyNetTcpTick( INT Handle, ANSICHAR* Buffer, INT MaxLen, UBOOL* Opened, UBOOL* Closed )
{
	FProxyMaster* M = ProxyMasterFind( Handle );
	if( !M )
	{
		*Closed = 1;
		return 0;
	}
	if( M->Phase == 0 )
	{
		*Opened = 1;
		M->Out = TEXT("\\basic\\\\secure\\ufront");	// the 6-character challenge: validated by nobody
		M->Phase = 1;
	}
	else if( M->Phase == 1 && M->WantList )
	{
		const INT State = ProxyListPoll();
		if( State >= 2 )
		{
			for( INT i = 0; State == 2 && i < GProxyRows.Num(); i++ )
			{
				const DWORD A = GProxyRows(i).Addr;
				M->Out += FString::Printf( TEXT("\\ip\\%i.%i.%i.%i:%i"),
					(A >> 24) & 255, (A >> 16) & 255, (A >> 8) & 255, A & 255, GProxyRows(i).Port + 1 );
			}
			M->Out += TEXT("\\final\\");
			M->Phase = 2;
		}
	}
	const INT Count = Min( M->Out.Len(), MaxLen );
	if( Count > 0 )
	{
		appMemcpy( Buffer, appToAnsi(*M->Out), Count );
		// ★ Mid( Start ) with its default Count (MAXINT) computes Start+MAXINT: a signed overflow, and under
		// clang -O2 it asked for a 2 GB string (a null write, then the heap-at-address-zero abort). Say the count.
		M->Out = M->Out.Mid( Count, M->Out.Len() - Count );
	}
	if( M->Phase == 2 && !M->Out.Len() )
	{
		M->Phase = 3;
		*Closed = 1;
	}
	return Count;
}
static void ProxyNetTcpClose( INT Handle )
{
	for( INT i = 0; i < GProxyMasters.Num(); i++ )
		if( GProxyMasters(i).Id == Handle )
			GProxyMasters.Remove( i-- );
}

// --- the servers: UdpLink ---
struct FProxyReply
{
	void*   Link;
	DWORD   Addr;
	INT     Port;
	DOUBLE  Due;
	FString Text;
};
static TArray<FProxyReply> GProxyReplies;

static UBOOL ProxyNetUdpSend( void* Link, DWORD Addr, INT Port, const ANSICHAR* Text )
{
	if( GProxyListState != 2 )
		return 0;
	for( INT r = 0; r < GProxyRows.Num(); r++ )
	{
		if( GProxyRows(r).Addr != Addr || GProxyRows(r).Port + 1 != Port )
			continue;
		INT i = GProxyReplies.AddZeroed();
		GProxyReplies(i).Link = Link;
		GProxyReplies(i).Addr = Addr;
		GProxyReplies(i).Port = Port;
		GProxyReplies(i).Due  = appSeconds() + Max( GProxyPingMs, 0 ) / 1000.0;
		// \info\ and \status\ get what the list knows; \rules\ and \players\ it does not know.
		GProxyReplies(i).Text = (strstr( Text, "\\info\\" ) || strstr( Text, "\\status\\" ) || strstr( Text, "\\basic\\" ))
			? GProxyRows(r).Info : FString( TEXT("\\final\\") );
		if( !GProxyPingsLogged )
		{
			GProxyPingsLogged = 1;
			GLog->Logf( TEXT("ProxyNet: the in-game browser's pings are answered from the list (first: %i.%i.%i.%i:%i)"),
				(Addr >> 24) & 255, (Addr >> 16) & 255, (Addr >> 8) & 255, Addr & 255, Port );
		}
		return 1;
	}
	return 0;
}
static UBOOL ProxyNetUdpNextReply( void* Link, DWORD* Addr, INT* Port, ANSICHAR* Buffer, INT MaxLen )
{
	const DOUBLE Now = appSeconds();
	for( INT i = 0; i < GProxyReplies.Num(); i++ )
	{
		FProxyReply& R = GProxyReplies(i);
		if( R.Link != Link || R.Due > Now )
			continue;
		*Addr = R.Addr;
		*Port = R.Port;
		const INT Count = Min( R.Text.Len(), MaxLen - 1 );
		appMemcpy( Buffer, appToAnsi(*R.Text), Count );
		Buffer[Count] = 0;
		GProxyReplies.Remove( i );
		return 1;
	}
	return 0;
}
static void ProxyNetUdpUnbind( void* Link )
{
	for( INT i = 0; i < GProxyReplies.Num(); i++ )
		if( GProxyReplies(i).Link == Link )
			GProxyReplies.Remove( i-- );
}

static FWebLinkHooks GProxyNetLinkHooks = { ProxyNetLinksActive, ProxyNetTcpOpen, ProxyNetTcpSend, ProxyNetTcpTick,
	ProxyNetTcpClose, ProxyNetUdpSend, ProxyNetUdpNextReply, ProxyNetUdpUnbind };

// IpDrv's pointer is constant-initialised to NULL, so this dynamic initialiser always runs after it.
static struct FRegisterProxyNetLinks
{
	FRegisterProxyNetLinks() { GWebLinks = &GProxyNetLinkHooks; }
} GRegisterProxyNetLinks;
#endif

/*-----------------------------------------------------------------------------
	UProxyNetDriver.
-----------------------------------------------------------------------------*/
class DLL_EXPORT_CLASS UProxyNetDriver : public UNetDriver
{
	DECLARE_CLASS(UProxyNetDriver,UNetDriver,CLASS_Transient|CLASS_Config)

	// Constructor.
	UProxyNetDriver()
	{}

	UProxyNetConnection* GetServerConnection()
	{
		return (UProxyNetConnection*)ServerConnection;
	}

	// A player whose saved ini predates this driver has no [ProxyNetDrv.ProxyNetDriver] section, so every
	// CPF_Config property arrives as zero — and a zero ConnectionTimeout closes the connection on its first
	// tick. Substitute the stock TcpNetDriver values for whatever is missing, and say so in the log.
	//
	// ⚠ Done HERE, at InitConnect, and not as constructor assignments: on this engine a C++ constructor runs
	// AFTER the ini was loaded into the object and would overwrite real ini values with these fallbacks.
	void NormalizeConfigDefaults()
	{
		guard(UProxyNetDriver::NormalizeConfigDefaults);
		// A missing SECTION is recognisable by its timeout being zero — no real ini sets that. The two
		// download switches are a bool and a size, which cannot say "unset" on their own, so they follow it:
		// without them a joined server could never send a custom map.
		if( ConnectionTimeout <= 0.f )
		{
			AllowDownloads  = 1;
			MaxDownloadSize = 0; // 0 = no limit on this engine
		}
		FString Patched;
		#define PROXYNET_CFG( Field, Default )									\
			if( Field <= 0 )													\
			{																	\
				Field = Default;												\
				Patched += FString::Printf( TEXT(" %s"), TEXT(#Field) );		\
			}
		// The shipped [IpDrv.TcpNetDriver] values, except the two timeouts: no game ticks run while a web
		// client streams a map it has never touched, so it needs more patience than a native one. These are
		// the CLIENT's timeouts; the SERVER's own (15 s / 150 s on a stock 469e) are not ours to set.
		PROXYNET_CFG( ConnectionTimeout,        60.f )
		PROXYNET_CFG( InitialConnectTimeout,   200.f )
		PROXYNET_CFG( KeepAliveTime,            0.2f )
		PROXYNET_CFG( RelevantTimeout,          5.0f )
		PROXYNET_CFG( SpawnPrioritySeconds,     1.0f )
		PROXYNET_CFG( ServerTravelPause,        4.0f )
		PROXYNET_CFG( MaxClientRate,         10000000 )
		PROXYNET_CFG( NetServerMaxTickRate,        20 )
		PROXYNET_CFG( LanServerMaxTickRate,        35 )
		#undef PROXYNET_CFG
		if( Patched.Len() )
			debugf( NAME_Log, TEXT("ProxyNetDrv: no ini value for:%s - using the stock defaults (add a [ProxyNetDrv.ProxyNetDriver] section to set them)"), *Patched );
		unguard;
	}

	// UNetDriver interface.
	UBOOL InitConnect( FNetworkNotify* InNotify, FURL& ConnectURL, FString& Error )
	{
		guard(UProxyNetDriver::InitConnect);
		if( !Super::InitConnect( InNotify, ConnectURL, Error ) )
			return 0;
		NormalizeConfigDefaults();

		if( !ConnectURL.Host.Len() )
		{
			Error = TEXT("Proxy networking needs a server address (host:port).");
			return 0;
		}

		// Redirect downloads: registered on the first Proxy connection and left in place — the functions
		// are static, and Open() turns away every connection that is not ours.
		GWebHttp = &GProxyNetHttpHooks;

		// Connect to remote — through the game proxy. A NEW connection id every time.
		INT NewConnId = ue_proxynet_connect( appToAnsi(*ConnectURL.Host), ConnectURL.Port );
		if( NewConnId < 0 )
		{
			// The module is absent, or this build has no game proxy configured. Fail here, in words, rather
			// than sit in "Connecting..." until InitialConnectTimeout.
			Error = TEXT("Proxy networking is not available in this build.");
			debugf( NAME_Log, TEXT("ProxyNetDrv: %s"), *Error );
			return 0;
		}

		// Create new connection.
		ServerConnection = new UProxyNetConnection( NewConnId, this, USOCK_Pending, ConnectURL );
		debugf( NAME_DevNet, TEXT("ProxyNetDrv: game client -> %s:%i (proxy conn %i), rate %i"),
			*ConnectURL.Host, ConnectURL.Port, NewConnId, ServerConnection->CurrentNetSpeed );

		// Create channel zero.
		GetServerConnection()->CreateChannel( CHTYPE_Control, 1, 0 );

		return 1;
		unguard;
	}
	UBOOL InitListen( FNetworkNotify* InNotify, FURL& LocalURL, FString& Error )
	{
		guard(UProxyNetDriver::InitListen);
		// A browser cannot accept UDP, and the game proxy carries a client to ONE server — it is not a
		// rendezvous. Hosting is what P2P networking is for: refuse in words.
		Error = TEXT("Proxy networking cannot host a game. Switch to P2P networking to host.");
		debugf( NAME_Log, TEXT("ProxyNetDrv: %s"), *Error );
		return 0;
		unguard;
	}
	void TickDispatch( FLOAT DeltaTime )
	{
		guard(UProxyNetDriver::TickDispatch);
		Super::TickDispatch( DeltaTime );

		UProxyNetConnection* Conn = GetServerConnection();
		if( !Conn || Conn->ConnId < 0 )
			return;

		// The transport's verdict. CONNECTING covers "reached the proxy" AND "queued by the proxy" (it paces
		// new connections to one game server), so it is simply waited out. CLOSED/INVALID is final: refused,
		// unreachable, or lost beyond the proxy's resume window.
		//
		// ★ Never promote the UE connection to USOCK_Open here. It stays USOCK_Pending through the whole
		// download + join handshake and the engine's own join flow opens it — exactly as under TcpNetDriver,
		// which knows nothing of connection state either. This block only ever CLOSES.
		INT TransportState = ue_proxynet_state( Conn->ConnId );
		if( (TransportState==PROXYNET_STATE_CLOSED || TransportState==PROXYNET_STATE_INVALID) && Conn->State!=USOCK_Closed )
		{
			debugf( NAME_DevNet, TEXT("ProxyNetDrv: transport closed (proxy conn %i) - disconnecting"), Conn->ConnId );
			Conn->State = USOCK_Closed;
			return;
		}

		// Process all incoming packets.
		if( Conn->State==USOCK_Open || Conn->State==USOCK_Pending )
		{
			BYTE Data[PROXYNET_RECV_BUFFER];
			for( ; ; )
			{
				INT Size = ue_proxynet_recv( Conn->ConnId, Data, sizeof(Data) );
				if( Size <= 0 )
					break;
				Conn->ReceivedRawPacket( Data, Size );
				// ReceivedRawPacket can close the connection (a malformed packet, a server-side close).
				if( Conn->State==USOCK_Closed || Conn->ConnId < 0 )
					break;
			}
		}
		unguard;
	}
	FString LowLevelGetNetworkNumber()
	{
		guard(UProxyNetDriver::LowLevelGetNetworkNumber);
		// TcpNetDriver answers with the local interface address. A browser has none to report, and the
		// address the game server sees belongs to the proxy.
		return TEXT("proxy");
		unguard;
	}
	void LowLevelDestroy()
	{
		guard(UProxyNetDriver::LowLevelDestroy);
		// The connection closes its own transport in Destroy(); this covers a driver torn down first.
		if( GetServerConnection() )
			GetServerConnection()->CloseTransport();
		unguard;
	}
};
IMPLEMENT_CLASS(UProxyNetDriver);

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
