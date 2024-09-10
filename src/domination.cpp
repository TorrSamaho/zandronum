//-----------------------------------------------------------------------------
//
// Skulltag Source
// Copyright (C) 2008 Braden Obrzut
// Copyright (C) 2008-2012 Skulltag Development Team
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
// Filename: domination.cpp
//
// Description:
//
//-----------------------------------------------------------------------------

#include "gamemode.h"

#include "doomtype.h"
#include "doomstat.h"
#include "v_font.h"
#include "v_palette.h"
#include "v_video.h"
#include "v_text.h"
#include "chat.h"
#include "st_stuff.h"
#include "domination.h"

#include "g_level.h"
#include "network.h"
#include "r_defs.h"
#include "sv_commands.h"
#include "team.h"
#include "sectinfo.h"
#include "cl_demo.h"
#include "p_acs.h"
#include "gi.h"

// [TRSR] Private helper function(s)
static void domination_SetControlPointColor( unsigned int point );

CUSTOM_CVAR(Int, sv_dominationscorerate, 3, CVAR_SERVERINFO | CVAR_GAMEPLAYSETTING)
{
	if ( self <= 0 )
		self = 1;
};

// These default the default fade for points.  I hope no one has a grey team...
#define POINT_DEFAULT_R	0x7F
#define POINT_DEFAULT_G	0x7F
#define POINT_DEFAULT_B	0x7F

EXTERN_CVAR(Bool, domination)

//CREATE_GAMEMODE(domination, DOMINATION, "Domination", "DOM", "F1_DOM", GMF_TEAMGAME|GMF_PLAYERSEARNPOINTS|GMF_PLAYERSONTEAMS)

bool finished;

void DOMINATION_Reset(void)
{
	if(!domination)
		return;

	finished = false;

	for(unsigned int i = 0;i < level.info->SectorInfo.Points.Size();i++)
	{
		level.info->SectorInfo.Points[i].owner = TEAM_None;
		domination_SetControlPointColor( i );
	}
}

void DOMINATION_Init(void)
{
	if(!domination)
		return;

	finished = false;

	DOMINATION_Reset();
}

void DOMINATION_Tick(void)
{
	if(!domination)
		return;

	if(finished)
		return;

	// [BB] Scoring is server-side.
	// [TRSR] Control point management is also server-side.
	if ( NETWORK_InClientMode() )
		return;

	for( unsigned int i = 0; i < level.info->SectorInfo.Points.Size(); i++ )
	{
		unsigned int teamPlayers[MAX_TEAMS] = { 0 };

		// [TRSR] Count number of players per team on the point.
		for ( unsigned int p = 0; p < MAXPLAYERS; p++ ) {
			if ( !level.info->SectorInfo.Points[i].PlayerInsidePoint( p ) )
				continue;

			if( !players[p].bOnTeam )
				continue;

			// [TRSR] Call event script to allow modders to say whether this player's contesting status counts.
			// For example, if player is too high up or above 3D floor, a modder may not want them to be able to contest.
			if (( !gameinfo.bAllowDominationContestScripts ) || ( GAMEMODE_HandleEvent( GAMEEVENT_DOMINATION_CONTEST, players[p].mo, i, 0, true ) != 0 ))
				teamPlayers[players[p].Team]++;
		}

		// [TRSR] If the point is owned and one of that team's players is contesting, don't let the point swap.
		if( level.info->SectorInfo.Points[i].owner != TEAM_None && teamPlayers[level.info->SectorInfo.Points[i].owner] > 0 )
			continue;

		// [TRSR] Figure out which team has the most contesters. Point will swap to them.
		unsigned int winner = TEAM_None;
		unsigned int max = 0;
		for ( int team = 0; team < MAX_TEAMS; team++ )
		{
			if( teamPlayers[team] > max )
			{
				max = teamPlayers[team];
				winner = team;
			}
			// [TRSR] If two teams are tied, neither gets the point.
			// A bit awkward, but it gives a resolution to priority issues.
			else if ( teamPlayers[team] == max )
			{
				winner = TEAM_None;
			}
		}

		if( winner == TEAM_None )
			continue;

		// [TRSR] Trigger an event to allow denying of the capture.
		if ( GAMEMODE_HandleEvent( GAMEEVENT_DOMINATION_PRECONTROL, nullptr, winner, i, true ) == 0 )
			continue;

		DOMINATION_SetOwnership( i, winner );
	}

	if(!(level.maptime % (sv_dominationscorerate * TICRATE)))
	{
		for(unsigned int i = 0;i < level.info->SectorInfo.Points.Size();i++)
		{
			if(level.info->SectorInfo.Points[i].owner != TEAM_None)
			{
				// [AK] Trigger an event script when this team gets a point from a point sector.
				// The first argument is the team that owns the sector and the second argument is the name
				// of the sector. Don't let event scripts change the result value to anything less than zero.
				LONG lResult = MAX<LONG>( GAMEMODE_HandleEvent( GAMEEVENT_DOMINATION_POINT, NULL, level.info->SectorInfo.Points[i].owner, i, true ), 0 );
				
				if ( lResult != 0 )
					TEAM_SetPointCount( level.info->SectorInfo.Points[i].owner, TEAM_GetPointCount( level.info->SectorInfo.Points[i].owner ) + lResult, false );
			}
		}
	}

	// [TRSR] Check this every tick in case a modder gives score via non-control point means.
	if( pointlimit )
	{
		for( int team = 0; team < MAX_TEAMS; team++ )
		{
			if( TEAM_GetPointCount( team ) >= pointlimit )
			{
				DOMINATION_WinSequence(0);
				break;
			}
		}
	}
}

