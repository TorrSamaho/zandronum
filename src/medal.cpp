//-----------------------------------------------------------------------------
//
// Skulltag Source
// Copyright (C) 2002 Brad Carney
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
// Filename: medal.cpp
//
// Description: Contains medal routines and globals
//
//-----------------------------------------------------------------------------

#include "a_sharedglobal.h"
#include "announcer.h"
#include "chat.h"
#include "cl_demo.h"
#include "deathmatch.h"
#include "doomstat.h"
#include "duel.h"
#include "gi.h"
#include "gamemode.h"
#include "g_level.h"
#include "info.h"
#include "medal.h"
#include "p_local.h"
#include "d_player.h"
#include "network.h"
#include "s_sound.h"
#include "st_stuff.h"
#include "sv_commands.h"
#include "team.h"
#include "templates.h"
#include "v_text.h"
#include "v_video.h"
#include "w_wad.h"
#include "p_acs.h"
#include "st_hud.h"
#include "c_console.h"
#include "voicechat.h"
#include "p_tick.h"

// [AK] Implement the string table and the conversion functions for the medal enums.
#define GENERATE_ENUM_STRINGS  // Start string generation
#include "medal_enums.h"
#undef GENERATE_ENUM_STRINGS   // Stop string generation

//*****************************************************************************
//	VARIABLES

// A list of all defined medals.
static	TArray<MEDAL_t *>	medalList;

// Any medals that players have recently earned that need to be displayed.
static	MEDALQUEUE_t		medalQueue[MAXPLAYERS];

// Has the first frag medal been awarded this round?
static	bool			g_bFirstFragAwarded;

// [Dusk] Need this from p_interaction.cpp for spawn telefrag checking
extern FName MeansOfDeath;

//*****************************************************************************
//	CONSOLE VARIABLES

CVAR( Bool, cl_medals, true, CVAR_ARCHIVE )
CVAR( Bool, cl_icons, true, CVAR_ARCHIVE )

// [AK] Determines when ally icons should appear.
CUSTOM_CVAR( Int, cl_showallyicon, SHOW_ICON_TEAMS_ONLY, CVAR_ARCHIVE )
{
	const int clampedValue = clamp<int>( self, SHOW_ICON_NEVER, SHOW_ICON_ALWAYS );

	if ( self != clampedValue )
		self = clampedValue;
}

// [AK] Determines when enemy icons should appear.
CUSTOM_CVAR( Int, cl_showenemyicon, SHOW_ICON_NEVER, CVAR_ARCHIVE )
{
	const int clampedValue = clamp<int>( self, SHOW_ICON_NEVER, SHOW_ICON_ALWAYS );

	if ( self != clampedValue )
		self = clampedValue;
}

//*****************************************************************************
//	PROTOTYPES

ULONG	medal_GetDesiredIcon( player_t *pPlayer, AInventory *&pTeamItem );
void	medal_TriggerMedal( ULONG ulPlayer );
void	medal_SelectIcon( player_t *player );
void	medal_CheckForFirstFrag( ULONG ulPlayer );
void	medal_CheckForDomination( ULONG ulPlayer );
void	medal_CheckForFisting( ULONG ulPlayer );
void	medal_CheckForExcellent( ULONG ulPlayer );
void	medal_CheckForTermination( ULONG ulDeadPlayer, ULONG ulPlayer );
void	medal_CheckForLlama( ULONG ulDeadPlayer, ULONG ulPlayer );
void	medal_CheckForYouFailIt( ULONG ulPlayer );
bool	medal_PlayerHasCarrierIcon( player_t *player );
bool	medal_CanShowAllyOrEnemyIcon( const bool checkEnemyIcon );

//*****************************************************************************
//	FUNCTIONS

