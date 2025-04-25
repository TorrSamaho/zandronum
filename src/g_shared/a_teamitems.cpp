//-----------------------------------------------------------------------------
//
// Skulltag Source
// Copyright (C) 2002-2006 Brad Carney
// Copyright (C) 2025 Adam Kaminski
// Copyright (C) 2007-2025 Skulltag/Zandronum Development Team
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
// Date created:  7/5/06
//
//
// Filename: a_flags.cpp
//
// Description: Contains definitions for the flags, as well as skulltag's skulls.
//
//-----------------------------------------------------------------------------

#include "a_sharedglobal.h"
#include "announcer.h"
#include "doomstat.h"
#include "cl_demo.h"
#include "cl_main.h"
#include "g_level.h"
#include "network.h"
#include "p_acs.h"
#include "sbar.h"
#include "st_hud.h"
#include "sv_commands.h"
#include "team.h"
#include "v_text.h"
#include "v_video.h"
#include "gamemode.h"
#include "c_console.h"

//*****************************************************************************
//	DEFINES

enum
{
	DENY_PICKUP,
	ALLOW_PICKUP,
	RETURN_FLAG,
};

// Base team item -----------------------------------------------------------

IMPLEMENT_CLASS( ATeamItem )

//===========================================================================
//
// ATeamItem :: ShouldRespawn
//
// A team item should never respawn, so this function should always return false.
//
//===========================================================================

bool ATeamItem::ShouldRespawn( )
{
	return ( false );
}

//===========================================================================
//
// ATeamItem :: TryPickup
//
//===========================================================================

