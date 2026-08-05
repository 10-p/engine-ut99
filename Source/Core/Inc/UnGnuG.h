/*=============================================================================
	UnGnuG.h: Unreal definitions for Gnu G++. Unfinished. Unsupported.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

/*----------------------------------------------------------------------------
	Platform compiler definitions.
----------------------------------------------------------------------------*/


#ifdef PLATFORM_WIN32
#define __WIN32__	1
#endif

#ifndef PLATFORM_BIG_ENDIAN
#define __INTEL__	1
#define __INTEL_BYTE_ORDER__ 1
#endif

#include <stdarg.h>
#include <math.h>   // floorf/ceilf for the rounding primitives below
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <alloca.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <malloc.h>
#include <dirent.h>
#include <limits.h>
// ufront 2.7b (web port): emscripten's musl libc doesn't pull PATH_MAX transitively the way the native
// Linux headers do; UnUnix.cpp + the launcher use fixed TCHAR[PATH_MAX] buffers. Guard so native/DC (which
// already define it) are untouched.
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#ifdef PLATFORM_WIN32
#include <minwindef.h>
#endif

/*----------------------------------------------------------------------------
	Platform specifics types and defines.
----------------------------------------------------------------------------*/

// Undo any Windows defines.
#undef BYTE
#undef WORD
#undef DWORD
#undef INT
#undef FLOAT
#undef MAXBYTE
#undef MAXWORD
#undef MAXDWORD
#undef MAXINT
#undef VOID
#undef CDECL

#ifndef _WINDOWS_
	#define HANDLE uintptr_t
	#define HINSTANCE uintptr_t
#endif


// Sizes.
enum {DEFAULT_ALIGNMENT = 8 }; // Default boundary to align memory allocations on.
enum {CACHE_LINE_SIZE   = 32}; // Cache line size.
#define GCC_PACK(n) __attribute__((packed,aligned(n)))
#define GCC_ALIGN(n) __attribute__((aligned(n)))

// Optimization macros

#define DISABLE_OPTIMIZATION _Pragma("GCC push_options") \
	_Pragma("GCC optimize(\"O0\")")
#define ENABLE_OPTIMIZATION  _Pragma("GCC pop_options")


// Function type macros.
#define DLL_IMPORT
#define DLL_EXPORT	
#define DLL_EXPORT_CLASS
#define VARARGS
#define CDECL
#define STDCALL
#define FORCEINLINE /* Force code to be inline */
#define ZEROARRAY 0 /* Zero-length arrays in structs */
#define __cdecl


#ifdef UNREAL_STATIC
#undef DLL_IMPORT
#define DLL_IMPORT
#endif

#ifdef PLATFORM_WIN32
#define GCC_HIDDEN
#elif !defined(UNREAL_STATIC)
#define GCC_HIDDEN __attribute__((visibility("hidden")))
#else
#define GCC_HIDDEN
#endif

#define GCC_USED __attribute__((used))

// Variable arguments.
#define GET_VARARGS(msg,len,fmt)	\
{	\
	va_list ArgPtr;	\
	va_start( ArgPtr, fmt );	\
	vsprintf( msg, fmt, ArgPtr );	\
	va_end( ArgPtr );	\
}

#define GET_VARARGS_RESULT(msg,len,fmt,result)	\
{	\
	va_list ArgPtr;	\
	va_start( ArgPtr, fmt );	\
	result = vsprintf( msg, fmt, ArgPtr );	\
	va_end( ArgPtr );	\
}

// Compiler name.
#ifdef _DEBUG
	#define COMPILER "Compiled with GCC (Debug)"
#else
	#define COMPILER "Compiled with GCC"
#endif

// Unsigned base types.
typedef uint8_t				BYTE;		// 8-bit  unsigned.
typedef uint16_t			_WORD;		// 16-bit unsigned.
typedef uint32_t 			DWORD;		// 32-bit unsigned.
typedef uint64_t 			QWORD;		// 64-bit unsigned.

// Signed base types.
typedef	int8_t				SBYTE;		// 8-bit  signed.
typedef int16_t  			SWORD;		// 16-bit signed.
typedef int32_t    			INT;		// 32-bit signed.
typedef int64_t  			SQWORD;		// 64-bit signed.

// Character types.
typedef char			    ANSICHAR;	// An ANSI character.
typedef uint16_t    		 UNICHAR;	// A unicode character.
typedef uint8_t				ANSICHARU;	// An ANSI character.
typedef uint16_t     		UNICHARU;	// A unicode character.

// Other base types.
typedef int32_t				UBOOL;		// Boolean 0 (false) or 1 (true).
typedef float				FLOAT;		// 32-bit IEEE floating point.
typedef double				DOUBLE;		// 64-bit IEEE double.
typedef size_t        		SIZE_T;     // Corresponds to C SIZE_T.

// Bitfield type.
typedef uint32_t			BITFIELD;	// For bitfields.


// Make sure characters are unsigned.
static_assert((char)-1 < 0, "char must be signed.");

// Strings.
#define LINE_TERMINATOR TEXT("\n")
#define PATH_SEPARATOR TEXT("/")
#define DLLEXT TEXT(".so")

// No VC++ asm.
#undef ASM
#define ASM 0
#undef ASMLINUX
#define ASMLINUX 0
#undef ASM3DNOW
#define ASM3DNOW 0


