// Emacs style mode select	 -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//		Handling interactions (i.e., collisions).
//
//-----------------------------------------------------------------------------




// Data.
#include "doomdef.h"
#include "gstrings.h"

#include "doomstat.h"

#include "m_random.h"
#include "i_system.h"
#include "announcer.h"

#include "am_map.h"

#include "c_console.h"
#include "c_dispatch.h"

#include "p_local.h"

#include "p_lnspec.h"
#include "p_effect.h"
#include "p_acs.h"

#include "ravenshared.h"
#include "a_hexenglobal.h"
#include "a_sharedglobal.h"
#include "a_pickups.h"
#include "gi.h"
#include "templates.h"
#include "sbar.h"
#include "s_sound.h"
#include "g_level.h"
#include "d_net.h"
#include "d_netinf.h"
// [BB] New #includes.
#include "deathmatch.h"
#include "duel.h"
#include "d_event.h"
#include "medal.h"
#include "network.h"
#include "sv_commands.h"
#include "g_game.h"
#include "gamemode.h"
#include "cl_main.h"
#include "joinqueue.h"
#include "lastmanstanding.h"
#include "scoreboard.h"
#include "cooperative.h"
#include "invasion.h"
#include "survival.h"
#include "team.h"
#include "cl_commands.h"
#include "cl_demo.h"
#include "r_data/r_translate.h"
#include "p_enemy.h"
#include "r_data/colormaps.h"
#include "v_video.h"
#include "st_hud.h"
#include "p_terrain.h"

// [BC] Ugh.
void SERVERCONSOLE_UpdatePlayerInfo( LONG lPlayer, ULONG ulUpdateFlags );
void SERVERCONSOLE_UpdateScoreboard( void );

static FRandom pr_obituary ("Obituary");
static FRandom pr_botrespawn ("BotRespawn");
static FRandom pr_killmobj ("ActorDie");
FRandom pr_damagemobj ("ActorTakeDamage");
static FRandom pr_lightning ("LightningDamage");
static FRandom pr_poison ("PoisonDamage");
static FRandom pr_switcher ("SwitchTarget");
static FRandom pr_kickbackdir ("KickbackDir");

EXTERN_CVAR (Bool, show_obituaries)
// [BB] FIXME
//EXTERN_CVAR( Int, menu_teamidxjointeammenu )

// [AK] If true, the intensity of the blood on the screen takes into account
// the player's max health instead of a hard-coded health of 100.
CVAR( Bool, blood_fade_usemaxhealth, false, CVAR_ARCHIVE )

FName MeansOfDeath;
bool FriendlyFire;

//
// GET STUFF
//

//
// P_TouchSpecialThing
//
void P_TouchSpecialThing (AActor *special, AActor *toucher)
{
	fixed_t delta = special->z - toucher->z;

	// The pickup is at or above the toucher's feet OR
	// The pickup is below the toucher.
	if (delta > toucher->height || delta < MIN(-32*FRACUNIT, -special->height))
	{ // out of reach
		return;
	}

	// Dead thing touching.
	// Can happen with a sliding player corpse.
	if (toucher->health <= 0)
		return;

	// [BC] Don't allow non-players to touch special things.
	if ( toucher->player == NULL )
		return;

	// [BC] Don't allow spectating players to pickup items.
	if ( toucher->player->bSpectating )
		return;

	special->Touch (toucher);
}


// [RH]
// SexMessage: Replace parts of strings with gender-specific pronouns
//
// The following expansions are performed:
//		%g -> he/she/it
//		%h -> him/her/it
//		%p -> his/her/its
//		%o -> other (victim)
//		%k -> killer
//
void SexMessage (const char *from, char *to, int gender, const char *victim, const char *killer)
{
	static const char *genderstuff[3][3] =
	{
		{ "he",  "him", "his" },
		{ "she", "her", "her" },
		{ "it",  "it",  "its" }
	};
	static const int gendershift[3][3] =
	{
		{ 2, 3, 3 },
		{ 3, 3, 3 },
		{ 2, 2, 3 }
	};
	const char *subst = NULL;

	do
	{
		if (*from != '%')
		{
			*to++ = *from;
		}
		else
		{
			int gendermsg = -1;
			
			switch (from[1])
			{
			case 'g':	gendermsg = 0;	break;
			case 'h':	gendermsg = 1;	break;
			case 'p':	gendermsg = 2;	break;
			case 'o':	subst = victim;	break;
			case 'k':	subst = killer;	break;
			}
			if (subst != NULL)
			{
				size_t len = strlen (subst);
				memcpy (to, subst, len);
				to += len;
				from++;
				subst = NULL;
			}
			else if (gendermsg < 0)
			{
				*to++ = '%';
			}
			else
			{
				strcpy (to, genderstuff[gender][gendermsg]);
				to += gendershift[gender][gendermsg];
				from++;
			}
		}
	} while (*from++);
}

// [RH]
// ClientObituary: Show a message when a player dies
//
// [BC] Allow passing in of the MOD so clients can use this function too.
void ClientObituary (AActor *self, AActor *inflictor, AActor *attacker, int dmgflags, FName MeansOfDeath)
{
	FName	mod;
	const char *message;
	const char *messagename;
	char gendermessage[1024];
	bool friendly;
	int  gender;

	// [AK] Check if we shouldn't print the obituary due to ZADF_NO_OBITUARIES or if the
	// player was forced to dead spectators through ACS.
	if ((zacompatflags & ZACOMPATF_NO_OBITUARIES) || MeansOfDeath == NAME_DeadSpectate)
		return;

	// No obituaries for non-players, voodoo dolls or when not wanted
	if (self->player == NULL || self->player->mo != self || !show_obituaries)
		return;

	gender = self->player->userinfo.GetGender();

	// Treat voodoo dolls as unknown deaths
	if (inflictor && inflictor->player && inflictor->player->mo != inflictor)
		MeansOfDeath = NAME_None;

	// Must be in cooperative mode.
	// [TP] Changed to work in deathmatch modes too. Clients must set FriendlyFire
	// to false here because they do not enter P_DamageMobj.
	if ( attacker && attacker->IsTeammate( self ) )
		FriendlyFire = true;
	else if ( NETWORK_InClientMode() )
		FriendlyFire = false;

	friendly = FriendlyFire;
	mod = MeansOfDeath;
	message = NULL;
	messagename = NULL;

	switch (mod)
	{
	case NAME_Suicide:		messagename = "OB_SUICIDE";		break;
	case NAME_Falling:		messagename = "OB_FALLING";		break;
	case NAME_Crush:		messagename = "OB_CRUSH";		break;
	case NAME_Exit:			messagename = "OB_EXIT";		break;
	case NAME_Drowning:		messagename = "OB_WATER";		break;
	case NAME_Slime:		messagename = "OB_SLIME";		break;
	case NAME_Fire:			if (attacker == NULL) messagename = "OB_LAVA";		break;
	}

	// Check for being killed by a voodoo doll.
	if (inflictor && inflictor->player && inflictor->player->mo != inflictor)
	{
		messagename = "OB_VOODOO";
	}

	if (messagename != NULL)
		message = GStrings(messagename);

	if (attacker != NULL && message == NULL)
	{
		if (attacker == self)
		{
			if (messagename != NULL)
				message = GStrings(messagename);
			else
			{
				// [SB] Replaced Skulltag's hardcoded obituaries for killing oneself with
				// the BFG 10k and grenades with a new SelfObituary property.
				if (inflictor != NULL && inflictor != attacker)
				{
					message = inflictor->GetClass()->Meta.GetMetaString (AMETA_SelfObituary);
				}
				// [SB] Just in case the player somehow manages to shoot themselves...
				if (message == NULL && (dmgflags & DMG_PLAYERATTACK) && attacker->player->ReadyWeapon != NULL)
				{
					message = attacker->player->ReadyWeapon->GetClass()->Meta.GetMetaString (AMETA_SelfObituary);
				}
				if (message == NULL)
				{
					message = GStrings("OB_KILLEDSELF");
				}
			}
		}
		else if (attacker->player == NULL)
		{
			if ((mod == NAME_Telefrag) || (mod == NAME_SpawnTelefrag))
			{
				message = GStrings("OB_MONTELEFRAG");
			}
			else if (mod == NAME_Melee)
			{
				message = attacker->GetClass()->Meta.GetMetaString (AMETA_HitObituary);
				if (message == NULL)
				{
					message = attacker->GetClass()->Meta.GetMetaString (AMETA_Obituary);
				}
			}
			else
			{
				message = attacker->GetClass()->Meta.GetMetaString (AMETA_Obituary);
			}
		}
	}

	if (message == NULL && attacker != NULL && attacker->player != NULL)
	{
		if (friendly)
		{
			// [BC] We'll do this elsewhere.
//			attacker->player->fragcount -= 2;
			self = attacker;
			gender = self->player->userinfo.GetGender();
			mysnprintf (gendermessage, countof(gendermessage), "OB_FRIENDLY%c", '1' + (pr_obituary() & 3));
			message = GStrings(gendermessage);
		}
		else
		{
			// [BC] NAME_SpawnTelefrag, too.
			if ((mod == NAME_Telefrag) || (mod == NAME_SpawnTelefrag)) message = GStrings("OB_MPTELEFRAG");
			// [BC] Handle Skulltag's reflection rune.
			// [RC] Moved here to fix the "[victim] was killed via [victim]'s reflection rune" bug.
			else if ( mod == NAME_Reflection )
			{
				messagename = "OB_REFLECTION";
				message = GStrings(messagename);
			}

			if (message == NULL)
			{
				// [AK] We must also check if the actor inflicting the damage isn't also
				// the attacker. This ensures obituaries for the BFG9000's tracer or railgun
				// attacks are used first instead of the one defined in the player's class.
				if (inflictor != NULL && inflictor != attacker)
				{
					message = inflictor->GetClass()->Meta.GetMetaString (AMETA_Obituary);
				}
				if (message == NULL && (dmgflags & DMG_PLAYERATTACK) && attacker->player->ReadyWeapon != NULL)
				{
					message = attacker->player->ReadyWeapon->GetClass()->Meta.GetMetaString (AMETA_Obituary);
				}
				if (message == NULL)
				{
					switch (mod)
					{
					case NAME_BFGSplash:	messagename = "OB_MPBFG_SPLASH";	break;
					case NAME_Railgun:		messagename = "OB_RAILGUN";			break;
					}
					if (messagename != NULL)
						message = GStrings(messagename);
				}
				if (message == NULL)
				{
					message = attacker->GetClass()->Meta.GetMetaString (AMETA_Obituary);
				}
			}
		}
	}
	// [TRSR] If the inflictor exists and is unowned, check for selfobituary here too.
	else if (message == NULL && attacker == NULL && inflictor != NULL)
	{
		message = inflictor->GetClass()->Meta.GetMetaString (AMETA_SelfObituary);
		attacker = self;
	}
	else attacker = self;	// for the message creation

	if (message != NULL && message[0] == '$') 
	{
		message = GStrings[message+1];
	}

	if (message == NULL)
	{
		message = GStrings("OB_DEFAULT");
	}

	SexMessage (message, gendermessage, gender,
		self->player->userinfo.GetName(), attacker->player->userinfo.GetName());

	// [AK] Format our message so color codes can appear.
	V_ColorizeString( gendermessage );
	Printf (PRINT_MEDIUM, "%s\n", gendermessage);
}


//
// KillMobj
//
EXTERN_CVAR (Int, fraglimit)

