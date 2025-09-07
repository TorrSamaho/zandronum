//-----------------------------------------------------------------------------
//
// Skulltag Source
// Copyright (C) 2007 Brad Carney
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
// Date created:  7/12/07
//
//
// Filename: gamemode.cpp
//
// Description: 
//
//-----------------------------------------------------------------------------

#include "cooperative.h"
#include "deathmatch.h"
#include "domination.h"
#include "doomstat.h"
#include "d_event.h"
#include "gamemode.h"
#include "team.h"
#include "network.h"
#include "sv_commands.h"
#include "g_game.h"
#include "joinqueue.h"
#include "cl_demo.h"
#include "survival.h"
#include "duel.h"
#include "invasion.h"
#include "lastmanstanding.h"
#include "possession.h"
#include "p_lnspec.h"
#include "p_acs.h"
#include "gi.h"

//*****************************************************************************
//	CONSOLE VARIABLES

CVAR( Bool, instagib, false, CVAR_SERVERINFO | CVAR_LATCH | CVAR_CAMPAIGNLOCK | CVAR_GAMEPLAYSETTING );
CVAR( Bool, buckshot, false, CVAR_SERVERINFO | CVAR_LATCH | CVAR_CAMPAIGNLOCK | CVAR_GAMEPLAYSETTING );

CVAR( Bool, sv_suddendeath, true, CVAR_SERVERINFO | CVAR_LATCH | CVAR_GAMEPLAYSETTING );

CUSTOM_CVAR( Int, sv_maxlives, 0, CVAR_SERVERINFO | CVAR_LATCH | CVAR_GAMEPLAYSETTING )
{
	// [AK] Limit the maximum number of lives to 255. This should be more than enough.
	if ( self > UCHAR_MAX )
	{
		self = UCHAR_MAX;
		return;
	}
	else if ( self < 0 )
	{
		self = 0;
		return;
	}

	// [AK] Notify the clients about the change.
	SERVER_SettingChanged( self, false );
}

// [AM] Set or unset a map as being a "lobby" map.
CUSTOM_CVAR( String, lobby, "", CVAR_SERVERINFO )
{
	if ( strcmp( *self, "" ) == 0 )
	{
		// Lobby map is empty.  Tell the client that if necessary.
		if (( NETWORK_GetState( ) == NETSTATE_SERVER ) && ( gamestate != GS_STARTUP ))
		{
			SERVER_Printf( PRINT_HIGH, "%s unset\n", self.GetName( ));
			SERVERCOMMANDS_SetGameModeLimits( );
		}
	}
	else
	{
		// Prevent setting a lobby map that doesn't exist.
		level_info_t *map = FindLevelByName( *self );
		if ( map == NULL )
		{
			Printf( "map %s doesn't exist.\n", *self );
			self = "";
			return;
		}

		// Update the client about the lobby map if necessary.
		SERVER_SettingChanged( self, false );
	}
}

//*****************************************************************************
//	VARIABLES

// Data for all our game modes.
static	GAMEMODE_s				g_GameModes[NUM_GAMEMODES];

// Our current game mode.
static	GAMEMODE_e				g_CurrentGameMode;

// [AK] The result value of the current event being executed.
static	LONG					g_lEventResult = 1;

// [BB] Implement the string table and the conversion functions for the GMF and GAMEMODE enums.
#define GENERATE_ENUM_STRINGS  // Start string generation
#include "gamemode_enums.h"
#undef GENERATE_ENUM_STRINGS   // Stop string generation

//*****************************************************************************
//	FUNCTIONS

bool GAMEPLAYSETTING_s::IsOutOfScope( void )
{
	if ( Scope != GAMESCOPE_OFFLINEANDONLINE )
	{
		// [AK] "offlineonly" settings are not applied in online games.
		if (( Scope == GAMESCOPE_OFFLINEONLY ) && ( NETWORK_GetState( ) == NETSTATE_SERVER ))
			return true;

		// [AK] "onlineonly" settings are not applied in offline games.
		if (( Scope == GAMESCOPE_ONLINEONLY ) && ( NETWORK_GetState( ) != NETSTATE_SERVER ))
			return true;
	}

	return false;
}

//*****************************************************************************
//
void GAMEMODE_Tick( void )
{
	static GAMESTATE_e oldState = GAMESTATE_UNSPECIFIED;
	const GAMESTATE_e state = GAMEMODE_GetState();

	// [BB] If the state change, potentially trigger an event and update the saved state.
	if ( oldState != state )
	{
		// [BB] Apparently the round just ended.
		if ( ( oldState == GAMESTATE_INPROGRESS ) && ( state == GAMESTATE_INRESULTSEQUENCE ) )
			GAMEMODE_HandleEvent ( GAMEEVENT_ROUND_ENDS );
		// [BB] Changing from GAMESTATE_INPROGRESS to anything but GAMESTATE_INRESULTSEQUENCE means the roudn was aborted.
		else if ( oldState == GAMESTATE_INPROGRESS )
			GAMEMODE_HandleEvent ( GAMEEVENT_ROUND_ABORTED );
		// [BB] Changing from anything to GAMESTATE_INPROGRESS means the round started.
		else if ( state == GAMESTATE_INPROGRESS )
			GAMEMODE_HandleEvent ( GAMEEVENT_ROUND_STARTS );			

		oldState = state;
	}
}

//*****************************************************************************
//
void GAMEMODE_ParseGameModeBlock( FScanner &sc, const GAMEMODE_e GameMode )
{
	sc.MustGetStringName( "{" );

	while ( !sc.CheckString( "}" ))
	{
		sc.MustGetString( );

		if ( stricmp( sc.String, "removeflag" ) == 0 )
		{
			g_GameModes[GameMode].ulFlags &= ~sc.MustGetEnumName( "flag", "GMF_", GetValueGMF );
		}
		else if ( stricmp( sc.String, "addflag" ) == 0 )
		{
			g_GameModes[GameMode].ulFlags |= sc.MustGetEnumName( "flag", "GMF_", GetValueGMF );
		}
		else if ( stricmp( sc.String, "name" ) == 0 )
		{
			sc.MustGetString( );
			g_GameModes[GameMode].Name = sc.String;
		}
		else if ( stricmp( sc.String, "shortname" ) == 0 )
		{
			sc.MustGetString( );
			g_GameModes[GameMode].ShortName = sc.String;

			// [AK] Limit the short name to only 8 characters.
			g_GameModes[GameMode].ShortName.Truncate( 8 );
		}
		else if ( stricmp( sc.String, "f1texture" ) == 0 )
		{
			sc.MustGetString( );
			g_GameModes[GameMode].F1Texture = sc.String;

			// [AK] The F1 texture cannot exceed more than 8 characters.
			g_GameModes[GameMode].F1Texture.Truncate( 8 );
		}
		else if ( stricmp( sc.String, "welcomesound" ) == 0 )
		{
			sc.MustGetString( );
			g_GameModes[GameMode].WelcomeSound = sc.String;
		}
		else if (( stricmp( sc.String, "gamesettings" ) == 0 ) || ( stricmp( sc.String, "lockedgamesettings" ) == 0 ))
		{
			GAMEMODE_ParseGameSettingBlock( sc, GameMode, !stricmp( sc.String, "lockedgamesettings" ));
		}
		else if ( stricmp( sc.String, "removegamesetting" ) == 0 )
		{
			sc.MustGetString( );
			FBaseCVar *pCVar = FindCVar( sc.String, NULL );

			// [AK] Make sure that this CVar exists.
			if ( pCVar == NULL )
				sc.ScriptError( "'%s' is not a CVar.", sc.String );
			
			for ( unsigned int i = 0; i < g_GameModes[GameMode].GameplaySettings.Size( ); i++ )
			{
				if ( pCVar == g_GameModes[GameMode].GameplaySettings[i].pCVar )
				{
					g_GameModes[GameMode].GameplaySettings.Delete( i );
					break;
				}
			}
		}
		else
		{
			sc.ScriptError( "Unknown option '%s', on line %d in GAMEMODE.", sc.String, sc.Line );
		}
	}
}

