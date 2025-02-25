//-----------------------------------------------------------------------------
//
// Skulltag Source
// Copyright (C) 2002-2006 Brad Carney
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
// A flag should never respawn, so this function should always return false.
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

bool ATeamItem::TryPickup( AActor *&pToucher )
{
	AInventory	*pCopy;
	AInventory	*pInventory;

	// If we're not in teamgame mode, just use the default pickup handling.
	if ( !( GAMEMODE_GetCurrentFlags() & GMF_USETEAMITEM ) )
		return ( Super::TryPickup( pToucher ));

	// First, check to see if any of the toucher's inventory items want to
	// handle the picking up of this flag (other flags, perhaps?).

	// If HandlePickup() returns true, it will set the IF_PICKUPGOOD flag
	// to indicate that this item has been picked up. If the item cannot be
	// picked up, then it leaves the flag cleared.
	ItemFlags &= ~IF_PICKUPGOOD;
	if (( pToucher->Inventory != NULL ) && ( pToucher->Inventory->HandlePickup( this )))
	{
		// Let something else the player is holding intercept the pickup.
		if (( ItemFlags & IF_PICKUPGOOD ) == false )
			return ( false );

		ItemFlags &= ~IF_PICKUPGOOD;
		GoAwayAndDie( );

		// Nothing more to do in this case.
		return ( true );
	}

	// Only players that are on a team may pickup flags.
	if (( pToucher->player == NULL ) || ( pToucher->player->bOnTeam == false ))
		return ( false );

	switch ( AllowFlagPickup( pToucher ))
	{
	case DENY_PICKUP:

		// If we're not allowed to pickup this flag, return false.
		return ( false );
	case RETURN_FLAG:

		// Execute the return scripts.
		if ( NETWORK_InClientMode() == false )
		{
			if ( this->IsKindOf( PClass::FindClass( "WhiteFlag" ) ))
			{
				FBehavior::StaticStartTypedScripts( SCRIPT_WhiteReturn, NULL, true );
			}
			else
			{
				FBehavior::StaticStartTypedScripts( TEAM_GetReturnScriptOffset( TEAM_GetTeamFromItem( this )), NULL, true );
			}
		}

		// In non-simple CTF mode, scripts take care of the returning and displaying messages.
		if ( TEAM_GetSimpleCTFSTMode( ))
		{
			if ( NETWORK_InClientMode() == false )
			{
				// The player is touching his own dropped flag; return it now.
				ReturnFlag( pToucher );

				// Mark the flag as no longer being taken.
				MarkFlagTaken( false );
			}

			// Display text saying that the flag has been returned.
			DisplayFlagReturn( );
		}

		// Reset the return ticks for this flag.
		ResetReturnTicks( );

		// Announce that the flag has been returned.
		AnnounceFlagReturn( );

		// Delete the flag.
		GoAwayAndDie( );

		// If we're the server, tell clients to destroy the flag.
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			SERVERCOMMANDS_DestroyThing( this );

		// Tell clients that the flag has been returned.
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		{
			SERVERCOMMANDS_TeamFlagReturned( TEAM_GetTeamFromItem( this ) );
		}
		else
			HUD_ShouldRefreshBeforeRendering( );

		return ( false );
	}

	// Announce the pickup of this flag.
	AnnounceFlagPickup( pToucher );

	// Player is picking up the flag.
	if ( NETWORK_InClientMode() == false )
	{
		FBehavior::StaticStartTypedScripts( SCRIPT_Pickup, pToucher, true );

		// If we're in simple CTF mode, we need to display the pickup messages.
		if ( TEAM_GetSimpleCTFSTMode( ))
		{
			// [CK] Signal that the flag/skull/some pickableable team item was taken
			GAMEMODE_HandleEvent ( GAMEEVENT_TOUCHES, pToucher, static_cast<int> ( TEAM_GetTeamFromItem( this ) ) );

			// Display the flag taken message.
			DisplayFlagTaken( pToucher );

			// Also, mark the flag as being taken.
			MarkFlagTaken( true );
		}

		// Reset the return ticks for this flag.
		ResetReturnTicks( );

		// Also, refresh the HUD.
		HUD_ShouldRefreshBeforeRendering( );
	}

	pCopy = CreateCopy( pToucher );
	if ( pCopy == NULL )
		return ( false );

	pCopy->AttachToOwner( pToucher );

	// When we pick up a flag, take away any invisibility objects the player has.
	pInventory = pToucher->Inventory;
	while ( pInventory )
	{
		if (( pInventory->IsKindOf( RUNTIME_CLASS( APowerInvisibility ))) ||
			( pInventory->IsKindOf( RUNTIME_CLASS( APowerTranslucency ))))
		{
			// If we're the server, tell clients to destroy this inventory item.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVERCOMMANDS_TakeInventory( ULONG( pToucher->player - players ), pInventory->GetClass(), 0 );

			pInventory->Destroy( );
		}

		pInventory = pInventory->Inventory;
	}

	return ( true );
}