void AActor::Die (AActor *source, AActor *inflictor, int dmgflags)
{
	// [BB] Potentially get rid of some corpses. This isn't necessarily client-only.
	//CLIENT_RemoveMonsterCorpses();

	// [BC]
	bool	bPossessedTerminatorArtifact;

	// Handle possible unmorph on death
	bool wasgibbed = (health < GibHealth());

	AActor *realthis = NULL;
	int realstyle = 0;
	int realhealth = 0;
	if (P_MorphedDeath(this, &realthis, &realstyle, &realhealth))
	{
		if (!(realstyle & MORPH_UNDOBYDEATHSAVES))
		{
			if (wasgibbed)
			{
				int realgibhealth = realthis->GibHealth();
				if (realthis->health >= realgibhealth)
				{
					realthis->health = realgibhealth -1; // if morphed was gibbed, so must original be (where allowed)l
				}
			}
			realthis->Die(source, inflictor, dmgflags);
		}
		return;
	}

	// [SO] 9/2/02 -- It's rather funny to see an exploded player body with the invuln sparkle active :) 
	effects &= ~FX_RESPAWNINVUL;

	// Also kill the alpha flicker cause by the visibility flicker.
	if ( effects & FX_VISIBILITYFLICKER )
	{
		RenderStyle = STYLE_Normal;
		effects &= ~FX_VISIBILITYFLICKER;
	}

	//flags &= ~MF_INVINCIBLE;
/*
	if (debugfile && this->player)
	{
		static int dieticks[MAXPLAYERS];
		int pnum = int(this->player-players);
		dieticks[pnum] = gametic;
		fprintf (debugfile, "died (%d) on tic %d (%s)\n", pnum, gametic,
		this->player->cheats&CF_PREDICTING?"predicting":"real");
	}
*/
	// [BC] Since the player loses his terminator status after we tell his inventory
	// that we died, check for it before doing so.
	bPossessedTerminatorArtifact = !!(( player ) && ( player->cheats2 & CF2_TERMINATORARTIFACT ));

	// [AK] Some stuff to do if the actor that died is a player.
	if (( player ) && ( NETWORK_InClientMode( ) == false ))
	{
		const ULONG ulPlayer = player - players;

		// [BC] Check to see if any medals need to be awarded.
		MEDAL_PlayerDied( ulPlayer, (( source ) && ( source->player )) ? static_cast<ULONG>( source->player - players ) : MAXPLAYERS );

		// [AK] Increment this player's death count, except during warm-ups or spawn telefrags.
		if (( MeansOfDeath != NAME_SpawnTelefrag ) && ( GAMEMODE_IsGameInCountdown( ) == false ))
			PLAYER_SetDeaths( &players[ulPlayer], players[ulPlayer].ulDeathCount + 1 );
	}

	// [RH] Notify this actor's items.
	for (AInventory *item = Inventory; item != NULL; )
	{
		AInventory *next = item->Inventory;
		item->OwnerDied();
		item = next;
	}

	if (flags & MF_MISSILE)
	{ // [RH] When missiles die, they just explode
		P_ExplodeMissile (this, NULL, NULL);
		return;
	}
	// [RH] Set the target to the thing that killed it. Strife apparently does this.
	if (source != NULL)
	{
		target = source;
	}

	// [JM] Fire KILL type scripts for actor. Not needed for players, since they have the "DEATH" script type.
	if (!player && !(flags7 & MF7_NOKILLSCRIPTS) && ((flags7 & MF7_USEKILLSCRIPTS) || gameinfo.forcekillscripts))
	{
		// [BB] Clients should only do this for client handled actors.
		if ( NETWORK_InClientModeAndActorNotClientHandled( this ) == false )
			FBehavior::StaticStartTypedScripts(SCRIPT_Kill, this, true, 0, true);
	}

	flags &= ~(MF_SHOOTABLE|MF_FLOAT|MF_SKULLFLY);
	if (!(flags4 & MF4_DONTFALL)) flags&=~MF_NOGRAVITY;
	flags |= MF_DROPOFF;
	if ((flags3 & MF3_ISMONSTER) || FindState(NAME_Raise) != NULL || IsKindOf(RUNTIME_CLASS(APlayerPawn)))
	{	// [RH] Only monsters get to be corpses.
		// Objects with a raise state should get the flag as well so they can
		// be revived by an Arch-Vile. Batman Doom needs this.
		// [RC] And disable this if DONTCORPSE is set, of course.
		if(!(flags6 & MF6_DONTCORPSE)) flags |= MF_CORPSE;
		// [BB] Potentially get rid of one corpse in invasion from the previous wave.
		if ( invasion )
			INVASION_RemoveMonsterCorpse();
	}
	flags6 |= MF6_KILLED;

	// [RH] Allow the death height to be overridden using metadata.
	fixed_t metaheight = 0;
	if (DamageType == NAME_Fire)
	{
		metaheight = GetClass()->Meta.GetMetaFixed (AMETA_BurnHeight);
	}
	if (metaheight == 0)
	{
		metaheight = GetClass()->Meta.GetMetaFixed (AMETA_DeathHeight);
	}
	if (metaheight != 0)
	{
		height = MAX<fixed_t> (metaheight, 0);
	}
	else
	{
		height >>= 2;
	}

	// [RH] If the thing has a special, execute and remove it
	//		Note that the thing that killed it is considered
	//		the activator of the script.
	// New: In Hexen, the thing that died is the activator,
	//		so now a level flag selects who the activator gets to be.
	// Everything is now moved to P_ActivateThingSpecial().
	if (special && (!(flags & MF_SPECIAL) || (flags3 & MF3_ISMONSTER))
		&& !(activationtype & THINGSPEC_NoDeathSpecial))
	{
		P_ActivateThingSpecial(this, source, true); 
	}

	if (CountsAsKill())
	{
		level.killed_monsters++;
		
		// [BB] The number of remaining monsters decreased, update the invasion monster count accordingly.
		INVASION_UpdateMonsterCount ( this, true );
	}

	if (source && source->player)
	{
		// [BC] Don't do this in client mode.
		if ((CountsAsKill()) &&
			( NETWORK_InClientMode() == false ))
		{ // count for intermission
			// [AK] Call PLAYER_SetKills to increment the kill count instead.
			PLAYER_SetKills( source->player, source->player->killcount + 1 );
			//source->player->killcount++;
		}

		// Don't count any frags at level start, because they're just telefrags
		// resulting from insufficient deathmatch starts, and it wouldn't be
		// fair to count them toward a player's score.
		if (player && ( MeansOfDeath != NAME_SpawnTelefrag ))
		{
			if ( source->player->pSkullBot )
			{
				source->player->pSkullBot->m_ulPlayerKilled = player - players;
				if (( player - players ) == static_cast<int> (source->player->pSkullBot->m_ulPlayerEnemy) )
					source->player->pSkullBot->PostEvent( BOTEVENT_ENEMY_KILLED );
			}

			if (player == source->player)	// [RH] Cumulative frag count
			{
				// [BC] Frags are server side.
				if ( NETWORK_InClientMode() == false )
					PLAYER_SetFragcount( player, player->fragcount - (( bPossessedTerminatorArtifact ) ? 10 : 1 ), true, true );
			}
			else
			{
				// [BB] A player just fragged another player.
				GAMEMODE_HandleEvent ( GAMEEVENT_PLAYERFRAGS, source->player->mo, static_cast<int> ( player - players ) );

				// [BC] Frags are server side.
				// [BC] Player receives 10 frags for killing the terminator!
				if ( NETWORK_InClientMode() == false )
				{
					if ((dmflags2 & DF2_YES_LOSEFRAG) && deathmatch)
						PLAYER_SetFragcount( player, player->fragcount - 1, true, true );

					if ( source->IsTeammate( this ))
						PLAYER_SetFragcount( source->player, source->player->fragcount - (( bPossessedTerminatorArtifact ) ? 10 : 1 ), true, true );
					else
						PLAYER_SetFragcount( source->player, source->player->fragcount + (( bPossessedTerminatorArtifact ) ? 10 : 1 ), true, true );
				}

				// [BC] Add this frag to the server's statistic module.
				if ( NETWORK_GetState( ) == NETSTATE_SERVER )
					SERVER_STATISTIC_AddToTotalFrags( );

				if (source->player->morphTics)
				{ // Make a super chicken
					source->GiveInventoryType (RUNTIME_CLASS(APowerWeaponLevel2));
				}
			}

			// Play announcer sounds for amount of frags remaining.
			if ( ( GAMEMODE_GetCurrentFlags() & GMF_PLAYERSEARNFRAGS ) && fraglimit )
			{
				// [RH] Implement fraglimit
				// [BC] Betterized!
				// [BB] Clients may not do this.
				if ( fraglimit <= D_GetFragCount( source->player )
					 && ( NETWORK_InClientMode() == false ) )
				{
					NETWORK_Printf( "%s\n", GStrings( "TXT_FRAGLIMIT" ));
					if ( teamplay && ( source->player->bOnTeam ))
						NETWORK_Printf( "%s wins!\n", TEAM_GetName( source->player->Team ));
					else
						NETWORK_Printf( "%s wins!\n", source->player->userinfo.GetName() );

					if (( NETWORK_GetState( ) != NETSTATE_SERVER )
						&& ( duel == false )
						&& ( source->player - players == consoleplayer ))
					{
						ANNOUNCER_PlayEntry( cl_announcer, "YouWin" );
					}

					// Pause for five seconds for the win sequence.
					if ( duel )
					{
						// Also, do the win sequence for the player.
						DUEL_SetLoser( player - players );
						DUEL_DoWinSequence( source->player - players );

						// Give the winner a win.
						PLAYER_SetWins( source->player, source->player->ulWins + 1 );

						GAME_SetEndLevelDelay( 5 * TICRATE );
					}
					// End the level after five seconds.
					else
					{
						FString message;
						EColorRange color = CR_RED;

						// Just print "YOU WIN!" in single player.
						if (( NETWORK_GetState( ) == NETSTATE_SINGLE_MULTIPLAYER ) && ( players[consoleplayer].mo->CheckLocalView( source->player - players )))
						{
							message = "YOU WIN!";
						}
						else if (( teamplay ) && ( source->player->bOnTeam ))
						{
							message.Format( "%s wins!", TEAM_GetName( source->player->Team ));
							color = static_cast<EColorRange>( TEAM_GetTextColor( source->player->Team ));
						}
						else
						{
							message.Format( "%s WINS!", players[source->player - players].userinfo.GetName( ));
						}

						// Display "%s WINS!" HUD message.
						HUD_DrawStandardMessage( message, color, false, 3.0f, 2.0f, true );

						// [AK] Clear the frag and place HUD messages from the screen.
						HUD_ClearFragAndPlaceMessages( true );

						GAME_SetEndLevelDelay( 5 * TICRATE );
					}
				}
			}
		}

		// If the player got telefragged by a player trying to spawn, allow him to respawn.
		if (( player ) && ( MeansOfDeath == NAME_SpawnTelefrag ))
			player->bSpawnTelefragged = true;
	}
	else if (( NETWORK_InClientMode() == false ) && (CountsAsKill()))
	{
		// count all monster deaths,
		// even those caused by other monsters

		// Why give player 0 credit? :P
		// Meh, just do it in single player.
		if ( NETWORK_GetState( ) == NETSTATE_SINGLE )
		{
			// [AK] Call PLAYER_SetKills to increment the kill count instead.
			PLAYER_SetKills( &players[0], players[0].killcount + 1 );
			// players[0].killcount++;
		}

		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		{
//			SERVER_PlayerKilledMonster( MAXPLAYERS );

			// Also, update the scoreboard.
			SERVERCONSOLE_UpdateScoreboard( );
		}
	}
	
	// [BC] Don't do this block in client mode.
	if (player &&
		( NETWORK_InClientMode() == false ))
	{
		// [BC] If this is a bot, tell it it died.
		if ( player->pSkullBot )
		{
			if ( source && source->player )
				player->pSkullBot->m_ulPlayerKilledBy = source->player - players;
			else
				player->pSkullBot->m_ulPlayerKilledBy = MAXPLAYERS;

			if ( source && source->player )
			{
				if ( source->player == player )
					player->pSkullBot->PostEvent( BOTEVENT_KILLED_BYSELF );
				else if (( source->player - players ) == static_cast<int> (player->pSkullBot->m_ulPlayerEnemy) )
					player->pSkullBot->PostEvent( BOTEVENT_KILLED_BYENEMY );
				else
					player->pSkullBot->PostEvent( BOTEVENT_KILLED_BYPLAYER );
			}
			else
				player->pSkullBot->PostEvent( BOTEVENT_KILLED_BYENVIORNMENT );
		}

		// Death script execution, care of Skull Tag
		FBehavior::StaticStartTypedScripts (SCRIPT_Death, this, true);

		// [EP] Avoid instant body disappearing if the player had no lives left.
		bool bNoMoreLivesLeft = (( GAMEMODE_ShouldPlayerLoseLife( )) && ( player->ulLivesLeft == 0 ));

		// [RH] Force a delay between death and respawn
		if ((( zacompatflags & ZACOMPATF_INSTANTRESPAWN ) == false ) ||
			( player->bSpawnTelefragged ) || ( bNoMoreLivesLeft ))
		{
			float fRespawnDelayTime = 1.0f;

			// [AK] The respawn delay can be adjusted if the player wasn't spawn telefragged and still has lives left.
			// Don't use this in singleplayer games, or during countdown sequences.
			if (( NETWORK_GetState( ) != NETSTATE_SINGLE ) && ( GAMEMODE_IsGameInCountdown( ) == false ) &&
				( player->bSpawnTelefragged == false ) && ( bNoMoreLivesLeft == false ))
			{
				player->respawn_time = level.time + static_cast<int>( sv_respawndelaytime * TICRATE );
				fRespawnDelayTime = sv_respawndelaytime;
			}
			else
			{
				player->respawn_time = level.time + TICRATE;
			}

			// [AK] Show how long we must wait until we can respawn on the screen.
			// Don't display the timer at all in singleplayer games.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVERCOMMANDS_SetLocalPlayerRespawnDelayTime( player - players );
			else if (( NETWORK_GetState( ) != NETSTATE_SINGLE ) && ( player - players == consoleplayer ))
				HUD_SetRespawnTimeLeft( fRespawnDelayTime );

			// [BC] Don't respawn quite so fast on forced respawn. It sounds weird when your
			// scream isn't completed.
			// [RK] We can add on a custom force respawn delay instead drawn from the forcerespawn time CVAR
			// [AK] The forced respawn time shouldn't apply to players who don't have any lives left.
			if (( dmflags & DF_FORCE_RESPAWN ) && ( bNoMoreLivesLeft == false ))
				player->respawn_time += ( sv_forcerespawntime == 0 ? TICRATE/2 : sv_forcerespawntime * TICRATE );
		}

		// [RK] When instant respawn and force respawn are on, use sv_forcerespawntime to set the player's respawn time
		if ( zacompatflags & ZACOMPATF_INSTANTRESPAWN && dmflags & DF_FORCE_RESPAWN )
		{
			player->respawn_time = level.time + (sv_forcerespawntime * TICRATE);
		}

		// count environment kills against you
		if (!source)
		{
			PLAYER_SetFragcount( player, player->fragcount - (( bPossessedTerminatorArtifact ) ? 10 : 1 ), true, true );	// [RH] Cumulative frag count
		}
						
		// [BC] Increment team deathcount.
		if (( GAMEMODE_GetCurrentFlags() & GMF_PLAYERSONTEAMS ) && ( player->bOnTeam ))
			TEAM_SetDeathCount( player->Team, TEAM_GetDeathCount( player->Team ) + 1 );
	}

	// [BC] We can do this stuff in client mode.
	if (player)
	{
		flags &= ~MF_SOLID;
		player->playerstate = PST_DEAD;

		P_DropWeapon (player);

		if (this == players[consoleplayer].camera && automapactive)
		{
			// don't die in auto map, switch view prior to dying
			AM_Stop ();
		}

		// [GRB] Clear extralight. When you killed yourself with weapon that
		// called A_Light1/2 before it called A_Light0, extraligh remained.
		player->extralight = 0;
	}

	// [RH] If this is the unmorphed version of another monster, destroy this
	// actor, because the morphed version is the one that will stick around in
	// the level.
	if (flags & MF_UNMORPHED)
	{
		Destroy ();
		return;
	}

	// [BC] Tell clients that this thing died.
	// [BC] We need to move this block a little higher, otherwise things can be destroyed
	// before we tell the client to kill them.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
	{
		// If there isn't a player attached to this object, treat it like a normal thing.
		if ( player == NULL )
			SERVERCOMMANDS_KillThing( this, source, inflictor );
		else
			SERVERCOMMANDS_KillPlayer( ULONG( player - players ), source, inflictor, MeansOfDeath );
	}

	FState *diestate = NULL;
	int gibhealth = GibHealth();
	int iflags4 = inflictor == NULL ? 0 : inflictor->flags4;
	bool extremelydead = ((health < gibhealth || iflags4 & MF4_EXTREMEDEATH) && !(iflags4 & MF4_NOEXTREMEDEATH));

	// Special check for 'extreme' damage type to ensure that it gets recorded properly as an extreme death for subsequent checks.
	if (DamageType == NAME_Extreme)
	{
		extremelydead = true;
		DamageType = NAME_None;
	}

	// find the appropriate death state. The order is:
	//
	// 1. If damagetype is not 'none' and death is extreme, try a damage type specific extreme death state
	// 2. If no such state is found or death is not extreme try a damage type specific normal death state
	// 3. If damagetype is 'ice' and actor is a monster or player, try the generic freeze death (unless prohibited)
	// 4. If no state has been found and death is extreme, try the extreme death state
	// 5. If no such state is found or death is not extreme try the regular death state.
	// 6. If still no state has been found, destroy the actor immediately.

	if (DamageType != NAME_None)
	{
		if (extremelydead)
		{
			FName labels[] = { NAME_Death, NAME_Extreme, DamageType };
			diestate = FindState(3, labels, true);
		}
		if (diestate == NULL)
		{
			diestate = FindState (NAME_Death, DamageType, true);
			if (diestate != NULL) extremelydead = false;
		}
		if (diestate == NULL)
		{
			if (DamageType == NAME_Ice)
			{ // If an actor doesn't have an ice death, we can still give them a generic one.

				if (!deh.NoAutofreeze && !(flags4 & MF4_NOICEDEATH) && (player || (flags3 & MF3_ISMONSTER)))
				{
					diestate = FindState(NAME_GenericFreezeDeath);
					extremelydead = false;
				}
			}
		}
	}
	if (diestate == NULL)
	{
		
		// Don't pass on a damage type this actor cannot handle.
		// (most importantly, prevent barrels from passing on ice damage.)
		// Massacre must be preserved though.
		if (DamageType != NAME_Massacre)
		{
			DamageType = NAME_None;	
		}

		if (extremelydead)
		{ // Extreme death
			diestate = FindState (NAME_Death, NAME_Extreme, true);
		}
		if (diestate == NULL)
		{ // Normal death
			extremelydead = false;
			diestate = FindState (NAME_Death);
		}
	}

	if (extremelydead)
	{ 
		// We'll only get here if an actual extreme death state was used.

		// For players, mark the appropriate flag.
		if (player != NULL)
		{
			player->cheats |= CF_EXTREMELYDEAD;
		}
		// If a non-player, mark as extremely dead for the crash state.
		else if (health >= gibhealth)
		{
			health = gibhealth - 1;
		}
	}

	if (diestate != NULL)
	{
		SetState (diestate);

		if (tics > 1)
		{
			tics -= pr_killmobj() & 3;
			if (tics < 1)
				tics = 1;
		}
	}
	else
	{
		Destroy();
	}

	// [RH] Death messages
	if (( player ) && ( NETWORK_InClientMode() == false ))
	{
		// [AK] Try to draw a large frag message if the player (was) fragged (by) another player.
		HUD_PrepareToDrawFragMessage( player, source, MeansOfDeath );
		ClientObituary (this, inflictor, source, dmgflags, MeansOfDeath);
	}

}




//---------------------------------------------------------------------------
//
// PROC P_AutoUseHealth
//
//---------------------------------------------------------------------------
static int CountHealth(TArray<AInventory *> &Items)
{
	int counted = 0;
	for(unsigned i = 0; i < Items.Size(); i++)
	{
		counted += Items[i]->Amount * Items[i]->health;
	}
	return counted;
}

static int UseHealthItems(TArray<AInventory *> &Items, int &saveHealth)
{
	int saved = 0;

	while (Items.Size() > 0 && saveHealth > 0)
	{
		int maxhealth = 0;
		int index = -1;

		// Find the largest item in the list
		for(unsigned i = 0; i < Items.Size(); i++)
		{
			if (Items[i]->health > maxhealth)
			{
				index = i;
				maxhealth = Items[i]->health;
			}
		}

		// [BB] Keep track of some values to inform the client how many items were used.
		int oldAmount = Items[index]->Amount;
		int newAmount = oldAmount;
		const PClass *itemClass = Items[index]->GetClass();
		ULONG ulPlayer = MAXPLAYERS;
		if ( Items[index]->Owner && Items[index]->Owner->player )
			ulPlayer = static_cast<ULONG>(Items[index]->Owner->player - players);

		// Now apply the health items, using the same logic as Heretic and Hexen.
		int count = (saveHealth + maxhealth-1) / maxhealth;
		for(int i = 0; i < count; i++)
		{
			saved += maxhealth;
			saveHealth -= maxhealth;

			// [BB] Update newAmount separately from Items[index]->Amount. If the item is depleted
			// we can't access Items[index]->Amount after the i-loop.
			--newAmount;

			if (--Items[index]->Amount == 0)
			{
				if (!(Items[index]->ItemFlags & IF_KEEPDEPLETED))
				{
					Items[index]->Destroy ();
				}
				Items.Delete(index);
				break;
			}
		}

		// [BB] Inform the client about ths used items. 
		// Note: SERVERCOMMANDS_TakeInventory checks the validity of the ulPlayer value.
		if ( ( NETWORK_GetState( ) == NETSTATE_SERVER ) && ( newAmount != oldAmount ) )
			SERVERCOMMANDS_TakeInventory ( ulPlayer, itemClass, newAmount );
	}
	return saved;
}

