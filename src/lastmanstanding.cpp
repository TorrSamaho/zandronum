//-----------------------------------------------------------------------------
//
// Skulltag Source
// Copyright (C) 2004 Brad Carney
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
// Date created:  4/24/04
//
//
// Filename: lastmanstanding.cpp
//
// Description: Contains LMS routines
//
//-----------------------------------------------------------------------------

#include "a_action.h"
#include "announcer.h"
#include "c_cvars.h"
#include "cl_demo.h"
#include "cl_main.h"
#include "deathmatch.h"
#include "doomstat.h"
#include "d_event.h"
#include "g_game.h"
#include "g_level.h"
#include "gamemode.h"
#include "gi.h"
#include "gstrings.h"
#include "joinqueue.h"
#include "lastmanstanding.h"
#include "network.h"
#include "p_effect.h"
#include "sbar.h"
#include "st_hud.h"
#include "sv_commands.h"
#include "team.h"
#include "v_video.h"

//*****************************************************************************
//	MISC CRAP THAT SHOULDN'T BE HERE BUT HAS TO BE BECAUSE OF SLOPPY CODING

void	SERVERCONSOLE_UpdatePlayerInfo( LONG lPlayer, ULONG ulUpdateFlags );

//*****************************************************************************
//	VARIABLES

static	ULONG		g_ulLMSCountdownTicks = 0;
static	ULONG		g_ulLMSMatches = 0;
static	bool		g_bStartNextMatchOnLevelLoad = false;
static	LMSSTATE_e	g_LMSState;

//*****************************************************************************
//	FUNCTIONS

void LASTMANSTANDING_Construct( void )
{
	g_LMSState = LMSS_WAITINGFORPLAYERS;
}

