//-----------------------------------------------------------------------------
//
// Skulltag Source
// Copyright (C) 2003 Brad Carney
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
//
//
// Filename: maprotation.cpp
//
// Description: The server's list of maps to play.
//
//-----------------------------------------------------------------------------

#include <string.h>
#include <vector>
#include "c_cvars.h"
#include "c_dispatch.h"
#include "g_level.h"
#include "m_random.h"
#include "maprotation.h"
#include "p_setup.h"
#include "joinqueue.h"
#include "sv_main.h"

//*****************************************************************************
//	VARIABLES

std::vector<MAPROTATIONENTRY_t>	g_MapRotationEntries;

static	ULONG					g_ulCurMapInList;
static	ULONG					g_ulNextMapInList;

//*****************************************************************************
//	FUNCTIONS

void MAPROTATION_Construct( void )
{
	g_MapRotationEntries.clear( );
	g_ulCurMapInList = g_ulNextMapInList = 0;
}

//*****************************************************************************
//
ULONG MAPROTATION_GetNumEntries( void )
{
	return g_MapRotationEntries.size( );
}

//*****************************************************************************
//
ULONG MAPROTATION_GetCurrentPosition( void )
{
	return ( g_ulCurMapInList );
}

//*****************************************************************************
//
bool MAPROTATION_CanEnterMap( ULONG ulIdx, ULONG ulPlayerCount )
{
	if ( ulIdx >= g_MapRotationEntries.size( ))
		return ( false );

	return (( g_MapRotationEntries[ulIdx].ulMinPlayers <= ulPlayerCount ) && ( g_MapRotationEntries[ulIdx].ulMaxPlayers >= ulPlayerCount ));
}

//*****************************************************************************
//
static bool MAPROTATION_MapHasLowestOrHighestLimit( ULONG ulIdx, ULONG ulLowest, ULONG ulHighest, bool bUseMax )
{
	if ( ulIdx >= g_MapRotationEntries.size( ))
		return ( false );

	if ( bUseMax )
		return ( g_MapRotationEntries[ulIdx].ulMaxPlayers == ulHighest );
	else
		return ( g_MapRotationEntries[ulIdx].ulMinPlayers == ulLowest );
}

//*****************************************************************************
//
static bool MAPROTATION_GetLowestAndHighestLimits( ULONG ulPlayerCount, ULONG &ulLowest, ULONG &ulHighest )
{
	bool bUseMaxLimit = false;
	ulLowest = MAXPLAYERS;
	ulHighest = 1;

	// [AK] Get the lowest min player limit and highest max player limit from the list.
	for ( unsigned int i = 0; i < g_MapRotationEntries.size( ); i++ )
	{
		if ( g_MapRotationEntries[i].ulMinPlayers < ulLowest )
			ulLowest = g_MapRotationEntries[i].ulMinPlayers;

		if ( g_MapRotationEntries[i].ulMaxPlayers > ulHighest )
			ulHighest = g_MapRotationEntries[i].ulMaxPlayers;

		// [AK] If there's any map where the player count exceeds the min limit, then use the max limit.
		if ( ulPlayerCount >= g_MapRotationEntries[i].ulMinPlayers )
			bUseMaxLimit = true;
	}

	return ( bUseMaxLimit );
}