void P_AutoUseHealth(player_t *player, int saveHealth)
{
	TArray<AInventory *> NormalHealthItems;
	TArray<AInventory *> LargeHealthItems;

	for(AInventory *inv = player->mo->Inventory; inv != NULL; inv = inv->Inventory)
	{
		if (inv->Amount > 0 && inv->IsKindOf(RUNTIME_CLASS(AHealthPickup)))
		{
			int mode = static_cast<AHealthPickup*>(inv)->autousemode;

			if (mode == 1) NormalHealthItems.Push(inv);
			else if (mode == 2) LargeHealthItems.Push(inv);
		}
	}

	int normalhealth = CountHealth(NormalHealthItems);
	int largehealth = CountHealth(LargeHealthItems);

	bool skilluse = !!G_SkillProperty(SKILLP_AutoUseHealth);

	if (skilluse && normalhealth >= saveHealth)
	{ // Use quartz flasks
		player->health += UseHealthItems(NormalHealthItems, saveHealth);
	}
	else if (largehealth >= saveHealth)
	{ 
		// Use mystic urns
		player->health += UseHealthItems(LargeHealthItems, saveHealth);
	}
	else if (skilluse && normalhealth + largehealth >= saveHealth)
	{ // Use mystic urns and quartz flasks
		player->health += UseHealthItems(NormalHealthItems, saveHealth);
		if (saveHealth > 0) player->health += UseHealthItems(LargeHealthItems, saveHealth);
	}
	player->mo->health = player->health;
}

//============================================================================
//
// P_AutoUseStrifeHealth
//
//============================================================================
CVAR(Bool, sv_disableautohealth, false, CVAR_ARCHIVE|CVAR_SERVERINFO)

void P_AutoUseStrifeHealth (player_t *player)
{
	TArray<AInventory *> Items;

	for(AInventory *inv = player->mo->Inventory; inv != NULL; inv = inv->Inventory)
	{
		if (inv->Amount > 0 && inv->IsKindOf(RUNTIME_CLASS(AHealthPickup)))
		{
			int mode = static_cast<AHealthPickup*>(inv)->autousemode;

			if (mode == 3) Items.Push(inv);
		}
	}

	if (!sv_disableautohealth)
	{
		while (Items.Size() > 0)
		{
			int maxhealth = 0;
			int index = -1;

			// Find the largest item in the list
			for(unsigned i = 0; i < Items.Size(); i++)
			{
				if (Items[i]->health > maxhealth)
				{
					index = i;
					maxhealth = Items[i]->Amount;
				}
			}

			while (player->health < 50)
			{
				if (!player->mo->UseInventory (Items[index]))
					break;
			}
			if (player->health >= 50) return;
			// Using all of this item was not enough so delete it and restart with the next best one
			Items.Delete(index);
		}
	}
}

/*
=================
=
= P_DamageMobj
=
= Damages both enemies and players
= inflictor is the thing that caused the damage
= 		creature or missile, can be NULL (slime, etc)
= source is the thing to target after taking damage
=		creature or NULL
= Source and inflictor are the same for melee attacks
= source can be null for barrel explosions and other environmental stuff
==================
*/

// [TIHan/Spleen] Factor for damage dealt to players by monsters.
CUSTOM_CVAR (Float, sv_coop_damagefactor, 1.0f, CVAR_SERVERINFO | CVAR_GAMEPLAYSETTING)
{
	if (self <= 0)
		self = 1.0f;

	// [AK] Notify the clients about the change and update the server console.
	SERVER_SettingChanged( self, true, 2 );
}

// [TIHan/Spleen] Apply factor for damage dealt to players by monsters.
void ApplyCoopDamagefactor(int &damage, AActor *source)
{
	if ((sv_coop_damagefactor != 1.0f) && (source != NULL) && (source->flags3 & MF3_ISMONSTER))
		damage = int(damage * sv_coop_damagefactor);
}

static inline bool MustForcePain(AActor *target, AActor *inflictor)
{
	return (!(target->flags5 & MF5_NOPAIN) && inflictor != NULL &&
		(inflictor->flags6 & MF6_FORCEPAIN) && !(inflictor->flags5 & MF5_PAINLESS));
}

// Returns the amount of damage actually inflicted upon the target, or -1 if
// the damage was cancelled.
int P_DamageMobj (AActor *target, AActor *inflictor, AActor *source, int damage, FName mod, int flags)
{
	unsigned ang;
	player_t *player = NULL;
	fixed_t thrust;
	int temp;
	int painchance = 0;
	FState * woundstate = NULL;
	PainChanceList * pc = NULL;
	bool justhit = false;
	// [BC]
	LONG	lOldTargetHealth;

	// [BC] Game is currently in a suspended state; don't hurt anyone.
	if ( GAME_GetEndLevelDelay( ))
		return -1;

	if (target == NULL || !((target->flags & MF_SHOOTABLE) || (target->flags6 & MF6_VULNERABLE)))
	{ // Shouldn't happen
		return -1;
	}

	// [BB] For the time being, unassigned voodoo dolls can't be damaged.
	// [RK] But we should thrust them about if they're being used to trigger line actions.
	if ( target->player == COOP_GetVoodooDollDummyPlayer() )
		if ( source && inflictor )
			goto thrust;
		else
			return -1;

	// Spectral targets only take damage from spectral projectiles.
	if (target->flags4 & MF4_SPECTRAL && damage < TELEFRAG_DAMAGE)
	{
		if (inflictor == NULL || !(inflictor->flags4 & MF4_SPECTRAL))
		{
			return -1;
		}
	}
	if (target->health <= 0)
	{
		if (inflictor && mod == NAME_Ice)
		{
			return -1;
		}
		else if (target->flags & MF_ICECORPSE) // frozen
		{
			target->tics = 1;
			target->flags6 |= MF6_SHATTERING;
			target->velx = target->vely = target->velz = 0;

			// [BC] If we're the server, tell clients to update this thing's tics and
			// velocity.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			{
				SERVERCOMMANDS_SetThingTics( target );
				SERVERCOMMANDS_MoveThing( target, CM_VELX|CM_VELY|CM_VELZ );
			}
		}
		return -1;
	}
	if ((target->flags2 & MF2_INVULNERABLE) && damage < TELEFRAG_DAMAGE && !(flags & DMG_FORCED))
	{ // actor is invulnerable
		if (target->player == NULL)
		{
			if (inflictor == NULL || (!(inflictor->flags3 & MF3_FOILINVUL) && !(flags & DMG_FOILINVUL)))
			{
				return -1;
			}
		}
		else
		{
			// Players are optionally excluded from getting thrust by damage.
			if (static_cast<APlayerPawn *>(target)->PlayerFlags & PPF_NOTHRUSTWHENINVUL)
			{
				return -1;
			}
		}
		
	}
	if (inflictor != NULL)
	{
		if (inflictor->flags5 & MF5_PIERCEARMOR)
			flags |= DMG_NO_ARMOR;
	}
	
	MeansOfDeath = mod;
	FriendlyFire = false;
	// [RH] Andy Baker's Stealth monsters
	if (target->flags & MF_STEALTH)
	{
		target->alpha = OPAQUE;
		target->visdir = -1;

		// [TP] If we're the server, tell clients to flash this stealth monster
		if ( NETWORK_GetState() == NETSTATE_SERVER )
			SERVERCOMMANDS_FlashStealthMonster( target, target->visdir );
	}
	// [BB] The clients may not do this.
	if ( (target->flags & MF_SKULLFLY)
	     && ( NETWORK_InClientMode() == false ) )
	{
		target->velx = target->vely = target->velz = 0;

		// [BC] If we're the server, tell clients to update this thing's velocity
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			SERVERCOMMANDS_MoveThing( target, CM_VELX|CM_VELY|CM_VELZ );
	}
	if (!(flags & DMG_FORCED))	// DMG_FORCED skips all special damage checks
	{
		if (target->flags2 & MF2_DORMANT)
		{
			// Invulnerable, and won't wake up
			return -1;
		}
		player = target->player;
		if (player && damage > 1 && damage < TELEFRAG_DAMAGE)
		{
			// Take half damage in trainer mode
			damage = FixedMul(damage, G_SkillProperty(SKILLP_DamageFactor));
		}
		// Special damage types
		if (inflictor)
		{
			if (inflictor->flags4 & MF4_SPECTRAL)
			{
				if (player != NULL)
				{
					if (!deathmatch && inflictor->FriendPlayer > 0)
						return -1;
				}
				else if (target->flags4 & MF4_SPECTRAL)
				{
					if (inflictor->FriendPlayer == 0 && !target->IsHostile(inflictor))
						return -1;
				}
			}

			damage = inflictor->DoSpecialDamage (target, damage, mod);
			if (damage == -1)
			{
				return -1;
			}
		}
		// Handle active damage modifiers (e.g. PowerDamage)
		if (source != NULL && source->Inventory != NULL)
		{
			int olddam = damage;
			source->Inventory->ModifyDamage(olddam, mod, damage, false);
			if (olddam != damage && damage <= 0)
			{ // Still allow FORCEPAIN
				if (MustForcePain(target, inflictor))
				{
					goto dopain;
				}
				return -1;
			}
		}
		// Handle passive damage modifiers (e.g. PowerProtection)
		if (target->Inventory != NULL)
		{
			int olddam = damage;
			target->Inventory->ModifyDamage(olddam, mod, damage, true);
			if (olddam != damage && damage <= 0)
			{ // Still allow FORCEPAIN
				if (MustForcePain(target, inflictor))
				{
					goto dopain;
				}
				return -1;
			}
		}

		// [Dusk] Unblocked players don't telefrag each other, they
		// just pass through each other.
		// [BB] Voodoo dolls still telefrag.
		if ( P_CheckUnblock ( source, target )
			&& ( source->player )
			&& ( source->player->mo == source )
			&& ( target->player )
			&& ( target->player->mo == target )
			&& ( mod == NAME_Telefrag || mod == NAME_SpawnTelefrag ))
		{
			return -1;
		}

		if (!(flags & DMG_NO_FACTOR))
		{
			damage = FixedMul(damage, target->DamageFactor);
			if (damage >= 0)
			{
				damage = DamageTypeDefinition::ApplyMobjDamageFactor(damage, mod, target->GetClass()->ActorInfo->DamageFactors);
			}
			if (damage <= 0)
			{ // Still allow FORCEPAIN
				if (MustForcePain(target, inflictor))
				{
					goto dopain;
				}
				return -1;
			}
		}

		damage = target->TakeSpecialDamage (inflictor, source, damage, mod);
	}
	if (damage == -1)
	{
		return -1;
	}

	// [BC] If the target player has the reflection rune, damage the source with 50% of the
	// this player is being damaged with.
	if (( target->player ) &&
		( target->player->cheats & CF_REFLECTION ) &&
		( source ) &&
		( mod != NAME_Reflection ) &&
		( NETWORK_InClientMode() == false ))
	{
		if ( target != source )
		{
			P_DamageMobj( source, NULL, target, (( damage * 3 ) / 4 ), NAME_Reflection );

			// Reset means of death flag.
			MeansOfDeath = mod;
		}
	}
