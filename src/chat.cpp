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
// Filename: chat.cpp
//
// Description: Contains chat routines
//
//-----------------------------------------------------------------------------

#include "botcommands.h"
#include "c_console.h"
#include "cl_commands.h"
#include "cl_demo.h"
#include "cl_main.h"
#include "chat.h"
#include "doomstat.h"
#include "d_gui.h"
#include "d_net.h"
#include "deathmatch.h"
#include "gi.h"
#include "gamemode.h"
#include "hu_stuff.h"
#include "i_input.h"
#include "d_gui.h"
#include "d_player.h"
#include "network.h"
#include "s_sound.h"
#include "scoreboard.h"
#include "st_stuff.h"
//#include "sv_main.h"
#include "team.h"
#include "v_text.h"
#include "v_video.h"
#include "w_wad.h"
#include "sbar.h"
#include "st_hud.h"
#include "sectinfo.h"
#include "g_level.h"
#include "p_acs.h"
#include "farchive.h"

//*****************************************************************************
//
// [TP] ChatBuffer
//
// Encapsulates the storage for the message in the chat input line.
//
class ChatBuffer
{
public:
	enum { MaxMessages = 10 + 1 };
	ChatBuffer();

	void BeginNewMessage();
	void Clear();
	FString &GetEditableMessage();
	const FString &GetMessage() const;
	int GetPosition() const;
	int Length() const;
	void Insert( char character );
	void Insert( const char *text );
	bool IsInArchive() const;
	void MoveCursor( int offset );
	void MoveCursorTo( int position );
	void MoveInArchive( int offset );
	void RemoveCharacter( bool forward );
	void RemoveRange( int start, int end );
	void ResetTabCompletion();
	void PasteChat( const char *clip );
	void SetCurrentMessage( int position );
	void TabComplete();

	const char &operator[]( int position ) const;

private:
	TArray<FString> Messages; // List of messages stored. Last one is the current message being edited.
	int MessagePosition; // Index in Messages that the user is currently using.
	int CursorPosition; // Position of the cursor within the message.
	int TabCompletionPlayerOffset; // Player the tab completion routine should look at.
	int TabCompletionStart; // Position of the message where current tab completion begins.
	bool IsInTabCompletion; // Is tab completion routine currently active?
	FString TabCompletionWord; // The word being tab completed.
};

//*****************************************************************************
//	VARIABLES

static	ChatBuffer g_ChatBuffer;
static	ULONG	g_ulChatMode;

// [AK] The index of the player we're sending a private chat message to.
static	ULONG	g_ulChatPlayer = 0;

// [AK] A ticker that's used for controlling the blink of the cursor.
static	ULONG	g_ulChatTicker = 0;

// [AK] A collection of previously sent chat message from each player plus the server.
static	RingBuffer<FString, MAX_SAVED_MESSAGES> g_SavedChatMessages[MAXPLAYERS + 1];

//*****************************************************************************
//	PROTOTYPES

void		chat_ClampChatSoundCVar( FIntCVar &cvar ); // [AK]
void		chat_SendMessage( ULONG ulMode, const char *pszString );
FString		chat_GetIgnoredPlayers( const bool doVoice ); // [RC/AK]
void		chat_DoSubstitution( FString &Input ); // [CW]
bool		chat_IsPlayerValidReceiver( ULONG ulPlayer ); // [AK]

//*****************************************************************************
//	CONSOLE VARIABLES

CVAR( String, chatmacro1, "I'm ready to kick butt!", CVAR_ARCHIVE )
CVAR( String, chatmacro2, "I'm OK.", CVAR_ARCHIVE )
CVAR( String, chatmacro3, "I'm not looking too good!", CVAR_ARCHIVE )
CVAR( String, chatmacro4, "Help!", CVAR_ARCHIVE )
CVAR( String, chatmacro5, "You suck!", CVAR_ARCHIVE )
CVAR( String, chatmacro6, "Next time, scumbag...", CVAR_ARCHIVE )
CVAR( String, chatmacro7, "Come here!", CVAR_ARCHIVE )
CVAR( String, chatmacro8, "I'll take care of it.", CVAR_ARCHIVE )
CVAR( String, chatmacro9, "Yes", CVAR_ARCHIVE )
CVAR( String, chatmacro0, "No", CVAR_ARCHIVE )

// [CW]
CVAR( Bool, chat_substitution, false, CVAR_ARCHIVE )

EXTERN_CVAR( Int, con_colorinmessages );

// [RC] Played when a chat message arrives.
CUSTOM_CVAR( Int, chat_sound, 1, CVAR_ARCHIVE )
{
	chat_ClampChatSoundCVar( self );
}

// [AK] Played when a private chat message arrives.
CUSTOM_CVAR( Int, privatechat_sound, 2, CVAR_ARCHIVE )
{
	chat_ClampChatSoundCVar( self );
}

// [SB/Cata] Allows text to be added before a message.
CUSTOM_CVAR (String, cl_chatprefix, "", CVAR_ARCHIVE)
{
	// [AK] Don't let the chat prefix be more than 16 characters.
	if ( strlen( self ) > 16 )
	{
		Printf( "cl_chatprefix cannot be greater than 16 characters in length!\n" );

		FString truncatedPrefix = self.GetGenericRep( CVAR_String ).String;
		self = truncatedPrefix.Left( 16 );
	}
}

// [SB/Cata] Allows text to be added after a message.
CUSTOM_CVAR (String, cl_chatsuffix, "", CVAR_ARCHIVE)
{
	// [AK] Don't let the chat suffix be more than 16 characters.
	if ( strlen( self ) > 16 )
	{
		Printf( "cl_chatsuffix cannot be greater than 16 characters in length!\n" );

		FString truncatedSuffix = self.GetGenericRep( CVAR_String ).String;
		self = truncatedSuffix.Left( 16 );
	}
}

//*****************************************************************************
FStringCVar	*g_ChatMacros[10] =
{
	&chatmacro0,
	&chatmacro1,
	&chatmacro2,
	&chatmacro3,
	&chatmacro4,
	&chatmacro5,
	&chatmacro6,
	&chatmacro7,
	&chatmacro8,
	&chatmacro9
};

//*****************************************************************************
//	FUNCTIONS
ChatBuffer::ChatBuffer() :
    MessagePosition( 0 ),
    CursorPosition( 0 ),
    TabCompletionPlayerOffset( 0 ),
    TabCompletionStart( 0 )
{
	Messages.Push( "" );
}

//*****************************************************************************
//
// [TP] Returns the message the user is currently looking at, for viewing purposes.
//
const FString &ChatBuffer::GetMessage() const
{
	return Messages[MessagePosition];
}

//*****************************************************************************
//
// [TP] Returns the current message to be edited. Only edit the current chat message through this function,
//      as it ensures the user sends the message he intends to, and that the archive won't be edited!
//      Never edit the elements of Messages directly! If you do not intend to edit this string, use
//      GetMessage() instead, as this has the side effect of unwinding the archive.
//
FString &ChatBuffer::GetEditableMessage()
{
	// We're about to edit the message, so tab completion needs to reset, unless tab completion is causing the edit.
	if ( IsInTabCompletion == false )
		ResetTabCompletion();

	if ( IsInArchive() )
	{
		// If we're not already using the newest message, make the current message the newest, and then use it.
		Messages.Last() = GetMessage();
		MessagePosition = Messages.Size() - 1;
		CursorPosition = clamp( CursorPosition, 0, static_cast<signed>( GetMessage().Len() ));
	}

	// The user should be out of the archive now.
	assert( &GetMessage() == &Messages.Last() );
	return Messages.Last();
}

//*****************************************************************************
//
void ChatBuffer::Insert( char character )
{
	char text[2] = { character, '\0' };
	Insert( text );
}

//*****************************************************************************
//
void ChatBuffer::Insert( const char *text )
{
	FString &message = GetEditableMessage();
	message.Insert( GetPosition(), text );

	// [AK] Also take into account the length of the chat prefix and suffix.
	unsigned int maxLength = MAX_CHATBUFFER_LENGTH - (strlen( cl_chatprefix ) + strlen( cl_chatsuffix ));
	if ( message.Len() > maxLength )
		message.Truncate( maxLength );

	MoveCursor( strlen( text ));
}

//*****************************************************************************
//
void ChatBuffer::RemoveCharacter( bool forward )
{
	int deletePosition = GetPosition();
	FString &message = GetEditableMessage();

	if ( forward == false )
		deletePosition--;

	if ( message.IsNotEmpty() && ( deletePosition >= 0 ) && ( deletePosition < Length() ))
	{
		char *messageBuffer = message.LockBuffer();

		// Move all characters from the cursor position to the end of string back by one.
		for ( int i = deletePosition; i < Length() - 1; ++i )
			messageBuffer[i] = messageBuffer[i + 1];

		// Remove the last character.
		message.UnlockBuffer();
		message.Truncate( Length() - 1 );

		if ( forward == false )
			MoveCursor( -1 );
	}
}