//*****************************************************************************
//
static void MAPROTATION_CalcNextMap( void )
{
	if ( g_MapRotationEntries.empty( ))
		return;

	ULONG ulPlayerCount = 0;
	ULONG ulLowestLimit;
	ULONG ulHighestLimit;
	bool bUseMaxLimit;

	// [AK] We only want to count players who are already playing or are in the join queue.
	for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if (( playeringame[ulIdx] ) && (( !players[ulIdx].bSpectating ) || ( JOINQUEUE_GetPositionInLine( ulIdx ) != -1 )))
			ulPlayerCount++;
	}

	// [BB] The random selection is only necessary if there is more than one map.
	if ( sv_randommaprotation && ( g_MapRotationEntries.size( ) > 1 ) )
	{
		// If all the maps have been played, make them all available again. 
		{
			bool bAllMapsPlayed = true;
			for ( ULONG ulIdx = 0; ulIdx < g_MapRotationEntries.size( ); ulIdx++ )
			{
				// [AK] Ignore rotation entries that we can't select due to player limits.
				if ( MAPROTATION_CanEnterMap( ulIdx, ulPlayerCount ) == false )
					continue;

				if ( !g_MapRotationEntries[ulIdx].bUsed )			
				{
					bAllMapsPlayed = false;
					break;
				}
			}
			
			if ( bAllMapsPlayed )
			{
				for ( ULONG ulIdx = 0; ulIdx < g_MapRotationEntries.size( ); ulIdx++ )
					g_MapRotationEntries[ulIdx].bUsed = false;
			}
		}

		// Select a new map.
		std::vector<unsigned int> unusedEntries;
		for ( unsigned int i = 0; i < g_MapRotationEntries.size( ); ++i )
		{
			// [AK] Only select maps that we can enter with the current number of players.
			if (( g_MapRotationEntries[i].bUsed == false ) && ( MAPROTATION_CanEnterMap( i, ulPlayerCount )))
				unusedEntries.push_back ( i );
		}

		// [AK] If we can't select any maps because the player count exceeds all limits, we'll just select the map with the lowest
		// lowest min player or highest max player limit, based on if there's too few or too many players.
		if ( unusedEntries.empty( ))
		{
			bUseMaxLimit = MAPROTATION_GetLowestAndHighestLimits( ulPlayerCount, ulLowestLimit, ulHighestLimit );
			for ( unsigned int i = 0; i < g_MapRotationEntries.size( ); i++ )
			{
				if ( MAPROTATION_MapHasLowestOrHighestLimit( i, ulLowestLimit, ulHighestLimit, bUseMaxLimit ))
					unusedEntries.push_back ( i );
			}
		}

		g_ulNextMapInList = unusedEntries[ M_Random ( unusedEntries.size() ) ];
	}
	else
	{
		g_ulNextMapInList = g_ulCurMapInList + 1;
		g_ulNextMapInList = ( g_ulNextMapInList % MAPROTATION_GetNumEntries( ));

		// [AK] Check if the next map in the list can be entered with the current number of players.
		if (( g_MapRotationEntries.size( ) > 1 ) && ( MAPROTATION_CanEnterMap( g_ulNextMapInList, ulPlayerCount ) == false ))
		{
			ULONG ulOldMapInList = g_ulNextMapInList;
			bool bNothingFound = false;

			do
			{
				// [AK] Cycle through the entire list until we find a map that can be entered.
				g_ulNextMapInList = ++g_ulNextMapInList % g_MapRotationEntries.size( );

				// [AK] We went through the entire list and couldn't find a valid map.
				if ( g_ulNextMapInList == ulOldMapInList )
				{
					bNothingFound = true;
					break;
				}
			}
			while ( MAPROTATION_CanEnterMap( g_ulNextMapInList, ulPlayerCount ) == false );

			if ( bNothingFound )
			{
				bUseMaxLimit = MAPROTATION_GetLowestAndHighestLimits( ulPlayerCount, ulLowestLimit, ulHighestLimit );
				g_ulNextMapInList = ulOldMapInList;

				// [AK] Find the next map in the list with the lowest min player or highest max player limit.
				while ( MAPROTATION_MapHasLowestOrHighestLimit( g_ulNextMapInList, ulLowestLimit, ulHighestLimit, bUseMaxLimit ) == false )
				{
					g_ulNextMapInList = ++g_ulNextMapInList % g_MapRotationEntries.size( );
					if ( g_ulNextMapInList == ulOldMapInList )
						break;
				}
			}
		}
	}
}

//*****************************************************************************
//
void MAPROTATION_AdvanceMap( bool bMarkUsed )
{
	g_ulCurMapInList = g_ulNextMapInList;
	if (( bMarkUsed ) && ( g_ulCurMapInList < g_MapRotationEntries.size( )))
		g_MapRotationEntries[g_ulCurMapInList].bUsed = true;
}

//*****************************************************************************
//
level_info_t *MAPROTATION_GetNextMap( void )
{
	// [BB] If we don't want to use the rotation, there is no scheduled next map.
	if (( sv_maprotation == false ) || ( g_MapRotationEntries.empty( )))
		return NULL;

	// [BB] See if we need to calculate the next map.
	if ( g_ulNextMapInList == g_ulCurMapInList )
		MAPROTATION_CalcNextMap();

	return ( g_MapRotationEntries[g_ulNextMapInList].pMap );
}

//*****************************************************************************
//
level_info_t *MAPROTATION_GetMap( ULONG ulIdx )
{
	if ( ulIdx >= g_MapRotationEntries.size( ))
		return ( NULL );

	return ( g_MapRotationEntries[ulIdx].pMap );
}

//*****************************************************************************
//
ULONG MAPROTATION_GetPlayerLimits( ULONG ulIdx, bool bMaxPlayers )
{
	if ( ulIdx >= g_MapRotationEntries.size( ))
		return ( 0 );

	return ( bMaxPlayers ? g_MapRotationEntries[ulIdx].ulMaxPlayers : g_MapRotationEntries[ulIdx].ulMinPlayers );
}

