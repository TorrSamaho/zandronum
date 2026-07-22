/*
** d_dehacked.h
**
**---------------------------------------------------------------------------
** Copyright 1998-2006 Randy Heit
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#ifndef __D_DEHACK_H__
#define __D_DEHACK_H__

#include "a_pickups.h"

class ADehackedPickup : public AInventory
{
	DECLARE_CLASS (ADehackedPickup, AInventory)
	HAS_OBJECT_POINTERS
public:
	void Destroy ();
	const char *PickupMessage ();
	bool ShouldRespawn ();
	bool ShouldStay ();
	bool TryPickup (AActor *&toucher);
	void PlayPickupSound (AActor *toucher);
	void DoPickupSpecial (AActor *toucher);
	void Serialize(FArchive &arc);
private:
	const PClass *DetermineType ();
	AInventory *RealPickup;
public:
	bool droppedbymonster;
	// [RK] Accessor
	AInventory* GetRealPickup();
};

int D_LoadDehLumps();
bool D_LoadDehLump(int lumpnum);
bool D_LoadDehFile(const char *filename);
void FinishDehPatch ();

// [LZ] MBF21 thing flags, as defined by the spec. These are translated to the
// engine's native flags and properties; the helpers below are shared between
// the DeHackEd parser and the MBF21 runtime codepointers (A_AddFlags & co).
enum
{
	DEH21F_LOGRAV			= 0x00000001,	// low gravity (1/8)
	DEH21F_SHORTMRANGE		= 0x00000002,	// short missile range (archvile)
	DEH21F_DMGIGNORED		= 0x00000004,	// other things ignore its attacks (archvile)
	DEH21F_NORADIUSDMG		= 0x00000008,	// doesn't take splash damage
	DEH21F_FORCERADIUSDMG	= 0x00000010,	// causes splash damage even if target shouldn't
	DEH21F_HIGHERMPROB		= 0x00000020,	// higher missile attack probability (cyberdemon)
	DEH21F_RANGEHALF		= 0x00000040,	// use half distance for missile attack probability
	DEH21F_NOTHRESHOLD		= 0x00000080,	// no targeting threshold (archvile)
	DEH21F_LONGMELEE		= 0x00000100,	// long melee range (revenant)
	DEH21F_BOSS				= 0x00000200,	// full volume see/death sound and splash immunity
	DEH21F_MAP07BOSS1		= 0x00000400,	// tag 666 boss on doom 2 map 7
	DEH21F_MAP07BOSS2		= 0x00000800,	// tag 667 boss on doom 2 map 7
	DEH21F_E1M8BOSS			= 0x00001000,	// E1M8 boss
	DEH21F_E2M8BOSS			= 0x00002000,	// E2M8 boss
	DEH21F_E3M8BOSS			= 0x00004000,	// E3M8 boss
	DEH21F_E4M6BOSS			= 0x00008000,	// E4M6 boss
	DEH21F_E4M8BOSS			= 0x00010000,	// E4M8 boss
	DEH21F_RIP				= 0x00020000,	// ripper projectile
	DEH21F_FULLVOLSOUNDS	= 0x00040000,	// full volume see/death sounds

	DEH21F_ALLFLAGS			= 0x0007ffff,
};

// Applies (or removes) a combination of MBF21 flags on an actor.
void DEH_ChangeMBF21Flags (AActor *actor, DWORD bits, bool set);
// Checks whether all given MBF21 flags are set on an actor.
bool DEH_CheckMBF21Flags (AActor *actor, DWORD bits);
// Same pair for the vanilla Doom thing flags word used by the MBF21
// A_AddFlags/A_RemoveFlags/A_JumpIfFlagsSet codepointers.
void DEH_ChangeVanillaFlags (AActor *actor, DWORD bits, bool set);
bool DEH_CheckVanillaFlags (AActor *actor, DWORD bits);

// [TP] & [Zalewa]
TArray<FString> D_GetDehFileNames();
const TArray<FString>& D_GetDehFiles( );

#endif //__D_DEHACK_H__
