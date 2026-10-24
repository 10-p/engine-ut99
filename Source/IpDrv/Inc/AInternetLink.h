/*=============================================================================
	AInternetLink.h.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

	AInternetLink();
	void Destroy();
	UBOOL Tick( FLOAT DeltaTime, enum ELevelTick TickType );	
	// Socket is the script's int, which holds a descriptor on every target (a WinSock handle has 32 significant
	// bits); it reads back sign-extended, so INVALID_SOCKET stays INVALID_SOCKET. Store with `Socket = ...`.
	SOCKET GetSocket()
	{
		return (SOCKET)Socket;
	}
	// Does this link own a socket? On BSD sockets 0 is a valid descriptor and INVALID_SOCKET (-1) is none, and
	// FD_SET(-1) writes outside the fd_set. Every "has a socket" test goes through here.
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