//*****************************************************************************
//
void LASTMANSTANDING_Tick( void )
{
	// Not in LMS mode.
	if (( lastmanstanding == false ) && ( teamlms == false ))
		return;

	switch ( g_LMSState )
	{
	case LMSS_WAITINGFORPLAYERS:
	case LMSS_PRENEXTROUNDCOUNTDOWN:

		if ( NETWORK_InClientMode() )
		{
			break;
		}

		// [AK] Set the state back to waiting for players if there's not enough
		// players and we're in the pre-next round countdown state.
		if ((( lastmanstanding ) && ( GAME_CountActivePlayers( ) >= 2 )) ||
			(( teamlms ) && ( TEAM_TeamsWithPlayersOn( ) > 1 )))
		{
			if ( sv_lmscountdowntime > 0 )
				LASTMANSTANDING_StartCountdown(( sv_lmscountdowntime * TICRATE ) - 1 );
			else
				LASTMANSTANDING_StartCountdown(( 10 * TICRATE ) - 1 );
		}
		else if ( LASTMANSTANDING_GetState( ) == LMSS_PRENEXTROUNDCOUNTDOWN )
		{
			LASTMANSTANDING_SetState( LMSS_WAITINGFORPLAYERS );
		}

		break;
	case LMSS_COUNTDOWN:
	case LMSS_NEXTROUNDCOUNTDOWN:

		if ( g_ulLMSCountdownTicks )
		{
			g_ulLMSCountdownTicks--;

			// FIGHT!
			if (( g_ulLMSCountdownTicks == 0 ) &&
				( NETWORK_InClientMode() == false ))
			{
				LASTMANSTANDING_DoFight( );
			}
			// Play "3... 2... 1..." sounds.
			else if ( g_ulLMSCountdownTicks == ( 3 * TICRATE ))
				ANNOUNCER_PlayEntry( cl_announcer, "Three" );
			else if ( g_ulLMSCountdownTicks == ( 2 * TICRATE ))
				ANNOUNCER_PlayEntry( cl_announcer, "Two" );
			else if ( g_ulLMSCountdownTicks == ( 1 * TICRATE ))
				ANNOUNCER_PlayEntry( cl_announcer, "One" );
		}
		break;
	case LMSS_INPROGRESS:

		if ( NETWORK_InClientMode() )
		{
			break;
		}

		// Check to see how many men are left standing.
		if ( lastmanstanding )
		{
			// If only one man is left standing, somebody just won!
			if ( GAME_CountLivingAndRespawnablePlayers( ) == 1 )
			{
				LONG	lWinner;

				lWinner = LASTMANSTANDING_GetLastManStanding( );
				if ( lWinner != -1 )
				{
					NETWORK_Printf( "%s wins!\n", players[lWinner].userinfo.GetName() );

					if (( NETWORK_GetState() != NETSTATE_SERVER ) && ( lWinner == consoleplayer ))
						ANNOUNCER_PlayEntry( cl_announcer, "YouWin" );

					// Give the winner a win.
					PLAYER_SetWins( &players[lWinner], players[lWinner].ulWins + 1 );

					// Pause for five seconds for the win sequence.
					LASTMANSTANDING_DoWinSequence( lWinner );
					GAME_SetEndLevelDelay( 5 * TICRATE );
				}
			}
			// If NOBODY is left standing, it's a draw game!
			else if ( GAME_CountLivingAndRespawnablePlayers( ) == 0 )
			{
				ULONG	ulIdx;

				for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
				{
					if (( playeringame[ulIdx] ) && ( PLAYER_IsTrueSpectator( &players[ulIdx] ) == false ))
						break;
				}

				if ( ulIdx != MAXPLAYERS )
				{
					NETWORK_Printf( "DRAW GAME!\n" );

					// Pause for five seconds for the win sequence.
					LASTMANSTANDING_DoWinSequence( MAXPLAYERS );
					GAME_SetEndLevelDelay( 5 * TICRATE );
				}
			}
		}

		// Check to see how many men are left standing on each team.
		if ( teamlms )
		{
			if ( LASTMANSTANDING_TeamsWithAlivePlayersOn( ) <= 1)
			{
				LONG	lWinner;

				lWinner = LASTMANSTANDING_TeamGetLastManStanding( );
				if ( lWinner != -1 )
				{
					NETWORK_Printf( "%s wins!\n", TEAM_GetName( lWinner ));

					if ( NETWORK_GetState() != NETSTATE_SERVER
						&& players[consoleplayer].bOnTeam
						&& lWinner == (LONG)players[consoleplayer].Team )
					{
						ANNOUNCER_PlayEntry( cl_announcer, "YouWin" );
					}

					// Give the team a win.
					TEAM_SetWinCount( lWinner, TEAM_GetWinCount( lWinner ) + 1, false );

					// [BB] Every player who is still alive also gets a win.
					for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
					{
						if ( playeringame[ulIdx] && ( players[ulIdx].bOnTeam ) && ( players[ulIdx].bSpectating == false ) && PLAYER_IsAliveOrCanRespawn ( &players[ulIdx] ) )
							PLAYER_SetWins( &players[ulIdx], players[ulIdx].ulWins + 1 );
					}

					// Pause for five seconds for the win sequence.
					LASTMANSTANDING_DoWinSequence( lWinner );
					GAME_SetEndLevelDelay( 5 * TICRATE );
				}
				// If NOBODY is left standing, it's a draw game!
				else
				{
					ULONG	ulIdx;

					for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
					{
						if (( playeringame[ulIdx] ) && ( PLAYER_IsTrueSpectator( &players[ulIdx] ) == false ))
							break;
					}

					if ( ulIdx != MAXPLAYERS )
					{
						NETWORK_Printf( "DRAW GAME!\n" );

						// Pause for five seconds for the win sequence.
						LASTMANSTANDING_DoWinSequence( teams.Size( ) );
						GAME_SetEndLevelDelay( 5 * TICRATE );
					}
				}
			}
		}
		break;
	default:
		break;
	}
}

//*****************************************************************************
//
LONG LASTMANSTANDING_GetLastManStanding( void )
{
	ULONG	ulIdx;

	// Not in lastmanstanding mode.
	if ( lastmanstanding == false )
		return ( -1 );

	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if ( playeringame[ulIdx] && ( players[ulIdx].bSpectating == false ) && PLAYER_IsAliveOrCanRespawn ( &players[ulIdx] ) )
			return ( ulIdx );
	}

	return ( -1 );
}