//*****************************************************************************
//
// [TP] Removes all characters from 'start' to 'end', including 'start' but not 'end'.
//
void ChatBuffer::RemoveRange( int start, int end )
{
	FString &message = GetEditableMessage();
	message = message.Mid( 0, start ) + message.Mid( end );

	// Move the cursor if appropriate.
	if ( CursorPosition >= start )
	{
		CursorPosition -= end - start;

		// Don't move it in front of 'start', though, in case the cursor is inside the range.
		CursorPosition = MAX( CursorPosition, start );
	}
}

//*****************************************************************************
//
void ChatBuffer::Clear()
{
	MessagePosition = Messages.Size() - 1;
	MoveCursorTo( 0 );
	GetEditableMessage() = "";
}

//*****************************************************************************
//
int ChatBuffer::Length() const
{
	return GetMessage().Len();
}

//*****************************************************************************
//
const char &ChatBuffer::operator[]( int position ) const
{
	return GetMessage()[position];
}

//*****************************************************************************
//
int ChatBuffer::GetPosition() const
{
	return CursorPosition;
}

//*****************************************************************************
//
void ChatBuffer::MoveCursor( int offset )
{
	MoveCursorTo( GetPosition() + offset );
}

//*****************************************************************************
//
void ChatBuffer::MoveCursorTo( int position )
{
	CursorPosition = clamp( position, 0, Length() );

	if ( IsInTabCompletion == false )
		ResetTabCompletion();

	// [AK] Ensure that the cursor is always white while typing.
	g_ulChatTicker = 0;
}

//*****************************************************************************
//
void ChatBuffer::MoveInArchive( int offset )
{
	SetCurrentMessage( MessagePosition + offset );
}

//*****************************************************************************
//
void ChatBuffer::SetCurrentMessage( int position )
{
	MessagePosition = clamp( position, 0, static_cast<signed>( Messages.Size() - 1 ));
	MoveCursorTo( GetMessage().Len() );
}

//*****************************************************************************
//
// [BB] From ZDoom
void ChatBuffer::PasteChat(const char *clip)
{
	if (clip != NULL && *clip != '\0')
	{
		// Only paste the first line.
		while (*clip != '\0')
		{
			if (*clip == '\n' || *clip == '\r' || *clip == '\b')
			{
				break;
			}
			Insert( *clip++ );
		}
	}
}

//*****************************************************************************
//
void ChatBuffer::BeginNewMessage()
{
	// Only begin a new message if we don't already have a cleared message buffer.
	if ( GetMessage().IsNotEmpty() )
	{
		// If we re-sent something from the archive, store it into the current chat line. This way it is copied and becomes the most recent
		// message again.
		if ( &GetMessage() != &Messages.Last() )
			Messages.Last() = GetMessage();

		// Put a new empty string to be our current message, but avoid side-by-side duplicate entries in history: if the current buffer is
		// the same as the most recent entry in history (Messages[Messages.Size() - 2]), then we just clear the input buffer and don't push
		// anything.
		if (( Messages.Size() >= 2 ) && ( Messages.Last().CompareNoCase( Messages[Messages.Size() - 2] ) == 0 ))
			Messages.Last() = "";
		else
			Messages.Push( "" );

		// If there now are too many messages, drop some from the archive.
		while ( Messages.Size() > MaxMessages )
			Messages.Delete( 0 );

		// Select the newly created message.
		MessagePosition = Messages.Size() - 1;
		MoveCursorTo( 0 );
		ResetTabCompletion();
	}
}

//*****************************************************************************
//
bool ChatBuffer::IsInArchive() const
{
	return &GetMessage() != &Messages.Last();
}

//*****************************************************************************
//
void ChatBuffer::TabComplete()
{
	IsInTabCompletion = true;
	FString message = GetMessage();

	// If we do not yet have a word to tab complete, then find one now.
	// Only consider doing tab completion if there is no graphic character in front of the cursor.
	if ( TabCompletionWord.IsEmpty() && ( isgraph( message[GetPosition()] ) == false ))
	{
		TabCompletionStart = GetPosition();

		// Where's the beginning of the word?
		while (( TabCompletionStart > 0 ) && ( isspace( message[TabCompletionStart - 1] ) == false ))
			TabCompletionStart--;

		TabCompletionPlayerOffset = 0;
		TabCompletionWord = message.Mid( TabCompletionStart, CursorPosition - TabCompletionStart );
	}

	// If we now have a word to tab complete, proceed to try to complete it.
	if ( TabCompletionWord.IsNotEmpty() )
	{
		// Go through each player, and compare our string with their names.
		for ( int i = 0; i < MAXPLAYERS; ++i )
		{
			player_t &player = players[( i + TabCompletionPlayerOffset ) % MAXPLAYERS];

			if ( PLAYER_IsValidPlayer( &player - players ))
			{
				FString name = player.userinfo.GetName();
				V_RemoveColorCodes( name );

				// See if this player's name begins with our tab completion word.
				if ( strnicmp( name, TabCompletionWord, TabCompletionWord.Len() ) == 0 )
				{
					// Do the completion.
					RemoveRange( TabCompletionStart, GetPosition() );
					Insert( name );

					// Remove any whitespace from in front of the cursor.
					while ( isspace( GetMessage()[GetPosition()] ))
						RemoveCharacter( true );

					// Now add the suffix.
					Insert(( TabCompletionStart == 0 ) ? ": " : " " );

					// Mark down where to start looking next time. This allows us to cycle through player names.
					TabCompletionPlayerOffset = ( &player - players ) + 1;
					TabCompletionPlayerOffset %= MAXPLAYERS;
					break;
				}
			}
		}
	}

	IsInTabCompletion = false;
}

//*****************************************************************************
//
void ChatBuffer::ResetTabCompletion()
{
	TabCompletionPlayerOffset = 0;
	TabCompletionStart = 0;
	TabCompletionWord = "";
}

//*****************************************************************************
//
void CHAT_Construct( void )
{
	// Initialize the chat mode.
	g_ulChatMode = CHATMODE_NONE;

	// Clear out the chat buffer.
	g_ChatBuffer.Clear();
}

//*****************************************************************************
//
void CHAT_Tick( void )
{
	// Check the chat ignore timers.
	for ( ULONG i = 0; i < MAXPLAYERS; i++ )
	{
		// [BB] Nothing to do for players that are not in the game.
		if ( playeringame[i] == false )
			continue;

		// Decrement this player's timer.
		if ( players[i].ignoreChat.enabled && ( players[i].ignoreChat.ticks > 0 ))
			players[i].ignoreChat.ticks--;

		// Is it time to un-ignore him?
		if ( players[i].ignoreChat.ticks == 0 )
		{
			// [AK] Don't let the local player unignore themselves if they've
			// been ignored on the server. The server will tell them when.
			if (( NETWORK_GetState( ) == NETSTATE_SERVER ) || ( i != static_cast<unsigned>( consoleplayer )))
				CHAT_UnignorePlayer( i, false );
		}
	}

	// [AK] Reset the chat cursor's ticker if it goes too high.
	if (( g_ulChatMode != CHATMODE_NONE ) && ( ++g_ulChatTicker >= TICRATE ))
		g_ulChatTicker = 0;

	// [AK] If we're typing a chat message to another player, we must constantly check if
	// they're still valid, or if the server disabled private messaging.
	if ( g_ulChatMode == CHATMODE_PRIVATE_SEND )
	{
		if ( sv_allowprivatechat == PRIVATECHAT_OFF )
			CHAT_SetChatMode( CHATMODE_NONE );

		if ( chat_IsPlayerValidReceiver( g_ulChatPlayer ) == false )
			CHAT_SetChatMode( CHATMODE_NONE );
	}
}