// [RK] This label is for voodoo dolls online to be pushed since we aren't damaging them.
thrust:
	// Push the target unless the source's weapon's kickback is 0.
	// (i.e. Gauntlets/Chainsaw)
	// [BB] The server handles this.
	// [AK] Don't push teammates if ZADF_DONT_PUSH_ALLIES is enabled.
	if (inflictor && inflictor != target	// [RH] Not if hurting own self
		&& !(target->flags & MF_NOCLIP)
		&& !(inflictor->flags2 & MF2_NODMGTHRUST)
		&& !(flags & DMG_THRUSTLESS)
		&& (source == NULL || source->player == NULL || !(source->flags2 & MF2_NODMGTHRUST))
		&& (( PLAYER_CannotAffectAllyWith( source, target, inflictor, ZADF_DONT_PUSH_ALLIES ) == false ) || ( target->player == COOP_GetVoodooDollDummyPlayer() )) // [RK] Dolls need to be pushed.
		&& ( NETWORK_InClientMode() == false ) )
	{
		int kickback;

		if (inflictor && inflictor->projectileKickback)
			kickback = inflictor->projectileKickback;
		else if (!source || !source->player || !source->player->ReadyWeapon)
			kickback = gameinfo.defKickback;
		else
			kickback = source->player->ReadyWeapon->Kickback;

		if (kickback)
		{
			// [BB] Safe the original z-velocity of the target. This way we can check if we need to update it.
			const fixed_t oldTargetVelz = target->velz;

			AActor *origin = (source && (flags & DMG_INFLICTOR_IS_PUFF))? source : inflictor;

			// If the origin and target are in exactly the same spot, choose a random direction.
			// (Most likely cause is from telefragging somebody during spawning because they
			// haven't moved from their spawn spot at all.)
			if (origin->x == target->x && origin->y == target->y)
			{
				ang = pr_kickbackdir.GenRand32();
			}
			else
			{
				ang = R_PointToAngle2 (origin->x, origin->y, target->x, target->y);
			}

			// Calculate this as float to avoid overflows so that the
			// clamping that had to be done here can be removed.
            double fltthrust;

            fltthrust = mod == NAME_MDK ? 10 : 32;
            if (target->Mass > 0)
            {
                fltthrust = clamp((damage * 0.125 * kickback) / target->Mass, 0., fltthrust);
            }

			thrust = FLOAT2FIXED(fltthrust);

			// Don't apply ultra-small damage thrust
			if (thrust < FRACUNIT/100) thrust = 0;

			// make fall forwards sometimes
			if ((damage < 40) && (damage > target->health)
				 && (target->z - origin->z > 64*FRACUNIT)
				 && (pr_damagemobj()&1)
				 // [RH] But only if not too fast and not flying
				 && thrust < 10*FRACUNIT
				 && !(target->flags & MF_NOGRAVITY)
				 && (inflictor == NULL || !(inflictor->flags5 & MF5_NOFORWARDFALL))
				 )
			{
				ang += ANG180;
				thrust *= 4;
			}
			ang >>= ANGLETOFINESHIFT;
			if (source && source->player && (flags & DMG_INFLICTOR_IS_PUFF)
				&& source->player->ReadyWeapon != NULL &&
				(source->player->ReadyWeapon->WeaponFlags & WIF_STAFF2_KICKBACK))
			{
				// Staff power level 2
				target->velx += FixedMul (10*FRACUNIT, finecosine[ang]);
				target->vely += FixedMul (10*FRACUNIT, finesine[ang]);
				if (!(target->flags & MF_NOGRAVITY))
				{
					target->velz += 5*FRACUNIT;
				}
			}
			else
			{
				target->velx += FixedMul (thrust, finecosine[ang]);
				target->vely += FixedMul (thrust, finesine[ang]);
			}

			// [BC] Set the thing's velocity.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			{
				// [BB] Only update z-velocity if it has changed.
				SERVER_UpdateThingVelocity ( target, oldTargetVelz != target->velz );
			}
		}
	}
	// [RK] This is all we need to do for voodoo dolls.
	if ( target->player == COOP_GetVoodooDollDummyPlayer() )
		return -1;

	// [RH] Avoid friendly fire if enabled
	if (!(flags & DMG_FORCED) && source != NULL &&
		((player && player != source->player) || (!player && target != source)) &&
		((target->IsTeammate (source)) || ( target->IsFriend(source) && (zadmflags & ZADF_SHOOT_THROUGH_ALLIES)))) // [RK] Treat allied monsters like teammates with shoot through.
	{
		// [BL] Some adjustments for Skulltag
		if (player && (( teamlms || survival ) && ( MeansOfDeath == NAME_SpawnTelefrag )) == false )
			FriendlyFire = true;
		if (damage < TELEFRAG_DAMAGE)
		{ // Still allow telefragging :-(
			damage = (int)((float)damage * level.teamdamage);
			if (damage <= 0)
				return damage;
		}
	}

	//
	// player specific
	//
	// [BC]
	lOldTargetHealth = target->health;
	if (player)
	{
		bool bDamageEventHandled = false; // [AK]

		// [TIHan/Spleen] Apply factor for damage dealt to players by monsters.
		ApplyCoopDamagefactor(damage, source);

		// end of game hell hack
		if ((target->Sector->special & 255) == dDamage_End
			&& damage >= target->health
			// [BB] A player who tries to exit a map in a competitive game mode when DF_NO_EXIT is set,
			// should not be saved by the hack, but killed.
			&& MeansOfDeath != NAME_Exit)
		{
			damage = target->health - 1;
		}

		if (!(flags & DMG_FORCED))
		{
			// check the real player, not a voodoo doll here for invulnerability effects
			if (damage < TELEFRAG_DAMAGE && ((player->mo->flags2 & MF2_INVULNERABLE) ||
				(player->cheats & CF_GODMODE)))
			{ // player is invulnerable, so don't hurt him
				return -1;
			}

			// [AK] Trigger an event script indicating that the player has taken damage before any damage
			// can be absorbed by their armor. If the event returns 0, don't do anything else.
			if (GAMEMODE_HandleDamageEvent(target, inflictor, source, damage, mod, true) == false)
				return -1;

			if (!(flags & DMG_NO_ARMOR) && player->mo->Inventory != NULL)
			{
				int newdam = damage;
				player->mo->Inventory->AbsorbDamage (damage, mod, newdam);
				damage = newdam;
				if (damage <= 0)
				{
					// [BB] The player didn't lose health but armor. The server needs
					// to tell the client about this.
					if ( NETWORK_GetState( ) == NETSTATE_SERVER )
						SERVERCOMMANDS_SetPlayerArmor( player - players );

					// If MF6_FORCEPAIN is set, make the player enter the pain state.
					if (!(target->flags5 & MF5_NOPAIN) && inflictor != NULL &&
						(inflictor->flags6 & MF6_FORCEPAIN) && !(inflictor->flags5 & MF5_PAINLESS))
					{
						goto dopain;
					}
					return damage;
				}
			}
			
			// [AK] Trigger an event script indicating that the player has taken damage, if we can.
			// If the event returns 0, then the target doesn't take damage and we do nothing.
			if ( GAMEMODE_HandleDamageEvent( target, inflictor, source, damage, mod ) == false )
				return -1;

			bDamageEventHandled = true;

			if (damage >= player->health
				&& (G_SkillProperty(SKILLP_AutoUseHealth) || deathmatch)
				&& !player->morphTics)
			{ // Try to use some inventory health
				P_AutoUseHealth (player, damage - player->health + 1);
			}
		}

		// [AK] If we haven't done so already, trigger an event script indicating that the player has taken damage.
		// If the event returns 0, then the target doesn't take damage and we do nothing.
		if (( bDamageEventHandled == false ) && ( GAMEMODE_HandleDamageEvent( target, inflictor, source, damage, mod ) == false ))
			return -1;

		player->health -= damage;		// mirror mobj health here for Dave
		// [RH] Make voodoo dolls and real players record the same health
		target->health = player->mo->health -= damage;
		if (player->health < 50 && !deathmatch && !(flags & DMG_FORCED))
		{
			P_AutoUseStrifeHealth (player);
			player->mo->health = player->health;
		}
		if (player->health <= 0)
		{
			// [SP] Buddha cheat: if the player is about to die, rescue him to 1 health.
			// This does not save the player if damage >= TELEFRAG_DAMAGE, still need to
			// telefrag him right? ;) (Unfortunately the damage is "absorbed" by armor,
			// but telefragging should still do enough damage to kill the player)
			if ((player->cheats & CF_BUDDHA) && damage < TELEFRAG_DAMAGE)
			{
				// If this is a voodoo doll we need to handle the real player as well.
				player->mo->health = target->health = player->health = 1;
			}
			else
			{
				player->health = 0;
			}
		}
		player->LastDamageType = mod;

		if ( player->pSkullBot )
		{
			// Tell the bot he's been damaged by a player.
			if ( source && source->player && ( source->player != player ))
			{
				player->pSkullBot->m_ulLastPlayerDamagedBy = source->player - players;
				player->pSkullBot->PostEvent( BOTEVENT_DAMAGEDBY_PLAYER );
			}
		}

		// [AK] In case blood_fade_usemaxhealth is enabled and we want to scale the intensity
		// of the blood based on the player's max health, we scale the incoming damage using
		// the max health. By default, the damagecount is based on a max health of 100.
		int oldDamage = damage;
		PLAYER_ScaleDamageCountWithMaxHealth( player, damage );

		player->attacker = source;
		player->damagecount += damage;	// add damage after armor / invuln
		if (player->damagecount > 100)
		{
			player->damagecount = 100;	// teleport stomp does 10k points...
		}
		if ( player->damagecount < 0 )
			player->damagecount = 0;
		temp = damage < 100 ? damage : 100;
		if (player == &players[consoleplayer])
		{
			I_Tactile (40,10,40+temp*2);
		}

		// [AK] Restore the old damage value, in case it was modified above.
		damage = oldDamage;
	}
	else
	{
		// [AK] Trigger an event script indicating that the actor has taken damage before any damage
		// can be absorbed by their armor. If the event returns 0, don't do anything else.
		if (GAMEMODE_HandleDamageEvent(target, inflictor, source, damage, mod, true) == false)
			return -1;

		// Armor for monsters.
		if (!(flags & (DMG_NO_ARMOR|DMG_FORCED)) && target->Inventory != NULL && damage > 0)
		{
			int newdam = damage;
			target->Inventory->AbsorbDamage (damage, mod, newdam);
			damage = newdam;
			if (damage <= 0)
			{
				return damage;
			}
		}
	
		// [AK] Trigger an event script indicating that the target actor has taken damage, if we can.
		// If the event returns 0, then the target doesn't take damage and we do nothing.
		if ( GAMEMODE_HandleDamageEvent( target, inflictor, source, damage, mod ) == false )
			return -1;

		target->health -= damage;	
	}

	//
	// the damage has been dealt; now deal with the consequences
	//
	target->DamageTypeReceived = mod;

	// If the damaging player has the power of drain, give the player 50% of the damage
	// done in health.
	if ( source && source->player && source->player->cheats & CF_DRAIN && !(target->flags5 & MF5_DONTDRAIN) &&
		( NETWORK_InClientMode() == false ))
	{
		if (( target->player == NULL ) || ( target->player != source->player ))
		{
			if ( P_GiveBody( source, MIN( (int)lOldTargetHealth, damage ) / 2 ))
			{
				// [BC] If we're the server, send out the health change.
				if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				{
					SERVERCOMMANDS_SetPlayerHealth( source->player - players );
				}

				S_Sound( source, CHAN_ITEM, "misc/i_pkup", 1, ATTN_NORM, true );	// [BC] Inform the clients.
			}
		}
	}

	// [BB] Save the damage the player has dealt to monsters here, it's only converted to points
	// though if ZADF_AWARD_DAMAGE_INSTEAD_KILLS is set.
	// [TP] Only award damage dealt to monsters with COUNTKILL set.
	if ( source != NULL
		&& ( source->player != NULL )
		&& ( target->player == NULL )
		&& ( NETWORK_InClientMode() == false )
		&& ( target->flags & MF_COUNTKILL ))
	{
		source->player->ulUnrewardedDamageDealt += MIN( (int)lOldTargetHealth, damage );
	}

	// [BC] Tell clients that this thing was damaged.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
	{
		if ( player )
			SERVERCOMMANDS_DamagePlayer( ULONG( player - players ));
	}

	if (target->health <= 0)
	{ // Death
		target->special1 = damage;

		// use inflictor's death type if it got one.
		if (inflictor && inflictor->DeathType != NAME_None) mod = inflictor->DeathType;

		// check for special fire damage or ice damage deaths
		if (mod == NAME_Fire)
		{
			if (player && !player->morphTics)
			{ // Check for flame death
				if (!inflictor ||
					((target->health > -50) && (damage > 25)) ||
					!(inflictor->flags5 & MF5_SPECIALFIREDAMAGE))
				{
					target->DamageType = NAME_Fire;
				}
			}
			else
			{
				target->DamageType = NAME_Fire;
			}
		}
		else
		{
			target->DamageType = mod;
		}
		if (source && source->tracer && (source->flags5 & MF5_SUMMONEDMONSTER))
		{ // Minotaur's kills go to his master
			// Make sure still alive and not a pointer to fighter head
			if (source->tracer->player && (source->tracer->player->mo == source->tracer))
			{
				source = source->tracer;
			}
		}
		// kgKILL start
		else if ( source && source->FriendPlayer ) {
			const player_t *pl = &players[source->FriendPlayer - 1];
			if (pl) source = pl->mo;
		}
		// kgKILL end

		// Deaths are server side.
		if ( NETWORK_InClientMode() == false )
		{
			target->Die (source, inflictor, flags);
		}
		return damage;
	}

	woundstate = target->FindState(NAME_Wound, mod);
	// [BB] The server takes care of this.
	if ( (woundstate != NULL) && ( NETWORK_InClientMode() == false ) )
	{
		int woundhealth = RUNTIME_TYPE(target)->Meta.GetMetaInt (AMETA_WoundHealth, 6);

		if (target->health <= woundhealth)
		{
			// [Dusk] As the server, update the clients on the state
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVERCOMMANDS_SetThingFrame( target, woundstate );

			target->SetState (woundstate);
			return damage;
		}
	}

	
	if (!(target->flags5 & MF5_NOPAIN) && (inflictor == NULL || !(inflictor->flags5 & MF5_PAINLESS)) &&
		(target->player != NULL || !G_SkillProperty(SKILLP_NoPain)) && !(target->flags & MF_SKULLFLY))
	{
		pc = target->GetClass()->ActorInfo->PainChances;
		painchance = target->PainChance;
		if (pc != NULL)
		{
			int *ppc = pc->CheckKey(mod);
			if (ppc != NULL)
			{
				painchance = *ppc;
			}
		}

		if (((damage >= target->PainThreshold && pr_damagemobj() < painchance) ||
			(inflictor != NULL && (inflictor->flags6 & MF6_FORCEPAIN))) &&
			( NETWORK_InClientMode() == false ))
		{
dopain:	
			if (mod == NAME_Electric)
			{
				if (pr_lightning() < 96)
				{
					justhit = true;
					FState *painstate = target->FindState(NAME_Pain, mod);
					if (painstate != NULL)
					{
						// If we are the server, tell clients about the state change.
						if ( NETWORK_GetState( ) == NETSTATE_SERVER )
							SERVERCOMMANDS_SetThingFrame( target, painstate );

						target->SetState (painstate);
					}
				}
				else
				{ // "electrocute" the target
					target->renderflags |= RF_FULLBRIGHT;
					if ((target->flags3 & MF3_ISMONSTER) && pr_lightning() < 128)
					{
						target->Howl ();
					}
				}
			}
			else
			{
				justhit = true;
				FState *painstate = target->FindState(NAME_Pain, ((inflictor && inflictor->PainType != NAME_None) ? inflictor->PainType : mod));
				if (painstate != NULL)
				{
					// If we are the server, tell clients about the state change.
					if ( NETWORK_GetState( ) == NETSTATE_SERVER )
						SERVERCOMMANDS_SetThingFrame( target, painstate );

					target->SetState (painstate);
				}
				if (mod == NAME_PoisonCloud)
				{
					if ((target->flags3 & MF3_ISMONSTER) && pr_poison() < 128)
					{
						target->Howl ();
					}
				}
			}
		}
	}

	// Nothing more to do!
	if ( NETWORK_InClientMode() )
		return -1;

	target->reactiontime = 0;			// we're awake now...	
	if (source)
	{
		if (source == target->target)
		{
			target->threshold = BASETHRESHOLD;
			if (target->state == target->SpawnState && target->SeeState != NULL)
			{
				// [BB] If we are the server, tell clients about the state change.
				if ( NETWORK_GetState( ) == NETSTATE_SERVER )
					SERVERCOMMANDS_SetThingState( target, STATE_SEE );

				target->SetState (target->SeeState);
			}
		}
		else if (source != target->target && target->OkayToSwitchTarget (source))
		{
			// Target actor is not intent on another actor,
			// so make him chase after source

			// killough 2/15/98: remember last enemy, to prevent
			// sleeping early; 2/21/98: Place priority on players

			if (target->lastenemy == NULL ||
				(target->lastenemy->player == NULL && target->TIDtoHate == 0) ||
				target->lastenemy->health <= 0)
			{
				target->lastenemy = target->target; // remember last enemy - killough
			}
			target->target = source;
			target->threshold = BASETHRESHOLD;
			if (target->state == target->SpawnState && target->SeeState != NULL)
			{
				// If we are the server, tell clients about the state change.
				if ( NETWORK_GetState( ) == NETSTATE_SERVER )
					SERVERCOMMANDS_SetThingState( target, STATE_SEE );

				target->SetState (target->SeeState);
			}
		}
	}

	// killough 11/98: Don't attack a friend, unless hit by that friend.
	if (justhit && (target->target == source || !target->target || !target->IsFriend(target->target)))
		target->flags |= MF_JUSTHIT;    // fight back!

	return damage;
}

void P_PoisonMobj (AActor *target, AActor *inflictor, AActor *source, int damage, int duration, int period, FName type)
{
	// Check for invulnerability.
	if (!(inflictor->flags6 & MF6_POISONALWAYS))
	{
		if (target->flags2 & MF2_INVULNERABLE)
		{ // actor is invulnerable
			if (target->player == NULL)
			{
				if (!(inflictor->flags3 & MF3_FOILINVUL))
				{
					return;
				}
			}
			else
			{
				return;
			}
		}
	}

	target->Poisoner = source;
	target->PoisonDamageTypeReceived = type;
	target->PoisonPeriodReceived = period;

	if (inflictor->flags6 & MF6_ADDITIVEPOISONDAMAGE)
	{
		target->PoisonDamageReceived += damage;
	}
	else
	{
		target->PoisonDamageReceived = damage;
	}

	if (inflictor->flags6 & MF6_ADDITIVEPOISONDURATION)
	{
		target->PoisonDurationReceived += duration;
	}
	else
	{
		target->PoisonDurationReceived = duration;
	}

}

bool AActor::OkayToSwitchTarget (AActor *other)
{
	if (other == this)
		return false;		// [RH] Don't hate self (can happen when shooting barrels)

	if (other->flags7 & MF7_NEVERTARGET)
		return false;		// never EVER target me!

	if (!(other->flags & MF_SHOOTABLE))
		return false;		// Don't attack things that can't be hurt

	if ((flags4 & MF4_NOTARGETSWITCH) && target != NULL)
		return false;		// Don't switch target if not allowed

	if ((master != NULL && other->IsA(master->GetClass())) ||		// don't attack your master (or others of its type)
		(other->master != NULL && IsA(other->master->GetClass())))	// don't attack your minion (or those of others of your type)
	{
		if (!IsHostile (other) &&								// allow target switch if other is considered hostile
			(other->tid != TIDtoHate || TIDtoHate == 0) &&		// or has the tid we hate
			other->TIDtoHate == TIDtoHate)						// or has different hate information
		{
			return false;
		}
	}

	if ((other->flags3 & MF3_NOTARGET) &&
		(other->tid != TIDtoHate || TIDtoHate == 0) &&
		!IsHostile (other))
		return false;
	if (threshold != 0 && !(flags4 & MF4_QUICKTORETALIATE))
		return false;
	if (IsFriend (other))
	{ // [RH] Friendlies don't target other friendlies
		return false;
	}
	
	int infight;
	if (flags5 & MF5_NOINFIGHTING) infight=-1;	
	else if (level.flags2 & LEVEL2_TOTALINFIGHTING) infight=1;
	else if (level.flags2 & LEVEL2_NOINFIGHTING) infight=-1;	
	else infight = infighting;
	
	// [BC] No infighting during invasion mode.
	if ((infight < 0 || invasion )&&	other->player == NULL && !IsHostile (other))
	{
		return false;	// infighting off: Non-friendlies don't target other non-friendlies
	}
	if (TIDtoHate != 0 && TIDtoHate == other->TIDtoHate)
		return false;		// [RH] Don't target "teammates"
	if (other->player != NULL && (flags4 & MF4_NOHATEPLAYERS))
		return false;		// [RH] Don't target players
	if (target != NULL && target->health > 0 &&
		TIDtoHate != 0 && target->tid == TIDtoHate && pr_switcher() < 128 &&
		P_CheckSight (this, target))
		return false;		// [RH] Don't be too quick to give up things we hate

	return true;
}

//==========================================================================
//
// P_PoisonPlayer - Sets up all data concerning poisoning
//
// poisoner is the object directly responsible for poisoning the player,
// such as a missile. source is the actor responsible for creating the
// poisoner.
//
//==========================================================================

bool P_PoisonPlayer (player_t *player, AActor *poisoner, AActor *source, int poison)
{
	// [BC] This is handled server side.
	if ( NETWORK_InClientMode() )
		return false;

	if((player->cheats&CF_GODMODE) || (player->mo->flags2 & MF2_INVULNERABLE))
	{
		return false;
	}
	if (source != NULL && source->player != player && player->mo->IsTeammate (source))
	{
		poison = (int)((float)poison * level.teamdamage);
	}
	if (poison > 0)
	{
		player->poisoncount += poison;
		player->poisoner = source;
		if (poisoner == NULL)
		{
			player->poisontype = player->poisonpaintype = NAME_None;
		}
		else
		{ // We need to record these in case the poisoner disappears before poisoncount reaches 0.
			player->poisontype = poisoner->DamageType;
			player->poisonpaintype = poisoner->PainType != NAME_None ? poisoner->PainType : poisoner->DamageType;
		}
		if(player->poisoncount > 100)
		{
			player->poisoncount = 100;
		}

		// [BC] Update the player's poisoncount.
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			SERVERCOMMANDS_SetPlayerPoisonCount( ULONG( player - players ));
	}
	return true;
}