//*****************************************************************************
//
LONG LASTMANSTANDING_TeamGetLastManStanding( void )
{
	ULONG	ulIdx;

	// Not in team lastmanstanding mode.
	if ( teamlms == false )
		return ( -1 );

	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if ( playeringame[ulIdx] && ( players[ulIdx].bOnTeam ) && ( players[ulIdx].bSpectating == false ) && PLAYER_IsAliveOrCanRespawn ( &players[ulIdx] ) )
			return ( players[ulIdx].Team );
	}

	return ( -1 );
}

//*****************************************************************************
//
LONG LASTMANSTANDING_TeamsWithAlivePlayersOn( void )
{
	LONG lTeamsWithAlivePlayersOn = 0;

	for ( ULONG i = 0; i < teams.Size( ); i++ )
	{
		if (TEAM_CountLivingAndRespawnablePlayers (i) > 0)
			lTeamsWithAlivePlayersOn ++;
	}

	return lTeamsWithAlivePlayersOn;
}

//*****************************************************************************
//
void LASTMANSTANDING_StartCountdown( ULONG ulTicks )
{
	ULONG	ulIdx;

	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if (( playeringame[ulIdx] ) && ( players[ulIdx].pSkullBot ))
			players[ulIdx].pSkullBot->PostEvent( BOTEVENT_LMS_STARTINGCOUNTDOWN );
	}

/*
	// First, reset everyone's fragcount. This must be done before setting the state to LMSS_COUNTDOWN
	// otherwise PLAYER_SetFragcount will ignore our request.
	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if ( playeringame[ulIdx] )
			PLAYER_SetFragcount( &players[ulIdx], 0, false, false );
	}
*/
/*
	for ( ULONG i = 0; i < teams.Size( ); i++ )
		TEAM_SetFragCount( i, 0, false );
*/
	// Put the game in a countdown state.
	if ( NETWORK_InClientMode() == false )
	{
		LASTMANSTANDING_SetState( LASTMANSTANDING_GetState( ) == LMSS_PRENEXTROUNDCOUNTDOWN ? LMSS_NEXTROUNDCOUNTDOWN : LMSS_COUNTDOWN );
	}

	// Set the LMS countdown ticks.
	LASTMANSTANDING_SetCountdownTicks( ulTicks );

	// Announce that the fight will soon start.
	ANNOUNCER_PlayEntry( cl_announcer, LASTMANSTANDING_GetState( ) == LMSS_COUNTDOWN ? "PrepareToFight" : "NextRoundIn" );

	// Tell clients to start the countdown.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVERCOMMANDS_DoGameModeCountdown( ulTicks );
}

//*****************************************************************************
//
void LASTMANSTANDING_DoFight( void )
{
	// The match is now in progress.
	if ( NETWORK_InClientMode() == false )
	{
		LASTMANSTANDING_SetState( LMSS_INPROGRESS );
	}

	// Make sure this is 0. Can be non-zero in network games if they're slightly out of sync.
	g_ulLMSCountdownTicks = 0;

	// Since the level time is being reset, also reset the last frag/excellent time for
	// each player.
	PLAYER_ResetAllPlayersSpecialCounters();

	// Tell clients to "fight!".
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVERCOMMANDS_DoGameModeFight( 0 );

	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
	{
		// Play fight sound.
		ANNOUNCER_PlayEntry( cl_announcer, "Fight" );

		// Display "FIGHT!" HUD message.
		HUD_DrawStandardMessage( "FIGHT!", CR_RED, true, 2.0f, 1.0f );
	}
	// Display a little thing in the server window so servers can know when matches begin.
	else
		Printf( "FIGHT!\n" );

	// Reset the map.
	GAME_ResetMap( );
	GAMEMODE_RespawnAllPlayers( BOTEVENT_LMS_FIGHT );

	HUD_ShouldRefreshBeforeRendering( );
}

