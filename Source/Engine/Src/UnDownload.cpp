/*=============================================================================
	UnDownload.cpp: Unreal file-download implementation.
	Copyright 1997-2000 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Jack Porter.
=============================================================================*/

#include "EnginePrivate.h"
#include "UnNet.h"
#include "FConfigCacheIni.h"
#include "FCodec.h"

/*-----------------------------------------------------------------------------
	UDownload implementation.
-----------------------------------------------------------------------------*/

void UDownload::StaticConstructor()
{
	guard(UDownload::StaticConstructor);
	DownloadParams = TEXT("");
	UseCompression = 0;
	unguard;
}
void UDownload::Destroy()
{
	guard(UDownload::Destroy);
	if( RecvFileAr )
	{
		delete RecvFileAr;
		RecvFileAr = NULL;
	}
	GFileManager->Delete( TempFilename );
	if( Connection && Connection->Download==this )
		Connection->Download = NULL;
	Connection = NULL;
	Super::Destroy();
	unguard;
}
void UDownload::Serialize( FArchive& Ar )
{
	guard(UDownload::Serialize);
	Super::Serialize( Ar );
	Ar << Connection;
	unguard;
}
UBOOL UDownload::TrySkipFile()
{
	// Only an optional package may be skipped, and only while it is being received.
	if( RecvFileAr && (Info->PackageFlags & PKG_ClientOptional) )
	{
		SkippedFile = 1;
		return 1;
	}
	return 0;
}
void UDownload::ReceiveFile( UNetConnection* InConnection, INT InPackageIndex, const TCHAR* Params, UBOOL InCompression )
{
	Connection   = InConnection;
	PackageIndex = InPackageIndex;
	Info         = &Connection->PackageMap->List( PackageIndex );
}
void UDownload::ReceiveData( BYTE* Data, INT Count )
{
	guard(UDownload::ReceiveData);

	if( Transfered==0 && !RecvFileAr )
	{
		// Open temporary file initially.
		debugf( NAME_DevNet, TEXT("Receiving package '%s'"), Info->Parent->GetName() );
		GFileManager->MakeDirectory( *GSys->CachePath, 0 );
		appCreateTempFilename( *GSys->CachePath, TempFilename );
		RecvFileAr = GFileManager->CreateFileWriter( TempFilename );
	}

	// Receive.
	if( !RecvFileAr )
	{
		// Opening file failed.
		DownloadError( LocalizeError(TEXT("NetOpen")) );
		return;
	}
	if( Count > 0 )
	{
		RecvFileAr->Serialize( Data, Count );
		if( RecvFileAr->IsError() )
		{
			// Write failed.
			DownloadError( *FString::Printf( LocalizeError("NetWrite"), TempFilename ) );
			return;
		}

		// Successful.
		Transfered += Count;
		TCHAR Msg1[256], Msg2[256];
		appSprintf( Msg1, LocalizeProgress( (Info->PackageFlags & PKG_ClientOptional) ? "ReceiveOptionalFile" : "ReceiveFile" ), Info->Parent->GetName() );
		appSprintf( Msg2, LocalizeProgress("ReceiveSize"), Info->FileSize/1024, 100.f*Transfered/Info->FileSize );
		Connection->Driver->Notify->NotifyProgress( Msg1, Msg2, 4.f );
	}

	unguard;
}
void UDownload::DownloadError( const TCHAR* InError )
{
	guard(UDownload::DownloadError);
	appStrcpy( Error, InError );
	unguard;
}
void UDownload::DownloadDone()
{
	guard(UDownload::DownloadDone);

	if( RecvFileAr )
	{
		delete RecvFileAr;
		RecvFileAr = NULL;
	}

	if( SkippedFile )
	{
		debugf( TEXT("Skipped download of '%s'"), Info->Parent->GetName() );
		GFileManager->Delete( TempFilename );
		TCHAR Msg[256];
		appSprintf( Msg, TEXT("Skipped '%s'"), Info->Parent->GetName() );
		Connection->Driver->Notify->NotifyProgress( TEXT("Success"), Msg, 4.f );
		Connection->Driver->Notify->NotifyReceivedFile( Connection, PackageIndex, TEXT(""), 1 );
		return;
	}

	TCHAR Dest[256];
	appSprintf( Dest, TEXT("%s") PATH_SEPARATOR TEXT("%s.uxx"), *GSys->CachePath, Info->Guid.String() );

	if( !*Error && Transfered==0 )
		DownloadError( *FString::Printf( LocalizeError("NetRefused"), Info->Parent->GetName() ) );

	if( !*Error && IsCompressed )
	{
		// Decompress into a second temporary file, which becomes the received file.
		TCHAR CFilename[256];
		appStrcpy( CFilename, TempFilename );
		appCreateTempFilename( *GSys->CachePath, TempFilename );
		FArchive* CFileAr = GFileManager->CreateFileReader( CFilename );
		FArchive* UFileAr = GFileManager->CreateFileWriter( TempFilename );
		if( !CFileAr || !UFileAr )
		{
			DownloadError( LocalizeError(TEXT("NetOpen")) );
		}
		else
		{
			INT Signature;
			*CFileAr << Signature;
			if( Signature!=1234 && Signature!=5678 )
			{
				DownloadError( LocalizeError("NetSize") );
			}
			else
			{
				FString OrigFilename;
				*CFileAr << OrigFilename;
				FCodecFull Codec;
				Codec.AddCodec(new FCodecRLE);
				Codec.AddCodec(new FCodecBWT);
				Codec.AddCodec(new FCodecMTF);
				if( Signature==5678 )
				{
					Codec.AddCodec(new FCodecRLE);
					Codec.AddCodec(new FCodecHuffman);
				}
				Codec.Decode( *CFileAr, *UFileAr );
			}
		}
		if( CFileAr )
		{
			GFileManager->Delete( CFilename );
			delete CFileAr;
		}
		if( UFileAr )
			delete UFileAr;
	}

	if( !*Error && GFileManager->FileSize(TempFilename)!=Info->FileSize )
		DownloadError( LocalizeError("NetSize") );

	if( !*Error && !GFileManager->Move( Dest, TempFilename ) )
		DownloadError( LocalizeError("NetMove") );

	if( *Error )
	{
		// Failure.
		GFileManager->Delete( TempFilename );
		Connection->Driver->Notify->NotifyReceivedFile( Connection, PackageIndex, Error, 0 );
	}
	else
	{
		// Success. Record the package in the cache index so it can be found by name later.
		FConfigCacheIni Config;
		TCHAR Ini[256];
		appSprintf( Ini, TEXT("%s") PATH_SEPARATOR TEXT("cache.ini"), *GSys->CachePath );
		Config.SetString( TEXT("Cache"), Info->Guid.String(), (*Info->URL) ? *Info->URL : Info->Parent->GetName(), Ini );

		TCHAR Msg[256];
		appSprintf( Msg, TEXT("Received '%s'"), Info->Parent->GetName() );
		Connection->Driver->Notify->NotifyProgress( TEXT("Success"), Msg, 4.f );
		Connection->Driver->Notify->NotifyReceivedFile( Connection, PackageIndex, Error, 0 );
	}

	unguard;
}
IMPLEMENT_CLASS(UDownload);

