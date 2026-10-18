/*=============================================================================
	WebLinks.h: the TcpLink / UdpLink conversations a web build plays itself.

	A web build has no sockets (under -sWASMFS every socket syscall is -ENOSYS), so the stock server
	browser (UnrealScript, unchanged) cannot reach a GameSpy master or ping a server. In Proxy
	networking mode the game proxy already holds the public list (it polls the masters for
	everybody), so whoever registered GWebLinks plays both ends from it:
	  * a TcpLink opened to a master's list port gets the master's side of the conversation
	    (the challenge, then "\ip\a.b.c.d:queryport\...\final\"), and
	  * a UdpLink query to a server from that list gets the server's "\info\" answer.

	IpDrv imports nothing from JavaScript for this; the package that owns the transport (ProxyNetDrv)
	registers the table. With no hook, or Active() false (P2P mode), the links keep their usual web
	paths. A connection the table turns down fails to open; a query it turns down goes to the
	synthetic LAN.
=============================================================================*/

#ifndef _INC_WEBLINKS
#define _INC_WEBLINKS

struct FWebLinkHooks
{
	// Is Proxy networking this launch's mode? Answered without calling JavaScript.
	UBOOL (*Active)();

	// A TCP connection played here: a handle >= 0, or -1 = not one of ours (the link's Open fails).
	INT   (*TcpOpen )( DWORD Addr, INT Port );
	// What the script sent.
	void  (*TcpSend )( INT Handle, const ANSICHAR* Text );
	// Every tick: sets *Opened once the far end has "accepted", copies up to MaxLen bytes of the far
	// end's text into Buffer and returns the count, and sets *Closed when the far end has hung up.
	INT   (*TcpTick )( INT Handle, ANSICHAR* Buffer, INT MaxLen, UBOOL* Opened, UBOOL* Closed );
	void  (*TcpClose)( INT Handle );

	// A UDP query answered here: TRUE = taken, and the answer comes out of UdpNextReply (from the
	// address that was asked, as a real server's would). FALSE = not ours.
	UBOOL (*UdpSend     )( void* Link, DWORD Addr, INT Port, const ANSICHAR* Text );
	UBOOL (*UdpNextReply)( void* Link, DWORD* Addr, INT* Port, ANSICHAR* Buffer, INT MaxLen );
	void  (*UdpUnbind   )( void* Link );
};

// NULL until somebody registers. Defined in TcpLink.cpp, on __EMSCRIPTEN__ only.
extern FWebLinkHooks* GWebLinks;

#endif

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
