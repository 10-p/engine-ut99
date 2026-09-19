/*=============================================================================
	UnDownload.h: Unreal file-download interface
	Copyright 1997-2000 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Jack Porter.
=============================================================================*/

//
// v436-compat port: the v436 download system. v400 had none: UFileChannel both drove the transfer and
// wrote the cache file. In 436 a UDownload owns the transfer and the cache write, and the channel is
// just the wire for the one manager that uses a channel (UChannelDownload). Declarations are Epic's
// v432 public UnDownload.h; the bodies come from the retail v436 Engine.dll.
//
class ENGINE_API UDownload : public UObject
{
	DECLARE_ABSTRACT_CLASS(UDownload,UObject,CLASS_Transient|CLASS_Config)
	NO_DEFAULT_CONSTRUCTOR(UDownload)

	// Variables.
	UNetConnection* Connection;			// Connection
	INT				PackageIndex;		// Index of package in Map.
	FPackageInfo*	Info;				// Package Info
	FString			DownloadParams;		// Download params sent to the client.
	UBOOL			UseCompression;		// Send compressed files to the client.
	FArchive*		RecvFileAr;			// File being received.
	TCHAR			TempFilename[256];	// Filename being transfered.
	TCHAR			Error[256];			// A download error occurred.
	INT				Transfered;			// Bytes transfered.
	UBOOL			SkippedFile;		// File was skipped.
	UBOOL			IsCompressed;		// Use file compression.

	// Constructors.
	void StaticConstructor();

	// UObject interface.
	void Destroy();
	void Serialize( FArchive& Ar );

	// UDownload Interface.
	virtual UBOOL TrySkipFile();
	virtual void ReceiveFile( UNetConnection* InConnection, INT PackageIndex, const TCHAR* Params=NULL, UBOOL InCompression=0 );
	virtual void ReceiveData( BYTE* Data, INT Count );
	virtual void Tick() {}
	virtual void DownloadError( const TCHAR* Error );
	virtual void DownloadDone();
};

class ENGINE_API UChannelDownload : public UDownload
{
	DECLARE_CLASS(UChannelDownload,UDownload,CLASS_Transient|CLASS_Config)
	NO_DEFAULT_CONSTRUCTOR(UChannelDownload)

	// Variables.
	UFileChannel* Ch;

	// Constructors.
	void StaticConstructor();

	// UObject interface.
	void Destroy();
	void Serialize( FArchive& Ar );

	// UDownload Interface.
	void ReceiveFile( UNetConnection* InConnection, INT PackageIndex, const TCHAR* Params=NULL, UBOOL InCompression=0 );
	UBOOL TrySkipFile();
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
