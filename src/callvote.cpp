//-----------------------------------------------------------------------------
//
// Skulltag Source
// Copyright (C) 2003-2005 Brad Carney
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
// Filename: callvote.cpp
//
// Description: Handles client-called votes.
//
// Possible improvements:
//	- Remove all the Yes( ) and No( ) code duplication. (suggested by Rivecoder)
//
//-----------------------------------------------------------------------------

#include "announcer.h"
#include "c_dispatch.h"
#include "cl_commands.h"
#include "cl_demo.h"
#include "cl_main.h"
#include "callvote.h"
#include "doomstat.h"
#include "network.h"
#include "p_acs.h"
#include "templates.h"
#include "sbar.h"
#include "sv_commands.h"
#include "sv_main.h"
#include "v_video.h"
#include "maprotation.h"
#include "gamemode.h"
#include "c_bind.h"
#include "c_console.h"
#include "gi.h"
#include "st_hud.h"
#include <list>

static TArray<VOTETYPE_s> g_VoteTypeDefinitions;

//*****************************************************************************
//	VARIABLES

static	VOTESTATE_e				g_VoteState;
static	FString					g_VoteCommand;
static	FString					g_VoteParameters;
static	FString					g_VoteMessage;
static	FString					g_VoteReason;
static	ULONG					g_ulVoteCaller;
static	ULONG					g_ulVoteCountdownTicks = 0;
static	ULONG					g_ulVoteCompletedTicks = 0;
static	ULONG					g_ulShowVoteScreenTicks = 0;
static	bool					g_bVotePassed;
static	bool					g_bVoteCancelled;
static	ULONG					g_ulPlayersWhoVotedYes[(MAXPLAYERS / 2) + 1];
static	ULONG					g_ulPlayersWhoVotedNo[(MAXPLAYERS / 2) + 1];
static	NETADDRESS_s			g_KickVoteVictimAddress;
static	std::list<VOTE_s>		g_PreviousVotes;
static	ULONG					g_ulPlayerVoteChoice[MAXPLAYERS]; // [AK]
static	ULONG					g_ulNumYesVotes = 0; // [AK]
static	ULONG					g_ulNumNoVotes = 0; // [AK]

//*****************************************************************************
//	PROTOTYPES

static	void			callvote_EndVote( void );
static	bool			callvote_CheckForFlooding( FString &Command, FString &Parameters, ULONG ulPlayer );
static	bool			callvote_CheckValidity( FString &Command, FString &Parameters );
static	ULONG			callvote_GetVoteType( const char *pszCommand );
static	bool			callvote_IsKickVote( const ULONG ulVoteType );
static	bool			callvote_VoteRequiresParameter( const ULONG ulVoteType );
static	bool			callvote_IsFlagValid( const char *pszName );

//*****************************************************************************
//	FUNCTIONS

void CALLVOTE_Construct( void )
{
	// Calling this function initialized everything.
	CALLVOTE_ClearVote( );
}

//*****************************************************************************
//
// [TP] Read VOTEINFO lumps and parse vote type definitions
//
void CALLVOTE_ReadVoteInfo( void )
{
	// [TP] Read custom vote definitions
	int lump, lastlump = 0;
	while ( ( lump = Wads.FindLump( "VOTEINFO", &lastlump ) ) != -1 )
	{
		FScanner sc ( lump );
		sc.SetCMode( true );

		while (sc.GetToken())
		{
			VOTETYPE_s newVoteType;
			if ( sc.Compare( "votetype" ) == false )
			{
				sc.ScriptError( "Expected \"votetype\", got \"%s\"", sc.String );
			}
			sc.MustGetToken( TK_Identifier );
			newVoteType.name = sc.String;
			if ( callvote_GetVoteType( newVoteType.name ) != NUM_VOTECMDS )
			{
				sc.ScriptError( "Vote type \"%s\" already exists", newVoteType.name.GetChars() );
			}
			sc.MustGetToken( '{' );
			while ( sc.CheckToken( '}' ) == false )
			{
				sc.MustGetAnyToken();
				if ( ( sc.TokenType == TK_Action ) || ( ( sc.TokenType == TK_Identifier ) && sc.Compare( "PreflightAction" ) ) )
				{
					const bool isAction = ( sc.TokenType == TK_Action );
					sc.MustGetToken( '=' );
					sc.MustGetToken( TK_Identifier );
					if ( sc.Compare( "CallACS" ) == false )
					{
						sc.ScriptError( "%s must be CallACS", isAction ? "Action" : "PreflightAction" );
					}
					sc.MustGetToken( '(' );
					sc.MustGetToken( TK_StringConst );
					if ( isAction )
						newVoteType.scriptName = sc.String;
					else
						newVoteType.preflightScript = sc.String;
					sc.MustGetToken( ')' );
					sc.MustGetToken( ';' );
				}
				else if ( ( sc.TokenType == TK_Identifier ) && sc.Compare( "ForbidCVar" ) )
				{
					sc.MustGetToken( '=' );
					sc.MustGetToken( TK_Identifier );
					FBaseCVar* const cvar = FindCVar( sc.String, nullptr );
					if ( cvar == nullptr )
					{
						sc.ScriptError( "CVar not found: \"%s\".\n"
							"If you want this to be a new CVar, please put the following into a CVARINFO lump:\n"
							"server bool %s = false;", sc.String, sc.String );
					}
					newVoteType.forbidCvarName = sc.String;
					sc.MustGetToken( ';' );
				}
				else if ( ( sc.TokenType == TK_Identifier ) && sc.Compare( "Arg" ) )
				{
					sc.MustGetToken( '=' );
					sc.MustGetAnyToken();
					if ( sc.TokenType == TK_Int )
					{
						newVoteType.parameterType = VOTETYPE_s::parametertype_e::INT;
					}
					else if ( sc.TokenType == TK_Float )
					{
						newVoteType.parameterType = VOTETYPE_s::parametertype_e::FLOAT;
					}
					else if ( sc.TokenType == TK_Identifier && sc.Compare( "Str" ) )
					{
						newVoteType.parameterType = VOTETYPE_s::parametertype_e::STRING;
					}
					else if ( sc.TokenType == TK_Identifier && sc.Compare( "Player" ) )
					{
						newVoteType.parameterType = VOTETYPE_s::parametertype_e::PLAYER;
					}
					else if ( sc.TokenType == TK_Map )
					{
						newVoteType.parameterType = VOTETYPE_s::parametertype_e::MAP;
					}
					else
					{
						sc.ScriptError( "Unknown vote parameter type \"%s\"", sc.String );
					}
					sc.MustGetToken(';');
				}
				else if ( ( sc.TokenType == TK_Identifier ) && sc.Compare( "DisplayName" ) )
				{
					sc.MustGetToken( '=' );
					sc.MustGetToken( TK_StringConst );
					if ( sc.StringLen == 0 )
					{
						sc.ScriptError( "DisplayName cannot be empty." );
					}
					newVoteType.displayName = sc.String;
					sc.MustGetToken( ';' );
				}
				else if ( ( sc.TokenType == TK_Identifier ) && sc.Compare( "Menu" ) )
				{
					sc.MustGetToken( '=' );
					sc.MustGetToken( TK_Identifier );
					newVoteType.menu = sc.String;
					if ( sc.CheckToken( ',' ) )
					{
						sc.MustGetToken( TK_StringConst );
						if ( sc.StringLen == 0 )
						{
							sc.ScriptError( "Menu title cannot be empty." );
						}
						newVoteType.menuName = sc.String;
					}
					sc.MustGetToken( ';' );
				}
				else
				{
					sc.ScriptError( "Unknown key \"%s\" in \"%s\"", sc.String, newVoteType.name.GetChars() );
				}
			}
			if ( newVoteType.scriptName.IsEmpty() )
			{
				sc.ScriptError( "Vote type \"%s\" does not have ScriptName", newVoteType.name.GetChars() );
			}
			if ( newVoteType.displayName.IsEmpty() )
			{
				newVoteType.displayName = newVoteType.name;
			}
			if ( ( newVoteType.menuName.IsEmpty() ) && ( newVoteType.menu.IsNotEmpty() ) )
			{
				newVoteType.menuName = newVoteType.displayName;
			}
			g_VoteTypeDefinitions.Push( newVoteType );
		}
	}
}

//*****************************************************************************
//
// [TRSR] Returns a reference of the defined custom votes for use elsewhere.
//
const TArray<VOTETYPE_s> &CALLVOTE_GetCustomVotes( void )
{
	return g_VoteTypeDefinitions;
}