//*****************************************************************************
//
void LASTMANSTANDING_DoWinSequence( ULONG ulWinner )
{
	ULONG	ulIdx;

	// Put the game state in the win sequence state.
	if ( NETWORK_InClientMode() == false )
	{
		LASTMANSTANDING_SetState( LMSS_WINSEQUENCE );
	}

	// Tell clients to do the win sequence.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVERCOMMANDS_DoGameModeWinSequence( ulWinner );

	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
	{
		FString message;
		EColorRange color = CR_GREEN;

		if ( teamlms )
		{
			if ( ulWinner == teams.Size( ))
			{
				message = "Draw Game!";
			}
			else
			{
				message.Format( "%s Wins!", TEAM_GetName( ulWinner ));
				color = static_cast<EColorRange>( TEAM_GetTextColor( ulWinner ));
			}
		}
		else if ( ulWinner == MAXPLAYERS )
		{
			message = "DRAW GAME!";
		}
		else
		{
			message.Format( "%s WINS!", players[ulWinner].userinfo.GetName( ));
			color = CR_RED;
		}

		// Display "%s WINS!" HUD message.
		HUD_DrawStandardMessage( message, color );

		// [AK] Clear the frag and place HUD messages from the screen.
		HUD_ClearFragAndPlaceMessages( false );
	}

	// Award a victory or perfect medal to the winner.
	// If the winner has full health, give him a "Perfect!".
	if (( lastmanstanding ) && ( NETWORK_InClientMode( ) == false ))
		MEDAL_GiveMedal( ulWinner, players[ulWinner].health == deh.MegasphereHealth ? "Perfect" : "Victory" );

	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if (( playeringame[ulIdx] ) && ( players[ulIdx].pSkullBot ))
			players[ulIdx].pSkullBot->PostEvent( BOTEVENT_LMS_WINSEQUENCE );
	}
}

//*****************************************************************************
//
void LASTMANSTANDING_TimeExpired( void )
{
	LONG				lHighestHealth = 0;
	bool				bTie = false;
	bool				bFoundPlayer = false;
	LONG				lWinner = -1;

	// Don't end the level if we're not in a game.
	if ( LASTMANSTANDING_GetState( ) != LMSS_INPROGRESS )
		return;

	// Try to find the player with the highest health.
	if ( lastmanstanding )
	{
		TArray<ULONG> possibleWinners;
		for ( unsigned int i = 0; i < MAXPLAYERS; ++i )
			possibleWinners.Push ( i );

		// [BB] Find the player with the most lives left.
		PLAYER_SelectPlayersWithHighestValue ( PLAYER_GetLivesLeft, possibleWinners );
		// [BB] If more than one player has the most lives left, select the player with the highest lives.
		if ( possibleWinners.Size() != 1 )
			PLAYER_SelectPlayersWithHighestValue ( PLAYER_GetHealth, possibleWinners );

		// [BB] If more then one player have the most lives and also the same health, then the game it a tie.
		if ( possibleWinners.Size() == 1 )
			lWinner = possibleWinners[0];
		else
		{
			lWinner = MAXPLAYERS;
			bTie = true;
		}
	}
	else if ( teamlms )
	{
		if ( LASTMANSTANDING_TeamsWithAlivePlayersOn( ) == 1 )
		{
			for ( ULONG i = 0; i < teams.Size( ); i++ )
			{
				if ( TEAM_CountLivingAndRespawnablePlayers( i ) )
					lWinner = i;
			}
		}
		else
		{
			lWinner = teams.Size( );
			bTie = true;
		}
	}

	// If for some reason we don't have any players, just end the map like normal.
	if ( lWinner == -1 )
	{
		NETWORK_Printf( "%s\n", GStrings( "TXT_TIMELIMIT" ));
		GAME_SetEndLevelDelay( 5 * TICRATE );
		return;
	}

	// If there was a tie, then go into sudden death!
	if ( bTie )
	{
		// Only print the message the instant we reach sudden death.
		if ( level.time == static_cast<int>( timelimit * TICRATE * 60 ))
			HUD_DrawStandardMessage( "SUDDEN DEATH!", CR_GREEN, false, 3.0f, 2.0f, true );

		return;
	}

	// Also, do the win sequence for the player.
	LASTMANSTANDING_DoWinSequence( lWinner );

	// Give the winner a win.
	if ( lastmanstanding )
		PLAYER_SetWins( &players[lWinner], players[lWinner].ulWins + 1 );
	// [BB] In a team game of course give the win to the winning team.
	if ( teamlms )
		TEAM_SetWinCount( lWinner, TEAM_GetWinCount( lWinner ) + 1, false );

	NETWORK_Printf( "%s\n", GStrings( "TXT_TIMELIMIT" ));
	GAME_SetEndLevelDelay( 5 * TICRATE );
}

