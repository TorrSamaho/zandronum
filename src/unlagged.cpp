//-----------------------------------------------------------------------------
//
// Unlagged Source
// Copyright (C) 2010 "Spleen"
// Copyright (C) 2010-2012 Skulltag Development Team
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
// Filename: unlagged.cpp
//
// Description: Contains variables and routines related to the backwards
// reconciliation.
//
//-----------------------------------------------------------------------------

#include "unlagged.h"
#include "doomstat.h"
#include "doomdef.h"
#include "actor.h"
#include "d_player.h"
#include "r_defs.h"
#include "network.h"
#include "r_state.h"
#include "p_local.h"
#include "i_system.h"
#include "sv_commands.h"
#include "templates.h"
#include "d_netinf.h"

CVAR(Flag, sv_nounlagged, zadmflags, ZADF_NOUNLAGGED);
CVAR( Bool, sv_unlagged_debugactors, false, 0 )

bool reconciledGame = false;
int reconciliationBlockers = 0;

// To keep track of the shooter's height adjustement.
fixed_t reconcilledZ;

void UNLAGGED_Tick( void )
{
	// [BB] Only the server has to do anything here.
	if ( NETWORK_GetState() != NETSTATE_SERVER )
		return;

	// [Spleen] Record sectors soon before they are reconciled/restored
	UNLAGGED_RecordSectors( );

	// [Spleen] Record players
	for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ++ulIdx )
	{
		if ( PLAYER_IsValidPlayerWithMo( ulIdx ) )
			UNLAGGED_RecordPlayer( &players[ulIdx] );
	}
}

//Figure out which tic to use for reconciliation
int UNLAGGED_Gametic( player_t *player )
{
	// [BB] Sanity check.
	if ( player == NULL )
		return gametic;

	const ULONG playerNum = static_cast<ULONG> ( player - players );

	// Sanity check
	if ( playerNum >= MAXPLAYERS )
		return gametic;

	// [CK] We do tick-based ping now.
	const CLIENT_s *pClient = SERVER_GetClient(playerNum);
	if ( pClient == NULL )
		return gametic;

	// [CK] If the client wants ping unlagged, use that.
	int unlaggedGametic = ( players[playerNum].userinfo.GetClientFlags() & CLIENTFLAGS_PING_UNLAGGED ) ?
							gametic - ( player->ulPing * TICRATE / 1000 ) :
							pClient->lLastServerGametic;

	// Do not let the client send us invalid gametics ahead of the server's
	// gametic. This should be guarded against, but just in case...
	if ( unlaggedGametic > gametic )
		unlaggedGametic = gametic;

	//don't look up tics that are too old
	if ( (gametic - unlaggedGametic) >= UNLAGGEDTICS)
		unlaggedGametic = gametic - UNLAGGEDTICS + 1;

	//don't look up negative tics
	if (unlaggedGametic < 0)
		unlaggedGametic = 0;

	return unlaggedGametic;
}

