//-----------------------------------------------------------------------------
//
// Skulltag Source
// Copyright (C) 2004-2005 Brad Carney
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
// Date created:  8/15/05
//
//
// Filename: callvote.h
//
// Description: 
//
//-----------------------------------------------------------------------------

#ifndef	__CALLVOTE_H__
#define	__CALLVOTE_H__

#include "c_cvars.h"
#include "network.h"

//*****************************************************************************
//	DEFINES

#define	VOTE_COUNTDOWN_TIME				15
#define	VOTE_PASSED_TIME				4

// [AK] Definitions for whether we (the local player) voted yes or no.
#define VOTE_UNDECIDED					0
#define VOTE_YES						1
#define VOTE_NO							2

//*****************************************************************************
enum
{
	FORBIDVOTE_KICK			= 1 << 0,
	FORBIDVOTE_FORCESPEC	= 1 << 1,
	FORBIDVOTE_MAP			= 1 << 2,
	FORBIDVOTE_CHANGEMAP	= 1 << 3,
	FORBIDVOTE_FRAGLIMIT	= 1 << 4,
	FORBIDVOTE_TIMELIMIT	= 1 << 5,
	FORBIDVOTE_WINLIMIT		= 1 << 6,
	FORBIDVOTE_DUELLIMIT	= 1 << 7,
	FORBIDVOTE_POINTLIMIT	= 1 << 8,
	FORBIDVOTE_FLAG			= 1 << 9,
	FORBIDVOTE_NEXTMAP		= 1 << 10,
	FORBIDVOTE_NEXTSECRET	= 1 << 11,
	FORBIDVOTE_RESETMAP		= 1 << 12,
};

//*****************************************************************************
enum
{
	VOTECMD_KICK,
	VOTECMD_FORCETOSPECTATE,
	VOTECMD_MAP,
	VOTECMD_CHANGEMAP,
	VOTECMD_FRAGLIMIT,
	VOTECMD_TIMELIMIT,
	VOTECMD_WINLIMIT,
	VOTECMD_DUELLIMIT,
	VOTECMD_POINTLIMIT,
	VOTECMD_FLAG,
	VOTECMD_NEXTMAP,
	VOTECMD_NEXTSECRET,
	VOTECMD_RESETMAP,

	// [AK] Custom vote definitions start after NUM_VOTECMDS
	// ( i.e. the first one is indexed with NUM_VOTECMDS + 1 ).
	NUM_VOTECMDS
};

//*****************************************************************************
typedef enum
{
	VOTESTATE_NOVOTE,
	VOTESTATE_INVOTE,
	VOTESTATE_VOTECOMPLETED,

} VOTESTATE_e;

struct VOTETYPE_s
{
	enum class parametertype_e
	{
		NONE,
		INT,
		FLOAT,
		STRING,
		PLAYER,
		MAP
	};
	FString name;
	FString displayName;
	FString menu;
	FString menuName;
	FString scriptName;
	FString preflightScript;
	FString forbidCvarName;
	parametertype_e parameterType = parametertype_e::NONE;
};


//*****************************************************************************
//	STRUCTURES

typedef struct
{
	// The vote caller's address.
	NETADDRESS_s	Address;

	// Time that this vote was called.
	time_t			tTimeCalled;

	// The type of vote (see NUM_VOTECMDS).
	ULONG			ulVoteType;
	
	// Parameter of the vote ("map01", "50", etc).
	FString			fsParameter;

	// For kick votes: the address being kicked.
	NETADDRESS_s	KickAddress;

	// Was it passed?
	bool			bPassed;

} VOTE_s;

//*****************************************************************************
//	PROTOTYPES

void			CALLVOTE_Construct( void );
void			CALLVOTE_ReadVoteInfo( void );
const TArray<VOTETYPE_s> &CALLVOTE_GetCustomVotes( void );
void			CALLVOTE_Tick( void );
void			CALLVOTE_Render( void );
void			CALLVOTE_RenderClassic( void );
void			CALLVOTE_BeginVote( FString Command, FString Parameters, FString Reason, ULONG ulPlayer );
void			CALLVOTE_ClearVote( void );
bool			CALLVOTE_VoteYes( ULONG ulPlayer );
bool			CALLVOTE_VoteNo( ULONG ulPlayer );
ULONG			CALLVOTE_CountNumEligibleVoters( void );
void			CALLVOTE_EndVote( bool bPassed );

const char		*CALLVOTE_GetVoteMessage( void );
const char		*CALLVOTE_GetReason( void );
void			CALLVOTE_DisconnectedVoter( ULONG ulPlayer );
void			CALLVOTE_TallyVotes( void );
ULONG			CALLVOTE_GetYesVoteCount( void );
ULONG			CALLVOTE_GetNoVoteCount( void );
ULONG			CALLVOTE_GetVoteCaller( void );
VOTESTATE_e		CALLVOTE_GetVoteState( void );
ULONG			CALLVOTE_GetCountdownTicks( void );
ULONG			*CALLVOTE_GetPlayersWhoVotedYes( void );
ULONG			*CALLVOTE_GetPlayersWhoVotedNo( void );
bool			CALLVOTE_ShouldShowVoteScreen( void );
ULONG			CALLVOTE_GetPlayerVoteChoice( ULONG ulPlayer );
const VOTETYPE_s *CALLVOTE_GetCustomVoteTypeDefinition( ULONG ulVoteType );
void			CALLVOTE_ConvertCustomVoteParameter( const VOTETYPE_s *customVoteType, FString &Parameters );

//*****************************************************************************
//	EXTERNAL CONSOLE VARIABLES

EXTERN_CVAR( Int, sv_minvoters );
EXTERN_CVAR( Int, sv_forbidvoteflags );
EXTERN_CVAR( Int, sv_nocallvote )
EXTERN_CVAR( Int, sv_votecooldown );
EXTERN_CVAR( Int, sv_voteconnectwait );
EXTERN_CVAR( Bool, cl_showfullscreenvote )
EXTERN_CVAR( Bool, cl_hidevotescreen );
#endif	// __CALLVOTE_H__