//*****************************************************************************
//
// [TP] If ulVoteType is a custom vote type, returns the definition of
// that vote type, a null pointer otherwise.
//
const VOTETYPE_s* CALLVOTE_GetCustomVoteTypeDefinition( ULONG ulVoteType )
{
	const ULONG index = ulVoteType - NUM_VOTECMDS - 1;
	if ( ( index >= 0 ) && ( index < g_VoteTypeDefinitions.Size() ) )
	{
		return &g_VoteTypeDefinitions[index];
	}
	else
	{
		return nullptr;
	}
}

//*****************************************************************************
//
// [TRSR] Converts specific custom vote parameters to easier to deal with
// forms early in the voting process.
//
void CALLVOTE_ConvertCustomVoteParameter( const VOTETYPE_s *customVoteType, FString &Parameters )
{
	if ( customVoteType == nullptr )
		return;

	// [TRSR] It's easier to treat invalid floats as 0, so that conversion is performed
	if ( customVoteType->parameterType == VOTETYPE_s::parametertype_e::FLOAT )
	{
		Parameters.Format( "%g", static_cast<float>( Parameters.ToDouble( ) ) );
	}
	// [TRSR] The parameter types below accept multiple types of input,
	// so we convert them to one type early to keep the logic downstream cleaner.
	else if ( customVoteType->parameterType == VOTETYPE_s::parametertype_e::PLAYER )
	{
		unsigned int idxFromName = SERVER_GetPlayerIndexFromName( Parameters.GetChars(), true, true );
		if ( idxFromName < MAXPLAYERS )
		{
			Parameters.Format( "%u", idxFromName );
		}
	}
	else if ( customVoteType->parameterType == VOTETYPE_s::parametertype_e::MAP )
	{
		if ( FindLevelInfo( Parameters.GetChars(), false ) == nullptr )
		{
			unsigned int mapIdx = atoi( Parameters.GetChars() );
			if ( ( mapIdx > 0 && mapIdx < wadlevelinfos.Size() ) || ( ( mapIdx == 0 ) && ( Parameters.Len() == 1 ) && ( Parameters.GetChars()[0] == '0' ) ) )
			{
				Parameters.Format( "%s", wadlevelinfos[mapIdx].mapname );
			}
		}
	}
}

//*****************************************************************************
//
void CALLVOTE_Tick( void )
{
	switch ( g_VoteState )
	{
	case VOTESTATE_NOVOTE:

		break;
	case VOTESTATE_INVOTE:

		// [RC] Hide the voteing screen shortly after voting.
		if ( g_ulShowVoteScreenTicks )
			g_ulShowVoteScreenTicks--;

		if ( g_ulVoteCountdownTicks )
		{
			g_ulVoteCountdownTicks--;
			if ( NETWORK_InClientMode() == false )
			{
				// [AK] If the current vote is for changing a flag, we must check if it's still valid to keep the vote active.
				if ( g_PreviousVotes.back().ulVoteType == VOTECMD_FLAG )
				{
					FString flagName = g_VoteCommand.Left( g_VoteCommand.IndexOf( ' ' ));
					FFlagCVar *flag = static_cast<FFlagCVar *>( FindCVar( flagName, NULL ));
					bool bEnable = !g_VoteCommand.Right( g_VoteCommand.Len() - ( strlen( flagName ) + 1 )).CompareNoCase( "true" );

					// [AK] Cancel the vote if the flag has already been changed to the desired value.
					if ( flag->GetGenericRep( CVAR_Bool ).Bool == bEnable )
					{
						SERVER_Printf( "%s has been changed so the vote has been cancelled.\n", flag->GetName() );
						g_bVoteCancelled = true;
					}
				}
				else
				{
					const VOTETYPE_s *customVoteType = CALLVOTE_GetCustomVoteTypeDefinition( g_PreviousVotes.back().ulVoteType );

					if (( customVoteType != nullptr ) && ( customVoteType->parameterType == VOTETYPE_s::parametertype_e::PLAYER ))
					{
						if ( PLAYER_IsValidPlayer( atoi( g_VoteParameters )) == false )
						{
							SERVER_Printf( "The player involved in the vote left the game, so the vote has been cancelled.\n" );
							g_bVoteCancelled = true;
						}
					}
				}

				if ( g_bVoteCancelled )
				{
					g_PreviousVotes.back().ulVoteType = NUM_VOTECMDS;
					g_bVotePassed = false;
					callvote_EndVote();
					return;
				}

				// [RK] Perform the final tally of votes.
				if ( g_ulVoteCountdownTicks == 0 )
					CALLVOTE_TallyVotes();
			}
		}
		break;
	case VOTESTATE_VOTECOMPLETED:

		if ( g_ulVoteCompletedTicks )
		{
			if ( --g_ulVoteCompletedTicks == 0 )
			{
				g_PreviousVotes.back( ).bPassed = g_bVotePassed;

				// If the vote passed, execute the command string.
				if (( g_bVotePassed ) && ( !g_bVoteCancelled ) &&
					( NETWORK_InClientMode() == false ))
				{
					// [TP] Custom votes call scripts instead
					const VOTETYPE_s *customVoteType = CALLVOTE_GetCustomVoteTypeDefinition( g_PreviousVotes.back().ulVoteType );
					if ( customVoteType != nullptr )
					{
						int arg;
						switch ( customVoteType->parameterType )
						{
						case VOTETYPE_s::parametertype_e::NONE:
							arg = 0;
							break;
						case VOTETYPE_s::parametertype_e::INT:
						case VOTETYPE_s::parametertype_e::PLAYER:
							arg = atoi( g_VoteParameters );
							break;
						case VOTETYPE_s::parametertype_e::FLOAT:
							arg = FLOAT2FIXED( static_cast<float>( g_VoteParameters.ToDouble( ) ) );
							break;
						case VOTETYPE_s::parametertype_e::MAP:
						case VOTETYPE_s::parametertype_e::STRING:
							arg = ACS_PushAndReturnDynamicString( g_VoteParameters );
							break;
						}
						P_StartScript( PLAYER_IsValidPlayer( g_ulVoteCaller ) ? players[g_ulVoteCaller].mo : nullptr, nullptr, -FName( customVoteType->scriptName ), level.mapname, &arg, 1, ACS_ALWAYS );
					}
					else
					{
						// [BB, RC] If the vote is a kick vote, we have to rewrite g_VoteCommand to both use the stored IP, and temporarily ban it.
						// [Dusk] Write the kick reason into the ban reason, [BB] but only if it's not empty.
						// [BB] "forcespec" votes need a similar handling.
						if ( ( strncmp( g_VoteCommand, "kick", 4 ) == 0 ) || ( strncmp( g_VoteCommand, "forcespec", 9 ) == 0 ) )
						{
							if ( strncmp( g_VoteCommand, "kick", 4 ) == 0 )
								g_VoteCommand.Format( "addban %s 10min \"Vote kick", g_KickVoteVictimAddress.ToString() );
							else
								g_VoteCommand.Format( "forcespec_idx %d \"Vote forcespec", static_cast<int>(SERVER_FindClientByAddress ( g_KickVoteVictimAddress )) );
							g_VoteCommand.AppendFormat( ", %u to %u", static_cast<unsigned int>(g_ulNumYesVotes), static_cast<unsigned int>(g_ulNumNoVotes) );
							if ( g_VoteReason.IsNotEmpty() )
								g_VoteCommand.AppendFormat ( " (%s)", g_VoteReason.GetChars( ) );
							g_VoteCommand += ".\"";
						}

						AddCommandString( (char *)g_VoteCommand.GetChars( ));
					}
				}
				// Reset the module.
				CALLVOTE_ClearVote( );
			}
		}
		break;
	}
}

