/*=============================================================================
	android_bridge.cpp: the Android app's process set-up for the engine.

	GameActivity calls nativePrepareProcess before SDLActivity starts SDL_main, so the engine's
	first look at its environment finds its game folder: UT99_ANDROID_DATA is the folder,
	UT99_ANDROID_BASEDIR its System directory, which is also the working directory (System/*.ini,
	../Maps, ../Textures resolve as on a desktop), and $HOME is the app's private storage. The
	folder's writable directories (Cache, Save, Logs) are created if missing. JNI and POSIX only.
=============================================================================*/

#include <jni.h>
#include <android/log.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define LOG_TAG "UT99Bridge"
#define LOGI(...) __android_log_print( ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__ )
#define LOGE(...) __android_log_print( ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__ )

// mkdir -p.
static bool MakeDirs( const char* Path )
{
	char Tmp[1024];
	int Len = snprintf( Tmp, sizeof(Tmp), "%s", Path );
	if( Len <= 0 || Len >= (int)sizeof(Tmp) )
		return false;
	if( Tmp[Len-1] == '/' )
		Tmp[Len-1] = 0;
	for( char* P = Tmp + 1; *P; P++ )
		if( *P == '/' )
		{
			*P = 0;
			if( mkdir( Tmp, 0775 ) != 0 && errno != EEXIST )
				return false;
			*P = '/';
		}
	return mkdir( Tmp, 0775 ) == 0 || errno == EEXIST;
}

static bool Child( char* Out, size_t OutSize, const char* Root, const char* Name )
{
	int Len = snprintf( Out, OutSize, "%s/%s", Root, Name );
	return Len > 0 && (size_t)Len < OutSize;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ut99_android_GameActivity_nativePrepareProcess( JNIEnv* Env, jclass, jstring DataRootString, jstring HomeDirString )
{
	if( !DataRootString || !HomeDirString )
		return JNI_FALSE;
	const char* DataRoot = Env->GetStringUTFChars( DataRootString, NULL );
	const char* HomeDir  = Env->GetStringUTFChars( HomeDirString, NULL );
	bool Ok = DataRoot && HomeDir;

	// System/ is where the engine runs; the game writes its cache, saves and logs beside it.
	char SystemDir[1024];
	if( Ok && !Child( SystemDir, sizeof(SystemDir), DataRoot, "System" ) )
		Ok = false;
	static const char* Writable[] = { "Cache", "Save", "Logs" };
	for( size_t i=0; Ok && i<sizeof(Writable)/sizeof(Writable[0]); i++ )
	{
		char Dir[1024];
		if( !Child( Dir, sizeof(Dir), DataRoot, Writable[i] ) || !MakeDirs( Dir ) )
			LOGE( "could not create %s/%s: %s", DataRoot, Writable[i], strerror(errno) );
	}
	if( Ok && !MakeDirs( HomeDir ) )
		LOGE( "could not create the home directory %s: %s", HomeDir, strerror(errno) );

	if( Ok && setenv( "HOME", HomeDir, 1 ) != 0 )
		Ok = false;
	if( Ok && setenv( "UT99_ANDROID_DATA", DataRoot, 1 ) != 0 )
		Ok = false;
	if( Ok && setenv( "UT99_ANDROID_BASEDIR", SystemDir, 1 ) != 0 )
		Ok = false;
	if( Ok && chdir( SystemDir ) != 0 )
	{
		LOGE( "chdir(%s) failed: %s", SystemDir, strerror(errno) );
		Ok = false;
	}
	if( Ok )
		LOGI( "game folder %s, working directory %s, home %s", DataRoot, SystemDir, HomeDir );
	else
		LOGE( "process set-up failed (game folder %s)", DataRoot ? DataRoot : "?" );

	if( DataRoot )
		Env->ReleaseStringUTFChars( DataRootString, DataRoot );
	if( HomeDir )
		Env->ReleaseStringUTFChars( HomeDirString, HomeDir );
	return Ok ? JNI_TRUE : JNI_FALSE;
}