void DOMINATION_WinSequence(unsigned int winner)
{
	if(!domination)
		return;

	finished = true;
}

void DOMINATION_SetOwnership(unsigned int point, unsigned int team, bool broadcast)
{
	if(!domination)
		return;

	if(point >= level.info->SectorInfo.Points.Size())
		return;

	if ( !TEAM_CheckIfValid( team ) && team != TEAM_None )
		return;

	// [TRSR] Need to save previous team for event script below.
	int prevTeam = level.info->SectorInfo.Points[point].owner;
	if( team == prevTeam )
		return;

	if ( broadcast ) {
		if( team != TEAM_None ) {
			Printf( "\034%s%s" TEXTCOLOR_NORMAL " has taken control of %s.\n", TEAM_GetTextColorName( team ), TEAM_GetName( team ), level.info->SectorInfo.Points[point].name.GetChars() );
		} else {
			Printf( "\034%s%s" TEXTCOLOR_NORMAL " has lost control of %s.\n", TEAM_GetTextColorName( prevTeam ), TEAM_GetName( prevTeam ), level.info->SectorInfo.Points[point].name.GetChars() );
		}
	}

	level.info->SectorInfo.Points[point].owner = team;
	domination_SetControlPointColor( point );

	// [TRSR] Trigger an event script when a team takes ownership of a point sector.
	GAMEMODE_HandleEvent( GAMEEVENT_DOMINATION_CONTROL, nullptr, prevTeam, point );

	// [TRSR] Let clients know about the change in management too.
	if ( NETWORK_GetState() == NETSTATE_SERVER )
		SERVERCOMMANDS_SetDominationPointOwner ( point, team, broadcast );
}

static void domination_SetControlPointColor( unsigned int point )
{
	if (( !domination ) || ( point >= level.info->SectorInfo.Points.Size( )))
		return;

	for ( unsigned int i = 0; i < level.info->SectorInfo.Points[point].sectors.Size(); i++ )
	{
		unsigned int secnum = level.info->SectorInfo.Points[point].sectors[i];

		if ( secnum >= static_cast<unsigned>( numsectors ))
			continue;

		if ( level.info->SectorInfo.Points[point].owner != TEAM_None )
		{
			int color = TEAM_GetColor( level.info->SectorInfo.Points[point].owner );
			sectors[secnum].SetFade( RPART( color ), GPART( color ), BPART( color ));
		}
		else
		{
			sectors[secnum].SetFade( POINT_DEFAULT_R, POINT_DEFAULT_G, POINT_DEFAULT_B );
		}
	}
}

//END_GAMEMODE(DOMINATION)