//*****************************************************************************
//
// [RC] New compact version; RenderInVoteClassic is the fullscreen version
//
void CALLVOTE_Render( void )
{
	ULONG ulVoteChoice = CALLVOTE_GetPlayerVoteChoice( consoleplayer );
	FString text;

	// Render the title and time left.
	ULONG ulYPos = 8;
	text.Format( "VOTE NOW! ( %d )", static_cast<unsigned int>(( CALLVOTE_GetCountdownTicks( ) + TICRATE ) / TICRATE ));
	HUD_DrawTextCentered( BigFont, gameinfo.gametype == GAME_Doom ? CR_RED : CR_UNTRANSLATED, ulYPos, text, g_bScale );

	// Render the command being voted on.
	ulYPos += 14;
	HUD_DrawTextCentered( SmallFont, CR_WHITE, ulYPos, CALLVOTE_GetVoteMessage( ), g_bScale );

	ulYPos += 4;

	// Render the reason of the vote being voted on.
	if ( strlen( CALLVOTE_GetReason( )) > 0 )
	{
		ulYPos += 8;
		text.Format( "Reason: %s", CALLVOTE_GetReason( ));
		HUD_DrawTextCentered( SmallFont, CR_ORANGE, ulYPos, text, g_bScale );
	}

	ulYPos += 8;

	// Render the number of votes.
	text.Format( "%sYes: %d", ulVoteChoice == VOTE_YES ? TEXTCOLOR_YELLOW : "", static_cast<unsigned int>( CALLVOTE_GetYesVoteCount( )));
	text.AppendFormat( TEXTCOLOR_NORMAL ", %sNo: %d", ulVoteChoice == VOTE_NO ? TEXTCOLOR_YELLOW : "", static_cast<unsigned int>( CALLVOTE_GetNoVoteCount( )));
	HUD_DrawTextCentered( SmallFont, CR_DARKBROWN, ulYPos, text, g_bScale );

	// Render the explanation of keys.
	if ( ulVoteChoice == VOTE_UNDECIDED )
	{
		char keyVoteYes[16];
		C_FindBind( "vote_yes", keyVoteYes );

		char keyVoteNo[16];
		C_FindBind( "vote_no", keyVoteNo );

		ulYPos += 8;
		text.Format( "%s | %s", keyVoteYes, keyVoteNo );
		HUD_DrawTextCentered( SmallFont, CR_BLACK, ulYPos, text, g_bScale );
	}
}

//*****************************************************************************
//
void CALLVOTE_RenderClassic( void )
{
	ULONG *pulPlayersWhoVotedYes = CALLVOTE_GetPlayersWhoVotedYes( );
	ULONG *pulPlayersWhoVotedNo = CALLVOTE_GetPlayersWhoVotedNo( );
	ULONG ulMaxYesOrNoVoters = ( MAXPLAYERS / 2 ) + 1;
	ULONG ulPlayer;
	FString text;

	// Start with the "VOTE NOW!" title.
	ULONG ulYPos = 16;
	HUD_DrawTextCleanCentered( BigFont, gameinfo.gametype == GAME_Doom ? CR_RED : CR_UNTRANSLATED, ulYPos, "VOTE NOW!" );

	// Render who called the vote.
	ulYPos += 24;
	text.Format( "Vote called by: %s", players[CALLVOTE_GetVoteCaller( )].userinfo.GetName( ));
	HUD_DrawTextCleanCentered( SmallFont, CR_UNTRANSLATED, ulYPos, text );

	// Render the command being voted on.
	ulYPos += 16;
	HUD_DrawTextCleanCentered( SmallFont, CR_WHITE, ulYPos, CALLVOTE_GetVoteMessage( ));

	// Render the reason for the vote being voted on.
	if ( strlen( CALLVOTE_GetReason( )) > 0 )
	{
		ulYPos += 16;
		text.Format( "Reason: %s", CALLVOTE_GetReason( ));
		HUD_DrawTextCleanCentered( SmallFont, CR_ORANGE, ulYPos, text );
	}

	// Render how much time is left to vote.
	ulYPos += 16;
	text.Format( "Vote ends in: %d", static_cast<unsigned int>(( CALLVOTE_GetCountdownTicks( ) + TICRATE ) / TICRATE ));
	HUD_DrawTextCleanCentered( SmallFont, CR_RED, ulYPos, text );

	// Display how many have voted for "Yes" and "No".
	ulYPos += 16;
	text.Format( "Yes: %d", static_cast<unsigned int>( CALLVOTE_GetYesVoteCount( )));
	HUD_DrawTextClean( SmallFont, CR_UNTRANSLATED, 32, ulYPos, text );

	text.Format( "No: %d", static_cast<unsigned int>( CALLVOTE_GetNoVoteCount( )));
	HUD_DrawTextClean( SmallFont, CR_UNTRANSLATED, 320 - 32 - SmallFont->StringWidth( text ), ulYPos, text );

	ulYPos += 8;
	ULONG ulOldYPos = ulYPos;

	// [AK] Show a list of all the players who voted yes.
	for ( ULONG ulIdx = 0; ulIdx < ulMaxYesOrNoVoters; ulIdx++ )
	{
		ulPlayer = pulPlayersWhoVotedYes[ulIdx];

		if ( ulPlayer != MAXPLAYERS )
		{
			ulYPos += 8;
			text.Format( "%s", players[ulPlayer].userinfo.GetName( ));
			HUD_DrawTextClean( SmallFont, CR_UNTRANSLATED, 32, ulYPos, text );
		}
	}

	ulYPos = ulOldYPos;

	// [AK] Next, show another list with all the players who voted no.
	for ( ULONG ulIdx = 0; ulIdx < ulMaxYesOrNoVoters; ulIdx++ )
	{
		ulPlayer = pulPlayersWhoVotedNo[ulIdx];

		if ( ulPlayer != MAXPLAYERS )
		{
			ulYPos += 8;
			text.Format( "%s", players[ulPlayer].userinfo.GetName( ));
			HUD_DrawTextClean( SmallFont, CR_UNTRANSLATED, 320 - 32 - SmallFont->StringWidth( text ), ulYPos, text );
		}
	}
}

//*****************************************************************************
//
void CALLVOTE_BeginVote( FString Command, FString Parameters, FString Reason, ULONG ulPlayer )
{
	level_info_t *pLevel = NULL;

	// Don't allow a vote in the middle of another vote.
	if ( g_VoteState != VOTESTATE_NOVOTE )
	{
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			SERVER_PrintfPlayer( SERVER_GetCurrentClient( ), "Another vote is already underway.\n" );
		return;
	}

	// Check and make sure all the parameters are valid.
	if ( callvote_CheckValidity( Command, Parameters ) == false )
		return;

	// Prevent excessive re-voting.
	if (( NETWORK_GetState( ) == NETSTATE_SERVER ) && callvote_CheckForFlooding( Command, Parameters, ulPlayer ) == false )
		return;

	// Play the announcer sound for this.
	ANNOUNCER_PlayEntry( cl_announcer, "VoteNow" );

	// [TP] If the reason contains color codes, make sure that the color codes
	// are terminated properly.
	if (( Reason.IndexOf( TEXTCOLOR_ESCAPE ) != -1 ) && ( Reason.Right( 2 ).Compare( TEXTCOLOR_NORMAL ) != 0 ))
		Reason += TEXTCOLOR_NORMAL;

	// Create the vote console command.
	// [TRSR] For custom votes, this isn't a real command, so we can prettify it.
	const VOTETYPE_s *customVoteType = CALLVOTE_GetCustomVoteTypeDefinition( callvote_GetVoteType( Command ));
	g_VoteCommand = ( customVoteType != nullptr ) ? customVoteType->displayName : Command;

	// [SB] Only include parameters if there actually are any
	if ( Parameters.Len() > 0 )
	{
		g_VoteCommand += " ";
		// [TRSR] If custom vote + using player type parameter, convert parameter index to name.
		if (( customVoteType != nullptr ) && ( customVoteType->parameterType == VOTETYPE_s::parametertype_e::PLAYER ))
		{
			FString name = players[atoi( Parameters )].userinfo.GetName();
			V_RemoveColorCodes( name );
			V_EscapeBacklashes( name );
			g_VoteCommand += name;
		}
		else
		{
			g_VoteCommand += Parameters;
		}

		// [TRSR] Also handle map type parameters...
		if (( customVoteType != nullptr ) && ( customVoteType->parameterType == VOTETYPE_s::parametertype_e::MAP ))
		{
			pLevel = FindLevelByName( Parameters.GetChars() );
		}
	}

	g_ulVoteCaller = ulPlayer;
	g_VoteReason = Reason.Left(25);
	g_VoteParameters = Parameters; // [TP] Store the parameters separately for custom votes

	// Create the record of the vote for flood prevention.
	{
		VOTE_s VoteRecord;
		VoteRecord.fsParameter = Parameters;
		time_t tNow;
		time( &tNow );
		VoteRecord.tTimeCalled = tNow;
		VoteRecord.Address = SERVER_GetClient( g_ulVoteCaller )->Address;
		VoteRecord.ulVoteType = callvote_GetVoteType( Command );

		if ( callvote_IsKickVote ( VoteRecord.ulVoteType ) )
			VoteRecord.KickAddress = g_KickVoteVictimAddress; 

		g_PreviousVotes.push_back( VoteRecord );
	}

	// Display the message in the console.
	{
		FString	ReasonBlurb = ( g_VoteReason.Len( )) ? ( ", reason: \"" + g_VoteReason + "\"" ) : "";
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			Printf( "%s (%s) has called a vote (\"%s\"%s).\n", players[ulPlayer].userinfo.GetName(), SERVER_GetClient( ulPlayer )->Address.ToString(), g_VoteCommand.GetChars(), ReasonBlurb.GetChars() );
		else
			Printf( "%s has called a vote (\"%s\"%s).\n", players[ulPlayer].userinfo.GetName(), g_VoteCommand.GetChars(), ReasonBlurb.GetChars() );
	}

	g_VoteMessage = g_VoteCommand.GetChars();

	// [AK] If this is a map or changemap vote, get the level we're changing to.
	if ( strncmp( g_VoteCommand.GetChars(), "map", 3 ) == 0 )
		pLevel = FindLevelByName( g_VoteCommand.GetChars() + 4 );
	else if ( strncmp( g_VoteCommand.GetChars(), "changemap", 9 ) == 0 )
		pLevel = FindLevelByName( g_VoteCommand.GetChars() + 10 );

	// [AK] Add the full name of the level to the vote message if valid.
	if ( pLevel != NULL )
		g_VoteMessage.AppendFormat( " - %s", pLevel->LookupLevelName().GetChars() );

	g_VoteState = VOTESTATE_INVOTE;
	g_ulVoteCountdownTicks = VOTE_COUNTDOWN_TIME * TICRATE;
	g_ulShowVoteScreenTicks = g_ulVoteCountdownTicks;
	g_bVoteCancelled = false;

	// Inform clients about the vote being called.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVERCOMMANDS_CallVote( ulPlayer, Command, Parameters, Reason );
}