//===========================================================================
//
// ATeamItem :: HandlePickup
//
//===========================================================================

bool ATeamItem::HandlePickup( AInventory *pItem )
{
	// Don't allow the pickup of invisibility objects when carrying a flag.
	if (( pItem->IsKindOf( RUNTIME_CLASS( APowerInvisibility ))) ||
		( pItem->IsKindOf( RUNTIME_CLASS( APowerTranslucency ))))
	{
		ItemFlags &= ~IF_PICKUPGOOD;

		return ( true );
	}

	return ( Super::HandlePickup( pItem ));
}

//===========================================================================
//
// ATeamItem :: AllowFlagPickup
//
// Determine whether or not we should be allowed to pickup this flag.
//
//===========================================================================

int ATeamItem::AllowFlagPickup( AActor *toucher )
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
// ATeamItem :: AnnounceFlagPickup
//
// Play the announcer sound for picking up this flag.
//
//===========================================================================

void ATeamItem::AnnounceFlagPickup( AActor *toucher )
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
// ATeamItem :: DisplayFlagTaken
//
// Display the text for picking up this flag.
//
//===========================================================================

void ATeamItem::DisplayFlagTaken( AActor *toucher )
{
	const int touchingPlayer = static_cast<int>( toucher->player - players );
	const unsigned int team = TEAM_GetTeamFromItem( this );
	EColorRange color = static_cast<EColorRange>( TEAM_GetTextColor( team ));
	FString message;

	// Create the "pickup" message and print it... or if necessary, send it to clients.
	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
	{
		if ( touchingPlayer == consoleplayer )
			message.Format( "You have the %s %s!", TEAM_GetName( team ), GetType( ));
		else
			message.Format( "%s %s taken!", TEAM_GetName( team ), GetType( ));

		HUD_DrawCNTRMessage( message.GetChars( ), color );
	}
	else
	{
		message.Format( "You have the %s %s!", TEAM_GetName( team ), GetType( ));
		HUD_DrawCNTRMessage( message.GetChars( ), color, 3.0f, 0.25f, true, touchingPlayer, SVCF_ONLYTHISCLIENT );

		message.Format( "%s %s taken!", TEAM_GetName( team ), GetType( ));
		HUD_DrawCNTRMessage( message.GetChars( ), color, 3.0f, 0.25f, true, touchingPlayer, SVCF_SKIPTHISCLIENT );
	}

	// [RC] Create the "held by" message for the team.
	// [AK] Don't show this message to the player picking up the item.
	if (( NETWORK_GetState( ) == NETSTATE_SERVER ) || ( touchingPlayer != consoleplayer ))
	{
		color = static_cast<EColorRange>( TEAM_GetTextColor( players[touchingPlayer].Team ));
		message.Format( "Held by: %s", players[touchingPlayer].userinfo.GetName( ));

		// Now, print it... or if necessary, send it to clients.
		HUD_DrawSUBSMessage( message.GetChars( ), color, 3.0f, 0.25f, true, touchingPlayer, SVCF_SKIPTHISCLIENT );
	}

	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
	{
		message.Format( "%s has taken the ", players[touchingPlayer].userinfo.GetName( ));
		message += TEXTCOLOR_ESCAPE;
		message.AppendFormat( "%s%s " TEXTCOLOR_NORMAL "%s.", TEAM_GetTextColorName( team ), TEAM_GetName( team ), GetType( ));

		SERVER_Printf( PRINT_MEDIUM, "%s\n", message.GetChars( ));
	}
}

