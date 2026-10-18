/*=============================================================================
	UnPenLev.h: Unreal pending level definition.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

/*-----------------------------------------------------------------------------
	UPendingLevel.
-----------------------------------------------------------------------------*/

//
// Class controlling a pending game level.
//
class UPendingLevel : public ULevelBase
{
	DECLARE_ABSTRACT_CLASS(UPendingLevel,UObject,0)
	NO_DEFAULT_CONSTRUCTOR(UPendingLevel)

	// Variables.
	UBOOL		Success;
	UBOOL		SentJoin;
	UBOOL		LonePlayer;
	INT			FilesNeeded;
	FString		Error;
	FString		FailCode;		// Server's FAILCODE, handed to the console by UGameEngine::Tick.
	FString		FailURL;		// The URL that failed (PASSWORD= options removed).

	// Constructors.
	UPendingLevel( UEngine* InEngine, const FURL& InURL );

	// UPendingLevel interface.
	virtual void Tick( FLOAT DeltaTime )=0;
	virtual UNetDriver* GetDriver()=0;
	virtual void SendJoin()=0;
	// Skips the file being downloaded if it can be (UGameEngine::Exec CANCEL: F10 during a download).
	virtual UBOOL TrySkipFile()=0;
};

/*-----------------------------------------------------------------------------
	UNetPendingLevel.
-----------------------------------------------------------------------------*/


class UNetPendingLevel : public UPendingLevel
{
	DECLARE_CLASS(UNetPendingLevel,UPendingLevel,CLASS_Transient)
	NO_DEFAULT_CONSTRUCTOR(UNetPendingLevel)

	// Constructors.
	UNetPendingLevel( UEngine* InEngine, const FURL& InURL );

	// FNetworkNotify interface.
	EAcceptConnection NotifyAcceptingConnection();
	void NotifyAcceptedConnection( class UNetConnection* Connection );
	UBOOL NotifyAcceptingChannel( class UChannel* Channel );
	ULevel* NotifyGetLevel();
	void NotifyReceivedText( UNetConnection* Connection, const TCHAR* Text );
	void NotifyReceivedFile( UNetConnection* Connection, INT PackageIndex, const TCHAR* Error, UBOOL Skipped );
	UBOOL NotifySendingFile( UNetConnection* Connection, FGuid Guid );

	// UPendingLevel interface.
	void Tick( FLOAT DeltaTime );
	UNetDriver* GetDriver() { return NetDriver; }
	void SendJoin();
	UBOOL TrySkipFile();

	// UNetPendingLevel interface.
	// Request the next package still flagged PKG_Need, or finish the download.
	void ReceiveNextFile( UNetConnection* Connection );
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