//*****************************************************************************
//
void GAMEMODE_ParseGameSettingBlock( FScanner &sc, const GAMEMODE_e GameMode, bool bLockCVars, bool bResetCVars )
{
	GAMESCOPE_e Scope = GAMESCOPE_OFFLINEANDONLINE;
	sc.MustGetStringName( "{" );
	
	// [AK] If this is the start of a "defaultgamesettings" or "defaultlockedgamesettings" block, empty the CVar
	// list for all game modes. We don't want to do this more than once in a single GAMEMODE lump in case both
	// blocks are declared in the same lump.
	if (( GameMode == NUM_GAMEMODES ) && ( bResetCVars ))
	{
		for ( unsigned int mode = GAMEMODE_COOPERATIVE; mode < NUM_GAMEMODES; mode++ )
			g_GameModes[mode].GameplaySettings.Clear( );
	}

	// [AK] Keep looping until we exited out of all blocks.
	while ( true )
	{
		sc.MustGetString( );

		// [AK] "offlineonly" or "onlineonly" indicate the start of a new subblock and scope. CVars added into
		// either of these subblocks are only set in offline or online games respectively.
		if (( stricmp( sc.String, "offlineonly" ) == 0 ) || ( stricmp( sc.String, "onlineonly" ) == 0 ))
		{
			// [AK] Don't start a new subblock while in the middle of another subblock.
			if ( Scope != GAMESCOPE_OFFLINEANDONLINE )
				sc.ScriptError( "Tried to start a new \"%s\" subblock in the middle of an \"%s\" subblock.", sc.String, Scope == GAMESCOPE_OFFLINEONLY ? "offlineonly" : "onlineonly" );
			
			Scope = ( stricmp( sc.String, "offlineonly" ) == 0 ) ? GAMESCOPE_OFFLINEONLY : GAMESCOPE_ONLINEONLY;
			sc.MustGetStringName( "{" );
			continue;
		}
		// [AK] This indicates the closing of a (sub)block.
		else if ( stricmp( sc.String, "}" ) == 0 )
		{
			// [AK] If we're not in an "offlineonly" or "onlineonly" subblock, then exit out of the game settings block entirely.
			if ( Scope == GAMESCOPE_OFFLINEANDONLINE )
				break;
			
			Scope = GAMESCOPE_OFFLINEANDONLINE;
			continue;
		}

		FBaseCVar *pCVar = FindCVar( sc.String, NULL );

		// [AK] Make sure that this CVar exists.
		if ( pCVar == NULL )
			sc.ScriptError( "'%s' is not a CVar.", sc.String );

		// [AK] Only CVars with the CVAR_GAMEPLAYSETTING flag are acceptable. If it's a flag CVar, then only
		// the flagset CVar needs the flag. Mask CVars aren't allowed to keep this implementation simple.
		if ( pCVar->IsFlagCVar( ) == false )
		{
			if (( pCVar->GetFlags( ) & CVAR_GAMEPLAYSETTING ) == false )
			{
				if ( pCVar->GetFlags( ) & CVAR_GAMEPLAYFLAGSET )
					sc.ScriptError( "Only include flag CVars belonging to '%s' in the game settings block.", pCVar->GetName( ));
				else
					sc.ScriptError( "'%s' cannot be used in a game settings block.", pCVar->GetName( ));
			}
		}
		else if (( static_cast<FFlagCVar *>( pCVar )->GetValueVar( )->GetFlags( ) & CVAR_GAMEPLAYFLAGSET ) == false )
		{
			sc.ScriptError( "'%s' is a flag that cannot be used in a game settings block.", pCVar->GetName( ));
		}

		// [AK] There must be an equal sign and value after the name of the CVar.
		sc.MustGetStringName( "=" );
		sc.MustGetString( );

		GAMEPLAYSETTING_s Setting;
		Setting.pCVar = pCVar;

		switch ( pCVar->GetRealType( ))
		{
			case CVAR_Bool:
			case CVAR_Dummy:
			{
				if ( stricmp( sc.String, "true" ) == 0 )
					Setting.Val.Bool = true;
				else if ( stricmp( sc.String, "false" ) == 0 )
					Setting.Val.Bool = false;
				else
					Setting.Val.Bool = !!atoi( sc.String );

				Setting.Type = CVAR_Bool;
				break;
			}

			case CVAR_Float:
			{
				Setting.Val.Float = static_cast<float>( atof( sc.String ));
				Setting.Type = CVAR_Float;
				break;
			}

			default:
			{
				Setting.Val.Int = atoi( sc.String );
				Setting.Type = CVAR_Int;
				break;
			}
		}

		Setting.DefaultVal = Setting.Val;
		Setting.bIsLocked = bLockCVars;

		for ( unsigned int mode = GAMEMODE_COOPERATIVE; mode < NUM_GAMEMODES; mode++ )
		{
			// [AK] If this CVar was added inside a "defaultgamesettings" or "defaultlockedgamesettings" block, apply
			// it to all the game modes. Otherwise, just apply it to the one we specified.
			if (( GameMode == NUM_GAMEMODES ) || ( GameMode == static_cast<GAMEMODE_e>( mode )))
			{
				bool bPushToList = true;
				Setting.Scope = Scope;

				// [AK] Check if this CVar is already in the list. We don't want to have multiple copies of the same CVar.
				for ( unsigned int i = 0; i < g_GameModes[mode].GameplaySettings.Size( ); i++ )
				{
					if ( g_GameModes[mode].GameplaySettings[i].pCVar == Setting.pCVar )
					{
						// [AK] Check if these two CVars have the same scope (i.e. offline or online games only), or if the
						// new CVar that we're trying to add has no scope (i.e. works in both offline and online games).
						if (( g_GameModes[mode].GameplaySettings[i].Scope == Setting.Scope ) || ( Setting.Scope == GAMESCOPE_OFFLINEANDONLINE ))
						{
							// [AK] A locked CVar always replaces any unlocked copies of the same CVar that already exist.
							// On the other hand, an unlocked CVar cannot replace any locked copies.
							if (( g_GameModes[mode].GameplaySettings[i].bIsLocked ) && ( Setting.bIsLocked == false ))
							{
								// [AK] If the new/unlocked CVar has no scope, but the old/locked CVar is "offlineonly" or "onlineonly",
								// then change the new CVar's scope so that it's opposite to the old CVar's. The two CVars can then co-exist.
								// Otherwise, the new CVar must be discarded.
								if ( g_GameModes[mode].GameplaySettings[i].Scope != Setting.Scope )
									Setting.Scope = g_GameModes[mode].GameplaySettings[i].Scope != GAMESCOPE_OFFLINEONLY ? GAMESCOPE_OFFLINEONLY : GAMESCOPE_ONLINEONLY;
								else
									bPushToList = false;

								break;
							}

							g_GameModes[mode].GameplaySettings.Delete( i );
						}
						// [AK] If the old CVar has no scope, but the new CVar is "offlineonly" or "onlineonly", just change the old CVar's
						// scope so that it becomes opposite to the new CVar's. The two CVars can then co-exist.
						else if ( g_GameModes[mode].GameplaySettings[i].Scope == GAMESCOPE_OFFLINEANDONLINE )
						{
							g_GameModes[mode].GameplaySettings[i].Scope = Setting.Scope != GAMESCOPE_OFFLINEONLY ? GAMESCOPE_OFFLINEONLY : GAMESCOPE_ONLINEONLY;
						}
					}
				}

				if ( bPushToList )
					g_GameModes[mode].GameplaySettings.Push( Setting );
			}
		}
	}
}

