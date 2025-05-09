//-----------------------------------------------------------------------------
//
// Skulltag Source
// Copyright (C) 2003 Brad Carney
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
// Filename: chat.h
//
// Description: Contains chat structures and prototypes
//
//-----------------------------------------------------------------------------

#ifndef __CHAT_H__
#define __CHAT_H__

#include "c_cvars.h"
#include "c_dispatch.h"
#include "d_event.h"

//*****************************************************************************
//	DEFINES

// Maximum size of the chat buffer.
#define	MAX_CHATBUFFER_LENGTH		128

// [AK] Maximum saved chat messages for each player.
#define MAX_SAVED_MESSAGES		5

//*****************************************************************************
typedef enum
{
	CHATMODE_NONE,
	CHATMODE_GLOBAL,
	CHATMODE_TEAM,
	CHATMODE_PRIVATE_SEND,			// [AK] We sent a private message.
	CHATMODE_PRIVATE_RECEIVE,		// [AK] We received a private message.
	CHATMODE_PRIVATE_RCON_SEND,		// [AK] We have RCON access and received a private message sent by the server.
	CHATMODE_PRIVATE_RCON_RECEIVE,	// [AK] We have RCON access and received a private message sent to the server.

	NUM_CHATMODES

} CHATMODE_e;

//*****************************************************************************
typedef enum
{
	PRIVATECHAT_OFF,
	PRIVATECHAT_EVERYONE,
	PRIVATECHAT_TEAMMATESONLY,

} PRIVATECHAT_e;

//*****************************************************************************
//	PROTOTYPES

void		CHAT_Construct( void );
void		CHAT_Destruct( void );
void		CHAT_Tick( void );
bool		CHAT_Input( event_t *pEvent );
void		CHAT_Render( void );

void		CHAT_SetChatMode( ULONG ulMode );
ULONG		CHAT_GetChatMode( void );
const char	*CHAT_GetChatMessage( ULONG ulPlayer, ULONG ulOffset ); // [AK]
void		CHAT_AddChatMessage( ULONG ulPlayer, const char *pszString ); // [AK]
void		CHAT_ClearChatMessages( ULONG ulPlayer ); // [AK]
void		CHAT_SerializeMessages( FArchive &arc ); // [AK]
bool		CHAT_CleanChatString( FString &ChatString );
void		CHAT_PrintChatString( ULONG ulPlayer, ULONG ulMode, const char *pszString );
bool		CHAT_CanPrivateChatToTeammatesOnly( void );
bool		CHAT_CanSendPrivateMessageTo( ULONG ulSender, ULONG ulReceiver );
bool		CHAT_CanUseTeamChat( unsigned int player, bool printMessage );
void		CHAT_IgnorePlayer( const unsigned int player, const bool ignoreVoice, const unsigned int ticks, const char *reason );
void		CHAT_ExecuteIgnoreCmd( FCommandLine &argv, const bool isIndexCmd, const bool isVoiceCmd );
void		CHAT_UnignorePlayer( const unsigned int player, const bool unignoreVoice );
void		CHAT_ExecuteUnignoreCmd( FCommandLine &argv, const bool isIndexCmd, const bool isVoiceCmd );
void		CHAT_PrintMutedMessage( const bool doVoice );

//*****************************************************************************
//  EXTERNAL CONSOLE VARIABLES

EXTERN_CVAR( Bool, con_scaletext )
EXTERN_CVAR( Int, con_virtualwidth )
EXTERN_CVAR( Int, con_virtualheight )
EXTERN_CVAR( Bool, con_scaletext_usescreenratio ) // [BB]
EXTERN_CVAR( Bool, show_messages )

#endif	// __CHAT_H__