//==========================================================================
//
// P_PoisonDamage - Similar to P_DamageMobj
//
//==========================================================================

void P_PoisonDamage (player_t *player, AActor *source, int damage,
	bool playPainSound)
{
	// [Dusk] clients shouldn't execute any of this
	if ( NETWORK_InClientMode() )
		return;

	AActor *target;
	AActor *inflictor;

	if (player == NULL)
	{
		return;
	}
	target = player->mo;
	inflictor = source;
	if (target->health <= 0)
	{
		return;
	}
	if (damage < TELEFRAG_DAMAGE && ((target->flags2 & MF2_INVULNERABLE) ||
		(player->cheats & CF_GODMODE)))
	{ // target is invulnerable
		return;
	}
	// Take half damage in trainer mode
	damage = FixedMul(damage, G_SkillProperty(SKILLP_DamageFactor));

		// [TIHan/Spleen] Apply factor for damage dealt to players by monsters.
		ApplyCoopDamagefactor(damage, source);
	// Handle passive damage modifiers (e.g. PowerProtection)
	if (target->Inventory != NULL)
	{
		target->Inventory->ModifyDamage(damage, player->poisontype, damage, true);
	}
	// Modify with damage factors
	damage = FixedMul(damage, target->DamageFactor);
	if (damage > 0)
	{
		damage = DamageTypeDefinition::ApplyMobjDamageFactor(damage, player->poisontype, target->GetClass()->ActorInfo->DamageFactors);
	}
	if (damage <= 0)
	{ // Damage was reduced to 0, so don't bother further.
		return;
	}

	// [AK] Trigger an event script indicating that the player has taken damage.
	// If the event returns 0, then the player doesn't take damage and we do nothing.
	if ( GAMEMODE_HandleDamageEvent( target, NULL, source, damage, player->poisontype ) == false )
		return;

	if (damage >= player->health
		&& (G_SkillProperty(SKILLP_AutoUseHealth) || deathmatch)
		&& !player->morphTics)
	{ // Try to use some inventory health
		P_AutoUseHealth(player, damage - player->health+1);
	}
	player->health -= damage; // mirror mobj health here for Dave
	if (player->health < 50 && !deathmatch)
	{
		P_AutoUseStrifeHealth(player);
	}
	if (player->health < 0)
	{
		player->health = 0;
	}
	player->attacker = source;

	// [Dusk] Update the player health
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVERCOMMANDS_SetPlayerHealth( player - players );

	//
	// do the damage
	//
	target->health -= damage;
	if (target->health <= 0)
	{ // Death
		if (player->cheats & CF_BUDDHA && damage < TELEFRAG_DAMAGE)
		{ // [SP] Save the player... 
			player->health = target->health = 1;
		}
		else
		{
			target->special1 = damage;
			if (player && !player->morphTics)
			{ // Check for flame death
				if ((player->poisontype == NAME_Fire) && (target->health > -50) && (damage > 25))
				{
					target->DamageType = NAME_Fire;
				}
				else
				{
					target->DamageType = player->poisontype;
				}
			}
			target->Die(source, source);
			return;
		}
	}
	if (!(level.time&63) && playPainSound)
	{
		FState *painstate = target->FindState(NAME_Pain, player->poisonpaintype);
		if (painstate != NULL)
		{
			// [BB] If we are the server, tell clients about the state change.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVERCOMMANDS_SetThingFrame( target, painstate );

			target->SetState(painstate);
		}
	}
/*
	if((P_Random() < target->info->painchance)
		&& !(target->flags&MF_SKULLFLY))
	{
		target->flags |= MF_JUSTHIT; // fight back!
		P_SetMobjState(target, target->info->painstate);
	}
*/
	return;
}


//*****************************************************************************
//
void PLAYER_SetFragcount( player_t *pPlayer, LONG lFragCount, bool bAnnounce, bool bUpdateTeamFrags )
{
	// Don't bother with fragcount during warm-ups.
	// [AK] Clients shouldn't need to check this.
	if (( NETWORK_InClientMode( ) == false ) && ( GAMEMODE_IsGameInCountdown( )))
		return;

	// Don't announce events related to frag changes during teamplay, LMS,
	// or possession games.
	if (( bAnnounce ) && ( GAMEMODE_GetCurrentFlags( ) & GMF_PLAYERSEARNFRAGS ) && !( GAMEMODE_GetCurrentFlags( ) & GMF_PLAYERSONTEAMS ))
	{
		// [AK] Clients must check a few more things if they are to play any frag sounds.
		// [BB] If we are still in the first tic of the level, we are receiving the frag count
		// as part of the full update (that is not considered as a snapshot after a "changemap"
		// map change). Thus don't announce anything in this case.
		if (( NETWORK_InClientMode( ) == false ) || (( CLIENT_GetConnectionState( ) == CTS_ACTIVE ) && ( GAMEMODE_IsGameInProgress( )) && ( level.time != 0 )))
			ANNOUNCER_PlayFragSounds( pPlayer - players, pPlayer->fragcount, lFragCount );
	}

	// If this is a teamplay deathmatch, update the team frags.
	if ( bUpdateTeamFrags )
	{
		if (( GAMEMODE_GetCurrentFlags() & GMF_PLAYERSONTEAMS ) && ( pPlayer->bOnTeam ))
			TEAM_SetFragCount( pPlayer->Team, TEAM_GetFragCount( pPlayer->Team ) + ( lFragCount - pPlayer->fragcount ), bAnnounce );
	}

	// Set the player's fragcount.
	pPlayer->fragcount = lFragCount;

	// If we're the server, notify the clients of the fragcount change.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
	{
		SERVERCOMMANDS_SetPlayerFrags( pPlayer - players );

		// Also, update the scoreboard.
		SERVERCONSOLE_UpdatePlayerInfo( pPlayer - players, UDF_FRAGS );
		SERVERCONSOLE_UpdateScoreboard( );
	}

	// Refresh the HUD since a score has changed.
	HUD_ShouldRefreshBeforeRendering( );
}

//*****************************************************************************
//
void PLAYER_ResetAllScoreCounters( player_t *pPlayer )
{
	// [BB] Sanity check.
	if ( pPlayer == NULL )
		return;

	if ( pPlayer->lPointCount != 0 )
		PLAYER_SetPoints ( pPlayer, 0 );

	if ( pPlayer->fragcount != 0 )
		PLAYER_SetFragcount( pPlayer, 0, false, false );

	// [AK] Reset the player's kill count too.
	if ( pPlayer->killcount != 0 )
		PLAYER_SetKills( pPlayer, 0 );

	if ( pPlayer->ulWins > 0 )
		PLAYER_SetWins( pPlayer, 0 );

	// [AK] Reset the player's death count too.
	if ( pPlayer->ulDeathCount > 0 )
		PLAYER_SetDeaths( pPlayer, 0 );
}

//*****************************************************************************
//
void PLAYER_ResetAllPlayersFragcount( void )
{
	ULONG	ulIdx;

	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if ( playeringame[ulIdx] == false )
			continue;

		players[ulIdx].fragcount = 0;

		// If we're the server, 
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		{
			SERVERCONSOLE_UpdatePlayerInfo( ulIdx, UDF_FRAGS );
			SERVERCONSOLE_UpdateScoreboard( );
		}
	}

	// Refresh the HUD since a score has changed.
	HUD_ShouldRefreshBeforeRendering( );
}

//*****************************************************************************
//
void PLAYER_ResetAllPlayersSpecialCounters ( )
{
	for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
		PLAYER_ResetSpecialCounters ( &players[ulIdx] );
}

//*****************************************************************************
//
void PLAYER_ResetSpecialCounters ( player_t *pPlayer )
{
	if ( pPlayer == NULL )
		return;

	pPlayer->ulLastExcellentTick = 0;
	pPlayer->ulLastFragTick = 0;
	pPlayer->ulLastBFGFragTick = 0;
	pPlayer->ulConsecutiveHits = 0;
	pPlayer->ulConsecutiveRailgunHits = 0;
	pPlayer->ulDeathsWithoutFrag = 0;
	pPlayer->ulFragsWithoutDeath = 0;
	pPlayer->RailgunShots = 0;
	pPlayer->ulUnrewardedDamageDealt = 0;
}

//*****************************************************************************
//
void PLAYER_SetTeam( player_t *pPlayer, ULONG ulTeam, bool bNoBroadcast )
{
	bool	bBroadcastChange = false;

	if (
		// Player was removed from a team.
		( pPlayer->bOnTeam && ulTeam == teams.Size( ) ) || 

		// Player was put on a team after not being on one.
		( pPlayer->bOnTeam == false && ulTeam < teams.Size( ) ) ||
		
		// Player is on a team, but is being put on a different team.
		( pPlayer->bOnTeam && ( ulTeam < teams.Size( ) ) && ( ulTeam != pPlayer->Team ))
		)
	{
		bBroadcastChange = true;
	}

	// We don't want to broadcast a print message.
	if ( bNoBroadcast )
		bBroadcastChange = false;

	// Set whether or not this player is on a team.
	if ( ulTeam == teams.Size( ) )
		pPlayer->bOnTeam = false;
	else
		pPlayer->bOnTeam = true;

	// Set the team.
	pPlayer->Team = ulTeam;

	// [BB] Keep track of the original playerstate. In case it's altered by TEAM_EnsurePlayerHasValidClass,
	// the player had a class forbidden to the new team and needs to be respawned.
	const int origPlayerstate = pPlayer->playerstate;

	// [BB] Make sure that the player only uses a class available to his team.
	TEAM_EnsurePlayerHasValidClass ( pPlayer );

	// [BB] The class was changed, so we remove the old player body and respawn the player immediately.
	if ( ( pPlayer->playerstate != origPlayerstate ) && ( pPlayer->playerstate == PST_REBORNNOINVENTORY ) )
	{
		// [BB] Morphed players need to be unmorphed before changing teams.
		// [AK] Using MORPH_UNDOBYTIMEOUT ensures this succeeds when they're invulnerable.
		if ( pPlayer->morphTics )
			P_UndoPlayerMorphWithoutFlash( pPlayer, pPlayer, MORPH_UNDOBYTIMEOUT, true );

		if ( pPlayer->mo )
		{
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVERCOMMANDS_DestroyThing( pPlayer->mo );

			pPlayer->mo->Destroy( );
			pPlayer->mo = NULL;
		}

		GAMEMODE_SpawnPlayer ( pPlayer - players );
	}
	// [AK] If the player isn't respawned, the HUD should still be refreshed in
	// case the ally or enemy counters need to be updated.
	else if ( GAMEMODE_GetCurrentFlags( ) & GMF_DEADSPECTATORS )
	{
		HUD_ShouldRefreshBeforeRendering( );
	}

	// If we're the server, tell clients about this team change.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
	{
		SERVERCOMMANDS_SetPlayerTeam( pPlayer - players );

		// Player has changed his team! Tell clients.
		if ( bBroadcastChange )
		{
			SERVER_Printf( "%s joined the \034%s%s " TEXTCOLOR_NORMAL "team.\n", pPlayer->userinfo.GetName(), TEAM_GetTextColorName( ulTeam ), TEAM_GetName( ulTeam ));
		}		
	}

	// Finally, update the player's color.
	R_BuildPlayerTranslation( pPlayer - players );
	if ( StatusBar && pPlayer->mo && ( pPlayer->mo->CheckLocalView( consoleplayer )))
		StatusBar->AttachToPlayer( pPlayer );

	// Update this player's info on the scoreboard.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVERCONSOLE_UpdatePlayerInfo( pPlayer - players, UDF_FRAGS );

	// [BL] If the player was "unarmed" give back his inventory now.
	// [BB] Note: On the clients bUnarmed is never true!
	// [BB] This may be called to set team of a player who wasn't spawned yet (for instance in the CSkullBot constructor).
	if ( pPlayer->bUnarmed && pPlayer->mo )
	{
		pPlayer->mo->GiveDefaultInventory();
		if ( deathmatch )
			pPlayer->mo->GiveDeathmatchInventory();

		pPlayer->bUnarmed = false;

		// [BB] Since the clients never come here, tell them about their new inventory (includes bringing up the weapon on the client).
		if ( NETWORK_GetState() == NETSTATE_SERVER )
		{
			const ULONG ulPlayer = static_cast<ULONG>( pPlayer-players );
			SERVER_ResetInventory( ulPlayer, true, false ); // [RK] Don't give out inventory in reverse order.
			// [BB] SERVER_ResetInventory only informs the player ulPlayer. Let the others know of at least the ammo of the player.
			SERVERCOMMANDS_SyncPlayerAmmoAmount ( ulPlayer, ulPlayer, SVCF_SKIPTHISCLIENT );
		}

		P_BringUpWeapon(pPlayer);
	}

	// [TP] Update player translations if we override the colors, odds are they're very different now.
	if ( D_ShouldOverridePlayerColors() )
		D_UpdatePlayerColors();

	// [Dusk] Update the "join team" menu now if it was not random, so that when the map changes,
	// we can just rejoin the team without changing the team in the menu. This is super annoying to
	// deal with manually in for instance private CTF when the wrong team is accidentally chosen,
	// fixed with "changeteam" and then causing you to wind up in the wrong team on map restart
	// again...
	/* [BB] FIXME
	if (( NETWORK_GetState() != NETSTATE_SERVER )
		&& ( pPlayer == &players[consoleplayer] )
		&& ( pPlayer->bOnTeam )
		// This filters out the random team case.
		&& ( menu_teamidxjointeammenu < signed( TEAM_GetNumAvailableTeams( ) )))
	{
		menu_teamidxjointeammenu = ulTeam;
	}
	*/
}