void MEDAL_Construct( void )
{
	int currentLump, lastLump = 0;

	while (( currentLump = Wads.FindLump( "MEDALDEF", &lastLump )) != -1 )
	{
		FScanner sc( currentLump );

		while ( sc.GetString( ))
		{
			MEDAL_t *medal = MEDAL_GetMedal( sc.String );

			// [AK] If the medal isn't already defined, create a new one.
			if ( medal == nullptr )
			{
				medal = new MEDAL_t( sc.String );
				medalList.Push( medal );
			}

			sc.MustGetToken( '{' );

			while ( sc.CheckToken( '}' ) == false )
			{
				sc.MustGetString( );
				FString command = sc.String;

				if ( command.CompareNoCase( "addflag" ) == 0 )
				{
					medal->flags |= sc.MustGetEnumName( "medal flag", "MEDALFLAG_", GetValueMedalFlag );
					continue;
				}
				else if ( command.CompareNoCase( "removeflag" ) == 0 )
				{
					medal->flags &= ~sc.MustGetEnumName( "medal flag", "MEDALFLAG_", GetValueMedalFlag );
					continue;
				}

				sc.MustGetToken( '=' );
				sc.MustGetToken( TK_StringConst );

				// [AK] Throw a fatal error if an empty value was passed.
				if ( sc.StringLen == 0 )
					sc.ScriptError( "Got an empty string for the value of '%s'.", command.GetChars( ));

				if (( command.CompareNoCase( "icon" ) == 0 ) || ( command.CompareNoCase( "scoreboardicon" ) == 0 ))
				{
					FTextureID &icon = ( command.CompareNoCase( "icon" ) == 0 ) ? medal->icon : medal->scoreboardIcon;
					icon = TexMan.CheckForTexture( sc.String, FTexture::TEX_MiscPatch );

					// [AK] Make sure that the icon exists.
					if ( icon.isValid( ) == false )
						sc.ScriptError( "Icon '%s' wasn't found.", sc.String );
				}
				else if ( command.CompareNoCase( "class" ) == 0 )
				{
					medal->iconClass = PClass::FindClass( sc.String );

					// [AK] Make sure that the class exists.
					if ( medal->iconClass == nullptr )
						sc.ScriptError( "Class '%s' wasn't found.", sc.String );

					// [AK] Also make sure it inherits from FloatyIcon.
					if ( medal->iconClass->IsDescendantOf( RUNTIME_CLASS( AFloatyIcon )) == false )
						sc.ScriptError( "Class '%s' is not a descendant of 'FloatyIcon'.", sc.String );

					medal->iconState = nullptr;
				}
				else if ( command.CompareNoCase( "state" ) == 0 )
				{
					if ( medal->iconClass == nullptr )
						sc.ScriptError( "Medal '%s' needs a class before specifying a state.", medal->name.GetChars( ));

					medal->iconState = medal->iconClass->ActorInfo->FindStateByString( sc.String, true );

					// [AK] Make sure that the passed state exists.
					if ( medal->iconState == nullptr )
						sc.ScriptError( "State '%s' wasn't found in '%s'.", sc.String, medal->iconClass->TypeName.GetChars( ));
				}
				else if ( command.CompareNoCase( "text" ) == 0 )
				{
					medal->text = sc.String[0] == '$' ? GStrings( sc.String + 1 ) : sc.String;
				}
				else if ( command.CompareNoCase( "textcolor" ) == 0 )
				{
					medal->textColor = V_FindFontColor( sc.String );
				}
				else if ( command.CompareNoCase( "quantitycolor" ) == 0 )
				{
					medal->quantityColor = sc.String;
				}
				else if ( command.CompareNoCase( "announcerentry" ) == 0 )
				{
					medal->announcerEntry = sc.String;
				}
				else if ( command.CompareNoCase( "lowermedal" ) == 0 )
				{
					medal->lowerMedal = MEDAL_GetMedal( sc.String );

					// [AK] Make sure that the passed medal exists.
					if ( medal->lowerMedal == nullptr )
						sc.ScriptError( "Medal '%s' wasn't found.", sc.String );

					// [AK] Don't allow this medal to be its own lower medal.
					if ( medal->lowerMedal == medal )
						sc.ScriptError( "Medal '%s' can't be a lower medal of itself.", sc.String );
				}
				else if ( command.CompareNoCase( "sound" ) == 0 )
				{
					medal->sound = sc.String;
				}
				else
				{
					sc.ScriptError( "Unknown option '%s'.", command.GetChars( ));
				}
			}

			// [AK] Throw a fatal error if this medal has no icon, class or state.
			if ( medal->icon.isValid( ) == false )
				sc.ScriptError( "Medal '%s' has no icon.", medal->name.GetChars( ));
			else if ( medal->iconClass == nullptr )
				sc.ScriptError( "Medal '%s' has no defined class.", medal->name.GetChars( ));
			else if ( medal->iconState == nullptr )
				sc.ScriptError( "Medal '%s' has no defined state.", medal->name.GetChars( ));
		}
	}

	g_bFirstFragAwarded = false;
}

//*****************************************************************************
//
void MEDAL_Tick( void )
{
	const bool isPaused = (( paused ) || ( P_CheckTickerPaused( )));

	for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		// No need to do anything.
		if ( playeringame[ulIdx] == false )
			continue;

		// Tick down the duration of the medal on the top of the queue. If time
		// has expired on this medal, pop it and potentially trigger a new one.
		if (( isPaused == false ) && ( medalQueue[ulIdx].medals.empty( ) == false ) && ( medalQueue[ulIdx].ticks ) && ( --medalQueue[ulIdx].ticks == 0 ))
		{
			medalQueue[ulIdx].medals.erase( medalQueue[ulIdx].medals.begin( ));

			// If a new medal is now at the top of the queue, trigger it.
			if ( medalQueue[ulIdx].medals.empty( ) == false )
			{
				medalQueue[ulIdx].ticks = MEDAL_ICON_DURATION;
				medal_TriggerMedal( ulIdx );
			}
			// If there isn't, just delete the medal that has been displaying.
			else if ( players[ulIdx].pIcon != nullptr )
			{
				players[ulIdx].pIcon->Destroy( );
				players[ulIdx].pIcon = nullptr;
			}
		}

		// [BB] We don't need to know what medal_GetDesiredIcon puts into pTeamItem, but we still need to supply it as argument.
		AInventory *pTeamItem;
		const ULONG ulDesiredSprite = medal_GetDesiredIcon( &players[ulIdx], pTeamItem );

		// If we're not currently displaying a medal for the player, potentially display
		// some other type of icon.
		// [BB] Also let carrier icons override medals.
		if (( medalQueue[ulIdx].medals.empty( )) || (( ulDesiredSprite >= SPRITE_WHITEFLAG ) && ( ulDesiredSprite <= SPRITE_TEAMITEM )))
			medal_SelectIcon( &players[ulIdx] );

		// [BB] If the player is being awarded a medal at the moment but has no icon, restore the medal.
		// This happens when the player respawns while being awarded a medal.
		// [AK] Don't spawn the icon while the player is still respawning.
		if (( medalQueue[ulIdx].medals.empty( ) == false ) && ( players[ulIdx].pIcon == nullptr ) && ( players[ulIdx].playerstate != PST_REBORN ))
			medal_TriggerMedal( ulIdx );

		// [BB] Remove any old carrier icons.
		medal_PlayerHasCarrierIcon( &players[ulIdx] );
	}
}

