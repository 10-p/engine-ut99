/*=============================================================================
	AWeapon.h.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

	// Constructors.
	AWeapon() {}

	// AActor interface.
	// v436-compat port: v436 replicates this nativereplication class's properties in C++.
	INT* GetOptimizedRepList( BYTE* InDefault, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, INT NumReps );

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
