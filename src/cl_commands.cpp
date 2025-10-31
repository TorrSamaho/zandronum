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
// Filename: cl_commands.cpp
//
// Description: Contains a set of functions that correspond to each message a client
// can send out. Each functions handles the send out of each message.
//
//-----------------------------------------------------------------------------

#include "cl_commands.h"
#include "cl_demo.h"
#include "cl_main.h"
#include "doomstat.h"
#include "d_event.h"
#include "gi.h"
#include "network.h"
#include "r_state.h"
#include "v_text.h" // [RC] To conform player names
#include "gamemode.h"
#include "lastmanstanding.h"
#include "deathmatch.h"
#include "chat.h"
#include "network_enums.h"
#include "p_acs.h"
#include "v_video.h"

//*****************************************************************************
//	VARIABLES

static	ULONG	g_ulLastChangeTeamTime = 0;
static	ULONG	g_ulLastSuicideTime = 0;
static	ULONG	g_ulLastJoinTime = 0;
static	ULONG	g_ulLastDropTime = 0;
static	ULONG	g_ulLastSVCheatMessageTime = 0;
static	ULONG	g_ulLastWeaponSelectTime = 0; // [AK]
static	bool g_bIgnoreWeaponSelect = false;
SDWORD g_sdwCheckCmd = 0;

// [AK] Backups of the last few movement commands we sent to the server.
static RingBuffer<CLIENT_MOVE_COMMAND_s, MAX_BACKUP_COMMANDS> g_BackupMoveCMDs;

// [AK] The last weapon class we tried switching to before sending to the server.
static const PClass	*g_pLastWeaponClass = NULL;

//*****************************************************************************
//	FUNCTIONS

void CLIENT_ResetFloodTimers( void )
{
	g_ulLastChangeTeamTime = 0;
	g_ulLastSuicideTime = 0;
	g_ulLastJoinTime = 0;
	g_ulLastDropTime = 0;
	g_ulLastSVCheatMessageTime = 0;
}

//*****************************************************************************
//
void CLIENT_ClearBackupCommands( void )
{
	g_BackupMoveCMDs.clear( );
	g_ulLastWeaponSelectTime = 0;
	g_pLastWeaponClass = NULL;
}

//*****************************************************************************
//
void CLIENT_IgnoreWeaponSelect( bool bIgnore )
{
	g_bIgnoreWeaponSelect = bIgnore;
}

//*****************************************************************************
//
bool CLIENT_GetIgnoreWeaponSelect( void )
{
	return g_bIgnoreWeaponSelect;
}

//*****************************************************************************
//
bool CLIENT_AllowSVCheatMessage( void )
{
	if ( ( g_ulLastSVCheatMessageTime > 0 ) && ( (ULONG)gametic < ( g_ulLastSVCheatMessageTime + ( TICRATE * 1 ))))
		return false;
	else
	{
		g_ulLastSVCheatMessageTime = gametic;
		return true;
	}
}

//*****************************************************************************
//
bool UserInfoSortingFunction::operator()( FName cvar1Name, FName cvar2Name ) const
{
	FBaseCVar* cvar1 = FindCVar( cvar1Name, nullptr );
	FBaseCVar* cvar2 = FindCVar( cvar2Name, nullptr );

	if ( cvar1 && cvar2 && ((( cvar1->GetFlags() & CVAR_MOD ) ^ ( cvar2->GetFlags() & CVAR_MOD )) != 0 ))
	{
		// If one of the cvars contains the mod flag and the other one does not,the one that
		// does is goes before the other one.
		return !!( cvar2->GetFlags() & CVAR_MOD );
	}
	else
	{
		// Otherwise we don't really care.
		return cvar1 < cvar2;
	}
}

