/*=============================================================================
	AInternetLink.h.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

	AInternetLink();
	void Destroy();
	UBOOL Tick( FLOAT DeltaTime, enum ELevelTick TickType );	
	SOCKET& GetSocket() 
	{ 
		return *(SOCKET*)&Socket;
	}
	// ufront: "does this link own a socket?" -- on BSD sockets 0 is a VALID descriptor and INVALID_SOCKET
	// (-1) is none. The stock code mixed `if( GetSocket() )` with `== INVALID_SOCKET`, which only agreed on
	// WinSock (INVALID_SOCKET = ~0 is truthy there too, and an fd_set is a handle LIST, so FD_SET of it is
	// harmless). On BSD sockets FD_SET(-1) writes outside the fd_set (glibc's fortified build aborts; wasm
	// corrupts silently), and 0 is somebody's descriptor. Every "has a socket" test goes through here.
	UBOOL HasSocket()
	{
		return GetSocket() != INVALID_SOCKET;
	}
	FResolveInfo*& GetResolveInfo()
	{
		return *(FResolveInfo**)&PrivateResolveInfo;
	}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