//*****************************************************************************
//
void MAPROTATION_SetPositionToMap( const char *pszMapName )
{
	for ( ULONG ulIdx = 0; ulIdx < g_MapRotationEntries.size( ); ulIdx++ )
	{
		if ( stricmp( g_MapRotationEntries[ulIdx].pMap->mapname, pszMapName ) == 0 )
		{
			g_ulCurMapInList = ulIdx;
			g_MapRotationEntries[g_ulCurMapInList].bUsed = true;
			break;
		}
	}
	g_ulNextMapInList = g_ulCurMapInList;
}

//*****************************************************************************
//
bool MAPROTATION_IsMapInRotation( const char *pszMapName )
{
	for ( ULONG ulIdx = 0; ulIdx < g_MapRotationEntries.size( ); ulIdx++ )
	{
		if ( stricmp( g_MapRotationEntries[ulIdx].pMap->mapname, pszMapName ) == 0 )
			return true;
	}
	return false;
}

//*****************************************************************************
//
bool MAPROTATION_IsUsed( ULONG ulIdx )
{
	if ( ulIdx >= g_MapRotationEntries.size( ))
		return ( false );

	return ( g_MapRotationEntries[ulIdx].bUsed );
}

//*****************************************************************************
//
void MAPROTATION_AddMap( FCommandLine &argv, bool bSilent, bool bInsert )
{
	// Find the map.
	level_info_t *pMap = FindLevelByName( argv[1] );
	if ( pMap == NULL )
	{
		Printf( "map %s doesn't exist.\n", argv[1] );
		return;
	}

	MAPROTATIONENTRY_t newEntry;
	newEntry.pMap = pMap;
	newEntry.bUsed = false;

	int iPosition = bInsert ? atoi( argv[2] ) : 0;
	int iLimitArg = bInsert ? 3 : 2;

	// [AK] Add the minimum and maximum player limits the map will use.
	newEntry.ulMinPlayers = ( argv.argc( ) > iLimitArg) ? clamp( atoi( argv[iLimitArg] ), 0, MAXPLAYERS ) : 0;
	newEntry.ulMaxPlayers = ( argv.argc( ) > iLimitArg + 1 ) ? clamp( atoi( argv[iLimitArg + 1] ), 1, MAXPLAYERS ) : MAXPLAYERS;

	// [AK] The minimum limit should never be greater than the maximum limit.
	if ( newEntry.ulMinPlayers > newEntry.ulMaxPlayers )
		swapvalues( newEntry.ulMinPlayers, newEntry.ulMaxPlayers );

	// [Dusk] iPosition of 0 implies the end of the maplist.
	if (iPosition == 0) {
		// Add it to the queue.
		g_MapRotationEntries.push_back( newEntry );
		
		// [Dusk] note down the position for output
		iPosition = g_MapRotationEntries.end() - g_MapRotationEntries.begin();
	} else {
		// [Dusk] insert the map into a certain position
		std::vector<MAPROTATIONENTRY_t>::iterator itPosition = g_MapRotationEntries.begin() + iPosition - 1;

		// sanity check.
		if (itPosition < g_MapRotationEntries.begin () || itPosition > g_MapRotationEntries.end ()) {
			Printf ("Bad index specified!\n");
			return;
		}

		g_MapRotationEntries.insert( itPosition, 1, newEntry );
	}

	MAPROTATION_SetPositionToMap( level.mapname );
	if ( !bSilent )
		Printf( "%s (%s) added to map rotation list at position %d.\n", pMap->mapname, pMap->LookupLevelName( ).GetChars( ), iPosition);
}

//*****************************************************************************
// [Dusk] Removes a map from map rotation
void MAPROTATION_DelMap (char *pszMapName, bool bSilent)
{
	// look up the map
	level_info_t *pMap = FindLevelByName (pszMapName);
	if (pMap == NULL)
	{
		Printf ("map %s doesn't exist.\n", pszMapName);
		return;
	}

	// search the map in the map rotation and throw it to trash
	level_info_t entry;
	std::vector<MAPROTATIONENTRY_t>::iterator iterator;
	bool gotcha = false;
	for (iterator = g_MapRotationEntries.begin (); iterator < g_MapRotationEntries.end (); iterator++)
	{
		entry = *iterator->pMap;
		if (!stricmp(entry.mapname, pszMapName)) {
			g_MapRotationEntries.erase (iterator);
			gotcha = true;
			break;
		}
	}

	if (gotcha && !bSilent)
	{
		Printf ("%s (%s) has been removed from map rotation list.\n",
			pMap->mapname, pMap->LookupLevelName().GetChars());
	}
	else if (!gotcha)
		Printf ("Map %s is not in rotation.\n", pszMapName);
}