//*****************************************************************************
//
void GAMEMODE_ParseGameModeInfo( void )
{
	int lastlump = 0, lump;

	while (( lump = Wads.FindLump( "GAMEMODE", &lastlump )) != -1 )
	{
		FScanner sc( lump );
		bool bParsedDefGameSettings = false;
		bool bParsedDefLockedSettings = false;

		while ( sc.GetString( ))
		{
			if ( stricmp( sc.String, "defaultgamesettings" ) == 0 )
			{
				// [AK] Don't allow more than one "defaultgamesettings" block in the same lump.
				if ( bParsedDefGameSettings )
					sc.ScriptError( "There is already a \"DefaultGameSettings\" block defined in this lump." );

				GAMEMODE_ParseGameSettingBlock( sc, NUM_GAMEMODES, false, !( bParsedDefGameSettings || bParsedDefLockedSettings ));
				bParsedDefGameSettings = true;
			}
			else if ( stricmp( sc.String, "defaultlockedgamesettings" ) == 0 )
			{
				// [AK] Don't allow more than one "defaultlockedgamesettings" block in the same lump.
				if ( bParsedDefLockedSettings )
					sc.ScriptError( "There is already a \"DefaultLockedGameSettings\" block defined in this lump." );

				GAMEMODE_ParseGameSettingBlock( sc, NUM_GAMEMODES, true, !( bParsedDefGameSettings || bParsedDefLockedSettings ));
				bParsedDefLockedSettings = true;
			}
			else
			{
				GAMEMODE_e GameMode = static_cast<GAMEMODE_e>( sc.MustGetEnumName( "gamemode", "GAMEMODE_", GetValueGAMEMODE_e, true ));
				GAMEMODE_ParseGameModeBlock( sc, GameMode );
			}
		}
	}

	const ULONG ulPrefixLen = strlen( "GAMEMODE_" );

	// [AK] Check if all game mode are acceptable.
	for ( unsigned int i = GAMEMODE_COOPERATIVE; i < NUM_GAMEMODES; i++ )
	{
		FString name = ( GetStringGAMEMODE_e( static_cast<GAMEMODE_e>( i )) + ulPrefixLen );
		name.ToLower( );

		// [AK] Make sure the game mode has a (short) name.
		if ( g_GameModes[i].Name.IsEmpty( ))
			I_Error( "\"%s\" has no name.", name.GetChars( ));
		if ( g_GameModes[i].ShortName.IsEmpty( ))
			I_Error( "\"%s\" has no short name.", name.GetChars( ));

		// [AK] Get the game mode type (cooperative, deathmatch, or team game). There shouldn't be more than one enabled or none at all.
		ULONG ulFlags = g_GameModes[i].ulFlags & GAMETYPE_MASK;
		if (( ulFlags == 0 ) || (( ulFlags & ( ulFlags - 1 )) != 0 ))
			I_Error( "Can't determine if \"%s\" is cooperative, deathmatch, or team-based.", name.GetChars( ));

		// [AK] Get the type of "players earn" flag this game mode is currently using.
		ulFlags = g_GameModes[i].ulFlags & EARNTYPE_MASK;

		// [AK] If all of these flags were removed or if more than one was added, then throw an error.
		if ( ulFlags == 0 )
			I_Error( "Players have no way of earning kills, frags, points, or wins in \"%s\".", name.GetChars( ));
		else if (( ulFlags & ( ulFlags - 1 )) != 0 )
			I_Error( "There is more than one PLAYERSEARN flag enabled in \"%s\".", name.GetChars( ));
	}

	// Our default game mode is co-op.
	g_CurrentGameMode = GAMEMODE_COOPERATIVE;
}

//*****************************************************************************
//
ULONG GAMEMODE_GetFlags( GAMEMODE_e GameMode )
{
	if ( GameMode >= NUM_GAMEMODES )
		return ( 0 );

	return ( g_GameModes[GameMode].ulFlags );
}

//*****************************************************************************
//
ULONG GAMEMODE_GetCurrentFlags( void )
{
	return ( g_GameModes[g_CurrentGameMode].ulFlags );
}

//*****************************************************************************
//
const char *GAMEMODE_GetShortName( GAMEMODE_e GameMode )
{
	if ( GameMode >= NUM_GAMEMODES )
		return ( NULL );

	return ( g_GameModes[GameMode].ShortName.GetChars( ));
}

//*****************************************************************************
//
const char *GAMEMODE_GetName( GAMEMODE_e GameMode )
{
	if ( GameMode >= NUM_GAMEMODES )
		return ( NULL );

	return ( g_GameModes[GameMode].Name.GetChars( ));
}

//*****************************************************************************
//
const char *GAMEMODE_GetCurrentName( void )
{
	return ( g_GameModes[g_CurrentGameMode].Name.GetChars( ));
}

//*****************************************************************************
//
const char *GAMEMODE_GetF1Texture( GAMEMODE_e GameMode )
{
	if ( GameMode >= NUM_GAMEMODES )
		return ( NULL );

	return ( g_GameModes[GameMode].F1Texture.GetChars( ));
}

//*****************************************************************************
//
const char *GAMEMODE_GetWelcomeSound( GAMEMODE_e GameMode )
{
	if ( GameMode >= NUM_GAMEMODES )
		return ( NULL );

	return ( g_GameModes[GameMode].WelcomeSound.GetChars( ));
}

//*****************************************************************************
//
void GAMEMODE_DetermineGameMode( void )
{
	g_CurrentGameMode = GAMEMODE_COOPERATIVE;
	if ( survival )
		g_CurrentGameMode = GAMEMODE_SURVIVAL;
	if ( invasion )
		g_CurrentGameMode = GAMEMODE_INVASION;
	if ( deathmatch )
		g_CurrentGameMode = GAMEMODE_DEATHMATCH;
	if ( teamplay )
		g_CurrentGameMode = GAMEMODE_TEAMPLAY;
	if ( duel )
		g_CurrentGameMode = GAMEMODE_DUEL;
	if ( terminator )
		g_CurrentGameMode = GAMEMODE_TERMINATOR;
	if ( lastmanstanding )
		g_CurrentGameMode = GAMEMODE_LASTMANSTANDING;
	if ( teamlms )
		g_CurrentGameMode = GAMEMODE_TEAMLMS;
	if ( possession )
		g_CurrentGameMode = GAMEMODE_POSSESSION;
	if ( teampossession )
		g_CurrentGameMode = GAMEMODE_TEAMPOSSESSION;
	if ( teamgame )
		g_CurrentGameMode = GAMEMODE_TEAMGAME;
	if ( ctf )
		g_CurrentGameMode = GAMEMODE_CTF;
	if ( oneflagctf )
		g_CurrentGameMode = GAMEMODE_ONEFLAGCTF;
	if ( skulltag )
		g_CurrentGameMode = GAMEMODE_SKULLTAG;
	if ( domination )
		g_CurrentGameMode = GAMEMODE_DOMINATION;
}

//*****************************************************************************
//
bool GAMEMODE_IsGameWaitingForPlayers( void )
{
	if ( survival )
		return ( SURVIVAL_GetState( ) == SURVS_WAITINGFORPLAYERS );
	else if ( invasion )
		return ( INVASION_GetState( ) == IS_WAITINGFORPLAYERS );
	else if ( duel )
		return ( DUEL_GetState( ) == DS_WAITINGFORPLAYERS );
	else if ( teamlms || lastmanstanding )
		return ( LASTMANSTANDING_GetState( ) == LMSS_WAITINGFORPLAYERS );
	else if ( possession || teampossession )
		return ( POSSESSION_GetState( ) == PSNS_WAITINGFORPLAYERS );
	// [BB] Non-coop game modes need two or more players.
	else if ( ( GAMEMODE_GetCurrentFlags() & GMF_COOPERATIVE ) == false )
		return ( GAME_CountActivePlayers( ) < 2 );
	// [BB] For coop games one player is enough.
	else
		return ( GAME_CountActivePlayers( ) < 1 );
}

//*****************************************************************************
//
bool GAMEMODE_IsGameInCountdown( void )
{
	if ( survival )
		return ( SURVIVAL_GetState( ) == SURVS_COUNTDOWN );
	else if ( invasion )
		return ( ( INVASION_GetState( ) == IS_FIRSTCOUNTDOWN ) || ( INVASION_GetState( ) == IS_COUNTDOWN ) );
	else if ( duel )
		return ( DUEL_GetState( ) == DS_COUNTDOWN );
	else if ( teamlms || lastmanstanding )
		return ( ( LASTMANSTANDING_GetState( ) == LMSS_COUNTDOWN ) || ( LASTMANSTANDING_GetState( ) == LMSS_NEXTROUNDCOUNTDOWN ) );
	// [BB] What about PSNS_PRENEXTROUNDCOUNTDOWN?
	else if ( possession || teampossession )
		return ( ( POSSESSION_GetState( ) == PSNS_COUNTDOWN ) || ( POSSESSION_GetState( ) == PSNS_NEXTROUNDCOUNTDOWN ) );
	// [BB] The other game modes don't have a countdown.
	else
		return ( false );
}