//*****************************************************************************
//
void CALLVOTE_ClearVote( void )
{
	ULONG	ulIdx;

	g_VoteState = VOTESTATE_NOVOTE;
	g_VoteCommand = "";
	g_ulVoteCaller = MAXPLAYERS;
	g_ulVoteCountdownTicks = 0;
	g_ulShowVoteScreenTicks = 0;
	g_ulNumYesVotes = 0;
	g_ulNumNoVotes = 0;

	for ( ulIdx = 0; ulIdx < (( MAXPLAYERS / 2 ) + 1 ); ulIdx++ )
	{
		g_ulPlayersWhoVotedYes[ulIdx] = MAXPLAYERS;
		g_ulPlayersWhoVotedNo[ulIdx] = MAXPLAYERS;
	}

	// [AK] Reset the choices of all players.
	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
		g_ulPlayerVoteChoice[ulIdx] = VOTE_UNDECIDED;

	g_bVoteCancelled = false;
}

//*****************************************************************************
//
bool CALLVOTE_VoteYes( ULONG ulPlayer )
{
	ULONG	ulIdx;

	// Don't allow the vote unless we're in the middle of a vote.
	if ( g_VoteState != VOTESTATE_INVOTE )
		return ( false );

	// [TP] Don't allow improper clients vote (they could be calling this without having been authenticated)
	if (( NETWORK_GetState() == NETSTATE_SERVER ) && ( SERVER_IsValidClient( ulPlayer ) == false ))
		return ( false );

	// [RC] If this is our vote, hide the vote screen soon.
	if (( NETWORK_GetState( ) == NETSTATE_CLIENT ) && ( cl_hidevotescreen && static_cast<LONG>(ulPlayer) == consoleplayer ))
		g_ulShowVoteScreenTicks = 1 * TICRATE;

	// Also, don't allow spectator votes if the server has them disabled.
	if (( NETWORK_GetState( ) == NETSTATE_SERVER ) && ( sv_nocallvote == 2 && players[ulPlayer].bSpectating ))
	{
		SERVER_PrintfPlayer( ulPlayer, "This server requires spectators to join the game to vote.\n" );
		return false;
	}

	// If this player has already voted, ignore his vote.
	for ( ulIdx = 0; ulIdx < ( MAXPLAYERS / 2 ) + 1; ulIdx++ )
	{
		if ( g_ulPlayersWhoVotedYes[ulIdx] == ulPlayer )
			return ( false );

		if ( g_ulPlayersWhoVotedNo[ulIdx] == ulPlayer )
			return ( false );

		// If this person matches the IP of a person who already voted, don't let him vote.
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		{
			if ( g_ulPlayersWhoVotedYes[ulIdx] < MAXPLAYERS )
			{
				if ( SERVER_GetClient( g_ulPlayersWhoVotedYes[ulIdx] )->Address.CompareNoPort( SERVER_GetClient( ulPlayer )->Address ))
					return ( false );
			}

			if ( g_ulPlayersWhoVotedNo[ulIdx] < MAXPLAYERS )
			{
				if ( SERVER_GetClient( g_ulPlayersWhoVotedNo[ulIdx] )->Address.CompareNoPort( SERVER_GetClient( ulPlayer )->Address ))
					return ( false );
			}
		}
	}

	// Add this player's vote.
	for ( ulIdx = 0; ulIdx < ( MAXPLAYERS / 2 ) + 1; ulIdx++ )
	{
		if ( g_ulPlayersWhoVotedYes[ulIdx] == MAXPLAYERS )
		{
			g_ulPlayersWhoVotedYes[ulIdx] = ulPlayer;
			break;
		}
	}

	// [AK] Update this player's choice to "yes" and increment the tally.
	g_ulPlayerVoteChoice[ulPlayer] = VOTE_YES;
	g_ulNumYesVotes++;

	// Display the message in the console.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		Printf( "%s (%s) votes \"yes\".\n", players[ulPlayer].userinfo.GetName(), SERVER_GetClient( ulPlayer )->Address.ToString() );
	else
		Printf( "%s votes \"yes\".\n", players[ulPlayer].userinfo.GetName() );

	// Nothing more to do here for clients.
	if ( NETWORK_InClientMode() )
	{
		return ( true );
	}

	SERVERCOMMANDS_PlayerVote( ulPlayer, true );

	// [RK] Check for a majority after the player has voted.
	CALLVOTE_TallyVotes();

	return ( true );
}

//*****************************************************************************
//
bool CALLVOTE_VoteNo( ULONG ulPlayer )
{
	ULONG	ulIdx;

	// Don't allow the vote unless we're in the middle of a vote.
	if ( g_VoteState != VOTESTATE_INVOTE )
		return ( false );

	// [TP] Don't allow improper clients vote (they could be calling this without having been authenticated)
	if (( NETWORK_GetState() == NETSTATE_SERVER ) && ( SERVER_IsValidClient( ulPlayer ) == false ))
		return ( false );

	// [RC] If this is our vote, hide the vote screen soon.
	if (( NETWORK_GetState( ) == NETSTATE_CLIENT ) && ( cl_hidevotescreen && static_cast<LONG>(ulPlayer) == consoleplayer ))
		g_ulShowVoteScreenTicks = 1 * TICRATE;

	// [RC] Vote callers can cancel their votes by voting "no".
	if ( ulPlayer == g_ulVoteCaller && ( NETWORK_GetState( ) == NETSTATE_SERVER ))
	{
		// [BB] If a player canceled his own vote, don't prevent others from making this type of vote again.
		g_PreviousVotes.back( ).ulVoteType = NUM_VOTECMDS;

		SERVER_Printf( "Vote caller cancelled the vote.\n" );
		g_bVoteCancelled = true;
		g_bVotePassed = false;
		callvote_EndVote( );
		return ( true );
	}

	// Also, don't allow spectator votes if the server has them disabled.
	if (( NETWORK_GetState( ) == NETSTATE_SERVER ) && ( sv_nocallvote == 2 && players[ulPlayer].bSpectating ))
	{
		SERVER_PrintfPlayer( ulPlayer, "This server requires spectators to join the game to vote.\n" );
		return false;
	}

	// If this player has already voted, ignore his vote.
	for ( ulIdx = 0; ulIdx < ( MAXPLAYERS / 2 ) + 1; ulIdx++ )
	{
		if ( g_ulPlayersWhoVotedYes[ulIdx] == ulPlayer )
			return ( false );

		if ( g_ulPlayersWhoVotedNo[ulIdx] == ulPlayer )
			return ( false );

		// If this person matches the IP of a person who already voted, don't let him vote.
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		{
			if ( g_ulPlayersWhoVotedYes[ulIdx] < MAXPLAYERS )
			{
				if ( SERVER_GetClient( g_ulPlayersWhoVotedYes[ulIdx] )->Address.CompareNoPort( SERVER_GetClient( ulPlayer )->Address ))
					return ( false );
			}

			if ( g_ulPlayersWhoVotedNo[ulIdx] < MAXPLAYERS )
			{
				if ( SERVER_GetClient( g_ulPlayersWhoVotedNo[ulIdx] )->Address.CompareNoPort( SERVER_GetClient( ulPlayer )->Address ))
					return ( false );
			}
		}
	}

	// Add this player's vote.
	for ( ulIdx = 0; ulIdx < ( MAXPLAYERS / 2 ) + 1; ulIdx++ )
	{
		if ( g_ulPlayersWhoVotedNo[ulIdx] == MAXPLAYERS )
		{
			g_ulPlayersWhoVotedNo[ulIdx] = ulPlayer;
			break;
		}
	}

	// [AK] Update this player's choice to "no" and increment the tally.
	g_ulPlayerVoteChoice[ulPlayer] = VOTE_NO;
	g_ulNumNoVotes++;

	// Display the message in the console.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		Printf( "%s (%s) votes \"no\".\n", players[ulPlayer].userinfo.GetName(), SERVER_GetClient( ulPlayer )->Address.ToString() );
	else
		Printf( "%s votes \"no\".\n", players[ulPlayer].userinfo.GetName() );

	// Nothing more to do here for clients.
	if ( NETWORK_InClientMode() )
	{
		return ( true );
	}
	
	SERVERCOMMANDS_PlayerVote( ulPlayer, false );

	// [RK] Check for a majority after the player has voted.
	CALLVOTE_TallyVotes();

	return ( true );
}