//*****************************************************************************
//
bool LASTMANSTANDING_IsWeaponDisallowed( const PClass *pType )
{
	// [BB] Not in LMS mode, everything is allowed.
	if (( lastmanstanding == false ) && ( teamlms == false ))
		return false;

	if ((( lmsallowedweapons & LMS_AWF_CHAINSAW ) == false ) &&
		( pType == PClass::FindClass( "Chainsaw" )))
	{
		return true;
	}
	if ((( lmsallowedweapons & LMS_AWF_PISTOL ) == false ) &&
		( pType == PClass::FindClass( "Pistol" )))
	{
		return true;
	}
	if ((( lmsallowedweapons & LMS_AWF_SHOTGUN ) == false ) &&
		( pType == PClass::FindClass( "Shotgun" )))
	{
		return true;
	}
	if (( pType == PClass::FindClass( "SuperShotgun" )) &&
		((( lmsallowedweapons & LMS_AWF_SSG ) == false ) ||
		(( gameinfo.flags & GI_MAPxx ) == false )))
	{
		return true;
	}
	if ((( lmsallowedweapons & LMS_AWF_CHAINGUN ) == false ) &&
		( pType == PClass::FindClass( "Chaingun" )))
	{
		return true;
	}
	if ((( lmsallowedweapons & LMS_AWF_MINIGUN ) == false ) &&
		( pType == PClass::FindClass( "Minigun" )))
	{
		return true;
	}
	if ((( lmsallowedweapons & LMS_AWF_ROCKETLAUNCHER ) == false ) &&
		( pType == PClass::FindClass( "RocketLauncher" )))
	{
		return true;
	}
	if ((( lmsallowedweapons & LMS_AWF_GRENADELAUNCHER ) == false ) &&
		( pType == PClass::FindClass( "GrenadeLauncher" )))
	{
		return true;
	}
	if ((( lmsallowedweapons & LMS_AWF_PLASMA ) == false ) &&
		( pType == PClass::FindClass( "PlasmaRifle" )))
	{
		return true;
	}
	if ((( lmsallowedweapons & LMS_AWF_RAILGUN ) == false ) &&
		( pType == PClass::FindClass( "Railgun" )))
	{
		return true;
	}

	return false;
}

//*****************************************************************************
//*****************************************************************************
//
ULONG LASTMANSTANDING_GetCountdownTicks( void )
{
	return ( g_ulLMSCountdownTicks );
}

//*****************************************************************************
//
void LASTMANSTANDING_SetCountdownTicks( ULONG ulTicks )
{
	g_ulLMSCountdownTicks = ulTicks;
}

//*****************************************************************************
//
LMSSTATE_e LASTMANSTANDING_GetState( void )
{
	return ( g_LMSState );
}

//*****************************************************************************
//
void LASTMANSTANDING_SetState( LMSSTATE_e State )
{
	ULONG	ulIdx;

	// [AK] Try clearing the winner's name and score from the scoreboard, but
	// only after the end of a round.
	SCOREBOARD_TryClearingWinnerAndScore( true );

	g_LMSState = State;

	// Tell clients about the state change.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVERCOMMANDS_SetGameModeState( State, g_ulLMSCountdownTicks );

	switch ( State )
	{
	case LMSS_WINSEQUENCE:

		break;
	case LMSS_WAITINGFORPLAYERS:
	case LMSS_PRENEXTROUNDCOUNTDOWN:

		// Zero out the countdown ticker.
		LASTMANSTANDING_SetCountdownTicks( 0 );

		if ( lastmanstanding || teamlms )
			GAMEMODE_RespawnDeadPlayersAndPopQueue( );

		break;
	default:
		break;
	}

	// Since some players might have respawned, update the server console window.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
	{
		for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
		{
			if ( playeringame[ulIdx] )
				SERVERCONSOLE_UpdatePlayerInfo( ulIdx, UDF_FRAGS );
		}
	}
}

