/*=============================================================================
	ACarcass.h.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

	// Constructors.
	ACarcass() {}

	// AActor interface.
	// v436: a resting carcass replicates at no more than 4 Hz.
	virtual FLOAT UpdateFrequency( AActor* Viewer, FVector& ViewDir, FVector& ViewPos );

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