//*****************************************************************************
//
ULONG CALLVOTE_CountNumEligibleVoters( void )
{
	ULONG	ulIdx;
	ULONG	ulNumVoters;

	ulNumVoters = 0;
	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		// A voter is anyone in the game who isn't a bot.
		if ( SERVER_IsValidClient ( ulIdx ) == false )
			continue;

		// [RK] If spectators can't vote, ignore them as well.
		if (( sv_nocallvote == 0 ) || (( players[ulIdx].bSpectating == false ) && ( sv_nocallvote == 2 )))
			ulNumVoters++;
	}

	return ( ulNumVoters );
}

//*****************************************************************************
//
void CALLVOTE_EndVote( bool bPassed )
{
	// This is a client-only function.
	if ( NETWORK_InClientMode() == false )
	{
		return;
	}

	g_bVotePassed = bPassed;
	callvote_EndVote( );
}

//*****************************************************************************
//
const char *CALLVOTE_GetVoteMessage( void )
{
	return ( g_VoteMessage.GetChars( ));
}

//*****************************************************************************
//
const char *CALLVOTE_GetReason( void )
{
	return ( g_VoteReason.GetChars( ));
}

//*****************************************************************************
//
ULONG CALLVOTE_GetVoteCaller( void )
{
	return ( g_ulVoteCaller );
}

//*****************************************************************************
//
VOTESTATE_e CALLVOTE_GetVoteState( void )
{
	return ( g_VoteState );
}

//*****************************************************************************
//
ULONG CALLVOTE_GetCountdownTicks( void )
{
	return ( g_ulVoteCountdownTicks );
}

//*****************************************************************************
//
ULONG *CALLVOTE_GetPlayersWhoVotedYes( void )
{
	return ( g_ulPlayersWhoVotedYes );
}

//*****************************************************************************
//
ULONG *CALLVOTE_GetPlayersWhoVotedNo( void )
{
	return ( g_ulPlayersWhoVotedNo );
}

//*****************************************************************************
//
bool CALLVOTE_ShouldShowVoteScreen( void )
{
	return (( CALLVOTE_GetVoteState( ) == VOTESTATE_INVOTE ) && g_ulShowVoteScreenTicks );
}

//*****************************************************************************
//
ULONG CALLVOTE_GetPlayerVoteChoice( ULONG ulPlayer )
{
	// [AK] Sanity check.
	if ( ulPlayer > MAXPLAYERS )
		return VOTE_UNDECIDED;

	return ( g_ulPlayerVoteChoice[ulPlayer] );
}

//*****************************************************************************
//
void CALLVOTE_DisconnectedVoter( ULONG ulPlayer )
{
	ULONG ulIdx;
	
	// [RK] Make sure a vote is in progress
	if ( g_VoteState == VOTESTATE_INVOTE )
	{
		// If the disconnected player called the vote, end it.
		if ( CALLVOTE_GetVoteCaller() == ulPlayer )
		{
			g_PreviousVotes.back().ulVoteType = NUM_VOTECMDS;
			SERVER_Printf("The vote caller has disconnected and the vote has been cancelled.\n");
			g_bVoteCancelled = true;
			g_bVotePassed = false;
			callvote_EndVote();
			return;
		}
		
		// Disconnected players need to have their vote removed.
		for ( ulIdx = 0; ulIdx < ( MAXPLAYERS / 2 ) + 1; ulIdx++ )
		{
			// Since the actual player ID is stored in the indices sequentially we'll check
			// each index against ulPlayer and then change the indexed value if necessary.
			if ( g_ulPlayersWhoVotedYes[ulIdx] == ulPlayer )
				g_ulPlayersWhoVotedYes[ulIdx] = MAXPLAYERS;

			if ( g_ulPlayersWhoVotedNo[ulIdx] == ulPlayer )
				g_ulPlayersWhoVotedNo[ulIdx] = MAXPLAYERS;
		}
	}
}

//*****************************************************************************
//
void CALLVOTE_TallyVotes( void )
{
	// If More than half of the total eligible voters have voted, we must have a majority!
	if ( MAX( g_ulNumYesVotes, g_ulNumNoVotes ) > ( CALLVOTE_CountNumEligibleVoters( ) / 2 ))
	{
		g_bVotePassed = ( g_ulNumYesVotes > g_ulNumNoVotes );
		callvote_EndVote();
	}

	// This will serve as the final tally.
	if ( g_ulVoteCountdownTicks == 0 )
	{
		if (( g_ulNumYesVotes > 0 ) && ( g_ulNumYesVotes > g_ulNumNoVotes ))
			g_bVotePassed = true;
		else
			g_bVotePassed = false;

		callvote_EndVote();
	}
}

//*****************************************************************************
//*****************************************************************************
//
static void callvote_EndVote( void )
{
	char				szString[32];
	DHUDMessageFadeOut	*pMsg;

	if ( g_VoteState != VOTESTATE_INVOTE )
		return;

	g_VoteState = VOTESTATE_VOTECOMPLETED;
	g_ulVoteCompletedTicks = VOTE_PASSED_TIME * TICRATE;

	// If we're the server, inform the clients that the vote has ended.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVERCOMMANDS_VoteEnded( g_bVotePassed );
	else
	{
		if ( g_bVotePassed )
			sprintf( szString, "VOTE PASSED!" );
		else
			sprintf( szString, "VOTE FAILED!" );

		// Display "%s WINS!" HUD message.
		pMsg = new DHUDMessageFadeOut( BigFont, szString,
			160.4f,
			14.0f,
			320,
			200,
			CR_RED,
			3.0f,
			2.0f );

		StatusBar->AttachMessage( pMsg, MAKE_ID('C','N','T','R') );
	}

	// Log to the console.
	Printf( "Vote %s!\n", g_bVotePassed ? "passed" : "failed" );

	// Play the announcer sound associated with this event.
	if ( g_bVotePassed )
		ANNOUNCER_PlayEntry( cl_announcer, "VotePassed" );
	else
		ANNOUNCER_PlayEntry( cl_announcer, "VoteFailed" );
}

//*****************************************************************************
//
ULONG CALLVOTE_GetYesVoteCount( void )
{
	return ( g_ulNumYesVotes );
}

//*****************************************************************************
//
ULONG CALLVOTE_GetNoVoteCount( void )
{
	return ( g_ulNumNoVotes );
}