//*****************************************************************************
//
bool CHAT_Input( event_t *pEvent )
{
	if ( pEvent->type != EV_GUI_Event )
		return ( false );

	// Server doesn't use this at all.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		return ( false );

	// Determine to do with our keypress.
	if ( g_ulChatMode != CHATMODE_NONE )
	{
		if ( pEvent->subtype == EV_GUI_KeyDown || pEvent->subtype == EV_GUI_KeyRepeat )
		{
			if (( pEvent->subtype != EV_GUI_KeyRepeat ) && ( pEvent->data1 == '\r' ))
			{
				chat_SendMessage( g_ulChatMode, g_ChatBuffer.GetMessage() );
				CHAT_SetChatMode( CHATMODE_NONE );
				return ( true );
			}
			else if (( pEvent->subtype != EV_GUI_KeyRepeat ) && ( pEvent->data1 == GK_ESCAPE ))
			{
				CHAT_SetChatMode( CHATMODE_NONE );
				return ( true );
			}
			else if ( pEvent->data1 == '\b' )
			{
				g_ChatBuffer.RemoveCharacter( false );
				return ( true );
			}
			else if ( pEvent->data1 == GK_DEL )
			{
				g_ChatBuffer.RemoveCharacter( true );
				return ( true );
			}
			// Ctrl+C. 
			else if ( pEvent->data1 == 'C' && ( pEvent->data3 & GKM_CTRL ))
			{
				I_PutInClipboard( g_ChatBuffer.GetMessage() );
				return ( true );
			}
			// Ctrl+V.
			else if ( pEvent->data1 == 'V' && ( pEvent->data3 & GKM_CTRL ))
			{
				g_ChatBuffer.PasteChat( I_GetFromClipboard( false ));
				return ( true );
			}
			// Arrow keys
			else if ( pEvent->data1 == GK_LEFT )
			{
				g_ChatBuffer.MoveCursor( -1 );
				return ( true );
			}
			else if ( pEvent->data1 == GK_RIGHT )
			{
				g_ChatBuffer.MoveCursor( 1 );
				return ( true );
			}
			else if ( pEvent->data1 == GK_UP )
			{
				g_ChatBuffer.MoveInArchive( -1 );
				return ( true );
			}
			else if ( pEvent->data1 == GK_DOWN )
			{
				g_ChatBuffer.MoveInArchive( 1 );
				return ( true );
			}
			// Home
			else if ( pEvent->data1 == GK_HOME )
			{
				g_ChatBuffer.MoveCursorTo( 0 );
				return ( true );
			}
			// End
			else if ( pEvent->data1 == GK_END )
			{
				g_ChatBuffer.MoveCursorTo( g_ChatBuffer.Length() );
				return ( true );
			}
			else if ( pEvent->data1 == '\t' )
			{
				if (( g_ulChatMode == CHATMODE_PRIVATE_SEND ) && ( SERVER_CountPlayers( false ) >= 2 ))
				{
					int tempPlayer = g_ulChatPlayer;
					int direction = ( pEvent->data3 & GKM_SHIFT ) ? -1 : 1;

					do
					{
						tempPlayer += direction;

						if ( tempPlayer < 0 )
							tempPlayer = MAXPLAYERS;
						else if ( tempPlayer > MAXPLAYERS )
							tempPlayer = 0;

						if ( static_cast<ULONG>( tempPlayer ) == g_ulChatPlayer )
							break;
					}
					while ( chat_IsPlayerValidReceiver( tempPlayer ) == false );
					g_ulChatPlayer = static_cast<ULONG>( tempPlayer );
				}
				else
				{
					g_ChatBuffer.TabComplete();
				}
				return ( true );
			}
		}
		else if ( pEvent->subtype == EV_GUI_Char )
		{
			// Send a macro.
			if ( pEvent->data2 && (( pEvent->data1 >= '0' ) && ( pEvent->data1 <= '9' )))
			{
				chat_SendMessage( g_ulChatMode, *g_ChatMacros[pEvent->data1 - '0'] );
				CHAT_SetChatMode( CHATMODE_NONE );
			}
			else
				g_ChatBuffer.Insert( static_cast<char> ( pEvent->data1 ) );

			return ( true );
		}
#ifdef __unix__
		else if (pEvent->subtype == EV_GUI_MButtonDown)
		{
			g_ChatBuffer.PasteChat( I_GetFromClipboard( true ));
		}
#endif
	}

	return ( false );
}

//*****************************************************************************
//
void CHAT_Render( void )
{
	FString prompt = "Say: ";
	FString cursor = gameinfo.gametype == GAME_Doom ? "_" : "[";
	int positionY = ( gamestate == GS_INTERMISSION ) ? SCREENHEIGHT : ST_Y;

	if ( g_ulChatMode == CHATMODE_NONE )
		return;
	else if ( g_ulChatMode == CHATMODE_TEAM )
		prompt.Format( "Say <to %s>: ", PLAYER_IsTrueSpectator( &players[consoleplayer] ) ? "Spectators" : TEAM_GetName( players[consoleplayer].Team ));
	else if ( g_ulChatMode == CHATMODE_PRIVATE_SEND )
		prompt.Format( "Say <to %s>: ", g_ulChatPlayer != MAXPLAYERS ? players[g_ulChatPlayer].userinfo.GetName() : "Server" );

	if ( g_bScale )
	{
		positionY = positionY - Scale( SCREENHEIGHT, SmallFont->GetHeight() + 1, con_virtualheight ) + 1;
		positionY = static_cast<int>( positionY * g_rYScale );
	}
	else
	{
		positionY = positionY - SmallFont->GetHeight() + 1;
	}

	int chatWidth = static_cast<int>( SCREENWIDTH * g_rXScale );
	chatWidth -= static_cast<int>( round( SmallFont->GetCharWidth( '_' ) * g_rXScale * 2 + SmallFont->StringWidth( prompt )) );

	// [AK] Also blink the cursor between dark gray and white.
	if ( g_ulChatTicker >= C_BLINKRATE )
	{
		if ( g_ChatBuffer.IsInArchive() )
		{
			cursor.Insert( 0, TEXTCOLOR_BLACK );
			cursor += TEXTCOLOR_DARKGRAY;
		}
		else
		{
			cursor.Insert( 0, TEXTCOLOR_DARKGRAY );
			cursor += TEXTCOLOR_GRAY;
		}
	}

	// Build the message that we will display to clients.
	FString displayString = g_ChatBuffer.GetMessage();
	// Insert the cursor string into the message.
	displayString = displayString.Mid( 0, g_ChatBuffer.GetPosition() ) + cursor + displayString.Mid( g_ChatBuffer.GetPosition() );
	EColorRange promptColor = CR_GREEN;
	EColorRange messageColor = CR_GRAY;

	// Use different colors in team chat.
	if ( g_ulChatMode == CHATMODE_TEAM )
		promptColor = PLAYER_IsTrueSpectator( &players[consoleplayer] ) ? CR_DARKGRAY : static_cast<EColorRange>( TEAM_GetTextColor( players[consoleplayer].Team ));
	// [AK] Use a different color when sending a private message to the server.
	else if (( g_ulChatMode == CHATMODE_PRIVATE_SEND ) && ( g_ulChatPlayer == MAXPLAYERS ))
		promptColor = CR_GREY;

	// [TP] If we're currently viewing the archive, use a different color
	if ( g_ChatBuffer.IsInArchive() )
		messageColor = CR_DARKGRAY;

	// Render the chat string.
	HUD_DrawText( SmallFont, promptColor, 0, positionY, prompt );

	if ( SmallFont->StringWidth( displayString ) > chatWidth )
	{
		// Break it onto multiple lines, if necessary.
		const BYTE *bytes = reinterpret_cast<const BYTE*>( displayString.GetChars() );
		FBrokenLines *lines = V_BreakLines( SmallFont, chatWidth, bytes );
		int messageY = positionY;

		for ( int i = 0; lines[i].Width != -1; ++i )
		{
			HUD_DrawText( SmallFont, messageColor, SmallFont->StringWidth( prompt ), messageY, lines[i].Text );
			messageY += SmallFont->GetHeight();
		}

		V_FreeBrokenLines( lines );
	}
	else
	{
		HUD_DrawText( SmallFont, messageColor, SmallFont->StringWidth( prompt ), positionY, displayString );
	}

	FString note;
	positionY -= SmallFont->GetHeight( ) * 2 + 1;

	// [RC] Tell chatters about the iron curtain of LMS chat.
	if ( GAMEMODE_AreSpectatorsForbiddenToChatToPlayers( false ))
	{
		bool bDrawNote = true;
		note = "NOTE: " TEXTCOLOR_GRAY;

		// Is this the spectator talking?
		if ( players[consoleplayer].bSpectating )
		{
			if ( g_ulChatMode != CHATMODE_PRIVATE_SEND )
				note += "Players cannot hear you chat";
			else if ( g_ulChatPlayer == MAXPLAYERS )
				note += "The server cannot hear you chat";
			else if ( players[g_ulChatPlayer].bSpectating == false )
				note.AppendFormat( "%s " TEXTCOLOR_GRAY "cannot hear you chat", players[g_ulChatPlayer].userinfo.GetName() );
			else bDrawNote = false;
		}
		else
		{
			if ( g_ulChatMode != CHATMODE_PRIVATE_SEND )
				note += "Spectators cannot talk to you";
			else if (( g_ulChatPlayer != MAXPLAYERS ) && ( players[g_ulChatPlayer].bSpectating ))
				note.AppendFormat( "%s " TEXTCOLOR_GRAY "cannot talk to you", players[g_ulChatPlayer].userinfo.GetName() );
			else bDrawNote = false;
		}

		if ( bDrawNote )
		{
			HUD_DrawTextCentered( SmallFont, CR_GREEN, positionY, note, g_bScale );
			positionY -= SmallFont->GetHeight( ) + 1;
		}
	}

	// [AK] If we're sending a private message, tell us how to change the player we want to send the message to.
	if (( g_ulChatMode == CHATMODE_PRIVATE_SEND ) && ( SERVER_CountPlayers( false ) >= 2 ))
	{
		note = "Press 'TAB' to move forward a player, or 'TAB + SHIFT' to move backward.";
		HUD_DrawTextCentered( SmallFont, CR_GREY, positionY, note, g_bScale );
	}

	BorderTopRefresh = screen->GetPageCount( );
}