// Shift stuff back in time before doing hitscan calculations
// Call UNLAGGED_Restore afterwards to restore everything
void UNLAGGED_Reconcile( AActor *actor )
{
	// [AK] Don't do anything if it's not a server with unlagged or if reconciliation is being blocked.
	if (( NETWORK_GetState( ) != NETSTATE_SERVER ) || ( zadmflags & ZADF_NOUNLAGGED ) || ( reconciliationBlockers > 0 ))
		return;

	// [AK] Don't do anything if the actor isn't a player, is a bot, or if this player disabled unlagged for themselves.
	if (( actor == NULL ) || ( actor->player == NULL ) || ( actor->player->bIsBot ) || (( actor->player->userinfo.GetClientFlags( ) & CLIENTFLAGS_UNLAGGED ) == 0 ))
		return;

	//Something went wrong, reconciliation was attempted when the gamestate
	//was already reconciled!
	if (reconciledGame)
	{
		// [BB] I_Error terminates the current game, so we need to reset the value of reconciledGame,
		// otherwise UNLAGGED_Reconcile will always trigger this error from now on.
		reconciledGame = false;
		I_Error("UNLAGGED_Reconcile called while reconciledGame is true");
	}

	const int unlaggedGametic = UNLAGGED_Gametic( actor->player );

	//Don't reconcile if the unlagged gametic is the same as the current
	//because unlagged data for this tic may not be completely recorded yet
	if (unlaggedGametic == gametic)
		return;

	reconciledGame = true;

	//find the index
	const int unlaggedIndex = unlaggedGametic % UNLAGGEDTICS;

	//reconcile the sectors
	for (int i = 0; i < numsectors; ++i)
	{
		sectors[i].floorplane.restoreD = sectors[i].floorplane.d;
		sectors[i].ceilingplane.restoreD = sectors[i].ceilingplane.d;

		sectors[i].floorplane.d = sectors[i].floorplane.unlaggedD[unlaggedIndex];
		sectors[i].ceilingplane.d = sectors[i].ceilingplane.unlaggedD[unlaggedIndex];
	}

	//reconcile the players
	for (int i = 0; i < MAXPLAYERS; ++i)
	{
		if (playeringame[i] && players[i].mo && !players[i].bSpectating)
		{
			players[i].restorePos[0] = players[i].mo->x;
			players[i].restorePos[1] = players[i].mo->y;
			players[i].restorePos[2] = players[i].mo->z;

			//Work around limitations of SetOrigin to prevent players
			//from getting stuck in ledges
			players[i].restoreFloorZ = players[i].mo->floorz;
			// [BB] ... and from jumping up through solid 3D floors.
			players[i].restoreCeilingZ = players[i].mo->ceilingz;

			//Also, don't reconcile the shooter because the client is supposed
			//to predict him
			if (players+i != actor->player)
			{
				players[i].mo->SetOrigin( players[i].unlaggedPos[unlaggedIndex][0], players[i].unlaggedPos[unlaggedIndex][1], players[i].unlaggedPos[unlaggedIndex][2] );

				// [AK] We only need to restore the heights of the other players except the shooter.
				players[i].restoreHeight = players[i].mo->height;
				players[i].mo->height = players[i].unlaggedHeight[unlaggedIndex];
			}
			else
				//However, the client sometimes mispredicts itself if it's on a moving sector.
				//We need to correct for that.
			{
				//current server floorz/ceilingz before reconciliation
				fixed_t serverFloorZ = actor->floorz;
				fixed_t serverCeilingZ = actor->ceilingz;

				// [BB] Try to reset floorz/ceilingz to account for the fact that the sector the actor is in was possibly reconciled.
				actor->floorz = actor->Sector->floorplane.ZatPoint (actor->x, actor->y);
				actor->ceilingz = actor->Sector->ceilingplane.ZatPoint (actor->x, actor->y);
				P_FindFloorCeiling(actor, false);

				//force the shooter out of the floor/ceiling - a client has to mispredict in this case,
				//because not mispredicting would mean the client would think he's inside the floor/ceiling
				if (actor->z + actor->height > actor->ceilingz)
					actor->z = actor->ceilingz - actor->height;

				if (actor->z < actor->floorz)
					actor->z = actor->floorz;

				//floor moved up - a client might have mispredicted himself too low due to gravity
				//and the client thinking the floor is lower than it actually is
				// [BB] But only do this if the sector actually moved. Note: This adjustment seems to break on some kind of non-moving 3D floors.
				if ( (serverFloorZ > actor->floorz) && (( actor->Sector->floorplane.restoreD != actor->Sector->floorplane.d ) || ( actor->Sector->ceilingplane.restoreD != actor->Sector->ceilingplane.d )) )
				{
					//shooter was standing on the floor, let's pull him down to his floor if
					//he wasn't falling
					if ( (actor->z == serverFloorZ) && (actor->velz >= 0) )
						actor->z = actor->floorz;

					//todo: more correction for floor moving up
				}

				//todo: more correction for client misprediction

				// Keep track of our adjustements.
				reconcilledZ = actor->z;
			}
		}
	}	
}

void UNLAGGED_SwapSectorUnlaggedStatus( )
{
	if ( reconciledGame == false )
		return;

	for (int i = 0; i < numsectors; ++i)
	{
		swapvalues ( sectors[i].floorplane.d, sectors[i].floorplane.restoreD );
		swapvalues ( sectors[i].ceilingplane.d, sectors[i].ceilingplane.restoreD );
	}
}