//*****************************************************************************
//
bool GAMEMODE_IsGameInProgress( void )
{
	// [BB] Since there is currently no way unified way to check the state of
	// the active game mode, we have to do it manually.
	if ( survival )
		return ( SURVIVAL_GetState( ) == SURVS_INPROGRESS );
	else if ( invasion )
		return ( ( INVASION_GetState( ) == IS_INPROGRESS ) || ( INVASION_GetState( ) == IS_BOSSFIGHT ) || ( INVASION_GetState( ) == IS_WAVECOMPLETE ) );
	else if ( duel )
		return ( DUEL_GetState( ) == DS_INDUEL );
	else if ( teamlms || lastmanstanding )
		return ( LASTMANSTANDING_GetState( ) == LMSS_INPROGRESS );
	else if ( possession || teampossession )
		return ( ( POSSESSION_GetState( ) == PSNS_INPROGRESS ) || ( POSSESSION_GetState( ) == PSNS_ARTIFACTHELD ) );
	// [BB] In non-coop game modes without warmup phase, we just say the game is
	// in progress when there are two or more players and the game is not frozen
	// due to the end level delay.
	else if ( ( GAMEMODE_GetCurrentFlags() & GMF_COOPERATIVE ) == false )
		return ( ( GAME_CountActivePlayers( ) >= 2 ) && ( GAME_GetEndLevelDelay () == 0 ) );
	// [BB] For coop games one player is enough.
	else
		return ( ( GAME_CountActivePlayers( ) >= 1 ) && ( GAME_GetEndLevelDelay () == 0 ) );
}

//*****************************************************************************
//
bool GAMEMODE_IsGameInResultSequence( void )
{
	if ( survival )
		return ( SURVIVAL_GetState( ) == SURVS_MISSIONFAILED );
	else if ( invasion )
		return ( INVASION_GetState( ) == IS_MISSIONFAILED );
	else if ( duel )
		return ( DUEL_GetState( ) == DS_WINSEQUENCE );
	else if ( teamlms || lastmanstanding )
		return ( LASTMANSTANDING_GetState( ) == LMSS_WINSEQUENCE );
	// [BB] The other game modes don't have such a sequnce. Arguably, possession
	// with PSNS_HOLDERSCORED could also be considered for this.
	// As substitute for such a sequence we consider whether the game is
	// frozen because of the end level delay.
	else
		return ( GAME_GetEndLevelDelay () > 0 );
}

//*****************************************************************************
//
bool GAMEMODE_IsGameInProgressOrResultSequence( void )
{
	return ( GAMEMODE_IsGameInProgress() || GAMEMODE_IsGameInResultSequence() );
}

//*****************************************************************************
//
bool GAMEMODE_IsLobbyMap( void )
{
	return level.flagsZA & LEVEL_ZA_ISLOBBY || stricmp(level.mapname, lobby) == 0;
}

//*****************************************************************************
//
bool GAMEMODE_IsLobbyMap( const char* mapname )
{
	// [BB] The level is not loaded yet, so we can't use level.flags2 directly.
	const level_info_t *levelinfo = FindLevelInfo( mapname, false );

	if (levelinfo == NULL)
	{
		return false;
	}

	return levelinfo->flagsZA & LEVEL_ZA_ISLOBBY || stricmp( levelinfo->mapname, lobby ) == 0;
}

//*****************************************************************************
//
bool GAMEMODE_IsNextMapCvarLobby( void )
{
	// If we're using a CVAR lobby and we're not on the lobby map, the next map
	// should always be the lobby.
	return strcmp(lobby, "") != 0 && stricmp(lobby, level.mapname) != 0;
}

//*****************************************************************************
//
bool GAMEMODE_IsTimelimitActive( void )
{
	// [AM] If the map is a lobby, ignore the timelimit.
	if ( GAMEMODE_IsLobbyMap( ) )
		return false;

	// [BB] In gamemodes that reset the time during a map reset, the timelimit doesn't make sense when the game is not in progress.
	if ( ( GAMEMODE_GetCurrentFlags() & GMF_MAPRESET_RESETS_MAPTIME ) && ( GAMEMODE_IsGameInProgress( ) == false ) )
		return false;

	// [BB] Teamlms doesn't support timelimit, so just turn it off in this mode.
	if ( teamlms )
		return false;

	// [BB] SuperGod insisted to have timelimit in coop, e.g. for jumpmaze, but its implementation conceptually doesn't work in invasion or survival.
	return (/*( deathmatch || teamgame ) &&*/ ( invasion == false ) && ( survival == false ) && timelimit );
}

//*****************************************************************************
//
void GAMEMODE_GetTimeLeftString( FString &TimeLeftString )
{
	LONG	lTimeLeft = (LONG)( timelimit * ( TICRATE * 60 )) - level.time;
	ULONG	ulHours, ulMinutes, ulSeconds;

	if ( lTimeLeft <= 0 )
		ulHours = ulMinutes = ulSeconds = 0;
	else
	{
		ulHours = lTimeLeft / ( TICRATE * 3600 );
		lTimeLeft -= ulHours * TICRATE * 3600;
		ulMinutes = lTimeLeft / ( TICRATE * 60 );
		lTimeLeft -= ulMinutes * TICRATE * 60;
		ulSeconds = lTimeLeft / TICRATE;
	}

	if ( ulHours )
		TimeLeftString.Format ( "%02d:%02d:%02d", static_cast<unsigned int> (ulHours), static_cast<unsigned int> (ulMinutes), static_cast<unsigned int> (ulSeconds) );
	else
		TimeLeftString.Format ( "%02d:%02d", static_cast<unsigned int> (ulMinutes), static_cast<unsigned int> (ulSeconds) );
}

//*****************************************************************************
//
void GAMEMODE_RespawnDeadPlayers( playerstate_t deadSpectatorState, playerstate_t deadPlayerState )
{
	// [BB] This is server side.
	if ( NETWORK_InClientMode() )
	{
		return;
	}

	// [BB] Any player spawning in this game state would fail.
	// [BB] The same is true when we are starting a new game at the moment.
	if ( ( gamestate == GS_STARTUP ) || ( gamestate == GS_FULLCONSOLE ) || ( gameaction == ga_newgame )  || ( gameaction == ga_newgame2 ) )
	{
		return;
	}

	// Respawn any players who were downed during the previous round.
	for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if (( playeringame[ulIdx] == false ) ||
			( PLAYER_IsTrueSpectator( &players[ulIdx] )))
		{
			continue;
		}

		// We don't want to respawn players as soon as the map starts; we only
		// [BB] respawn all dead players and dead spectators.
		if ((( players[ulIdx].mo == NULL ) ||
			( players[ulIdx].mo->health > 0 )) &&
			( players[ulIdx].bDeadSpectator == false ))
		{
			continue;
		}

		// [AK] Using PST_DEAD for the state means to ignore them, in case only
		// dead players should respawn but not dead spectators, and vice-versa.
		if ( players[ulIdx].bDeadSpectator )
		{
			if ( deadSpectatorState == PST_DEAD )
				continue;
		}
		else if ( deadPlayerState == PST_DEAD )
		{
			continue;
		}

		// [AK] When dead players (i.e. not dead spectators) are respawned with
		// PST_REBORN, their lives aren't fully replenished like it is for dead
		// spectators. If the game was already in progress, then they will also
		// lose a life upon respawning. This is particularly useful for survival
		// invasion when dead players respawn after the end of a wave.
		if ( GAMEMODE_GetCurrentFlags() & GMF_USEMAXLIVES )
		{
			if (( players[ulIdx].bDeadSpectator ) || ( deadPlayerState != PST_REBORN ))
				PLAYER_SetLivesLeft( &players[ulIdx], GAMEMODE_GetMaxLives( ) - 1 );
			else if (( GAMEMODE_IsGameInProgress( )) && ( players[ulIdx].ulLivesLeft > 0 ))
				PLAYER_SetLivesLeft( &players[ulIdx], players[ulIdx].ulLivesLeft - 1 );
		}

		players[ulIdx].playerstate = players[ulIdx].bDeadSpectator ? deadSpectatorState : deadPlayerState;
		players[ulIdx].bSpectating = false;
		players[ulIdx].bDeadSpectator = false;

		APlayerPawn *oldactor = players[ulIdx].mo;

		GAMEMODE_SpawnPlayer( ulIdx );

		// [CK] Ice corpses that are persistent between rounds must not affect
		// the client post-death in any gamemode with a countdown.
		if (( oldactor ) && ( oldactor->health > 0 || oldactor->flags & MF_ICECORPSE ))
		{
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVERCOMMANDS_DestroyThing( oldactor );

			oldactor->Destroy( );
		}


		// [BB] If he's a bot, tell him that he successfully joined.
		if ( players[ulIdx].bIsBot && players[ulIdx].pSkullBot )
			players[ulIdx].pSkullBot->PostEvent( BOTEVENT_JOINEDGAME );
	}

	// [BB] Dead spectators were allowed to use chasecam, but are not necessarily allowed to use it
	// when alive again. Re-applying dmflags2 takes care of this.
	dmflags2 = dmflags2.GetGenericRep( CVAR_Int ).Int;
}