//*****************************************************************************
//
static bool callvote_CheckForFlooding( FString &Command, FString &Parameters, ULONG ulPlayer )
{
	NETADDRESS_s Address = SERVER_GetClient( ulPlayer )->Address;
	ULONG ulVoteType = callvote_GetVoteType( Command );
	const VOTETYPE_s *customVoteType = CALLVOTE_GetCustomVoteTypeDefinition( ulVoteType );
	time_t tNow;
	time( &tNow );

	// Remove old votes that no longer affect flooding.
	while ( g_PreviousVotes.size( ) > 0 && (( tNow - g_PreviousVotes.front( ).tTimeCalled ) > ( sv_votecooldown * 2) * MINUTE ))
		g_PreviousVotes.pop_front( );

	// [BB] If the server doesn't want to limit the number of votes, there is no check anything.
	// [TP] sv_limitcommands == false also implies limitless voting.
	if ( sv_votecooldown == 0 || sv_limitcommands == false )
		return true;

	// [RK] Make recently connected clients wait before they can call a vote.
	if ( static_cast<int>( players[ulPlayer].ulTime ) <= ( sv_voteconnectwait * TICRATE ))
	{
		int iSecondsLeft = static_cast<int>( sv_voteconnectwait );
		SERVER_PrintfPlayer( ulPlayer, "You must wait %i second%s after connecting to call a vote.\n", iSecondsLeft, ( iSecondsLeft == 1 ? "" : "s" ));
		return false;
	}

	// Run through the vote cache (backwards, from recent to old) and search for grounds on which to reject the vote.
	for( std::list<VOTE_s>::reverse_iterator i = g_PreviousVotes.rbegin(); i != g_PreviousVotes.rend(); ++i )
	{
		// One *type* of vote per voter per ## minutes (excluding kick votes if they passed).
		if ( !( callvote_IsKickVote ( i->ulVoteType ) && i->bPassed ) && i->Address.CompareNoPort( Address ) && ( ulVoteType == i->ulVoteType ) && (( tNow - i->tTimeCalled ) < ( sv_votecooldown * 2 ) * MINUTE ))
		{
			int iMinutesLeft = static_cast<int>( 1 + ( i->tTimeCalled + ( sv_votecooldown * 2 ) * MINUTE - tNow ) / MINUTE );
			SERVER_PrintfPlayer( ulPlayer, "You must wait %d minute%s to call another %s vote.\n", iMinutesLeft, ( iMinutesLeft == 1 ? "" : "s" ), customVoteType != nullptr ? customVoteType->displayName.GetChars() : Command.GetChars() );
			return false;
		}

		// One vote per voter per ## minutes.
		if ( i->Address.CompareNoPort( Address ) && (( tNow - i->tTimeCalled ) < sv_votecooldown * MINUTE ))
		{
			int iMinutesLeft = static_cast<int>( 1 + ( i->tTimeCalled + sv_votecooldown * MINUTE - tNow ) / MINUTE );
			SERVER_PrintfPlayer( ulPlayer, "You must wait %d minute%s to call another vote.\n", iMinutesLeft, ( iMinutesLeft == 1 ? "" : "s" ));
			return false;
		}

		// Specific votes ("map map30") that fail can't be re-proposed for ## minutes.
		if (( ulVoteType == i->ulVoteType ) && ( !i->bPassed ) && (( tNow - i->tTimeCalled ) < ( sv_votecooldown * 2 ) * MINUTE ))
		{
			int iMinutesLeft = static_cast<int>( 1 + ( i->tTimeCalled + ( sv_votecooldown * 2 ) * MINUTE - tNow ) / MINUTE );

			// Kickvotes (can't give the IP to clients!).
			if ( callvote_IsKickVote ( i->ulVoteType ) && ( !i->bPassed ) && i->KickAddress.CompareNoPort( g_KickVoteVictimAddress ))
			{
				SERVER_PrintfPlayer( ulPlayer, "That specific player was recently voted to be kicked or forced to spectate, but the vote failed. You must wait %d minute%s to call it again.\n", iMinutesLeft, ( iMinutesLeft == 1 ? "" : "s" ));
				return false;
			}

			// Other votes.
			if ( ( callvote_IsKickVote ( i->ulVoteType ) == false ) && ( stricmp( i->fsParameter.GetChars(), Parameters.GetChars() ) == 0 ))
			{
				if (( customVoteType != nullptr ) && ( customVoteType->parameterType == VOTETYPE_s::parametertype_e::PLAYER ))
				{
					Parameters = players[atoi( Parameters )].userinfo.GetName();
					V_RemoveColorCodes( Parameters );
					V_EscapeBacklashes( Parameters );
				}
				SERVER_PrintfPlayer( ulPlayer, "That specific vote (\"%s %s\") was recently called, and failed. You must wait %d minute%s to call it again.\n", customVoteType != nullptr ? customVoteType->displayName.GetChars() : Command.GetChars(), Parameters.GetChars(), iMinutesLeft, ( iMinutesLeft == 1 ? "" : "s" ));
				return false;
			}
		}
	}

	return true;
}