bool ATeamItem::TryPickup( AActor *&toucher )
{
	AInventory *inventory = toucher->Inventory;

	// If we're not in teamgame mode, just use the default pickup handling.
	if (( GAMEMODE_GetCurrentFlags( ) & GMF_USETEAMITEM ) == false )
		return ( Super::TryPickup( toucher ));

	// First, check to see if any of the toucher's inventory items want to
	// handle the picking up of this flag (other flags, perhaps?).

	// If HandlePickup() returns true, it will set the IF_PICKUPGOOD flag
	// to indicate that this item has been picked up. If the item cannot be
	// picked up, then it leaves the flag cleared.
	ItemFlags &= ~IF_PICKUPGOOD;
	if (( inventory != nullptr ) && ( inventory->HandlePickup( this )))
	{
		// Let something else the player is holding intercept the pickup.
		if (( ItemFlags & IF_PICKUPGOOD ) == false )
			return ( false );

		ItemFlags &= ~IF_PICKUPGOOD;
		GoAwayAndDie( );

		// Nothing more to do in this case.
		return ( true );
	}

	// Only players that are on a team may pickup items.
	if (( toucher->player == nullptr ) || ( toucher->player->bOnTeam == false ))
		return ( false );

	// [AK] Check if we're allowed to pickup this item.
	const int allowPickup = AllowPickup( toucher );

	// If we're not allowed to pickup this item, return false.
	if ( allowPickup != ALLOW_PICKUP )
	{
		if ( allowPickup == RETURN_FLAG )
		{
			// Execute the return scripts.
			if ( NETWORK_InClientMode( ) == false )
			{
				if ( this->IsKindOf( PClass::FindClass( "WhiteFlag" )))
					FBehavior::StaticStartTypedScripts( SCRIPT_WhiteReturn, nullptr, true );
				else
					FBehavior::StaticStartTypedScripts( TEAM_GetReturnScriptOffset( TEAM_GetTeamFromItem( this )), nullptr, true );
			}
			// In non-simple CTF mode, scripts take care of the returning and displaying messages.
			if ( TEAM_GetSimpleCTFSTMode( ))
			{
				if ( NETWORK_InClientMode( ) == false )
				{
					// The player is touching his own dropped item; return it now.
					Return( toucher );

					// Mark the item as no longer being taken.
					MarkTaken( false );
				}

				// Display text saying that the item has been returned.
				DisplayReturn( toucher );
			}

			// Reset the return ticks for this item.
			ResetReturnTicks( );

			// Announce that the item has been returned.
			AnnounceReturn( );

			// Delete the item.
			GoAwayAndDie( );

			// If we're the server, tell clients to destroy the item.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			{
				SERVERCOMMANDS_DestroyThing( this );

				// Tell clients that the item has been returned.
				// [AK] Also tell them who returned the item.
				SERVERCOMMANDS_TeamItemReturned( toucher->player ? toucher->player - players : MAXPLAYERS, TEAM_GetTeamFromItem( this ));
			}
			else
			{
				HUD_ShouldRefreshBeforeRendering( );
			}
		}

		return ( false );
	}

	// [AK] If we reached this point, the player is picking up the item. Execute the pickup scripts.
	if ( NETWORK_InClientMode( ) == false )
		FBehavior::StaticStartTypedScripts( SCRIPT_Pickup, toucher, true );

	// If we're in simple CTF mode, we need to display the pickup messages.
	if ( TEAM_GetSimpleCTFSTMode( ))
	{
		if ( NETWORK_InClientMode( ) == false )
		{
			// [CK] Signal that the flag/skull/some pickableable team item was taken
			GAMEMODE_HandleEvent( GAMEEVENT_TOUCHES, toucher, static_cast<int>( TEAM_GetTeamFromItem( this )));

			// Also, mark the item as being taken.
			MarkTaken( true );
		}

		// Display the item taken message.
		DisplayTaken( toucher );
	}

	// Reset the return ticks for this item.
	ResetReturnTicks( );

	// Announce the pickup of this item.
	AnnouncePickup( toucher );

	// Also, refresh the HUD.
	HUD_ShouldRefreshBeforeRendering( );

	AInventory *copy = CreateCopy( toucher );

	if ( copy == nullptr )
		return ( false );

	copy->AttachToOwner( toucher );

	// When we pick up the item, take away any invisibility objects the player has.
	while ( inventory )
	{
		if (( inventory->IsKindOf( RUNTIME_CLASS( APowerInvisibility ))) || ( inventory->IsKindOf( RUNTIME_CLASS( APowerTranslucency ))))
		{
			// If we're the server, tell clients to destroy this inventory item.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVERCOMMANDS_TakeInventory( static_cast<unsigned>( toucher->player - players ), inventory->GetClass( ), 0 );

			inventory->Destroy( );
		}

		inventory = inventory->Inventory;
	}

	return ( true );
}

//===========================================================================
//
// ATeamItem :: HandlePickup
//
//===========================================================================

bool ATeamItem::HandlePickup( AInventory *item )
{
	// Don't allow the pickup of invisibility objects when carrying a flag.
	if (( item->IsKindOf( RUNTIME_CLASS( APowerInvisibility ))) || ( item->IsKindOf( RUNTIME_CLASS( APowerTranslucency ))))
	{
		ItemFlags &= ~IF_PICKUPGOOD;

		return ( true );
	}

	return ( Super::HandlePickup( item ));
}

//===========================================================================
//
// ATeamItem :: AllowPickup
//
// Determine whether or not we should be allowed to pickup this item.
//
//===========================================================================