//*****************************************************************************
//
void GAMEMODE_RespawnDeadPlayersAndPopQueue( playerstate_t deadSpectatorState, playerstate_t deadPlayerState )
{
	GAMEMODE_RespawnDeadPlayers( deadSpectatorState, deadPlayerState );
	// Let anyone who's been waiting in line join now.
	JOINQUEUE_PopQueue( -1 );
}

//*****************************************************************************
//
void GAMEMODE_RespawnAllPlayers( BOTEVENT_e BotEvent, playerstate_t PlayerState )
{
	// [BB] This is server side.
	if ( NETWORK_InClientMode() == false )
	{
		// [AK] In offline games, remember the old player that was being spied on.
		player_t *const localPlayer = &players[consoleplayer];
		player_t *const oldSpiedPlayer = (( NETWORK_GetState( ) != NETSTATE_SERVER ) && ( localPlayer->camera )) ? localPlayer->camera->player : nullptr;

		// Respawn the players.
		for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
		{
			if (( playeringame[ulIdx] == false ) ||
				( PLAYER_IsTrueSpectator( &players[ulIdx] )))
			{
				continue;
			}

			// [BB] Disassociate the player body, but don't delete it right now. The clients
			// still need the old body while the player is respawned so that they can properly
			// transfer their camera if they are spying through the eyes of the respawned player.
			APlayerPawn* pOldPlayerBody = players[ulIdx].mo;
			players[ulIdx].mo = NULL;

			players[ulIdx].playerstate = PlayerState;
			GAMEMODE_SpawnPlayer( ulIdx );

			if ( pOldPlayerBody )
			{
				if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				{
					// [AK] Also tell the clients to stop all sounds on the player.
					SERVERCOMMANDS_StopAllSoundsOnThing( pOldPlayerBody );
					SERVERCOMMANDS_DestroyThing( pOldPlayerBody );
				}

				// [AK] Stop any sounds from this player before destroying them.
				S_StopAllSoundsFromActor( pOldPlayerBody );
				pOldPlayerBody->Destroy( );
			}

			if ( players[ulIdx].pSkullBot && ( BotEvent < NUM_BOTEVENTS ) )
				players[ulIdx].pSkullBot->PostEvent( BotEvent );
		}

		// [AK] After all players have respawned, return the local player's view
		// back to the player they were spying on before, if they were spectating.
		// We must do this here because the old player's body was destroyed.
		if (( NETWORK_GetState( ) != NETSTATE_SERVER ) && ( localPlayer->bSpectating ))
		{
			if (( oldSpiedPlayer != nullptr ) && ( oldSpiedPlayer != localPlayer ) && ( oldSpiedPlayer->mo != nullptr ))
				localPlayer->camera = oldSpiedPlayer->mo;
		}
	}
}

//*****************************************************************************
//
void GAMEMODE_SpawnPlayer( const ULONG ulPlayer, bool bClientUpdate )
{
	// Spawn the player at their appropriate team start.
	if ( GAMEMODE_GetCurrentFlags() & GMF_TEAMGAME )
	{
		if ( players[ulPlayer].bOnTeam )
			G_TeamgameSpawnPlayer( ulPlayer, players[ulPlayer].Team, bClientUpdate );
		else
			G_TemporaryTeamSpawnPlayer( ulPlayer, bClientUpdate );
	}
	// If deathmatch, just spawn at a random spot.
	else if ( GAMEMODE_GetCurrentFlags() & GMF_DEATHMATCH )
		G_DeathMatchSpawnPlayer( ulPlayer, bClientUpdate );
	// Otherwise, just spawn at their normal player start.
	else
		G_CooperativeSpawnPlayer( ulPlayer, bClientUpdate );
}

//*****************************************************************************
//
void GAMEMODE_ResetPlayersKillCount( const bool bInformClients )
{
	// Reset everyone's kill count.
	for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		players[ulIdx].killcount = 0;
		players[ulIdx].RailgunShots = 0;
		// [BB] Also reset the things for ZADF_AWARD_DAMAGE_INSTEAD_KILLS.
		players[ulIdx].lPointCount = 0;
		players[ulIdx].ulUnrewardedDamageDealt = 0;

		// [BB] Notify the clients about the killcount change.
		if ( playeringame[ulIdx] && bInformClients && (NETWORK_GetState() == NETSTATE_SERVER) )
		{
			SERVERCOMMANDS_SetPlayerKillCount ( ulIdx );
			SERVERCOMMANDS_SetPlayerPoints ( ulIdx );
		}
	}
}

//*****************************************************************************
//
bool GAMEMODE_AreSpectatorsForbiddenToChatToPlayers( const bool doVoice )
{
	if (( lmsspectatorsettings & ( doVoice ? LMS_SPF_VOICECHAT : LMS_SPF_CHAT )) == false )
	{
		if (( teamlms || lastmanstanding ) && ( LASTMANSTANDING_GetState( ) == LMSS_INPROGRESS ))
			return true;

		if ( ( zadmflags & ZADF_ALWAYS_APPLY_LMS_SPECTATORSETTINGS ) && GAMEMODE_IsGameInProgress() )
			return true;
	}

	return false;
}

//*****************************************************************************
//
bool GAMEMODE_IsClientForbiddenToChatToPlayers( const ULONG client, const bool doVoice )
{
	// [BB] If it's not a valid client, there are no restrictions. Note:
	// client == MAXPLAYERS means the server wants to say something.
	if ( client >= MAXPLAYERS )
		return false;

	// [BB] Ingame players are allowed to chat to other players.
	if ( players[client].bSpectating == false )
		return false;

	return GAMEMODE_AreSpectatorsForbiddenToChatToPlayers( doVoice );
}

//*****************************************************************************
//
bool GAMEMODE_PreventPlayersFromJoining( ULONG ulExcludePlayer )
{
	// [BB] No free player slots.
	if ( ( NETWORK_GetState( ) == NETSTATE_SERVER ) && ( SERVER_CalcNumNonSpectatingPlayers( ulExcludePlayer ) >= static_cast<unsigned> (sv_maxplayers) ) )
		return true;

	// [BB] Don't let players join during intermission. They will be put in line and the queue is popped once intermission ends.
	if ( gamestate == GS_INTERMISSION )
		return true;

	// [BB] Duel in progress.
	if ( duel && ( GAME_CountActivePlayers( ) >= 2 ) )
		return true;

	// [BB] If lives are limited, players are not allowed to join most of the time.
	// [BB] The ga_worlddone check makes sure that in game players (at least in survival and survival invasion) aren't forced to spectate after a "changemap" map change.
	// [BB] The ga_newgame check fixes some problem when starting a survival invasion skirmish with bots while already in a survival invasion game with bots
	// (the consoleplayer is spawned as spectator in this case and leaves a ghost player upon joining)
	if ( ( gameaction != ga_worlddone ) && ( gameaction != ga_newgame ) )
	{
		// [AK] Also check if it's survival invasion and the player is allowed to join.
		if ( ( GAMEMODE_AreLivesLimited( ) && GAMEMODE_IsGameInProgressOrResultSequence( ) ) || INVASION_PreventPlayersFromJoining( ) )
			return true;
	}

	return false;
}

//*****************************************************************************
//
bool GAMEMODE_AreLivesLimited( void )
{
	// [BB] Invasion is a special case: If sv_maxlives == 0 in invasion, players have infinite lives.
	return ( ( ( sv_maxlives > 0 ) || ( invasion == false ) ) && ( GAMEMODE_GetCurrentFlags() & GMF_USEMAXLIVES ) );
}

//*****************************************************************************
//
bool GAMEMODE_ShouldPlayerLoseLife( void )
{
	// [AK] Players don't lose lives in survival invasion when a wave is complete.
	return (( GAMEMODE_AreLivesLimited( )) && ( GAMEMODE_IsGameInProgress( )) && (( invasion == false ) || ( INVASION_GetState( ) != IS_WAVECOMPLETE )));
}