//*****************************************************************************
//
void CHAT_SetChatMode( ULONG ulMode )
{
	if ( ulMode < NUM_CHATMODES )
	{
		player_t	*pPlayer = &players[consoleplayer];

		g_ulChatMode = ulMode;

		if ( ulMode != CHATMODE_NONE )
		{
			PLAYER_SetStatus( pPlayer, PLAYERSTATUS_CHATTING, true, SETPLAYERSTATUS_CLIENTSENDSUPDATE );

			// [AK] Ensure that the cursor starts off as white.
			g_ulChatTicker = 0;
		}
		else
		{
			PLAYER_SetStatus( pPlayer, PLAYERSTATUS_CHATTING, false, SETPLAYERSTATUS_CLIENTSENDSUPDATE );
		}

	}
}

//*****************************************************************************
//
ULONG CHAT_GetChatMode( void )
{
	return ( g_ulChatMode );
}

//*****************************************************************************
//
const char *CHAT_GetChatMessage( ULONG ulPlayer, ULONG ulOffset )
{
	return ( g_SavedChatMessages[ulPlayer].getOldestEntry( ulOffset ).GetChars( ));
}

//*****************************************************************************
//
void CHAT_AddChatMessage( ULONG ulPlayer, const char *pszString )
{
	g_SavedChatMessages[ulPlayer].put( pszString );
}

//*****************************************************************************
//
void CHAT_ClearChatMessages( ULONG ulPlayer )
{
	g_SavedChatMessages[ulPlayer].clear();
}

//*****************************************************************************
//
void CHAT_SerializeMessages( FArchive &arc )
{
	FString serializedMessages[MAX_SAVED_MESSAGES];
	unsigned int serializedPosition;

	// [AK] We need to save the current position of the saved messages ring buffer
	// so the saved game knows which entry is the oldest.
	if ( arc.IsStoring( ))
	{
		serializedPosition = g_SavedChatMessages[consoleplayer].getPosition( );
		arc << serializedPosition;
	}
	else
	{
		arc << serializedPosition;
		g_SavedChatMessages[consoleplayer].setPosition( serializedPosition );
	}

	// [AK] We only need to save the local player's messages, as they'll be the only
	// player left when the save is loaded. We don't need to save anybody else's.
	for ( unsigned int i = 0; i < MAX_SAVED_MESSAGES; i++ )
	{
		if ( arc.IsStoring( ))
		{
			serializedMessages[i] = g_SavedChatMessages[consoleplayer].getOldestEntry( i );
			arc << serializedMessages[i];
		}
		else
		{
			arc << serializedMessages[i];
			g_SavedChatMessages[consoleplayer].put( serializedMessages[i] );
		}
	}
}

//*****************************************************************************
//
// [AK] Returns true if the string didn't only contain crap, or false if it did.
//
bool CHAT_CleanChatString( FString &ChatString )
{
	static const char strips[] = { 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,29,30,31,127,0 };

	// [BB] Remove any kind of trailing crap.
	// [AK] Temporarily uncolorize the chat string so that V_RemoveTrailingCrapFromFString removes trailing color codes.
	V_UnColorizeString( ChatString );
	V_RemoveTrailingCrapFromFString( ChatString );

	// [K6] Idk why is this part processed as FString, but let me join in on the fun and possibly strip ascii control characters.
	// ...except 28 which is TEXTCOLOR_ESCAPE.
	ChatString.StripChars( strips );

	// [BB] If the chat string is empty now, it only contained crap.
	if ( ChatString.IsEmpty( ))
		return false;

	V_ColorizeString( ChatString );
	return true;
}

//*****************************************************************************
//
void CHAT_PrintChatString( ULONG ulPlayer, ULONG ulMode, const char *pszString )
{
	ULONG		ulChatLevel = 0;
	FString		OutString;
	FString		ChatString;

	// [RC] Are we ignoring this player?
	if (( ulPlayer != MAXPLAYERS ) && players[ulPlayer].ignoreChat.enabled )
		return;

	// [AK] Sanity check, make sure the chat mode is valid.
	if (( ulMode == CHATMODE_NONE ) || ( ulMode >= NUM_CHATMODES ))
		return;

	// If ulPlayer == MAXPLAYERS, it is the server talking.
	if ( ulPlayer == MAXPLAYERS )
	{
		if (( ulMode == CHATMODE_PRIVATE_SEND ) || ( ulMode == CHATMODE_PRIVATE_RECEIVE ))
			ulChatLevel = PRINT_PRIVATECHAT;
		else
			ulChatLevel = PRINT_HIGH;

		// Special support for "/me" commands.
		if ( strnicmp( "/me", pszString, 3 ) == 0 )
		{
			pszString += 3;
			if ( ulChatLevel == PRINT_PRIVATECHAT )
				OutString.Format( TEXTCOLOR_GREY "<%s Server> ", ulMode == CHATMODE_PRIVATE_SEND ? "To" : "From" );

			OutString.AppendFormat( "* %s" TEXTCOLOR_GREY, ulMode == CHATMODE_PRIVATE_SEND ? players[consoleplayer].userinfo.GetName() : "<Server>" );
		}
		else
		{
			if ( ulChatLevel == PRINT_PRIVATECHAT )
				OutString.Format( TEXTCOLOR_GREY "<%s Server>: ", ulMode == CHATMODE_PRIVATE_SEND ? "To" : "From" );
			else
				OutString = "<Server>: ";
		}
	}
	else if ( ulMode == CHATMODE_GLOBAL )
	{
		ulChatLevel = PRINT_CHAT;

		// Special support for "/me" commands.
		if ( strnicmp( "/me", pszString, 3 ) == 0 )
		{
			ulChatLevel = PRINT_HIGH;
			pszString += 3;
			OutString.AppendFormat( "* %s" TEXTCOLOR_GREY, players[ulPlayer].userinfo.GetName() );
		}
		else
		{
			OutString.AppendFormat( "%s" TEXTCOLOR_CHAT ": ", players[ulPlayer].userinfo.GetName() );
		}
	}
	else if ( ulMode == CHATMODE_TEAM )
	{
		ulChatLevel = PRINT_TEAMCHAT;
		if ( PLAYER_IsTrueSpectator ( &players[consoleplayer] ) )
			OutString += "<SPEC> ";
		else
		{
			OutString = TEXTCOLOR_ESCAPE;
			OutString += TEAM_GetTextColorName( players[consoleplayer].Team );
			OutString += "<TEAM> ";
		}

		// Special support for "/me" commands.
		if ( strnicmp( "/me", pszString, 3 ) == 0 )
		{
			ulChatLevel = PRINT_HIGH;
			pszString += 3;
			OutString.AppendFormat( TEXTCOLOR_GREY "* %s" TEXTCOLOR_GREY, players[ulPlayer].userinfo.GetName() );
		}
		else
		{
			OutString.AppendFormat( TEXTCOLOR_GREEN "%s" TEXTCOLOR_TEAMCHAT ": ", players[ulPlayer].userinfo.GetName() );
		}
	}
	else
	{
		ulChatLevel = PRINT_PRIVATECHAT;
		const bool bRconReceived = ( ulMode == CHATMODE_PRIVATE_RCON_SEND ) || ( ulMode == CHATMODE_PRIVATE_RCON_RECEIVE );

		// [AK] Check if this is a private message we received because it was sent to/from the server.
		// In this case, the header needs to be formatted a little differently.
		if ( bRconReceived )
		{
			OutString += TEXTCOLOR_GREY;

			if ( ulMode == CHATMODE_PRIVATE_RCON_SEND )
				OutString.AppendFormat( "<Server to %s" TEXTCOLOR_GREY ">", players[ulPlayer].userinfo.GetName() );
			else
				OutString.AppendFormat( "<%s" TEXTCOLOR_GREY " to Server>", players[ulPlayer].userinfo.GetName() );
		}
		else
		{
			OutString.AppendFormat( TEXTCOLOR_GREEN "<%s ", ulMode == CHATMODE_PRIVATE_SEND ? "To" : "From" );
			OutString.AppendFormat( "%s" TEXTCOLOR_GREEN ">", players[ulPlayer].userinfo.GetName() );
		}

		// Special support for "/me" commands.
		if ( strnicmp( "/me", pszString, 3 ) == 0 )
		{
			pszString += 3;
			OutString.AppendFormat( TEXTCOLOR_GREY " * %s" TEXTCOLOR_GREY, players[ulMode == CHATMODE_PRIVATE_SEND ? consoleplayer : ulPlayer].userinfo.GetName() );
		}
		else
		{
			OutString.AppendFormat( "%s: ", bRconReceived == false ? TEXTCOLOR_PRIVATECHAT : "" );
		}
	}

	ChatString = pszString;

	// [BB] Remove invalid color codes, those can confuse the printing and create new lines.
	V_RemoveInvalidColorCodes( ChatString );

	// [AK] We need to make a copy of the chat string that we're going to save right here, since
	// we don't want con_colorinmessages to remove the color codes.
	FString ChatStringToSave = ChatString;

	// [RC] ...if the user wants them.
	if ( con_colorinmessages == 2)
		V_RemoveColorCodes( ChatString );

	// [BB] If the chat string is empty now, it only contained crap and is ignored.
	if ( CHAT_CleanChatString( ChatString ) == false )
		return;

	OutString += ChatString;

	// [AK] Only save chat messages for non-private chat messages.
	if (( ulMode != CHATMODE_PRIVATE_SEND ) && ( ulMode != CHATMODE_PRIVATE_RECEIVE ))
	{
		// [AK] Remove any color codes that may still be in the original string.
		V_RemoveColorCodes( ChatString );

		// [AK] We shouldn't have to check if the copy string is empty (i.e. CHAT_CleanChatString
		// returned false) because we already did so in the original string. The only difference
		// is that the copy is guaranteed still have its color codes.
		CHAT_CleanChatString( ChatStringToSave );
		g_SavedChatMessages[ulPlayer].put( ChatStringToSave );

		// [AK] Trigger an event script indicating that a chat message was received.
		// If the event returns 0, then don't print the message.
		if ( GAMEMODE_HandleEvent( GAMEEVENT_CHAT, NULL, ulPlayer != MAXPLAYERS ? ulPlayer : -1, ulMode - CHATMODE_GLOBAL, true ) == 0 )
			return;

		BOTCMD_SetLastChatString( ChatString );
		BOTCMD_SetLastChatPlayer( PLAYER_IsValidPlayer( ulPlayer ) ? players[ulPlayer].userinfo.GetName() : "" );

		for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
		{
			if ( playeringame[ulIdx] == false )
				continue;

			// Don't tell the bot someone talked if it was it who talked.
			if ( ulIdx == ulPlayer )
				continue;

			// If this is a bot, tell it a player said something.
			if ( players[ulIdx].pSkullBot )
				players[ulIdx].pSkullBot->PostEvent( BOTEVENT_PLAYER_SAY );
		}
	}

	Printf( ulChatLevel, "%s\n", OutString.GetChars() );

	// [BB] If the user doesn't want to see the messages, they shouldn't make a sound.
	// [AK] Also take into account the minimum message level.
	if (( show_messages ) && ( ulChatLevel >= C_GetMessageLevel() ))
	{
		// [RC] User can choose the chat sound.
		int sound = ( ulMode > CHATMODE_TEAM ) ? privatechat_sound : chat_sound;

		if ( sound > 0 )
		{
			// Default
			if ( sound == 1 )
				S_Sound( CHAN_VOICE | CHAN_UI, gameinfo.chatSound, 1, ATTN_NONE );
			// [AK] Only Doom 1's chat sound is "misc/chat2".
			else if (( sound == 2 ) && ( gameinfo.gametype == GAME_Doom ))
				S_Sound( CHAN_VOICE | CHAN_UI, "misc/chat2", 1, ATTN_NONE );
			// [AK] In every other IWAD, the chat sound is "misc/chat".
			else
				S_Sound( CHAN_VOICE | CHAN_UI, "misc/chat", 1, ATTN_NONE );
		}
	}
}