int ATeamItem::AllowPickup( AActor *toucher )
{
	// [BB] Only players on a team can pick up team items.
	if (( toucher == nullptr ) || ( toucher->player == nullptr ) || ( toucher->player->bOnTeam == false ))
		return ( DENY_PICKUP );

	// [BB] Players are always allowed to return their own dropped team item.
	if (( this->GetClass( ) == TEAM_GetItem( toucher->player->Team )) && ( this->flags & MF_DROPPED ))
		return ( RETURN_FLAG );

	// [BB] If a client gets here, the server already made all necessary checks. So just allow the pickup.
	if ( NETWORK_InClientMode( ))
		return ( ALLOW_PICKUP );

	// [BB] If a player already carries an enemy team item, don't let him pick up another one.
	if ( TEAM_FindOpposingTeamsItemInPlayersInventory( toucher->player ))
		return ( DENY_PICKUP );

	// [BB] If the team the item belongs to doesn't have any players, don't let it be picked up.
	if ( TEAM_CountPlayers( TEAM_GetTeamFromItem( this )) == 0 )
	{
		FString message;
		message.Format( "You can't pick up the %s\nof a team with no players!", GetType( ));

		HUD_DrawSUBSMessage( message.GetChars( ), CR_UNTRANSLATED, 3.0f, 0.25f, true, static_cast<unsigned>( toucher->player - players ), SVCF_ONLYTHISCLIENT );
		return ( DENY_PICKUP );
	}

	// [CK] Do not let pickups occur after the match has ended
	if ( GAMEMODE_IsGameInProgress( ) == false )
		return ( DENY_PICKUP );

	// Player is touching the enemy flag.
	if ( this->GetClass( ) != TEAM_GetItem( toucher->player->Team ))
		return ( ALLOW_PICKUP );

	return ( DENY_PICKUP );
}

//===========================================================================
//
// ATeamItem :: AnnouncePickup
//
// Play the announcer sound for picking up this item.
//
//===========================================================================

void ATeamItem::AnnouncePickup( AActor *toucher )
{
	// Don't announce the pickup if the item is being given to someone as part of a snapshot.
	if (( NETWORK_GetState( ) == NETSTATE_CLIENT ) && ( CLIENT_GetConnectionState( ) == CTS_RECEIVINGSNAPSHOT ))
		return;

	// Build the message. Whatever the team's name is, is the first part of
	// the message, followed by the item's current type. This way we don't have
	// to change every announcer to use a new system.
	FString name = TEAM_GetName( TEAM_GetTeamFromItem( this ));
	name.AppendFormat( "%sTaken", GetType( ));

	ANNOUNCER_PlayEntry( cl_announcer, name.GetChars( ));
}

//===========================================================================
//
// ATeamItem :: DisplayTaken
//
// Display the text for picking up this item.
//
//===========================================================================

void ATeamItem::DisplayTaken( AActor *toucher )
{
	const int touchingPlayer = static_cast<int>( toucher->player - players );
	const unsigned int team = TEAM_GetTeamFromItem( this );
	EColorRange color = static_cast<EColorRange>( TEAM_GetTextColor( team ));
	FString message;

	message.Format( "%s has taken the ", players[touchingPlayer].userinfo.GetName( ));
	message += TEXTCOLOR_ESCAPE;
	message.AppendFormat( "%s%s " TEXTCOLOR_NORMAL "%s.", TEAM_GetTextColorName( team ), TEAM_GetName( team ), GetType( ));

	Printf( "%s\n", message.GetChars( ));

	// [AK] The server doesn't need to do anything past this point.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		return;

	if ( touchingPlayer == consoleplayer )
		message.Format( "You have the %s %s!", TEAM_GetName( team ), GetType( ));
	else
		message.Format( "%s %s taken!", TEAM_GetName( team ), GetType( ));

	HUD_DrawCNTRMessage( message.GetChars( ), color );

	// [RC] Create the "held by" message for the team.
	// [AK] Don't show this message to the player picking up the item.
	if ( touchingPlayer != consoleplayer )
	{
		color = static_cast<EColorRange>( TEAM_GetTextColor( players[touchingPlayer].Team ));
		message.Format( "Held by: %s", players[touchingPlayer].userinfo.GetName( ));

		// Now, print it.
		HUD_DrawSUBSMessage( message.GetChars( ), color );
	}
	else
	{
		StatusBar->DetachMessage( MAKE_ID( 'S', 'U', 'B', 'S' ));
	}
}

//===========================================================================
//
// ATeamItem :: Return
//
// Spawn a new team item at its original location.
//
//===========================================================================

