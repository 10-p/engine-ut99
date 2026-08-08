/*=============================================================================
	UnMinGW.cpp: Win32 calls the portable core does not have.

	ufront step 2.33 — the Windows build.

	The engine's platform layer for every GCC/Clang target is UnUnix.cpp (Core/CMakeLists.txt picks
	UnVcWin32.cpp only for MSVC). That layer is very nearly portable to MinGW already; this file exists
	for the few places where there is no POSIX answer at all, so that Windows work stays OUT of the
	shared sources instead of accreting as #ifdefs inside them.

	COMPILED ON WINDOWS ONLY — Linux and web never see it, which is exactly the point.
=============================================================================*/

#ifdef PLATFORM_WIN32

#include <windows.h>
#include <objbase.h>

#include "CorePrivate.h"

/*-----------------------------------------------------------------------------
	System errors.
-----------------------------------------------------------------------------*/

//
// Get system error. Mirrors UnVcWin32.cpp's implementation; needed by the Windows file manager and
// error handler (FFileManagerWindows.h, FOutputDeviceWindowsError.h), which UnVcWin32.cpp would
// otherwise have supplied.
//
CORE_API const TCHAR* appGetSystemErrorMessage( INT Error )
{
	guard(appGetSystemErrorMessage);
	static TCHAR Msg[1024];
	*Msg = 0;
	if( Error == 0 )
		Error = GetLastError();
	FormatMessageA( FORMAT_MESSAGE_FROM_SYSTEM, NULL, Error,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), Msg, ARRAY_COUNT(Msg), NULL );
	// FormatMessage terminates with CRLF; the callers log this inline.
	if( appStrchr(Msg,'\r') )
		*appStrchr(Msg,'\r') = 0;
	if( appStrchr(Msg,'\n') )
		*appStrchr(Msg,'\n') = 0;
	return Msg;
	unguard;
}

/*-----------------------------------------------------------------------------
	Guids.
-----------------------------------------------------------------------------*/

//
// appCreateGuid (UnUnix.cpp) delegates here. The POSIX implementation is UnGUID.cpp — the OSF/DCE
// algorithm over gettimeofday plus a MAC address — which Core/CMakeLists.txt drops on Windows in favour
// of the OS's own generator. FGuid is four DWORDs, the same shape and byte order as a Win32 GUID.
//
void appGetGUID( void* GUID )
{
	guard(appGetGUID);
	check(GUID);
	if( CoCreateGuid( (::GUID*)GUID ) != S_OK )
		appErrorf( TEXT("CoCreateGuid failed: %s"), appGetSystemErrorMessage() );
	unguard;
}

/*-----------------------------------------------------------------------------
	Sockets.
-----------------------------------------------------------------------------*/

//
// WinSock needs an explicit process-wide startup that BSD sockets do not; without it every socket call
// fails with WSANOTINITIALISED. IpDrv calls this from its own init (see IpDrvPrivate.h), and appComputerName
// /appGetLocalIP in UnUnix.cpp reach gethostname through the same library.
//
CORE_API void appWinSockInit()
{
	guard(appWinSockInit);
	static UBOOL Initialized = 0;
	if( !Initialized )
	{
		WSADATA WsaData;
		INT Error = WSAStartup( MAKEWORD(2,2), &WsaData );
		if( Error != 0 )
			appErrorf( TEXT("WSAStartup failed: %s"), appGetSystemErrorMessage(Error) );
		Initialized = 1;
	}
	unguard;
}

#endif // PLATFORM_WIN32

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