//*****************************************************************************
//
void MEDAL_Render( void )
{
	if (( players[consoleplayer].camera == nullptr ) || ( players[consoleplayer].camera->player == nullptr ))
		return;

	const unsigned int player = static_cast<unsigned>( players[consoleplayer].camera->player - players );

	// [TP] Sanity check
	if ( PLAYER_IsValidPlayer( player ) == false )
		return;

	// If the player doesn't have a medal to be drawn, don't do anything.
	if ( medalQueue[player].medals.empty( ))
		return;

	const MEDAL_t *medal = medalQueue[player].medals[0];
	const fixed_t alpha = medalQueue[player].ticks > TICRATE ? OPAQUE : static_cast<fixed_t>( OPAQUE * ( static_cast<float>( medalQueue[player].ticks ) / TICRATE ));

	// Get the graphic and text name from the global array.
	FTexture *icon = TexMan( medal->icon );
	FString string = medal->text.GetChars( );

	// Determine how much actual screen space it will take to render the amount of
	// medals the player has received up until this point.
	// [AK] Add at least one pixel worth of space between each medal.
	const int length = medal->awardedCount[player] * icon->GetScaledWidth( ) + ( medal->awardedCount[player] - 1 );

	// If that length is greater then the screen width, display the medals as "<icon> <name> X <num>".
	// Otherwise, the medal icon is displayed <num> times centered on the screen.
	// [AK] Also do this if the medal has the ALWAYSSHOWQUANTITY flag enabled.
	const bool showQuantity = (( length >= 320 ) || ( medal->flags & MEDALFLAG_ALWAYSSHOWQUANTITY ));

	if ( showQuantity )
	{
		if ( medal->quantityColor.IsNotEmpty( ))
		{
			string += TEXTCOLOR_ESCAPE;
			string.AppendFormat( "[%s]", medal->quantityColor.GetChars( ));
		}

		string.AppendFormat( " X %u", medal->awardedCount[player] );
	}

	int xPos = ( SCREENWIDTH - CleanXfac * SmallFont->StringWidth( string.GetChars( ))) / 2;
	int yPos = ( viewheight <= ST_Y ? ST_Y : SCREENHEIGHT ) - ( 4 + SmallFont->StringHeight( string.GetChars( ))) * CleanYfac;

	screen->DrawText( SmallFont, medal->textColor, xPos, yPos, string, DTA_CleanNoMove, true, DTA_Alpha, alpha, TAG_DONE );

	// [AK] For some reason, if the medal's icon is a sprite instead of a graphic,
	// it appears one pixel higher than it should. So, if it's a sprite, move it up
	// by three pixels. Otherwise, move it up by four pixels.
	yPos -= (( icon->UseType == FTexture::TEX_Sprite ? 3 : 4 ) + icon->GetScaledHeight( )) * CleanYfac;

	if ( showQuantity )
	{
		xPos = ( SCREENWIDTH - CleanXfac * icon->GetScaledWidth( )) / 2;

		screen->DrawTexture( icon, xPos, yPos,
			DTA_CleanNoMove, true,
			DTA_LeftOffset, 0,
			DTA_TopOffset, 0,
			DTA_Alpha, alpha,
			TAG_DONE );
	}
	else
	{
		xPos = ( SCREENWIDTH - CleanXfac * length ) / 2;

		for ( unsigned int i = 0; i < medal->awardedCount[player]; i++ )
		{
			screen->DrawTexture( icon, xPos, yPos,
				DTA_CleanNoMove, true,
				DTA_LeftOffset, 0,
				DTA_TopOffset, 0,
				DTA_Alpha, alpha,
				TAG_DONE );

			xPos += CleanXfac * ( icon->GetScaledWidth( ) + 1 );
		}
	}
}

//*****************************************************************************
//*****************************************************************************
//
bool MEDAL_GiveMedal( const ULONG player, const ULONG medalIndex, const bool silent )
{
	// [CK] Do not award if it's a countdown sequence
	// [AK] Or if we're playing a game mode where players don't earn medals.
	if (( GAMEMODE_IsGameInCountdown( )) || (( GAMEMODE_GetCurrentFlags( ) & GMF_PLAYERSEARNMEDALS ) == false ))
		return false;

	// [AK] Make sure that the player and medal are valid.
	if (( player >= MAXPLAYERS ) || ( players[player].mo == nullptr ) || ( medalIndex >= medalList.Size( )))
		return false;

	// [AK] Make sure that medals are allowed.
	if ( zadmflags & ZADF_NO_MEDALS )
		return false;

	MEDAL_t *const medal = medalList[medalIndex];

	// [CK] Trigger events if a medal is received
	// [AK] If the event returns 0, then the player doesn't receive the medal.
	if ( GAMEMODE_HandleEvent( GAMEEVENT_MEDALS, players[player].mo, ACS_PushAndReturnDynamicString( medal->name.GetChars( )), 0, true ) == 0 )
		return false;

	// Increase the player's count of this type of medal.
	medal->awardedCount[player]++;

	if (( NETWORK_GetState( ) != NETSTATE_SERVER ) && ( cl_medals ) && ( silent == false ))
	{
		// [AK] Check if the medal being give is already in this player's queue.
		std::vector<MEDAL_t *> &queue = medalQueue[player].medals;
		auto iterator = std::find( queue.begin( ), queue.end( ), medal );

		// [AK] If not, then check if a suboordinate of the new medal is already
		// in the list. If so, then the lower medal will be replaced. Otherwise,
		// the new medal gets added to the end of the queue.
		if ( iterator == queue.end( ))
		{
			iterator = std::find( queue.begin( ), queue.end( ), medal->lowerMedal );

			if ( iterator != queue.end( ))
			{
				*iterator = medal;
			}
			else
			{
				queue.push_back( medal );

				// [AK] Changing the size of the queue invalidates the iterator,
				// so set it to the last element in the queue. In case the queue
				// was empty before (there's only one element now, which is what
				// just got added), then the iterator will now point to the start
				// so the timer gets reset properly.
				iterator = queue.end( ) - 1;
			}
		}

		// [AK] If the new medal is at the start. reset the timer and trigger it.
		if ( iterator == queue.begin( ))
		{
			medalQueue[player].ticks = MEDAL_ICON_DURATION;
			medal_TriggerMedal( player );
		}
	}

	// If this player is a bot, tell it that it received a medal.
	if ( players[player].pSkullBot )
	{
		players[player].pSkullBot->m_lLastMedalReceived = medalIndex;
		players[player].pSkullBot->PostEvent( BOTEVENT_RECEIVEDMEDAL );
	}

	// [AK] If we're the server, tell clients that this player earned a medal.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVERCOMMANDS_GivePlayerMedal( player, medalIndex, silent );

	return true;
}