void ATeamItem::Return( AActor *returner )
{
	const unsigned int returningPlayer = ( returner && returner->player ) ? static_cast<unsigned>( returner->player - players ) : MAXPLAYERS;
	const unsigned int team = TEAM_GetTeamFromItem( this );
	FString message;

	// Respawn the item.
	const POS_t origin = TEAM_GetItemOrigin( TEAM_GetTeamFromItem( this ));
	AActor *actor = Spawn( this->GetClass( ), origin.x, origin.y, origin.z, NO_REPLACE );

	if ( actor )
	{
		// If we're the server, tell clients to spawn the new item.
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			SERVERCOMMANDS_SpawnThing( actor );

		// Since all inventory spawns with the MF_DROPPED flag, we need to unset it.
		actor->flags &= ~MF_DROPPED;
	}

	// Mark the item as no longer being taken.
	TEAM_SetItemTaken( team, false );

	// If an opposing team's item has been taken by one of the team members of the returner
	// the player who returned this item has the chance to earn an "Assist!" medal.
	if ( returningPlayer != MAXPLAYERS )
	{
		for ( unsigned int i = 0; i < MAXPLAYERS; i++ )
		{
			if (( players[i].Team == team ) && ( TEAM_FindOpposingTeamsItemInPlayersInventory( &players[i] )))
				TEAM_SetAssistPlayer( returner->player->Team, returningPlayer );
		}

		// [CK] Send out an event that a flag/skull was returned, this is the easiest place to do it
		// Second argument is the team index, third argument is what kind of return it was
		GAMEMODE_HandleEvent( GAMEEVENT_RETURNS, returner, team, GAMEEVENT_RETURN_PLAYERRETURN );
	}
	else
	{
		// [CK] Indicate the server returned the flag/skull after a timeout
		GAMEMODE_HandleEvent( GAMEEVENT_RETURNS, nullptr, team, GAMEEVENT_RETURN_TIMEOUTRETURN );
	}
}

//===========================================================================
//
// ATeamItem :: AnnounceReturn
//
// Play the announcer sound for this item being returned.
//
//===========================================================================

void ATeamItem::AnnounceReturn( void )
{
	// Build the message. Whatever the team's name is, is the first part of
	// the message, followed by the item's current type. This way we don't have
	// to change every announcer to use a new system.
	FString name = TEAM_GetName( TEAM_GetTeamFromItem( this ));
	name.AppendFormat( "%sReturned", GetType( ));

	ANNOUNCER_PlayEntry( cl_announcer, name.GetChars( ));
}

//===========================================================================
//
// ATeamItem :: DisplayReturn
//
// Display the text for this item being returned.
//
//===========================================================================

void ATeamItem::DisplayReturn( AActor *returner )
{
	const unsigned int returningPlayer = ( returner && returner->player ) ? static_cast<unsigned>( returner->player - players ) : MAXPLAYERS;
	const unsigned int team = TEAM_GetTeamFromItem( this );
	const EColorRange color = static_cast<EColorRange>( TEAM_GetTextColor( team ));
	FString message;

	FString itemName = TEXTCOLOR_ESCAPE;
	itemName.AppendFormat( "%s%s " TEXTCOLOR_NORMAL "%s", TEAM_GetTextColorName( team ), TEAM_GetName( team ), GetType( ));

	if ( returningPlayer != MAXPLAYERS )
		message.Format( "%s returned the %s.", players[returningPlayer].userinfo.GetName( ), itemName.GetChars( ));
	else
		message.Format( "%s returned automatically.", itemName.GetChars( ));

	Printf( "%s\n", message.GetChars( ));

	// [AK] The server doesn't need to do anything past this point.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		return;

	// Create the "returned" message.
	message.Format( "%s %s returned", TEAM_GetName( team ), GetType( ));
	HUD_DrawCNTRMessage( message.GetChars( ), color );

	// [RC] Create the "returned by" message for this team.
	if ( returningPlayer != MAXPLAYERS )
		message.Format( "Returned by: %s", players[returningPlayer].userinfo.GetName( ));
	// [RC] Create the "returned automatically" message for this team.
	else
		message = "Returned automatically";

	HUD_DrawSUBSMessage( message.GetChars( ), color );
}