//*****************************************************************************
//
// [BC] *grumble*
void	G_DoReborn (int playernum, bool freshbot);
void PLAYER_SetSpectator( player_t *pPlayer, bool bBroadcast, bool bDeadSpectator )
{
	AActor	*pOldBody;

	// Already a spectator. Check if their spectating state is changing.
	if ( pPlayer->bSpectating == true )
	{
		// Player is trying to become a true spectator (if not one already).
		if ( bDeadSpectator == false )
		{
			// If they're becoming a true spectator after being a dead spectator, do all the
			// special spectator stuff we didn't do before.
			if ( pPlayer->bDeadSpectator )
			{
				pPlayer->bDeadSpectator = false;
				// As dead spectators preserve inventory, destroy it now.
				if ( pPlayer->mo != NULL )
				{
					pPlayer->mo->DestroyAllInventory( );
				}

				// Run the disconnect scripts now that the player is leaving.
				PLAYER_LeavesGame ( pPlayer - players );

				if ( NETWORK_InClientMode() == false )
				{
					// Tell the join queue module that a player is leaving the game.
					JOINQUEUE_PlayerLeftGame( pPlayer - players, true );
				}

				pPlayer->health = deh.StartHealth;
				if ( pPlayer->mo )
					pPlayer->mo->health = pPlayer->health;

				if ( bBroadcast )
				{
					// Send out a message saying this player joined the spectators.
					NETWORK_Printf( "%s joined the spectators.\n", pPlayer->userinfo.GetName() );
				}

				// This player no longer has a team affiliation.
				pPlayer->bOnTeam = false;

				// [AK] The spectator count has changed, so refresh the HUD.
				HUD_ShouldRefreshBeforeRendering( );
			}
		}

		// [BB] Make sure the player loses his frags, wins and points.
		if ( NETWORK_InClientMode() == false )
			PLAYER_ResetAllScoreCounters ( pPlayer );

		return;
	}
	// [AK] If this player's current mobj derives from APlayerChunk due to A_SkullPop
	// then we must reset their mobj back to the original body.
	else if (( bDeadSpectator == false ) && ( pPlayer->mo != NULL ) && ( pPlayer->cls != NULL ))
	{
		APlayerPawn *mo = pPlayer->mo;

		if (( mo->IsKindOf( RUNTIME_CLASS( APlayerChunk ))) && ( mo->target != NULL ))
		{
			APlayerPawn *pmo = barrier_cast<APlayerPawn *>( pPlayer->mo->target );
			mo->player = NULL;
			pmo->player = pPlayer;
			pPlayer->mo = pmo;

			// [AK] Set the camera back to the original body.
			if ( pPlayer->camera == mo )
				pPlayer->camera = pmo;
		}
	}

	// [BB] Morphed players need to be unmorphed before being changed to spectators.
	// [WS] This needs to be done before we turn our player into a spectator.
	// [AK] Don't do this yet if they're turning into a dead spectator. Also, use
	// MORPH_UNDOBYTIMEOUT to ensure this succeeds when they're invulnerable.
	if (( pPlayer->morphTics ) && ( NETWORK_InClientMode( ) == false ) && ( bDeadSpectator == false ))
		P_UndoPlayerMorphWithoutFlash( pPlayer, pPlayer, MORPH_UNDOBYTIMEOUT, true );

	// Flag this player as being a spectator.
	pPlayer->bSpectating = true;
	pPlayer->bDeadSpectator = bDeadSpectator;
	// [BB] Spectators have to be excluded from the special handling that prevents selection room pistol-fights.
	pPlayer->bUnarmed = false;

	// Run the disconnect scripts if the player is leaving the game.
	if ( bDeadSpectator == false )
	{
		PLAYER_LeavesGame( pPlayer - players );
	}

	// If this player was eligible to get an assist, cancel that.
	if ( NETWORK_InClientMode() == false )
		TEAM_CancelAssistsOfPlayer ( static_cast<unsigned>( pPlayer - players ) );

	if ( pPlayer->mo )
	{
		// [AK] Remember what weapon the player used before they become a specator. In case they
		// become a dead spectator, we need to know if the weapon has its own preferred skin so
		// we can apply the skin's scale to their old body correctly.
		AWeapon *pOldWeapon = pPlayer->ReadyWeapon ? static_cast<AWeapon *>( pPlayer->ReadyWeapon->GetDefault( )) : NULL;

		// [BB] Stop all scripts of the player that are still running.
		if ( !( zacompatflags & ZACOMPATF_DONT_STOP_PLAYER_SCRIPTS_ON_DISCONNECT ) )
			FBehavior::StaticStopMyScripts ( pPlayer->mo );
		// Before we start fucking with the player's body, drop important items
		// like flags, etc.
		if ( NETWORK_InClientMode() == false )
			pPlayer->mo->DropImportantItems( false );

		if ( !bDeadSpectator || !( zadmflags & ZADF_DEAD_PLAYERS_CAN_KEEP_INVENTORY ) )
		{
			// Take away all of the player's inventory when they become true spectator
			// or when dead spectators are not allowed to keep inventory.
			// [BB] Needs to be done before G_DoReborn is called for dead spectators. Otherwise ReadyWeapon is not NULLed.
			pPlayer->mo->DestroyAllInventory( );
		}
		else
		{
			// Dead spectators preserve inventory.
			pPlayer->ReadyWeapon = NULL;
		}

		// Is this player tagged as a dead spectator, give him life.
		pPlayer->playerstate = PST_LIVE;
		if ( bDeadSpectator == false )
		{
			pPlayer->health = pPlayer->mo->health = deh.StartHealth;
			// [BB] Spectators don't crouch. We don't need to uncrouch dead spectators, that is done automatically
			// when they are reborn as dead spectator.
			pPlayer->Uncrouch();
		}

		// If this player is being forced into spectatorship, don't destroy his or her
		// old body.
		if ( bDeadSpectator )
		{
			// Save the player's old body, and respawn him or her.
			pOldBody = pPlayer->mo;
			// [BB] This also transfers the inventory from the old to the new body.
			players[pPlayer - players].playerstate = ( zadmflags & ZADF_DEAD_PLAYERS_CAN_KEEP_INVENTORY ) ? PST_REBORN : PST_REBORNNOINVENTORY;
			GAMEMODE_SpawnPlayer( pPlayer - players );

			// [AK] Remember our old body when we become a dead spectator. This is so we can respawn back
			// at our corpse in case DF2_SAME_SPAWN_SPOT is enabled.
			pPlayer->pCorpse = pOldBody;

			if ( pOldBody )
			{
				// Set the player's new body to the position of his or her old body.
				if ( pPlayer->mo )
				{
					// [BB] It's possible that the old body is at a place that's inaccessible to spectators
					// (whatever source killed the player possibly moved the body after the player's death).
					// If that's the case, don't move the spectator to the old body position, but to the place
					// where G_DoReborn spawned him.
					fixed_t playerSpawnX = pPlayer->mo->x;
					fixed_t playerSpawnY = pPlayer->mo->y;
					fixed_t playerSpawnZ = pPlayer->mo->z;
					pPlayer->mo->SetOrigin( pOldBody->x, pOldBody->y, pOldBody->z );
					if ( P_TestMobjLocation ( pPlayer->mo ) == false )
						pPlayer->mo->SetOrigin( playerSpawnX, playerSpawnY, playerSpawnZ );

					if ( NETWORK_GetState( ) == NETSTATE_SERVER )
						SERVERCOMMANDS_MoveLocalPlayer( ULONG( pPlayer - players ));
				}

				// [AK] Disassociate the player from their old body. This prevents the old body from
				// being frozen and not finishing their animation when they become a spectator.
				// Add their old body to body queue too.
				G_QueueBody( pOldBody );
				pOldBody->player = NULL;

				// [AK] Apply the skin's scale to the old body's scale.
				PLAYER_ApplySkinScaleToBody( pPlayer, pOldBody, pOldWeapon );
			}

			// [AK] If the player was morphed before turning into a dead spectator, unmorph them now.
			// Use MORPH_UNDOBYTIMEOUT to ensure this succeeds when they're invulnerable.
			if (( pPlayer->morphTics ) && ( NETWORK_InClientMode( ) == false ))
				P_UndoPlayerMorphWithoutFlash( pPlayer, pPlayer, MORPH_UNDOBYTIMEOUT, true );
		}
		// [BB] In case the player is not respawned as dead spectator, we have to manually clear its TID.
		else
		{
			pPlayer->mo->RemoveFromHash ();
			pPlayer->mo->tid = 0;
		}

		// [BB] Set a bunch of stuff, e.g. make the player unshootable, etc.
		PLAYER_SetDefaultSpectatorValues ( pPlayer );

		// [BB] We also need to stop all sounds associated to the player pawn, spectators
		// aren't supposed to make any sounds. This is especially crucial if a looping sound
		// is played by the player pawn.
		S_StopAllSoundsFromActor ( pPlayer->mo );

		if (( bDeadSpectator == false ) && bBroadcast )
		{
			// Send out a message saying this player joined the spectators.
			NETWORK_Printf( "%s joined the spectators.\n", pPlayer->userinfo.GetName() );
		}
	}

	// This player no longer has a team affiliation.
	if ( bDeadSpectator == false )
		pPlayer->bOnTeam = false;

	// Player's lose all their frags when they become a spectator.
	if ( bDeadSpectator == false )
	{
		// [BB] Make sure the player loses his frags, wins and points.
		if ( NETWORK_InClientMode() == false )
			PLAYER_ResetAllScoreCounters ( pPlayer );

		// Also, tell the joinqueue module that a player has left the game.
		if ( NETWORK_InClientMode() == false )
		{
			// Tell the join queue module that a player is leaving the game.
			JOINQUEUE_PlayerLeftGame( pPlayer - players, true );
		}

		if ( pPlayer->pSkullBot )
			pPlayer->pSkullBot->PostEvent( BOTEVENT_SPECTATING );
	}

	// Update this player's info on the scoreboard.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVERCONSOLE_UpdatePlayerInfo( pPlayer - players, UDF_FRAGS );

	// [TP] If we left the game, we need to rebuild player translations if we overrid them.
	if ( D_ShouldOverridePlayerColors() && pPlayer - players == consoleplayer )
		D_UpdatePlayerColors();

	// [AK] The spectator count has changed, so refresh the HUD.
	HUD_ShouldRefreshBeforeRendering( );
}

//*****************************************************************************
//
void PLAYER_SetDefaultSpectatorValues( player_t *pPlayer )
{
	if ( ( pPlayer == NULL ) || ( pPlayer->mo == NULL ) )
		return;

	// Make the player unshootable, etc.
	pPlayer->mo->flags &= ~(MF_CORPSE|MF_SOLID|MF_SHOOTABLE|MF_PICKUP);
	pPlayer->mo->flags |= MF_NOGRAVITY;
	pPlayer->mo->flags2 &= ~(MF2_PASSMOBJ|MF2_FLOATBOB);
	pPlayer->mo->flags2 |= (MF2_CANNOTPUSH|MF2_SLIDE|MF2_THRUACTORS|MF2_FLY);
	pPlayer->mo->flags3 = MF3_NOBLOCKMONST;
	pPlayer->mo->flags4 = 0;
	pPlayer->mo->flags5 = 0;
	pPlayer->mo->RenderStyle = STYLE_None;

	// [RK] Clear the frozen flags so the spectator can move.
	pPlayer->cheats &= ~(CF_FROZEN | CF_TOTALLYFROZEN);

	// [AK] Enable the NOINTERACTION flag if there should be no physical restrictions.
	if ( P_IsSpectatorUnrestricted( pPlayer->mo ))
		pPlayer->mo->flags5 |= MF5_NOINTERACTION;

	// [BB] Speed and viewheight of spectators should be independent of the player class.
	pPlayer->mo->Speed = FRACUNIT;
	pPlayer->mo->ForwardMove1 = pPlayer->mo->ForwardMove2 = FRACUNIT;
	pPlayer->mo->SideMove1 = pPlayer->mo->SideMove2 = FRACUNIT;
	pPlayer->mo->ViewHeight = 41*FRACUNIT;
	// [BB] Also can't hurt to reset gravity.
	pPlayer->mo->gravity = FRACUNIT;

	// Make the player flat, so he can travel under doors and such.
	pPlayer->mo->height = 0;

	// [Dusk] Player is now a spectator so he no longer is damaged by anything.
	pPlayer->mo->DamageType = NAME_None;

	// Make monsters unable to "see" this player.
	// Turn the fly cheat on.
	pPlayer->cheats |= (CF_NOTARGET|CF_FLY);

	// Reset a bunch of other stuff.
	pPlayer->extralight = 0;
	pPlayer->fixedcolormap = NOFIXEDCOLORMAP;
	pPlayer->fixedlightlevel = -1;
	pPlayer->damagecount = 0;
	pPlayer->bonuscount = 0;
	pPlayer->hazardcount= 0;
	pPlayer->poisoncount = 0;
	pPlayer->inventorytics = 0;

	// [BB] Reset any screen fade.
	pPlayer->BlendR = 0;
	pPlayer->BlendG = 0;
	pPlayer->BlendB = 0;
	pPlayer->BlendA = 0;

	// [BB] Also cancel any active faders.
	{
		TThinkerIterator<DFlashFader> iterator;
		DFlashFader *fader;

		while ( (fader = iterator.Next()) )
		{
			if ( fader->WhoFor() == pPlayer->mo )
			{
				fader->Cancel ();
			}
		}
	}

	// [BB] Remove all dynamic lights associated to the player's body.
	for ( unsigned int i = 0; i < pPlayer->mo->dynamiclights.Size(); ++i )
		pPlayer->mo->dynamiclights[i]->Destroy();
	pPlayer->mo->dynamiclights.Clear();
}

//*****************************************************************************
//
void PLAYER_SpectatorJoinsGame( player_t *pPlayer )
{
	if ( pPlayer == NULL )
		return;

	pPlayer->playerstate = PST_ENTERNOINVENTORY;
	// [BB] Mark the spectator body as obsolete, but don't delete it before the
	// player gets a new body.
	if ( pPlayer->mo && pPlayer->bSpectating )
	{
		pPlayer->mo->STFlags |= STFL_OBSOLETE_SPECTATOR_BODY;
		// [BB] Also stop all associated scripts. Otherwise they would get disassociated
		// and continue to run even if the player disconnects later.
		if ( !( zacompatflags & ZACOMPATF_DONT_STOP_PLAYER_SCRIPTS_ON_DISCONNECT ) )
			FBehavior::StaticStopMyScripts (pPlayer->mo);
	}

	pPlayer->bSpectating = false;
	pPlayer->bDeadSpectator = false;

	// [AK] Reset the client's last move tick to zero so that the server doesn't
	// immediately assume they're missing packets because it doesn't receive their
	// movement commands right away, depending on their ping.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVER_GetClient( pPlayer - players )->lLastMoveTick = 0;

	// [BB] If the spectator used the chasecam or noclip cheat (which is always allowed for spectators)
	// remove it now that he joins the game.
	// [Leo] The fly cheat is set by default in PLAYER_SetDefaultSpectatorValues.
	if ( pPlayer->cheats & ( CF_CHASECAM|CF_NOCLIP|CF_FLY ))
	{
		pPlayer->cheats &= ~(CF_CHASECAM|CF_NOCLIP|CF_FLY);
		if ( NETWORK_GetState() == NETSTATE_SERVER  )
			SERVERCOMMANDS_SetPlayerCheats( static_cast<ULONG>( pPlayer - players ), static_cast<ULONG>( pPlayer - players ), SVCF_ONLYTHISCLIENT );
	}

	// [BB] If he's a bot, tell him that he successfully joined.
	if ( pPlayer->pSkullBot )
		pPlayer->pSkullBot->PostEvent( BOTEVENT_JOINEDGAME );
}

//*****************************************************************************
//
void PLAYER_SetPoints( player_t *pPlayer, ULONG ulPoints )
{
	// Set the player's point count.
	pPlayer->lPointCount = ulPoints;

	// Refresh the HUD since a score has changed.
	HUD_ShouldRefreshBeforeRendering( );

	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
	{
		// If we're the server, notify the clients of the point count change.
		SERVERCOMMANDS_SetPlayerPoints( static_cast<ULONG>( pPlayer - players ));

		// Also, update the scoreboard.
		SERVERCONSOLE_UpdatePlayerInfo( static_cast<ULONG>( pPlayer - players ), UDF_FRAGS );
		SERVERCONSOLE_UpdateScoreboard( );
	}
}

//*****************************************************************************
//
void PLAYER_SetWins( player_t *pPlayer, ULONG ulWins )
{
	// Set the player's win count.
	pPlayer->ulWins = ulWins;

	// Refresh the HUD since a score has changed.
	HUD_ShouldRefreshBeforeRendering( );

	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
	{
		// If we're the server, notify the clients of the win count change.
		SERVERCOMMANDS_SetPlayerWins( pPlayer - players );

		// Also, update the scoreboard.
		SERVERCONSOLE_UpdatePlayerInfo( pPlayer - players, UDF_FRAGS );
		SERVERCONSOLE_UpdateScoreboard( );
	}
}

//*****************************************************************************
//
void PLAYER_SetKills( player_t *pPlayer, ULONG ulKills )
{
	// Set the player's kill count.
	pPlayer->killcount = ulKills;

	// Refresh the HUD since a score has changed.
	HUD_ShouldRefreshBeforeRendering( );

	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
	{
		// If we're the server, notify the clients of the kill count change.
		SERVERCOMMANDS_SetPlayerKillCount( pPlayer - players );

		// Also, update the scoreboard.
		SERVERCONSOLE_UpdatePlayerInfo( pPlayer - players, UDF_FRAGS );
		SERVERCONSOLE_UpdateScoreboard( );
	}
}

//*****************************************************************************
//
void PLAYER_SetDeaths( player_t *pPlayer, ULONG ulDeaths )
{
	// Set the player's death count.
	pPlayer->ulDeathCount = ulDeaths;

	// Refresh the HUD since a score has changed.
	HUD_ShouldRefreshBeforeRendering( );

	// If we're the server, notify the clients of the death count change.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVERCOMMANDS_SetPlayerDeaths( pPlayer - players );
}

//*****************************************************************************
//
void PLAYER_SetTime( player_t *pPlayer, ULONG ulTime )
{
	if ( pPlayer == NULL )
		return;

	// Set the player's time.
	pPlayer->ulTime = ulTime;

	// Potentially update the scoreboard or send out an update.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
	{
		if (( pPlayer->ulTime % ( TICRATE * 60 )) == 0 )
		{
			// Send out the updated time field to all clients.
			SERVERCOMMANDS_UpdatePlayerTime( pPlayer - players );

			// Update the console as well.
			SERVERCONSOLE_UpdatePlayerInfo( pPlayer - players, UDF_TIME );
		}
	}
}

//*****************************************************************************
//
void PLAYER_SetStatus( player_t *player, const int statuses, const bool enable, const int networkFlags )
{
	if ( player == nullptr )
		return;

	const int oldStatuses = player->statuses;

	if ( enable )
		player->statuses |= statuses;
	else
		player->statuses &= ~statuses;

	// [AK] Don't send updates if none of the statuses changed.
	if ( player->statuses != oldStatuses )
	{
		// [AK] If we're a client, tell the server that our status changed.
		if ( NETWORK_GetState( ) == NETSTATE_CLIENT )
		{
			if ( networkFlags & SETPLAYERSTATUS_CLIENTSENDSUPDATE )
				CLIENTCOMMANDS_SetStatus( );
		}
		// [AK] If we're the server, tell the clients that this player's status
		// changed, except when sending updates is forbidding, or if we update
		// this player's "ready to go on" status when everyone's ready to go on now.
		else if (( NETWORK_GetState( ) == NETSTATE_SERVER ) && (( networkFlags & SETPLAYERSTATUS_SERVERCANTSENDUPDATE ) == false ))
		{
			if (( statuses != PLAYERSTATUS_READYTOGOON ) || ( SERVER_IsEveryoneReadyToGoOn( ) == false ))
				SERVERCOMMANDS_SetPlayerStatus( player - players );
		}

		// [AK] If we're recording a demo, write a command to update our status.
		// Don't update the "talking" status since voice chat isn't used in demos.
		if (( CLIENTDEMO_IsRecording( )) && ( player == &players[consoleplayer] ))
			CLIENTDEMO_WriteSetStatus( statuses & ~PLAYERSTATUS_TALKING, enable );
	}
}