//*****************************************************************************
//
bool MEDAL_GiveMedal( const ULONG player, const FName medalName, const bool silent )
{
	const int index = MEDAL_GetMedalIndex( medalName );
	return ( index != -1 ? MEDAL_GiveMedal( player, index, silent ) : false );
}

//*****************************************************************************
//
void MEDAL_SetMedalAwardedCount( const unsigned int player, const unsigned int medalIndex, const unsigned int count )
{
	if (( PLAYER_IsValidPlayer( player ) == false ) || ( medalIndex >= medalList.Size( )))
		return;

	medalList[medalIndex]->awardedCount[player] = count;
}

//*****************************************************************************
//
int MEDAL_GetMedalIndex( const FName medalName )
{
	for ( unsigned int i = 0; i < medalList.Size( ); i++ )
	{
		if ( medalList[i]->name == medalName )
			return i;
	}

	return -1;
}

//*****************************************************************************
//
MEDAL_t *MEDAL_GetMedal( const FName medalName )
{
	const int index = MEDAL_GetMedalIndex( medalName );
	return ( index != -1 ? medalList[index] : nullptr );
}

//*****************************************************************************
//
MEDAL_t *MEDAL_GetDisplayedMedal( const ULONG player )
{
	if (( player < MAXPLAYERS ) && ( medalQueue[player].medals.empty( ) == false ))
		return medalQueue[player].medals[0];

	return nullptr;
}

//*****************************************************************************
//
void MEDAL_RetrieveAwardedMedals( const unsigned int player, TArray<MEDAL_t *> &list )
{
	list.Clear( );

	if ( PLAYER_IsValidPlayer( player ))
	{
		for ( unsigned int i = 0; i < medalList.Size( ); i++ )
		{
			if ( medalList[i]->awardedCount[player] > 0 )
				list.Push( medalList[i] );
		}
	}
}

//*****************************************************************************
//
void MEDAL_ResetPlayerMedals( const ULONG player, const bool resetAll )
{
	if ( player >= MAXPLAYERS )
		return;

	// Reset the number of medals this player has.
	// [AK] Let players keep any medals that persist between levels unless all
	// of the medals must be reset (e.g. when they leave the game).
	for ( unsigned int i = 0; i < medalList.Size( ); i++ )
	{
		if (( resetAll ) || (( medalList[i]->flags & MEDALFLAG_KEEPBETWEENLEVELS ) == false ))
			medalList[i]->awardedCount[player] = 0;
	}

	medalQueue[player].medals.clear( );
	medalQueue[player].ticks = 0;
}

//*****************************************************************************
//
void MEDAL_PlayerDied( ULONG ulPlayer, ULONG ulSourcePlayer )
{
	if ( PLAYER_IsValidPlayerWithMo ( ulPlayer ) == false )
		return;

	// [Dusk] As players do not get frags for spawn telefrags, they shouldn't
	// get medals for that either.
	// [AK] Neither should dying players get punished for being spawn telefragged
	// because their death count doesn't increase when they do.
	if ( MeansOfDeath != NAME_SpawnTelefrag )
	{
		// Check for domination and first frag medals.
		if (( PLAYER_IsValidPlayerWithMo( ulSourcePlayer )) &&
			( players[ulSourcePlayer].mo->IsTeammate( players[ulPlayer].mo ) == false ))
		{
			players[ulSourcePlayer].ulFragsWithoutDeath++;
			players[ulSourcePlayer].ulDeathsWithoutFrag = 0;

			medal_CheckForFirstFrag( ulSourcePlayer );
			medal_CheckForDomination( ulSourcePlayer );
			medal_CheckForFisting( ulSourcePlayer );
			medal_CheckForExcellent( ulSourcePlayer );
			medal_CheckForTermination( ulPlayer, ulSourcePlayer );
			medal_CheckForLlama( ulPlayer, ulSourcePlayer );

			players[ulSourcePlayer].ulLastFragTick = level.time;
		}

		// [BB] Don't punish being killed by a teammate (except if a player kills himself).
		if (( ulPlayer == ulSourcePlayer ) ||
			( PLAYER_IsValidPlayerWithMo( ulSourcePlayer ) == false ) ||
			( players[ulSourcePlayer].mo->IsTeammate( players[ulPlayer].mo ) == false ))
		{
			players[ulPlayer].ulDeathsWithoutFrag++;
			medal_CheckForYouFailIt( ulPlayer );
		}
	}

	players[ulPlayer].ulFragsWithoutDeath = 0;
}

//*****************************************************************************
//
void MEDAL_ResetFirstFragAwarded( void )
{
	g_bFirstFragAwarded = false;
}

//*****************************************************************************
//*****************************************************************************
//