//*****************************************************************************
//
static void clientcommands_WriteCVarToUserinfo( FName name, FBaseCVar *cvar )
{
	// [BB] It's pointless to tell the server of the class, if only one class is available.
	if (( name == NAME_PlayerClass ) && ( PlayerClasses.Size( ) == 1 ))
		return;

	// [TP] Don't bother sending these
	if (( cvar == nullptr ) || ( cvar->GetFlags() & CVAR_UNSYNCED_USERINFO ))
		return;

	FString value;
	// [BB] Skin needs special treatment, so that the clients can use skins the server doesn't have.
	if ( name == NAME_Skin )
		value = skins[players[consoleplayer].userinfo.GetSkin()].name;
	else
		value = cvar->GetGenericRep( CVAR_String ).String;

	unsigned int elementNetSize = value.Len() + 1;

	// [BB] Name will be transferred as short.
	if ( name.IsPredefined() )
		elementNetSize += 2;
	// [BB] Name will be transferred as short (-1) + string + terminating 0.
	else
		elementNetSize += strlen ( name.GetChars() ) + 2 + 1;

	// [BB] If the this cvar doesn't fit into the packet anymore, send what we have
	// and start a new packet.
	// NAME_None is transferred as short and the maximum packet size is intentionally
	// hard coded to 1024. The clients shouldn't mess with this setting.
	if ( ( CLIENT_GetLocalBuffer( )->CalcSize() + elementNetSize + 2 ) >= 1024 )
	{
		// [BB] Terminate the current CLC_USERINFO command.
		NETWORK_WriteName( &CLIENT_GetLocalBuffer( )->ByteStream, NAME_None );
		CLIENT_SendServerPacket();
		CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_USERINFO );
	}

	NETWORK_WriteName( &CLIENT_GetLocalBuffer( )->ByteStream, name );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteString( value );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_SendAllUserInfo()
{
	// Temporarily disable userinfo for when the player setup menu updates our userinfo. Then
	// we can just send all our userinfo in one big bulk, instead of each time it updates
	// a userinfo property.
	if ( CLIENT_GetAllowSendingOfUserInfo( ) == false )
		return;

	const userinfo_t &userinfo = players[consoleplayer].userinfo;
	userinfo_t::ConstIterator iterator ( userinfo );
	UserInfoChanges cvarNames;

	for ( userinfo_t::ConstPair *pair; iterator.NextPair( pair ); )
		cvarNames.insert( pair->Key );

	CLIENTCOMMANDS_UserInfo ( cvarNames );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_UserInfo( const UserInfoChanges &cvars )
{
	// Temporarily disable userinfo for when the player setup menu updates our userinfo. Then
	// we can just send all our userinfo in one big bulk, instead of each time it updates
	// a userinfo property.
	if ( CLIENT_GetAllowSendingOfUserInfo( ) == false )
		return;

	// [BB] Make sure that we only send anything to the server, if cvarNames actually
	// contains cvars that we want to send.
	bool sendUserinfo = false;

	for ( UserInfoChanges::const_iterator iterator = cvars.begin(); iterator != cvars.end(); ++iterator )
	{
		FBaseCVar **cvarPointer = players[consoleplayer].userinfo.CheckKey( *iterator );
		if ( cvarPointer && ( (*cvarPointer)->GetFlags() & CVAR_UNSYNCED_USERINFO ) == false )
		{
			sendUserinfo = true;
			break;
		}
	}

	if ( sendUserinfo == false )
		return;

	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_USERINFO );

	for ( UserInfoChanges::const_iterator iterator = cvars.begin(); iterator != cvars.end(); ++iterator )
	{
		FName name = *iterator;
		FBaseCVar **cvarPointer = players[consoleplayer].userinfo.CheckKey( name );
		FBaseCVar *cvar = cvarPointer ? *cvarPointer : nullptr;
		clientcommands_WriteCVarToUserinfo( name, cvar );
	}

	NETWORK_WriteName( &CLIENT_GetLocalBuffer( )->ByteStream, NAME_None );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_SetStatus( void )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_SETSTATUS );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( players[consoleplayer].statuses );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_Say( ULONG ulMode, const char *pszString, ULONG ulPlayer )
{
	// [TP] Limit messages to certain length.
	FString chatstring ( pszString );

	if ( chatstring.Len() > MAX_CHATBUFFER_LENGTH )
		chatstring = chatstring.Left( MAX_CHATBUFFER_LENGTH );

	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_SAY );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( ulMode );

	// [AK] If we're sending a private message, also send the receiver's number.
	if ( ulMode == CHATMODE_PRIVATE_SEND )
		CLIENT_GetLocalBuffer()->ByteStream.WriteByte( ulPlayer );

	CLIENT_GetLocalBuffer( )->ByteStream.WriteString( chatstring );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_Ignore( const unsigned int player, const bool ignore, const bool doVoice, const int ticks )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_IGNORE );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( player );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteBit( ignore );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteBit( doVoice );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteLong( ticks );
}