// Restore everything that has been shifted
// back in time by UNLAGGED_Reconcile
void UNLAGGED_Restore( AActor *actor )
{
	//Only do anything if the game is currently reconciled
	// [BB] Since reconciledGame can only be true if all necessary checks in UNLAGGED_Reconcile were passed,
	// we don't need to check anything but reconciledGame here.
	// [Spleen] Also need to check for any reconciliationBlockers
	if ( !reconciledGame || ( reconciliationBlockers > 0 ) )
		return;

	//restore the sectors
	for (int i = 0; i < numsectors; ++i)
	{
		sectors[i].floorplane.d = sectors[i].floorplane.restoreD;
		sectors[i].ceilingplane.d = sectors[i].ceilingplane.restoreD;
	}

	const int unlaggedIndex = UNLAGGED_Gametic( actor->player ) % UNLAGGEDTICS;

	//restore the players
	for (int i = 0; i < MAXPLAYERS; ++i)
	{
		if (playeringame[i] && players[i].mo && !players[i].bSpectating)
		{
			if ( players + i != actor->player )
			{
				// [AK] Always restore their height unless it changed during reconciliation (e.g. the player died).
				if ( players[i].mo->height == players[i].unlaggedHeight[unlaggedIndex] )
					players[i].mo->height = players[i].restoreHeight;

				// Do not restore this player's position if the shot resulted in his direct teleportation.
				if ( players[i].mo->x != players[i].unlaggedPos[unlaggedIndex][0] ||
					players[i].mo->y != players[i].unlaggedPos[unlaggedIndex][1] ||
					players[i].mo->z != players[i].unlaggedPos[unlaggedIndex][2] )
				{
					continue;
				}
			}
			else if ( actor->x != players[i].restorePos[0] || actor->y != players[i].restorePos[1] || actor->z != reconcilledZ )
			{
				continue;
			}

			players[i].mo->SetOrigin( players[i].restorePos[0], players[i].restorePos[1], players[i].restorePos[2] );
			players[i].mo->floorz = players[i].restoreFloorZ;
			players[i].mo->ceilingz = players[i].restoreCeilingZ;
		}
	}

	reconciledGame = false;
}


// Record the positions of just one player
// in order to be able to reconcile them later
void UNLAGGED_RecordPlayer( player_t *player )
{
	// [BB] Sanity check.
	if ( player == NULL )
		return;

	//Only do anything if it's on a server
	if (NETWORK_GetState() != NETSTATE_SERVER)
		return;

	//find the index
	const int unlaggedIndex = gametic % UNLAGGEDTICS;

	//record the player
	player->unlaggedPos[unlaggedIndex][0] = player->mo->x;
	player->unlaggedPos[unlaggedIndex][1] = player->mo->y;
	player->unlaggedPos[unlaggedIndex][2] = player->mo->z;
	player->unlaggedHeight[unlaggedIndex] = player->mo->height;
}


// Reset the reconciliation buffers of a player
// Should be called when a player is spawned
void UNLAGGED_ResetPlayer( player_t *player )
{
	// [BB] Sanity check.
	if ( player == NULL )
		return;

	//Only do anything if it's on a server
	if (NETWORK_GetState() != NETSTATE_SERVER)
		return;

	for (int unlaggedIndex = 0; unlaggedIndex < UNLAGGEDTICS; ++unlaggedIndex)
	{
		player->unlaggedPos[unlaggedIndex][0] = player->mo->x;
		player->unlaggedPos[unlaggedIndex][1] = player->mo->y;
		player->unlaggedPos[unlaggedIndex][2] = player->mo->z;
		player->unlaggedHeight[unlaggedIndex] = player->mo->height;
	}
}


// Record the positions of the sectors
void UNLAGGED_RecordSectors( )
{
	//Only do anything if it's on a server
	if (NETWORK_GetState() != NETSTATE_SERVER)
		return;

	//find the index
	const int unlaggedIndex = gametic % UNLAGGEDTICS;

	//record the sectors
	for (int i = 0; i < numsectors; ++i)
	{
		sectors[i].floorplane.unlaggedD[unlaggedIndex] = sectors[i].floorplane.d;
		sectors[i].ceilingplane.unlaggedD[unlaggedIndex] = sectors[i].ceilingplane.d;
	}
}