//*****************************************************************************
// [BB, RC] Returns whether the player wears a carrier icon (flag/skull/hellstone/etc) and removes any invalid ones.
//
bool medal_PlayerHasCarrierIcon( player_t *player )
{
	bool invalid = false;
	bool hasIcon = true;

	// [BB] If the player has no icon, he obviously doesn't have a carrier icon.
	if (( player == nullptr ) || ( player->pIcon == nullptr ))
		return false;

	// Verify that our current icon is valid.
	if ( player->pIcon->bTeamItemFloatyIcon == false )
	{
		switch ( player->pIcon->currentSprite )
		{
			// White flag icon. Delete it if the player no longer has it.
			case SPRITE_WHITEFLAG:
			{
				// Delete the icon if teamgame has been turned off, or if the player
				// is not on a team.
				if (( teamgame == false ) || ( player->bOnTeam == false ))
					invalid = true;
				// Delete the white flag if the player no longer has it.
				else if ( player->mo->FindInventory( PClass::FindClass( "WhiteFlag" ), true ) == nullptr )
					invalid = true;

				break;
			}

			// Terminator artifact icon. Delete it if the player no longer has it.
			case SPRITE_TERMINATORARTIFACT:
			{
				if (( terminator == false ) || (( player->cheats2 & CF2_TERMINATORARTIFACT ) == false ))
					invalid = true;

				break;
			}

			// Possession artifact icon. Delete it if the player no longer has it.
			case SPRITE_POSSESSIONARTIFACT:
			{
				if ((( possession == false ) && ( teampossession == false )) || (( player->cheats2 & CF2_POSSESSIONARTIFACT ) == false ))
					invalid = true;

				break;
			}

			default:
				hasIcon = false;
				break;
		}
	}
	else if ( GAMEMODE_GetCurrentFlags( ) & GMF_USETEAMITEM )
	{
		invalid = ( TEAM_FindOpposingTeamsItemInPlayersInventory( player ) == nullptr );
	}

	// Remove it.
	if (( invalid ) && ( hasIcon ))
	{
		player->pIcon->Destroy( );
		player->pIcon = NULL;

		medal_TriggerMedal( player - players );
	}

	return ( hasIcon && !invalid );
}

//*****************************************************************************
//
bool medal_CanShowAllyOrEnemyIcon( const bool checkEnemyIcon )
{
	// [AK] Check if we're forbidden from seeing the desired icon.
	if ( zadmflags & ( checkEnemyIcon ? ZADF_NO_ENEMY_ICONS : ZADF_NO_ALLY_ICONS ))
		return false;

	const FIntCVar &cvar = checkEnemyIcon ? cl_showenemyicon : cl_showallyicon;
	return ((( cvar == SHOW_ICON_TEAMS_ONLY ) && ( GAMEMODE_GetCurrentFlags( ) & GMF_PLAYERSONTEAMS )) || ( cvar == SHOW_ICON_ALWAYS ));
}

//*****************************************************************************
//
void medal_TriggerMedal( ULONG ulPlayer )
{
	player_t	*pPlayer;

	pPlayer = &players[ulPlayer];

	// Servers don't actually spawn medals.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		return;

	// Make sure this player is valid and they have a medal in their queue.
	if (( pPlayer->mo == NULL ) || ( medalQueue[ulPlayer].medals.empty( )))
		return;

	const MEDAL_t *const medal = medalQueue[ulPlayer].medals[0];

	// Medals don't override carrier symbols.
	if ( !medal_PlayerHasCarrierIcon( pPlayer ))
	{
		if ( pPlayer->pIcon )
			pPlayer->pIcon->Destroy( );

		// Spawn the medal as an icon above the player and set its properties.
		pPlayer->pIcon = static_cast<AFloatyIcon *>( Spawn( medal->iconClass, pPlayer->mo->x, pPlayer->mo->y, pPlayer->mo->z, NO_REPLACE ));
		if ( pPlayer->pIcon )
		{
			pPlayer->pIcon->SetState( medal->iconState );
			// [BB] Instead of MEDAL_ICON_DURATION only use the remaining ticks of the medal as ticks for the icon.
			// It is possible that the medal is just restored because the player respawned or that the medal was
			// suppressed by a carrier icon.
			pPlayer->pIcon->lTick = medalQueue[ulPlayer].ticks;
			pPlayer->pIcon->SetTracer( pPlayer->mo );
		}
	}

	// [BB] Only announce the medal when it reaches the top of the queue. Otherwise it could be
	// announced multiple times (for instance when a carrier dies).
	if ( medalQueue[ulPlayer].ticks == MEDAL_ICON_DURATION )
	{
		// Also, locally play the announcer sound associated with this medal.
		// [Dusk] Check coop spy too
		if ( pPlayer->mo->CheckLocalView( consoleplayer ) )
		{
			if ( medal->announcerEntry.IsNotEmpty( ))
				ANNOUNCER_PlayEntry( cl_announcer, medal->announcerEntry.GetChars( ));
		}
		// If a player besides the console player got the medal, play the remote sound.
		else
		{
			// Play the sound effect associated with this medal type.
			if ( medal->sound > 0 )
				S_Sound( pPlayer->mo, CHAN_AUTO, medal->sound, 1.0f, ATTN_NORM );
		}
	}
}