//*****************************************************************************
//
static CLIENT_MOVE_COMMAND_s clientcommand_CreateMoveCommand( void )
{
	CLIENT_MOVE_COMMAND_s moveCMD;

	moveCMD.ulGametic = gametic;
	moveCMD.ulServerGametic = CLIENT_GetLatestServerGametic( ) + CLIENT_GetServerGameticOffset( );
	moveCMD.cmd = players[consoleplayer].cmd;

	// [BB] If we think that we can't move, don't even try to tell the server that we
	// want to move.
	if ( players[consoleplayer].mo->reactiontime )
	{
		moveCMD.cmd.ucmd.forwardmove = 0;
		moveCMD.cmd.ucmd.sidemove = 0;
		moveCMD.cmd.ucmd.buttons &= ~BT_JUMP;
	}

	moveCMD.angle = players[consoleplayer].mo->angle;
	moveCMD.pitch = players[consoleplayer].mo->pitch;
	// [AK] Calculate the checksum of this ticcmd from this tic.
	moveCMD.sdwChecksum = g_sdwCheckCmd;

	if ( players[consoleplayer].ReadyWeapon == NULL )
		moveCMD.usWeaponNetworkIndex = 0;
	else
		moveCMD.usWeaponNetworkIndex = players[consoleplayer].ReadyWeapon->GetClass( )->getActorNetworkIndex( );

	return moveCMD;
}