bool UNLAGGED_DrawRailClientside ( AActor *attacker )
{
	if ( ( attacker == NULL ) || ( attacker->player == NULL ) )
		return false;

	// [BB] Rails are only client side when unlagged is on.
	if ( ( zadmflags & ZADF_NOUNLAGGED ) || ( ( attacker->player->userinfo.GetClientFlags() & CLIENTFLAGS_UNLAGGED ) == 0 ) )
		return false;

	// [BB] A client should only draw rails for its own player.
	if ( ( NETWORK_GetState() != NETSTATE_SERVER ) && ( ( attacker->player - players ) != consoleplayer ) )
		return false;

	return true;
}

// [BB] If reconciliation moved the actor we hit, this function calculates the offset.
void UNLAGGED_GetHitOffset ( const AActor *attacker, const FTraceResults &trace, TVector3<fixed_t> &hitOffset )
{
	hitOffset.Zero();

	// [BB] The game is not reconciled, so no offset. If the attacker is unknown, we can't calculate the offset.
	if ( ( reconciledGame == false ) || ( attacker == NULL ) )
		return;

	const int unlaggedGametic = UNLAGGED_Gametic( attacker->player );

	// [BB] No reconciliation no offset.
	if ( unlaggedGametic == gametic )
		return;

	if ( ( trace.HitType == TRACE_HitActor ) && trace.Actor && trace.Actor->player )
	{
		const int unlaggedIndex = unlaggedGametic % UNLAGGEDTICS;

		const player_t *hitPlayer = trace.Actor->player;
		hitOffset = hitPlayer->restorePos - hitPlayer->unlaggedPos[unlaggedIndex];
	}
}

bool UNLAGGED_IsReconciled ( )
{
  return reconciledGame;
}

void UNLAGGED_AddReconciliationBlocker ( )
{
	reconciliationBlockers++;
}

void UNLAGGED_RemoveReconciliationBlocker ( )
{
	if ( reconciliationBlockers == 0 )
		I_Error("UNLAGGED_RemoveReconciliationBlocker called when reconciliationBlockers == 0");
	else
		reconciliationBlockers--;
}

void UNLAGGED_SpawnDebugActors ( )
{
	const PClass *pType = PClass::FindClass( "UnlaggedDebugActor" );
	if ( pType == NULL )
		I_FatalError( "To spawn unlagged debug actors a DECORATE actor called \"UnlaggedDebugActor\" needs to be defined!\n" );

	// [BB] Since there is no function that lets the server instruct a client to spawn an actor
	// of a certain type at a specified position without actually having such an actor at that
	// position (and I don't feel like adding such a thing just for this debug function) we
	// create and move a dummy actor here.
	AActor *pActor = Spawn( pType, 0, 0, 0, NO_REPLACE );
	if ( pActor )
	{
		for ( ULONG ulPlayer = 0; ulPlayer < MAXPLAYERS; ++ulPlayer )
		{
			if ( SERVER_IsValidClient( ulPlayer ) == false )
				continue;

			const int unlaggedGametic = UNLAGGED_Gametic( &players[ulPlayer] );

			if ( unlaggedGametic == gametic )
				continue;

			const int unlaggedIndex = unlaggedGametic % UNLAGGEDTICS;

			for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ++ulIdx )
			{
				if ( ( ulPlayer == ulIdx ) || ( PLAYER_IsValidPlayer ( ulIdx ) == false ) || players[ulIdx].bSpectating )
					continue;

				pActor->x = players[ulIdx].unlaggedPos[unlaggedIndex][0];
				pActor->y = players[ulIdx].unlaggedPos[unlaggedIndex][1];
				pActor->z = players[ulIdx].unlaggedPos[unlaggedIndex][2];

				SERVERCOMMANDS_SpawnThingNoNetID( pActor, ulPlayer, SVCF_ONLYTHISCLIENT );
			}
		}
		// [BB] Get rid ot the dummy actor.
		pActor->Destroy();
	}
}
