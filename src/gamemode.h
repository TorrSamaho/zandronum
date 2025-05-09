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
// Filename: gamemode.h
//
// Description: 
//
//-----------------------------------------------------------------------------

#ifndef __GAMEMODE_H__
#define __GAMEMODE_H__

#include "bots.h"
#include "c_cvars.h"
#include "doomtype.h"
#include "doomdef.h"

//*****************************************************************************
//	DEFINES

// [BB] The GMF_* and GAMEMODE_* defines are now enums with "EnumToString" support and need
// to go into a separate header for technical reasons.
#include "gamemode_enums.h"

#define GAMETYPE_MASK ( GMF_COOPERATIVE | GMF_DEATHMATCH | GMF_TEAMGAME )
#define EARNTYPE_MASK ( GMF_PLAYERSEARNKILLS | GMF_PLAYERSEARNFRAGS | GMF_PLAYERSEARNPOINTS | GMF_PLAYERSEARNWINS )

// [CK] Event defines
#define GAMEEVENT_CAPTURE_NOASSIST -1	// The third arg meaning no player assisted
#define GAMEEVENT_RETURN_TIMEOUTRETURN 0
#define GAMEEVENT_RETURN_PLAYERRETURN 1

//*****************************************************************************
//  EXTERNAL CONSOLE VARIABLES

EXTERN_CVAR( Bool, instagib )
EXTERN_CVAR( Bool, buckshot )
EXTERN_CVAR( Bool, sv_suddendeath )
EXTERN_CVAR( Int, sv_maxlives )

EXTERN_CVAR( String, lobby )

//*****************************************************************************
typedef enum
{
	MODIFIER_NONE,
	MODIFIER_INSTAGIB,
	MODIFIER_BUCKSHOT,

	NUM_MODIFIERS

} MODIFIER_e;

//*****************************************************************************
typedef enum
{
	GAMESTATE_UNSPECIFIED = -1,
	GAMESTATE_WAITFORPLAYERS = 0,
	GAMESTATE_COUNTDOWN,
	GAMESTATE_INPROGRESS,
	GAMESTATE_INRESULTSEQUENCE
} GAMESTATE_e;

//*****************************************************************************
typedef enum
{
	GAMEEVENT_PLAYERFRAGS = 0,
	GAMEEVENT_MEDALS,
	GAMEEVENT_CAPTURES,
	GAMEEVENT_TOUCHES,	// When the player touches a flag (or skull)
	GAMEEVENT_RETURNS,	// When the flag returns to it's stand (or skull)
	GAMEEVENT_ROUND_STARTS,
	GAMEEVENT_ROUND_ENDS,
	GAMEEVENT_ROUND_ABORTED,
	GAMEEVENT_CHAT,
	GAMEEVENT_PLAYERCONNECT,
	GAMEEVENT_ACTOR_SPAWNED,
	GAMEEVENT_ACTOR_DAMAGED,
	GAMEEVENT_ACTOR_DAMAGED_PREMOD,
	GAMEEVENT_DOMINATION_CONTROL,
	GAMEEVENT_DOMINATION_POINT,
	GAMEEVENT_PLAYERLEAVESSERVER,
	GAMEEVENT_LEVEL_INIT,
	GAMEEVENT_JOINQUEUECHANGED,
	GAMEEVENT_DOMINATION_PRECONTROL,
	GAMEEVENT_DOMINATION_CONTEST,
} GAMEEVENT_e;

//*****************************************************************************
typedef enum
{
	GAMELIMIT_FRAGS = 0,
	GAMELIMIT_TIME,
	GAMELIMIT_POINTS,
	GAMELIMIT_DUELS,
	GAMELIMIT_WINS,
	GAMELIMIT_WAVES,
} GAMELIMIT_e;

//*****************************************************************************
typedef enum
{
	GAMESCOPE_OFFLINEANDONLINE = 0,
	GAMESCOPE_OFFLINEONLY,
	GAMESCOPE_ONLINEONLY
} GAMESCOPE_e;

//*****************************************************************************
//	STRUCTURES

struct GAMEPLAYSETTING_s
{
	// [AK] A pointer to the CVar.
	FBaseCVar *pCVar;

	// [AK] The type of value to set for this CVar.
	ECVarType Type;

	// [AK] The value that this CVar should be set to.
	UCVarValue Val;

	// [AK] The default value of this CVar that is restored when a new game starts.
	// This is in case the CVar's value is changed with SetGameplaySetting in ACS.
	UCVarValue DefaultVal;

	// [AK] The CVar is locked and cannot be changed from the console.
	bool bIsLocked;

	// [AK] What kind of game (i.e. offline, online, or both) is the CVar's value applied.
	GAMESCOPE_e Scope;

	bool IsOutOfScope( void );

};

//*****************************************************************************
typedef struct
{
	// Flags for this game mode.
	ULONG ulFlags;

	// [RC] The name of this game mode.
	FString Name;

	// This is what's displayed in the internal browser for a server's game mode.
	FString ShortName;

	// This is the name of the texture that displays when we press the F1 key in
	// this game mode.
	FString F1Texture;

	// [AK] The announcer sound (e.g. "welcome to...") that plays at the start of a level.
	FString WelcomeSound;

	// [AK] All CVars that we want to configure for this game mode.
	TArray<GAMEPLAYSETTING_s> GameplaySettings;

} GAMEMODE_s;