//*****************************************************************************
//
bool CHAT_CanPrivateChatToTeammatesOnly( void )
{
	return (( sv_allowprivatechat == PRIVATECHAT_TEAMMATESONLY ) && ( GAMEMODE_GetCurrentFlags( ) & GMF_PLAYERSONTEAMS ));
}

//*****************************************************************************
//
bool CHAT_CanSendPrivateMessageTo( ULONG ulSender, ULONG ulReceiver )
{
	// [AK] If we're not restricted to sending private messages to only teammates, then we
	// can send private messages to anyone including this player.
	if ( CHAT_CanPrivateChatToTeammatesOnly( ) == false )
		return true;

	// [AK] True spectators are still allowed to send private messages to other true spectators.
	if (( PLAYER_IsTrueSpectator( &players[ulSender] )) && ( PLAYER_IsTrueSpectator( &players[ulReceiver] )))
		return true;

	// [AK] In-game players are only allowed to send private messages to their teammates.
	if (( ulReceiver < MAXPLAYERS ) && ( players[ulSender].mo != NULL ))
		return players[ulSender].mo->IsTeammate( players[ulReceiver].mo );

	return false;
}

//*****************************************************************************
//
// [AK] Used to ignore either a player's chat messages or voice.
//
void CHAT_IgnorePlayer( const unsigned int player, const bool ignoreVoice, const unsigned int ticks, const char *reason )
{
	if ( PLAYER_IsValidPlayer( player ) == false )
		return;

	if ( ignoreVoice )
		players[player].ignoreVoice( true, ticks, reason );
	else
		players[player].ignoreChat( true, ticks, reason );

	// [JK] Tell the client that they've been muted on the server.
	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
	{
		if ( player == static_cast<unsigned>( consoleplayer ))
			CHAT_PrintMutedMessage( ignoreVoice );
	}
	else
	{
		SERVERCOMMANDS_IgnoreLocalPlayer( player, true, ignoreVoice, ticks, reason );
	}
}

//*****************************************************************************
//
// [AK] Works for "ignore/ignore_idx", or "voice_ignore/voice_ignore_idx".
//
void CHAT_ExecuteIgnoreCmd( FCommandLine &argv, const bool isIndexCmd, const bool isVoiceCmd )
{
	const char *muteType = isVoiceCmd ? "voice" : "chat messages";
	int playerIndex = MAXPLAYERS;

	// [AK] This function may not be used by ConsoleCommand.
	if ( ACS_IsCalledFromConsoleCommand( ))
		return;

	// Print the explanation message.
	if ( argv.argc( ) < 2 )
	{
		// Create a list of currently ignored players.
		FString message = chat_GetIgnoredPlayers( isVoiceCmd );

		if ( message.Len( ))
		{
			message.Insert( 0, TEXTCOLOR_RED "Ignored players: " TEXTCOLOR_NORMAL );
			message += "\nUse ";

			if ( isVoiceCmd )
				message += "\"voice_unignore\" or \"voice_unignore_idx\"";
			else
				message += "\"unignore\" or \"unignore_idx\"";

			message += " to undo.";
		}
		else
		{
			message.Format( "Ignores a certain player's %s.\nUsage: %s <%s> [duration, in minutes]", muteType, argv[0], isIndexCmd ? "index" : "name" );

			// [JK] Only the server can specify a reason.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				message += " [reason]";
		}

		Printf( "%s\n", message.GetChars( ));
		return;
	}

	if ( argv.GetPlayerFromArg( playerIndex, 1, isIndexCmd ))
	{
		const IgnoreComm &ignoreType = isVoiceCmd ? players[playerIndex].ignoreVoice : players[playerIndex].ignoreChat;
		const LONG minutes = ( argv.argc( ) >= 3 ) ? atoi( argv[2] ) : -1;
		const char *reason = ( argv.argc( ) >= 4 ) ? argv[3] : NULL;
		LONG ticks = -1;

		// Did the user specify a set duration?
		if (( minutes > 0 ) && ( minutes < LONG_MAX / ( TICRATE * MINUTE )))
			ticks = minutes * TICRATE * MINUTE;

		if (( playerIndex == consoleplayer ) && ( NETWORK_GetState( ) != NETSTATE_SERVER ))
		{
			Printf( "You can't ignore yourself.\n" );
		}
		else if (( ignoreType.enabled ) && ( ignoreType.ticks == ticks ))
		{
			Printf( "You're already ignoring %s's %s.\n", players[playerIndex].userinfo.GetName( ), muteType );
		}
		else
		{
			FString message;

			CHAT_IgnorePlayer( playerIndex, isVoiceCmd, ticks, reason );
			message.Format( "%s's %s will now be ignored", players[playerIndex].userinfo.GetName( ), muteType );

			if ( ticks > 0 )
				message.AppendFormat( ", for %d minutes", static_cast<int>( minutes ));

			Printf( "%s.\n", message.GetChars( ));

			// Add a helpful note about bots.
			if ( players[playerIndex].bIsBot )
				Printf( "Note: you can disable all bot chat by setting the CVAR bot_allowchat to false.\n" );

			// Notify the server so that others using this IP are also ignored.
			if ( NETWORK_GetState( ) == NETSTATE_CLIENT )
				CLIENTCOMMANDS_Ignore( playerIndex, true, isVoiceCmd, ticks );
		}
	}
}

