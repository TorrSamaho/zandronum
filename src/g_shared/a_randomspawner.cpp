/*
** a_randomspawner.cpp
** A thing that randomly spawns one item in a list of many, before disappearing.
** bouncecount is used to keep track of recursions (so as to prevent infinite loops).
** Species is used to store the index of the spawned actor's name.
*/

#include "actor.h"
#include "info.h"
#include "m_random.h"
#include "p_local.h"
#include "p_enemy.h"
#include "s_sound.h"
#include "statnums.h"
#include "gstrings.h"
#include "a_action.h"
#include "thingdef/thingdef.h"
// [BB] New #includes.
#include "cl_demo.h"
#include "sv_commands.h"

#define MAX_RANDOMSPAWNERS_RECURSION 32 // Should be largely more than enough, honestly.
static FRandom pr_randomspawn("RandomSpawn");

class ARandomSpawner : public AActor
{
	DECLARE_CLASS (ARandomSpawner, AActor)

	// To handle "RandomSpawning" missiles, the code has to be split in two parts.
	// If the following code is not done in BeginPlay, missiles will use the
	// random spawner's velocity (0...) instead of their own.
	void BeginPlay()
	{
		FDropItem *di;   // di will be our drop item list iterator
		FDropItem *drop; // while drop stays as the reference point.
		int n=0;
		bool nomonsters = (dmflags & DF_NO_MONSTERS) || (level.flags2 & LEVEL2_NOMONSTERS);

		Super::BeginPlay();
		// [BB] This is server-side.
		if ( NETWORK_InClientMode() )
		{
			if (( this->NetworkFlags & NETFL_CLIENTSIDEONLY ) == false )
				return;
		}

		// [BB] If the server handles the spawner, the client doesn't need know about it.
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		{
			this->NetworkFlags |= NETFL_SERVERSIDEONLY;
			this->FreeNetID();
		}

		drop = di = GetDropItems();
		if (di != NULL)
		{
			while (di != NULL)
			{
				if (di->Name != NAME_None)
				{
					if (!nomonsters || !(GetDefaultByType(PClass::FindClass(di->Name))->flags3 & MF3_ISMONSTER))
					{
						if (di->amount < 0) di->amount = 1; // default value is -1, we need a positive value.
						n += di->amount; // this is how we can weight the list.
					}
					di = di->Next;
				}
			}
			if (n == 0)
			{ // Nothing left to spawn. They must have all been monsters, and monsters are disabled.
				Destroy();
				return;
			}
			// Then we reset the iterator to the start position...
			di = drop;
			// Take a random number...
			n = pr_randomspawn(n);
			// And iterate in the array up to the random number chosen.
			while (n > -1 && di != NULL)
			{
				if (di->Name != NAME_None &&
					(!nomonsters || !(GetDefaultByType(PClass::FindClass(di->Name))->flags3 & MF3_ISMONSTER)))
				{
					n -= di->amount;
					if ((di->Next != NULL) && (n > -1))
						di = di->Next;
					else
						n = -1;
				}
				else
				{
					di = di->Next;
				}
			}
			// So now we can spawn the dropped item.
			if (di == NULL || bouncecount >= MAX_RANDOMSPAWNERS_RECURSION)	// Prevents infinite recursions
			{
				Spawn("Unknown", x, y, z, NO_REPLACE);		// Show that there's a problem.
				Destroy();
				return;
			}
			else if (pr_randomspawn() <= di->probability)	// prob 255 = always spawn, prob 0 = never spawn.
			{
				// Handle replacement here so as to get the proper speed and flags for missiles
				const PClass *cls;
				cls = PClass::FindClass(di->Name);
				if (cls != NULL)
				{
					const PClass *rep = cls->GetReplacement();
					if (rep != NULL)
					{
						cls = rep;
					}
				}
				if (cls != NULL)
				{
					Species = cls->TypeName;
					AActor *defmobj = GetDefaultByType(cls);
					this->Speed   =  defmobj->Speed;
					this->flags  |= (defmobj->flags  & MF_MISSILE);
					this->flags2 |= (defmobj->flags2 & MF2_SEEKERMISSILE);
					this->flags4 |= (defmobj->flags4 & MF4_SPECTRAL);
				}
				else
				{
					Species = NAME_None;
				}
			}
		}
	}