//*****************************************************************************
//
LONG PLAYER_GetHealth( ULONG ulPlayer )
{
	return players[ulPlayer].health;
}

//*****************************************************************************
//
LONG PLAYER_GetLivesLeft( ULONG ulPlayer )
{
	return players[ulPlayer].ulLivesLeft;
}

//*****************************************************************************
//
void PLAYER_SelectPlayersWithHighestValue ( LONG (*GetValue) ( ULONG ulPlayer ), TArray<ULONG> &Players )
{
	LONG lHighestValue;

	TArray<ULONG> selectedPlayers;

	for ( unsigned int i = 0; i < Players.Size(); ++i )
	{
		const ULONG ulIdx = Players[i];

		if (( playeringame[ulIdx] == false ) ||
			( players[ulIdx].bSpectating ))
		{
			continue;
		}

		if ( selectedPlayers.Size() == 0 )
		{
			lHighestValue = GetValue ( ulIdx );
			selectedPlayers.Push ( ulIdx );
		}
		else
		{
			if ( GetValue ( ulIdx ) > lHighestValue )
			{
				lHighestValue = GetValue ( ulIdx );
				selectedPlayers.Clear();
				selectedPlayers.Push ( ulIdx );
			}
			else if ( GetValue ( ulIdx ) == lHighestValue )
				selectedPlayers.Push ( ulIdx );
		}
	}
	Players = selectedPlayers;
}

//*****************************************************************************
//
bool PLAYER_IsValidPlayer( const ULONG ulPlayer )
{
	// If the player index is out of range, or this player is not in the game, then the
	// player index is not valid.
	if (( ulPlayer >= MAXPLAYERS ) || ( playeringame[ulPlayer] == false ))
		return ( false );

	return ( true );
}

//*****************************************************************************
//
bool PLAYER_IsValidPlayerWithMo( const ULONG ulPlayer )
{
	// Spectators cannot interact with the game so
	// they're treated like players without a mo.
	return ( PLAYER_IsValidPlayer ( ulPlayer ) && players[ulPlayer].mo && !players[ulPlayer].bSpectating );
}

//*****************************************************************************
//
bool PLAYER_IsTrueSpectator( player_t *pPlayer )
{
	if ( GAMEMODE_GetCurrentFlags() & GMF_DEADSPECTATORS )
		return (( pPlayer->bSpectating ) && ( pPlayer->bDeadSpectator == false ));

	return ( pPlayer->bSpectating );
}

//*****************************************************************************
//
void PLAYER_CheckStruckPlayer( AActor *actor )
{
	if ( NETWORK_InClientMode( ))
		return;

	if (( actor != nullptr ) && ( actor->player != nullptr ))
	{
		player_t *player = actor->player;

		if ( player->bStruckPlayer )
		{
			player->ulConsecutiveHits++;

			// If the player has made 5 straight consecutive hits with a weapon, award a medal.
			// Award a "Precision" medal if they made 10+ consecutive hits. Otherwise, award an "Accuracy" medal.
			if (( player->ulConsecutiveHits % 5 ) == 0 )
				MEDAL_GiveMedal( player - players, player->ulConsecutiveHits >= 10 ? "Precision" : "Accuracy" );

			// Reset the struck player flag.
			player->bStruckPlayer = false;
		}
		else
		{
			player->ulConsecutiveHits = 0;
		}
	}
}

//*****************************************************************************
//
bool PLAYER_ShouldSpawnAsSpectator( player_t *pPlayer )
{
	UCVarValue	Val;

	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
	{
		// [BB] Possibly check if the player is authenticated.
		if ( sv_forcelogintojoin )
		{
			const ULONG ulPlayer = static_cast<ULONG> ( pPlayer - players );
			if ( SERVER_IsValidClient ( ulPlayer ) && SERVER_GetClient ( ulPlayer )->loggedIn == false )
				return true;
		}

		// If there's a join password, the player should start as a spectator.
		Val = sv_joinpassword.GetGenericRep( CVAR_String );
		if (( sv_forcejoinpassword ) && ( strlen( Val.String )))
		{
			// [BB] Only force the player to start as spectator if he didn't already join.
			// In that case the join password was already checked.
			const ULONG ulPlayer = static_cast<ULONG> ( pPlayer - players );
			if ( PLAYER_IsValidPlayer( ulPlayer ) == false )
				return ( true );

			// [BB] If the player is already spectating, he should still spawn as spectator.
			if ( pPlayer->bSpectating )
				return ( true );

			// [BB] If this is a client that hasn't been spawned yet, he didn't pass the password check yet, so force him to start as spectator.
			if ( ( pPlayer->bIsBot == false ) && ( SERVER_GetClient( ulPlayer )->State < CLS_SPAWNED_BUT_NEEDS_AUTHENTICATION ) )
				return ( true );
		}
	}

	// [BB] Check if the any reason prevents this particular player from joining.
	if ( GAMEMODE_PreventPlayersFromJoining( static_cast<ULONG> ( pPlayer - players ) ) )
		return ( true );

	// Players entering a teamplay game must choose a team first before joining the fray.
	if (( pPlayer->bOnTeam == false ) || ( playeringame[pPlayer - players] == false ))
	{
		if ( ( GAMEMODE_GetCurrentFlags() & GMF_PLAYERSONTEAMS ) &&
			( !( GAMEMODE_GetCurrentFlags() & GMF_TEAMGAME ) || ( TemporaryTeamStarts.Size( ) == 0 ) ) &&
			(( dmflags2 & DF2_NO_TEAM_SELECT ) == false ))
		{
			return ( true );
		}
	}

	// Passed all checks!
	return ( false );
}

//*****************************************************************************
//
bool PLAYER_Taunt( player_t *pPlayer )
{
	// Don't taunt if we're not in a level!
	if ( gamestate != GS_LEVEL )
		return ( false );

	// Spectators or dead people can't taunt!
	if (( pPlayer->bSpectating ) ||
		( pPlayer->health <= 0 ) ||
		( pPlayer->mo == NULL ) ||
		( zacompatflags & ZACOMPATF_DISABLETAUNTS ))
	{
		return ( false );
	}

	if ( cl_taunts )
		S_Sound( pPlayer->mo, CHAN_VOICE, "*taunt", 1, ATTN_NORM );

	return ( true );
}

//*****************************************************************************
//
LONG PLAYER_GetRailgunColor( player_t *pPlayer )
{
	// Determine the railgun trail's color.
	switch ( pPlayer->userinfo.GetRailColor() )
	{
	case RAILCOLOR_BLUE:
	default:

		return ( V_GetColorFromString( NULL, "00 00 ff" ));
	case RAILCOLOR_RED:

		return ( V_GetColorFromString( NULL, "ff 00 00" ));
	case RAILCOLOR_YELLOW:

		return ( V_GetColorFromString( NULL, "ff ff 00" ));
	case RAILCOLOR_BLACK:

		return ( V_GetColorFromString( NULL, "0f 0f 0f" ));
	case RAILCOLOR_SILVER:

		return ( V_GetColorFromString( NULL, "9f 9f 9f" ));
	case RAILCOLOR_GOLD:

		return ( V_GetColorFromString( NULL, "bf 8f 2f" ));
	case RAILCOLOR_GREEN:

		return ( V_GetColorFromString( NULL, "00 ff 00" ));
	case RAILCOLOR_WHITE:

		return ( V_GetColorFromString( NULL, "ff ff ff" ));
	case RAILCOLOR_PURPLE:

		return ( V_GetColorFromString( NULL, "ff 00 ff" ));
	case RAILCOLOR_ORANGE:

		return ( V_GetColorFromString( NULL, "ff 8f 00" ));
	case RAILCOLOR_RAINBOW:

		return ( -2 );
	}
}

//*****************************************************************************
//
void PLAYER_AwardDamagePointsForAllPlayers( void )
{
	if ( (zadmflags & ZADF_AWARD_DAMAGE_INSTEAD_KILLS)
		&& (GAMEMODE_GetCurrentFlags() & GMF_PLAYERSEARNKILLS) )
	{
		for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
		{
			if ( playeringame[ulIdx] == false )
				continue;

			player_t *p = &players[ulIdx];

			int points = p->ulUnrewardedDamageDealt / 100;
			if ( points > 0 )
			{
				PLAYER_SetPoints ( p, p->lPointCount + points );
				p->ulUnrewardedDamageDealt -= 100 * points;
			}
		}
	}
}

//*****************************************************************************
//
void PLAYER_SetWeapon( player_t *pPlayer, AWeapon *pWeapon, bool bClearWeaponForClientOnServer )
{
	// [BB] Validity check.
	if ( pPlayer == NULL )
		return;

	// [BB] If the server should just clear the weapon for the client (and this player is a client and not a bot), do so and return.
	// [BB] The server also has to do the weapon change if the coressponding client is still loading the level.
	if ( bClearWeaponForClientOnServer && ( NETWORK_GetState( ) == NETSTATE_SERVER ) && ( pPlayer->bIsBot == false ) && ( SERVER_GetClient( pPlayer - players )->State != CLS_SPAWNED_BUT_NEEDS_AUTHENTICATION ) )
	{
		PLAYER_ClearWeapon ( pPlayer );
		// [BB] Since PLAYER_SetWeapon is hopefully only called with bClearWeaponForClientOnServer == true in 
		// APlayerPawn::GiveDefaultInventory() assume that the weapon here is the player's starting weapon.
		// PLAYER_SetWeapon is possibly called multiple times, but the last call should be the starting weapon.
		pPlayer->StartingWeaponName = pWeapon ? pWeapon->GetClass()->TypeName : NAME_None;
		return;
	}

	// Set the ready and pending weapon.
	// [BB] When playing a client side demo, the weapon for the consoleplayer will
	// be selected by a recorded CLD_LCMD_INVUSE command.
	if ( ( CLIENTDEMO_IsPlaying() == false ) || ( pPlayer - players ) != consoleplayer )
		pPlayer->ReadyWeapon = pPlayer->PendingWeapon = pWeapon;

	// [BC] If we're a client, tell the server we're switching weapons.
	// [BB] It's possible, that a mod doesn't give the player any weapons. Therefore we also must check pWeapon
	// and can allow PLAYER_SetWeapon to be called with pWeapon == NULL.
	if (( NETWORK_GetState( ) == NETSTATE_CLIENT ) && (( pPlayer - players ) == consoleplayer ) && pWeapon )
	{
		CLIENTCOMMANDS_WeaponSelect( pWeapon->GetClass( ));

		if ( CLIENTDEMO_IsRecording( ))
			CLIENTDEMO_WriteLocalCommand( CLD_LCMD_INVUSE, pWeapon->GetClass( )->TypeName.GetChars( ) );
	}
	// [BB] Make sure to inform clients of bot weapon changes.
	else if ( ( NETWORK_GetState( ) == NETSTATE_SERVER ) && ( pPlayer->bIsBot == true ) )
		SERVERCOMMANDS_SetPlayerPendingWeapon( static_cast<ULONG> ( pPlayer - players ) );
}

//*****************************************************************************
//
void PLAYER_ClearWeapon( player_t *pPlayer )
{
	// [BB] Validity check.
	if ( pPlayer == NULL )
		return;

	pPlayer->ReadyWeapon = NULL;
	pPlayer->PendingWeapon = WP_NOCHANGE;
	pPlayer->psprites[ps_weapon].state = NULL;
	pPlayer->psprites[ps_flash].state = NULL;
	// [BB] Assume that it was not the client's decision to clear the weapon.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		pPlayer->bClientSelectedWeapon = false;
}

//*****************************************************************************
//
int PLAYER_GetOverrideSkin( player_t *player )
{
	int skin = -1;

	if ( player != nullptr )
	{
		int overrideSkin = player->CurrentPlayerClass;
		const char *skinName = nullptr;

		// [AK] Check if the player's skin was overridden from ACS.
		if ( player->ACSSkin != NAME_None )
		{
			skinName = player->ACSSkin;
			overrideSkin = R_FindSkin( skinName, player->CurrentPlayerClass );

			// [AK] Make sure that the overridden skin actually exists.
			if (( overrideSkin != player->CurrentPlayerClass ) || ( stricmp( skinName, "Base" ) == 0 ))
				skin = overrideSkin;
		}

		// [AK] Next, check if the player's current weapon has its own preferred
		// skin. Only apply this skin if the skin from ACS doesn't override it.
		if (( player->ReadyWeapon != nullptr ) && ( player->ReadyWeapon->PreferredSkin != NAME_None ))
		{
			if (( skin == -1 ) || ( player->ACSSkinOverridesWeaponSkin == false ))
			{
				skinName = player->ReadyWeapon->PreferredSkin;
				overrideSkin = R_FindSkin( skinName, player->CurrentPlayerClass );

				// [AK] Check if the weapon's PreferredSkin actually exists.
				if (( overrideSkin != player->CurrentPlayerClass ) || ( stricmp( skinName, "Base" ) == 0 ))
					skin = overrideSkin;
			}
		}
	}

	return skin;
}

//*****************************************************************************
//
bool PLAYER_ShouldForceBaseSkin( player_t *player )
{
	if ( player == nullptr )
		return true;

	// [AK] Force the base skin when all skins are disabled, or if only cheat
	// skins are are supposed to be disabled and the player's using one.
	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
	{
		if (( cl_skins <= 0 ) || (( cl_skins >= 2 ) && ( skins[player->userinfo.GetSkin( )].bCheat )))
			return true;
	}

	// [BB] MF4_NOSKIN should force the player to have the base skin too, the
	// same is true for morphed players.
	if ((( player->mo != nullptr ) && ( player->mo->flags4 & MF4_NOSKIN )) || ( player->morphTics ))
		return true;

	return false;
}

//*****************************************************************************
//
void PLAYER_ApplySkinScaleToBody( player_t *player, AActor *body, AWeapon *weapon )
{
	bool usingOverrideSkin = false;
	int skinIdx = 0;

	if (( player == nullptr ) || ( body == nullptr ))
		return;

	// [AK] Check if the player's skin was overridden from ACS and actually exists.
	if ( player->ACSSkin != NAME_None )
	{
		const int acsSkin = R_FindSkin( player->ACSSkin, player->CurrentPlayerClass );

		if ( acsSkin != player->CurrentPlayerClass )
		{
			skinIdx = acsSkin;
			usingOverrideSkin = true;
		}
	}

	// [AK] Next, check if the weapon's PreferredSkin actually exists. Only apply
	// this skin if the skin from ACS doesn't override it.
	if (( weapon ) && ( weapon->PreferredSkin != NAME_None ))
	{
		if (( usingOverrideSkin == false ) || ( player->ACSSkinOverridesWeaponSkin == false ))
		{
			const int weaponSkin = R_FindSkin( weapon->PreferredSkin, player->CurrentPlayerClass );

			if ( weaponSkin != player->CurrentPlayerClass )
			{
				skinIdx = weaponSkin;
				usingOverrideSkin = true;
			}
		}
	}

	// [AK] If the player isn't using an overridden skin, use their personal skin instead.
	if ( usingOverrideSkin == false )
		skinIdx = player->userinfo.GetSkin( );

	// [AK] An overridden skin also overrides NOSKIN.
	if (( usingOverrideSkin ) || ( skinIdx != 0 && ( body->flags4 & MF4_NOSKIN ) == false ))
	{
		const FPlayerSkin &skin = skins[skinIdx];

		// [AK] Don't apply a skin's scale to the body if it's not supposed to be visible.
		if ( skin.sprite == body->sprite )
		{
			const AActor *const defaultActor = body->GetDefault( );

			body->scaleX = Scale( body->scaleX, skin.ScaleX, defaultActor->scaleX );
			body->scaleY = Scale( body->scaleY, skin.ScaleY, defaultActor->scaleY );
		}
	}
}

//*****************************************************************************
//
void PLAYER_SetLivesLeft( player_t *player, const unsigned int livesLeft, const bool informClients )
{
	// [BB] Validity check.
	if ( player == nullptr )
		return;

	player->ulLivesLeft = livesLeft;

	if (( NETWORK_GetState( ) == NETSTATE_SERVER ) && ( informClients ))
		SERVERCOMMANDS_SetPlayerLivesLeft ( static_cast<ULONG>( player - players ));
}