//*****************************************************************************
//
bool GAMEMODE_IsPlayerCarryingGameModeItem( player_t *player )
{
	GAMEMODE_e mode = GAMEMODE_GetCurrentMode( );

	// [AK] Check if this player is carrying a team item like an enemy team's flag or skull.
	if (( GAMEMODE_GetCurrentFlags( ) & GMF_USETEAMITEM ) && ( player->mo != NULL ))
	{
		if ( TEAM_FindOpposingTeamsItemInPlayersInventory( player ))
			return true;

		// [AK] Check if the player is carrying any team item. This can be useful for mods that
		// might define their own special items.
		AInventory *item = player->mo->FindInventory( PClass::FindClass( "TeamItem" ), true );

		// [AK] The player shouldn't have the white flag when we're not playing one-flag CTF.
		return (( item ) && (( item->IsKindOf( PClass::FindClass( "WhiteFlag" )) == false ) || ( mode == GAMEMODE_ONEFLAGCTF )));
	}

	// [AK] Check if this player is carrying the terminator sphere while playing terminator.
	if (( mode == GAMEMODE_TERMINATOR ) && ( player->cheats2 & CF2_TERMINATORARTIFACT ))
		return true;

	// [AK] Check if this player is carrying the hellstone while playing (team) possession.
	if ((( mode == GAMEMODE_POSSESSION ) || ( mode == GAMEMODE_TEAMPOSSESSION )) && ( player->cheats2 & CF2_POSSESSIONARTIFACT ))
		return true;

	return false;
}

//*****************************************************************************
//
unsigned int GAMEMODE_GetMaxLives( void )
{
	return ( ( sv_maxlives > 0 ) ? static_cast<unsigned int> ( sv_maxlives ) : 1 );
}

//*****************************************************************************
//
void GAMEMODE_AdjustActorSpawnFlags ( AActor *pActor )
{
	if ( pActor == NULL )
		return;

	// [BB] Since several Skulltag versions added NOGRAVITY to some spheres on default, allow the user to restore this behavior.
	if ( zacompatflags & ZACOMPATF_NOGRAVITY_SPHERES )
	{
		if ( ( stricmp ( pActor->GetClass()->TypeName.GetChars(), "InvulnerabilitySphere" ) == 0 )
			|| ( stricmp ( pActor->GetClass()->TypeName.GetChars(), "Soulsphere" ) == 0 )
			|| ( stricmp ( pActor->GetClass()->TypeName.GetChars(), "Megasphere" ) == 0 ) 
			|| ( stricmp ( pActor->GetClass()->TypeName.GetChars(), "BlurSphere" ) == 0 ) )
			pActor->flags |= MF_NOGRAVITY;
	}
}

//*****************************************************************************
//
void GAMEMODE_SpawnSpecialGamemodeThings ( void )
{
	// [BB] The server will let the clients know of any necessary spawns.
	if ( NETWORK_InClientMode() == false )
	{
		// Spawn the terminator artifact in terminator mode.
		if ( terminator )
			GAME_SpawnTerminatorArtifact( );

		// Spawn the possession artifact in possession/team possession mode.
		if ( possession || teampossession )
			GAME_SpawnPossessionArtifact( );
	}
}

//*****************************************************************************
//
void GAMEMODE_ResetSpecalGamemodeStates ( void )
{
	// [BB] If playing Domination reset ownership, even the clients can do this.
	if ( domination )
		DOMINATION_Init();

	// [BB] If playing possession make sure to end the held countdown, even the clients can do this.
	if ( possession || teampossession )
	{
		POSSESSION_SetArtifactHoldTicks ( 0 );
		if ( POSSESSION_GetState() == PSNS_ARTIFACTHELD )
			POSSESSION_SetState( PSNS_PRENEXTROUNDCOUNTDOWN );
	}
}

//*****************************************************************************
//
bool GAMEMODE_IsSpectatorAllowedSpecial ( const int Special )
{
	return ( ( Special == Teleport ) || ( Special == Teleport_NoFog ) || ( Special == Teleport_NoStop ) || ( Special == Teleport_Line ) );
}

//*****************************************************************************
//
bool GAMEMODE_IsHandledSpecial ( AActor *Activator, int Special )
{
	// [BB] Non-player activated specials are never handled by the client.
	if ( Activator == NULL || Activator->player == NULL )
		return ( NETWORK_InClientMode() == false );

	// [EP/BB] Spectators activate a very limited amount of specials and ignore all others.
	if ( Activator->player->bSpectating )
	{
		// [AK] Spectators without physical restrictions should only activate
		// specials if they're not under the floor or above the ceiling.
		if ( P_IsSpectatorUnrestricted( Activator ) )
		{
			const int viewZ = Activator->z + static_cast<APlayerPawn *>( Activator )->ViewHeight;

			if ( viewZ < Activator->Sector->floorplane.ZatPoint( Activator->x, Activator->y ) )
				return false;

			if ( viewZ > Activator->Sector->ceilingplane.ZatPoint( Activator->x, Activator->y ) )
				return false;
		}

		return ( GAMEMODE_IsSpectatorAllowedSpecial( Special ) );
	}

	// [BB] Clients predict a very limited amount of specials for the local player and ignore all others (spectators were already handled)
	if ( NETWORK_InClientMode() )
		return ( NETWORK_IsConsolePlayer ( Activator ) && NETWORK_IsClientPredictedSpecial( Special ) );

	// [BB] Neither spectator, nor client.
	return true;
}

//*****************************************************************************
//
GAMESTATE_e GAMEMODE_GetState( void )
{
	if ( GAMEMODE_IsGameWaitingForPlayers() )
		return GAMESTATE_WAITFORPLAYERS;
	else if ( GAMEMODE_IsGameInCountdown() )
		return GAMESTATE_COUNTDOWN;
	else if ( GAMEMODE_IsGameInProgress() )
		return GAMESTATE_INPROGRESS;
	else if ( GAMEMODE_IsGameInResultSequence() )
		return GAMESTATE_INRESULTSEQUENCE;

	// [BB] Some of the above should apply, but this function always has to return something.
	return GAMESTATE_UNSPECIFIED;
}

//*****************************************************************************
//
void GAMEMODE_SetState( GAMESTATE_e GameState )
{
	if( GameState == GAMESTATE_WAITFORPLAYERS )
	{
		if ( survival )
			SURVIVAL_SetState( SURVS_WAITINGFORPLAYERS );
		else if ( invasion )
			INVASION_SetState( IS_WAITINGFORPLAYERS );
		else if ( duel )
			DUEL_SetState( DS_WAITINGFORPLAYERS );
		else if ( teamlms || lastmanstanding )
			LASTMANSTANDING_SetState( LMSS_WAITINGFORPLAYERS );
		else if ( possession || teampossession )
			POSSESSION_SetState( PSNS_WAITINGFORPLAYERS );
	}
	else if( GameState == GAMESTATE_COUNTDOWN )
	{
		if ( survival )
			SURVIVAL_SetState( SURVS_COUNTDOWN );
		else if ( invasion )
			INVASION_SetState( IS_FIRSTCOUNTDOWN );
		else if ( duel )
			DUEL_SetState( DS_COUNTDOWN );
		else if ( teamlms || lastmanstanding )
			LASTMANSTANDING_SetState( LMSS_COUNTDOWN );
		else if ( possession || teampossession )
			POSSESSION_SetState( PSNS_COUNTDOWN );
	}
	else if( GameState == GAMESTATE_INPROGRESS )
	{
		if ( survival )
			SURVIVAL_SetState( SURVS_INPROGRESS );
		else if ( invasion )
			INVASION_SetState( IS_INPROGRESS );
		else if ( duel )
			DUEL_SetState( DS_INDUEL );
		else if ( teamlms || lastmanstanding )
			LASTMANSTANDING_SetState( LMSS_INPROGRESS );
		else if ( possession || teampossession )
			POSSESSION_SetState( PSNS_INPROGRESS );
	}
	else if( GameState == GAMESTATE_INRESULTSEQUENCE )
	{
		if ( survival )
			SURVIVAL_SetState( SURVS_MISSIONFAILED );
		else if ( invasion )
			INVASION_SetState( IS_MISSIONFAILED );
		else if ( duel )
			DUEL_SetState( DS_WINSEQUENCE );
		else if ( teamlms || lastmanstanding )
			LASTMANSTANDING_SetState( LMSS_WINSEQUENCE );
	}
}