	// The second half of random spawning. Now that the spawner is initialized, the
	// real actor can be created. If the following code were in BeginPlay instead,
	// missiles would not have yet obtained certain information that is absolutely
	// necessary to them -- such as their source and destination.
	void PostBeginPlay()
	{
		AActor * newmobj = NULL;
		bool boss = false;
		Super::PostBeginPlay();
		if (Species == NAME_None) 
		{ 
			Destroy(); 
			return; 
		}
		const PClass * cls = PClass::FindClass(Species);
		if (this->flags & MF_MISSILE && target && target->target) // Attempting to spawn a missile.
		{
			if ((tracer == NULL) && (flags2 & MF2_SEEKERMISSILE)) tracer = target->target;
			newmobj = P_SpawnMissileXYZ(x, y, z, target, target->target, cls, false);
		}
		else newmobj = Spawn(cls, x, y, z, NO_REPLACE);
		if (newmobj != NULL)
		{
			// copy everything relevant
			newmobj->SpawnAngle = newmobj->angle = angle;
			newmobj->SpawnPoint[2] = SpawnPoint[2];
			newmobj->special    = special;
			newmobj->args[0]    = args[0];
			newmobj->args[1]    = args[1];
			newmobj->args[2]    = args[2];
			newmobj->args[3]    = args[3];
			newmobj->args[4]    = args[4];
			newmobj->special1   = special1;
			newmobj->special2   = special2;
			newmobj->SpawnFlags = SpawnFlags & ~MTF_SECRET;	// MTF_SECRET needs special treatment to avoid incrementing the secret counter twice. It had already been processed for the spawner itself.
			newmobj->HandleSpawnFlags();
			newmobj->SpawnFlags = SpawnFlags;
			newmobj->tid        = tid;
			newmobj->AddToHash();
			newmobj->velx = velx;
			newmobj->vely = vely;
			newmobj->velz = velz;
			newmobj->master = master;	// For things such as DamageMaster/DamageChildren, transfer mastery.
			newmobj->target = target;
			newmobj->tracer = tracer;
			newmobj->CopyFriendliness(this, false);
			// This handles things such as projectiles with the MF4_SPECTRAL flag that have
			// a health set to -2 after spawning, for internal reasons.
			if (health != SpawnHealth()) newmobj->health = health;
			if (!(flags & MF_DROPPED)) newmobj->flags &= ~MF_DROPPED;
			// Handle special altitude flags
			if (newmobj->flags & MF_SPAWNCEILING)
			{
				newmobj->z = newmobj->ceilingz - newmobj->height - SpawnPoint[2];
			}
			else if (newmobj->flags2 & MF2_SPAWNFLOAT) 
			{
				fixed_t space = newmobj->ceilingz - newmobj->height - newmobj->floorz;
				if (space > 48*FRACUNIT)
				{
					space -= 40*FRACUNIT;
					newmobj->z = MulScale8 (space, pr_randomspawn()) + newmobj->floorz + 40*FRACUNIT;
				}
				newmobj->z += SpawnPoint[2];
			}
			if (newmobj->flags & MF_MISSILE)
				P_CheckMissileSpawn(newmobj, 0);
			// Bouncecount is used to count how many recursions we're in.
			if (newmobj->IsKindOf(PClass::FindClass("RandomSpawner")))
				newmobj->bouncecount = ++bouncecount;
			// If the spawned actor has either of those flags, it's a boss.
			if ((newmobj->flags4 & MF4_BOSSDEATH) || (newmobj->flags2 & MF2_BOSS))
				boss = true;
			// If a replaced actor has either of those same flags, it's also a boss.
			AActor * rep = GetDefaultByType(GetClass()->ActorInfo->GetReplacee()->Class);
			if (rep && ((rep->flags4 & MF4_BOSSDEATH) || (rep->flags2 & MF2_BOSS)))
				boss = true;

			// [AK] Indicate that the spawned actor was spawned randomly.
			newmobj->STFlags |= STFL_RANDOMSPAWNED;

			// [AK] If the spawner was spawned by the level, then if the spawned actor triggers
			// GAMEEVENT_ACTOR_SPAWNED, arg1 should be "true" (i.e. treat the spawned actor as
			// being spawned by the level too). This flag will be enabled temporarily, then
			// disabled in AActor::PostBeginPlay.
			if ( this->STFlags & STFL_LEVELSPAWNED )
				newmobj->STFlags |= STFL_LEVELSPAWNED;

			// [BB] If we're the server, tell clients to spawn the actor.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			{
				SERVERCOMMANDS_SpawnThing( newmobj );
				// [BB] Also set the angle and velocity if necessary.
				SERVER_SetThingNonZeroAngleAndVelocity( newmobj );
			}
			// [BB] The client did the spawning, so this has to be a client side only actor.
			else if ( NETWORK_InClientMode() )
				newmobj->NetworkFlags |= NETFL_CLIENTSIDEONLY;
		}
		if (boss)
			this->tracer = newmobj;
		// [BB] Only destroy the actor if it's not needed for a map reset. Otherwise just hide it.
		else	// "else" because a boss-replacing spawner must wait until it can call A_BossDeath.
			HideOrDestroyIfSafe();

		// [BB] Workaround to ensure that the spawner is properly reset in GAME_ResetMap.
		this->STFlags |= STFL_POSITIONCHANGED;
	}

	void Tick()	// This function is needed for handling boss replacers
	{
		Super::Tick();
		if (tracer == NULL || tracer->health <= 0)
		{
			// [BB] Don't do this on actors that already have been hidden by HideOrDestroyIfSafe()
			if ( ( this->STFlags & STFL_HIDDEN_INSTEAD_OF_DESTROYED ) == false ) {
				CALL_ACTION(A_BossDeath, this);
				// [BB] Only destroy the actor if it's not needed for a map reset. Otherwise just hide it.
				HideOrDestroyIfSafe();
			}
		}
	}

};

IMPLEMENT_CLASS (ARandomSpawner)