//*****************************************************************************
//
static void clientcommand_WriteMoveCommandToBuffer( CLIENT_MOVE_COMMAND_s moveCMD )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteLong( moveCMD.ulGametic );
	// [CK] Send the server the latest known server-gametic
	CLIENT_GetLocalBuffer( )->ByteStream.WriteLong( moveCMD.ulServerGametic );

	// Decide what additional information needs to be sent.
	ULONG ulBits = 0;
	ticcmd_t *pCmd = &moveCMD.cmd;

	if ( pCmd->ucmd.yaw )
		ulBits |= CLIENT_UPDATE_YAW;
	if ( pCmd->ucmd.pitch )
		ulBits |= CLIENT_UPDATE_PITCH;
	if ( pCmd->ucmd.roll )
		ulBits |= CLIENT_UPDATE_ROLL;
	if ( pCmd->ucmd.buttons )
	{
		ulBits |= CLIENT_UPDATE_BUTTONS;
		if ( zacompatflags & ZACOMPATF_CLIENTS_SEND_FULL_BUTTON_INFO )
			ulBits |= CLIENT_UPDATE_BUTTONS_LONG;
	}
	if ( pCmd->ucmd.forwardmove )
		ulBits |= CLIENT_UPDATE_FORWARDMOVE;
	if ( pCmd->ucmd.sidemove )
		ulBits |= CLIENT_UPDATE_SIDEMOVE;
	if ( pCmd->ucmd.upmove )
		ulBits |= CLIENT_UPDATE_UPMOVE;

	// Tell the server what information we'll be sending.
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( ulBits );

	// Send the necessary movement/steering information.
	if ( ulBits & CLIENT_UPDATE_YAW )
		CLIENT_GetLocalBuffer( )->ByteStream.WriteShort( pCmd->ucmd.yaw );
	if ( ulBits & CLIENT_UPDATE_PITCH )
		CLIENT_GetLocalBuffer( )->ByteStream.WriteShort( pCmd->ucmd.pitch );
	if ( ulBits & CLIENT_UPDATE_ROLL )
		CLIENT_GetLocalBuffer( )->ByteStream.WriteShort( pCmd->ucmd.roll );
	if ( ulBits & CLIENT_UPDATE_BUTTONS )
	{
		if ( ulBits & CLIENT_UPDATE_BUTTONS_LONG )
			CLIENT_GetLocalBuffer( )->ByteStream.WriteLong( pCmd->ucmd.buttons );
		else
			CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( pCmd->ucmd.buttons );
	}
	if ( ulBits & CLIENT_UPDATE_FORWARDMOVE )
		CLIENT_GetLocalBuffer( )->ByteStream.WriteShort( pCmd->ucmd.forwardmove );
	if ( ulBits & CLIENT_UPDATE_SIDEMOVE )
		CLIENT_GetLocalBuffer( )->ByteStream.WriteShort( pCmd->ucmd.sidemove );
	if ( ulBits & CLIENT_UPDATE_UPMOVE )
		CLIENT_GetLocalBuffer( )->ByteStream.WriteShort( pCmd->ucmd.upmove );

	CLIENT_GetLocalBuffer( )->ByteStream.WriteLong( players[consoleplayer].mo->angle );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteLong( players[consoleplayer].mo->pitch );
	// [BB] Send the checksum of our ticcmd we calculated when we originally generated the ticcmd from the user input.
	CLIENT_GetLocalBuffer( )->ByteStream.WriteLong( moveCMD.sdwChecksum );

	// Attack button.
	if ( pCmd->ucmd.buttons & BT_ATTACK )
		CLIENT_GetLocalBuffer( )->ByteStream.WriteShort( moveCMD.usWeaponNetworkIndex );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_ClientMove( void )
{
	// [AK] Create the movement command for the current tic.
	CLIENT_MOVE_COMMAND_s moveCMD = clientcommand_CreateMoveCommand( );

	// [AK] If we don't want to send backup commands, send only this one and that's it.
	if ( cl_backupcommands == 0 )
	{
		CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_CLIENTMOVE );
		clientcommand_WriteMoveCommandToBuffer( moveCMD );
	}
	else
	{
		// [AK] Save the movement command from this tic for future use.
		g_BackupMoveCMDs.put( moveCMD );

		ULONG ulNumSavedCMDs = 0;
		ULONG ulNumExpectedCMDs = cl_backupcommands + 1;

		// [AK] Determine how many movement commands we now have saved in the buffer.
		for ( unsigned int i = 0; i < MAX_BACKUP_COMMANDS; i++ )
		{
			if ( g_BackupMoveCMDs.getOldestEntry( i ).ulGametic != 0 )
				ulNumSavedCMDs++;
		}

		ULONG ulNumCMDsToSend = MIN( ulNumSavedCMDs, ulNumExpectedCMDs );
		CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_CLIENTMOVEBACKUP );

		// [AK] We need to tell the server the number of movement commands we sent.
		CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( ulNumCMDsToSend );

		// [AK] Older movement commands must be written to the buffer before newer ones.
		for ( int i = ulNumCMDsToSend; i >= 1; i-- )
		{
			moveCMD = g_BackupMoveCMDs.getOldestEntry( MAX_BACKUP_COMMANDS - i );
			clientcommand_WriteMoveCommandToBuffer( moveCMD );
		}
	}
}

//*****************************************************************************
//
void CLIENTCOMMANDS_MissingPacket( void )
{
}

//*****************************************************************************
//
void CLIENTCOMMANDS_Pong( unsigned int time )
{
	// [BB] CLIENTCOMMANDS_Pong is the only client command function that
	// immediately launches a network packet. This is something that
	// we obviously don't want to do when playing a demo.
	if ( CLIENTDEMO_IsPlaying( ) )
		return;

	// [BB] For an accurate ping measurement, the client has to answer
	// immediately instead of sending the answer together with the the
	// other commands tic-synced in CLIENT_EndTick().
	NETBUFFER_s	TempBuffer;
	TempBuffer.Init( MAX_UDP_PACKET, BUFFERTYPE_WRITE );
	TempBuffer.ByteStream.WriteByte( CLC_PONG );
	TempBuffer.ByteStream.WriteLong( time );
	NETWORK_LaunchPacket( &TempBuffer, NETWORK_GetFromAddress( ) );
	TempBuffer.Free();
}

