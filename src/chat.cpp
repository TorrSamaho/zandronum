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
#include "c_dispatch.h"
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

// [AK] A collection of previously sent chat message from each player plus the server.
static	RingBuffer<FString, MAX_SAVED_MESSAGES> g_SavedChatMessages[MAXPLAYERS + 1];

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
// [RC] Played when a chat message arrives. Values: off, default, Doom 1 (dstink), Doom 2 (dsradio).
CVAR (Int, chat_sound, 1, CVAR_ARCHIVE)
// [AK] Played when a private chat message arrives.
CVAR (Int, privatechat_sound, 2, CVAR_ARCHIVE)

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
//	PROTOTYPES

void	chat_SetChatMode( ULONG ulMode );
void	chat_SendMessage( ULONG ulMode, const char *pszString );
void	chat_GetIgnoredPlayers( FString &Destination ); // [RC]
void	chat_DoSubstitution( FString &Input ); // [CW]
bool	chat_IsPlayerValidReceiver( ULONG ulPlayer ); // [AK]

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
		if ( players[i].bIgnoreChat && ( players[i].lIgnoreChatTicks > 0 ))
			players[i].lIgnoreChatTicks--;

		// Is it time to un-ignore him?
		if ( players[i].lIgnoreChatTicks == 0 )
		{
			players[i].bIgnoreChat = false;
			// [BB] The player is unignored indefinitely. If we wouldn't do this,
			// bIgnoreChat would be set to false every tic once lIgnoreChatTicks reaches 0.
			players[i].lIgnoreChatTicks = -1;
		}
	}

	// [AK] If we're typing a chat message to another player, we must constantly check if
	// they're in the game. If not, cancel the message entirely.
	if ( g_ulChatMode == CHATMODE_PRIVATE_SEND )
	{
		if (( g_ulChatPlayer != MAXPLAYERS ) && ( chat_IsPlayerValidReceiver( g_ulChatPlayer ) == false ))
			chat_SetChatMode( CHATMODE_NONE );
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
			if ( pEvent->data1 == '\r' )
			{
				chat_SendMessage( g_ulChatMode, g_ChatBuffer.GetMessage() );
				chat_SetChatMode( CHATMODE_NONE );
				return ( true );
			}
			else if ( pEvent->data1 == GK_ESCAPE )
			{
				chat_SetChatMode( CHATMODE_NONE );
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

						if (( tempPlayer == MAXPLAYERS ) || ( tempPlayer == g_ulChatPlayer ))
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
				chat_SetChatMode( CHATMODE_NONE );
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
	static const char *cursor = gameinfo.gametype == GAME_Doom ? "_" : "[";
	bool scale = ( con_scaletext ) && ( con_virtualwidth > 0 ) && ( con_virtualheight > 0 );
	float scaleX = 1.0f;
	float scaleY = 1.0f;
	int positionY = ( gamestate == GS_INTERMISSION ) ? SCREENHEIGHT : ST_Y;

	if ( g_ulChatMode == CHATMODE_NONE )
		return;
	else if ( g_ulChatMode == CHATMODE_PRIVATE_SEND )
		prompt.Format( "Say <to %s>: ", g_ulChatPlayer != MAXPLAYERS ? players[g_ulChatPlayer].userinfo.GetName() : "Server" );

	if ( scale )
	{
		scaleX = static_cast<float>( *con_virtualwidth ) / SCREENWIDTH;
		scaleY = static_cast<float>( *con_virtualheight ) / SCREENHEIGHT;
		positionY = positionY - Scale( SCREENHEIGHT, SmallFont->GetHeight() + 1, con_virtualheight ) + 1;
		positionY = static_cast<int> ( positionY * scaleY );
	}
	else
	{
		positionY = positionY - SmallFont->GetHeight() + 1;
	}

	int chatWidth = static_cast<int> ( SCREENWIDTH * scaleX );
	chatWidth -= static_cast<int> ( round( SmallFont->GetCharWidth( '_' ) * scaleX * 2 + SmallFont->StringWidth( prompt )) );

	// Build the message that we will display to clients.
	FString displayString = g_ChatBuffer.GetMessage();
	// Insert the cursor string into the message.
	displayString = displayString.Mid( 0, g_ChatBuffer.GetPosition() ) + cursor + displayString.Mid( g_ChatBuffer.GetPosition() );
	EColorRange promptColor = CR_GREEN;
	EColorRange messageColor = CR_GRAY;

	// Use different colors in team chat.
	if ( g_ulChatMode == CHATMODE_TEAM )
	{
		promptColor = CR_GREY;
		messageColor = static_cast<EColorRange>( TEAM_GetTextColor( players[consoleplayer].ulTeam ));
	}
	// [AK] Use a different color when sending a private message to the server.
	else if (( g_ulChatMode == CHATMODE_PRIVATE_SEND ) && ( g_ulChatPlayer == MAXPLAYERS ))
	{
		promptColor = CR_GREY;
	}

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

	// [RC] Tell chatters about the iron curtain of LMS chat.
	if ( GAMEMODE_AreSpectatorsFordiddenToChatToPlayers() )
	{
		FString note = "\\cdNOTE: \\cc";
		bool bDrawNote = true;

		// Is this the spectator talking?
		if ( players[consoleplayer].bSpectating )
		{
			if ( g_ulChatMode != CHATMODE_PRIVATE_SEND )
				note += "\\ccPlayers cannot hear you chat";
			else if (( g_ulChatPlayer != MAXPLAYERS ) && ( players[g_ulChatPlayer].bSpectating == false ))
				note.AppendFormat( "%s \\cccannot hear you chat", players[g_ulChatPlayer].userinfo.GetName());
			else bDrawNote = false;
		}

		else
		{
			if ( g_ulChatMode != CHATMODE_PRIVATE_SEND )
				note += "\\ccSpectators cannot talk to you";
			else if (( g_ulChatPlayer != MAXPLAYERS ) && ( players[g_ulChatPlayer].bSpectating ))
				note.AppendFormat( "%s \\cccannot talk to you", players[g_ulChatPlayer].userinfo.GetName() );
			else bDrawNote = false;
		}

		if ( bDrawNote )
		{
			V_ColorizeString( note );
			HUD_DrawText( SmallFont, CR_UNTRANSLATED,
				(LONG)(( ( scale ? *con_virtualwidth : SCREENWIDTH )/ 2 ) - ( SmallFont->StringWidth( note ) / 2 )),
				(LONG)(( positionY * scaleY ) - ( SmallFont->GetHeight( ) * 2 ) + 1 ),
				note );
		}
	}

	BorderTopRefresh = screen->GetPageCount( );
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
void CHAT_PrintChatString( ULONG ulPlayer, ULONG ulMode, const char *pszString )
{
	ULONG		ulChatLevel = 0;
	FString		OutString;
	FString		ChatString;

	// [RC] Are we ignoring this player?
	if (( ulPlayer != MAXPLAYERS ) && players[ulPlayer].bIgnoreChat )
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
			OutString += V_GetColorChar( TEAM_GetTextColor( players[consoleplayer].ulTeam ));
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
	else if (( ulMode == CHATMODE_PRIVATE_SEND ) || ( ulMode == CHATMODE_PRIVATE_RECEIVE ))
	{
		ulChatLevel = PRINT_PRIVATECHAT;

		OutString.AppendFormat( TEXTCOLOR_GREEN "<%s ", ulMode == CHATMODE_PRIVATE_SEND ? "To" : "From" );
		OutString.AppendFormat( "%s" TEXTCOLOR_GREEN ">", players[ulPlayer].userinfo.GetName() );

		// Special support for "/me" commands.
		if ( strnicmp( "/me", pszString, 3 ) == 0 )
		{
			pszString += 3;
			OutString.AppendFormat( TEXTCOLOR_GREY " * %s" TEXTCOLOR_GREY, players[ulMode == CHATMODE_PRIVATE_SEND ? consoleplayer : ulPlayer].userinfo.GetName() );
		}
		else
		{
			OutString.AppendFormat( TEXTCOLOR_PRIVATECHAT ": " );
		}
	}

	ChatString = pszString;

	// [BB] Remove invalid color codes, those can confuse the printing and create new lines.
	V_RemoveInvalidColorCodes( ChatString );

	// [RC] ...if the user wants them.
	if ( con_colorinmessages == 2)
		V_RemoveColorCodes( ChatString );

	// [BB] Remove any kind of trailing crap.
	V_RemoveTrailingCrapFromFString ( ChatString );

	// [BB] If the chat string is empty now, it only contained crap and is ignored.
	if ( ChatString.IsEmpty() )
		return;

	OutString += ChatString;

	Printf( ulChatLevel, "%s\n", OutString.GetChars() );

	// [BB] If the user doesn't want to see the messages, they shouldn't make a sound.
	// [AK] Also take into account the minimum message level.
	if (( show_messages ) && ( ulChatLevel >= C_GetMessageLevel() ))
	{
		// [RC] User can choose the chat sound.
		int sound = ( ulMode > CHATMODE_TEAM ) ? privatechat_sound : chat_sound;

		if ( sound == 1 ) // Default
			S_Sound( CHAN_VOICE | CHAN_UI, gameinfo.chatSound, 1, ATTN_NONE );
		else if ( sound == 2 ) // Doom 1
			S_Sound( CHAN_VOICE | CHAN_UI, "misc/chat2", 1, ATTN_NONE );
		else if ( sound == 3 ) // Doom 2
			S_Sound( CHAN_VOICE | CHAN_UI, "misc/chat", 1, ATTN_NONE );
	}

	// [AK] Only save chat messages for non-private chat messages.
	if (( ulMode != CHATMODE_PRIVATE_SEND ) && ( ulMode != CHATMODE_PRIVATE_RECEIVE ))
	{
		g_SavedChatMessages[ulPlayer].put( pszString );

		// [AK] Trigger an event script indicating that a chat message was received.
		GAMEMODE_HandleEvent( GAMEEVENT_CHAT, 0, ulPlayer != MAXPLAYERS ? ulPlayer : -1, ulMode - CHATMODE_GLOBAL );

		BOTCMD_SetLastChatString( pszString );
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
}

//*****************************************************************************
//*****************************************************************************
//
void chat_SetChatMode( ULONG ulMode )
{
	if ( ulMode < NUM_CHATMODES )
	{
		player_t	*pPlayer = &players[consoleplayer];

		g_ulChatMode = ulMode;

		if ( ulMode != CHATMODE_NONE )
		{
			pPlayer->bChatting = true;

			// Tell the server we're beginning to chat.
			if ( NETWORK_GetState( ) == NETSTATE_CLIENT )
				CLIENTCOMMANDS_StartChat( );
		}
		else
		{
			pPlayer->bChatting = false;

			// Tell the server we're done chatting.
			if ( NETWORK_GetState( ) == NETSTATE_CLIENT )
				CLIENTCOMMANDS_EndChat( );
		}

	}
}

//*****************************************************************************
//
void chat_SendMessage( ULONG ulMode, const char *pszString )
{
	FString ChatMessage = pszString;

	// [AK] Don't process and send chat messages that are empty.
	if ( ChatMessage.IsEmpty( ) )
		return;

	// Format our message so color codes can appear.
	V_ColorizeString( ChatMessage );

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

	// [TP] The message has been sent. Start creating a new one.
	g_ChatBuffer.BeginNewMessage();
}

//*****************************************************************************
//
// [RC] Fills Destination with a list of ignored players.
//
void chat_GetIgnoredPlayers( FString &Destination )
{
	Destination = "";

	// Append all the players' names.
	for ( ULONG i = 0; i < MAXPLAYERS; i++ )
	{
		if ( players[i].bIgnoreChat )
		{
			Destination += players[i].userinfo.GetName();
			
			// Add the time remaining.
			if ( players[i].lIgnoreChatTicks > 0 )
			{
				int iMinutesLeft = static_cast<int>( 1 + players[i].lIgnoreChatTicks / ( MINUTE * TICRATE ));
				Destination.AppendFormat( " (%d minute%s left)", iMinutesLeft, ( iMinutesLeft == 1 ? "" : "s" ));
			}

			Destination += ", ";
		}
	}

	// Remove the last ", ".
	if ( Destination.Len( ) )
		Destination = Destination.Left( Destination.Len( ) - 2 );
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
		chat_SetChatMode( CHATMODE_GLOBAL );
		
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
			SERVER_SendChatMessage( MAXPLAYERS, CHATMODE_GLOBAL, ChatString.GetChars( ));
		else
			// We typed out our message in the console or with a macro. Go ahead and send the message now.
			chat_SendMessage( CHATMODE_GLOBAL, ChatString.GetChars( ));
	}
}

//*****************************************************************************
//
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

	// Make sure we have teammates to talk to before we use team chat.
	if ( GAMEMODE_GetCurrentFlags() & GMF_PLAYERSONTEAMS )
	{
		// Not on a team. No one to talk to.
		if ( ( players[consoleplayer].bOnTeam == false ) && ( PLAYER_IsTrueSpectator ( &players[consoleplayer] ) == false ) )
			return;
	}
	// Not in any team mode. Nothing to do!
	else
		return;

	// The server never should have a team!
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		return;

	if ( argv.argc( ) < 2 )
	{
		// The message we send is a message to teammates only.
		chat_SetChatMode( CHATMODE_TEAM );
		
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
	// [AK] If the player doesn't exist, they can't be a receiver.
	if ( PLAYER_IsValidPlayer( ulPlayer ) == false )
		return false;

	// [AK] The receiver can't be ourselves or a bot.
	if (( ulPlayer == static_cast<ULONG>( consoleplayer )) || ( players[ulPlayer].bIsBot ))
		return false;

	return true;
}

//*****************************************************************************
//
// [AK] Finds a valid player whom we can send private messages to.
//
bool chat_FindValidReceiver( void )
{
	// [AK] We can always send private messages to the server.
	if ( g_ulChatPlayer == MAXPLAYERS )
		return true;

	if ( SERVER_CountPlayers( false ) >= 2 )
	{
		// [AK] If we're trying to send a message to an invalid player, find another one.
		if ( chat_IsPlayerValidReceiver( g_ulChatPlayer ) == false )
		{
			ULONG oldPlayer = g_ulChatPlayer;

			do
			{
				// [AK] Keep looping until we find another player who we can send a message to.
				if ( ++g_ulChatPlayer >= MAXPLAYERS )
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

	return false;
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
		}

		g_ulChatPlayer = ulReceiver;

		if ( argv.argc( ) > 2 )
		{
			for ( ulIdx = 2; ulIdx < static_cast<unsigned int>( argv.argc( ) ); ulIdx++ )
				ChatString.AppendFormat( "%s ", argv[ulIdx] );

			// Send the server's chat string out to clients, and print it in the console.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVER_SendChatMessage( MAXPLAYERS, CHATMODE_PRIVATE_SEND, ChatString.GetChars( ), g_ulChatPlayer );
			else
				// We typed out our message in the console or with a macro. Go ahead and send the message now.
				chat_SendMessage( CHATMODE_PRIVATE_SEND, ChatString.GetChars( ) );

			return;
		}
	}
	
	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
	{
		// [AK] Find a valid receiver, if necessary.
		if (( bServerSelected == false ) && ( chat_FindValidReceiver( ) == false ))
		{
			Printf( "There's nobody to send private messages to. Use \"sayto server\" or \"sayto_idx -1\" to message the host instead.\n" );
			return;
		}

		// The message we send will only be for the player who we wish to chat with.
		chat_SetChatMode( CHATMODE_PRIVATE_SEND );

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
void chat_IgnorePlayer( FCommandLine &argv, const ULONG ulPlayer )
{
	// [AK] This function may not be used by ConsoleCommand.
	if ( ACS_IsCalledFromConsoleCommand( ))
		return;

	// Print the explanation message.
	if ( argv.argc( ) < 2 )
	{
		// Create a list of currently ignored players.
		FString PlayersIgnored;
		chat_GetIgnoredPlayers( PlayersIgnored );

		if ( PlayersIgnored.Len( ))
			Printf( TEXTCOLOR_RED "Ignored players: " TEXTCOLOR_NORMAL "%s\nUse \"unignore\" or \"unignore_idx\" to undo.\n", PlayersIgnored.GetChars() );
		else
			Printf( "Ignores a certain player's chat messages.\nUsage: ignore <name> [duration, in minutes]\n" );

		return;
	}
	
	LONG	lTicks = -1;
	const LONG lArgv2 = ( argv.argc( ) >= 3 ) ? atoi( argv[2] ) : -1;
	
	// Did the user specify a set duration?
	if ( ( lArgv2 > 0 ) && ( lArgv2 < LONG_MAX / ( TICRATE * MINUTE )))
		lTicks = lArgv2 * TICRATE * MINUTE;

	if ( ulPlayer == MAXPLAYERS )
		Printf( "There isn't a player named %s" TEXTCOLOR_NORMAL ".\n", argv[1] );
	else if ( ( ulPlayer == (ULONG)consoleplayer ) && ( NETWORK_GetState( ) != NETSTATE_SERVER ) )
		Printf( "You can't ignore yourself.\n" );
	else if ( players[ulPlayer].bIgnoreChat && ( players[ulPlayer].lIgnoreChatTicks == lTicks ))
		Printf( "You're already ignoring %s.\n", players[ulPlayer].userinfo.GetName() );
	else
	{
		players[ulPlayer].bIgnoreChat = true;
		players[ulPlayer].lIgnoreChatTicks = lTicks;
		Printf( "%s will now be ignored", players[ulPlayer].userinfo.GetName() );
		if ( lTicks > 0 )
			Printf( ", for %d minutes", static_cast<int>(lArgv2));
		Printf( ".\n" );

		// Add a helpful note about bots.
		if ( players[ulPlayer].bIsBot )
			Printf( "Note: you can disable all bot chat by setting the CVAR bot_allowchat to false.\n" );

		// Notify the server so that others using this IP are also ignored.
		if ( NETWORK_GetState( ) == NETSTATE_CLIENT )
			CLIENTCOMMANDS_Ignore( ulPlayer, true, lTicks );
	}
}

CCMD( ignore )
{
	// Find the player and ignore him.
	chat_IgnorePlayer( argv, argv.argc( ) >= 2 ? SERVER_GetPlayerIndexFromName( argv[1], true, true ) : MAXPLAYERS );
}

CCMD( ignore_idx )
{
	int playerIndex;
	if ( argv.SafeGetNumber( 1, playerIndex ) == false )
		return;

	if ( PLAYER_IsValidPlayer( playerIndex ) == false )
		return;

	chat_IgnorePlayer( argv, playerIndex );
}

//*****************************************************************************
//
// [RC] Undos "ignore".
//
void chat_UnignorePlayer( FCommandLine &argv, const ULONG ulPlayer )
{
	// [AK] This function may not be used by ConsoleCommand.
	if ( ACS_IsCalledFromConsoleCommand( ))
		return;

	// Print the explanation message.
	if ( argv.argc( ) < 2 )
	{
		// Create a list of currently ignored players.
		FString PlayersIgnored = "";
		chat_GetIgnoredPlayers( PlayersIgnored );

		if ( PlayersIgnored.Len( ))
			Printf( TEXTCOLOR_RED "Ignored players: " TEXTCOLOR_NORMAL "%s\n", PlayersIgnored.GetChars() );
		else
			Printf( "Un-ignores a certain player's chat messages.\nUsage: unignore <name>\n" );

		return;
	}
	
	if ( ulPlayer == MAXPLAYERS )
		Printf( "There isn't a player named %s" TEXTCOLOR_NORMAL ".\n", argv[1] );
	else if ( ( ulPlayer == (ULONG)consoleplayer ) && ( NETWORK_GetState( ) != NETSTATE_SERVER ) )
		Printf( "You can't unignore yourself.\n" );
	else if ( !players[ulPlayer].bIgnoreChat )
		Printf( "You're not ignoring %s.\n", players[ulPlayer].userinfo.GetName() );
	else 
	{
		players[ulPlayer].bIgnoreChat = false;
		players[ulPlayer].lIgnoreChatTicks = -1;
		Printf( "%s will no longer be ignored.\n", players[ulPlayer].userinfo.GetName() );

		// Notify the server so that others using this IP are also ignored.
		if ( NETWORK_GetState( ) == NETSTATE_CLIENT )
			CLIENTCOMMANDS_Ignore( ulPlayer, false );
	}
}

CCMD( unignore )
{
	chat_UnignorePlayer( argv, argv.argc( ) >= 2 ? SERVER_GetPlayerIndexFromName( argv[1], true, true ) : MAXPLAYERS );
}

CCMD( unignore_idx )
{
	int playerIndex;
	if ( argv.SafeGetNumber( 1, playerIndex ) == false )
		return;

	if ( PLAYER_IsValidPlayer( playerIndex ) == false )
		return;

	chat_UnignorePlayer( argv, playerIndex );
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
		chat_SetChatMode( CHATMODE_GLOBAL );
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
		chat_SetChatMode( CHATMODE_TEAM );
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
		// [AK] Find a valid receiver, if necessary.
		if ( chat_FindValidReceiver( ) == false )
		{
			Printf( "There's nobody to send private messages to. Use \"sayto server\" or \"sayto_idx -1\" to message the host instead.\n" );
			return;
		}

		chat_SetChatMode( CHATMODE_PRIVATE_SEND );
		C_HideConsole( );
		g_ChatBuffer.Clear( );
	}
}