//*****************************************************************************
//
LONG GAMEMODE_HandleEvent ( const GAMEEVENT_e Event, AActor *pActivator, const int DataOne, const int DataTwo, const bool bRunNow, const int OverrideResult )
{
	// [BB] Clients don't start scripts.
	if ( NETWORK_InClientMode() )
		return 1;

	// [AK] Remember the old event's result value, in case we need to
	// handle nested event calls (i.e. an event that's triggered in
	// the middle of another event).
	const LONG lOldResult = GAMEMODE_GetEventResult( );
	GAMEMODE_SetEventResult( OverrideResult );

	// [BB] The activator of the event activates the event script.
	// The first argument is the type, e.g. GAMEEVENT_PLAYERFRAGS,
	// the second and third are specific to the event, e.g. the second is the number of the fragged player.
	// The third argument will be zero if it isn't used in the script.
	FBehavior::StaticStartTypedScripts( SCRIPT_Event, pActivator, true, Event, bRunNow, false, DataOne, DataTwo );

	// [AK] Get the result value of the event, then reset it back to the old value.
	LONG lResult = GAMEMODE_GetEventResult( );
	GAMEMODE_SetEventResult( lOldResult );

	// [AK] Return the result value of the event.
	return lResult;
}

//*****************************************************************************
//
void GAMEMODE_HandleSpawnEvent ( AActor *actor )
{
	if ( actor == nullptr )
		return;

	// [AK] We shouldn't need to execute this for players since we already have
	// special script types like ENTER, RETURN, and RESPAWN.
	if (( actor->player == nullptr ) && (( actor->STFlags & STFL_NOSPAWNEVENTSCRIPT ) == false ))
	{
		bool notImportant = false;

		// [AK] Projectiles and BulletPuffs can have NOBLOCKMAP enabled but that
		// doesn't make them unimportant.
		if (( actor->flags & MF_NOBLOCKMAP ) && ((( actor->flags & MF_MISSILE ) == false ) && ( actor->IsKindOf( PClass::FindClass( NAME_BulletPuff )) == false )))
			notImportant = true;
		else if (( actor->flags & MF_NOSECTOR ) || ( actor->IsKindOf( RUNTIME_CLASS( AHexenArmor ))))
			notImportant = true;

		// [AK] If we want to force GAMEEVENT_ACTOR_SPAWNED on every actor, then
		// ignore less important actors unless they enabled USESPAWNEVENTSCRIPT.
		if (( actor->STFlags & STFL_USESPAWNEVENTSCRIPT ) || (( gameinfo.bForceSpawnEventScripts ) && ( notImportant == false )))
		{
			enum
			{
				GAMEEVENT_SPAWN_LEVELSPAWNED	= 1 << 0,
				GAMEEVENT_SPAWN_RANDOMSPAWNED	= 1 << 1,
			};

			unsigned int spawnEventFlags = 0;

			if ( actor->STFlags & STFL_LEVELSPAWNED )
				spawnEventFlags |= GAMEEVENT_SPAWN_LEVELSPAWNED;

			if ( actor->STFlags & STFL_RANDOMSPAWNED )
				spawnEventFlags |= GAMEEVENT_SPAWN_RANDOMSPAWNED;

			GAMEMODE_HandleEvent( GAMEEVENT_ACTOR_SPAWNED, actor, spawnEventFlags, 0, true );
		}
	}
}

//*****************************************************************************
//
bool GAMEMODE_HandleDamageEvent ( AActor *target, AActor *inflictor, AActor *source, int &damage, FName mod, bool bBeforeArmor )
{
	// [AK] Don't run any scripts if the target doesn't allow executing GAMEEVENT_ACTOR_DAMAGED.
	if ( target->STFlags & STFL_NODAMAGEEVENTSCRIPT )
		return true;

	// [AK] Don't run any scripts if the target can't execute GAMEEVENT_ACTOR_DAMAGED unless
	// all actors are forced to execute it.
	if ((( target->STFlags & STFL_USEDAMAGEEVENTSCRIPT ) == false ) && ( gameinfo.bForceDamageEventScripts == false ))
		return true;
	
	const GAMEEVENT_e DamageEvent = bBeforeArmor ? GAMEEVENT_ACTOR_DAMAGED_PREMOD : GAMEEVENT_ACTOR_DAMAGED;
	const int originalDamage = damage;

	// [AK] We somehow need to pass all the actor pointers into the script itself. A simple way
	// to do this is temporarily spawn a temporary actor and change its actor pointers to the target,
	// source, and inflictor. We can then use these to initialize the AAPTR_DAMAGE_TARGET,
	// AAPTR_DAMAGE_SOURCE, and AAPTR_DAMAGE_INFLICTOR pointers of the script.
	AActor *temp = Spawn( "MapSpot", target->x, target->y, target->z, NO_REPLACE );

	temp->target = target;
	temp->master = source;
	temp->tracer = inflictor;

	damage = GAMEMODE_HandleEvent( DamageEvent, temp, damage, GlobalACSStrings.AddString( mod ), true, damage );

	// [AK] Destroy the temporary actor after executing all event scripts.
	temp->Destroy( );

	// [AK] If the new damage is zero, that means the target shouldn't take damage in P_DamageMobj.
	// Allow P_DamageMobj to execute anyways if the original damage was zero (i.e. NODAMAGE flag).
	return ( originalDamage == 0 || damage != 0 );
}

//*****************************************************************************
//
LONG GAMEMODE_GetEventResult( )
{
	return g_lEventResult;
}

//*****************************************************************************
//
void GAMEMODE_SetEventResult( LONG lResult )
{
	g_lEventResult = lResult;
}

//*****************************************************************************
//
GAMEMODE_e GAMEMODE_GetCurrentMode( void )
{
	return ( g_CurrentGameMode );
}

//*****************************************************************************
//
void GAMEMODE_SetCurrentMode( GAMEMODE_e GameMode )
{
	UCVarValue	 Val;
	g_CurrentGameMode = GameMode;	
	
	// [RC] Set all the CVars. We can't just use "= true;" because of the latched cvars.
	// (Hopefully Blzut's update will save us from this garbage.)

	Val.Bool = false;
	// [BB] Even though setting deathmatch and teamgame to false will set cooperative to true,
	// we need to set cooperative to false here first to clear survival and invasion.
	cooperative.ForceSet( Val, CVAR_Bool );
	deathmatch.ForceSet( Val, CVAR_Bool );
	teamgame.ForceSet( Val, CVAR_Bool );
	instagib.ForceSet( Val, CVAR_Bool );
	buckshot.ForceSet( Val, CVAR_Bool );

	Val.Bool = true;
	switch ( GameMode )
	{
	case GAMEMODE_COOPERATIVE:

		cooperative.ForceSet( Val, CVAR_Bool );
		break;
	case GAMEMODE_SURVIVAL:

		survival.ForceSet( Val, CVAR_Bool );
		break;
	case GAMEMODE_INVASION:

		invasion.ForceSet( Val, CVAR_Bool );
		break;
	case GAMEMODE_DEATHMATCH:

		deathmatch.ForceSet( Val, CVAR_Bool );
		break;
	case GAMEMODE_TEAMPLAY:

		teamplay.ForceSet( Val, CVAR_Bool );
		break;
	case GAMEMODE_DUEL:

		duel.ForceSet( Val, CVAR_Bool );
		break;
	case GAMEMODE_TERMINATOR:

		terminator.ForceSet( Val, CVAR_Bool );
		break;
	case GAMEMODE_LASTMANSTANDING:

		lastmanstanding.ForceSet( Val, CVAR_Bool );
		break;
	case GAMEMODE_TEAMLMS:

		teamlms.ForceSet( Val, CVAR_Bool );
		break;
	case GAMEMODE_POSSESSION:

		possession.ForceSet( Val, CVAR_Bool );
		break;
	case GAMEMODE_TEAMPOSSESSION:

		teampossession.ForceSet( Val, CVAR_Bool );
		break;
	case GAMEMODE_TEAMGAME:

		teamgame.ForceSet( Val, CVAR_Bool );
		break;
	case GAMEMODE_CTF:

		ctf.ForceSet( Val, CVAR_Bool );
		break;
	case GAMEMODE_ONEFLAGCTF:

		oneflagctf.ForceSet( Val, CVAR_Bool );
		break;
	case GAMEMODE_SKULLTAG:

		skulltag.ForceSet( Val, CVAR_Bool );
		break;
	case GAMEMODE_DOMINATION:

		domination.ForceSet( Val, CVAR_Bool );
		break;
	default:
		break;
	}

	// [AK] Reset the scoreboard to update the usability of the columns.
	SCOREBOARD_Reset( );
}