//===========================================================================
//
// [AK] ATeamItem :: Drop
//
// Display the text and play the announcer sound for this item being dropped.
//
//===========================================================================

void ATeamItem::Drop( player_t *player, unsigned int team )
{
	// [AK] We're assuming that the team's item is either a flag or skull.
	const FString itemName = skulltag ? "skull" : "flag";
	const EColorRange color = static_cast<EColorRange>( TEAM_GetTextColor( team ));
	const bool isWhiteFlag = ( team == teams.Size( ));
	FString message;

	// [AK] Make sure that the player is valid.
	if (( player == nullptr ) || ( player - players >= MAXPLAYERS ) || ( player - players < 0 ))
		return;

	// [AK] Also make sure that they're on a valid team.
	if (( player->bOnTeam == false ) || ( TEAM_CheckIfValid( player->Team ) == false ))
		return;

	// [AK] Print a message in the console that the player has dropped the item.
	message.Format( "%s lost the ", player->userinfo.GetName( ));

	if ( isWhiteFlag )
	{
		message.AppendFormat( TEXTCOLOR_GREY "White " TEXTCOLOR_NORMAL "flag." );
	}
	else
	{
		message += TEXTCOLOR_ESCAPE;
		message.AppendFormat( "%s%s " TEXTCOLOR_NORMAL "%s.", TEAM_GetTextColorName( team ), TEAM_GetName( team ), itemName.GetChars( ));
	}

	// [AK] Don't print the same message twice for the current RCON client.
	CONSOLE_ShouldPrintToRCONPlayer( false );
	Printf( "%s\n", message.GetChars( ));

	// If we're the server, just tell clients to do this.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
	{
		SERVERCOMMANDS_TeamItemDropped( static_cast<unsigned>( player - players ), team );
		return;
	}

	// [AK] Build the dropped HUD message and print it in the middle of the
	// screen, then play the announcer entry associated with this event.
	if ( isWhiteFlag )
	{
		HUD_DrawCNTRMessage( "White flag dropped!", color );
		ANNOUNCER_PlayEntry( cl_announcer, "WhiteFlagDropped" );
	}
	else
	{
		message.Format( "%s %s dropped!", TEAM_GetName( team ), itemName.GetChars( ));
		HUD_DrawCNTRMessage( message.GetChars( ), color );

		message.Format( "%s%sDropped", TEAM_GetName( team ), itemName.GetChars( ));
		ANNOUNCER_PlayEntry( cl_announcer, message.GetChars( ));
	}

	StatusBar->DetachMessage( MAKE_ID( 'S', 'U', 'B', 'S' ));
}

//===========================================================================
//
// ATeamItem :: MarkTaken
//
// Signal to the team module whether or not this item has been taken.
//
//===========================================================================

void ATeamItem::MarkTaken( bool taken )
{
	// [AK] For the white flag, TEAM_GetTeamFromItem should return teams.size( ).
	TEAM_SetItemTaken( TEAM_GetTeamFromItem( this ), taken );
}

//===========================================================================
//
// ATeamItem :: ResetReturnTicks
//
// Reset the return ticks for the team associated with this item.
//
//===========================================================================

void ATeamItem::ResetReturnTicks( void )
{
	// [AK] For the white flag, TEAM_GetTeamFromItem should return teams.size( ).
	TEAM_SetReturnTicks( TEAM_GetTeamFromItem( this ), 0 );
}

// Skulltag flag ------------------------------------------------------------

IMPLEMENT_CLASS( AFlag )

//===========================================================================
//
// AFlag :: HandlePickup
//
// Ask this item in the actor's inventory to potentially react to this object
// attempting to be picked up.
//
//===========================================================================