//*****************************************************************************
//
bool PLAYER_IsAliveOrCanRespawn( player_t *pPlayer )
{
	// [BB] Validity check.
	if ( pPlayer == NULL )
		return false;

	return ( ( pPlayer->health > 0 ) || ( pPlayer->ulLivesLeft > 0 ) || ( pPlayer->bSpawnTelefragged == true ) || ( pPlayer->playerstate != PST_DEAD ) );
}

//*****************************************************************************
//
void PLAYER_RemoveFriends( const ULONG ulPlayer )
{
	// [BB] Validity check.
	if ( ulPlayer >= MAXPLAYERS )
		return;

	AActor *pActor = NULL;
	TThinkerIterator<AActor> iterator;

	while ( (pActor = iterator.Next ()) )
	{
		if ((pActor->flags3 & MF3_ISMONSTER) &&
			(pActor->flags & MF_FRIENDLY) &&
			pActor->FriendPlayer &&
			( (ULONG)( pActor->FriendPlayer - 1 ) == ulPlayer) )
		{
			pActor->FriendPlayer = 0;
			if ( !(GAMEMODE_GetCurrentFlags() & GMF_COOPERATIVE)
				// [BB] In a gamemode with teams, monsters with DesignatedTeam need to keep MF_FRIENDLY.
				&& ( !(GAMEMODE_GetCurrentFlags() & GMF_PLAYERSONTEAMS) || !TEAM_CheckIfValid ( pActor->DesignatedTeam ) ) )
				pActor->flags &= ~MF_FRIENDLY; // this is required in DM or monsters will be friendly to all players
		}
	}
}

//*****************************************************************************
//
void PLAYER_LeavesGame( const ULONG ulPlayer )
{
	// [BB] Validity check.
	if ( ulPlayer >= MAXPLAYERS )
		return;

	// [BB] Offline we need to create the disconnect particle effect here.
	if ( ( ( NETWORK_GetState( ) == NETSTATE_SINGLE ) || ( NETWORK_GetState( ) == NETSTATE_SINGLE_MULTIPLAYER ) ) && players[ulPlayer].mo )
		P_DisconnectEffect( players[ulPlayer].mo );

	// Run the disconnect scripts now that the player is leaving.
	if ( NETWORK_InClientMode() == false )
	{
		FBehavior::StaticStartTypedScripts( SCRIPT_Disconnect, NULL, true, ulPlayer );
		PLAYER_RemoveFriends ( ulPlayer );
		PLAYER_ClearEnemySoundFields( ulPlayer );
	}

	// [BB] Clear the players medals and the medal related counters. The former is something also clients need to do.
	MEDAL_ResetPlayerMedals( ulPlayer, true );
	PLAYER_ResetSpecialCounters ( &players[ulPlayer] );

	// [AK] We have no more use for our corpse since we left the game.
	players[ulPlayer].pCorpse = NULL;
}

//*****************************************************************************
//
// [Dusk] Remove sound targets from the given player
//
void PLAYER_ClearEnemySoundFields( const ULONG ulPlayer )
{
	if ( PLAYER_IsValidPlayer( ulPlayer ) == false )
		return;

	TThinkerIterator<AActor> it;
	AActor* mo;

	while (( mo = it.Next() ) != NULL )
	{
		if ( mo->LastHeard != NULL && mo->LastHeard->player == &players[ulPlayer] )
			mo->LastHeard = NULL;
	}

	for ( int i = 0; i < numsectors; ++i )
	{
		if ( sectors[i].SoundTarget != NULL && sectors[i].SoundTarget->player == &players[ulPlayer] )
			sectors[i].SoundTarget = NULL;
	}
}

//*****************************************************************************
//
bool PLAYER_NameMatchesServer( const FString &Name )
{
	FString nameNoColor = Name;
	V_RemoveColorCodes( nameNoColor );

	return (( !nameNoColor.CompareNoCase( "server" )) || ( !nameNoColor.CompareNoCase( "<server>" )));
}

//*****************************************************************************
//
bool PLAYER_NameUsed( const FString &Name, const ULONG ulIgnorePlayer )
{
	FString nameNoColor = Name;
	V_RemoveColorCodes( nameNoColor );

	for ( unsigned int i = 0; i < MAXPLAYERS; ++i )
	{
		if ( playeringame[i] == false )
			continue;

		if ( i == ulIgnorePlayer )
			continue;

		FString curName = players[i].userinfo.GetName();
		V_RemoveColorCodes( curName );
		if ( curName.CompareNoCase ( nameNoColor ) == 0 )
		 return true;
	}
	return false;
}

//*****************************************************************************
//
FString	PLAYER_GenerateUniqueName( void )
{
	FString name;
	do
	{
		name.Format ( "Player %d", M_Random( 10000 ) );
	} while ( PLAYER_NameUsed ( name ) == true );
	return name;
}

//*****************************************************************************
//
bool PLAYER_CanRespawnWhereDied( player_t *pPlayer )
{
	AActor *mo = pPlayer->pCorpse ? pPlayer->pCorpse : pPlayer->mo;

	// [AK] The player shouldn't respawn in any sectors that have damaging floors.
	if (( mo->Sector->damage > 0 ) || ( Terrains[P_GetThingFloorType( mo )].DamageAmount > 0 ))
		return false;

	switch ( mo->Sector->special )
	{
		case dLight_Strobe_Hurt:
		case dDamage_Hellslime:
		case dDamage_Nukage:
		case dDamage_End:
		case dDamage_SuperHellslime:
		case dDamage_LavaWimpy:
		case dDamage_LavaHefty:
		case dScroll_EastLavaDamage:
		case hDamage_Sludge:
		case sLight_Strobe_Hurt:
		case sDamage_Hellslime:
		case sDamage_SuperHellslime:
			return false;
	}

	// [AK] Also handle extended sector damage types.
	if ( mo->Sector->special & DAMAGE_MASK )
		return false;

	// [AK] Don't respawn the player in an instant death sector. Taken directly from P_PlayerSpawn.
	if (( mo->Sector->Flags & SECF_NORESPAWN ) || (( mo->Sector->special & 255 ) == Damage_InstantDeath ))
		return false;

	// [AK] Make sure they're not going to be blocked by anything upon respawning where they died.
	AActor *temp = Spawn( pPlayer->cls->TypeName.GetChars( ), mo->x, mo->y, mo->z, NO_REPLACE );
	bool bCanSpawn = P_TestMobjLocation( temp );

	temp->Destroy( );
	if ( bCanSpawn == false )
		return false;

	DCeiling *pCeiling;
	TThinkerIterator<DCeiling> CeilingIterator;

	// [AK] The player shouldn't respawn in a sector that has any active crushers. We'll first iterate
	// through all ceilings crushers and see if one is connected to the sector the player's body is in.
	while (( pCeiling = CeilingIterator.Next( )) != NULL )
	{
		if ( pCeiling->GetCrush( ) <= 0 )
			continue;

		// [AK] Don't respawn the player where they died if their body is partially inside a crusher.
		for ( msecnode_t *snode = mo->touching_sectorlist; snode; snode = snode->m_snext )
		{
			if ( snode->m_sector == pCeiling->GetSector( ))
				return false;
		}
	}

	DFloor *pFloor;
	TThinkerIterator<DFloor> FloorIterator;

	// [AK] Next, check all the floor crushers.	
	while (( pFloor = FloorIterator.Next( )) != NULL )
	{
		if ( pFloor->GetCrush( ) <= 0 )
			continue;

		// [AK] Don't respawn the player where they died if their body is partially inside a crusher.
		for ( msecnode_t *snode = mo->touching_sectorlist; snode; snode = snode->m_snext )
		{
			if ( snode->m_sector == pFloor->GetSector( ))
				return false;
		}
	}

	return true;
}

//*****************************************************************************
//
bool PLAYER_CannotAffectAllyWith( AActor *pActor1, AActor *pActor2, AActor *pInflictor, int flag )
{
	// [AK] Check if we have the corresponding zadmflag enabled.
	if (( zadmflags & flag ) == false )
		return false;

	// [RK] Voodoo dolls still need to be affected since they're not really an 'ally'.
	if ( pActor2 && pActor2->player && (pActor2->player->mo != pActor2 ))
		return false;

	// [AK] If the inflicting actor (e.g. projectile) is forced to affect allied players
	// then don't bother checking.
	if (( pInflictor != NULL ) && ( pInflictor->STFlags & STFL_FORCEALLYCOLLISION ))
		return false;

	// [AK] One of the actors must be a player, at least.
	if (( pActor1 && pActor2 ) && ( pActor1 != pActor2 ) && ( pActor1->player || pActor2->player ))
	{
		// [AK] Check if the other actor is a teammate of the first actor.
		if ( pActor1->IsTeammate( pActor2 ))
			return true;

		// [AK] Check if the other actor is a friend of the first actor. One of these actors must not be a player.
		if (( pActor1->player == NULL || pActor2->player == NULL ) && ( pActor1->IsFriend( pActor2 )))
			return true;
	}

	return false;
}

//*****************************************************************************
//
LONG PLAYER_CalcSpread( ULONG ulPlayer )
{
	ULONG ulFlags = GAMEMODE_GetCurrentFlags( );
	LONG lHighestScore = 0;
	bool bInit = true;

	// First, find the highest fragcount that isn't ours.
	for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if (( ulPlayer == ulIdx ) || ( playeringame[ulIdx] == false ) || ( PLAYER_IsTrueSpectator( &players[ulIdx] )))
			continue;

		if (( ulFlags & GMF_PLAYERSEARNWINS ) && (( bInit ) || ( players[ulIdx].ulWins > static_cast<ULONG>( lHighestScore ))))
		{
			lHighestScore = players[ulIdx].ulWins;
			bInit = false;
		}
		else if (( ulFlags & GMF_PLAYERSEARNPOINTS ) && (( bInit ) || ( players[ulIdx].lPointCount > lHighestScore )))
		{
			lHighestScore = players[ulIdx].lPointCount;
			bInit = false;
		}
		else if (( ulFlags & GMF_PLAYERSEARNFRAGS ) && (( bInit ) || ( players[ulIdx].fragcount > lHighestScore )))
		{
			lHighestScore = players[ulIdx].fragcount;
			bInit = false;
		}
	}

	// [AK] Return the difference between our score and the highest score.
	if ( bInit == false )
	{
		if ( ulFlags & GMF_PLAYERSEARNWINS )
			return ( players[ulPlayer].ulWins - lHighestScore );
		else if ( ulFlags & GMF_PLAYERSEARNPOINTS )
			return ( players[ulPlayer].lPointCount - lHighestScore );
		else
			return ( players[ulPlayer].fragcount - lHighestScore );
	}

	// [AK] If we're the only person in the game just return zero.
	return ( 0 );
}

//*****************************************************************************
//
ULONG PLAYER_CalcRank( ULONG ulPlayer )
{
	ULONG ulFlags = GAMEMODE_GetCurrentFlags( );
	ULONG ulRank = 0;

	for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if (( ulIdx == ulPlayer ) || ( playeringame[ulIdx] == false ) || ( PLAYER_IsTrueSpectator( &players[ulIdx] )))
			continue;

		if (( ulFlags & GMF_PLAYERSEARNWINS ) && ( players[ulIdx].ulWins > players[ulPlayer].ulWins ))
			ulRank++;
		else if (( ulFlags & GMF_PLAYERSEARNPOINTS ) && ( players[ulIdx].lPointCount > players[ulPlayer].lPointCount ))
			ulRank++;
		else if (( ulFlags & GMF_PLAYERSEARNFRAGS ) && ( players[ulIdx].fragcount > players[ulPlayer].fragcount ))
			ulRank++;
	}

	return ( ulRank );
}

//*****************************************************************************
//
void PLAYER_ScaleDamageCountWithMaxHealth( player_t *pPlayer, int &damage )
{
	// [AK] We'll only scale the damage count if the user wants to scale the intensity of the
	// blood based on the player's max health.
	// This doesn't work if the server wants to force max blood on the screen.
	if (( zadmflags & ZADF_MAX_BLOOD_SCALAR ) || ( blood_fade_usemaxhealth == false ))
		return;

	if (( pPlayer == NULL ) || ( pPlayer->mo == NULL ))
		return;

	const int maxHealth = pPlayer->mo->GetMaxHealth( );

	// [AK] The player's max health shouldn't be 100 if we want to scale the damage.
	if ( maxHealth != 100 )
		damage = ( damage * 100 ) / maxHealth;
}

//*****************************************************************************
//
void PLAYER_ResetCustomValues( const ULONG ulPlayer )
{
	// [AK] Don't do anything if there is no data.
	if ( gameinfo.CustomPlayerData.CountUsed( ) == 0 )
		return;

	TMapIterator<FName, PlayerData> it( gameinfo.CustomPlayerData );
	TMap<FName, PlayerData>::Pair *pair;

	while ( it.NextPair( pair ))
		pair->Value.ResetToDefault( ulPlayer, false );
}

//*****************************************************************************
//
CCMD (kill)
{
	// Only allow it in a level.
	if ( gamestate != GS_LEVEL )
		return;

	if (argv.argc() > 1)
	{
		// [BB] Special handling for "kill monsters" on the server: It's allowed
		// independent of the sv_cheats setting and bypasses the DEM_* handling.
		// [TP] Also handle the kill [class] case
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		{
			SERVER_KillCheat( argv[1] );
			return;
		}

		if (CheckCheatmode ())
			return;

		// [TP] If we're the client, ask the server to do this
		if ( NETWORK_GetState() == NETSTATE_CLIENT )
		{
			CLIENTCOMMANDS_KillCheat( argv[1] );
			return;
		}

		if (!stricmp (argv[1], "monsters"))
		{
			// Kill all the monsters
			if (CheckCheatmode ())
				return;

			Net_WriteByte (DEM_GENERICCHEAT);
			Net_WriteByte (CHT_MASSACRE);
		}
		else
		{
			Net_WriteByte (DEM_KILLCLASSCHEAT);
			Net_WriteString (argv[1]);
		}
	}
	else
	{
		// [BB] The server can't suicide, so it can ignore this checks.
		if ( NETWORK_GetState( ) != NETSTATE_SERVER )
		{
			// [BC] Don't let spectators kill themselves.
			if ( players[consoleplayer].bSpectating == true )
				return;

			// [BC] Don't allow suiciding during a duel.
			if ( duel && ( DUEL_GetState( ) == DS_INDUEL ))
			{
				Printf( "You cannot suicide during a duel.\n" );
				return;
			}
		}

		// If suiciding is disabled, then don't do it.
		if (dmflags2 & DF2_NOSUICIDE)
		{
			// [TP] Tell the user that this cannot be done.
			Printf( "Suiciding has been disabled.\n" );
			return;
		}

		// [BC] Tell the server we wish to suicide.
		if (( NETWORK_GetState( ) == NETSTATE_CLIENT ) && ( players[consoleplayer].bSpectating == false ))
			CLIENTCOMMANDS_Suicide( );

		// Kill the player
		Net_WriteByte (DEM_SUICIDE);
	}
	C_HideConsole ();
}

//*****************************************************************************
//
CCMD( spectate )
{
	// [BB] When playing a demo enter free spectate mode.
	if ( CLIENTDEMO_IsPlaying( ) == true )
	{
		C_DoCommand( "demo_spectatefreely" );
		return;
	}

	// [BB] The server can't use this.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
	{
		Printf ( "CCMD spectate can't be used on the server\n" );
		return;
	}

	// If we're a client, inform the server that we wish to spectate.
	// [BB] This also serves as way to leave the join queue.
	if ( NETWORK_GetState( ) == NETSTATE_CLIENT )
	{
		CLIENTCOMMANDS_Spectate( );
		return;
	}

	// Already a spectator!
	if ( PLAYER_IsTrueSpectator( &players[consoleplayer] ))
	{
		// [AK] If the local player is in the join queue, then remove them.
		if ( JOINQUEUE_GetPositionInLine( consoleplayer ) != -1 )
		{
			JOINQUEUE_RemovePlayerFromQueue( consoleplayer );
			Printf( "You have been removed from the join queue.\n" );
		}

		return;
	}

	// Make the player a spectator.
	PLAYER_SetSpectator( &players[consoleplayer], true, false );
}

//*****************************************************************************
//
CCMD( taunt )
{
	// [BB] No taunting while playing a demo.
	if ( CLIENTDEMO_IsPlaying( ) == true )
	{
		Printf ( "You can't taunt during demo playback.\n" );
		return;
	}

	if ( PLAYER_Taunt( &players[consoleplayer] ))
	{
		// Tell the server we taunted!
		if ( NETWORK_GetState( ) == NETSTATE_CLIENT )
			CLIENTCOMMANDS_Taunt( );

		if ( CLIENTDEMO_IsRecording( ))
			CLIENTDEMO_WriteLocalCommand( CLD_LCMD_TAUNT, NULL );
	}
}