/*-----------------------------------------------------------------------------
	UChannelDownload implementation.
-----------------------------------------------------------------------------*/

void UChannelDownload::StaticConstructor()
{
	guard(UChannelDownload::StaticConstructor);
	DownloadParams = TEXT("Enabled");
	unguard;
}
void UChannelDownload::Destroy()
{
	guard(UChannelDownload::Destroy);
	if( Ch && Ch->Download==this )
		Ch->Download = NULL;
	Ch = NULL;
	Super::Destroy();
	unguard;
}
void UChannelDownload::Serialize( FArchive& Ar )
{
	guard(UChannelDownload::Serialize);
	Super::Serialize( Ar );
	Ar << Ch;
	unguard;
}
void UChannelDownload::ReceiveFile( UNetConnection* InConnection, INT InPackageIndex, const TCHAR* Params, UBOOL InCompression )
{
	guard(UChannelDownload::ReceiveFile);
	Super::ReceiveFile( InConnection, InPackageIndex, Params, InCompression );

	// Create the file channel and ask for the package.
	Ch = (UFileChannel*)Connection->CreateChannel( CHTYPE_File, 1 );
	if( !Ch )
	{
		DownloadError( LocalizeError("ChAllocate") );
		DownloadDone();
		return;
	}
	Ch->Download     = this;
	Ch->PackageIndex = InPackageIndex;

	FOutBunch Bunch( Ch, 0 );
	Bunch << Info->Guid;
	Bunch.bReliable = 1;
	check(!Bunch.IsError());
	Ch->SendBunch( &Bunch, 0 );

	unguard;
}
UBOOL UChannelDownload::TrySkipFile()
{
	guard(UChannelDownload::TrySkipFile);
	if( Ch && Super::TrySkipFile() )
	{
		FOutBunch Bunch( Ch, 1 );
		FString Cmd = TEXT("SKIP");
		Bunch << Cmd;
		Bunch.bReliable = 1;
		Ch->SendBunch( &Bunch, 0 );
		return 1;
	}
	return 0;
	unguard;
}
IMPLEMENT_CLASS(UChannelDownload);

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