//*****************************************************************************
//
void CLIENTCOMMANDS_WeaponSelect( const PClass *pType )
{
	if ( ( pType == NULL ) || g_bIgnoreWeaponSelect )
		return;

	// [AK] If we don't want to send backup commands, send only this one and that's it.
	if ( cl_backupcommands == 0 )
	{
		CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_WEAPONSELECT );
		CLIENT_GetLocalBuffer( )->ByteStream.WriteShort( pType->getActorNetworkIndex() );
	}
	else
	{
		// [AK] Save the weapon class and the original time we sent out the command,
		// which we'll use when we re-send the command.
		g_pLastWeaponClass = pType;
		g_ulLastWeaponSelectTime = gametic;

		// [AK] Send out the backup command.
		CLIENTCOMMANDS_SendBackupWeaponSelect( );
	}
}

//*****************************************************************************
//
void CLIENTCOMMANDS_SendBackupWeaponSelect( void )
{
	if (( g_pLastWeaponClass == NULL ) || ( g_bIgnoreWeaponSelect ))
		return;

	// [AK] We don't want to send out this command more than we should (i.e. up to how
	// many backup commands we can send). If the time that we originally sent this
	// command is too far into the past, don't send it anymore.
	if ( gametic - g_ulLastWeaponSelectTime > static_cast<ULONG>( cl_backupcommands ))
		return;

	// [AK] A backup version of a weapon select command is different from an ordinary
	// CLC_WEAPONSELECT command, so we'll use a different CLC identifier for it. We must
	// also send the gametic so the server knows exactly where this command should go
	// in the client's command buffer, or can ignore it if it's already been received.
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_WEAPONSELECTBACKUP );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteShort( g_pLastWeaponClass->getActorNetworkIndex( ));
	CLIENT_GetLocalBuffer( )->ByteStream.WriteLong( g_ulLastWeaponSelectTime );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_Taunt( void )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_TAUNT );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_Spectate( void )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_SPECTATE );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_RequestJoin( const char *pszJoinPassword )
{
	if (( sv_limitcommands ) && ( g_ulLastJoinTime > 0 ) && ( (ULONG)gametic < ( g_ulLastJoinTime + ( TICRATE * 3 ))))
	{
		// [AK] Show the client how many more seconds they need to wait before they can join again.
		Printf( "You must wait %d seconds before joining again.\n", static_cast<int>( 3 - ( gametic - g_ulLastJoinTime ) / TICRATE ));
		return;
	}

	g_ulLastJoinTime = gametic;
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_REQUESTJOIN );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteString( pszJoinPassword );

	// [BB/Spleen] Send the gametic so that the client doesn't think it's lagging.
	CLIENT_GetLocalBuffer( )->ByteStream.WriteLong( gametic );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_ChangeRCONStatus( const bool bIsLoggingIn, const char *pszRCONPassword )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_CHANGERCONSTATUS );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( bIsLoggingIn );

	// [AK] It's only necessary to send the RCON password if we're trying to log in.
	if ( bIsLoggingIn )
		CLIENT_GetLocalBuffer( )->ByteStream.WriteString( pszRCONPassword );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_RCONCommand( const char *pszCommand )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_RCONCOMMAND );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteString( pszCommand );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_Suicide( void )
{
	if (( sv_limitcommands ) && ( g_ulLastSuicideTime > 0 ) && ( (ULONG)gametic < ( g_ulLastSuicideTime + ( TICRATE * 10 ))))
	{
		// [AK] Show the client how many more seconds they need to wait before they can suicide again.
		Printf( "You must wait %d seconds before suiciding again.\n", static_cast<int>( 10 - ( gametic - g_ulLastSuicideTime ) / TICRATE ));
		return;
	}

	g_ulLastSuicideTime = gametic;
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_SUICIDE );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_ChangeTeam( const char *pszJoinPassword, LONG lDesiredTeam )
{
	if (( sv_limitcommands ) && !( ( lastmanstanding || teamlms ) && ( LASTMANSTANDING_GetState( ) == LMSS_COUNTDOWN ) ) && ( g_ulLastChangeTeamTime > 0 ) && ( (ULONG)gametic < ( g_ulLastChangeTeamTime + ( TICRATE * 3 ))))
	{
		// [AK] Show the client how many more seconds they need to wait before they can change teams again.
		Printf( "You must wait %d seconds before changing teams again.\n", static_cast<int>( 3 - ( gametic - g_ulLastChangeTeamTime ) / TICRATE ));
		return;
	}

	g_ulLastChangeTeamTime = gametic;
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_CHANGETEAM );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteString( pszJoinPassword );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( lDesiredTeam );

	// [AK] Send the gametic so that the client doesn't think it's lagging.
	CLIENT_GetLocalBuffer( )->ByteStream.WriteLong( gametic );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_SpectateInfo( void )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_SPECTATEINFO );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteLong( gametic );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_GenericCheat( LONG lCheat )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_GENERICCHEAT );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( lCheat );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_GiveCheat( char *pszItem, LONG lAmount )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_GIVECHEAT );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteString( pszItem );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( lAmount );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_TakeCheat( const char *item, LONG amount )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_TAKECHEAT );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteString( item );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( amount );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_SummonCheat( const char *pszItem, LONG lType, const bool bSetAngle, const SHORT sAngle )
{
	int commandtype = 0;

	switch ( lType )
	{
	case DEM_SUMMON: commandtype = CLC_SUMMONCHEAT; break;
	case DEM_SUMMONFRIEND: commandtype = CLC_SUMMONFRIENDCHEAT; break;
	case DEM_SUMMONFOE: commandtype = CLC_SUMMONFOECHEAT; break;
	default: return;
	}

	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( commandtype );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteString( pszItem );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( bSetAngle );
	if ( bSetAngle )
		CLIENT_GetLocalBuffer( )->ByteStream.WriteShort( sAngle );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_ReadyToGoOn( void )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_READYTOGOON );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_ChangeDisplayPlayer( LONG lDisplayPlayer )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_CHANGEDISPLAYPLAYER );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( lDisplayPlayer );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_AuthenticateLevel( void )
{
}

