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
	// Does this link own a socket? On BSD sockets 0 is a valid descriptor and INVALID_SOCKET (-1) is none.
	// Every "has a socket" test goes through here.
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
