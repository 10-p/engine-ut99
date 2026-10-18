/*=============================================================================
	ALevelInfo.h.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

	// Constructors.
	ALevelInfo() {}

	// AActor interface.
	// As v436 does, this nativereplication class replicates its properties in C++.
	INT* GetOptimizedRepList( BYTE* InDefault, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, INT NumReps );

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
