/*=============================================================================
	UnScriptOverride.cpp: script functions that run as C++.

	A function listed in GScriptOverrides runs as C++ in place of its UnrealScript. UFunction::Bind asks
	BindScriptOverride when the function is loaded; the override is bound only in the game (not the editor or
	ucc), and only when the function is exactly the listed one: its export in the package file has the listed size
	and CRC, and its parameters are the ones the C++ reads. The function is then bound as a native, the way a
	script `native` function is: callers' bytecode is unchanged, and a mod's or another release's version of the
	function keeps its own script. Overrides are for client-side functions called from script only (no
	replication, no ProcessEvent).
=============================================================================*/

#include "CorePrivate.h"

/*-----------------------------------------------------------------------------
	UTMenu.UTLadderStub.SetupWinParams.
-----------------------------------------------------------------------------*/

// The byte offsets of UWindowWindow's WinLeft, WinTop, WinWidth and WinHeight, found when the override is bound.
static INT GWinLeft=-1, GWinTop=-1, GWinWidth=-1, GWinHeight=-1;

// Holds the overrides' C++ as UObject member functions, the type of a UFunction's Func. Never instantiated: the
// functions read only their frame.
class UScriptOverrides : public UObject
{
public:
	void execLadderWinParams( FFrame& Stack, RESULT_DECL );
};

//
// static function SetupWinParams( UWindowWindow Win, UWindowRootWindow RootWin, out int W, out int H ): Epic's box
// (1024x768 on a root wider than 1024, else the root's height at 4:3) where it fits the root, else the largest 4:3
// box that fits, centred; W and H as Epic's script computes them.
//
void UScriptOverrides::execLadderWinParams( FFrame& Stack, RESULT_DECL )
{
	guard(UScriptOverrides::execLadderWinParams);
	P_GET_OBJECT(UObject,Win);
	P_GET_OBJECT(UObject,RootWin);
	P_GET_INT_REF(W);
	P_GET_INT_REF(H);
	P_FINISH;
	if( !Win || !RootWin )
		return;
	FLOAT RootW = *(FLOAT*)((BYTE*)RootWin + GWinWidth);
	FLOAT RootH = *(FLOAT*)((BYTE*)RootWin + GWinHeight);
	FLOAT Width, Height;
	if( RootW>1024.f && RootH>=768.f )
	{
		Width  = 1024.f;
		Height = 768.f;
	}
	else if( (RootH/3.f)*4.f <= RootW )
	{
		Width  = (RootH/3.f)*4.f;
		Height = RootH;
	}
	else
	{
		Width  = RootW;
		Height = (RootW/4.f)*3.f;
	}
	*(FLOAT*)((BYTE*)Win + GWinWidth)  = Width;
	*(FLOAT*)((BYTE*)Win + GWinHeight) = Height;
	*(FLOAT*)((BYTE*)Win + GWinLeft)   = (RootW - Width)/2.f;
	*(FLOAT*)((BYTE*)Win + GWinTop)    = (RootH - Height)/2.f;
	*W = (INT)(Width/4.f);
	*H = *W;
	if( *W>256 || *H>256 )
	{
		*W = 256;
		*H = 256;
	}
	unguardexec;
}

// The parameters are a UWindowWindow, a UWindowRootWindow and two out ints, with no return value; the window
// class has the four float fields the C++ uses, at offsets the root's class shares.
static UBOOL CheckLadderWinParams( UFunction* Function )
{
	guard(CheckLadderWinParams);
	if( !(Function->FunctionFlags & FUNC_Static) )
		return 0;
	UProperty* Parms[5];
	INT NumParms = 0;
	for( TFieldIterator<UProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It )
	{
		if( NumParms==ARRAY_COUNT(Parms) )
			return 0;
		Parms[NumParms++] = *It;
	}
	if( NumParms!=4 )
		return 0;
	UObjectProperty* WinParm  = Cast<UObjectProperty>( Parms[0] );
	UObjectProperty* RootParm = Cast<UObjectProperty>( Parms[1] );
	if( !WinParm || !RootParm || !WinParm->PropertyClass || !RootParm->PropertyClass )
		return 0;
	if( appStricmp( WinParm->PropertyClass->GetName(), TEXT("UWindowWindow") )!=0
	||	appStricmp( RootParm->PropertyClass->GetName(), TEXT("UWindowRootWindow") )!=0
	||	!RootParm->PropertyClass->IsChildOf( WinParm->PropertyClass ) )
		return 0;
	for( INT i=2; i<4; i++ )
		if( !Cast<UIntProperty>( Parms[i] ) || !(Parms[i]->PropertyFlags & CPF_OutParm) || (Parms[i]->PropertyFlags & CPF_ReturnParm) )
			return 0;
	UFloatProperty* Left   = FindField<UFloatProperty>( WinParm->PropertyClass, TEXT("WinLeft") );
	UFloatProperty* Top    = FindField<UFloatProperty>( WinParm->PropertyClass, TEXT("WinTop") );
	UFloatProperty* Width  = FindField<UFloatProperty>( WinParm->PropertyClass, TEXT("WinWidth") );
	UFloatProperty* Height = FindField<UFloatProperty>( WinParm->PropertyClass, TEXT("WinHeight") );
	if( !Left || !Top || !Width || !Height )
		return 0;
	GWinLeft   = Left->Offset;
	GWinTop    = Top->Offset;
	GWinWidth  = Width->Offset;
	GWinHeight = Height->Offset;
	return 1;
	unguard;
}