//===========================================================================
//
// ATeamItem :: ReturnFlag
//
// Spawn a new flag at its original location.
//
//===========================================================================

void ATeamItem::ReturnFlag( AActor *returner )
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

		// [RC] Create the "returned by" message for this team.
		message.Format( "Returned by: %s", players[returningPlayer].userinfo.GetName( ));

		// [CK] Send out an event that a flag/skull was returned, this is the easiest place to do it
		// Second argument is the team index, third argument is what kind of return it was
		GAMEMODE_HandleEvent( GAMEEVENT_RETURNS, returner, team, GAMEEVENT_RETURN_PLAYERRETURN );
	}
	else
	{
		// [RC] Create the "returned automatically" message for this team.
		message = "Returned automatically.";

		// [CK] Indicate the server returned the flag/skull after a timeout
		GAMEMODE_HandleEvent( GAMEEVENT_RETURNS, nullptr, team, GAMEEVENT_RETURN_TIMEOUTRETURN );
	}

	HUD_DrawSUBSMessage( message.GetChars( ), static_cast<EColorRange>( TEAM_GetTextColor( team )), 3.0f, 0.25f, true );

	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
	{
		FString itemName = TEXTCOLOR_ESCAPE;
		itemName.AppendFormat( "%s%s " TEXTCOLOR_NORMAL "%s", TEAM_GetTextColorName( team ), TEAM_GetName( team ), GetType( ));

		if ( returningPlayer != MAXPLAYERS )
			message.Format( "%s returned the %s.", players[returningPlayer].userinfo.GetName( ), itemName.GetChars( ));
		else
			message.Format( "%s returned.", itemName.GetChars( ));

		SERVER_Printf( PRINT_MEDIUM, "%s\n", message.GetChars( ));
	}
}

//===========================================================================
//
// ATeamItem :: AnnounceFlagReturn
//
// Play the announcer sound for this flag being returned.
//
//===========================================================================

void ATeamItem::AnnounceFlagReturn( void )
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
// ATeamItem :: DisplayFlagReturn
//
// Display the text for this flag being returned.
//
//===========================================================================

void ATeamItem::DisplayFlagReturn( void )
{
	const unsigned int team = TEAM_GetTeamFromItem( this );
	const EColorRange color = static_cast<EColorRange>( TEAM_GetTextColor( team ));
	FString message;

	// Create the "returned" message.
	message.Format( "%s %s returned", TEAM_GetName( team ), GetType( ));
	HUD_DrawCNTRMessage( message.GetChars( ), color );
}

//===========================================================================
//
// ATeamItem :: MarkFlagTaken
//
// Signal to the team module whether or not this flag has been taken.
//
//===========================================================================

void ATeamItem::MarkFlagTaken( bool bTaken )
{
	// [AK] For the white flag, TEAM_GetTeamFromItem should return teams.size( ).
	TEAM_SetItemTaken( TEAM_GetTeamFromItem( this ), bTaken );
}