//*****************************************************************************
//
void CLIENTCOMMANDS_CallVote( LONG lVoteCommand, const char *pszArgument, const char *pszReason )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_CALLVOTE );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( lVoteCommand );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteString( pszArgument );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteString( pszReason );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_Vote( bool bVotedYes )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_VOTE );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( bVotedYes );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_RequestInventoryUseAll( void )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_INVENTORYUSEALL );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_RequestInventoryUse( AInventory *item )
{
	if ( item == NULL )
		return;

	const USHORT usActorNetworkIndex = item->GetClass( )->getActorNetworkIndex();

	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_INVENTORYUSE );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteShort( usActorNetworkIndex );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_RequestInventoryDrop( AInventory *pItem )
{
	// [BB] The server may forbid dropping completely.
	if ( zadmflags & ZADF_NODROP )
	{
		Printf( "Dropping items is not allowed in this server.\n" );
		return;
	}

	if ( sv_limitcommands && ( g_ulLastDropTime > 0 ) && ( (ULONG)gametic < g_ulLastDropTime + TICRATE ))
	{
		Printf( "You must wait at least one second before using drop again.\n" );
		return;
	}

	if ( pItem == NULL )
		return;

	const USHORT usActorNetworkIndex = pItem->GetClass( )->getActorNetworkIndex();

	g_ulLastDropTime = gametic;

	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_INVENTORYDROP );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteShort( usActorNetworkIndex );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_Puke ( int scriptNum, int args[4], bool always )
{
	if ( ACS_ExistsScript( scriptNum ) == false )
		return;

	// [TP] Calculate argn from args.
	int argn = ( args[3] != 0 ) ? 4 :
	           ( args[2] != 0 ) ? 3 :
	           ( args[1] != 0 ) ? 2 :
	           ( args[0] != 0 ) ? 1 : 0;

	const int scriptNetID = NETWORK_ACSScriptToNetID( scriptNum );

	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_PUKE );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteLong( scriptNetID );

	// [TP/BB] If we don't have a netID on file for this script, we send the name as a string.
	if ( scriptNetID == NO_SCRIPT_NETID )
		CLIENT_GetLocalBuffer( )->ByteStream.WriteString( FName( ENamedName( -scriptNum )));

	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( argn );

	for ( int i = 0; i < argn; ++i )
		CLIENT_GetLocalBuffer( )->ByteStream.WriteLong( args[i] );

	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( always );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_ACSSendString ( int scriptNum, const char *pszString )
{
	const int scriptNetID = NETWORK_ACSScriptToNetID( scriptNum );

	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_ACSSENDSTRING );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteLong( scriptNetID );

	// [AK] If we don't have a netID on file for this script, we send the name as a string.
	if ( scriptNetID == NO_SCRIPT_NETID )
		CLIENT_GetLocalBuffer( )->ByteStream.WriteString( FName( ENamedName( -scriptNum )));

	CLIENT_GetLocalBuffer( )->ByteStream.WriteString( pszString );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_MorphCheat ( const char *pszMorphClass )
{
	if ( pszMorphClass == NULL )
		return;

	if ( PClass::FindClass ( pszMorphClass ) == NULL )
	{
		Printf ( "Unknown class %s\n", pszMorphClass );
		return;
	}

	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_MORPHEX );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteString( pszMorphClass );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_FullUpdateReceived ( void )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_FULLUPDATE );
}