//*****************************************************************************
//
bool LASTMANSTANDING_GetStartNextMatchOnLevelLoad( void )
{
	return ( g_bStartNextMatchOnLevelLoad );
}

//*****************************************************************************
//
void LASTMANSTANDING_SetStartNextMatchOnLevelLoad( bool bStart )
{
	g_bStartNextMatchOnLevelLoad = bStart;
}

//*****************************************************************************
//	CONSOLE COMMANDS/VARIABLES

CVAR( Int, sv_lmscountdowntime, 10, CVAR_ARCHIVE | CVAR_GAMEPLAYSETTING );
CUSTOM_CVAR( Int, winlimit, 0, CVAR_SERVERINFO | CVAR_CAMPAIGNLOCK | CVAR_GAMEPLAYSETTING )
{
	if ( self >= 256 )
		self = 255;
	if ( self < 0 )
		self = 0;

	// [AK] Update the clients and update the server console.
	SERVER_SettingChanged( self, true );
}

// [AK] Added CVAR_GAMEPLAYFLAGSET.
CUSTOM_CVAR( Int, lmsallowedweapons, LMS_AWF_ALLALLOWED, CVAR_SERVERINFO | CVAR_GAMEPLAYFLAGSET )
{
	SERVER_FlagsetChanged( self );
}
CVAR( Flag, lms_allowpistol, lmsallowedweapons, LMS_AWF_PISTOL );
CVAR( Flag, lms_allowshotgun, lmsallowedweapons, LMS_AWF_SHOTGUN );
CVAR( Flag, lms_allowssg, lmsallowedweapons, LMS_AWF_SSG );
CVAR( Flag, lms_allowchaingun, lmsallowedweapons, LMS_AWF_CHAINGUN );
CVAR( Flag, lms_allowminigun, lmsallowedweapons, LMS_AWF_MINIGUN );
CVAR( Flag, lms_allowrocketlauncher, lmsallowedweapons, LMS_AWF_ROCKETLAUNCHER );
CVAR( Flag, lms_allowgrenadelauncher, lmsallowedweapons, LMS_AWF_GRENADELAUNCHER );
CVAR( Flag, lms_allowplasma, lmsallowedweapons, LMS_AWF_PLASMA );
CVAR( Flag, lms_allowrailgun, lmsallowedweapons, LMS_AWF_RAILGUN );
CVAR( Flag, lms_allowchainsaw, lmsallowedweapons, LMS_AWF_CHAINSAW );

// [AK] Added CVAR_GAMEPLAYFLAGSET.
CUSTOM_CVAR( Int, lmsspectatorsettings, LMS_SPF_VIEW, CVAR_SERVERINFO | CVAR_GAMEPLAYFLAGSET )
{
	// [AK] If LMS_SPF_VIEW is disabled and we're spying on an enemy player,
	// revert our view back to our own eyes.
	if (( self & LMS_SPF_VIEW ) == false )
	{
		for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
		{
			if (( players[ulIdx].mo != NULL ) && ( players[ulIdx].mo->IsTeammate( players[ulIdx].camera ) == false ))
			{
				players[ulIdx].camera = players[ulIdx].mo;

				S_UpdateSounds( players[ulIdx].camera );

				// [AK] The server doesn't have a status bar.
				if ( StatusBar != NULL )
				{
					StatusBar->AttachToPlayer( &players[ulIdx] );

					if ( demoplayback || ( NETWORK_GetState( ) != NETSTATE_SINGLE ))
						StatusBar->ShowPlayerName( );
				}
			}
		}
	}

	SERVER_FlagsetChanged( self );
}

CVAR( Flag, lms_spectatorchat, lmsspectatorsettings, LMS_SPF_CHAT );
CVAR( Flag, lms_spectatorview, lmsspectatorsettings, LMS_SPF_VIEW );
CVAR( Flag, lms_spectatorvoicechat, lmsspectatorsettings, LMS_SPF_VOICECHAT );