/*-----------------------------------------------------------------------------
	The overrides.
-----------------------------------------------------------------------------*/

struct FScriptOverride
{
	const TCHAR*	Package;		// the package, class and function names
	const TCHAR*	Class;
	const TCHAR*	Function;
	INT				ExportSize;		// the function's export in the package file: its size and appMemCrc
	DWORD			ExportCrc;
	UBOOL			(*Check)( UFunction* Function );	// the parameters (and what the C++ needs from them)
	Native			Func;			// the C++
	const TCHAR*	What;			// for the log line
};

static const FScriptOverride GScriptOverrides[] =
{
	// UT99 v400's retail UTMenu.u.
	{ TEXT("UTMenu"), TEXT("UTLadderStub"), TEXT("SetupWinParams"), 316, 0x8659E580,
	  CheckLadderWinParams, (Native)&UScriptOverrides::execLadderWinParams,
	  TEXT("the ladder windows take the largest 4:3 box that fits the menus, at most 1024x768") },
	// UT99 v436's retail UTMenu.u.
	{ TEXT("UTMenu"), TEXT("UTLadderStub"), TEXT("SetupWinParams"), 317, 0x70B532AC,
	  CheckLadderWinParams, (Native)&UScriptOverrides::execLadderWinParams,
	  TEXT("the ladder windows take the largest 4:3 box that fits the menus, at most 1024x768") },
};

// The function's export as stored in its package file, read from the file (the linker's own reader is left alone).
static UBOOL ReadExport( UFunction* Function, TArray<BYTE>& Data )
{
	guard(ReadExport);
	ULinkerLoad* Linker = Function->GetLinker();
	INT Index = Function->GetLinkerIndex();
	if( !Linker || Index<0 || Index>=Linker->ExportMap.Num() )
		return 0;
	FObjectExport& Export = Linker->ExportMap(Index);
	if( Export.SerialSize<=0 )
		return 0;
	FArchive* Reader = GFileManager->CreateFileReader( *Linker->Filename, 0, GNull );
	if( !Reader )
		return 0;
	Data.Empty();
	Data.Add( Export.SerialSize );
	UBOOL Ok = Export.SerialOffset>=0 && Export.SerialOffset+Export.SerialSize<=Reader->TotalSize();
	if( Ok )
	{
		Reader->Seek( Export.SerialOffset );
		Reader->Serialize( &Data(0), Export.SerialSize );
		Ok = !Reader->IsError();
	}
	delete Reader;
	return Ok;
	unguard;
}

//
// Binds a loaded script function to its listed C++ when it is exactly the listed one; returns whether it did.
//
UBOOL BindScriptOverride( UFunction* Function )
{
	guard(BindScriptOverride);
	if( GIsEditor || (Function->FunctionFlags & FUNC_Native) || !Function->GetLinker() )
		return 0;
	UClass* Class = Function->GetOwnerClass();
	TArray<BYTE> Data;
	UBOOL Listed = 0;
	DWORD Crc = 0;
	for( INT i=0; i<ARRAY_COUNT(GScriptOverrides); i++ )
	{
		// An entry per listed release of the function: the one whose export matches.
		const FScriptOverride& Entry = GScriptOverrides[i];
		if( appStricmp( Function->GetName(), Entry.Function )!=0
		||	!Class || appStricmp( Class->GetName(), Entry.Class )!=0
		||	appStricmp( Class->GetOuter()->GetName(), Entry.Package )!=0
		||	Function->GetOuter()!=Class )
			continue;
		if( !Listed )
		{
			Listed = 1;
			if( !ReadExport( Function, Data ) )
			{
				debugf( NAME_Init, TEXT("ufront: %s keeps its script: its export could not be read"), Function->GetFullName() );
				return 0;
			}
			Crc = appMemCrc( &Data(0), Data.Num() );
		}
		if( Data.Num()!=Entry.ExportSize || Crc!=Entry.ExportCrc )
			continue;
		if( !Entry.Check( Function ) )
		{
			debugf( NAME_Init, TEXT("ufront: %s keeps its script: its parameters are not the listed ones"), Function->GetFullName() );
			return 0;
		}
		Function->FunctionFlags |= FUNC_Native;
		Function->Func = Entry.Func;
		debugf( NAME_Init, TEXT("ufront: %s runs as C++: %s"), Function->GetFullName(), Entry.What );
		return 1;
	}
	if( Listed )
		debugf( NAME_Init, TEXT("ufront: %s keeps its script: its export (%i bytes, CRC %08X) is not a listed one"), Function->GetFullName(), Data.Num(), Crc );
	return 0;
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
