/*=============================================================================
	UnFont.cpp: Unreal font code.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "EnginePrivate.h"
#include "UnRender.h"

/*------------------------------------------------------------------------------
	UFont implementation.
------------------------------------------------------------------------------*/

UFont::UFont()
:	IsRemapped(0)
{}

void UFont::Serialize( FArchive& Ar )
{
	guard(UFont::Serialize);
	Super::Serialize( Ar );
	UBOOL GSavedLazyLoad = GLazyLoad;
	GLazyLoad = 1;
	Ar << Pages << CharactersPerPage;
	// v436: package version 69 and later append the char-remap table and its flag; v68 fonts have neither.
	if( Ar.Ver() >= 69 )
		Ar << CharRemap << IsRemapped;
	else
		IsRemapped = 0; // As v436 does, a pre-v69 font is never remapped.
	check(!(CharactersPerPage&(CharactersPerPage-1)));
	if( !GLazyLoad )
		for( INT c=0,p=0; c<256; c+=CharactersPerPage,p++ )
			if( p<Pages.Num() && Pages(p).Texture )
				for( INT i=0; i<Pages(p).Texture->Mips.Num(); i++ )
					Pages(p).Texture->Mips(i).DataArray.Load();
	GLazyLoad = GSavedLazyLoad;
	unguardobj;
}
IMPLEMENT_CLASS(UFont);

/*------------------------------------------------------------------------------
	The End.
------------------------------------------------------------------------------*/