//*****************************************************************************
//
// [AK] Used to unignore either a player's chat messages or voice.
//
void CHAT_UnignorePlayer( const unsigned int player, const bool unignoreVoice )
{
	if ( PLAYER_IsValidPlayer( player ) == false )
		return;

	if ( unignoreVoice )
		players[player].ignoreVoice.Reset( );
	else
		players[player].ignoreChat.Reset( );

	// [JK] Tell the client that they're no longer muted on the server.
	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
	{
		if ( player == static_cast<unsigned>( consoleplayer ))
			Printf( "Your %s no longer muted on the server.\n", unignoreVoice ? "voice is" : "chat messages are" );
	}
	else
	{
		SERVERCOMMANDS_IgnoreLocalPlayer( player, false, unignoreVoice );
	}
}

//*****************************************************************************
//
// [AK] Works for "unignore/unignore_idx", or "voice_unignore/voice_unignore_idx".
//
void CHAT_ExecuteUnignoreCmd( FCommandLine &argv, const bool isIndexCmd, const bool isVoiceCmd )
{
	const char *muteType = isVoiceCmd ? "voice" : "chat messages";
	int playerIndex = MAXPLAYERS;

	// [AK] This function may not be used by ConsoleCommand.
	if ( ACS_IsCalledFromConsoleCommand( ))
		return;

	// Print the explanation message.
	if ( argv.argc( ) < 2 )
	{
		// Create a list of currently ignored players.
		FString playersIgnored = chat_GetIgnoredPlayers( isVoiceCmd );

		if ( playersIgnored.Len( ))
			Printf( TEXTCOLOR_RED "Ignored players: " TEXTCOLOR_NORMAL "%s\n", playersIgnored.GetChars( ));
		else
			Printf( "Un-ignores a certain player's %s.\nUsage: %s <%s>\n", muteType, argv[0], isIndexCmd ? "index" : "name" );

		return;
	}

	if ( argv.GetPlayerFromArg( playerIndex, 1, isIndexCmd ))
	{
		const bool isIgnored = isVoiceCmd ? players[playerIndex].ignoreVoice.enabled : players[playerIndex].ignoreChat.enabled;

		if (( playerIndex == consoleplayer ) && ( NETWORK_GetState( ) != NETSTATE_SERVER ))
		{
			Printf( "You can't unignore yourself.\n" );
		}
		else if ( isIgnored == false )
		{
			Printf( "You're not ignoring %s's %s.\n", players[playerIndex].userinfo.GetName( ), muteType );
		}
		else
		{
			CHAT_UnignorePlayer( playerIndex, isVoiceCmd );
			Printf( "%s's %s will no longer be ignored.\n", players[playerIndex].userinfo.GetName( ), muteType );

			// Notify the server so that others using this IP are also ignored.
			if ( NETWORK_GetState( ) == NETSTATE_CLIENT )
				CLIENTCOMMANDS_Ignore( playerIndex, false, isVoiceCmd );
		}
	}
}

//*****************************************************************************
//
// [AK] Can handle both chat message and voice mutes by the server.
//
void CHAT_PrintMutedMessage( const bool doVoice )
{
	// [AK] The server should never execute this.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		return;

	const IgnoreComm &ignoreType = doVoice ? players[consoleplayer].ignoreVoice : players[consoleplayer].ignoreChat;

	// [BB] Tell the player that (and for how long) he is muted.
	// Except when the muting time is not limited.
	FString message = "The server has muted your ";

	// [AK] Specify if it's the player's voice or chat messages that are muted.
	if ( doVoice )
		message += "voice";
	else
		message += "chat messages";

	message += ". Nobody can ";

	// [AK] Then, add the verb that corresponds to the ignore type.
	if ( doVoice )
		message += "hear it";
	else
		message += "see them";

	if ( ignoreType.ticks != -1 )
	{
		// [EP] Print how many minutes and how many seconds are left.
		int minutes = static_cast<int>( ignoreType.ticks / ( TICRATE * MINUTE ));
		int seconds = static_cast<int>(( ignoreType.ticks / TICRATE ) % MINUTE );

		if (( minutes > 0 ) && ( seconds > 0 ))
		{
			message.AppendFormat( " for %d minute%s and %d second%s", minutes, minutes == 1 ? "" : "s", seconds, seconds == 1 ? "" : "s" );
		}
		// [EP] If the time to wait is just some tics,
		// tell the player that he can wait just a bit.
		// There's no need to print the tics.
		else if (( minutes == 0 ) && ( seconds == 0 ))
		{
			message += " for less than a second";
		}
		else
		{
			if ( minutes > 0 )
				message.AppendFormat( " for %d minute%s", minutes, minutes == 1 ? "" : "s" );

			if ( seconds > 0 )
				message.AppendFormat( " for %d second%s", seconds, seconds == 1 ? "" : "s" );
		}
	}

	message += '.';

	// [JK] If a reason is provided, print it.
	if ( ignoreType.reason.Len( ) > 0 )
		message.AppendFormat( " Reason: %s", ignoreType.reason.GetChars( ));

	Printf( "%s\n", message.GetChars( ));
}

//*****************************************************************************
//
// [AK] A helper function to clamp the values of chat_sound and privatechat_sound.
//
void chat_ClampChatSoundCVar( FIntCVar &cvar )
{
	// [AK] All IWADs support at least three different options. However, if
	// doom2.wad, tnt.wad, or plutonia.wad are loaded, then there's a fourth
	// option for selecting Doom 2's chat sound specifically.
	const int maxValue = (( gameinfo.gametype == GAME_Doom ) && ( gameinfo.flags & GI_MAPxx )) ? 3 : 2;
	const int clampedValue = clamp<int>( cvar, 0, maxValue );

	if ( cvar != clampedValue )
	{
		// [AK] Since all Doom IWADs share the same config, it's possible that
		// the CVar's value was set to Doom 2's chat sound, which would've been
		// acceptable in Doom 2 and its derivatives. If Doom 1 is being played,
		// print a warning message to let the user know about the change.
		if (( gameinfo.gametype == GAME_Doom ) && (( gameinfo.flags & GI_MAPxx ) == false ) && ( cvar == 3 ))
			Printf( TEXTCOLOR_YELLOW "WARNING: \"%s\" is using Doom 2's chat sound, which doesn't work in Doom 1.\n", cvar.GetName( ));

		cvar = clampedValue;
	}
}

//*****************************************************************************
//
void chat_SendMessage( ULONG ulMode, const char *pszString )
{
	// [AK] Don't send the chat message if we're ignored on the server.
	if ( players[consoleplayer].ignoreChat.enabled )
	{
		CHAT_PrintMutedMessage( false );
	}
	else
	{
		FString ChatMessage = pszString;

		// [AK] Don't process and send chat messages that are empty.
		if ( ChatMessage.IsEmpty( ) )
			return;

		// [CW] Substitute the message if necessary.
		chat_DoSubstitution( ChatMessage );

		// [SB] All commands used by Konar6's kpatch don't work with prefixes/suffixes, so don't add them.
		if (( strnicmp( "!irc", pszString, 4 ) != 0 ) &&
			( strnicmp( "!music", pszString, 6 ) != 0 ) &&
			( strnicmp( "!maplist", pszString, 8 ) != 0 ))
		{
			// [AK] Take into account the length of prefix and suffix and truncate the chat message if necessary.
			unsigned int maxLength = MAX_CHATBUFFER_LENGTH - (strlen( cl_chatprefix ) + strlen( cl_chatsuffix ));
			if ( ChatMessage.Len() > maxLength )
				ChatMessage.Truncate( maxLength );

			// [SB] Add the prefix after /me, so actions works
			ChatMessage.Insert( strnicmp( "/me", pszString, 3 ) == 0 ? 3 : 0, cl_chatprefix );
			ChatMessage += cl_chatsuffix;
		}

		// Format our message so color codes can appear.
		V_ColorizeString( ChatMessage );

		// If we're the client, let the server handle formatting/sending the msg to other players.
		if ( NETWORK_GetState( ) == NETSTATE_CLIENT )
		{
			CLIENTCOMMANDS_Say( ulMode, ChatMessage.GetChars( ), g_ulChatPlayer );
		}
		else if ( demorecording )
		{
			Net_WriteByte( DEM_SAY );
			Net_WriteByte( static_cast<BYTE> ( ulMode ) );
			Net_WriteString( ChatMessage.GetChars( ));
		}
		else
		{
			ULONG ulPlayer = ulMode == CHATMODE_PRIVATE_SEND ? g_ulChatPlayer : static_cast<ULONG>( consoleplayer );
			CHAT_PrintChatString( ulPlayer, ulMode, ChatMessage.GetChars( ));
		}
	}

	// [TP] The message has been sent. Start creating a new one.
	g_ChatBuffer.BeginNewMessage();
}

