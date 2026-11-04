/*=============================================================================
	FOutputDeviceFile.h: ANSI file output device.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include <stdio.h> // fputs/stdout for the GLog->stdout echo
#if defined(UE_ANDROID_APP)
#include <android/log.h> // the echo goes to logcat instead
#endif

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
				// Mirror every logged line to stdout, on every target: the terminal natively, the browser console
				// on the web (Module.print). The trailing '\n' flushes Emscripten's line-buffered stdout; the
				// NAME_Title filter matches the file write above.
				if( Event != NAME_Title )
				{
#if defined(UE_ANDROID_APP)
					// The app has no terminal: the line goes to logcat (tag UT99) instead of stdout.
					WriteLogcat( FName::SafeString(Event), Data );
#else
					fputs( FName::SafeString(Event), stdout );
					fputs( ": ", stdout );
					fputs( Data, stdout );
					fputc( '\n', stdout );
#endif
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
#if defined(UE_ANDROID_APP)
	// Writes "Event: Data" to logcat, one entry per line; a line longer than an entry's buffer continues in
	// the entries that follow, so none of it is lost.
	static void WriteLogcat( const TCHAR* Event, const TCHAR* Data )
	{
		ANSICHAR Entry[1024];
		INT Len = 0;
		const TCHAR* Parts[] = { Event, TEXT(": "), Data };
		for( INT p=0; p<ARRAY_COUNT(Parts); p++ )
		{
			for( const TCHAR* C=Parts[p]; *C; C++ )
			{
				if( Len == ARRAY_COUNT(Entry)-1 )
				{
					Entry[Len] = 0;
					__android_log_write( ANDROID_LOG_INFO, "UT99", Entry );
					Len = 0;
				}
				Entry[Len++] = *C;
			}
		}
		Entry[Len] = 0;
		__android_log_write( ANDROID_LOG_INFO, "UT99", Entry );
	}
#endif
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