bool AFlag::HandlePickup( AInventory *item )
{
	const unsigned int player = static_cast<unsigned>( Owner->player - players );
	const unsigned int team = players[player].Team;

	// If this object being given isn't a flag, then we don't really care.
	if ( item->GetClass( )->IsDescendantOf( RUNTIME_CLASS( AFlag )) == false )
		return ( Super::HandlePickup( item ));

	// If we're carrying the opposing team's flag, and trying to pick up our flag,
	// then that means we've captured the flag. Award a point.
	if (( this->GetClass( ) != TEAM_GetItem( team )) && ( item->GetClass( ) == TEAM_GetItem( team )))
	{
		// [NS] Do not allow scoring when the round is over.
		if ( GAMEMODE_IsGameInProgress( ) == false )
			return ( Super::HandlePickup( item ));

		// Don't award a point if we're touching a dropped version of our flag.
		if ( static_cast<AFlag *>( item )->AllowPickup( Owner ) == RETURN_FLAG )
			return ( Super::HandlePickup( item ));

		if (( TEAM_GetSimpleCTFSTMode( )) && ( NETWORK_InClientMode( ) == false ))
		{
			const unsigned int assistPlayer = TEAM_GetAssistPlayer( team );

			TEAM_PrintScoresMessage( team, player, 1 );

			// Give his team a point.
			TEAM_SetPointCount( team, TEAM_GetPointCount( team ) + 1, true, false );
			PLAYER_SetPoints( Owner->player, Owner->player->lPointCount + 1 );

			// Award the scorer with a "Capture!" medal.
			MEDAL_GiveMedal( player, "Capture" );

			this->Return( nullptr );

			// If someone just recently returned the flag, award him with an "Assist!" medal.
			// [CK] Trigger an event script (activator is the capturer, assister is the second arg),
			// The second arg will be GAMEEVENT_CAPTURE_NOASSIST (-1) if there was no assister.
			// [AK] Also pass the number of points earned.
			if ( assistPlayer != MAXPLAYERS )
			{
				MEDAL_GiveMedal( assistPlayer, "Assist" );
				TEAM_SetAssistPlayer( team, MAXPLAYERS );

				GAMEMODE_HandleEvent( GAMEEVENT_CAPTURES, Owner, assistPlayer, 1 );
			}
			else
			{
				GAMEMODE_HandleEvent( GAMEEVENT_CAPTURES, Owner, GAMEEVENT_CAPTURE_NOASSIST, 1 );
			}

			// Take the flag away.
			AInventory *inventory = Owner->FindInventory( this->GetClass( ));

			if ( inventory )
			{
				if ( NETWORK_GetState( ) == NETSTATE_SERVER )
					SERVERCOMMANDS_TakeInventory( player, inventory->GetClass( ), 0 );

				inventory->Destroy( );
				inventory = nullptr;
			}

			// Also, refresh the HUD.
			HUD_ShouldRefreshBeforeRendering( );
		}

		return ( true );
	}

	return ( Super::HandlePickup( item ));
}

//===========================================================================
//
// AFlag :: AllowPickup
//
// Determine whether or not we should be allowed to pickup this flag.
//
//===========================================================================

int AFlag::AllowPickup( AActor *toucher )
{
	// Don't allow the pickup of flags in One Flag CTF.
	if ( oneflagctf )
		return ( DENY_PICKUP );

	return Super::AllowPickup( toucher );
}

// White flag ---------------------------------------------------------------

IMPLEMENT_CLASS( AWhiteFlag )

//===========================================================================
//
// AWhiteFlag :: HandlePickup
//
// Ask this item in the actor's inventory to potentially react to this object
// attempting to be picked up.
//
//===========================================================================