//*****************************************************************************
//
// [RC] Returns a list of ignored players.
// [AK] Updated to return a list of either players whose chat messages are
// ignored, or players whose voices are ignored.
//
FString chat_GetIgnoredPlayers( const bool doVoice )
{
	IgnoreComm player_t::*ignoreType = doVoice ? &player_t::ignoreVoice : &player_t::ignoreChat;
	FString result;

	// Append all the players' names.
	for ( ULONG i = 0; i < MAXPLAYERS; i++ )
	{
		// [AK] Don't include the local player in this list.
		if ((( players[i].*ignoreType ).enabled ) && (( NETWORK_GetState( ) == NETSTATE_SERVER ) || ( i != static_cast<ULONG>( consoleplayer ))))
		{
			// [AK] Add a ", " after the previous player.
			if ( result.Len( ) > 0 )
				result += ", ";

			result += players[i].userinfo.GetName( );

			// Add the time remaining.
			if (( players[i].*ignoreType ).ticks > 0 )
			{
				int minutesLeft = static_cast<int>( 1 + ( players[i].*ignoreType ).ticks / ( MINUTE * TICRATE ));
				result.AppendFormat( " (%d minute%s left)", minutesLeft, ( minutesLeft == 1 ? "" : "s" ));
			}
		}
	}

	return result;
}

//*****************************************************************************
//
// [CW]
void chat_DoSubstitution( FString &Input )
{
	player_t *pPlayer = &players[consoleplayer];
	AWeapon *pReadyWeapon = pPlayer->ReadyWeapon;

	if ( chat_substitution )
	{
		FString Output;
		const char *pszString = Input.GetChars( );

		for ( ; *pszString != 0; pszString++ )
		{
			if ( !strncmp( pszString, "$ammocount", 10 ))
			{
				if ( pReadyWeapon && pReadyWeapon->Ammo1 )
				{
					Output.AppendFormat( "%d", pReadyWeapon->Ammo1->Amount );

					if ( pReadyWeapon->Ammo2 )
						Output.AppendFormat( "/%d", pReadyWeapon->Ammo2->Amount );
				}
				else
				{
					Output.AppendFormat( "no ammo" );
				}

				pszString += 9;
			}
			else if ( !strncmp( pszString, "$ammo", 5 ))
			{
				if ( pReadyWeapon && pReadyWeapon->Ammo1 )
				{
					// [AK] Print the tag of this ammo class if there is one.
					Output.AppendFormat( "%s", pReadyWeapon->Ammo1->GetTag( ));

					if ( pReadyWeapon->Ammo2 )
					{
						// [AK] Also print the tag of this ammo class if there is one.
						Output.AppendFormat( "/%s", pReadyWeapon->Ammo2->GetTag( ));
					}
				}
				else
				{
					Output.AppendFormat( "no ammo" );
				}

				pszString += 4;
			}
			else if ( !strncmp( pszString, "$armor", 6 ))
			{
				AInventory *pArmor = pPlayer->mo->FindInventory<ABasicArmor>( );
				int iArmorCount = 0;
				
				if ( pArmor )
					iArmorCount = pArmor->Amount;

				Output.AppendFormat( "%d", iArmorCount );

				pszString += 5;
			}
			else if ( !strncmp( pszString, "$health", 7 ))
			{
				Output.AppendFormat ("%d", pPlayer->health);

				pszString += 6;
			}
			else if ( !strncmp( pszString, "$weapon", 7 ))
			{
				if ( pReadyWeapon )
					// [AK] Print the tag of this weapon if there is one.
					Output.AppendFormat( "%s", pReadyWeapon->GetTag( ));
				else
					Output.AppendFormat( "no weapon" );

				pszString += 6;
			}
			else if ( !strncmp( pszString, "$location", 9 ))
			{
				// SECTINFO_GetPlayerLocation returns an unformatted string yet we should reach
				// chat_DoSubstitution after our chat line has already been formatted.
				FString szColorizedString = SECTINFO_GetPlayerLocation( consoleplayer );
				V_ColorizeString( szColorizedString );

				Output += szColorizedString;
				pszString += 8;
			}
			else
			{
				Output.AppendCStrPart( pszString, 1 );
			}
		}

		Input = Output;
	}
}

//*****************************************************************************
//	CONSOLE COMMANDS/VARIABLES

CCMD( say )
{
	ULONG		ulIdx;
	FString		ChatString;

	// [BB] Mods are not allowed to say anything in the player's name.
	if ( ACS_IsCalledFromConsoleCommand( ))
		return;

	// [BB] No chatting while playing a demo.
	if ( CLIENTDEMO_IsPlaying( ) == true )
	{
		Printf ( "You can't chat during demo playback.\n" );
		return;
	}

	if ( argv.argc( ) < 2 )
	{
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			return;

		// The message we send will be a global chat message to everyone.
		CHAT_SetChatMode( CHATMODE_GLOBAL );
		
		// Hide the console.
		C_HideConsole( );

		// Clear out the chat buffer.
		g_ChatBuffer.Clear();
	}
	else
	{
		for ( ulIdx = 1; ulIdx < static_cast<unsigned int>(argv.argc( )); ulIdx++ )
			ChatString.AppendFormat( "%s ", argv[ulIdx] );

		// Send the server's chat string out to clients, and print it in the console.
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		{
			// [AK] Make sure that color codes can appear in the message.
			V_ColorizeString( ChatString );
			SERVER_SendChatMessage( MAXPLAYERS, CHATMODE_GLOBAL, ChatString.GetChars( ));
		}
		else
		{
			// We typed out our message in the console or with a macro. Go ahead and send the message now.
			chat_SendMessage( CHATMODE_GLOBAL, ChatString.GetChars( ));
		}
	}
}

//*****************************************************************************
//
// [AK] Checks if the local player can use the team chat right now.
//
bool chat_CanUseTeamChat( void )
{
	// Make sure that we're playing on a game mode that supports teams.
	if (( GAMEMODE_GetCurrentFlags( ) & GMF_PLAYERSONTEAMS ) == false )
	{
		Printf( "You can't use the team chat in game modes that don't support teams.\n" );
		return false;
	}

	// Not on a team. No one to talk to.
	if (( players[consoleplayer].bOnTeam == false ) && ( PLAYER_IsTrueSpectator( &players[consoleplayer] ) == false ))
	{
		Printf( "You can't use the team chat if you're not on a team.\n" );
		return false;
	}

	return true;
}

CCMD( say_team )
{
	ULONG		ulIdx;
	FString		ChatString;

	// [BB] Mods are not allowed to say anything in the player's name.
	if ( ACS_IsCalledFromConsoleCommand( ))
		return;

	// [BB] No chatting while playing a demo.
	if ( CLIENTDEMO_IsPlaying( ) == true )
	{
		Printf ( "You can't chat during demo playback.\n" );
		return;
	}

	// The server never should have a team!
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		return;

	// Make sure we have teammates to talk to before we use team chat.
	if ( chat_CanUseTeamChat( ) == false )
		return;

	if ( argv.argc( ) < 2 )
	{
		// The message we send is a message to teammates only.
		CHAT_SetChatMode( CHATMODE_TEAM );
		
		// Hide the console.
		C_HideConsole( );

		// Clear out the chat buffer.
		g_ChatBuffer.Clear();
	}
	else
	{
		for ( ulIdx = 1; ulIdx < static_cast<unsigned int>(argv.argc( )); ulIdx++ )
			ChatString.AppendFormat( "%s ", argv[ulIdx] );

		// We typed out our message in the console or with a macro. Go ahead and send the message now.
		chat_SendMessage( CHATMODE_TEAM, ChatString.GetChars( ));
	}
}

//*****************************************************************************
//
// [AK] Checks if the player is eligible to receive private messages from us.
//
bool chat_IsPlayerValidReceiver( ULONG ulPlayer )
{
	if ( ulPlayer != MAXPLAYERS )
	{
		// [AK] If the player doesn't exist, they can't be a receiver.
		if ( PLAYER_IsValidPlayer( ulPlayer ) == false )
			return false;

		// [AK] The receiver can't be ourselves or a bot.
		if (( ulPlayer == static_cast<ULONG>( consoleplayer )) || ( players[ulPlayer].bIsBot ))
			return false;
	}

	// [AK] If we're only allowed to send private messages to teammates, then make
	// sure this player is a teammate of ours.
	return CHAT_CanSendPrivateMessageTo( consoleplayer, ulPlayer );
}