//*****************************************************************************
//
ULONG medal_GetDesiredIcon( player_t *pPlayer, AInventory *&pTeamItem )
{
	ULONG ulDesiredSprite = NUM_SPRITES;

	// [BB] Invalid players certainly don't need any icon.
	if ( ( pPlayer == NULL ) || ( pPlayer->mo == NULL ) )
		return NUM_SPRITES;

	// [AK] Draw an ally or enemy icon if this person is, or isn't, our teammate.
	// [BB] In free spectate mode, we don't have allies/enemies (and HUD_GetViewPlayer doesn't return a useful value).
	if ( CLIENTDEMO_IsInFreeSpectateMode( ) == false )
	{
		player_t *viewedPlayer = &players[HUD_GetViewPlayer( )];

		// [BB] Dead spectators shall see the icons for their teammates or enemies.
		// [AK] Don't draw the icon for the player being spied on.
		if (( PLAYER_IsTrueSpectator( viewedPlayer ) == false ) && ( viewedPlayer != pPlayer ))
		{
			if ( pPlayer->mo->IsTeammate( viewedPlayer->mo ))
			{
				if ( medal_CanShowAllyOrEnemyIcon( false ))
					ulDesiredSprite = SPRITE_ALLY;
			}
			else if ( medal_CanShowAllyOrEnemyIcon( true ))
			{
				ulDesiredSprite = SPRITE_ENEMY;
			}
		}
	}

	// Draw a chat icon over the player if they're typing.
	if ( pPlayer->statuses & PLAYERSTATUS_CHATTING )
		ulDesiredSprite = SPRITE_CHAT;

	// Draw a console icon over the player if they're in the console.
	if ( pPlayer->statuses & PLAYERSTATUS_INCONSOLE )
		ulDesiredSprite = SPRITE_INCONSOLE;

	// Draw a menu icon over the player if they're in the Menu.
	if ( pPlayer->statuses & PLAYERSTATUS_INMENU )
		ulDesiredSprite = SPRITE_INMENU;

	// Draw a speaker icon over the player if they're talking.
	if ( pPlayer->statuses & PLAYERSTATUS_TALKING )
		ulDesiredSprite = SPRITE_VOICECHAT;

	// Draw a lag icon over their head if they're lagging.
	if ( pPlayer->statuses & PLAYERSTATUS_LAGGING )
		ulDesiredSprite = SPRITE_LAG;

	// Draw a flag/skull above this player if he's carrying one.
	if ( GAMEMODE_GetCurrentFlags() & GMF_USETEAMITEM )
	{
		if ( pPlayer->bOnTeam )
		{
			if ( oneflagctf )
			{
				AInventory *pInventory = pPlayer->mo->FindInventory( PClass::FindClass( "WhiteFlag" ), true );
				if ( pInventory )
					ulDesiredSprite = SPRITE_WHITEFLAG;
			}

			else
			{
				pTeamItem = TEAM_FindOpposingTeamsItemInPlayersInventory ( pPlayer );
				if ( pTeamItem )
					ulDesiredSprite = SPRITE_TEAMITEM;
			}
		}
	}

	// Draw the terminator artifact over the terminator.
	if ( terminator && ( pPlayer->cheats2 & CF2_TERMINATORARTIFACT ))
		ulDesiredSprite = SPRITE_TERMINATORARTIFACT;

	// Draw the possession artifact over the player.
	if (( possession || teampossession ) && ( pPlayer->cheats2 & CF2_POSSESSIONARTIFACT ))
		ulDesiredSprite = SPRITE_POSSESSIONARTIFACT;

	return ulDesiredSprite;
}

