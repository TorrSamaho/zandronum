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
// Filename: maprotation.h
//
// Description: The server's list of maps to play.
//
//-----------------------------------------------------------------------------

#ifndef	__MAPROTATION_H__
#define	__MAPROTATION_H__

#include "g_level.h"
#include "c_dispatch.h"

//*****************************************************************************
//	STRUCTURES

struct MapRotationEntry
{
	// The map.
	level_info_t	*map;

	// Has this map already been used in the rotation?
	bool			isUsed;

	// [AK] The minimum number of players required to enter this map.
	unsigned int	minPlayers;

	// [AK] The maximum number of players allowed to enter this map.
	unsigned int	maxPlayers;
};

//*****************************************************************************
//	PROTOTYPES

void			MAPROTATION_Construct( void );
void			MAPROTATION_StartNewGame( void );
unsigned int	MAPROTATION_CountEligiblePlayers( void );
unsigned int	MAPROTATION_GetNumEntries( void );
unsigned int	MAPROTATION_GetCurrentPosition( void );
unsigned int	MAPROTATION_GetNextPosition( void );
void			MAPROTATION_SetCurrentPosition( unsigned int position );
void			MAPROTATION_SetNextPosition( unsigned int position, const bool ignoreLimits );
bool			MAPROTATION_ShouldNextMapIgnoreLimits( void );
bool			MAPROTATION_CanEnterMap( unsigned int position, unsigned int playerCount );
void			MAPROTATION_CalcNextMap( const bool updateClients );
level_info_t	*MAPROTATION_GetNextMap( void );
level_info_t	*MAPROTATION_GetMap( unsigned int position );
unsigned int	MAPROTATION_GetPlayerLimits( unsigned int position, bool getMaxPlayers );
void			MAPROTATION_SetPositionToMap( const char *mapName, const bool setNextMap );
bool			MAPROTATION_IsMapInRotation( const char *mapName );
bool			MAPROTATION_IsUsed( unsigned int position );
void			MAPROTATION_SetUsed( unsigned int position, bool used = true );
void			MAPROTATION_AddMap( FCommandLine &argv, bool silent, bool insert = false );
void			MAPROTATION_AddMap( const char *mapName, int position, unsigned int minPlayers, unsigned int maxPlayers, bool silent );
void			MAPROTATION_DelMap( const char *mapName, bool silent );

//*****************************************************************************
//  EXTERNAL CONSOLE VARIABLES

EXTERN_CVAR( Bool, sv_maprotation )
EXTERN_CVAR( Bool, sv_randommaprotation )

#endif	// __MAPROTATION_H__
