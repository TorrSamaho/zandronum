#pragma once

//==========================================================================
//
// Standard pointer acquisition functions
//
// Possible effective results at run-time
//   assigntovariable = NULL (or a RETURN statement is issued)
//   P_BulletSlope(pointer_owner, &temporary), assigntovariable = temporary
//   assigntovariable = pointer_owner->target or ...->master or ...->tracer
//
//==========================================================================

class AActor;

// Pointer selectors (enum)

enum AAPTR
{
	AAPTR_DEFAULT = 0,
	AAPTR_NULL = 0x1,
	AAPTR_TARGET = 0x2,
	AAPTR_MASTER = 0x4,
	AAPTR_TRACER = 0x8,

	AAPTR_PLAYER_GETTARGET = 0x10,
	AAPTR_PLAYER_GETCONVERSATION = 0x20,

	AAPTR_PLAYER1 = 0x40,
	AAPTR_PLAYER2 = 0x80,
	AAPTR_PLAYER3 = 0x100,
	AAPTR_PLAYER4 = 0x200,
	AAPTR_PLAYER5 = 0x400,
	AAPTR_PLAYER6 = 0x800,
	AAPTR_PLAYER7 = 0x1000,
	AAPTR_PLAYER8 = 0x2000,

	AAPTR_FRIENDPLAYER = 0x4000,

	// [AK] Pointers to the floaty icon above the player's head and the actor that
	// the player is looking from respectively.
	AAPTR_PLAYER_GETFLOATYICON = 0x4000000,
	AAPTR_PLAYER_GETCAMERA = 0x8000000,

	// [AK] Pointers to an ACS script's source, inflictor, and target actors. These
	// are only useful in event scripts triggered by GAMEEVENT_ACTOR_DAMAGED.
	AAPTR_DAMAGE_SOURCE = 0x10000000,
	AAPTR_DAMAGE_INFLICTOR = 0x40000000,
	AAPTR_DAMAGE_TARGET = 0x80000000,

	AAPTR_DAMAGE_SELECTORS =
		AAPTR_DAMAGE_SOURCE|AAPTR_DAMAGE_INFLICTOR|AAPTR_DAMAGE_TARGET,

	// [AK] Added AAPTR_PLAYER_GETFLOATYICON and AAPTR_PLAYER_GETCAMERA to the bitfield.
	AAPTR_PLAYER_SELECTORS =
		AAPTR_PLAYER_GETTARGET|AAPTR_PLAYER_GETCONVERSATION|
		AAPTR_PLAYER_GETFLOATYICON|AAPTR_PLAYER_GETCAMERA,

	AAPTR_GENERAL_SELECTORS =
		AAPTR_TARGET|AAPTR_MASTER|AAPTR_TRACER|AAPTR_FRIENDPLAYER,

	AAPTR_STATIC_SELECTORS =
		AAPTR_PLAYER1|AAPTR_PLAYER2|AAPTR_PLAYER3|AAPTR_PLAYER4|
		AAPTR_PLAYER5|AAPTR_PLAYER6|AAPTR_PLAYER7|AAPTR_PLAYER8|
		AAPTR_NULL

};

/*
	COPY_AAPTR

	Result overview in order of priority:

	1. Caller is player and a player specific selector is specified: Player specific selector is used.
	2. Caller is non-null and a general actor selector is specified: General actor selector is used.
	3. A static actor selector is specified: Static actor selector is used.
	4. The origin actor is used.

	Only one selector of each type can be used.
*/

AActor *COPY_AAPTR(AActor *origin, int selector);

// Use COPY_AAPTR_NOT_NULL to return from a function if the pointer is NULL
#define COPY_AAPTR_NOT_NULL(source, destination, selector) { destination = COPY_AAPTR(source, selector); if (!destination) return; }



enum PTROP
 {
	PTROP_UNSAFETARGET = 1,
	PTROP_UNSAFEMASTER = 2,
	PTROP_NOSAFEGUARDS = PTROP_UNSAFETARGET|PTROP_UNSAFEMASTER
};


// [FDARI] Exported logic for guarding against loops in Target (for missiles) and Master (for all) chains.
// It is called from multiple locations.
// The code may be in need of optimisation.


//==========================================================================
//
// Checks whether this actor is a missile
// Unfortunately this was buggy in older versions of the code and many
// released DECORATE monsters rely on this bug so it can only be fixed
// with an optional flag
//
//==========================================================================

void VerifyTargetChain(AActor *self, bool preciseMissileCheck=true);
void VerifyMasterChain(AActor *self);
void ASSIGN_AAPTR(AActor *toActor, int toSlot, AActor *ptr, int flags) ;