//*****************************************************************************
//
void medal_SelectIcon( player_t *player )
{
	// [BB] If player carries a TeamItem, e.g. flag or skull, we store a pointer
	// to it in teamItem and set the floaty icon to the carry (or spawn) state of
	// the TeamItem. We also need to copy the Translation of the TeamItem to the
	// FloatyIcon.
	AInventory	*teamItem = nullptr;

	if (( player == nullptr ) || ( player->mo == nullptr ))
		return;

	// Allow the user to disable icons.
	if (( cl_icons == false ) || ( NETWORK_GetState( ) == NETSTATE_SERVER ) || ( player->bSpectating ))
	{
		if ( player->pIcon )
		{
			player->pIcon->Destroy( );
			player->pIcon = nullptr;
		}

		return;
	}

	// Verify that our current icon is valid. (i.e. We may have had a chat bubble, then
	// stopped talking, so we need to delete it).
	if ( player->pIcon )
	{
		bool deleteIcon = false;

		if ( player->pIcon->bTeamItemFloatyIcon == false )
		{
			switch ( player->pIcon->currentSprite )
			{
				// Chat icon. Delete it if the player is no longer talking.
				case SPRITE_CHAT:
				{
					if (( player->statuses & PLAYERSTATUS_CHATTING ) == false )
						deleteIcon = true;

					break;
				}

				// Voice chat icon. Delete it if the player is no longer talking.
				case SPRITE_VOICECHAT:
				{
					if (( player->statuses & PLAYERSTATUS_TALKING ) == false )
						deleteIcon = true;

					break;
				}

				// In console icon. Delete it if the player is no longer in the console.
				case SPRITE_INCONSOLE:
				{
					if (( player->statuses & PLAYERSTATUS_INCONSOLE ) == false )
						deleteIcon = true;

					break;
				}

				// In menu icon . Delete it if the player is no longer in the menu.
				case SPRITE_INMENU:
				{
					if (( player->statuses & PLAYERSTATUS_INMENU ) == false )
						deleteIcon = true;

					break;
				}

				// Ally icon. Delete it if the player is now our enemy or if we're spectating.
				// [BB] Dead spectators shall keep the icon for their teammates.
				// [AK] Also delete it if the player is who we're spying on.
				case SPRITE_ALLY:
				case SPRITE_ENEMY:
				{
					player_t *viewedPlayer = &players[HUD_GetViewPlayer( )];

					if (( PLAYER_IsTrueSpectator( viewedPlayer )) || ( viewedPlayer == player ))
					{
						deleteIcon = true;
					}
					else if ( player->pIcon->currentSprite == SPRITE_ALLY )
					{
						if (( !player->mo->IsTeammate( viewedPlayer->mo )) || ( !medal_CanShowAllyOrEnemyIcon( false )))
							deleteIcon = true;
					}
					else if (( player->mo->IsTeammate( viewedPlayer->mo )) || ( !medal_CanShowAllyOrEnemyIcon( true )))
					{
						deleteIcon = true;
					}

					break;
				}

				// Lag icon. Delete it if the player is no longer lagging.
				case SPRITE_LAG:
				{
					if (( NETWORK_InClientMode( ) == false ) || (( player->statuses & PLAYERSTATUS_LAGGING ) == false ))
						deleteIcon = true;

					break;
				}


				// White flag icon. Delete it if the player no longer has it.
				case SPRITE_WHITEFLAG:
				{
					// Delete the icon if teamgame has been turned off, or if the player
					// is not on a team.
					if (( teamgame == false ) || ( player->bOnTeam == false ))
						deleteIcon = true;
					// Delete the white flag if the player no longer has it.
					else if (( oneflagctf ) && ( player->mo->FindInventory( PClass::FindClass( "WhiteFlag" ), true ) == nullptr ))
						deleteIcon = true;

					break;
				}

				// Terminator artifact icon. Delete it if the player no longer has it.
				case SPRITE_TERMINATORARTIFACT:
				{
					if (( terminator == false ) || (( player->cheats2 & CF2_TERMINATORARTIFACT ) == false ))
						deleteIcon = true;

					break;
				}

				// Possession artifact icon. Delete it if the player no longer has it.
				case SPRITE_POSSESSIONARTIFACT:
				{
					if ((( possession == false ) && ( teampossession == false )) || (( player->cheats2 & CF2_POSSESSIONARTIFACT ) == false ))
						deleteIcon = true;

					break;
				}

				default:
					break;
			}
		}
		else
		{
			// [AK] Team item icon. Delete it if the player no longer has one.
			if ((( GAMEMODE_GetCurrentFlags( ) & GMF_USETEAMITEM ) == false ) || ( player->bOnTeam == false ) ||
				( TEAM_FindOpposingTeamsItemInPlayersInventory( player ) == nullptr ))
			{
				deleteIcon = true;
			}
		}

		// We wish to delete the icon, so do that now.
		if ( deleteIcon )
		{
			player->pIcon->Destroy( );
			player->pIcon = nullptr;
		}
	}

	// Check if we need to have an icon above us, or change the current icon.
	const ULONG desiredSprite = medal_GetDesiredIcon( player, teamItem );
	const FActorInfo *floatyIconInfo = RUNTIME_CLASS( AFloatyIcon )->GetReplacement( )->ActorInfo;
	FState *desiredState = nullptr;

	// [BB] Determine the frame based on the desired sprite.
	switch ( desiredSprite )
	{
		case SPRITE_CHAT:
			desiredState = floatyIconInfo->FindState( "Chat" );
			break;

		case SPRITE_VOICECHAT:
			desiredState = floatyIconInfo->FindState( "VoiceChat" );
			break;

		case SPRITE_INCONSOLE:
			desiredState = floatyIconInfo->FindState( "InConsole" );
			break;

		case SPRITE_INMENU:
			desiredState = floatyIconInfo->FindState( "InMenu" );
			break;

		case SPRITE_ALLY:
			desiredState = floatyIconInfo->FindState( "Ally" );
			break;

		case SPRITE_ENEMY:
			desiredState = floatyIconInfo->FindState( "Enemy" );
			break;

		case SPRITE_LAG:
			desiredState = floatyIconInfo->FindState( "Lag" );
			break;

		case SPRITE_WHITEFLAG:
			desiredState = floatyIconInfo->FindState( "WhiteFlag" );
			break;

		case SPRITE_TERMINATORARTIFACT:
			desiredState = floatyIconInfo->FindState( "TerminatorArtifact" );
			break;

		case SPRITE_POSSESSIONARTIFACT:
			desiredState = floatyIconInfo->FindState( "PossessionArtifact" );
			break;

		case SPRITE_TEAMITEM:
		{
			if ( teamItem )
			{
				FState *carryState = teamItem->FindState( "Carry" );

				// [BB] If the TeamItem has a Carry state (like the built in flags), use it.
				// Otherwise use the spawn state (the built in skulls don't have a carry state).
				desiredState = carryState ? carryState : teamItem->SpawnState;
			}

			break;
		}

		default:
			break;
	}

	// We have an icon that needs to be spawned.
	if ((( desiredState != nullptr ) && ( desiredSprite != NUM_SPRITES )))
	{
		// [BB] If a TeamItem icon replaces an existing non-team icon, we have to delete the old icon first.
		if (( player->pIcon ) && ( player->pIcon->bTeamItemFloatyIcon == false ) && ( teamItem ))
		{
			player->pIcon->Destroy( );
			player->pIcon = nullptr;
		}

		if (( player->pIcon == nullptr ) || ( desiredSprite != player->pIcon->currentSprite ))
		{
			// [AK] Don't spawn the icon while the player is still respawning.
			if (( player->pIcon == nullptr ) && ( player->playerstate != PST_REBORN ))
			{
				player->pIcon = Spawn<AFloatyIcon>( player->mo->x, player->mo->y, player->mo->z + player->mo->height + ( 4 * FRACUNIT ), ALLOW_REPLACE );

				if ( teamItem )
				{
					player->pIcon->bTeamItemFloatyIcon = true;
					player->pIcon->Translation = teamItem->Translation;
				}
				else
				{
					player->pIcon->bTeamItemFloatyIcon = false;
				}
			}

			if ( player->pIcon )
			{
				// [BB] Potentially the new icon overrides an existing medal, so make sure that it doesn't fade out.
				player->pIcon->lTick = 0;
				player->pIcon->currentSprite = desiredSprite;
				player->pIcon->SetTracer( player->mo );
				player->pIcon->SetState( desiredState );
			}
		}
	}
}