//*****************************************************************************
//
MODIFIER_e GAMEMODE_GetModifier( void )
{
	if ( instagib )
		return MODIFIER_INSTAGIB;
	else if ( buckshot )
		return MODIFIER_BUCKSHOT;
	else
		return MODIFIER_NONE;
}

//*****************************************************************************
//
void GAMEMODE_SetModifier( MODIFIER_e Modifier )
{
	UCVarValue	 Val;

	// Turn them all off.
	Val.Bool = false;
	instagib.ForceSet( Val, CVAR_Bool );
	buckshot.ForceSet( Val, CVAR_Bool );

	// Turn the selected one on.
	Val.Bool = true;
	switch ( Modifier )
	{
	case MODIFIER_INSTAGIB:
		instagib.ForceSet( Val, CVAR_Bool );
		break;
	case MODIFIER_BUCKSHOT:
		buckshot.ForceSet( Val, CVAR_Bool );
		break;
	default:
		break;
	}
}

//*****************************************************************************
//
ULONG GAMEMODE_GetCountdownTicks( void )
{
	if ( g_CurrentGameMode == GAMEMODE_SURVIVAL )
		return ( SURVIVAL_GetCountdownTicks( ));
	else if ( g_CurrentGameMode == GAMEMODE_INVASION )
		return ( INVASION_GetCountdownTicks( ));
	else if ( g_CurrentGameMode == GAMEMODE_DUEL )
		return ( DUEL_GetCountdownTicks( ));
	else if (( g_CurrentGameMode == GAMEMODE_LASTMANSTANDING ) || ( g_CurrentGameMode == GAMEMODE_TEAMLMS ))
		return ( LASTMANSTANDING_GetCountdownTicks( ));
	else if (( g_CurrentGameMode == GAMEMODE_POSSESSION ) || ( g_CurrentGameMode == GAMEMODE_TEAMPOSSESSION ))
		return ( POSSESSION_GetCountdownTicks( ));

	// [AK] The other gamemodes don't have a countdown, so just return zero.
	return ( 0 );
}

//*****************************************************************************
//
player_t *GAMEMODE_GetArtifactCarrier( void )
{
	for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if ( playeringame[ulIdx] == false )
			continue;

		// [AK] Is this player carrying the terminator artifact?
		if ( g_CurrentGameMode == GAMEMODE_TERMINATOR )
		{
			if ( players[ulIdx].cheats2 & CF2_TERMINATORARTIFACT )
				return ( &players[ulIdx] );
		}
		// [AK] Is this player carrying the possession artifact?
		else if (( g_CurrentGameMode == GAMEMODE_POSSESSION ) || ( g_CurrentGameMode == GAMEMODE_TEAMPOSSESSION ))
		{
			if ( players[ulIdx].cheats2 & CF2_POSSESSIONARTIFACT )
				return ( &players[ulIdx] );
		}
		// [AK] Is this player carrying the white flag?
		else if (( players[ulIdx].mo ) && ( players[ulIdx].mo->FindInventory( PClass::FindClass( "WhiteFlag" ), true )))
		{
			return ( &players[ulIdx] );
		}
	}

	return ( NULL );
}

//*****************************************************************************
//
void GAMEMODE_SetLimit( GAMELIMIT_e GameLimit, int value )
{
	UCVarValue Val;

	if ( GameLimit == GAMELIMIT_TIME )
	{
		Val.Float = FIXED2FLOAT( value );
		GAMEMODE_SetGameplaySetting( &timelimit, Val, CVAR_Float );
	}
	else
	{
		FBaseCVar *pCVar = NULL;
		Val.Int = value;

		switch ( GameLimit )
		{
			case GAMELIMIT_FRAGS:
				pCVar = &fraglimit;
				break;

			case GAMELIMIT_POINTS:
				pCVar = &pointlimit;
				break;

			case GAMELIMIT_DUELS:
				pCVar = &duellimit;
				break;

			case GAMELIMIT_WINS:
				pCVar = &winlimit;
				break;

			case GAMELIMIT_WAVES:
				pCVar = &wavelimit;
				break;

			default:
				I_Error( "GAMEMODE_SetLimit: Unhandled GameLimit\n." );
				break;
		}

		GAMEMODE_SetGameplaySetting( pCVar, Val, CVAR_Int );
	}
}

//*****************************************************************************
//
void GAMEMODE_SetGameplaySetting( FBaseCVar *pCVar, UCVarValue Val, ECVarType Type )
{
	GAMEPLAYSETTING_s *pSetting = NULL;
	bool bWasLocked = false;

	// [AK] Check if this CVar was already configured in the current game mode.
	for ( unsigned int i = 0; i < g_GameModes[g_CurrentGameMode].GameplaySettings.Size( ); i++ )
	{
		if ( g_GameModes[g_CurrentGameMode].GameplaySettings[i].pCVar != pCVar )
			continue;

		// [AK] CVars that are "offlineonly" should only be set in offline games, and
		// CVars that are "onlineonly" should only be set in online games.
		if ( g_GameModes[g_CurrentGameMode].GameplaySettings[i].IsOutOfScope( ))
			continue;

		pSetting = &g_GameModes[g_CurrentGameMode].GameplaySettings[i];
		break;
	}

	// [AK] If this CVar is supposed to be locked, then temporarily disable the lock.
	if ( pSetting != NULL )
	{
		bWasLocked = pSetting->bIsLocked;
		pSetting->bIsLocked = false;
	}

	pCVar->ForceSet( Val, Type );

	// [AK] After changing the value of the CVar, its saved value must also be updated.
	// This assumes that the function's arguments "Val" and "Type" are equal to the
	// corresponding members in GAMESETTING_s of the same CVar.
	// Restore the lock too, if necessary.
	if ( pSetting != NULL )
	{
		pSetting->bIsLocked = bWasLocked;
		pSetting->Val = Val;
	}
}

//*****************************************************************************
//
bool GAMEMODE_IsGameplaySettingLocked( FBaseCVar *pCVar )
{
	for ( unsigned int i = 0; i < g_GameModes[g_CurrentGameMode].GameplaySettings.Size( ); i++ )
	{
		if ( g_GameModes[g_CurrentGameMode].GameplaySettings[i].bIsLocked == false )
			continue;

		// [AK] If this CVar matches one that's locked on the list, then it's obviously locked.
		if ( pCVar == g_GameModes[g_CurrentGameMode].GameplaySettings[i].pCVar )
		{
			// [AK] CVars that are "offlineonly" are only locked in offline games, and if they're
			// "onlineonly" then they're only locked in online games.
			if ( g_GameModes[g_CurrentGameMode].GameplaySettings[i].IsOutOfScope( ) == false )
				return true;
		}
	}

	return false;
}

//*****************************************************************************
//
void GAMEMODE_ResetGameplaySettings( bool bLockedOnly, bool bResetToDefault )
{
	// [AK] Don't let clients reset the CVars by themselves. The server will update them accordingly.
	if ( NETWORK_InClientMode( ))
		return;

	for ( unsigned int i = 0; i < g_GameModes[g_CurrentGameMode].GameplaySettings.Size( ); i++ )
	{
		GAMEPLAYSETTING_s *const pSetting = &g_GameModes[g_CurrentGameMode].GameplaySettings[i];

		// [AK] Only reset unlocked CVars if we need to. Also, CVars that are "offlineonly" should only
		// be reset in offline games, and CVars that are "onlineonly" should only be reset in online games.
		if ((( bLockedOnly ) && ( pSetting->bIsLocked == false )) || ( pSetting->IsOutOfScope( )))
			continue;

		// [AK] Do we also want to reset this CVar to its default value?
		if ( bResetToDefault )
			pSetting->Val = pSetting->DefaultVal;

		GAMEMODE_SetGameplaySetting( pSetting->pCVar, pSetting->Val, pSetting->Type );
	}
}
