/*=============================================================================
	FOutputDeviceFile.h: ANSI file output device.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include <stdio.h> // fputs/stdout for the GLog->stdout echo (ufront 2.8); already transitive via UnFile.h, explicit here.

//
// ANSI file output device.
//
class FOutputDeviceFile : public FOutputDevice
{
public:
	FOutputDeviceFile()
	: LogAr( NULL )
	, Opened( 0 )
	, Dead( 0 )
	{
		Filename[0]=0;
	}
	~FOutputDeviceFile()
	{
		if( LogAr )
		{
			Logf( NAME_Log, TEXT("Log file closed, %s"), appTimestamp() );
			delete LogAr;
			LogAr = NULL;
		}
	}
	void Serialize( const TCHAR* Data, enum EName Event )
	{
		static UBOOL Entry=0;
		if( !GIsCriticalError || Entry )
		{
			if( !FName::SafeSuppressed(Event) )
			{
				if( !LogAr && !Dead )
				{
					// Make log filename.
					if( !Filename[0] )
					{
						appStrcpy( Filename, appBaseDir() );
						if
						(	!Parse(appCmdLine(), TEXT("LOG="), Filename+appStrlen(Filename), ARRAY_COUNT(Filename)-appStrlen(Filename) )
						&&	!Parse(appCmdLine(), TEXT("ABSLOG="), Filename, ARRAY_COUNT(Filename) ) )
						{
							appStrcat( Filename, appPackage() );
							appStrcat( Filename, TEXT(".log") );
						}
					}

					// Open log file.
					LogAr = GFileManager->CreateFileWriter( Filename, FILEWRITE_AllowRead|FILEWRITE_Unbuffered|(Opened?FILEWRITE_Append:0));
					if( LogAr )
					{
						Opened = 1;
#if UNICODE && !FORCE_ANSI_LOG
						_WORD UnicodeBOM = UNICODE_BOM;
						LogAr->Serialize( &UnicodeBOM, 2 );
#endif
						Logf( NAME_Log, TEXT("Log file open, %s"), appTimestamp() );
					}
					else Dead = 1;
				}
				if( LogAr && Event!=NAME_Title )
				{
#if FORCE_ANSI_LOG && UNICODE
					TCHAR Ch[1024];
					ANSICHAR ACh[1024];
					appSprintf( Ch, TEXT("%s: %s%s"), FName::SafeString(Event), Data, LINE_TERMINATOR );
					for( INT i=0; Ch[i]; i++ )
						ACh[i] = ToAnsi(Ch[i] );
					ACh[i] = 0;
					LogAr->Serialize( ACh, i );
#else
					WriteRaw( FName::SafeString(Event) );
					WriteRaw( TEXT(": ") );
					WriteRaw( Data );
					WriteRaw( LINE_TERMINATOR );
#endif
				}
				// ufront 2.8: mirror every non-suppressed line to stdout so the browser console receives it
				// (the host's Module.print forwards stdout -> console.log). UT99 otherwise logs only to the
				// MEMFS this.program.log; this restores the standard UE1 GLog->console behaviour that
				// engine-unreal1 + engine-ut2004 already have — a live-crash-visible human/dev feature.
				// Deliberately NOT #ifdef __EMSCRIPTEN__'d: on native it prints to the terminal, on web
				// Module.print routes it to the console, mirroring unreal1's FGlobalPlatform::WriteBinary
				// fputs(stdout). Trailing '\n' flushes Emscripten's line-buffered stdout (one console.log
				// per line). Same Event!=NAME_Title filter as the file write above, so console == the .log.
				// (The prior echo lived in the never-compiled Src/FOutputDeviceFile.cpp — the live sink is
				// THIS inline Serialize; that orphan .cpp is deleted in the same change.)
				if( Event != NAME_Title )
				{
					fputs( FName::SafeString(Event), stdout );
					fputs( ": ", stdout );
					fputs( Data, stdout );
					fputc( '\n', stdout );
				}
				if( GLogHook )
					GLogHook->Serialize( Data, Event );
			}
		}
		else
		{
			Entry=1;
			try
			{
				// Ignore errors to prevent infinite-recursive exception reporting.
				Serialize( Data, Event );
			}
			catch( ... )
			{}
			Entry=0;
		}
	}
	FArchive* LogAr;
	TCHAR Filename[1024];
private:
	UBOOL Opened, Dead;
	void WriteRaw( const TCHAR* C )
	{
		LogAr->Serialize( const_cast<TCHAR*>(C), appStrlen(C)*sizeof(TCHAR) );
	}
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