//*****************************************************************************
//
void medal_CheckForFirstFrag( ULONG ulPlayer )
{
	// Only award it once.
	if ( g_bFirstFragAwarded )
		return;

	if (( deathmatch ) &&
		( lastmanstanding == false ) &&
		( teamlms == false ) &&
		( possession == false ) &&
		( teampossession == false ) &&
		(( duel == false ) || ( DUEL_GetState( ) == DS_INDUEL )))
	{
		MEDAL_GiveMedal( ulPlayer, "FirstFrag" );

		// It's been given.
		g_bFirstFragAwarded = true;
	}
}

//*****************************************************************************
//
void medal_CheckForDomination( ULONG ulPlayer )
{
	// If the player has gotten 5 straight frags without dying, award a medal.
	// Award a "Total Domination" medal if they get 10+ straight frags without dying. Otherwise, award a "Domination" medal.
	if (( players[ulPlayer].ulFragsWithoutDeath % 5 ) == 0 )
		MEDAL_GiveMedal( ulPlayer, players[ulPlayer].ulFragsWithoutDeath >= 10 ? "TotalDomination" : "Domination" );
}

//*****************************************************************************
//
void medal_CheckForFisting( ULONG ulPlayer )
{
	if ( players[ulPlayer].ReadyWeapon == NULL )
		return;

	// [BB/K6] Neither Fist nor BFG9000 will cause this MeansOfDeath.
	if ( MeansOfDeath == NAME_Telefrag )
		return;

	// If the player killed him with this fist, award him a "Fisting!" medal.
	if ( players[ulPlayer].ReadyWeapon->GetClass( ) == PClass::FindClass( "Fist" ))
		MEDAL_GiveMedal( ulPlayer, "Fisting" );

	// If this is the second frag this player has gotten THIS TICK with the
	// BFG9000, award him a "SPAM!" medal.
	if ( players[ulPlayer].ReadyWeapon->GetClass( ) == PClass::FindClass( "BFG9000" ))
	{
		if ( players[ulPlayer].ulLastBFGFragTick == static_cast<unsigned> (level.time) )
		{
			// Award the medal.
			MEDAL_GiveMedal( ulPlayer, "Spam" );

			// Also, cancel out the possibility of getting an Excellent/Incredible medal.
			players[ulPlayer].ulLastExcellentTick = 0;
			players[ulPlayer].ulLastFragTick = 0;
		}
		else
			players[ulPlayer].ulLastBFGFragTick = level.time;
	}
}

//*****************************************************************************
//
void medal_CheckForExcellent( ULONG ulPlayer )
{
	// If the player has gotten two Excelents within two seconds, award an "Incredible" medal.
	// [BB] Check that the player actually got an excellent medal.
	if ( ( ( players[ulPlayer].ulLastExcellentTick + ( 2 * TICRATE )) > (ULONG)level.time ) && players[ulPlayer].ulLastExcellentTick )
	{
		// Award the incredible.
		MEDAL_GiveMedal( ulPlayer, "Incredible" );

		players[ulPlayer].ulLastExcellentTick = level.time;
		players[ulPlayer].ulLastFragTick = level.time;
	}
	// If this player has gotten two frags within two seconds, award an "Excellent" medal.
	// [BB] Check that the player actually got a frag.
	else if ( ( ( players[ulPlayer].ulLastFragTick + ( 2 * TICRATE )) > (ULONG)level.time ) && players[ulPlayer].ulLastFragTick )
	{
		// Award the excellent.
		MEDAL_GiveMedal( ulPlayer, "Excellent" );

		players[ulPlayer].ulLastExcellentTick = level.time;
		players[ulPlayer].ulLastFragTick = level.time;
	}
}

//*****************************************************************************
//
void medal_CheckForTermination( ULONG ulDeadPlayer, ULONG ulPlayer )
{
	// If the target player is the terminatior, award a "termination" medal.
	if ( players[ulDeadPlayer].cheats2 & CF2_TERMINATORARTIFACT )
		MEDAL_GiveMedal( ulPlayer, "Termination" );
}

//*****************************************************************************
//
void medal_CheckForLlama( ULONG ulDeadPlayer, ULONG ulPlayer )
{
	// Award a "llama" medal if the victim had been typing, lagging, or in the console.
	if ( players[ulDeadPlayer].statuses & ( PLAYERSTATUS_CHATTING | PLAYERSTATUS_INCONSOLE | PLAYERSTATUS_INMENU | PLAYERSTATUS_LAGGING ))
		MEDAL_GiveMedal( ulPlayer, "Llama" );
}

//*****************************************************************************
//
void medal_CheckForYouFailIt( ULONG ulPlayer )
{
	// If the player dies TEN times without getting a frag, award a "Your skill is not enough" medal.
	if (( players[ulPlayer].ulDeathsWithoutFrag % 10 ) == 0 )
		MEDAL_GiveMedal( ulPlayer, "YourSkillIsNotEnough" );
	// If the player dies five times without getting a frag, award a "You fail it" medal.
	else if (( players[ulPlayer].ulDeathsWithoutFrag % 5 ) == 0 )
		MEDAL_GiveMedal( ulPlayer, "YouFailIt" );
}

#ifdef	_DEBUG
#include "c_dispatch.h"
CCMD( testgivemedal )
{
	for ( unsigned int i = 0; i < medalList.Size( ); i++ )
		MEDAL_GiveMedal( consoleplayer, i, false );
}
#endif	// _DEBUG
