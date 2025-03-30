//-----------------------------------------------------------------------------
//
// Skulltag Source
// Copyright (C) 2003-2005 Brad Carney
// Copyright (C) 2007-2012 Skulltag Development Team
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
// 3. Neither the name of the Skulltag Development Team nor the names of its
//    contributors may be used to endorse or promote products derived from this
//    software without specific prior written permission.
// 4. Redistributions in any form must be accompanied by information on how to
//    obtain complete source code for the software and any accompanying
//    software that uses the software. The source code must either be included
//    in the distribution or be available for no more than the cost of
//    distribution plus a nominal fee, and must be freely redistributable
//    under reasonable conditions. For an executable file, complete source
//    code means the source code for all modules it contains. It does not
//    include source code for modules or files that typically accompany the
//    major components of the operating system on which the executable file
//    runs.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// File created:  3/26/05
//
//
// Filename: sv_save.cpp
//
// Description: Saves players' scores when they leave the server, and restores it when they return.
//
//-----------------------------------------------------------------------------

#include "sv_save.h"
#include "v_text.h"

//*****************************************************************************
//	VARIABLES

// Global list of saved information.
static	SavedPlayerInfo		g_SavedPlayerInfo[MAXPLAYERS];

//*****************************************************************************
//	PROTOTYPES

void	server_save_UpdateSlotWithInfo( unsigned int slot, SavedPlayerInfo &info );

//*****************************************************************************
//	FUNCTIONS

void SERVER_SAVE_Construct( void )
{
	// Initialzed the saved player info list.
	SERVER_SAVE_ClearList( );
}

//*****************************************************************************
//
SavedPlayerInfo *SERVER_SAVE_GetSavedInfo( const char *playerName, NETADDRESS_s address )
{
	FString name = playerName;
	V_RemoveColorCodes( name );

	for ( unsigned int i = 0; i < MAXPLAYERS; i++ )
	{
		if ( g_SavedPlayerInfo[i].isInitialized == false )
			continue;

		if (( g_SavedPlayerInfo[i].name.CompareNoCase( name ) == 0 ) && ( address.Compare( g_SavedPlayerInfo[i].address )))
			return ( &g_SavedPlayerInfo[i] );
	}

	return ( nullptr );
}

//*****************************************************************************
//
void SERVER_SAVE_ClearInfo( SavedPlayerInfo &info )
{
	info.address.Clear( );
	info.isInitialized = false;
	info.fragCount = 0;
	info.pointCount = 0;
	info.winCount = 0;
	info.deathCount = 0;
	info.name = "";
	info.time = 0;
}

//*****************************************************************************
//
void SERVER_SAVE_ClearList( void )
{
	for ( unsigned int i = 0; i < MAXPLAYERS; i++ )
		SERVER_SAVE_ClearInfo( g_SavedPlayerInfo[i] );
}

//*****************************************************************************
//
void SERVER_SAVE_SaveInfo( SavedPlayerInfo &info )
{
	FString name = info.name;
	V_RemoveColorCodes( name );

	for ( unsigned int i = 0; i < MAXPLAYERS; i++ )
	{
		if ( g_SavedPlayerInfo[i].isInitialized )
		{
			// If this slot matches the player we're trying to save, just update it.
			if (( g_SavedPlayerInfo[i].name.CompareNoCase( name ) == 0 ) && ( info.address.Compare( g_SavedPlayerInfo[i].address )))
			{
				server_save_UpdateSlotWithInfo( i, info );
				return;
			}

			continue;
		}

		server_save_UpdateSlotWithInfo( i, info );
		return;
	}
}

//*****************************************************************************
//*****************************************************************************
//
void server_save_UpdateSlotWithInfo( unsigned int slot, SavedPlayerInfo &info )
{
	if ( slot >= MAXPLAYERS )
		return;

	g_SavedPlayerInfo[slot].isInitialized = true;
	g_SavedPlayerInfo[slot].address = info.address;
	g_SavedPlayerInfo[slot].fragCount = info.fragCount;
	g_SavedPlayerInfo[slot].pointCount = info.pointCount;
	g_SavedPlayerInfo[slot].winCount = info.winCount;
	g_SavedPlayerInfo[slot].deathCount = info.deathCount;
	g_SavedPlayerInfo[slot].time = info.time;
	g_SavedPlayerInfo[slot].name = info.name;

	V_RemoveColorCodes( g_SavedPlayerInfo[slot].name );
}
