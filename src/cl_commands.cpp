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
static	bool g_bIgnoreWeaponSelect = false;
SDWORD g_sdwCheckCmd = 0;

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
void CLIENTCOMMANDS_StartChat( void )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_STARTCHAT );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_EndChat( void )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_ENDCHAT );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_EnterConsole( void )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_ENTERCONSOLE );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_ExitConsole( void )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_EXITCONSOLE );
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
void CLIENTCOMMANDS_Ignore( ULONG ulPlayer, bool bIgnore, LONG lTicks )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_IGNORE );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( ulPlayer );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( bIgnore );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteLong( lTicks );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_ClientMove( void )
{
	ticcmd_t	*pCmd;
	ULONG		ulBits;

	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_CLIENTMOVE );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteLong( gametic );
	// [CK] Send the server the latest known server-gametic
	CLIENT_GetLocalBuffer( )->ByteStream.WriteLong( CLIENT_GetLatestServerGametic( ) + CLIENT_GetServerGameticOffset( ) );

	// Decide what additional information needs to be sent.
	ulBits = 0;
	pCmd = &players[consoleplayer].cmd;

	// [BB] If we think that we can't move, don't even try to tell the server that we
	// want to move.
	if ( players[consoleplayer].mo->reactiontime )
	{
		pCmd->ucmd.forwardmove = 0;
		pCmd->ucmd.sidemove = 0;
		pCmd->ucmd.buttons &= ~BT_JUMP;
	}

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
	CLIENT_GetLocalBuffer( )->ByteStream.WriteLong( g_sdwCheckCmd );

	// Attack button.
	if ( pCmd->ucmd.buttons & BT_ATTACK )
	{
		if ( players[consoleplayer].ReadyWeapon == NULL )
			CLIENT_GetLocalBuffer( )->ByteStream.WriteShort( 0 );
		else
			CLIENT_GetLocalBuffer( )->ByteStream.WriteShort( players[consoleplayer].ReadyWeapon->GetClass( )->getActorNetworkIndex() );
	}
}

//*****************************************************************************
//
void CLIENTCOMMANDS_MissingPacket( void )
{
}

//*****************************************************************************
//
void CLIENTCOMMANDS_Pong( ULONG ulTime )
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
	TempBuffer.Clear();
	TempBuffer.ByteStream.WriteByte( CLC_PONG );
	TempBuffer.ByteStream.WriteLong( ulTime );
	NETWORK_LaunchPacket( &TempBuffer, NETWORK_GetFromAddress( ) );
	TempBuffer.Free();
}

//*****************************************************************************
//
void CLIENTCOMMANDS_WeaponSelect( const PClass *pType )
{
	if ( ( pType == NULL ) || g_bIgnoreWeaponSelect )
		return;

	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_WEAPONSELECT );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteShort( pType->getActorNetworkIndex() );
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
		Printf( "You must wait at least 3 seconds before joining again.\n" );
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
void CLIENTCOMMANDS_RequestRCON( const char *pszRCONPassword )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_REQUESTRCON );
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
		Printf( "You must wait at least 10 seconds before suiciding again.\n" );
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
		Printf( "You must wait at least 3 seconds before changing teams again.\n" );
		return;
	}

	g_ulLastChangeTeamTime = gametic;
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_CHANGETEAM );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteString( pszJoinPassword );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( lDesiredTeam );
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
void CLIENTCOMMANDS_VoteYes( void )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_VOTEYES );
}

//*****************************************************************************
//
void CLIENTCOMMANDS_VoteNo( void )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_VOTENO );
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
	if ( mobj == NULL || mobj->lNetID == -1 )
		return;

	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_INFOCHEAT );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteShort( mobj->lNetID );
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
// [TP]
void CLIENTCOMMANDS_SetWantHideAccount( bool wantHideAccount )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_SETWANTHIDEACCOUNT );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( wantHideAccount );
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