//*****************************************************************************
// [Dusk]
void CLIENTCOMMANDS_InfoCheat( AActor* mobj, bool extended )
{
	if ( mobj == NULL || mobj->NetID == 0 )
		return;

	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_INFOCHEAT );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteShort( mobj->NetID );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( extended );
}

//*****************************************************************************
// [TP]
void CLIENTCOMMANDS_WarpCheat( fixed_t x, fixed_t y )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_WARPCHEAT );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteLong( x );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteLong( y );
}

//*****************************************************************************
// [TP]
void CLIENTCOMMANDS_KillCheat( const char* what )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_KILLCHEAT );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteString( what );
}

//*****************************************************************************
// [TP]
void CLIENTCOMMANDS_SpecialCheat( int special, const TArray<int> &args )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_SPECIALCHEAT );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( special );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( args.Size() );

	for ( unsigned int i = 0; i < args.Size(); ++i )
		CLIENT_GetLocalBuffer( )->ByteStream.WriteLong( args[i] );
}

//*****************************************************************************
// [TP/AK]
void CLIENTCOMMANDS_SetWantHideInfo( const int info, const bool value )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_SETWANTHIDEINFO );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( info );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( value );
}

//*****************************************************************************
// [TP]
void CLIENTCOMMANDS_SetVideoResolution()
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_SETVIDEORESOLUTION );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteShort( SCREENWIDTH );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteShort( SCREENHEIGHT );
}

//*****************************************************************************
// [TP]
void CLIENTCOMMANDS_RCONSetCVar( const char *cvarName, const char *cvarValue )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_RCONSETCVAR );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteString( cvarName );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteString( cvarValue );
}

//*****************************************************************************
// [AK]
void CLIENTCOMMANDS_VoIPAudioPacket( const unsigned int frame, const unsigned char *data, const unsigned int length )
{
	if (( data == nullptr ) || ( length == 0 ))
		return;

	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_VOIPAUDIOPACKET );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteLong( frame );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteShort( length );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteBuffer( data, length );
}

//*****************************************************************************
// [AK]
void CLIENTCOMMANDS_SetVoIPChannelVolume( const unsigned int player, const float volume )
{
	if ( PLAYER_IsValidPlayer( player ) == false )
		return;

	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_SETVOIPCHANNELVOLUME );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( player );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteFloat( volume );
}

//*****************************************************************************
// [SB]
void CLIENTCOMMANDS_ConversationReply( int selection )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_CONVERSATIONREPLY );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteLong( selection );
}

//*****************************************************************************
// [SB]
void CLIENTCOMMANDS_ConversationClose( )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_CONVERSATIONCLOSE );
}