//*****************************************************************************
//	PROTOTYPES

void		GAMEMODE_Tick( void );
void		GAMEMODE_ParseGameModeBlock ( FScanner &sc, const GAMEMODE_e GameMode );
void		GAMEMODE_ParseGameSettingBlock ( FScanner &sc, const GAMEMODE_e GameMode, bool bLockCVars, bool bResetCVars = false );
void		GAMEMODE_ParseGameModeInfo( void );
ULONG		GAMEMODE_GetFlags( GAMEMODE_e GameMode );
ULONG		GAMEMODE_GetCurrentFlags( void );
const char	*GAMEMODE_GetShortName( GAMEMODE_e GameMode );
const char	*GAMEMODE_GetName( GAMEMODE_e GameMode );
const char	*GAMEMODE_GetCurrentName( void );
const char	*GAMEMODE_GetF1Texture( GAMEMODE_e GameMode );
const char	*GAMEMODE_GetWelcomeSound( GAMEMODE_e GameMode );
void		GAMEMODE_DetermineGameMode( void );
bool		GAMEMODE_IsGameInCountdown( void );
bool		GAMEMODE_IsGameInProgress( void );
bool		GAMEMODE_IsGameInResultSequence( void );
bool		GAMEMODE_IsGameInProgressOrResultSequence( void );
bool		GAMEMODE_IsLobbyMap( void );
bool		GAMEMODE_IsLobbyMap( const char* levelinfo );
bool		GAMEMODE_IsNextMapCvarLobby( void );
bool		GAMEMODE_IsTimelimitActive( void );
void		GAMEMODE_GetTimeLeftString( FString &TimeLeftString );
void		GAMEMODE_RespawnDeadPlayers( playerstate_t deadSpectatorState = PST_REBORNNOINVENTORY, playerstate_t deadPlayerState = PST_REBORNNOINVENTORY );
void		GAMEMODE_RespawnDeadPlayersAndPopQueue( playerstate_t deadSpectatorState = PST_REBORNNOINVENTORY, playerstate_t deadPlayerState = PST_REBORNNOINVENTORY );
void		GAMEMODE_RespawnAllPlayers( BOTEVENT_e BotEvent = NUM_BOTEVENTS, playerstate_t PlayerState = PST_ENTER );
void		GAMEMODE_SpawnPlayer( const ULONG ulPlayer, bool bClientUpdate = true );
void		GAMEMODE_ResetPlayersKillCount( const bool bInformClients );
bool		GAMEMODE_AreSpectatorsForbiddenToChatToPlayers( const bool doVoice );
bool		GAMEMODE_IsClientForbiddenToChatToPlayers( const ULONG client, const bool doVoice );
bool		GAMEMODE_PreventPlayersFromJoining( ULONG ulExcludePlayer = MAXPLAYERS );
bool		GAMEMODE_AreLivesLimited( void );
bool		GAMEMODE_ShouldPlayerLoseLife( void );
bool		GAMEMODE_IsPlayerCarryingGameModeItem( player_t *player );
unsigned int	GAMEMODE_GetMaxLives( void );
void		GAMEMODE_AdjustActorSpawnFlags ( AActor *pActor );
void		GAMEMODE_SpawnSpecialGamemodeThings ( void );
void		GAMEMODE_ResetSpecalGamemodeStates ( void );
bool		GAMEMODE_IsSpectatorAllowedSpecial ( const int Special );
bool		GAMEMODE_IsHandledSpecial ( AActor *Activator, int Special );
GAMESTATE_e	GAMEMODE_GetState ( void );
void		GAMEMODE_SetState ( GAMESTATE_e GameState );
LONG		GAMEMODE_HandleEvent ( const GAMEEVENT_e Event, AActor *pActivator = NULL, const int DataOne = 0, const int DataTwo = 0, const bool bRunNow = false, const int OverrideResult = 1 );
void		GAMEMODE_HandleSpawnEvent( AActor *actor );
bool		GAMEMODE_HandleDamageEvent ( AActor *target, AActor *inflictor, AActor *source, int &damage, FName mod, bool bBeforeArmor = false );
LONG		GAMEMODE_GetEventResult ( void );
void		GAMEMODE_SetEventResult ( LONG lResult );
GAMEMODE_e	GAMEMODE_GetCurrentMode( void );
void		GAMEMODE_SetCurrentMode( GAMEMODE_e GameMode );
MODIFIER_e	GAMEMODE_GetModifier( void );
void		GAMEMODE_SetModifier( MODIFIER_e Modifier );
ULONG		GAMEMODE_GetCountdownTicks( void );
player_t	*GAMEMODE_GetArtifactCarrier( void );
void		GAMEMODE_SetLimit( GAMELIMIT_e GameLimit, int value );
void		GAMEMODE_SetGameplaySetting( FBaseCVar *pCVar, UCVarValue Val, ECVarType Type );
bool		GAMEMODE_IsGameplaySettingLocked( FBaseCVar *pCVar );
void		GAMEMODE_ResetGameplaySettings( bool bLockedOnly, bool bResetToDefault );

#endif // __GAMEMODE_H__