//*****************************************************************************
//
static bool callvote_CheckValidity( FString &Command, FString &Parameters )
{
	// Get the type of vote this is.
	ULONG	ulVoteCmd = callvote_GetVoteType( Command.GetChars( ));
	if ( ulVoteCmd == NUM_VOTECMDS )
		return ( false );

	// Check for any illegal characters.
	if ( callvote_IsKickVote ( ulVoteCmd ) == false )
	{
		int i = 0;
		while ( Parameters.GetChars()[i] != '\0' )
		{
			if ( Parameters.GetChars()[i] == ';' || Parameters.GetChars()[i] == ' ' )
			{
				if ( NETWORK_GetState( ) == NETSTATE_SERVER )
					SERVER_PrintfPlayer( SERVER_GetCurrentClient( ), "That vote command contained illegal characters.\n" );
  				return ( false );
			}
			i++;
		}
	}

	// Then, make sure the parameter for each vote is valid.
	int parameterInt = atoi( Parameters.GetChars() );
	switch ( ulVoteCmd )
	{
	case VOTECMD_KICK:
	case VOTECMD_FORCETOSPECTATE:
		{
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			{
				// Store the player's IP so he can't get away.
				ULONG ulIdx = SERVER_GetPlayerIndexFromName( Parameters.GetChars( ), true, false );
				if ( ulIdx < MAXPLAYERS )
				{
					if ( static_cast<LONG>(ulIdx) == SERVER_GetCurrentClient( ))
					{
						SERVER_PrintfPlayer( SERVER_GetCurrentClient( ), "You cannot call a vote to kick or to force to spectate yourself!\n" );
  						return ( false );
					}
					// [BB] Don't allow anyone to kick somebody who is on the admin list. [K6] ...or is logged into RCON.
					if ( SERVER_GetAdminList()->isIPInList( SERVER_GetClient( ulIdx )->Address )
						|| SERVER_GetClient( ulIdx )->bRCONAccess )
					{
						SERVER_PrintfPlayer( SERVER_GetCurrentClient( ), "This player is a server admin and thus can't be kicked or forced to spectate!\n" );
  						return ( false );
					}
					// [AK] Don't force a player to spectate if they're already a true spectator.
					if (( ulVoteCmd == VOTECMD_FORCETOSPECTATE ) && ( PLAYER_IsTrueSpectator( &players[ulIdx] )))
					{
						SERVER_PrintfPlayer( SERVER_GetCurrentClient( ), "You cannot force a player to spectate if they're already spectating!\n" );
						return ( false );
					}
					g_KickVoteVictimAddress = SERVER_GetClient( ulIdx )->Address;
					return ( true );
				}
				else
				{
					SERVER_PrintfPlayer( SERVER_GetCurrentClient( ), "That player doesn't exist.\n" );
					return ( false );
				}
			}
		}
		break;
	case VOTECMD_MAP:
	case VOTECMD_CHANGEMAP:

		// Don't allow the command if the map doesn't exist.
		if ( !P_CheckIfMapExists( Parameters.GetChars( )))
		{
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVER_PrintfPlayer( SERVER_GetCurrentClient( ), "That map does not exist.\n" );
			return ( false );
		}

		// Don't allow us to leave the map rotation.
		// [BB] Regardless of sv_maprotation, if the server has maps in the rotation,
		// assume players are restricted to these maps.
		if (( MAPROTATION_GetNumEntries( ) > 0 ) && ( MAPROTATION_IsMapInRotation( Parameters.GetChars( )) == false ))
		{
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVER_PrintfPlayer( SERVER_GetCurrentClient( ), "That map is not in the map rotation.\n" );
			return ( false );
		}
		break;
	case VOTECMD_FRAGLIMIT:
	case VOTECMD_WINLIMIT:
	case VOTECMD_DUELLIMIT:

		// Parameteter be between 0 and 255.
		if (( parameterInt < 0 ) || ( parameterInt >= 256 ))
		{
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVER_PrintfPlayer( SERVER_GetCurrentClient( ), "%s parameters must be between 0 and 255.\n", Command.GetChars() );
			return ( false );
		}
		else if ( parameterInt == 0 )
		{
			if (( Parameters.GetChars()[0] != '0' ) || ( Parameters.Len() != 1 ))
				return ( false );
		}
		Parameters.Format( "%d", parameterInt );
		break;
	case VOTECMD_TIMELIMIT:
	case VOTECMD_POINTLIMIT:

		// Parameteter must be between 0 and 65535.
		if (( parameterInt < 0 ) || ( parameterInt >= 65536 ))
		{
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVER_PrintfPlayer( SERVER_GetCurrentClient( ), "%s parameters must be between 0 and 65535.\n", Command.GetChars() );
			return ( false );
		}
		else if ( parameterInt == 0 )
		{
			if (( Parameters.GetChars()[0] != '0' ) || ( Parameters.Len() != 1 ))
				return ( false );
		}
		Parameters.Format( "%d", parameterInt );
		break;
	case VOTECMD_FLAG:
		{
			// [AK] Any valid input could be either a number or simply "true" or "false".
			if (( parameterInt == 0 ) && (( Parameters.GetChars()[0] != '0' ) || ( Parameters.Len() != 1 )))
			{
				if ( Parameters.CompareNoCase( "true" ) == 0 )
				{
					parameterInt = 1;
				}
				else if ( Parameters.CompareNoCase( "false" ) != 0 )
				{
					if ( NETWORK_GetState( ) == NETSTATE_SERVER )
						SERVER_PrintfPlayer( SERVER_GetCurrentClient( ), "%s is an invalid parameter.\n", Parameters.GetChars() );
					return ( false );
				}
			}
		
			FFlagCVar *flag = static_cast<FFlagCVar *>( FindCVar( Command, NULL));
			FIntCVar *flagset = flag->GetValueVar( );

			// [AK] Don't accept compatibility flags, only server hosts should be messing with these flags.
			if (( flagset == &compatflags ) || ( flagset == &compatflags2 ) || ( flagset == &zacompatflags ))
			{
				if ( NETWORK_GetState( ) == NETSTATE_SERVER )
					SERVER_PrintfPlayer( SERVER_GetCurrentClient( ), "compatibility flags cannot be changed in a vote.\n" );
				return ( false );
			}

			// [AK] Don't call the vote if this flag is supposed to be locked in the current game mode.
			if ( GAMEMODE_IsGameplaySettingLocked( flag ))
			{
				if ( NETWORK_GetState( ) == NETSTATE_SERVER )
					SERVER_PrintfPlayer( SERVER_GetCurrentClient( ), "%s cannot be changed in this game mode.\n", flag->GetName() );
				return ( false );
			}

			// [AK] Don't call the vote if this flag is already set to the parameter's value. 
			if ( flag->GetGenericRep( CVAR_Int ).Int == parameterInt )
			{
				if ( NETWORK_GetState( ) == NETSTATE_SERVER )
					SERVER_PrintfPlayer( SERVER_GetCurrentClient( ), "%s is already set to %s.\n", flag->GetName(), Parameters.GetChars() );
				return ( false );
			}

			Parameters.Format( "%s", parameterInt ? "true" : "false" );
		}
		break;
	case VOTECMD_NEXTMAP:
	case VOTECMD_NEXTSECRET:

		// [AK] Only let the server check if there's a (secret) exit map. If it's valid on the server's
		// end, then it should automatically be valid for the clients.
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		{
			const char *next = ( ulVoteCmd == VOTECMD_NEXTSECRET ? G_GetSecretExitMap() : G_GetExitMap() );

			if ( !next || !P_CheckIfMapExists( next ) )
			{
				SERVER_PrintfPlayer( SERVER_GetCurrentClient( ), "There is no next map, or it does not exist.\n" );
				return ( false );
			}
		}
		break;
	case VOTECMD_RESETMAP:
		{
			if ( !( GAMEMODE_GetCurrentFlags() & GMF_MAPRESETS ) )
			{
				SERVER_PrintfPlayer( SERVER_GetCurrentClient(), "ResetMap votes can only be called in game modes that support map resets.\n" );
				return ( false );
			}
		}
	    break;

	case NUM_VOTECMDS:

		return ( false );

	default:
		{
			const VOTETYPE_s *customVoteType = CALLVOTE_GetCustomVoteTypeDefinition( ulVoteCmd );
			if ( customVoteType != nullptr && NETWORK_GetState( ) == NETSTATE_SERVER )
			{
				int arg;
				switch ( customVoteType->parameterType )
				{
				case VOTETYPE_s::parametertype_e::NONE:
					arg = 0;
					break;

				case VOTETYPE_s::parametertype_e::STRING:
				case VOTETYPE_s::parametertype_e::MAP:

					if ( ( customVoteType->parameterType == VOTETYPE_s::parametertype_e::MAP ) && ( FindLevelInfo( Parameters.GetChars(), false ) == nullptr ) )
					{
						SERVER_PrintfPlayer( SERVER_GetCurrentClient(), "That map doesn't exist.\n" );
						return ( false );
					}

					arg = ACS_PushAndReturnDynamicString( Parameters );
					break;

				case VOTETYPE_s::parametertype_e::INT:
					if ( ( parameterInt == 0 ) && ( ( Parameters.GetChars()[0] != '0' ) || ( Parameters.Len() != 1 ) ) )
						return ( false );

					arg = parameterInt;
					break;

				case VOTETYPE_s::parametertype_e::FLOAT:
					arg = FLOAT2FIXED( static_cast<float>( strtod( Parameters.GetChars(), nullptr ) ) );
					break;

				case VOTETYPE_s::parametertype_e::PLAYER:
					if ( ( ( parameterInt == 0 ) && ( ( Parameters.GetChars()[0] != '0' ) || ( Parameters.Len() != 1 ) ) ) || ( !PLAYER_IsValidPlayer( parameterInt ) ) )
					{
						SERVER_PrintfPlayer( SERVER_GetCurrentClient(), "That player doesn't exist.\n" );
						return ( false );
					}

					arg = parameterInt;
					break;
				}

				if ( customVoteType->preflightScript.IsNotEmpty() )
					return P_StartScript( players[SERVER_GetCurrentClient()].mo, nullptr, -FName( customVoteType->preflightScript ), level.mapname, &arg, 1, ACS_ALWAYS | ACS_WANTRESULT ) ? true : false;
			}
		}
	}

	// Passed all checks!
	return ( true );
}

//*****************************************************************************
//
static ULONG callvote_GetVoteType( const char *pszCommand )
{
	for ( ULONG i = 0; i < g_VoteTypeDefinitions.Size(); ++i )
	{
		if ( g_VoteTypeDefinitions[i].name.CompareNoCase( pszCommand ) == 0 )
		{
			return NUM_VOTECMDS + 1 + i;
		}
	}
	if ( stricmp( "kick", pszCommand ) == 0 )
		return VOTECMD_KICK;
	else if ( stricmp( "forcespec", pszCommand ) == 0 )
		return VOTECMD_FORCETOSPECTATE;
	else if ( stricmp( "map", pszCommand ) == 0 )
		return VOTECMD_MAP;
	else if ( stricmp( "changemap", pszCommand ) == 0 )
		return VOTECMD_CHANGEMAP;
	else if ( stricmp( "fraglimit", pszCommand ) == 0 )
		return VOTECMD_FRAGLIMIT;
	else if ( stricmp( "timelimit", pszCommand ) == 0 )
		return VOTECMD_TIMELIMIT;
	else if ( stricmp( "winlimit", pszCommand ) == 0 )
		return VOTECMD_WINLIMIT;
	else if ( stricmp( "duellimit", pszCommand ) == 0 )
		return VOTECMD_DUELLIMIT;
	else if ( stricmp( "pointlimit", pszCommand ) == 0 )
		return VOTECMD_POINTLIMIT;
	else if ( stricmp( "nextmap", pszCommand ) == 0 )
		return VOTECMD_NEXTMAP;
	else if ( stricmp( "nextsecret", pszCommand ) == 0 )
		return VOTECMD_NEXTSECRET;
	else if ( stricmp( "resetmap", pszCommand ) == 0 )
		return VOTECMD_RESETMAP;
	else if ( callvote_IsFlagValid( pszCommand ))
		return VOTECMD_FLAG;
	return NUM_VOTECMDS;
}

//*****************************************************************************
//
static bool callvote_IsKickVote( const ULONG ulVoteType )
{
	return ( ( ulVoteType == VOTECMD_KICK ) || ( ulVoteType == VOTECMD_FORCETOSPECTATE ) );
}

//*****************************************************************************
//
static bool callvote_VoteRequiresParameter( const ULONG ulVoteType )
{
	switch ( ulVoteType )
	{
		case VOTECMD_NEXTMAP:
		case VOTECMD_NEXTSECRET:
		case VOTECMD_RESETMAP:
			return ( false );

		default:
			if ( ulVoteType > NUM_VOTECMDS )
			{
				const VOTETYPE_s *customVoteType = &g_VoteTypeDefinitions[ulVoteType - NUM_VOTECMDS - 1];
				return customVoteType->parameterType != VOTETYPE_s::parametertype_e::NONE;
			}
			else
			{
				return ( true );
			}
	}
}

