/*=============================================================================
	UnCheats.cpp: the position cheats WHEREAMI and TELEPORT. ufront port; not
	Epic's. Ported from engine-uc2 (Source/Engine/Src/UC2Cheats.cpp); the names,
	the format and the guard are the cross-engine spec of engine-uc2's
	docs/CHEAT-COMMANDS.md, shared by every engine of the frontend.

	Console commands (the console, a key binding, the web's ue_exec_command):
	  WHEREAMI                         the local player's position and view:
	                                   "<x> <y> <z> <yaw> <pitch>" (x y z: world units, one
	                                   decimal, the pawn's location; yaw 0..65535, pitch
	                                   -32768..32767: Unreal rotation units, the VIEW
	                                   rotation), on the console, on the screen
	                                   (ClientMessage) and in the log as
	                                   "Cheat: WHEREAMI <x> <y> <z> <yaw> <pitch>"
	  TELEPORT <x> <y> <z> [<yaw> [<pitch>]]
	                                   puts the local player's pawn there (WHEREAMI's output
	                                   pasted back gives the same place and view); without yaw
	                                   and pitch the view is kept. TELEPORT with no arguments is
	                                   not ours (the game's own, where it has one).

	The guard: allowed for the local player of a standalone game and of a listen server
	(the host is the server); every network client and a dedicated server refuse:
	"Cheat: <COMMAND> refused: ..." in the log and on the console. Test harnesses set
	[Engine.GameEngine] AllowNetCheats=True in that process's ini (-ini:System:...); it
	must never be in a shipped ini. Every command that runs logs one "Cheat: ..." line.

	Called first by UGameEngine::Exec: the web's ue_exec_command reaches the engine
	only, never the viewport, and the typed console ends there too (UViewport::Exec ->
	UPlayer::Exec -> Engine->Exec); no script of this game has a TELEPORT.
=============================================================================*/

#include "EnginePrivate.h"

// "Cheat: " is part of the text, on NAME_Log (never suppressed). The console (Ar) gets the short form,
// unless it is the log itself.
static void CheatLog( FOutputDevice& Ar, const TCHAR* Text )
{
	debugf( NAME_Log, TEXT("Cheat: %s"), Text );
	if( &Ar != GLog )
		Ar.Log( Text );
}

static APlayerPawn* FirstLocalPlayer( UEngine* Engine )
{
	if( Engine && Engine->Client && Engine->Client->Viewports.Num() && Engine->Client->Viewports(0) )
		return Engine->Client->Viewports(0)->Actor;
	return NULL;
}

static FString WhereAmI( APlayerPawn* P )
{
	INT Pitch = P->ViewRotation.Pitch & 65535;
	if( Pitch >= 32768 )
		Pitch -= 65536;
	return FString::Printf( TEXT("%.1f %.1f %.1f %i %i"), P->Location.X, P->Location.Y, P->Location.Z,
		P->ViewRotation.Yaw & 65535, Pitch );
}

// May cheats run for this player now? Logs the refusal, or the "Cheat:" line of the command that runs.
static UBOOL CheatsAllowed( APlayerPawn* P, const TCHAR* Command, const TCHAR* Args, FOutputDevice& Ar )
{
	INT NetMode = ( P && P->Level ) ? P->Level->NetMode : NM_DedicatedServer;
	UBOOL ByKey = 0;
	UBOOL Allowed = P && ( NetMode==NM_Standalone || NetMode==NM_ListenServer );
	if( !Allowed )
	{
		UBOOL NetCheats = 0;
		GConfig->GetBool( TEXT("Engine.GameEngine"), TEXT("AllowNetCheats"), NetCheats );
		Allowed = ByKey = NetCheats;
	}
	if( !Allowed )
	{
		CheatLog( Ar, *FString::Printf( TEXT("%s refused: cheats are only for a standalone game or the host of a listen server"), Command ) );
		return 0;
	}
	while( Args && *Args==' ' )
		Args++;
	debugf( NAME_Log, TEXT("Cheat: %s %s%s"), Command, Args ? Args : TEXT(""), ByKey ? TEXT(" (AllowNetCheats: a test setting)") : TEXT("") );
	return 1;
}

ENGINE_API UBOOL UfrontCheatExec( UEngine* Engine, const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(UfrontCheatExec);
	const TCHAR* Str = Cmd;
	if( ParseCommand( &Str, TEXT("WHEREAMI") ) )
	{
		APlayerPawn* P = FirstLocalPlayer( Engine );
		if( !P || P->bDeleteMe )
		{
			if( CheatsAllowed( P, TEXT("WHEREAMI"), TEXT(""), Ar ) )
				CheatLog( Ar, TEXT("WHEREAMI failed: no local player") );
			return 1;
		}
		FString Pos = WhereAmI( P );
		if( !CheatsAllowed( P, TEXT("WHEREAMI"), *Pos, Ar ) )
			return 1;
		if( &Ar != GLog )
			Ar.Log( *Pos );
		P->eventClientMessage( Pos, NAME_None, 0 );
		return 1;
	}
	if( ParseCommand( &Str, TEXT("TELEPORT") ) )
	{
		FString Tok[5];
		INT N = 0;
		const TCHAR* Args = Str;
		while( N<5 && ParseToken( Str, Tok[N], 0 ) )
			N++;
		if( N==0 )
			return 0;	// TELEPORT alone: not ours
		APlayerPawn* P = FirstLocalPlayer( Engine );
		if( !CheatsAllowed( P, TEXT("TELEPORT"), Args, Ar ) )
			return 1;
		if( N<3 )
		{
			CheatLog( Ar, TEXT("TELEPORT failed: usage TELEPORT <x> <y> <z> [<yaw> [<pitch>]]") );
			return 1;
		}
		if( !P || P->bDeleteMe || !P->GetLevel() || P->Level->NetMode==NM_Client )
		{
			CheatLog( Ar, TEXT("TELEPORT failed: no local pawn this process moves (a network client cannot)") );
			return 1;
		}
		FVector To( appAtof(*Tok[0]), appAtof(*Tok[1]), appAtof(*Tok[2]) );
		UBOOL Moved = P->GetLevel()->FarMoveActor( P, To, 0, 0 );
		P->Velocity = FVector(0,0,0);
		P->Acceleration = FVector(0,0,0);
		if( N>=4 )
		{
			INT Yaw = appAtoi( *Tok[3] );
			INT Pitch = N>=5 ? appAtoi( *Tok[4] ) : 0;
			P->Rotation = FRotator( 0, Yaw, 0 );
			P->ViewRotation = FRotator( Pitch, Yaw, 0 );
		}
		CheatLog( Ar, *FString::Printf( TEXT("TELEPORT %s: %s"), Moved ? TEXT("ok") : TEXT("failed (no room there)"), *WhereAmI( P ) ) );
		return 1;
	}
	return 0;
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