//===========================================================================
//
// ATeamItem :: ResetReturnTicks
//
// Reset the return ticks for the team associated with this flag.
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
	EColorRange color = static_cast<EColorRange>( TEAM_GetTextColor( team ));
	FString message;

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
		if ( static_cast<AFlag *>( item )->AllowFlagPickup( Owner ) == RETURN_FLAG )
			return ( Super::HandlePickup( item ));

		if (( TEAM_GetSimpleCTFSTMode( )) && ( NETWORK_InClientMode( ) == false ))
		{
			// Give his team a point.
			TEAM_SetPointCount( team, TEAM_GetPointCount( team ) + 1, true );
			PLAYER_SetPoints( Owner->player, Owner->player->lPointCount + 1 );

			// Award the scorer with a "Capture!" medal.
			MEDAL_GiveMedal( player, "Capture" );

			// [RC] Clear the 'returned automatically' message. A bit hackish, but leaves the flag structure unchanged.
			this->ReturnFlag( nullptr );
			HUD_DrawSUBSMessage( "", CR_UNTRANSLATED, 3.0f, 0.5f, true );

			// Create the "captured" message.
			message.Format( "%s team scores!", TEAM_GetName( team ));
			HUD_DrawCNTRMessage( message.GetChars( ), color, 3.0f, 0.5f, true );

			const unsigned int assistPlayer = TEAM_GetAssistPlayer( team );
			const bool selfAssisted = ( assistPlayer == player );

			// [RC] Create the "scored by" and "assisted by" message.
			message.Format( "Scored by: %s", players[player].userinfo.GetName( ));

			if ( assistPlayer != MAXPLAYERS )
			{
				message += '\n';

				if ( selfAssisted )
					message += "[ Self-Assisted ]";
 				else
					message.AppendFormat( "Assisted by: %s", players[assistPlayer].userinfo.GetName( ));
			}

			HUD_DrawSUBSMessage( message.GetChars( ), color, 3.0f, 0.5f, true );

			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			{
				message = players[player].userinfo.GetName( );

				// [AK] Include the assisting player's name in the message if they're not the one who's capturing.
				if (( assistPlayer != MAXPLAYERS ) && ( selfAssisted == false ))
					message.AppendFormat( " and %s", players[assistPlayer].userinfo.GetName( ));

				message += " scored for the ";
				message += TEXTCOLOR_ESCAPE;
				message.AppendFormat( "%s%s " TEXTCOLOR_NORMAL "team!", TEAM_GetTextColorName( team ), TEAM_GetName( team ));
				SERVER_Printf( "%s\n", message.GetChars( ));
			}

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

				Owner->RemoveInventory( inventory );
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
// AFlag :: AllowFlagPickup
//
// Determine whether or not we should be allowed to pickup this flag.
//
//===========================================================================

int AFlag::AllowFlagPickup( AActor *toucher )
{
	// Don't allow the pickup of flags in One Flag CTF.
	if ( oneflagctf )
		return ( DENY_PICKUP );

	return Super::AllowFlagPickup( toucher );
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
	EColorRange color = static_cast<EColorRange>( TEAM_GetTextColor( team ));
	FString message;

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
		// Give his team a point.
		TEAM_SetPointCount( team, TEAM_GetPointCount( team ) + 1, true );
		PLAYER_SetPoints( Owner->player, Owner->player->lPointCount + 1 );

		// Award the scorer with a "Capture!" medal.
		MEDAL_GiveMedal( player, "Capture" );

		// Create the "captured" message.
		message.Format( "%s team scores!", TEAM_GetName( team ));
		HUD_DrawCNTRMessage( message.GetChars( ), color, 3.0f, 0.5f, true );

		// [BC] Rivecoder's "scored by" message.
		message.Format( "Scored by: %s", players[player].userinfo.GetName( ));
		HUD_DrawSUBSMessage( message.GetChars( ), color, 3.0f, 0.5f, true );

		// [AK] Trigger an event script when the white flag is captured.
		GAMEMODE_HandleEvent( GAMEEVENT_CAPTURES, Owner, GAMEEVENT_CAPTURE_NOASSIST, 1 );

		// Take the flag away.
		AInventory *inventory = Owner->FindInventory( this->GetClass( ));

		if ( inventory )
		{
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVERCOMMANDS_TakeInventory( player, inventory->GetClass( ), 0 );

 			Owner->RemoveInventory( inventory );
		}

		this->ReturnFlag( nullptr );

		// Also, refresh the HUD.
		HUD_ShouldRefreshBeforeRendering( );

		return ( true );
	}

	return ( Super::HandlePickup( item ));
}