//*****************************************************************************
//
static bool callvote_IsFlagValid( const char *pszName )
{
	// [AK] Check to make sure the CVar the client sent by name actually exists.
	FBaseCVar *cvar = FindCVar( pszName, NULL );
	if ( cvar == NULL )
	{
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			SERVER_PrintfPlayer( SERVER_GetCurrentClient( ), "That cvar does not exist.\n" );
		return ( false );
	}

	// [AK]	Also check to make sure this is a flag-type CVar.
	if ( cvar->IsFlagCVar( ) == false )
	{
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			SERVER_PrintfPlayer( SERVER_GetCurrentClient( ), "That cvar is not a flag.\n" );
		return ( false );
	}

	// [AK] This flag must belong to a gameplay or compatibility flagset in order to be valid.
	FIntCVar* flagset = static_cast<FFlagCVar*>( cvar )->GetValueVar( );
	if (( flagset != &dmflags && flagset != &dmflags2 ) &&
		( flagset != &compatflags && flagset != &compatflags ) &&
		( flagset != &zadmflags && flagset != &zacompatflags ))
	{
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			SERVER_PrintfPlayer( SERVER_GetCurrentClient( ), "That cvar is not a valid gameplay or compatibility flag.\n" );
		return ( false );
	}

	return ( true );
}

//*****************************************************************************
//	CONSOLE COMMANDS/VARIABLES

CUSTOM_CVAR( Int, sv_minvoters, 1, CVAR_ARCHIVE )
{
	if ( self < 1 )
		self = 1;
}

CVAR( Int, sv_nocallvote, 0, CVAR_ARCHIVE | CVAR_SERVERINFO ); // 0 - everyone can call votes. 1 - nobody can. 2 - only players can.
CVAR( Bool, sv_nokickvote, false, CVAR_ARCHIVE | CVAR_SERVERINFO );
CVAR( Bool, sv_noforcespecvote, false, CVAR_ARCHIVE | CVAR_SERVERINFO );
CVAR( Bool, sv_nomapvote, false, CVAR_ARCHIVE | CVAR_SERVERINFO );
CVAR( Bool, sv_nochangemapvote, false, CVAR_ARCHIVE | CVAR_SERVERINFO );
CVAR( Bool, sv_nofraglimitvote, false, CVAR_ARCHIVE | CVAR_SERVERINFO );
CVAR( Bool, sv_notimelimitvote, false, CVAR_ARCHIVE | CVAR_SERVERINFO );
CVAR( Bool, sv_nowinlimitvote, false, CVAR_ARCHIVE | CVAR_SERVERINFO );
CVAR( Bool, sv_noduellimitvote, false, CVAR_ARCHIVE | CVAR_SERVERINFO );
CVAR( Bool, sv_nopointlimitvote, false, CVAR_ARCHIVE | CVAR_SERVERINFO );
CVAR( Bool, sv_noflagvote, true, CVAR_ARCHIVE | CVAR_SERVERINFO );
CVAR( Bool, sv_nonextmapvote, false, CVAR_ARCHIVE | CVAR_SERVERINFO );
CVAR( Bool, sv_nonextsecretvote, false, CVAR_ARCHIVE | CVAR_SERVERINFO );
CVAR( Bool, sv_noresetmapvote, false, CVAR_ARCHIVE | CVAR_SERVERINFO );
CVAR( Int, sv_votecooldown, 5, CVAR_ARCHIVE | CVAR_SERVERINFO );
CVAR( Int, sv_voteconnectwait, 0, CVAR_ARCHIVE | CVAR_SERVERINFO );  // [RK] The amount of seconds after client connect to wait before voting
CVAR( Bool, cl_showfullscreenvote, false, CVAR_ARCHIVE );
CVAR( Bool, cl_hidevotescreen, true, CVAR_ARCHIVE ); // [AK] Hides the vote screen shortly after the client makes their vote.

CCMD( callvote )
{
	ULONG	ulVoteCmd;
	char	szArgument[128];

	// [SB] Prevent the arguments buffer from being full of garbage when the vote has no parameters
	szArgument[0] = '\0';

	// Don't allow a vote unless the player is a client.
	if ( NETWORK_GetState( ) != NETSTATE_CLIENT )
	{
		Printf( "You cannot call a vote if you're not a client!\n" );
		return;
	}

	if ( CLIENT_GetConnectionState( ) != CTS_ACTIVE )
		return;

	if ( argv.argc( ) < 2 )
	{
		Printf( "callvote <command> [parameters] [reason]: Calls a vote\n" );
		return;
	}

	// [BB] No voting when not in a level, e.g. during intermission.
	if ( gamestate != GS_LEVEL )
	{
		Printf( "You cannot call a vote when not in a level!\n" );
		return;
	}

	ulVoteCmd = callvote_GetVoteType( argv[1] );
	if ( ulVoteCmd == NUM_VOTECMDS )
	{
		Printf( "Invalid callvote command.\n" );
		return;
	}

	bool requiresParameter = callvote_VoteRequiresParameter( ulVoteCmd );

	if ( requiresParameter && argv.argc( ) < 3 )
	{
		Printf( "That vote type requires a parameter.\n" );
		return;
	}

	// [AK] If we're calling a flag vote, put the CVar's name and the parameter together.
	if ( ulVoteCmd == VOTECMD_FLAG )
		sprintf( szArgument, "%s %s", argv[1], argv[2] );
	else if ( requiresParameter )
		sprintf( szArgument, "%s", argv[2] );

	int reasonOffset = requiresParameter ? 1 : 0;
	if ( argv.argc( ) >= 3 + reasonOffset )
		CLIENTCOMMANDS_CallVote( ulVoteCmd, szArgument, argv[2 + reasonOffset] );
	else
		CLIENTCOMMANDS_CallVote( ulVoteCmd, szArgument, "" );
/*
	g_lBytesSent += g_LocalBuffer.cursize;
	if ( g_lBytesSent > g_lMaxBytesSent )
		g_lMaxBytesSent = g_lBytesSent;
*/
	NETWORK_LaunchPacket( CLIENT_GetLocalBuffer( ), CLIENT_GetServerAddress( ));
	CLIENT_GetLocalBuffer( )->Clear();
}

//*****************************************************************************
//
CCMD( vote_yes )
{
	if ( NETWORK_GetState( ) != NETSTATE_CLIENT )
		return;

	if ( CLIENT_GetConnectionState( ) != CTS_ACTIVE )
		return;

	if ( g_VoteState != VOTESTATE_INVOTE )
		return;

	CLIENTCOMMANDS_Vote( true );
/*
	g_lBytesSent += g_LocalBuffer.cursize;
	if ( g_lBytesSent > g_lMaxBytesSent )
		g_lMaxBytesSent = g_lBytesSent;
*/
	NETWORK_LaunchPacket( CLIENT_GetLocalBuffer( ), CLIENT_GetServerAddress( ));
	CLIENT_GetLocalBuffer( )->Clear();
}

//*****************************************************************************
//
CCMD( vote_no )
{
	if ( NETWORK_GetState( ) != NETSTATE_CLIENT )
		return;

	if ( CLIENT_GetConnectionState( ) != CTS_ACTIVE )
		return;

	if ( g_VoteState != VOTESTATE_INVOTE )
		return;

	CLIENTCOMMANDS_Vote( false );
/*
	g_lBytesSent += g_LocalBuffer.cursize;
	if ( g_lBytesSent > g_lMaxBytesSent )
		g_lMaxBytesSent = g_lBytesSent;
*/
	NETWORK_LaunchPacket( CLIENT_GetLocalBuffer( ), CLIENT_GetServerAddress( ));
	CLIENT_GetLocalBuffer( )->Clear();
}

//*****************************************************************************
//
CCMD ( cancelvote )
{
	if ( g_VoteState != VOTESTATE_INVOTE )
		return;

	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
	{
		SERVER_Printf( "Server cancelled the vote.\n" );
		g_bVoteCancelled = true;
		g_bVotePassed = false;
		callvote_EndVote( );
	}
	else if ( static_cast<LONG>(g_ulVoteCaller) == consoleplayer )
	{
		// Just vote no; we're the original caller, so it will be cancelled.
		if ( CLIENT_GetConnectionState( ) == CTS_ACTIVE )
		{
			CLIENTCOMMANDS_Vote( false );
			NETWORK_LaunchPacket( CLIENT_GetLocalBuffer( ), CLIENT_GetServerAddress( ));
			CLIENT_GetLocalBuffer( )->Clear();
		}
	}
}