bool AWhiteFlag::HandlePickup( AInventory *item )
{
	const unsigned int player = static_cast<unsigned>( Owner->player - players );
	const unsigned int team = players[player].Team;

	// If this object being given isn't a flag, then we don't really care.
	if ( item->GetClass( )->IsDescendantOf( RUNTIME_CLASS( AFlag )) == false )
		return ( Super::HandlePickup( item ));

	// If this isn't one flag CTF mode, then we don't really care here.
	if ( oneflagctf == false )
		return ( Super::HandlePickup( item ));

	// [BB] Bringing a WhiteFlag to another WhiteFlag doesn't give a point.
	if ( item->IsKindOf ( PClass::FindClass( "WhiteFlag" )))
		return ( false );

	if ( TEAM_GetTeamFromItem( item ) == team )
		return ( false );

	// If we're trying to pick up the opponent's flag, award a point since we're
	// carrying the white flag.
	if (( TEAM_GetSimpleCTFSTMode( )) && ( NETWORK_InClientMode( ) == false ))
	{
		TEAM_PrintScoresMessage( team, player, 1 );

		// Give his team a point.
		TEAM_SetPointCount( team, TEAM_GetPointCount( team ) + 1, true, false );
		PLAYER_SetPoints( Owner->player, Owner->player->lPointCount + 1 );

		// Award the scorer with a "Capture!" medal.
		MEDAL_GiveMedal( player, "Capture" );

		// [AK] Trigger an event script when the white flag is captured.
		GAMEMODE_HandleEvent( GAMEEVENT_CAPTURES, Owner, GAMEEVENT_CAPTURE_NOASSIST, 1 );

		// Take the flag away.
		AInventory *inventory = Owner->FindInventory( this->GetClass( ));

		if ( inventory )
		{
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVERCOMMANDS_TakeInventory( player, inventory->GetClass( ), 0 );

			inventory->Destroy( );
			inventory = nullptr;
		}

		this->Return( nullptr );

		// Also, refresh the HUD.
		HUD_ShouldRefreshBeforeRendering( );

		return ( true );
	}

	return ( Super::HandlePickup( item ));
}

//===========================================================================
//
// AWhiteFlag :: AllowPickup
//
// Determine whether or not we should be allowed to pickup the white flag.
//
//===========================================================================

int AWhiteFlag::AllowPickup( AActor *toucher )
{
	// [BB] Carrying more than one WhiteFlag is not allowed.
	if (( toucher == nullptr ) || ( toucher->FindInventory( PClass::FindClass( "WhiteFlag" ), true ) == nullptr ))
	{
		// [AK] Don't allow the white flag to be picked up without an opposing team.
		if (( toucher != nullptr ) && ( toucher->player != nullptr ) && ( TEAM_TeamsWithPlayersOn( ) < 2 ))
		{
			const unsigned int player = static_cast<unsigned>( toucher->player - players );
			HUD_DrawSUBSMessage( "You can't pick up the white flag\nwithout an opposing team!", CR_UNTRANSLATED, 3.0f, 0.25f, true, player, SVCF_ONLYTHISCLIENT );

			return ( DENY_PICKUP );
		}

		// [AK] Also don't allow it to be picked up after the game's ended.
		if ( GAMEMODE_IsGameInProgress( ) == false )
			return ( DENY_PICKUP );

		return ( ALLOW_PICKUP );
	}
	else
	{
		return ( DENY_PICKUP );
	}
}

//===========================================================================
//
// AWhiteFlag :: AnnouncePickup
//
// Play the announcer sound for picking up the white flag.
//
//===========================================================================

void AWhiteFlag::AnnouncePickup( AActor *toucher )
{
	// Don't announce the pickup if the flag is being given to someone as part of a snapshot.
	if (( NETWORK_GetState( ) == NETSTATE_CLIENT ) && ( CLIENT_GetConnectionState( ) == CTS_RECEIVINGSNAPSHOT ))
		return;

	if (( toucher == nullptr ) || ( toucher->player == nullptr ))
		return;

	if (( playeringame[consoleplayer] ) && ( players[consoleplayer].bOnTeam ) && ( players[consoleplayer].mo ))
	{
		if (( toucher->player - players ) == consoleplayer )
			ANNOUNCER_PlayEntry( cl_announcer, "YouHaveTheFlag" );
		else if ( players[consoleplayer].mo->IsTeammate( toucher ))
			ANNOUNCER_PlayEntry( cl_announcer, "YourTeamHasTheFlag" );
		else
			ANNOUNCER_PlayEntry( cl_announcer, "TheEnemyHasTheFlag" );
	}
}