//===========================================================================
//
// AWhiteFlag :: AllowFlagPickup
//
// Determine whether or not we should be allowed to pickup this flag.
//
//===========================================================================

int AWhiteFlag::AllowFlagPickup( AActor *toucher )
{
	// [BB] Carrying more than one WhiteFlag is not allowed.
	if (( toucher == nullptr ) || ( toucher->FindInventory( PClass::FindClass( "WhiteFlag" ), true ) == nullptr ))
		return ( ALLOW_PICKUP );
	else
		return ( DENY_PICKUP );
}

//===========================================================================
//
// AWhiteFlag :: AnnounceFlagPickup
//
// Play the announcer sound for picking up this flag.
//
//===========================================================================

void AWhiteFlag::AnnounceFlagPickup( AActor *toucher )
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
// AWhiteFlag :: DisplayFlagTaken
//
// Display the text for picking up this flag.
//
//===========================================================================

void AWhiteFlag::DisplayFlagTaken( AActor *toucher )
{
	const int touchingPlayer = static_cast<int>( toucher->player - players );

	// Create the "pickup" message and print it... or if necessary, send it to clients.
	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
	{
		HUD_DrawCNTRMessage( touchingPlayer == consoleplayer ? "You have the flag!" : "White flag taken!", CR_GREY );
	}
	else
	{
		HUD_DrawCNTRMessage( "You have the flag!", CR_GREY, 3.0f, 0.25f, true, touchingPlayer, SVCF_ONLYTHISCLIENT );
		HUD_DrawCNTRMessage( "White flag taken!", CR_GREY, 3.0f, 0.25f, true, touchingPlayer, SVCF_SKIPTHISCLIENT );
	}

	// [BC] Rivecoder's "held by" messages.
	// [AK] Don't show this message to the player picking up the item.
	if (( NETWORK_GetState( ) == NETSTATE_SERVER ) || ( touchingPlayer != consoleplayer ))
	{
		const EColorRange color = static_cast<EColorRange>( TEAM_GetTextColor( players[touchingPlayer].Team ));
		FString message;

		// [AK] Colorize the message in the same way that it is for flags or skulls.
		message.Format( "Held by: %s", players[touchingPlayer].userinfo.GetName( ));

		// Now, print it... or if necessary, send it to clients.
		HUD_DrawSUBSMessage( message.GetChars( ), color, 3.0f, 0.25f, true, touchingPlayer, SVCF_SKIPTHISCLIENT );
	}
}

//===========================================================================
//
// AWhiteFlag :: ReturnFlag
//
// Spawn a new flag at its original location.
//
//===========================================================================

void AWhiteFlag::ReturnFlag( AActor *returner )
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
// AWhiteFlag :: AnnounceFlagReturn
//
// Play the announcer sound for this flag being returned.
//
//===========================================================================

void AWhiteFlag::AnnounceFlagReturn( void )
{
	ANNOUNCER_PlayEntry( cl_announcer, "WhiteFlagReturned" );
}

//===========================================================================
//
// AWhiteFlag :: DisplayFlagReturn
//
// Display the text for this flag being returned.
//
//===========================================================================

void AWhiteFlag::DisplayFlagReturn( void )
{
	// Create the "returned" message.
	HUD_DrawCNTRMessage( "White flag returned", CR_GREY );
}

// Skulltag skull -----------------------------------------------------------

IMPLEMENT_CLASS( ASkull )

//===========================================================================
//
// ASkull :: AllowFlagPickup
//
// Determine whether or not we should be allowed to pickup this flag.
//
//===========================================================================

int ASkull::AllowFlagPickup( AActor *toucher )
{
	return Super::AllowFlagPickup( toucher );
}