//*****************************************************************************
//
// [AK] Finds a valid player whom we can send private messages to.
//
bool chat_FindValidReceiver( void )
{
	// [AK] If we're trying to send a message to an invalid player, find another one.
	if ( chat_IsPlayerValidReceiver( g_ulChatPlayer ) == false )
	{
		ULONG oldPlayer = g_ulChatPlayer;

		do
		{
			// [AK] Keep looping until we find another player who we can send a message to.
			if ( ++g_ulChatPlayer > MAXPLAYERS )
				g_ulChatPlayer = 0;

			// [AK] If we're back to the old value for some reason, then there's no other
			// players to send a message to, so set the receiver to the server instead.
			if ( g_ulChatPlayer == oldPlayer )
				return false;
		}
		while ( chat_IsPlayerValidReceiver( g_ulChatPlayer ) == false );
	}

	return true;
}

//*****************************************************************************
//
// [AK] Allows players (or the server) to send private messages to other players.
//
void chat_PrivateMessage( FCommandLine &argv, const ULONG ulReceiver )
{
	ULONG		ulIdx;
	FString		ChatString;
	const bool	bServerSelected = ( ulReceiver == MAXPLAYERS );

	// [AK] Mods are not allowed to say anything in the player's name.
	if ( ACS_IsCalledFromConsoleCommand( ) )
		return;

	// [AK] No chatting while playing a demo.
	if ( CLIENTDEMO_IsPlaying( ) == true )
	{
		Printf( "You can't send private messages during demo playback.\n" );
		return;
	}

	// [AK] No sending private messages in a singleplayer game.
	if (( NETWORK_GetState( ) == NETSTATE_SINGLE ) || ( NETWORK_GetState( ) == NETSTATE_SINGLE_MULTIPLAYER ))
	{
		Printf ( "You can't send private messages in a singleplayer game.\n" );
		return;
	}

	// [AK] No sending private messages if the server has disabled them.
	if ( sv_allowprivatechat == PRIVATECHAT_OFF )
	{
		Printf( "Private messages have been disabled by the server.\n" );
		return;
	}

	if ( argv.argc( ) >= 2 )
	{
		if ( !bServerSelected )
		{
			// [AK] Don't send private messages to invalid players.
			if ( ulReceiver == MAXPLAYERS + 1 )
			{
				Printf( "There isn't a player named %s" TEXTCOLOR_NORMAL ".\n", argv[1] );
				return;
			}

			// [AK] Don't send private messages to bots.
			if (( ulReceiver != MAXPLAYERS ) && ( players[ulReceiver].bIsBot ))
			{
				Printf( "You can't send private messages to bots.\n" );
				return;
			}

			// [AK] Don't send private messages to ourselves.
			if (( ulReceiver == static_cast<ULONG>( consoleplayer )) && ( NETWORK_GetState( ) != NETSTATE_SERVER ))
			{
				Printf( "You can't send private messages to yourself.\n" );
				return;
			}

			// [AK] Don't let the player send privates messages to themselves via RCON.
			if ( CONSOLE_GetRCONPlayer( ) == ulReceiver )
			{
				SERVER_PrintfPlayer( ulReceiver, "You can't send private messages to yourself.\n" );
				return;
			}
		}

		// [AK] Check if we're only allowed to send private messages to our teammates.
		if (( NETWORK_GetState( ) != NETSTATE_SERVER ) && ( CHAT_CanSendPrivateMessageTo( consoleplayer, ulReceiver ) == false ))
		{
			Printf( "You can only send private messages to teammates.\n" );
			return;
		}

		g_ulChatPlayer = ulReceiver;

		if ( argv.argc( ) > 2 )
		{
			for ( ulIdx = 2; ulIdx < static_cast<unsigned int>( argv.argc( ) ); ulIdx++ )
				ChatString.AppendFormat( "%s ", argv[ulIdx] );

			// Send the server's chat string out to clients, and print it in the console.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			{
				// [AK] Make sure that color codes can appear in the message.
				V_ColorizeString( ChatString );
				SERVER_SendChatMessage( MAXPLAYERS, CHATMODE_PRIVATE_SEND, ChatString.GetChars( ), g_ulChatPlayer );
			}
			else
			{
				// We typed out our message in the console or with a macro. Go ahead and send the message now.
				chat_SendMessage( CHATMODE_PRIVATE_SEND, ChatString.GetChars( ) );
			}

			return;
		}
	}
	
	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
	{
		// [AK] Find a valid receiver, if necessary.
		if ( chat_FindValidReceiver( ) == false )
		{
			Printf( "There's no valid player to send private messages to.\n" );
			return;
		}

		// The message we send will only be for the player who we wish to chat with.
		CHAT_SetChatMode( CHATMODE_PRIVATE_SEND );

		// Hide the console.
		C_HideConsole( );

		// Clear out the chat buffer.
		g_ChatBuffer.Clear( );
	}
}

CCMD( sayto )
{
	ULONG ulPlayer = 0;

	if ( argv.argc() >= 2 )
	{
		if ( stricmp( argv[1], "Server" ) == 0 )
		{
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			{
				Printf( "The server can't send private messages to itself.\n" );
				return;
			}

			ulPlayer = MAXPLAYERS;
		}
		else
		{
			ulPlayer = SERVER_GetPlayerIndexFromName( argv[1], true, true );
			if ( ulPlayer == MAXPLAYERS ) ulPlayer++;
		}
	}

	chat_PrivateMessage( argv, ulPlayer );
}

CCMD( sayto_idx )
{
	int playerIndex = 0;

	if ( argv.argc() >= 2 )
	{
		if ( argv.SafeGetNumber( 1, playerIndex ) == false )
			return;

		if ( playerIndex == -1 )
		{
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			{
				Printf( "The server can't send private messages to itself.\n" );
				return;
			}

			playerIndex = MAXPLAYERS;
		}
		else if ( PLAYER_IsValidPlayer( playerIndex ) == false )
		{
			return;
		}
	}

	chat_PrivateMessage( argv, playerIndex );
}

//*****************************************************************************
//
// [RC] Lets clients ignore an annoying player's chat messages.
//
CCMD( ignore )
{
	CHAT_ExecuteIgnoreCmd( argv, false, false );
}

CCMD( ignore_idx )
{
	CHAT_ExecuteIgnoreCmd( argv, true, false );
}

//*****************************************************************************
//
// [RC] Undos "ignore".
//
CCMD( unignore )
{
	CHAT_ExecuteUnignoreCmd( argv, false, false );
}

CCMD( unignore_idx )
{
	CHAT_ExecuteUnignoreCmd( argv, true, false );
}

// [TP]
CCMD( messagemode )
{
	// [AK] Mods are not allowed to say anything in the player's name.
	if ( ACS_IsCalledFromConsoleCommand( ))
		return;

	// [AK] No chatting while playing a demo.
	if ( CLIENTDEMO_IsPlaying( ) == true )
	{
		Printf ( "You can't chat during demo playback.\n" );
		return;
	}

	if ( NETWORK_GetState() != NETSTATE_SERVER )
	{
		CHAT_SetChatMode( CHATMODE_GLOBAL );
		C_HideConsole( );
		g_ChatBuffer.Clear();
	}
}

// [TP]
CCMD( messagemode2 )
{
	// [AK] Mods are not allowed to say anything in the player's name.
	if ( ACS_IsCalledFromConsoleCommand( ))
		return;

	// [AK] No chatting while playing a demo.
	if ( CLIENTDEMO_IsPlaying( ) == true )
	{
		Printf ( "You can't chat during demo playback.\n" );
		return;
	}

	if ( NETWORK_GetState() != NETSTATE_SERVER )
	{
		// Make sure we have teammates to talk to before we use team chat.
		if ( chat_CanUseTeamChat( ) == false )
			return;

		CHAT_SetChatMode( CHATMODE_TEAM );
		C_HideConsole( );
		g_ChatBuffer.Clear();
	}
}

// [AK]
CCMD( messagemode3 )
{
	// [AK] Mods are not allowed to say anything in the player's name.
	if ( ACS_IsCalledFromConsoleCommand( ))
		return;

	// [AK] No chatting while playing a demo.
	if ( CLIENTDEMO_IsPlaying( ) == true )
	{
		Printf( "You can't send private messages during demo playback.\n" );
		return;
	}

	// [AK] No sending private messages in a singleplayer game.
	if (( NETWORK_GetState( ) == NETSTATE_SINGLE ) || ( NETWORK_GetState( ) == NETSTATE_SINGLE_MULTIPLAYER ))
	{
		Printf ( "You can't send private messages in a singleplayer game.\n" );
		return;
	}

	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
	{
		// [AK] No sending private messages if the server has disabled them.
		if ( sv_allowprivatechat == PRIVATECHAT_OFF )
		{
			Printf( "Private messages have been disabled by the server.\n" );
			return;
		}

		// [AK] Find a valid receiver, if necessary.
		if ( chat_FindValidReceiver( ) == false )
		{
			Printf( "There's no valid player to send private messages to.\n" );
			return;
		}

		CHAT_SetChatMode( CHATMODE_PRIVATE_SEND );
		C_HideConsole( );
		g_ChatBuffer.Clear( );
	}
}