//*****************************************************************************
//	CONSOLE COMMANDS

CCMD( addmap )
{
	if ( argv.argc( ) > 1 )
		MAPROTATION_AddMap( argv, false );
	else
		Printf( "addmap <lumpname> [minplayers] [maxplayers]: Adds a map to the map rotation list.\n" );
}

CCMD( addmapsilent ) // Backwards API needed for server console, RCON.
{
	if ( argv.argc( ) > 1 )
		MAPROTATION_AddMap( argv, true );
	else
		Printf( "addmapsilent <lumpname> [minplayers] [maxplayers]: Silently adds a map to the map rotation list.\n" );
}

//*****************************************************************************
//
CCMD( maplist )
{
	if ( g_MapRotationEntries.size( ) == 0 )
		Printf( "The map rotation list is empty.\n" );
	else
	{
		FString message;

		Printf( "Map rotation list: \n" );
		for ( ULONG ulIdx = 0; ulIdx < g_MapRotationEntries.size( ); ulIdx++ )
		{
			message.Format( "%lu. %s - %s", ulIdx + 1, g_MapRotationEntries[ulIdx].pMap->mapname, g_MapRotationEntries[ulIdx].pMap->LookupLevelName( ).GetChars( ));

			// [AK] Also print the min and max player limits if they're different from the default values.
			if (( g_MapRotationEntries[ulIdx].ulMinPlayers > 0 ) || ( g_MapRotationEntries[ulIdx].ulMaxPlayers < MAXPLAYERS ))
			{
				message += " (";

				if ( g_MapRotationEntries[ulIdx].ulMinPlayers > 0 )
					message.AppendFormat( "min = %lu", g_MapRotationEntries[ulIdx].ulMinPlayers );

				if ( g_MapRotationEntries[ulIdx].ulMaxPlayers < MAXPLAYERS )
				{
					if ( g_MapRotationEntries[ulIdx].ulMinPlayers > 0 )
						message += ", ";

					message.AppendFormat( "max = %lu", g_MapRotationEntries[ulIdx].ulMaxPlayers );
				}

				message += ')';
			}

			Printf( "%s\n", message );
		}
	}
}

//*****************************************************************************
//
CCMD( clearmaplist )
{
	// Reset the map list.
	MAPROTATION_Construct( );

	Printf( "Map rotation list cleared.\n" );
}

// [Dusk] delmap
CCMD (delmap) {
	if (argv.argc() > 1)
		MAPROTATION_DelMap (argv[1], false);
	else
		Printf ("delmap <lumpname>: Removes a map from the map rotation list.\n");
}

CCMD (delmapsilent) {
	if (argv.argc() > 1)
		MAPROTATION_DelMap (argv[1], true);
	else
		Printf ("delmapsilent <lumpname>: Silently removes a map from the map rotation list.\n");
}

CCMD (delmap_idx) {
	if (argv.argc() <= 1)
	{
		Printf ("delmap_idx <idx>: Removes a map from the map rotation list based in index number.\nUse maplist to list the rotation with index numbers.\n");
		return;
	}

	unsigned int idx = static_cast<unsigned int> ( ( atoi(argv[1]) - 1 ) );
	if ( idx >= g_MapRotationEntries.size() )
	{
		Printf ("No such map!\n");
		return;
	}

	Printf ("%s (%s) has been removed from map rotation list.\n",	g_MapRotationEntries[idx].pMap->mapname, g_MapRotationEntries[idx].pMap->LookupLevelName().GetChars());
	g_MapRotationEntries.erase (g_MapRotationEntries.begin()+idx);
}

//*****************************************************************************
// [Dusk] insertmap
CCMD (insertmap) {
	if ( argv.argc( ) > 2 )
		MAPROTATION_AddMap( argv, false, true );
	else
	{
		Printf( "insertmap <lumpname> <position> [minplayers] [maxplayers]: Inserts a map to the map rotation list, after <position>.\n"
			"Use maplist to list the rotation with index numbers.\n" );
	}
}

CCMD (insertmapsilent) {
	if ( argv.argc( ) > 2 )
		MAPROTATION_AddMap( argv, true, true );
	else
	{
		Printf( "insertmapsilent <lumpname> <position> [minplayers] [maxplayers]: Silently inserts a map to the map rotation list, after <position>.\n"
			"Use maplist to list the rotation with index numbers.\n" );
	}
}

//*****************************************************************************
//	CONSOLE VARIABLES

CVAR( Bool, sv_maprotation, true, CVAR_ARCHIVE );
CVAR( Bool, sv_randommaprotation, false, CVAR_ARCHIVE );