//===========================================================================
//
// AWhiteFlag :: DisplayTaken
//
// Display the text for picking up the white flag.
//
//===========================================================================

void AWhiteFlag::DisplayTaken( AActor *toucher )
{
	const int touchingPlayer = static_cast<int>( toucher->player - players );

	Printf( "%s has taken the " TEXTCOLOR_GREY "White " TEXTCOLOR_NORMAL "flag.\n", players[touchingPlayer].userinfo.GetName( ));

	// [AK] The server doesn't need to do anything here.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		return;

	// Create the "pickup" message and print it.
	HUD_DrawCNTRMessage( touchingPlayer == consoleplayer ? "You have the flag!" : "White flag taken!", CR_GREY );

	// [BC] Rivecoder's "held by" messages.
	// [AK] Don't show this message to the player picking up the item.
	if ( touchingPlayer != consoleplayer )
	{
		const EColorRange color = static_cast<EColorRange>( TEAM_GetTextColor( players[touchingPlayer].Team ));
		FString message;

		// [AK] Colorize the message in the same way that it is for flags or skulls.
		message.Format( "Held by: %s", players[touchingPlayer].userinfo.GetName( ));

		// Now, print it.
		HUD_DrawSUBSMessage( message.GetChars( ), color );
	}
	else
	{
		StatusBar->DetachMessage( MAKE_ID( 'S', 'U', 'B', 'S' ));
	}
}

//===========================================================================
//
// AWhiteFlag :: Return
//
// Spawn a new white flag at its original location.
//
//===========================================================================

void AWhiteFlag::Return( AActor *returner )
{
	// Respawn the white flag.
	const POS_t origin = TEAM_GetItemOrigin( teams.Size( ));
	AActor *actor = Spawn( this->GetClass( ), origin.x, origin.y, origin.z, NO_REPLACE );

	if ( actor )
	{
		// If we're the server, tell clients to spawn the new white flag.
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			SERVERCOMMANDS_SpawnThing( actor );

		// Since all inventory spawns with the MF_DROPPED flag, we need to unset it.
		actor->flags &= ~MF_DROPPED;
	}

	// Mark the white flag as no longer being taken.
	TEAM_SetItemTaken( teams.Size( ), false );

	// [AK] Trigger an event script. Since the white flag doesn't belong to any team, don't pass any team's ID.
	GAMEMODE_HandleEvent( GAMEEVENT_RETURNS, nullptr, teams.Size( ));
}

//===========================================================================
//
// AWhiteFlag :: AnnounceReturn
//
// Play the announcer sound for the white flag being returned.
//
//===========================================================================

void AWhiteFlag::AnnounceReturn( void )
{
	ANNOUNCER_PlayEntry( cl_announcer, "WhiteFlagReturned" );
}

//===========================================================================
//
// AWhiteFlag :: DisplayReturn
//
// Display the text for the white flag being returned.
//
//===========================================================================

void AWhiteFlag::DisplayReturn( AActor *returner )
{
	// Create the "returned" message.
	HUD_DrawCNTRMessage( "White flag returned", CR_GREY );
	Printf( TEXTCOLOR_GREY "White " TEXTCOLOR_NORMAL "flag returned automatically.\n" );

	// [AK] Create the "returned automatically" message.
	HUD_DrawSUBSMessage( "Returned automatically", CR_GREY );
}

// Skulltag skull -----------------------------------------------------------

IMPLEMENT_CLASS( ASkull )

//===========================================================================
//
// ASkull :: AllowPickup
//
// Determine whether or not we should be allowed to pickup this skull.
//
//===========================================================================

int ASkull::AllowPickup( AActor *toucher )
{
	return Super::AllowPickup( toucher );
}
