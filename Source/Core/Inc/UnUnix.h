/*============================================================================
	UnUnix.h: Unix system-specific declarations.

	Revision history:
		* Created by Mike Danylchuk
		* Modified for Unreal Engine standards compliance by Brandon Reinhart
============================================================================*/

#include <cstring>

// Pathnames.
#define PATH(s) appUnixPath( s )
char* appUnixPath( const char* Path );

// Networking.
unsigned long appGetLocalIP( void );

// String functions.
int stricmp( const char* s, const char* t );
int strnicmp( const char* s, const char* t, int n );
char* strupr( char* s );

// Globally Unique Identifiers.
void appGetGUID( void* GUID );

// ufront 2.33 — Windows side of this same platform layer (Core/Src/UnMinGW.cpp). WinSock needs an
// explicit process-wide startup that BSD sockets do not, and appComputerName/appGetLocalIP can run
// before IpDrv has loaded.
#ifdef PLATFORM_WIN32
CORE_API void appWinSockInit();
#endif

// Signal Handling
void HandleSignal( int Signal );
void HandleInterrupt( int Signal );

/*----------------------------------------------------------------------------
	The End.
----------------------------------------------------------------------------*/