// NULL.
#undef NULL
#define NULL 0

// Package implementation.
#ifdef UNREAL_STATIC
	#define IMPLEMENT_PACKAGE_PLATFORM(pkgname) \
		extern "C" {BYTE GCC_USED DLL_EXPORT GLoaded##pkgname;} \
		STATIC_EXPORT( GLoaded##pkgname, GLoaded##pkgname )
#else
	#define IMPLEMENT_PACKAGE_PLATFORM(pkgname) \
		extern "C" {HINSTANCE hInstance;} \
		BYTE GLoaded##pkgname;
#endif
// Platform support options.
#define PLATFORM_NEEDS_ARRAY_NEW 1
#define FORCE_ANSI_LOG           0

// OS unicode function calling.
#define TCHAR_CALL_OS(funcW,funcA) (funcA)
#define TCHAR_TO_ANSI(str) str
#define ANSI_TO_TCHAR(str) str

// !! Fixme: This is a workaround.
#define GCC_OPT_INLINE

// Memory
#define appAlloca(size) alloca((size+7)&~7)

extern CORE_API UBOOL GTimestamp;
extern CORE_API DOUBLE GSecondsPerCycle;
CORE_API DOUBLE appSecondsSlow();
//
// Round a floating point number to an integer.
// Note that (int+.5) is rounded to (int+1).
//
#define DEFINED_appRound 1
inline INT appRound( FLOAT F )
{
	// NOT (INT)F. The MSVC original (UnVcWin32.h) is `fld [F]; fistp [I]`, and fistp ROUNDS using
	// the FPU's current mode; the GCC port transliterated the shape and dropped that, leaving a
	// plain cast. C++ float->int truncates toward zero, so this rounded nothing: appRound(3.7)
	// returned 3. It disagreed with a true round on 49% of inputs, on every target -- native, ARM
	// and the web alike, since the language rule is what changed, not the instruction set.
	// engine-unreal1 has the correct definition in UnFile.cpp, which is why this never showed there.
	//
	// Owner-confirmed: restoring these two primitives fixed the long-standing horizontal-streak bug
	// in the software renderer -- 1px-tall streaks that ran until the span buffer stopped them,
	// taking their source's texture and lighting and degenerating into bright noise, on both BSP
	// surfaces (stairs, marble) and meshes (barrel, rocket launcher, vehicles).
	return (INT)floorf( F + 0.5f );
}

//
// Converts to integer equal to or less than.
//
#define DEFINED_appFloor 1
inline INT appFloor( FLOAT F )
{
	// Same story as appRound above: the MSVC original is `fld [F]; fsub [Half]; fistp [I]`, where
	// fistp's round-to-nearest turns (F - 0.5) into floor(F). Without the rounding it is just a
	// truncation of F-0.5, so appFloor(3.2) returned 2 and appFloor(-3.2) returned -3.
	//
	// The knock-on was worse than the off-by-one. IntY/YFloor come from here, so the effective
	// scanline boundary sat at y.5 instead of y.0 -- which defeats the engine's own degeneracy
	// guard (the deliberate `+1.0001` in FX15/FY15, UnCamera.cpp, exists to keep vertices off the
	// boundary and is calibrated for integers). Edges straddling a half-pixel line then passed the
	// rasterisers' `IntY != IntY` test with an arbitrarily small delta-Y, so 1/delta-Y exploded:
	// UnRender.cpp's span rasteriser overflowed its DWORD fixed-point step and DrawPoly's edge
	// walker lost its mantissa to cancellation. Both emitted one scanline at a garbage X, which is
	// the horizontal streaks -- bounded in length only by the span buffer, hence half the screen.
	return (INT)floorf( F );
}

//
// CPU cycles, related to GSecondsPerCycle.
//
#define DEFINED_appCycles 1
CORE_API DWORD appCycles();
//
// Seconds, arbitrarily based.
//
#define DEFINED_appSeconds 1
CORE_API DOUBLE appSeconds();

//
// Memory copy.
//
#define DEFINED_appMemcpy 1
inline void appMemcpy( void* Dest, const void* Src, INT Count )
{
	memcpy( Dest, Src, Count);
}

//
// Memory zero.
//
#define DEFINED_appMemzero 1
inline void appMemzero( void* Dest, INT Count )
{
	memset( Dest, 0, Count );
}

//
// POSIX equivalents for non-standard functions.
//
#ifndef strupr
inline char* strupr( char* str )
{
	if( str )
	{
		for( char* p = str; *p; ++p )
			*p = (char)toupper( (unsigned char)*p );
	}
	return str;
}
#endif
#ifndef strnicmp
#define strnicmp strncasecmp
#endif
#ifndef stricmp
#define stricmp strcasecmp
#endif

/*----------------------------------------------------------------------------
	Globals.
----------------------------------------------------------------------------*/

// System identification.
extern "C"
{
	extern HINSTANCE      hInstance;
	extern CORE_API UBOOL GIsMMX;
	extern CORE_API UBOOL GIsPentiumPro;
	extern CORE_API UBOOL GIsKatmai;
	extern CORE_API UBOOL GIsK6;
	extern CORE_API UBOOL GIs3DNow;
}

// Module name
extern ANSICHAR GModule[32];

/*----------------------------------------------------------------------------
	The End.
----------------------------------------------------------------------------*/
