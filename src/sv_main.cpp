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
// Filename: sv_main.cpp
//
// Description: Contains variables and routines related to the server portion
// of the program.
//
//-----------------------------------------------------------------------------

#include <algorithm>
#include <iterator>
#include <cmath>
#include <stdarg.h>
#include <time.h>

#include "networkheaders.h"

#include "../upnpnat/upnpnat.h"

// [BB] Needed for timeGetTime() in SERVER_Tick().
#ifdef _MSC_VER
#include <mmsystem.h>
#endif 

#include "a_doomglobal.h"
#include "a_keys.h"
#include "a_pickups.h"
#include "botcommands.h"
#include "callvote.h"
#include "cooperative.h"
#include "deathmatch.h"
#include "duel.h"
#include "doomtype.h"
#include "doomstat.h"
#include "d_player.h"
#include "s_sound.h"
#include "gi.h"
#include "d_net.h"
#include "g_game.h"
#include "p_local.h"
#include "sv_main.h"
#include "sv_ban.h"
#include "i_system.h"
#include "c_console.h"
#include "c_dispatch.h"
#include "m_argv.h"
#include "m_random.h"
#include "network.h"
#include "possession.h"
#include "s_sndseq.h"
#include "version.h"
#include "v_text.h"
#include "w_wad.h"
#include "p_acs.h"
#include "p_effect.h"
#include "m_cheat.h"
#include "stats.h"
#include "team.h"
#include "chat.h"
#include "v_palette.h"
#include "v_video.h"
#include "templates.h"
#include "joinqueue.h"
#include "invasion.h"
#include "lastmanstanding.h"
#include "survival.h"
#include "sv_commands.h"
#include "sv_save.h"
#include "sv_rcon.h"
#include "gamemode.h"
#include "domination.h"
#include "a_movingcamera.h"
#include "cl_main.h"
#include "d_netinf.h"
#include "po_man.h"
#include "network/cl_auth.h"
#include "network/sv_auth.h"
#include "r_data/colormaps.h"
#include "network_enums.h"
#include "d_protocol.h"
#include "p_conversation.h"
#include "p_enemy.h"
#include "network/packetarchive.h"
#include "p_lnspec.h"
#include "unlagged.h"
#include "scoreboard.h"

//*****************************************************************************
//	MISC CRAP THAT SHOULDN'T BE HERE BUT HAS TO BE BECAUSE OF SLOPPY CODING

FPolyObj	*GetPolyobj( int polyNum );
FPolyObj	*GetPolyobjByIndex( ULONG ulPoly );
extern fixed_t	sidemove[2];

void SERVERCONSOLE_UpdatePlayerInfo( LONG lPlayer, ULONG ulUpdateFlags );
void SERVERCONSOLE_ReListPlayers( void );

EXTERN_CVAR( Bool, sv_cheats );
EXTERN_CVAR( Bool, sv_showwarnings );
EXTERN_CVAR( Bool, sv_unlagged_debugactors )

//*****************************************************************************
//	PROTOTYPES

static	bool	server_Ignore( BYTESTREAM_s *pByteStream );
static	bool	server_Say( BYTESTREAM_s *pByteStream );
static	bool	server_ClientMove( BYTESTREAM_s *pByteStream, bool bSentBackup );
static	bool	server_MissingPacket( BYTESTREAM_s *pByteStream );
static	bool	server_UpdateClientPing( BYTESTREAM_s *pByteStream );
static	bool	server_WeaponSelect( BYTESTREAM_s *pByteStream, bool bSentBackup );
static	bool	server_Taunt( BYTESTREAM_s *pByteStream );
static	bool	server_Spectate( BYTESTREAM_s *pByteStream );
static	bool	server_RequestJoin( BYTESTREAM_s *pByteStream );
static	bool	server_RequestRCON( BYTESTREAM_s *pByteStream );
static	bool	server_RCONCommand( BYTESTREAM_s *pByteStream );
static	bool	server_Suicide( BYTESTREAM_s *pByteStream );
static	bool	server_ChangeTeam( BYTESTREAM_s *pByteStream );
static	bool	server_SpectateInfo( BYTESTREAM_s *pByteStream );
static	bool	server_GenericCheat( BYTESTREAM_s *pByteStream );
static	bool	server_GiveCheat( BYTESTREAM_s *pByteStream, bool take );
static	bool	server_SummonCheat( BYTESTREAM_s *pByteStream, LONG lType );
static	bool	server_ChangeDisplayPlayer( BYTESTREAM_s *pByteStream );
static	bool	server_AuthenticateLevel( BYTESTREAM_s *pByteStream );
static	bool	server_CallVote( BYTESTREAM_s *pByteStream );
static	bool	server_InventoryUseAll( BYTESTREAM_s *pByteStream );
static	bool	server_InventoryUse( BYTESTREAM_s *pByteStream );
static	bool	server_InventoryDrop( BYTESTREAM_s *pByteStream );
static	bool	server_Puke( BYTESTREAM_s *pByteStream );
static	bool	server_ReceiveACSString( BYTESTREAM_s *pByteStream );
static	bool	server_MorphCheat( BYTESTREAM_s *pByteStream );
static	bool	server_CheckForClientCommandFlood( ULONG ulClient );
static	bool	server_CheckForClientMinorCommandFlood( ULONG ulClient );
static	bool	server_CheckJoinPassword( const FString& clientPassword );
static	bool	server_InfoCheat( BYTESTREAM_s* pByteStream );
static	bool	server_CheckLogin( const ULONG ulClient );
static	void	server_PrintWithIP( FString message, const NETADDRESS_s &address );
static	void	server_PerformBacktrace( ULONG ulClient, ULONG ulNumLateMoveCMDs );
static	bool	server_ShouldPerformBacktrace( ULONG ulClient );
static	void	server_FixZFromBacktrace( APlayerPawn *pmo, fixed_t oldFloorZ );
static	void	server_ForceRenamePlayer( ULONG playerIndex ); // [SB]

// [RC]
#ifdef CREATE_PACKET_LOG
static  void	server_LogPacket( BYTESTREAM_s *pByteStream, NETADDRESS_s Address, const char *pszReason );
#endif

//*****************************************************************************
//	VARIABLES

// Global array of clients.
static	CLIENT_s		g_aClients[MAXPLAYERS];

// The last client we received a packet from.
static	LONG			g_lCurrentClient;

// Number of ticks that have passed since start of... level?
static	unsigned int	g_GameTime = 0;

// [AK] How many ticks to shift to ensure that the tick rate remains at 35 ticks per
// second if overflows occur when getting "new" and "previous" ticks in SERVER_Tick.
static	double			g_GameTicShift = 0.0;

#ifndef NO_SERVER_GUI
// Storage for commands issued through various menu options to be executed all at once.
static	TArray<FString>	g_ServerCommandQueue;
#endif

// Timer for restarting the map.
static	LONG			g_lMapRestartTimer;

// List of IP addresses that may connect to full servers.
static	IPList			g_AdminIPList;

// List of IP address that we want to ignore for a short amount of time.
static	QueryIPQueue	g_floodProtectionIPQueue( 10 );

// [BB] String to verify that the ban list was actually sent from the master server.
// It's generated randomly on startup by the server.
static	FString			g_MasterBanlistVerificationString;

// Statistics.
static	LONG		g_lTotalServerSeconds = 0;
static	LONG		g_lTotalNumPlayers = 0;
static	LONG		g_lMaxNumPlayers = 0;
static	LONG		g_lTotalNumFrags = 0;
static	QWORD		g_qwTotalOutboundDataTransferred = 0;
static	LONG		g_lMaxOutboundDataTransfer = 0;
static	LONG		g_lCurrentOutboundDataTransfer = 0;
static	LONG		g_lOutboundDataTransferLastSecond = 0;
static	QWORD		g_qwTotalInboundDataTransferred = 0;
static	LONG		g_lMaxInboundDataTransfer = 0;
static	LONG		g_lCurrentInboundDataTransfer = 0;
static	LONG		g_lInboundDataTransferLastSecond = 0;

// This is the current font the "screen" is using when it displays messages.
static	char		g_szCurrentFont[16];

// This is the music the loaded map is currently using.
static	FString		g_MapMusic;
static	int			g_MapMusicOrder;

// Maximum packet size.
static	ULONG		g_ulMaxPacketSize = 0;

// List of all translations edited by level scripts.
static	TArray<EDITEDTRANSLATION_s>		g_EditedTranslationList;

// [BB] List of all sector links created by calls to Sector_SetLink.
static	TArray<SECTORLINK_s>		g_SectorLinkList;

// [AK] List of all actor sound channels containing looping sounds.
static	TArray<FSoundChan>		g_LoopingChannelList;

// [RC] File to log packets to.
#ifdef CREATE_PACKET_LOG
static	FILE		*PacketLogFile = NULL;
static	IPList		g_HackerIPList;

CUSTOM_CVAR( String, sv_hackerlistfile, "hackerlist.txt", CVAR_ARCHIVE|CVAR_NOSETBYACS )
{
	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
		return;

	if ( !(g_HackerIPList.clearAndLoadFromFile( sv_hackerlistfile.GetGenericRep( CVAR_String ).String ) ) )
		Printf( "%s", g_HackerIPList.getErrorMessage() );
}

#endif

//*****************************************************************************
//	CONSOLE VARIABLES

CVAR( String, sv_motd, "", CVAR_ARCHIVE )
CVAR( Bool, sv_defaultdmflags, false, 0 )
CVAR( Bool, sv_forcepassword, false, CVAR_ARCHIVE|CVAR_NOSETBYACS|CVAR_SERVERINFO )
CVAR( Bool, sv_forcejoinpassword, false, CVAR_ARCHIVE|CVAR_NOSETBYACS|CVAR_SERVERINFO )
CVAR( Int, sv_forcerespawntime, 0, CVAR_ARCHIVE|CVAR_SERVERINFO|CVAR_GAMEPLAYSETTING ) // [RK]
CVAR( Bool, sv_showlauncherqueries, false, CVAR_ARCHIVE )
CVAR( Bool, sv_timestamp, false, CVAR_ARCHIVE|CVAR_NOSETBYACS )
CVAR( Int, sv_timestampformat, 0, CVAR_ARCHIVE|CVAR_NOSETBYACS )
CVAR( Int, sv_colorstripmethod, 0, CVAR_ARCHIVE )
CVAR( Bool, sv_minimizetosystray, true, CVAR_ARCHIVE )
CVAR( Int, sv_queryignoretime, 10, CVAR_ARCHIVE )
CVAR( Bool, sv_markchatlines, false, CVAR_ARCHIVE )
CVAR( Int, sv_distinguishteamchatlines, 0, CVAR_ARCHIVE )
CVAR( Flag, sv_nokill, dmflags2, DF2_NOSUICIDE )
CVAR( Bool, sv_pure, true, CVAR_SERVERINFO | CVAR_LATCH )
CVAR( Int, sv_maxclientsperip, 2, CVAR_ARCHIVE | CVAR_SERVERINFO )
CVAR( Int, sv_afk2spec, 0, CVAR_ARCHIVE | CVAR_SERVERINFO ) // [K6]
CVAR( Bool, sv_forcelogintojoin, false, CVAR_ARCHIVE|CVAR_NOSETBYACS )
CVAR( Bool, sv_useticbuffer, true, CVAR_ARCHIVE|CVAR_NOSETBYACS|CVAR_DEBUGONLY )
CVAR( Int, sv_showcommands, 0, CVAR_ARCHIVE|CVAR_DEBUGONLY )
CVAR( Int, sv_smoothplayers_debuginfo, 0, CVAR_ARCHIVE|CVAR_DEBUGONLY ) // [AK]
CVAR( Bool, sv_noplayertimeout, false, CVAR_NOSETBYACS|CVAR_DEBUGONLY ) // [SB]
CVAR( Bool, sv_printconnectionmessages, true, CVAR_ARCHIVE|CVAR_NOSETBYACS ) // [SB]

//*****************************************************************************
// [AK] Smooths the movement of lagging players using extrapolation and correction.
CUSTOM_CVAR( Int, sv_smoothplayers, 0, CVAR_ARCHIVE|CVAR_NOSETBYACS|CVAR_SERVERINFO|CVAR_DEBUGONLY )
{
	// [AK] We can't extrapolate for a negative number of tics.
	if ( self < 0 )
	{
		self = 0;
		return;
	}

	// [AK] Don't extrapolate for more than 3 tics. The longer the server has to predict
	// a player's movement, the more likely that prediction errors will occur.
	if ( self > 3 )
	{
		Printf( "A player's movement can only be extrapolated for up to 3 tics.\n" );
		self = 3;
		return;
	}

	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
	{
		static int oldValue = self;

		// [AK] Print a message indicating that the skip correction is enabled/disabled.
		if ( self != oldValue )
		{
			// [AK] Reset the extrapolation for all clients if we disable the skip correction.
			if ( !self )
			{
				for ( ULONG ulClient = 0; ulClient < MAXPLAYERS; ulClient++ )
					SERVER_ResetClientExtrapolation( ulClient );

				SERVER_Printf( "Skip correction disabled.\n" );
			}
			else
			{
				SERVER_Printf( "Players will now be extrapolated for %d tics.\n", *self );
			}

			oldValue = self;

			// [AK] Notify the clients about any changes to the skip correction.
			SERVERCOMMANDS_SetCVar( self );
		}
	}
}

//*****************************************************************************
//
CUSTOM_CVAR( String, sv_adminlistfile, "adminlist.txt", CVAR_ARCHIVE|CVAR_SENSITIVESERVERSETTING|CVAR_NOSETBYACS )
{
	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
		return;

	if ( !(g_AdminIPList.clearAndLoadFromFile( sv_adminlistfile ) ) )
		Printf( "%s", g_AdminIPList.getErrorMessage() );
}

//*****************************************************************************
// [BB] To stay compatible with old mods, the default value is at most 32.
CUSTOM_CVAR( Int, sv_maxclients, MIN ( MAXPLAYERS, 32 ), CVAR_ARCHIVE | CVAR_SERVERINFO )
{
	if ( self < 0 )
		self = 0;

	if ( self > MAXPLAYERS )
	{
		Printf( "sv_maxclients must be less than or equal to %d.\n", MAXPLAYERS );
		self = MAXPLAYERS;
	}
}

//*****************************************************************************
// [BB] To stay compatible with old mods, the default value is at most 32.
CUSTOM_CVAR( Int, sv_maxplayers, MIN ( MAXPLAYERS, 32 ), CVAR_ARCHIVE | CVAR_SERVERINFO | CVAR_GAMEPLAYSETTING )
{
	if ( self < 0 )
		self = 0;

	if ( self > MAXPLAYERS )
	{
		Printf( "sv_maxplayers must be less than or equal to %d.\n", MAXPLAYERS );
		self = MAXPLAYERS;
	}

	// [BB] Possibly the new sv_maxplayers value allows some players from the queue to join.
	JOINQUEUE_PopQueue( -1 );
}

//*****************************************************************************
//
CUSTOM_CVAR( String, sv_password, "password", CVAR_ARCHIVE|CVAR_NOSETBYACS|CVAR_SENSITIVESERVERSETTING )
{
	if ( strlen( self ) > 0 && strlen( self ) <= 4 )
	{
		Printf( "sv_password must be greater than 4 chars in length!\n" );
		self = "password";
	}
}

//*****************************************************************************
//
CUSTOM_CVAR( String, sv_joinpassword, "password", CVAR_ARCHIVE|CVAR_NOSETBYACS|CVAR_SENSITIVESERVERSETTING )
{
	if ( strlen( self ) > 0 && strlen( self ) <= 4 )
	{
		Printf( "sv_joinpassword must be greater than 4 chars in length!\n" );
		self = "password";
	}
}

//*****************************************************************************
//
CUSTOM_CVAR( String, sv_rconpassword, "", CVAR_ARCHIVE|CVAR_NOSETBYACS|CVAR_SENSITIVESERVERSETTING )
{
	if ( strlen( self ) > 0 && strlen( self ) <= 4 )
	{
		Printf( "sv_rconpassword must be greater than 4 chars in length!\n" );
		self = "";
	}
}

//*****************************************************************************
//
CUSTOM_CVAR( Int, sv_maxpacketsize, 1024, CVAR_ARCHIVE | CVAR_SERVERINFO )
{
	if ( self > MAX_UDP_PACKET )
	{
		Printf( "sv_maxpacketsize cannot exceed %d bytes.\n", MAX_UDP_PACKET );
		self = MAX_UDP_PACKET;
	}

	if (( gamestate != GS_STARTUP ) && ( NETWORK_GetState( ) == NETSTATE_SERVER ))
		Printf( "The server must be restarted before this change will take effect.\n" );
}

//*****************************************************************************
// [TP] Whether to enforce command limits. Set this false to disable
// flood protection.
//
CUSTOM_CVAR( Bool, sv_limitcommands, true, CVAR_ARCHIVE | CVAR_NOSETBYACS | CVAR_SERVERINFO | CVAR_DEBUGONLY )
{
	// [TP] The client also enforces command limits so this cvar must be synced.
	if ( NETWORK_GetState() == NETSTATE_SERVER )
		SERVERCOMMANDS_SetGameModeLimits();
}

//*****************************************************************************
//
CUSTOM_CVAR( Int, sv_allowprivatechat, PRIVATECHAT_EVERYONE, CVAR_ARCHIVE | CVAR_NOSETBYACS | CVAR_SERVERINFO )
{
	if ( self < PRIVATECHAT_OFF )
	{
		self = PRIVATECHAT_OFF;
		return;
	}
	else if ( self > PRIVATECHAT_TEAMMATESONLY )
	{
		self = PRIVATECHAT_TEAMMATESONLY;
		return;
	}

	// [AK] Notify the clients about the change.
	SERVER_SettingChanged( self, false );
}

//*****************************************************************************
//
CUSTOM_CVAR( Float, sv_respawndelaytime, 1.0f, CVAR_ARCHIVE | CVAR_SERVERINFO | CVAR_GAMEPLAYSETTING )
{
	// [AK] The respawn delay time should always be at least a tic long.
	if ( self <= 0.0f )
	{
		self = 1.0f / TICRATE;
		return;
	}

	// [AK] Notify the clients about the change.
	SERVER_SettingChanged( self, false, 1 );
}

//*****************************************************************************
//	FUNCTIONS

void SERVER_Construct( void )
{
	const char	*pszPort;
	const char	*pszMaxClients;
	ULONG		ulIdx;
	USHORT		usPort;

	// Check if the user wants to use an alternate port for the server.
	pszPort = Args->CheckValue( "-port" );
    if ( pszPort )
    {
       usPort = atoi( pszPort );
       Printf( PRINT_HIGH, "Server using alternate port %i.\n", usPort );
    }
	else 
	   usPort = DEFAULT_SERVER_PORT;

	// Set up a socket and network message buffer.
	NETWORK_Construct( usPort, false );

	// [BB] Forward the external port with UPnP.
	if ( Args->CheckParm ( "-upnp" ) )
	{
		UPNPNAT nat;
		nat.init(5,10);

		int externalPort = NETWORK_GetLocalPort();

		if ( Args->CheckValue( "-upnp" ) != NULL )
			externalPort = atoi( Args->CheckValue( "-upnp" ) );

		if( !nat.discovery() )
		{
			Printf( "NAT discovery error: %s\n", nat.get_last_error() );
		}
		else if ( !nat.add_port_mapping("test", NETWORK_GetCachedLocalAddress().ToStringNoPort(), externalPort, NETWORK_GetLocalPort(),"UDP") )
		{
			Printf( "Error adding port mapping: %s\n",nat.get_last_error() );
		}
		else
			Printf( "Sucessfully added port mapping.\n" );
	}

	// Initizlize the playeringame array (is this necessary?).
	for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ++ulIdx )
		playeringame[ulIdx] = false;

	// Initialize clients.
	g_ulMaxPacketSize = sv_maxpacketsize;
	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
    {
		g_aClients[ulIdx].PacketBuffer.Init( MAX_UDP_PACKET, BUFFERTYPE_WRITE );

		// Initialize the saved packet buffer.
		g_aClients[ulIdx].SavedPackets.Initialize( g_ulMaxPacketSize );
		g_aClients[ulIdx].SavedPackets.SetClientIndex ( ulIdx );

		// Initialize the unreliable packet buffer.
		g_aClients[ulIdx].UnreliablePacketBuffer.Init( MAX_UDP_PACKET, BUFFERTYPE_WRITE );

		// This is currently an open slot.
		g_aClients[ulIdx].State = CLS_FREE;
	}

	// If they used "-host <#>", make <#> the max number of players.
	pszMaxClients = Args->CheckValue( "-host" );
	if ( pszMaxClients )
		sv_maxclients = atoi( pszMaxClients );

#ifndef NO_SERVER_GUI
	g_ServerCommandQueue.Clear( );
#endif

	// [BB] Initialize g_MasterBanlistVerificationString.
	{
		FString randomString;
		for ( int i = 0; i < 100; ++i )
			randomString += static_cast<char>(M_Random( ));

		CMD5Checksum::GetMD5( reinterpret_cast<const BYTE*>(randomString.GetChars()), randomString.Len(), g_MasterBanlistVerificationString );
	}

	g_lMapRestartTimer = 0;

	sprintf( g_szCurrentFont, "SmallFont" );

#ifdef CREATE_PACKET_LOG

	// [RC] Create the packet log.
	FString logfilename;
	time_t clock;
	struct tm *lt;
	time (&clock);
	lt = localtime (&clock);
	if (lt != NULL)
		logfilename.Format("packetLog_%d_%02d_%02d-%02d_%02d_%02d.txt", lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min, lt->tm_sec);
	else
		logfilename.Format("packetLog.txt");

	if ( (PacketLogFile = fopen (logfilename, "w")) )
	{
		FString output;
		UCVarValue		Val;
		Val = sv_hostname.GetGenericRep( CVAR_String );
		output.Format("Packet logging started...\n\ton: %d/%d/%d, at %02d:%02d:%02d\n\tat server: %s\n\nDo not distribute this file to the public!\n=====================================================================\n",
			lt->tm_mon + 1, lt->tm_mday, lt->tm_year + 1900, lt->tm_hour, lt->tm_min, lt->tm_sec,  Val.String);
		fputs(output, PacketLogFile);
		Printf("Packet logging enabled. DO NOT DISTRIBUTE THIS EXE TO THE PUBLIC! -- Rivecoder\n");
	}
	else
		Printf("Could not start the packet log.\n"), 

#endif

	// Call SERVER_Destruct() when Skulltag closes.
	atterm( SERVER_Destruct );

	// Setup the child modules.
	SERVER_MASTER_Construct( );
	SERVER_SAVE_Construct( );
	SERVER_RCON_Construct( );

	for (int i = 0; i < MAXPLAYERS; i++)
	{
		players[i].userinfo.Reset();
	}
	// [BB] The voodoo doll dummy player also needs its userinfo reset.
	COOP_InitVoodooDollDummyPlayer();
}

//*****************************************************************************
//
void SERVER_Destruct( void )
{
	ULONG	ulIdx;

	// Free the clients' buffers.
	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		// [TP] If we still do have clients, tell them the server is going down
		if ( SERVER_IsValidClient( ulIdx ))
		{
			SERVER_KickPlayer( ulIdx, "Server is shutting down" );
			NETWORK_LaunchPacket( &SERVER_GetClient( ulIdx )->PacketBuffer, SERVER_GetClient( ulIdx )->Address );
		}

		g_aClients[ulIdx].PacketBuffer.Free();
		g_aClients[ulIdx].UnreliablePacketBuffer.Free();
		g_aClients[ulIdx].SavedPackets.Free();
	}

#ifdef CREATE_PACKET_LOG
	if ( PacketLogFile )
		fclose( PacketLogFile );
#endif
}

//DWORD	g_LastMS, g_LastSec, g_FrameCount, g_LastCount, g_LastTic;

void			SERVERCONSOLE_UpdateStatistics( void );
void			SERVERCONSOLE_UpdateScoreboard( void );

//*****************************************************************************
//
unsigned int server_GetDeltaTicks( unsigned int &nowTime, const unsigned int previousTics )
{
	static bool alreadyShiftedGameTic = false;
	unsigned int newTics = 0;

	nowTime = I_MSTime( );

	// [AK] Check if an integer overflow occurred in the timer (i.e. the "now"
	// time suddenly became smaller than the "previous" time). This happens once
	// every ~49 days when the server runs constantly.
	if ( nowTime < g_GameTime )
	{
		// [AK] First, get the "max" number of ticks if I_MSTime returns its
		// maxiumum possible value. This should be equal to 150323855.325.
		const double maxTics = UINT_MAX / MS_PER_TIC;
		double wholePortion = 0.0;

		// [AK] Next, split the whole (150323855) and fractional (0.325) portions
		// of maxTics from each other.
		const double fractionPortion = std::modf( maxTics, &wholePortion );

		// [AK] Remember that maxTics doesn't round to a whole number completely,
		// meaning that when the overflow occurs, we must compensate for an extra
		// 0.325 of a tic (~11 ms). If we don't do this, the tick rate will briefly
		// become ~1011 ms for 35 ticks, which can cause discrepancies between the
		// server and clients.
		//
		// By adding this to a global tic shift variable, and then calculating our
		// "new" and "previous" ticks based on this shift, we ensure that the tick
		// rate always stays at ~1000 ms for 35 ticks, which is what we want.
		if ( alreadyShiftedGameTic == false )
		{
			g_GameTicShift += fractionPortion;
			alreadyShiftedGameTic = true;
		}

		newTics = static_cast<unsigned>( wholePortion + g_GameTicShift + nowTime / MS_PER_TIC );
	}
	else
	{
		// [AK] Reset this boolean after the integer overflow fixes itself.
		if ( alreadyShiftedGameTic )
			alreadyShiftedGameTic = false;

		newTics = static_cast<unsigned>( g_GameTicShift + nowTime / MS_PER_TIC );
	}

	return newTics - previousTics;
}

//*****************************************************************************
//
void SERVER_Tick( void )
{
	unsigned int nowTime = 0;

	I_DoSelect();

	const unsigned int previousTics = static_cast<unsigned>( g_GameTicShift + g_GameTime / MS_PER_TIC );
	unsigned int deltaTics = server_GetDeltaTicks( nowTime, previousTics );

	while ( deltaTics == 0 )
	{
		// [BB] Recieve packets whenever possible (not only once each tic) to allow
		// for an accurate ping measurement.
		SERVER_GetPackets( );

		I_Sleep( 1 );
		deltaTics = server_GetDeltaTicks( nowTime, previousTics );
	}

#ifdef NO_SERVER_GUI
	// console input
	char *cmd = I_ConsoleInput();
	if (cmd)
		AddCommandString (cmd);
	fflush(stdout);
#else
	// Execute any commands that have been issued through server menus.
	while ( g_ServerCommandQueue.Size( ))
		SERVER_DeleteCommand( );
#endif

	int oldTime = level.time;

	while ( deltaTics-- )
	{
		//DObject::BeginFrame ();

		// Recieve packets.
		SERVER_GetPackets( );

		// [AK] After receiving packets, check if we didn't receive a movement
		// command from an in-game players during this gametic. If that's the
		// case, increment the number of missing packets for the player.
		if ( gamestate == GS_LEVEL )
		{
			for ( unsigned int i = 0; i < MAXPLAYERS; i++ )
			{
				if (( SERVER_IsValidClient( i ) == false ) || ( players[i].bSpectating ))
					continue;

				// [AK] If we didn't receive the command from them because they
				// recently joined the game and it's still taking time for the
				// commands to arrive (i.e. their last move tick is still zero),
				// don't treat it as a missing packet.
				if (( g_aClients[i].lLastMoveTick != 0 ) && ( g_aClients[i].lLastMoveTick != gametic ))
					g_aClients[i].numMissingPackets++;
			}
		}

		// We have to record player positions before their mobj moves.
		// [BB] Tick the unlagged module.
		UNLAGGED_Tick( );

		G_Ticker ();

		// However we need to spawn the unlagged debug actors here i.e. after having processed their
		// movement commands which updated their last server gametic.
		// [BB] Spawn debug actors if the server runner wants them.
		if ( sv_unlagged_debugactors )
			UNLAGGED_SpawnDebugActors( );

		gametic++;
		maketic++;

		// Update the scoreboard if we have a new second to display.
		if ( timelimit && (( level.time % TICRATE ) == 0 ) && ( level.time != oldTime ))
		{
			SERVERCONSOLE_UpdateScoreboard( );
			oldTime = level.time;
		}

		if ( g_lMapRestartTimer > 0 )
		{
			if ( --g_lMapRestartTimer == 0 )
			{
				FString string;

				if ( GAMEMODE_IsNextMapCvarLobby( ) )
				{
					// [AM] If we're using a lobby map, reset to the lobby.
					//      In theory, there can be many MAPINFO-lobbies, but there is only
					//      one lobby cvar setting, so we only need to bother with the cvar.
					string.Format( "map %s", *lobby );
				}
				else
				{
					string.Format( "map %s", level.mapname );
				}

				AddCommandString( string.LockBuffer() );
				string.UnlockBuffer();
			}
		}

		// Drop anyone who's been disconnected.
		SERVER_CheckTimeouts( );

		// Send out player's true position, etc.
		SERVER_WriteCommands( );

		// Check everyone's PacketBuffer for anything that needs to be sent.
		SERVER_SendOutPackets( );

		// [BB] Send out sheduled packets, respecting sv_maxpacketspertick.
		for ( unsigned int i = 0; i < MAXPLAYERS; i++ )
		{
			if ( g_aClients[i].State == CLS_FREE )
				continue;

			SERVER_GetClient ( i )->SavedPackets.Tick ( );
		}

		// Potentially send an update to the master server.
		SERVER_MASTER_Tick( );

		// Time out any old RCON sessions.
		SERVER_RCON_Tick( );

		// Broadcast the server signal so it can be detected on a LAN.
		SERVER_MASTER_Broadcast( );

		// Potentially re-parse the banfile.
		SERVERBAN_Tick( );

		// Print stats and get out.
		FStat::PrintStat( );

		for ( unsigned int i = 0; i < MAXPLAYERS; i++ )
		{
			if (( SERVER_IsValidClient( i ) == false ) || ( players[i].bSpectating ))
				continue;

			if ( g_aClients[i].lLastMoveTick != gametic && g_aClients[i].lOverMovementLevel > -MAX_OVERMOVEMENT_LEVEL )
			{
				g_aClients[i].lOverMovementLevel--;
//					Printf( "%s: -- (%d)\n", players[i].userinfo.GetName(), g_aClients[i].lOverMovementLevel );
			}

			// [BB] If the client didn't authenticate the new map by now, likely his authentication packet was lost.
			// Ask him to authenticate again.
			if ( ( SERVER_GetClient( i )->State == CLS_SPAWNED_BUT_NEEDS_AUTHENTICATION ) && ( ( level.maptime % ( 2 * TICRATE ) ) == 0 ) )
				SERVERCOMMANDS_MapAuthenticate ( level.mapname, i, SVCF_ONLYTHISCLIENT );
		}

		// Do some statistic stuff every second.
		if (( gametic % TICRATE ) == 0 )
		{
			// Increase the number of seconds the server has been active.
			g_lTotalServerSeconds++;

			// Count the number of active players.
			LONG currentNumPlayers = 0;
			for ( unsigned int i = 0; i < MAXPLAYERS; i++ )
			{
				if ( SERVER_IsValidClient( i ) == false )
					continue;

				g_lTotalNumPlayers++; // Divided by g_lTotalServerSeconds to form an average.
				currentNumPlayers++;
			}

			// Check for new peak records!
			if ( currentNumPlayers > g_lMaxNumPlayers )
				g_lMaxNumPlayers = currentNumPlayers;
			if ( g_lCurrentOutboundDataTransfer > g_lMaxOutboundDataTransfer )
				g_lMaxOutboundDataTransfer = g_lCurrentOutboundDataTransfer;
			if ( g_lCurrentInboundDataTransfer > g_lMaxInboundDataTransfer )
				g_lMaxInboundDataTransfer = g_lCurrentInboundDataTransfer;

			// Update "current" outbound data.
			g_lOutboundDataTransferLastSecond = g_lCurrentOutboundDataTransfer;
			g_lCurrentOutboundDataTransfer = 0;
			g_lInboundDataTransferLastSecond = g_lCurrentInboundDataTransfer;
			g_lCurrentInboundDataTransfer = 0;

			// Update the form.
			SERVERCONSOLE_UpdateStatistics( );
		}

		//DObject::EndFrame ();
	}
/*
	if ( 1 )
	{
		QWORD ms = I_MSTime ();
		DWORD howlong = DWORD(ms - g_LastMS);
		if (howlong > 0)
		{
			DWORD thisSec = ms/1000;

			if (g_LastSec < thisSec)
			{
//					Printf( "%2lu ms (%3lu fps)\n", howlong, g_LastCount );

				g_LastCount = g_FrameCount / (thisSec - g_LastSec);
				g_LastSec = thisSec;
				g_FrameCount = 0;
			}
			g_FrameCount++;
		}
		g_LastMS = ms;
	}
*/
	g_GameTime = nowTime;

	// [BB] Remove IP adresses from g_floodProtectionIPQueue that have been in there long enough.
	g_floodProtectionIPQueue.adjustHead ( g_GameTime / 1000 );

	for ( unsigned int i = 0; i < MAXPLAYERS; i++ )
	{
		if (( SERVER_IsValidClient( i ) == false ) || ( players[i].bSpectating ))
			continue;

		if ( g_aClients[i].lOverMovementLevel >= MAX_OVERMOVEMENT_LEVEL )
			SERVER_KickPlayer( i, "Abnormal level of movement commands detected!" );
	}
}

//*****************************************************************************
//*****************************************************************************
//
void SERVER_SendOutPackets( void )
{
	ULONG	ulIdx;

	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if ( SERVER_IsValidClient( ulIdx ) == false )
			continue;

		if ( g_aClients[ulIdx].PacketBuffer.CalcSize() > 0 )
			SERVER_SendClientPacket( ulIdx, true );

		if ( g_aClients[ulIdx].UnreliablePacketBuffer.CalcSize() > 0 )
			SERVER_SendClientPacket( ulIdx, false );
	}
}

//*****************************************************************************
//
void SERVER_SendClientPacket( ULONG ulClient, bool bReliable )
{
    NETBUFFER_s		TempBuffer;
	BYTE			abData[MAX_UDP_PACKET];
	CLIENT_s		*pClient;

	pClient = SERVER_GetClient( ulClient );
	if ( pClient == NULL )
		return;

	if ( bReliable )
	{
		pClient->SavedPackets.ScheduleUnsentPacket( pClient->PacketBuffer );
		pClient->PacketBuffer.Clear();
		return;
	}

	TempBuffer.pbData = abData;
	TempBuffer.ulMaxSize = sizeof( abData );
	TempBuffer.ulCurrentSize = 0;
	TempBuffer.BufferType = BUFFERTYPE_WRITE;
	TempBuffer.ByteStream.pbStream = abData;
	TempBuffer.ByteStream.pbStreamEnd = TempBuffer.ByteStream.pbStream + TempBuffer.ulMaxSize;

	// Write the header to our temporary buffer.
	TempBuffer.ByteStream.WriteByte( SVC_UNRELIABLEPACKET );

	// Write the body of the message to our temporary buffer.
	pClient->UnreliablePacketBuffer.WriteTo ( TempBuffer.ByteStream );

	// Finally, send the packet, and clear the buffer.
	NETWORK_LaunchPacket( &TempBuffer, pClient->Address );
	pClient->UnreliablePacketBuffer.Clear();
}

//*****************************************************************************
//
void SERVER_CheckClientBuffer( ULONG ulClient, ULONG ulSize, bool bReliable )
{
	NETBUFFER_s	*pBuffer;
	CLIENT_s	*pClient;

	pClient = SERVER_GetClient( ulClient );
	if ( pClient == NULL )
		return;

	if ( bReliable )
		pBuffer = &pClient->PacketBuffer;
	else
		pBuffer = &pClient->UnreliablePacketBuffer;

	// Make sure we have enough room for the upcoming message. If not, send
	// out the current buffer and clear the packet.
	pBuffer->ulCurrentSize = pBuffer->ByteStream.pbStream - pBuffer->pbData;
	if (( pBuffer->ulCurrentSize + ( ulSize + 5 )) >= SERVER_GetMaxPacketSize( ))
	{
		if ( debugfile )
			fprintf( debugfile, "Launching premature packet: %d\n", bReliable );

		// Lanch the packet so we can prepare another.
		SERVER_SendClientPacket( ulClient, bReliable );
	}
}

//*****************************************************************************
//
LONG SERVER_FindFreeClientSlot( void )
{
	ULONG	ulIdx;
	ULONG	ulNumSlots;

	// [BB] Even if maxclients is reached, we allow one additional connection from localhost.
	// The check (SERVER_CountPlayers( true ) >= sv_maxclients) for a non local connection
	// attempt is done in SERVER_SetupNewConnection.
	ulNumSlots = sv_maxclients + 1;

	// If the number of players in the game is greater than or equal to the number of current
	// players, disallow the join.
	if ( SERVER_CountPlayers( true ) >= ulNumSlots )
		return ( -1 );

	// Look for a free player slot.
	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if (( g_aClients[ulIdx].State == CLS_FREE ) && ( playeringame[ulIdx] == false ))
			return ( ulIdx );
	}

	// Didn't find an available slot.
	return ( -1 );
}

//*****************************************************************************
//
LONG SERVER_FindClientByAddress( NETADDRESS_s Address )
{
	for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
	   if ( g_aClients[ulIdx].State == CLS_FREE )
	       continue;

	   // If the client's address matches the given IP, return the client's index.
	   if ( g_aClients[ulIdx].Address.Compare( Address ))
	       return ( ulIdx );
	}
	
	return ( -1 );
}

//*****************************************************************************
//
CLIENT_s *SERVER_GetClient( ULONG ulIdx )
{
	if ( ulIdx >= MAXPLAYERS )
		return ( NULL );

	return ( &g_aClients[ulIdx] );
}

//*****************************************************************************
// [BB] Returns the number of clients that are somehow connected to the server.
// They don't need to be actually in the game yet, to count as connected client.
ULONG SERVER_CalcNumConnectedClients( void )
{
	ULONG	ulNumConnectedClients = 0;

	for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if ( SERVER_GetClient( ulIdx )->State != CLS_FREE )
			ulNumConnectedClients++;
	}

	return ( ulNumConnectedClients );
}

//*****************************************************************************
//
ULONG SERVER_CountPlayers( bool bCountBots )
{
	ULONG	ulNumPlayers = 0;

	for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if ( playeringame[ulIdx] && ( !players[ulIdx].bIsBot || ( players[ulIdx].bIsBot && bCountBots )))
			ulNumPlayers++;
	}

	return ( ulNumPlayers );
}

//*****************************************************************************
//
ULONG SERVER_CalcNumNonSpectatingPlayers( ULONG ulExcludePlayer )
{
	ULONG	ulIdx;
	ULONG	ulNumPlayers = 0;

	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if (( playeringame[ulIdx] == false ) ||
			( PLAYER_IsTrueSpectator( &players[ulIdx] )) ||
			( ulIdx == ulExcludePlayer ))
		{
			continue;
		}

		ulNumPlayers++;
	}

	return ( ulNumPlayers );
}

//*****************************************************************************
//
void SERVER_CheckTimeouts( void )
{
	ULONG	ulIdx;

	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if ( SERVER_IsValidClient( ulIdx ) == false )
		{
			if ( !sv_noplayertimeout && ( g_aClients[ulIdx].State != CLS_FREE )
			     && ( ( gametic - g_aClients[ulIdx].ulLastCommandTic ) >= ( CLIENT_TIMEOUT * TICRATE ) ) )
			{
				Printf( "Unfinished connection from %s timed out.\n", g_aClients[ulIdx].Address.ToString() );
				SERVER_DisconnectClient( ulIdx, false, false, LEAVEREASON_TIMEOUT );
			}
			continue;
		}

		const int lastCommandTicDiff = gametic - g_aClients[ulIdx].ulLastCommandTic;

		// If we haven't gotten a packet from this client in CLIENT_TIMEOUT seconds,
		// disconnect him.
		if ( !sv_noplayertimeout && lastCommandTicDiff >= CLIENT_TIMEOUT * TICRATE )
		{
		    SERVER_DisconnectClient( ulIdx, true, true, LEAVEREASON_TIMEOUT );
			continue;
		}

		// Also check to see if the client is lagging.
		// [AK] Players don't send updates as often during intermissions or when
		// they're spectating, so give them more time before marking them as lagging.
		if ( lastCommandTicDiff >= (( gamestate == GS_INTERMISSION || players[ulIdx].bSpectating ) ? TICRATE * 5 : TICRATE ))
		{
			// Have not heard from them in a while; mark them as lagging and tell clients.
			if (( players[ulIdx].statuses & PLAYERSTATUS_LAGGING ) == false )
				PLAYER_SetStatus( &players[ulIdx], PLAYERSTATUS_LAGGING, true );
		}
		else
		{
			// Player is no longer lagging. Tell clients.
			if ( players[ulIdx].statuses & PLAYERSTATUS_LAGGING )
				PLAYER_SetStatus( &players[ulIdx], PLAYERSTATUS_LAGGING, false );
		}
	}
}

//*****************************************************************************
//

#ifdef	_DEBUG
CVAR( Bool, sv_emulatepacketloss, false, 0 )
#endif

void SERVER_GetPackets( void )
{
	BYTESTREAM_s	*pByteStream;

	while ( NETWORK_GetPackets( ) > 0 )
	{
		// Set up our byte stream.
		pByteStream = &NETWORK_GetNetworkMessageBuffer( )->ByteStream;
		pByteStream->pbStream = NETWORK_GetNetworkMessageBuffer( )->pbData;
		pByteStream->pbStreamEnd = pByteStream->pbStream + NETWORK_GetNetworkMessageBuffer( )->ulCurrentSize;

		// [RC]
#ifdef CREATE_PACKET_LOG
		pByteStream->pbStreamBeginning = pByteStream->pbStream;
		pByteStream->bPacketAlreadyLogged = false;

		// We've already had trouble from this IP, so log all of his traffic.
		if ( g_HackerIPList.isIPInList( NETWORK_GetFromAddress( ) ) )
		{
			FString outString;
			outString.Format("Alleged hacker (first offense: %s)", g_HackerIPList.getEntryComment(  NETWORK_GetFromAddress( ) ));
			server_LogPacket( pByteStream, NETWORK_GetFromAddress( ), outString.GetChars() );
		}

#endif

		// We've gotten a packet. Try to figure out if it's from a connected client.
		g_lCurrentClient = SERVER_FindClientByAddress( NETWORK_GetFromAddress( ));

		// Packet is not from an existing client; must be someone trying to connect!
		if ( g_lCurrentClient == -1 )
		{
			// [BB] Or it's from the auth server.
			if ( NETWORK_GetFromAddress().Compare( NETWORK_AUTH_GetCachedServerAddress() ))
			{
				SERVER_AUTH_ParsePacket( pByteStream );
				continue;
			}

			SERVER_DetermineConnectionType( pByteStream );
			continue;
		}

#ifdef	_DEBUG
		// Emulate packet loss for debugging.
		if ( sv_emulatepacketloss )
		{
			if (( M_Random( ) < 170 ) && gamestate == GS_LEVEL )
				break;
		}
#endif

		// Parse the information sent by the clients.
		SERVER_ParsePacket( pByteStream );

		// Invalidate this.
		g_lCurrentClient = -1;
	}
}

//*****************************************************************************
//
void SERVER_SendChatMessage( ULONG ulPlayer, ULONG ulMode, const char *pszString, ULONG ulReceiver )
{
	// [BB] Ignore any chat messages with invalid chat mode. This is crucial because
	// the rest of the code assumes that only valid chat modes are used.
	if ( ( ulMode == CHATMODE_NONE ) || ( ulMode >= NUM_CHATMODES ) )
		return;

	// [AK] Verify if the player is sending a team chat message, and is allowed to.
	if ( ( ulMode == CHATMODE_TEAM ) && ( CHAT_CanUseTeamChat( ulPlayer, true ) == false ) )
		return;

	// Potentially prevent spectators from talking to active players during LMS games.
	const bool bForbidChatToPlayers = GAMEMODE_IsClientForbiddenToChatToPlayers( ulPlayer, false );
	FString cleanedChatString = pszString;

	// [BB] If the chat string is empty now, it only contained crap and is ignored.
	if ( CHAT_CleanChatString( cleanedChatString ) == false )
		return;

	// [BB] Replace the pointer to the chat string, with the cleaned version.
	// This way the code below doesn't need to be altered.
	pszString = cleanedChatString.GetChars();

	// [AK] Check if we're sending a private message.
	if ( ulMode == CHATMODE_PRIVATE_SEND )
	{
		SERVERCOMMANDS_PrivateSay( ulPlayer, ulReceiver, pszString, bForbidChatToPlayers );
	}
	else
	{
		CHAT_AddChatMessage( ulPlayer, pszString );

		// [AK] Trigger an event script indicating that a chat message was received.
		// If the event returns 0, then don't print the message or send it to the clients.
		if ( GAMEMODE_HandleEvent( GAMEEVENT_CHAT, NULL, ulPlayer != MAXPLAYERS ? ulPlayer : -1, ulMode - CHATMODE_GLOBAL, true ) == 0 )
			return;

		SERVERCOMMANDS_PlayerSay( ulPlayer, pszString, ulMode, bForbidChatToPlayers );
	}

	if ( ulMode == CHATMODE_PRIVATE_SEND )
	{
		// [AK] Don't log private messages that aren't sent to/from the server.
		if (( ulPlayer != MAXPLAYERS ) && ( ulReceiver != MAXPLAYERS ))
			return;

		// [AK] Don't log private messages sent by spectators when the chat is restricted.
		if (( ulReceiver == MAXPLAYERS ) && ( bForbidChatToPlayers ))
			return;
	}

	FString message;

	// [BB] This is to make the lines readily identifiable, necessary
	// for MiX-MaN's IRC server control tool for example.
	if ( sv_markchatlines )
		message = "CHAT ";

	// [AK] Distinguish team chat messages from normal chat messages if we want to.
	if (( sv_distinguishteamchatlines > 0 ) && ( ulMode == CHATMODE_TEAM ))
	{
		if ( PLAYER_IsTrueSpectator( &players[ulPlayer] ))
		{
			message += "<SPEC> ";
		}
		else if ( players[ulPlayer].bOnTeam )
		{
			// [AK] If sv_distinguishteamchatlines is set to 1, then show the team's name.
			// If it's greater than 1, then show the team's number instead.
			if ( sv_distinguishteamchatlines == 1 )
			{
				FString teamName = TEAM_GetName( players[ulPlayer].Team );
				teamName.ToUpper( );

				message.AppendFormat( "<%s> ", teamName.GetChars( ));
			}
			else
			{
				message.AppendFormat( "<TEAM #%d> ", players[ulPlayer].Team + 1 );
			}
		}
	}

	// Print this message in the server's local window.
	if ( strnicmp( "/me", pszString, 3 ) == 0 )
	{
		pszString += 3;

		if ( ulMode == CHATMODE_PRIVATE_SEND )
		{
			if ( ulPlayer == MAXPLAYERS )
				message.AppendFormat( "<To %s> ", players[ulReceiver].userinfo.GetName() );
			else
				message.AppendFormat( "<From %s> ", players[ulPlayer].userinfo.GetName() );
		}

		// [AK] Don't print the same message twice for the current RCON client.
		CONSOLE_ShouldPrintToRCONPlayer( false );
		message.AppendFormat( "* %s%s", ulPlayer != MAXPLAYERS ? players[ulPlayer].userinfo.GetName() : "<Server>", pszString );
	}
	else
	{
		if ( ulMode == CHATMODE_PRIVATE_SEND )
		{
			if ( ulPlayer == MAXPLAYERS )
				message.AppendFormat( "<To %s>: %s", players[ulReceiver].userinfo.GetName(), pszString );
			else
				message.AppendFormat( "<From %s>: %s", players[ulPlayer].userinfo.GetName(), pszString );
		}
		else
		{
			// [AK] Don't print the same message twice for the current RCON client.
			CONSOLE_ShouldPrintToRCONPlayer( false );
			message.AppendFormat( "%s: %s", ulPlayer != MAXPLAYERS ? players[ulPlayer].userinfo.GetName() : "<Server>", pszString );
		}
	}

	Printf( "%s\n", message.GetChars() );
}

//*****************************************************************************
//
void SERVER_RequestClientToAuthenticate( ULONG ulClient )
{
	g_aClients[ulClient].PacketBuffer.Clear();
	g_aClients[ulClient].PacketBuffer.ByteStream.WriteByte( SVCC_AUTHENTICATE );
	g_aClients[ulClient].PacketBuffer.ByteStream.WriteString( level.mapname );
	// [CK] This lets the client start off with a reasonable gametic. In case
	// the client would like to do any kind of prediction from gametics in the
	// future, we can use the current gametic as the base. This also prevents
	// the client from relying on a gametic of 0 or some unset number.
	g_aClients[ulClient].PacketBuffer.ByteStream.WriteLong( gametic );

	// Send the packet off.
	SERVER_SendClientPacket( ulClient, true );
}

//*****************************************************************************
//
void SERVER_AuthenticateClientLevel( BYTESTREAM_s *pByteStream )
{
	if ( SERVER_PerformAuthenticationChecksum( pByteStream ) == false )
	{
		SERVER_ClientError( g_lCurrentClient, NETWORK_ERRORCODE_AUTHENTICATIONFAILED );
		return;
	}

	// [BB] Don't timeout.
	g_aClients[g_lCurrentClient].ulLastCommandTic = gametic;

	// The client has now had his level authenticated.
	// [BB] Don't set the state for clients already spawned. They already have a body
	// and need to be trated differently.
	if ( g_aClients[g_lCurrentClient].State < CLS_SPAWNED_BUT_NEEDS_AUTHENTICATION )
		g_aClients[g_lCurrentClient].State = CLS_AUTHENTICATED;

	// Tell the client his level was authenticated.
	g_aClients[g_lCurrentClient].PacketBuffer.Clear();
	g_aClients[g_lCurrentClient].PacketBuffer.ByteStream.WriteByte( SVCC_MAPLOAD );
	// [BB] Also tell him the game mode, otherwise the client can't decide whether 3D floors should be spawned or not.
	g_aClients[g_lCurrentClient].PacketBuffer.ByteStream.WriteByte( GAMEMODE_GetCurrentMode( ) );
	
	// Send the packet off.
	SERVER_SendClientPacket( g_lCurrentClient, true );
}

//*****************************************************************************
//
bool SERVER_PerformAuthenticationChecksum( BYTESTREAM_s *pByteStream )
{
	// [BB] Since we are already using the map, we won't get a NULL pointer.
	MapData *map = P_OpenMapData( level.mapname, false );
	assert( map );

	// Compute the checksum for the map on our end.
	BYTE serverChecksum[16];
	map->GetChecksum( serverChecksum );
	delete map;

	// Read in the client's checksum.
	BYTE clientChecksum[sizeof serverChecksum];
	pByteStream->ReadBuffer( clientChecksum, sizeof clientChecksum );

	// Compare the checksums.
	return memcmp( serverChecksum, clientChecksum, sizeof serverChecksum ) == 0;
}

//*****************************************************************************
//
void SERVER_ConnectNewPlayer( BYTESTREAM_s *pByteStream )
{
	LONG								lCommand;
	ULONG								ulIdx;
	ULONG								ulState;
	ULONG								ulCountdownTicks;
	AInventory							*pInventory;

	// If the client hasn't authenticated his level, don't accept this connection.
	if ( g_aClients[g_lCurrentClient].State < CLS_AUTHENTICATED )
	{
		if ( g_aClients[g_lCurrentClient].State == CLS_AUTHENTICATED_BUT_OUTDATED_MAP )
			SERVER_RequestClientToAuthenticate( g_lCurrentClient );
		else
			SERVER_ClientError( g_lCurrentClient, NETWORK_ERRORCODE_AUTHENTICATIONFAILED );
		return;
	}

	// [BB] A client who is already spawned but not authenticated shouldn't ask for a new connection.
	// Just ask the client to authenticate again in this case.
	if ( g_aClients[g_lCurrentClient].State == CLS_SPAWNED_BUT_NEEDS_AUTHENTICATION )
	{
		SERVERCOMMANDS_MapAuthenticate ( level.mapname, g_lCurrentClient, SVCF_ONLYTHISCLIENT );
		return;
	}

	// This player is now in the game.
	playeringame[g_lCurrentClient] = true;

	// [BB] If necessary, spawn a voodoo doll for the player.
	if ( COOP_PlayersVoodooDollsNeedToBeSpawned ( g_lCurrentClient ) )
	{
		// [BB] players[g_lCurrentClient].mo already should be NULL, I'm not sure if the code works if it's not NULL.
		APlayerPawn* pOldPawn = players[g_lCurrentClient].mo;
		players[g_lCurrentClient].mo = NULL;
		COOP_SpawnVoodooDollsForPlayerIfNecessary ( g_lCurrentClient );
		// [BB] It is important that mo doesn't point to the voodoo doll, otherwise G_CooperativeSpawnPlayer called later
		// removes the MF_SOLID flag from the doll because it calls G_CheckSpot.
		players[g_lCurrentClient].mo = pOldPawn;
	}

	// Check and see if this player should spawn as a spectator.
	// [BB] We may only do this if the player has not been spawned already. Otherwise the client could get into a kind
	// of ghost mode if it connects with "cl_startasspectator 0", turns to a spectator after joining and then sends
	// CLCC_REQUESTSNAPSHOT (e.g. by packet injection) again while still in the game.
	if ( g_aClients[g_lCurrentClient].State != CLS_SPAWNED )
	{
		// [BB] Since PLAYER_ShouldSpawnAsSpectator calls GAMEMODE_PreventPlayersFromJoining which then calls DUEL_CountActiveDuelers,
		// we need to initialize bSpectating with true, otherwise this player could prevent itself from joining.
		players[g_lCurrentClient].bSpectating = true;
		players[g_lCurrentClient].bSpectating = (( PLAYER_ShouldSpawnAsSpectator( &players[g_lCurrentClient] )) || ( g_aClients[g_lCurrentClient].bWantStartAsSpectator ));
	}

	// Don't restart the map! There's people here!
	g_lMapRestartTimer = 0;

	g_aClients[g_lCurrentClient].ulLastGameTic = gametic;
	g_aClients[g_lCurrentClient].ulLastCommandTic = gametic;

	// Clear out the client's netbuffer.
	g_aClients[g_lCurrentClient].PacketBuffer.Clear();

	// Tell the client that we're about to send him a snapshot of the level.
	SERVERCOMMANDS_BeginSnapshot( g_lCurrentClient );

	// Send welcome message.
	SERVER_PrintfPlayer( g_lCurrentClient, "Version %s Server\n", DOTVERSIONSTR );

	// Send consoleplayer number.
	SERVERCOMMANDS_SetConsolePlayer( g_lCurrentClient );

	// [AK] Send the name of the server.
	SERVERCOMMANDS_SetCVar( sv_hostname, g_lCurrentClient, SVCF_ONLYTHISCLIENT );

	// [AK] Send the current state of the skip correction.
	// SERVERCOMMANDS_SetCVar( sv_smoothplayers, g_lCurrentClient, SVCF_ONLYTHISCLIENT );

	// Send dmflags.
	SERVERCOMMANDS_SetGameDMFlags( g_lCurrentClient, SVCF_ONLYTHISCLIENT );

	// Send skill level.
	SERVERCOMMANDS_SetGameSkill( g_lCurrentClient, SVCF_ONLYTHISCLIENT );

	// Send special settings like teamplay and deathmatch.
	SERVERCOMMANDS_SetGameMode( g_lCurrentClient, SVCF_ONLYTHISCLIENT );

	// Send timelimit, fraglimit, etc.
	SERVERCOMMANDS_SetGameModeLimits( g_lCurrentClient, SVCF_ONLYTHISCLIENT );

	// If this is LMS, send the allowed weapons.
	if ( lastmanstanding || teamlms )
		SERVERCOMMANDS_SetLMSAllowedWeapons( g_lCurrentClient, SVCF_ONLYTHISCLIENT );

	// [BB] Due to ZADF_ALWAYS_APPLY_LMS_SPECTATORSETTINGS, this is necessary in all game modes.
	SERVERCOMMANDS_SetLMSSpectatorSettings( g_lCurrentClient, SVCF_ONLYTHISCLIENT );

	// If this is CTF or ST, tell the client whether or not we're in simple mode.
	if ( GAMEMODE_GetCurrentFlags() & GMF_USETEAMITEM )
		SERVERCOMMANDS_SetSimpleCTFSTMode( g_lCurrentClient, SVCF_ONLYTHISCLIENT );
/*
	// Send the map name, and have the client load it.
	SERVERCOMMANDS_MapLoad( g_lCurrentClient, SVCF_ONLYTHISCLIENT );
*/
	// Send the map music.
	SERVERCOMMANDS_SetMapMusic( SERVER_GetMapMusic( ), SERVER_GetMapMusicOrder( ), g_lCurrentClient, SVCF_ONLYTHISCLIENT );

	// Send the message of the day.
	FString motd = *sv_motd;

	// [BB] This server doesn't enforce the special lump authentication. Inform the client about it!
	if ( !sv_pure )
		motd += "\n\nThis server doesn't enforce the special lump authentication.\nEnter at your own risk.\n";

	if ( motd.IsNotEmpty() )
		SERVERCOMMANDS_PrintMOTD( motd.GetChars(), g_lCurrentClient, SVCF_ONLYTHISCLIENT );

	// [BB] Client is using the "join full server from localhost" hack. Inform him about it!
	if ( ( SERVER_CountPlayers( true ) > static_cast<unsigned> (sv_maxclients) ) )
		SERVERCOMMANDS_PrintMOTD( "Emergency!\n\nYou are joining from localhost even though the server is full.\nDo whatever is necessary to clean the situation and disconnect afterwards.\n", g_lCurrentClient, SVCF_ONLYTHISCLIENT );

	// [RK] Since clients don't end votes when traversing hubs
	// the vote will be cleared here after a MAP command has executed.
	if ( CALLVOTE_GetVoteState() == false && level.clusterflags & CLUSTER_HUB )
		SERVERCOMMANDS_ClearVote( g_lCurrentClient, SVCF_ONLYTHISCLIENT );

	// If we're in a duel or LMS mode, tell him the state of the game mode.
	if ( duel || lastmanstanding || teamlms || possession || teampossession || survival || invasion )
	{
		if ( duel )
		{
			ulState = DUEL_GetState( );
			ulCountdownTicks = DUEL_GetCountdownTicks( );
		}
		else if ( survival )
		{
			ulState = SURVIVAL_GetState( );
			ulCountdownTicks = SURVIVAL_GetCountdownTicks( );
		}
		else if ( invasion )
		{
			ulState = INVASION_GetState( );
			ulCountdownTicks = INVASION_GetCountdownTicks( );
		}
		else if ( possession || teampossession )
		{
			ulState = POSSESSION_GetState( );
			if ( ulState == (PSNSTATE_e)PSNS_ARTIFACTHELD )
				ulCountdownTicks = POSSESSION_GetArtifactHoldTicks( );
			else
				ulCountdownTicks = POSSESSION_GetCountdownTicks( );
		}
		else
		{
			ulState = LASTMANSTANDING_GetState( );
			ulCountdownTicks = LASTMANSTANDING_GetCountdownTicks( );
		}

		SERVERCOMMANDS_SetGameModeState( ulState, ulCountdownTicks, g_lCurrentClient, SVCF_ONLYTHISCLIENT );

		// Also, if we're in invasion mode, tell the client what wave we're on.
		if ( invasion )
			SERVERCOMMANDS_SetInvasionWave( g_lCurrentClient, SVCF_ONLYTHISCLIENT );
	}

	// In a game mode that involves teams, potentially decide a team for him.
	if ( GAMEMODE_GetCurrentFlags() & GMF_PLAYERSONTEAMS )
	{
		// The server will decide his team!
		// [BB] But don't put spectators on a team!
		if ( ( dmflags2 & DF2_NO_TEAM_SELECT ) && ( players[g_lCurrentClient].bSpectating == false ) )
		{
			players[g_lCurrentClient].bOnTeam = true;
			players[g_lCurrentClient].Team = TEAM_ChooseBestTeamForPlayer( );

		}
		// [BB] Non-spectators in team games are always on a team. Under normal circumstances it shouldn't
		// be possible for a non-sectator to trigger this. It will happen though if a non-spectator sends
		// CLCC_REQUESTSNAPSHOT (e.g. by packet injection) again while still in the game.
		else if ( players[g_lCurrentClient].bSpectating )
			players[g_lCurrentClient].bOnTeam = false;
	}

	lCommand = pByteStream->ReadByte();

	// Read in the user's userinfo. If it returns false, the player was kicked for flooding
	// (though this shouldn't happen anymore).
	players[g_lCurrentClient].userinfo.Reset();
	if ( SERVER_GetUserInfo( pByteStream, false, true ) == false )
		return;

	// Apparently, we know the client is in the game, but the 
	// client dosent know this. So instead of calling multiple 
	// P_SpawnPlayers and messing ourselves up, we just re-send
	// the spawn data. Make sense?
	if (( g_aClients[g_lCurrentClient].State == CLS_SPAWNED ) && ( players[g_lCurrentClient].mo != NULL ))
		SERVERCOMMANDS_SpawnPlayer( g_lCurrentClient, PST_REBORNNOINVENTORY, g_lCurrentClient, SVCF_ONLYTHISCLIENT );
	else
	{
		players[g_lCurrentClient].playerstate = PST_ENTER;

		// [BB] Spawn the player at an appropriate start.
		GAMEMODE_SpawnPlayer ( g_lCurrentClient );
	}

	// Tell the client of any lines that have been altered since the level start.
	SERVER_UpdateLines( g_lCurrentClient );

	// Tell the client of any sides that have been altered since the level start.
	SERVER_UpdateSides( g_lCurrentClient );

	// Tell the client of any sectors that have been altered since the level start.
	SERVER_UpdateSectors( g_lCurrentClient );

	// [BB] Tell the client of things derived from DMover and similar classes.
	SERVER_UpdateMovers( g_lCurrentClient );

	// [TP] Tell the client his account name.
	SERVERCOMMANDS_SetPlayerAccountName( g_lCurrentClient, g_lCurrentClient, SVCF_ONLYTHISCLIENT );

	// [AK] Inform the client of the map rotation list and the next entry.
	SERVERCOMMANDS_SyncMapRotation(g_lCurrentClient, SVCF_ONLYTHISCLIENT );
	SERVERCOMMANDS_SetNextMapPosition( g_lCurrentClient, SVCF_ONLYTHISCLIENT );

	// Send a snapshot of the level.
	SERVER_SendFullUpdate( g_lCurrentClient );

	// If we need to start this client's enter scripts, do that now.
	if ( g_aClients[g_lCurrentClient].bRunEnterScripts )
	{
		FBehavior::StaticStartTypedScripts( SCRIPT_Enter, players[g_lCurrentClient].mo, true );
		g_aClients[g_lCurrentClient].bRunEnterScripts = false;
	}

	if ( GAMEMODE_GetCurrentFlags() & GMF_USETEAMITEM )
	{
		// In ST/CTF games, let the incoming player know who has flags/skulls.
		for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
		{
			if ( SERVER_IsValidClient( ulIdx ) == false )
				continue;

			// Player shouldn't have a flag/skull if he's not on a team...
			if (( players[ulIdx].bOnTeam == false ) || ( players[ulIdx].mo == NULL ))
				continue;

			// See if this player is carrying the opponents flag/skull.
			pInventory = TEAM_FindOpposingTeamsItemInPlayersInventory ( &players[ulIdx] );
			if ( pInventory )
				SERVERCOMMANDS_GiveInventory( ulIdx, pInventory, g_lCurrentClient, SVCF_ONLYTHISCLIENT );

			// See if the player is carrying the white flag in OFCTF.
			pInventory = players[ulIdx].mo->FindInventory( PClass::FindClass( "WhiteFlag" ), true );
			if (( oneflagctf ) && ( pInventory ))
				SERVERCOMMANDS_GiveInventory( ulIdx, pInventory, g_lCurrentClient, SVCF_ONLYTHISCLIENT );
		}

		// Also let the client know if flags/skulls are on the ground.
		for ( ulIdx = 0; ulIdx < teams.Size( ); ulIdx++ )
			SERVERCOMMANDS_SetTeamReturnTicks( ulIdx, TEAM_GetReturnTicks( ulIdx ), g_lCurrentClient, SVCF_ONLYTHISCLIENT );

		SERVERCOMMANDS_SetTeamReturnTicks( teams.Size( ), TEAM_GetReturnTicks( teams.Size( ) ), g_lCurrentClient, SVCF_ONLYTHISCLIENT );
	}

	// If we're playing terminator, potentially tell the client who's holding the terminator
	// artifact.
	if ( terminator )
	{
		for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
		{
			if (( playeringame[ulIdx] == false ) || ( players[ulIdx].mo == NULL ))
				continue;

			pInventory = players[ulIdx].mo->FindInventory( PClass::FindClass( "PowerTerminatorArtifact" ));
			if ( pInventory )
				SERVERCOMMANDS_GiveInventory( ulIdx, pInventory, g_lCurrentClient, SVCF_ONLYTHISCLIENT );
		}
	}

	// If we're playing possession/team possession, potentially tell the client who's holding
	// the possession artifact.
	if ( possession || teampossession )
	{
		for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
		{
			if (( playeringame[ulIdx] == false ) || ( players[ulIdx].mo == NULL ))
				continue;

			pInventory = players[ulIdx].mo->FindInventory( PClass::FindClass( "PowerPossessionArtifact" ));
			if ( pInventory )
				SERVERCOMMANDS_GiveInventory( ulIdx, pInventory, g_lCurrentClient, SVCF_ONLYTHISCLIENT );
		}
	}

	// Check and see if this is a disconnected player. If so, restore his fragcount.
	SavedPlayerInfo *savedInfo = SERVER_SAVE_GetSavedInfo( players[g_lCurrentClient].userinfo.GetName( ), g_aClients[g_lCurrentClient].Address );

	if (( savedInfo != nullptr ) && ( g_aClients[g_lCurrentClient].bWantNoRestoreFrags == false ))
	{
		PLAYER_SetFragcount( &players[g_lCurrentClient], savedInfo->fragCount, false, false );
		PLAYER_SetWins( &players[g_lCurrentClient], savedInfo->winCount );
		players[g_lCurrentClient].lPointCount = savedInfo->pointCount;

		if ( teamgame )
			SERVERCOMMANDS_SetPlayerPoints( g_lCurrentClient );

		// [AK] Also restore their death count.
		PLAYER_SetDeaths( &players[g_lCurrentClient], savedInfo->deathCount );

		// [RC] Also restore his playing time. This should agree with 'restore frags' as a whole, clean slate option.
		players[g_lCurrentClient].ulTime = savedInfo->time;
	}

	// If this player is on a team, tell all the other clients that a team has been selected
	// for him.
	if ( players[g_lCurrentClient].bOnTeam )
		SERVERCOMMANDS_SetPlayerTeam( g_lCurrentClient );

	if ( g_aClients[g_lCurrentClient].State != CLS_SPAWNED )
	{
		// [K6/BB] Show the player's country on connect, if the GeoIP db is available.
		FString countryInfo;

		// [AK] Always save the player's country index, even if they want to hide it.
		players[g_lCurrentClient].ulCountryIndex = NETWORK_GetCountryIndexFromAddress( SERVER_GetClient( g_lCurrentClient )->Address );

		// [BB] All players see the connect message, so only show the country code if the player doesn't want it to be hidden.
		// [AK] Also don't show it if their country index is "N/A".
		if (( SERVER_GetClient( g_lCurrentClient )->bWantHideCountry == false ) && ( players[g_lCurrentClient].ulCountryIndex > 0 ))
		{
			countryInfo.AppendFormat( " (from: %s)", NETWORK_GetCountryCodeFromIndex( players[g_lCurrentClient].ulCountryIndex, false ));

			// [AK] Send this player's country index to everyone.
			SERVERCOMMANDS_SetPlayerCountry( g_lCurrentClient );
		}

		FString message;
		message.Format( "%s{ip} %s.%s\n", players[g_lCurrentClient].userinfo.GetName(),
			players[g_lCurrentClient].bSpectating ? "has connected" : "entered the game",
			countryInfo.GetChars() );
		server_PrintWithIP( message, g_aClients[g_lCurrentClient].Address );
	}

	BOTCMD_SetLastJoinedPlayer( players[g_lCurrentClient].userinfo.GetName() );

	// Tell the bots that a new players has joined the game!
	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if ( playeringame[ulIdx] == false )
			continue;

		if ( players[ulIdx].pSkullBot )
			players[ulIdx].pSkullBot->PostEvent( BOTEVENT_PLAYER_JOINEDGAME );
	}

	if ( g_aClients[g_lCurrentClient].State != CLS_SPAWNED )
		SERVERCONSOLE_ReListPlayers( );

	// Update this client's state. He's in the game now!
	g_aClients[g_lCurrentClient].State = CLS_SPAWNED;

	// [RC] Update clients using the RCON utility.
	SERVER_RCON_UpdateInfo( SVRCU_PLAYERDATA );

	// Tell the client that the snapshot is done.
	SERVERCOMMANDS_EndSnapshot( g_lCurrentClient );

	// [RC] Clients may wish to ignore this new player.
	// [AK] This may also update the new player's VoIP channel volume for clients.
	SERVERCOMMANDS_PotentiallySendPlayerCommRule( g_lCurrentClient );

	// [AK] Trigger an event script indicating that the client has connected to the server.
	// Also indicate if they had previously connected to the server.
	GAMEMODE_HandleEvent( GAMEEVENT_PLAYERCONNECT, NULL, g_lCurrentClient, !!savedInfo );
}

//*****************************************************************************
//
void SERVER_DetermineConnectionType( BYTESTREAM_s *pByteStream )
{
	ULONG	ulFlags;
	ULONG	ulTime;
	LONG	lCommand;
	ULONG   ulFlags2 = 0; // [SB] extended flags
	bool	bSendSegmentedResponse = false; // [SB]

	// If either this IP is in our flood protection queue, or the queue is full (DOS), ignore the request.
	if ( g_floodProtectionIPQueue.isFull( ) || g_floodProtectionIPQueue.addressInQueue( NETWORK_GetFromAddress( )))
		return;

	lCommand = pByteStream->ReadByte();

	// [BB] It's absolutely crucial that we only handle the first command in a packet
	// that comes from someone that's not a client yet. Otherwise a single malformed
	// packet can be used to bombard the server with connection requests, freezing the server for about 5 seconds.

	// End of message.
	if ( lCommand == -1 )
		return;

	if ( lCommand == MSC_IPISBANNED
		&& NETWORK_GetFromAddress().Compare( SERVER_MASTER_GetMasterAddress() )
		&& SERVER_STATISTIC_GetTotalSecondsElapsed( ) < 20 )
	{
		Printf( "\n*** ERROR: You are banned from hosting on the master server!\n" );
		return;
	}

	// If it's not a launcher querying the server, it must be a client.
	if ( lCommand != CLCC_ATTEMPTCONNECTION )
	{
		// If the client sends a game command now, he's probably thinking he's still in a game, but isn't. Ignore those.
		if (( lCommand >= CLC_USERINFO ) && ( lCommand < NUM_CLIENT_COMMANDS ))
			return;

		switch ( lCommand )
		{		
		// [RC] An RCON utility is trying to connect to/control this server.
		case CLRC_BEGINCONNECTION:
		case CLRC_PASSWORD:
		case CLRC_COMMAND:
		case CLRC_PONG:
		case CLRC_DISCONNECT:
		case CLRC_TABCOMPLETE:

			SERVER_RCON_ParseMessage( NETWORK_GetFromAddress( ), lCommand, pByteStream );
			return;
		// Launcher is querying this server.
		case LAUNCHER_SERVER_CHALLENGE:

			// Read in three more bytes, because it was a long that was sent to us.
			pByteStream->ReadByte();
			pByteStream->ReadByte();
			pByteStream->ReadByte();

			// Read in what the query wants to know.
			ulFlags = pByteStream->ReadLong();

			// Read in the time the launcher sent us.
			ulTime = pByteStream->ReadLong();

			// [SB] read extended flags
			if ( ulFlags & SQF_EXTENDED_INFO )
				ulFlags2 = pByteStream->ReadLong();

			// [SB] Handle a special '2' byte as a request for a segmented response.
			if ( pByteStream->pbStream + 1 <= pByteStream->pbStreamEnd )
			{
				bSendSegmentedResponse = pByteStream->ReadByte() == 2;
			}

			// Received launcher query!
			if ( sv_showlauncherqueries )
				Printf( "Launcher challenge from: %s\n", NETWORK_GetFromAddress().ToString() );

			SERVER_MASTER_SendServerInfo( NETWORK_GetFromAddress( ), ulFlags, ulTime, ulFlags2, false, bSendSegmentedResponse );
			return;
		// [RC] Master server is sending us the holy banlist.
		case MASTER_SERVER_BANLIST:
		case MASTER_SERVER_VERIFICATION:
		case MASTER_SERVER_BANLISTPART:

			if ( NETWORK_GetFromAddress().Compare( SERVER_MASTER_GetMasterAddress() ))
			{
				FString MasterBanlistVerificationString = pByteStream->ReadString();
				if ( SERVER_GetMasterBanlistVerificationString().Compare ( MasterBanlistVerificationString ) == 0 )
				{
					if ( lCommand == MASTER_SERVER_BANLIST )
						SERVERBAN_ReadMasterServerBans( pByteStream );
					else if ( lCommand == MASTER_SERVER_BANLISTPART )
						SERVERBAN_ReadMasterServerBanlistPart( pByteStream );
					else
						SERVER_MASTER_HandleVerificationRequest( pByteStream );
				}
				else
					Printf ( "Master server message with wrong verification string received. Ignoring\n" );
			}
			return;
		default:

			// [SB]
			if ( sv_printconnectionmessages )
				Printf( "Unknown challenge (%d) from %s. Ignoring IP for 10 seconds.\n", static_cast<int> (lCommand), NETWORK_GetFromAddress().ToString() );
			// [BB] Block all further challenges of this IP for ten seconds to prevent log flooding.
			SERVER_IgnoreIP( NETWORK_GetFromAddress( ));

#ifdef CREATE_PACKET_LOG
			server_LogPacket(pByteStream,  NETWORK_GetFromAddress( ), "Unknown connection challenge.");
#endif
			return;
		}
	}

	// Don't handle connection attempts from clients if we're in intermission.
	if ( gamestate != GS_LEVEL )
		return;

	// Setup a new player (setup CLIENT_t and player_t)
	SERVER_SetupNewConnection( pByteStream, true );
}

//*****************************************************************************
//
void SERVER_SetupNewConnection( BYTESTREAM_s *pByteStream, bool bNewPlayer )
{
	LONG			lClient;
	FString			clientVersion;
	FString			clientPassword;
	char			szServerPassword[MAX_NETWORK_STRING];
	int				clientNetworkGameVersion;
	IPStringArray	szAddress;
	ULONG			ulIdx;
	NETADDRESS_s	AddressFrom;
	bool			bAdminClientConnecting;

	// Grab the IP address of the packet we've just received.
	AddressFrom = NETWORK_GetFromAddress( );

	// [RC] Prevent the fakeplayers exploit. 
	{
		// [RC] Count how many other clients are using this IP.
		ULONG ulOtherClientsFromIP = 0;
		for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
		{
			if ( AddressFrom.CompareNoPort( g_aClients[ulIdx].Address ))
				ulOtherClientsFromIP++;
		}

		// [RC] Kick if necessary.
		if ( sv_maxclientsperip > 0 && (static_cast<LONG>(ulOtherClientsFromIP) >= sv_maxclientsperip) )
		{
			// Printf( "Connection from %s refused: too many connections from that IP. (sv_maxclientsperip is %d.)", AddressFrom.ToString(), *sv_maxclientsperip );
			SERVER_ConnectionError( AddressFrom, "Too many connections from this IP.", NETWORK_ERRORCODE_TOOMANYCONNECTIONSFROMIP );
			return;
		}
	}

	if ( bNewPlayer )
	{
		// [BB]: In case of emergency you should be able to join your own server,
		// even if it is full. Here we check, if the connection request comes from
		// localhost, i.e. 127.0.0.1. If it does, we allow a connect even in case of
		// SERVER_CountPlayers( true ) >= sv_maxclients as long as SERVER_FindFreeClientSlot( )
		// finds a free slot.
		bAdminClientConnecting = false;
		if ( g_AdminIPList.isIPInList ( AddressFrom ) )
			bAdminClientConnecting = true;

		// Try to find a player slot for the connecting client.
		lClient = SERVER_FindFreeClientSlot( );

		// If the server is full, send him a packet saying that it is.
		if (( lClient == -1 ) || ( SERVER_CountPlayers( true ) >= static_cast<unsigned> (sv_maxclients) && !bAdminClientConnecting ))
		{
			// Tell the client a packet saying the server is full.
			SERVER_ConnectionError( AddressFrom, "Server is full.", NETWORK_ERRORCODE_SERVERISFULL );

			// User sent the version, password, start as spectator, restore frags, and network protcol version along with the challenge.
			pByteStream->ReadString();
			pByteStream->ReadString();
			pByteStream->ReadByte();
			pByteStream->ReadByte();
			pByteStream->ReadByte();
			// [BB] Lump authentication string.
			pByteStream->ReadString();
			return;
		}
	}
	else
		lClient = g_lCurrentClient;

	if ( g_aClients[lClient].State >= CLS_SPAWNED_BUT_NEEDS_AUTHENTICATION )
		SERVER_DisconnectClient( lClient, false, true, LEAVEREASON_RECONNECT );

	// Read in the client version info.
	clientVersion = pByteStream->ReadString();
	// [BB] Hijack the player name cleaning system to get rid of inappropriate chars in the version string.
	// Tampered clients can put in anything here!
	// [AK] V_CleanPlayerName also truncates the string if it's longer than MAXPLAYERNAMEBUFFER, but under
	// normal circumstances, the version string should never be this long anyways.
	V_CleanPlayerName ( clientVersion, false );
	// [BB] Version strings also will never be incredibly long.
	clientVersion = clientVersion.Left ( 32 );

	// Read in the client's password.
	clientPassword = pByteStream->ReadString();
	clientPassword.ToUpper();

	// [BB] Read in the client connection flags.
	const int connectFlags = pByteStream->ReadByte();

	// Read in whether or not the client wants to start as a spectator.
	g_aClients[lClient].bWantStartAsSpectator = !!( connectFlags & CCF_STARTASSPECTATOR );

	// Read in whether or not the client wants to start as a spectator.
	g_aClients[lClient].bWantNoRestoreFrags = !!( connectFlags & CCF_DONTRESTOREFRAGS );

	// [BB] Save whether the clients wants his country to be hidden.
	g_aClients[lClient].bWantHideCountry = !!( connectFlags & CCF_HIDECOUNTRY );

	// [TP] Save whether or not the player wants to hide his account.
	g_aClients[lClient].WantHideAccount = !!pByteStream->ReadByte();

	// Read in the client's network game version.
	clientNetworkGameVersion = pByteStream->ReadByte();

	g_aClients[lClient].SavedPackets.Clear();
	g_aClients[lClient].PacketBuffer.Clear();
	g_aClients[lClient].UnreliablePacketBuffer.Clear();

	// Who is connecting?
	// [SB] Only print if sv_printconnectionmessages is enabled.
	// We still see the player's IP when the "<player> has connected" message is printed once they're in the game.
	if ( sv_printconnectionmessages )
		Printf( "Connect (v%s): %s\n", clientVersion.GetChars(), NETWORK_GetFromAddress().ToString() );

	// Setup the client.
	g_aClients[lClient].State = CLS_CHALLENGE;
	g_aClients[lClient].Address = AddressFrom;

	{
		// Make sure the version matches.
		if ( stricmp( clientVersion.GetChars(), DOTVERSIONSTR ) != 0 )
		{
			SERVER_ClientError( lClient, NETWORK_ERRORCODE_WRONGVERSION );
#ifdef CREATE_PACKET_LOG
			server_LogPacket(pByteStream,  NETWORK_GetFromAddress( ), "Wrong version.");
#endif
			return;
		}
	}

	// Make sure the network game version matches.
	if ( NETGAMEVERSION != clientNetworkGameVersion )
	{
		SERVER_ClientError( lClient, NETWORK_ERRORCODE_WRONGPROTOCOLVERSION );
#ifdef CREATE_PACKET_LOG
		server_LogPacket(pByteStream,  NETWORK_GetFromAddress( ), "Wrong netcode version.");
#endif

		return;
	}

	// Client is now in the loop, prevent from a early timeout
	// [BB] Only do this if this is actually a new player connecting. Otherwise it
	// could be abused to keep non-finished connections alive.
	if ( bNewPlayer )
		g_aClients[lClient].ulLastCommandTic = g_aClients[lClient].ulLastGameTic = gametic;

	// Check if we require a password to join this server.
	if ( sv_forcepassword && ( strlen( sv_password ) > 0 ))
	{
		// Store password in temporary buffer (becuase we strupr it).
		strcpy( szServerPassword, sv_password );

		// Check their password against ours (both not case sensitive).
		if ( strcmp( strupr( szServerPassword ), clientPassword.GetChars() ) != 0 )
		{
			// Client has the wrong password! GET THE FUCK OUT OF HERE!
			SERVER_ClientError( lClient, NETWORK_ERRORCODE_WRONGPASSWORD );
			return;
		}
	}

	// Check if this IP has been banned.
	szAddress.SetFrom ( g_aClients[lClient].Address );
	if ( SERVERBAN_IsIPBanned( szAddress ))
	{
		// Client has been banned! GET THE FUCK OUT OF HERE!
		SERVER_ClientError( lClient, NETWORK_ERRORCODE_BANNED );
		return;
	}

	if ( sv_pure && strcmp ( pByteStream->ReadString(), g_lumpsAuthenticationChecksum.GetChars() ) )
	{
		// Client fails the lump authentication.
		SERVER_ClientError( lClient, NETWORK_ERRORCODE_PROTECTED_LUMP_AUTHENTICATIONFAILED );
		return;
	}

	// Client is now connected to the server.
	g_aClients[lClient].State = CLS_CONNECTED;

	// Reset stats, and a couple other things.
	players[lClient].fragcount = 0;
	players[lClient].killcount = 0;
	players[lClient].lPointCount = 0;
	players[lClient].ulWins = 0;
	PLAYER_ResetSpecialCounters ( &players[lClient] );
	players[lClient].ulDeathCount = 0;
	players[lClient].ulTime = 0;
	players[lClient].bSpectating = false;
	players[lClient].bDeadSpectator = false;
	players[lClient].Team = teams.Size( );
	players[lClient].bOnTeam = false;

	g_aClients[lClient].bRCONAccess = false;
	g_aClients[lClient].ulDisplayPlayer = lClient;
	g_aClients[lClient].bFullUpdateIncomplete = false;
	g_aClients[lClient].commandInstances.clear();
	g_aClients[lClient].minorCommandInstances.clear();
	for ( ulIdx = 0; ulIdx < MAX_CHATINSTANCE_STORAGE; ulIdx++ )
		g_aClients[lClient].lChatInstances[ulIdx] = 0;
	g_aClients[lClient].ulLastChatInstance = 0;
	for ( ulIdx = 0; ulIdx < MAX_USERINFOINSTANCE_STORAGE; ulIdx++ )
		g_aClients[lClient].lUserInfoInstances[ulIdx] = 0;
	g_aClients[lClient].ulLastUserInfoInstance = 0;
	g_aClients[lClient].ulLastChangeTeamTime = 0;
	g_aClients[lClient].ulLastSuicideTime = 0;
	g_aClients[lClient].lLastPacketLossTick = 0;
	g_aClients[lClient].lLastMoveTick = 0;
	g_aClients[lClient].lLastMoveTickProcess = 0;
	g_aClients[lClient].usLastWeaponNetworkIndex = 0;
	g_aClients[lClient].lOverMovementLevel = 0;
	g_aClients[lClient].bRunEnterScripts = false;
	g_aClients[lClient].bSuspicious = false;
	g_aClients[lClient].ulNumConsistencyWarnings = 0;
	g_aClients[lClient].numMissingPackets = 0;
	g_aClients[lClient].skinName = "";
	g_aClients[lClient].commRules.clear( );
	g_aClients[lClient].ScreenWidth = 0;
	g_aClients[lClient].ScreenHeight = 0;
	g_aClients[lClient].ulClientGameTic = 0;
	g_aClients[lClient].lastRespawnTick = 0;
	// [CK] Since the client is not up to date at all, the farthest the client
	// should be able to go back is the gametic they connected with.
	g_aClients[lClient].lLastServerGametic = gametic;

	// [AK] Clear any recent command gametics from the client.
	g_aClients[lClient].recentMoveCMDs.clear();
	g_aClients[lClient].recentSelectCMDs.clear();

	// [AK] Reset the client's tic buffer.
	SERVER_ResetClientTicBuffer( lClient );

	SERVER_InitClientSRPData ( lClient );

	// [BB] Inform the client that he is connected and needs to authenticate the map.
	SERVER_RequestClientToAuthenticate( lClient );
}

//*****************************************************************************
//
bool SERVER_GetUserInfo( BYTESTREAM_s *pByteStream, bool bAllowKick, bool bEnforceRequired )
{
	player_t	*pPlayer;
	FString		szSkin;
	ULONG		ulUserInfoInstance;
	// [BB] We may only kick the player after completely parsing the current message,
	// otherwise we'll get network parsing errors.
	bool		bKickPlayer = false;
	bool		bOverriddenName = false;
	FString kickReason;

	pPlayer = &players[g_lCurrentClient];

	// [TP] Don't check for userinfo flooding if we're not enforcing command limits.
	if (( bAllowKick ) && ( sv_limitcommands ))
	{
		ulUserInfoInstance = ( ++g_aClients[g_lCurrentClient].ulLastUserInfoInstance % MAX_USERINFOINSTANCE_STORAGE );
		g_aClients[g_lCurrentClient].lUserInfoInstances[ulUserInfoInstance] = gametic;

		// If this is the second time a player has updated their userinfo in a 7 tick interval (~1/5 of a second, ~1/5 of a second update interval),
		// kick him.
		if (( g_aClients[g_lCurrentClient].lUserInfoInstances[( ulUserInfoInstance + MAX_USERINFOINSTANCE_STORAGE - 1 ) % MAX_USERINFOINSTANCE_STORAGE] ) > 0 )
		{
			if ( ( g_aClients[g_lCurrentClient].lUserInfoInstances[ulUserInfoInstance] ) -
				( g_aClients[g_lCurrentClient].lUserInfoInstances[( ulUserInfoInstance + MAX_USERINFOINSTANCE_STORAGE - 1 ) % MAX_USERINFOINSTANCE_STORAGE] )
				<= 7 )
			{
				bKickPlayer = true;
				kickReason = "User info change flood.";
			}
		}

		// If this is the third time a player has updated their userinfo in a 42 tick interval (~1.5 seconds, ~.75 second update interval),
		// kick him.
		if (( g_aClients[g_lCurrentClient].lUserInfoInstances[( ulUserInfoInstance + MAX_USERINFOINSTANCE_STORAGE - 2 ) % MAX_USERINFOINSTANCE_STORAGE] ) > 0 )
		{
			if ( ( g_aClients[g_lCurrentClient].lUserInfoInstances[ulUserInfoInstance] ) -
				( g_aClients[g_lCurrentClient].lUserInfoInstances[( ulUserInfoInstance + MAX_USERINFOINSTANCE_STORAGE - 2 ) % MAX_USERINFOINSTANCE_STORAGE] )
				<= 42 )
			{
				bKickPlayer = true;
				kickReason = "User info change flood.";
			}
		}

		// If this is the fourth time a player has updated their userinfo in a 105 tick interval (~3 seconds, ~1 second interval ),
		// kick him.
		if (( g_aClients[g_lCurrentClient].lUserInfoInstances[( ulUserInfoInstance + MAX_USERINFOINSTANCE_STORAGE - 3 ) % MAX_USERINFOINSTANCE_STORAGE] ) > 0 )
		{
			if ( ( g_aClients[g_lCurrentClient].lUserInfoInstances[ulUserInfoInstance] ) -
				( g_aClients[g_lCurrentClient].lUserInfoInstances[( ulUserInfoInstance + MAX_USERINFOINSTANCE_STORAGE - 3 ) % MAX_USERINFOINSTANCE_STORAGE] )
				<= 105 )
			{
				bKickPlayer = true;
				kickReason = "User info change flood.";
			}
		}
	}

	FName name;
	FString value;
	std::set<FName> names;

	while (( name = NETWORK_ReadName( pByteStream ) ) != NAME_None )
	{
		// [SB] Kick the player if we run out of data.
		if ( pByteStream->pbStream >= pByteStream->pbStreamEnd )
		{
			bKickPlayer = true;
			kickReason = "Client sent malformed data.";
			break;
		}

		names.insert( name );
		value = pByteStream->ReadString();

		if ( name == NAME_Name )
		{
			FString oldPlayerName = pPlayer->userinfo.GetName();
			FString nameStringCopy = value;

			// [RC] Remove bad characters from their username.
			V_CleanPlayerName( value, false );

			// The user really shouldn't have an invalid name unless they are using a hacked executable.
			if ( value.Compare( nameStringCopy ) != 0 )
			{
				bKickPlayer = true;
				kickReason = "User name contains illegal characters." ;
			}

			FString message;

			// [AK] Don't allow players to name themselves as "server" in order to avert
			// any potential impersonation of the server.
			if ( PLAYER_NameMatchesServer ( value ) )
			{
				message.Format ( "You can't name yourself as the server." );
				bOverriddenName = true;
			}
			// [BB] Check whether the requested name is already in use.
			// If so, give the player a generic unused name and inform the client.
			else if ( PLAYER_NameUsed ( value, g_lCurrentClient ) )
			{
				message.Format ( "The name '%s' is already in use. ", value.GetChars() );
				bOverriddenName = true;
			}

			if ( bOverriddenName )
			{
				value = PLAYER_GenerateUniqueName();
				message.AppendFormat ( "You are renamed to '%s'.\n", value.GetChars() );
				SERVERCOMMANDS_PrintMid( message, true, g_lCurrentClient, SVCF_ONLYTHISCLIENT );
			}

			// [BB] It's extremely critical that we NEVER use the uncleaned player name!
			// If this is omitted, for example the RCON password can be optained by just using
			// a tampered name.
			pPlayer->userinfo.NameChanged ( value.GetChars() );

			if ( g_aClients[g_lCurrentClient].State == CLS_SPAWNED )
			{
				FString playerNameNoColor = pPlayer->userinfo.GetName();
				FString oldPlayerNameNoColor = oldPlayerName;

				V_ColorizeString( playerNameNoColor );
				V_RemoveColorCodes( playerNameNoColor );
				V_ColorizeString( oldPlayerNameNoColor );
				V_RemoveColorCodes( oldPlayerNameNoColor );

				if ( playerNameNoColor.CompareNoCase( oldPlayerNameNoColor ) != 0 )
				{
					SERVER_Printf( "%s is now known as %s\n", oldPlayerName.GetChars(), pPlayer->userinfo.GetName() );

					// [RC] Update clients using the RCON utility.
					SERVER_RCON_UpdateInfo( SVRCU_PLAYERDATA );
				}
			}
		}
		else if ( name == NAME_Gender )
			pPlayer->userinfo.GenderNumChanged ( value.ToLong() );
		else if ( name == NAME_Color )
			pPlayer->userinfo.ColorChanged ( value );
		else if ( name == NAME_ColorSet )
			pPlayer->userinfo.ColorSetChanged ( value.ToLong() );
		else if ( name == NAME_Autoaim )
			*static_cast<FFloatCVar *>(pPlayer->userinfo[NAME_Autoaim]) = static_cast<float> ( value.ToDouble() );
		else if ( name == NAME_Skin )
			szSkin = value;
		else if ( name == NAME_RailColor )
			pPlayer->userinfo.RailColorChanged ( value.ToLong() );
		else if ( name == NAME_Handicap )
			pPlayer->userinfo.HandicapChanged ( value.ToLong() );
		// [BB]
		else if ( name == NAME_CL_TicsPerUpdate )
			pPlayer->userinfo.TicsPerUpdateChanged ( value.ToLong() );
		// [BB]
		else if ( name == NAME_CL_ConnectionType )
			pPlayer->userinfo.ConnectionTypeChanged ( value.ToLong() );
		// [CK] We use a bitfield now.
		else if ( name == NAME_CL_ClientFlags )
			pPlayer->userinfo.ClientFlagsChanged ( value.ToLong() );
		// [AK]
		else if ( name == NAME_Voice_Enable )
			pPlayer->userinfo.VoiceEnableChanged ( value.ToLong() );
		// [AK]
		else if ( name == NAME_Voice_ListenFilter )
			pPlayer->userinfo.VoiceListenFilterChanged ( value.ToLong() );
		// [AK]
		else if ( name == NAME_Voice_TransmitFilter )
			pPlayer->userinfo.VoiceTransmitFilterChanged ( value.ToLong() );
		// If this is a Hexen game, read in the player's class.
		else if ( name == NAME_PlayerClass )
		{
			pPlayer->userinfo.PlayerClassNumChanged ( value.ToLong() );

			// If the player class is changed, we also have to reset cls.
			// Otherwise cls will not be reinitialized in P_SpawnPlayer.
			pPlayer->cls = NULL;
		}
		else
		{
			FBaseCVar **cvarPointer = pPlayer->userinfo.CheckKey( name );
			FBaseCVar *cvar = cvarPointer ? *cvarPointer : nullptr;

			if ( cvar )
			{
				UCVarValue cvarValue;
				cvarValue.String = value;
				cvar->SetGenericRep( cvarValue, CVAR_String );
			}
		}
	}

	// Now that we've (maybe) read in the player's class information, try to find a skin
	// for him based on his class.
	if ( szSkin.IsNotEmpty() )
	{
		// Store the name of the skin the client gave us, so others can view the skin
		// even if the server doesn't have the skin loaded.
		g_aClients[g_lCurrentClient].skinName = szSkin;

		// [BB] This can't be done if PlayerClass == -1, but shouldn't be necessary anyway,
		// since it's done as soon as the player is spawned in P_SpawnPlayer.
		if ( pPlayer->userinfo.GetPlayerClassNum() != -1 )
			pPlayer->userinfo.SkinNumChanged ( R_FindSkin( szSkin, pPlayer->userinfo.GetPlayerClassNum() ) );
	}

	// [BB] Make sure that the joining client sends the full user info (sending player class is not mandatory though).
	// [SB] This check is not done if bKickPlayer is true, allowing any decisions to kick a player to just fall through.
	if ( bEnforceRequired && !bKickPlayer )
	{
		static const std::set<FName> required = {
			NAME_Name, NAME_Autoaim, NAME_Gender, NAME_Skin, NAME_RailColor,
			NAME_CL_ConnectionType, NAME_CL_ClientFlags,
			NAME_Handicap, NAME_CL_TicsPerUpdate, NAME_Color, NAME_ColorSet,
			NAME_Voice_Enable, NAME_Voice_ListenFilter, NAME_Voice_TransmitFilter
		};
		std::set<FName> missing;
		std::set_difference( required.begin(), required.end(), names.begin(), names.end(),
							 std::inserter ( missing, missing.begin() ) );
		if ( missing.size() > 0 )
		{
			bKickPlayer = true;
			kickReason = "Userinfo is incomplete";
		}
	}

	if ( bKickPlayer )
	{
		// [TP] If the client isn't fully in yet, send an error and disconnect him.
		if ( g_aClients[g_lCurrentClient].State != CLS_SPAWNED )
		{
			SERVER_PrintfPlayer( g_lCurrentClient, "\n%s\n", kickReason.GetChars() );
			SERVER_ClientError( g_lCurrentClient, NETWORK_ERRORCODE_USERINFOREJECTED );
			return ( false );
		}

		SERVER_KickPlayer( g_lCurrentClient, kickReason.GetChars() );
		return ( false );
	}

	// Now send out the player's userinfo out to other players.
	SERVERCOMMANDS_SetPlayerUserInfo( g_lCurrentClient, names, g_lCurrentClient, SVCF_SKIPTHISCLIENT );

	// [BB] If his name was overridden, tell him that.
	if ( bOverriddenName )
		SERVERCOMMANDS_SetPlayerUserInfo( g_lCurrentClient, { NAME_Name }, g_lCurrentClient, SVCF_ONLYTHISCLIENT );

	// Also, update the scoreboard.
	SERVERCONSOLE_UpdatePlayerInfo( g_lCurrentClient, UDF_NAME );

	// Success!
	return ( true );
}

//*****************************************************************************
//
void SERVER_ConnectionError( NETADDRESS_s Address, const char *pszMessage, ULONG ulErrorCode )
{
	NETBUFFER_s	TempBuffer;

	TempBuffer.Init( MAX_UDP_PACKET, BUFFERTYPE_WRITE );

	// Display error message locally in the console.
	if ( sv_printconnectionmessages )
		Printf( "Denied connection for %s: %s\n", Address.ToString(), pszMessage );

	// Make sure the packet has a packet header. The client is expecting this!
	TempBuffer.ByteStream.WriteHeader( SVC_HEADER );
	TempBuffer.ByteStream.WriteLong( 0 );

	TempBuffer.ByteStream.WriteByte( SVCC_ERROR );
	TempBuffer.ByteStream.WriteByte( ulErrorCode );

//	NETWORK_LaunchPacket( TempBuffer, Address, true );
	NETWORK_LaunchPacket( &TempBuffer, Address );
	TempBuffer.Free();
}

//*****************************************************************************
//
void SERVER_ClientError( ULONG ulClient, ULONG ulErrorCode )
{
	g_aClients[ulClient].PacketBuffer.ByteStream.WriteByte( SVCC_ERROR );
	g_aClients[ulClient].PacketBuffer.ByteStream.WriteByte( ulErrorCode );

	FString printMessage;

	// Display error message locally in the console.
	switch ( ulErrorCode )
	{
	case NETWORK_ERRORCODE_WRONGPASSWORD:

		printMessage.Format( "Incorrect password.\n" );
		break;
	case NETWORK_ERRORCODE_WRONGVERSION:

		printMessage.Format( "Incorrect version.\n" );

		// Tell the client what version this server using.
		g_aClients[ulClient].PacketBuffer.ByteStream.WriteString( DOTVERSIONSTR );
		break;
	case NETWORK_ERRORCODE_WRONGPROTOCOLVERSION:

		printMessage.Format( "Incorrect protocol version.\n" );

		// Tell the client what version this server using.
		g_aClients[ulClient].PacketBuffer.ByteStream.WriteString( GetVersionStringRev() );
		break;
	case NETWORK_ERRORCODE_BANNED:
		{
			IPADDRESSBAN_s *entry = SERVERBAN_GetBanInformation( g_aClients[ulClient].Address );
			FString banReason = (( entry != nullptr ) && ( strlen( entry->szComment ) > 0 )) ? entry->szComment : "";

			if ( banReason.IsNotEmpty() )
				printMessage.Format( "Client banned (reason: %s)\n", banReason.GetChars() );
			else
				printMessage.Format( "Client banned.\n" );

			bool masterban = SERVERBAN_IsIPMasterBanned( g_aClients[ulClient].Address );
			g_aClients[ulClient].PacketBuffer.ByteStream.WriteByte( masterban );

			if ( masterban == false )
			{
				// Tell the client why he was banned, and when his ban expires.
				g_aClients[ulClient].PacketBuffer.ByteStream.WriteString( banReason );
				g_aClients[ulClient].PacketBuffer.ByteStream.WriteLong(( entry != nullptr ) ? static_cast<int>( entry->tExpirationDate ) : 0 );
				g_aClients[ulClient].PacketBuffer.ByteStream.WriteString( sv_hostemail );
			}
		}
		break;
	case NETWORK_ERRORCODE_AUTHENTICATIONFAILED:
	case NETWORK_ERRORCODE_PROTECTED_LUMP_AUTHENTICATIONFAILED:

		// [SB] Send all authenticated WADs to the client for comparison.
		g_aClients[ulClient].PacketBuffer.ByteStream.WriteByte( NETWORK_GetAuthenticatedWADsList().Size() );
		for ( unsigned int i = 0; i < NETWORK_GetAuthenticatedWADsList().Size(); ++i )
		{
			const NetworkPWAD& pwad = NETWORK_GetAuthenticatedWADsList()[i];
			g_aClients[ulClient].PacketBuffer.ByteStream.WriteString( pwad.name );
			g_aClients[ulClient].PacketBuffer.ByteStream.WriteString( pwad.checksum );
		}

		printMessage.Format( "%s authentication failed.\n", ( ulErrorCode == NETWORK_ERRORCODE_PROTECTED_LUMP_AUTHENTICATIONFAILED ) ? "Protected lump" : "Level" );
		break;
	case NETWORK_ERRORCODE_USERINFOREJECTED:

		printMessage.Format( "Userinfo rejected.\n" );
		break;
	}

	// Send the packet off.
	SERVER_SendClientPacket( ulClient, true );

	// [SB] Only print if sv_printconnectionmessages is set, or the client was already in the game.
	if ( sv_printconnectionmessages || g_aClients[ulClient].State >= CLS_SPAWNED_BUT_NEEDS_AUTHENTICATION )
	{
		Printf( "%s", printMessage.GetChars() );
		Printf( "%s disconnected. Ignoring IP for 10 seconds.\n", g_aClients[ulClient].Address.ToString() );
	}

	// [BB] Block this IP for ten seconds to prevent log flooding.
	SERVER_IgnoreIP( g_aClients[ulClient].Address );

	// [BB] Be sure to properly disconnect the client.
	SERVER_DisconnectClient( ulClient, false, false, LEAVEREASON_ERROR );
}

//*****************************************************************************
//
void SERVER_SendFullUpdate( ULONG ulClient )
{
	AActor						*pActor;
	ULONG						ulIdx;
	player_t*					pPlayer;
	AInventory					*pInventory;
	TThinkerIterator<AActor>	Iterator;

	// Send active players to the client.
	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if (( ulClient == ulIdx ) || ( playeringame[ulIdx] == false ))
			continue;

		pPlayer = &players[ulIdx];
		if ( pPlayer->mo == NULL )
			continue;

		// [BB] To properly spawn the players the client already needs to know the userinfo, e.g. the handicap value.
		SERVERCOMMANDS_SetAllPlayerUserInfo( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );
		// [BB] Make sure that morphed players are spawned as morphed.
		SERVERCOMMANDS_SpawnPlayer( ulIdx, PST_REBORNNOINVENTORY, ulClient, SVCF_ONLYTHISCLIENT, !!( pPlayer->morphTics ) );
		// [BB] Since the player possibly lost something from his default inventory, destory everything
		// he is spawned with on the client. Everything the client has to know about the inventory is
		// handled below.
		SERVERCOMMANDS_DestroyAllInventory( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );

		// Also send this player's team.
		if ( GAMEMODE_GetCurrentFlags() & GMF_PLAYERSONTEAMS )
			SERVERCOMMANDS_SetPlayerTeam( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );

		// Check if we need to tell the incoming player about any powerups this player may have.
		// [BB] Also tell about all the ammo, weapons, backpacks and keys this player has.
		// [BB] Keys need to be handled carefully. In order to display them properly in ST's fullscrenn HUD
		// in coop spy, they need to be given in reverse order.
		TArray<AInventory *> keys;
		for ( pInventory = pPlayer->mo->Inventory; pInventory != NULL; pInventory = pInventory->Inventory )
		{
			if ( pInventory->IsKindOf( RUNTIME_CLASS( APowerup )))
			{
				SERVERCOMMANDS_GivePowerup( ulIdx, static_cast<APowerup *>( pInventory ), ulClient, SVCF_ONLYTHISCLIENT );

				// [BB] If it's a rune, we need to explicitly set its icon since it was set by the RuneGiver.
				if ( pInventory == pInventory->Owner->Rune )
				{
					SERVERCOMMANDS_SetInventoryIcon( ulIdx, pInventory, ulClient, SVCF_ONLYTHISCLIENT );
				}
			}
			// [WS] Inform clients of their PowerupGiver items.
			else if ( pInventory->IsKindOf( RUNTIME_CLASS( AAmmo )) || pInventory->IsKindOf( RUNTIME_CLASS( AWeapon ))
				|| pInventory->IsKindOf( RUNTIME_CLASS( ABackpackItem ))
				|| pInventory->IsKindOf( RUNTIME_CLASS( APowerupGiver )) )
			{
				SERVERCOMMANDS_GiveInventory( ulIdx, pInventory, ulClient, SVCF_ONLYTHISCLIENT );
				// [BB] Ammo max amount needs to be handled explicitly.
				if ( pInventory->IsKindOf( RUNTIME_CLASS( AAmmo ) ) )
					SERVERCOMMANDS_SetPlayerAmmoCapacity( ulIdx, pInventory, ulClient, SVCF_ONLYTHISCLIENT );
			}
			else if ( pInventory->IsKindOf( RUNTIME_CLASS( AKey )) )
				keys.Push ( pInventory );
			else if ( pInventory->IsA( RUNTIME_CLASS( AWeaponHolder )) )
			{
				// [Dusk] Inform the client of weapon holders
				SERVERCOMMANDS_GiveWeaponHolder( ulIdx, static_cast<AWeaponHolder *>( pInventory ), ulClient, SVCF_ONLYTHISCLIENT );
			}
		}
		// [BB] Now give the keys we just collected from the inventory in reverse order.
		while ( keys.Size() )
		{
			keys.Pop( pInventory );
			SERVERCOMMANDS_GiveInventory( ulIdx, pInventory, ulClient, SVCF_ONLYTHISCLIENT );
		}

		// Also if this player is currently dead, let the incoming player know that.
		if ( pPlayer->mo->health <= 0 )
			SERVERCOMMANDS_ThingIsCorpse( pPlayer->mo, ulClient, SVCF_ONLYTHISCLIENT );
		// [BB] SERVERCOMMANDS_SpawnPlayer instructs the client to spawn a player
		// with default health. So we still need to send the correct current health value
		// (if the player is not dead). Also set the armor and the corresponding max bonuses.
		else
		{
			SERVERCOMMANDS_SetPlayerHealthAndMaxHealthBonus( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );
			SERVERCOMMANDS_SetPlayerArmorAndMaxArmorBonus( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );
			// [BB] Also send all non-default flag values.
			SERVERCOMMANDS_UpdateThingFlagsNotAtDefaults( pPlayer->mo, ulClient, SVCF_ONLYTHISCLIENT );

			// [BB] If the player is in its SeeState, let the client know.
			if ( ( pPlayer->mo->SeeState != NULL ) && ( pPlayer->mo->InStateSequence(pPlayer->mo->state, pPlayer->mo->SeeState) ) )
				SERVERCOMMANDS_SetPlayerState( ulIdx, STATE_PLAYER_SEE, ulClient, SVCF_ONLYTHISCLIENT);
		}

		// [BB] Clients need to know the active weapons of other players, so send it.
		SERVERCOMMANDS_WeaponChange( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );

		// [geNia] Clients need to know player's current skin in ACS.
		SERVERCOMMANDS_SetPlayerACSSkin( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );

		// [BB] It's possible that the MaxHealth property was changed dynamically with ACS, so send it.
		SERVERCOMMANDS_SetPlayerMaxHealth( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );

		// [BB] Send the number of lives left.
		SERVERCOMMANDS_SetPlayerLivesLeft( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );

		// [BB] Send this player's statuses to the new client.
		SERVERCOMMANDS_SetPlayerStatus( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );

		// [BB] If this player has any cheats, also inform the new client.
		if( players[ulIdx].cheats )
			SERVERCOMMANDS_SetPlayerCheats(  ulIdx, ulClient, SVCF_ONLYTHISCLIENT );

		// [Dusk] Hexen armor values
		SERVERCOMMANDS_SyncHexenArmorSlots( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );

		// [WS] Update the player's properties if they changed.
		SERVER_UpdateActorProperties( players[ulIdx].mo, ulClient );

		// [TP] Update the player's TID, if there is one.
		if ( players[ulIdx].mo->tid )
			SERVERCOMMANDS_SetThingTID( players[ulIdx].mo, ulClient, SVCF_ONLYTHISCLIENT );

		// [AK] Update the player's country index if they're not a bot, they aren't hiding it, and it isn't "N/A".
		if (( players[ulIdx].bIsBot == false ) && ( g_aClients[ulIdx].bWantHideCountry == false ) && ( players[ulIdx].ulCountryIndex > 0 ))
			SERVERCOMMANDS_SetPlayerCountry( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );

		// [TP] Account name.
		if ( g_aClients[ulIdx].WantHideAccount == false )
			SERVERCOMMANDS_SetPlayerAccountName( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );
	}

	// Server may have already picked a team for the incoming player. If so, tell him!
	if (( GAMEMODE_GetCurrentFlags() & GMF_PLAYERSONTEAMS ) && players[ulClient].bOnTeam )
		SERVERCOMMANDS_SetPlayerTeam( ulClient, ulClient, SVCF_ONLYTHISCLIENT );

	// [AK] In case this player's already dead, let them know how much time they
	// have left until they can respawn again.
	if ( players[ulClient].playerstate == PST_DEAD )
		SERVERCOMMANDS_SetLocalPlayerRespawnDelayTime( ulClient );

	// [BB] This game mode uses teams, so inform the incoming player about the scores/wins/frags of the teams.
	if ( GAMEMODE_GetCurrentFlags() & GMF_PLAYERSONTEAMS )
	{
		for ( ulIdx = 0; ulIdx < teams.Size( ); ulIdx++ )
		{
			if ( GAMEMODE_GetCurrentFlags() & GMF_PLAYERSEARNWINS )
				SERVERCOMMANDS_SetTeamScore( ulIdx, TEAMSCORE_WINS, false, ulClient, SVCF_ONLYTHISCLIENT );
			else if ( GAMEMODE_GetCurrentFlags() & GMF_PLAYERSEARNPOINTS )
				SERVERCOMMANDS_SetTeamScore( ulIdx, TEAMSCORE_POINTS, false, ulClient, SVCF_ONLYTHISCLIENT );
			else if ( GAMEMODE_GetCurrentFlags() & GMF_PLAYERSEARNFRAGS )
				SERVERCOMMANDS_SetTeamScore( ulIdx, TEAMSCORE_FRAGS, false, ulClient, SVCF_ONLYTHISCLIENT );
		}
	}

	// [BB] Tell individual player scores/wins/frags/kills to the client.
	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if ( playeringame[ulIdx] == false )
			continue;

		// [BB] In all cooperative game modes players get kills, otherwise they get frags
		// (even if the game mode is not won with frags).
		if ( GAMEMODE_GetCurrentFlags() & GMF_COOPERATIVE )
			SERVERCOMMANDS_SetPlayerKillCount( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );
		else
			SERVERCOMMANDS_SetPlayerFrags( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );

		if ( GAMEMODE_GetCurrentFlags() & GMF_PLAYERSEARNWINS )
			SERVERCOMMANDS_SetPlayerWins( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );
		else if ( GAMEMODE_GetCurrentFlags() & GMF_PLAYERSEARNPOINTS )
			SERVERCOMMANDS_SetPlayerPoints( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );

		// [AK] Tell the client how many times this player died.
		SERVERCOMMANDS_SetPlayerDeaths( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );
	}

	// Send Domination State
	if ( domination )
	{
		for ( unsigned int i = 0; i < level.info->SectorInfo.Points.Size(); i++ )
		{
			SERVERCOMMANDS_SetDominationPointOwner( i, level.info->SectorInfo.Points[i].owner, false, ulClient, SVCF_ONLYTHISCLIENT );
			SERVERCOMMANDS_SetDominationPointState( i, level.info->SectorInfo.Points[i], ulClient, SVCF_ONLYTHISCLIENT );
		}
	}

	// If we're in duel mode, tell the client how many duels have taken place.
	if ( duel )
		SERVERCOMMANDS_SetDuelNumDuels( ulClient, SVCF_ONLYTHISCLIENT );

	// Send the level time.
	if ( timelimit )
		SERVERCOMMANDS_SetMapTime( ulClient, SVCF_ONLYTHISCLIENT );

	// Go through all the items on the map, and tell the client to spawn those of which
	// are important.
	while (( pActor = Iterator.Next( )))
	{
		// If the actor doesn't have a network ID, don't spawn it (it
		// probably isn't important).
		if ( pActor->NetID == 0 )
			continue;

		// [BB] The other clients already have destroyed this actor, so don't spawn it.
		if ( pActor->NetworkFlags & NETFL_DESTROYED_ON_CLIENT )
			continue;

		// Don't spawn players, items about to be deleted, inventory items
		// that have an owner, or items that the client spawns himself.
		if (( pActor->IsKindOf( RUNTIME_CLASS( APlayerPawn ))) ||
			( pActor->state == RUNTIME_CLASS ( AInventory )->ActorInfo->FindState("HoldAndDestroy") ) ||	// S_HOLDANDDESTROY
			( pActor->state == RUNTIME_CLASS ( AInventory )->ActorInfo->FindState("Held") ) || // S_HELD
			( pActor->NetworkFlags & NETFL_ALLOWCLIENTSPAWN ))
		{
			continue;
		}

		// [BB] Don't spawn things hidden by AActor::HideOrDestroyIfSafe().
		// The clients don't need them at all, since the server will tell
		// them to spawn a new actor during GAME_ResetMap anyway.
		if ( !( pActor->IsKindOf( RUNTIME_CLASS( AInventory ) ) )
		     && ( pActor->state == RUNTIME_CLASS ( AInventory )->ActorInfo->FindState("HideIndefinitely") ) // S_HIDEINDEFINITELY 
		   )
		{
			continue;
		}

		// Spawn a missile. Missiles must be handled differently because they have
		// velocity.
		if ( pActor->flags & MF_MISSILE )
		{
			SERVERCOMMANDS_SpawnMissile( pActor, ulClient, SVCF_ONLYTHISCLIENT );
		}
		// Tell the client to spawn this thing.
		else
		{
			// [EP] Handle level-spawned actors which didn't move yet on X/Y axes.
			bool shouldLevelSpawn = false;
			if ((pActor->STFlags & STFL_LEVELSPAWNED) != 0)
			{
				shouldLevelSpawn = (pActor->x == pActor->SpawnPoint[0] 
					&& pActor->y == pActor->SpawnPoint[1]);
			}
			if ( shouldLevelSpawn )
			{
				SERVERCOMMANDS_LevelSpawnThing( pActor, ulClient, SVCF_ONLYTHISCLIENT );
			}
			else
			{
				SERVERCOMMANDS_SpawnThing( pActor, ulClient, SVCF_ONLYTHISCLIENT );
			}
			// [BB] If the thing is not at its spawn point, let the client know about the spawn point.
			if ( ( pActor->x != pActor->SpawnPoint[0] )
				|| ( pActor->y != pActor->SpawnPoint[1] )
				|| ( pActor->z != pActor->SpawnPoint[2] )
				)
			{
				SERVERCOMMANDS_SetThingSpawnPoint( pActor, ulClient, SVCF_ONLYTHISCLIENT );
			}

			// [BB] Since the monster movement is client side, the client needs to be
			// informed about the velocity and the current state. If the frame is not
			// set, the client thinks the actor is in its spawn state.
			{

				if ( (pActor->InSpawnState() == false)
					 && !(( pActor->health <= 0 ) && ( pActor->flags & MF_COUNTKILL )) // [BB] Corpses are handled later.
					 )
				{
					SERVERCOMMANDS_SetThingFrame( pActor, pActor->state, ulClient, SVCF_ONLYTHISCLIENT, false );
				}

				// [WS/BB] Always inform client of the actor's lastX/Y/Z.
				ULONG ulBits = CM_LAST_X|CM_LAST_Y|CM_LAST_Z;

				if ( pActor->velx != 0 )
					ulBits |= CM_VELX;

				if ( pActor->vely != 0 )
					ulBits |= CM_VELY;

				if ( pActor->velz != 0 )
					ulBits |= CM_VELZ;

				if ( pActor->pitch != 0 )
					ulBits |= CM_PITCH;

				if ( pActor->movedir != 0 )
					ulBits |= CM_MOVEDIR;

				if ( ulBits != 0 )
					SERVERCOMMANDS_MoveThingExact( pActor, ulBits, ulClient, SVCF_ONLYTHISCLIENT );
			}

			// If it's important to update this thing's arguments, do that now.
			// [BB] Wouldn't it be better, if this is done for all things, for which
			// at least one of the arguments is not equal to zero?
			// [BC] It's not necessarily important for clients to know this, such
			// as with invasion spawners. You can do it if you want, though! It would
			// probably save headache later on.
			//if ( pActor->NetworkFlags & NETFL_UPDATEARGUMENTS )
			// [BB] I don't want to export NETFL_UPDATEARGUMENTS to DECORATE, so we have
			// to tell the clients all the arguments.
			if ( ( pActor->args[0] != 0 )
				|| ( pActor->args[1] != 0 )
				|| ( pActor->args[2] != 0 )
				|| ( pActor->args[3] != 0 )
				|| ( pActor->args[4] != 0 ) )
				SERVERCOMMANDS_SetThingArguments( pActor, ulClient, SVCF_ONLYTHISCLIENT );

			// [BB] Clients need to know the SectorAction specials to predict them.
			// [EP] Spectators need to know the allowed specials to use them.
			if ( ( NETWORK_IsClientPredictedSpecial ( pActor->special ) || GAMEMODE_IsSpectatorAllowedSpecial ( pActor->special ) )
				&& pActor->IsKindOf( PClass::FindClass( "SectorAction" ) ) )
				SERVERCOMMANDS_SetThingSpecial ( pActor, ulClient, SVCF_ONLYTHISCLIENT );

			// [BB] Some things like AMovingCamera rely on the AActor tid.
			// So tell it to the client. I have no idea if this has unwanted side
			// effects. Has to be checked.
			if ( pActor->tid != 0 )
				SERVERCOMMANDS_SetThingTID( pActor, ulClient, SVCF_ONLYTHISCLIENT );

			// If this thing's translation has been altered, tell the client.
			if ( pActor->Translation != 0 )
				SERVERCOMMANDS_SetThingTranslation( pActor, ulClient, SVCF_ONLYTHISCLIENT );

			// This item has been picked up, and is in its hidden, respawn state. Let
			// the client know that.
			if (( pActor->state == RUNTIME_CLASS ( AInventory )->ActorInfo->FindState("HideDoomish") ) ||	// S_HIDEDOOMISH
				( pActor->state == RUNTIME_CLASS ( AInventory )->ActorInfo->FindState("HideSpecial") ) ||	// S_HIDESPECIAL
				( pActor->state == RUNTIME_CLASS ( AInventory )->ActorInfo->FindState("HideIndefinitely") ))
			{
				SERVERCOMMANDS_HideThing( pActor, ulClient, SVCF_ONLYTHISCLIENT );
			}

			// Let the clients know if an object is dormant or not.
			if ( pActor->IsActive( ) == false )
				SERVERCOMMANDS_ThingDeactivate( pActor, NULL, ulClient, SVCF_ONLYTHISCLIENT );

			// [BB] Active ActorMovers need to be synced with the client.
			if ( pActor->IsKindOf( PClass::FindClass( "ActorMover" ) ) && pActor->IsActive( ) )
			{
				static_cast<APathFollower *> ( pActor )->SyncWithClient ( ulClient );
				SERVERCOMMANDS_ThingActivate( pActor, NULL, ulClient, SVCF_ONLYTHISCLIENT );
			}

			// Update the water level of the actor, but not if it's a player!
			if (( pActor->waterlevel > 0 ) && ( pActor->player == NULL ))
				SERVERCOMMANDS_SetThingWaterLevel( pActor, ulClient, SVCF_ONLYTHISCLIENT );

			// [WS] Update the actor's properties if they changed.
			SERVER_UpdateActorProperties( pActor, ulClient );

			// If any of this actor's flags have changed during the course of the level, notify
			// the client.
			// [BB] InterpolationPoint abuses the MF_AMBUSH flag, so we have to exclude this class here.
			if ( pActor->IsKindOf( PClass::FindClass( "InterpolationPoint" ) ) == false )
				SERVERCOMMANDS_UpdateThingFlagsNotAtDefaults( pActor, ulClient, SVCF_ONLYTHISCLIENT );

			// [BB] Now that the ammo amount from weapon pickups is handled on the server
			// this shouldn't be necessary anymore. Remove after thorough testing.
			// If this is a weapon, tell the client how much ammo it gives.
			//if ( pActor->IsKindOf( RUNTIME_CLASS( AWeapon )))
			//	SERVERCOMMANDS_SetWeaponAmmoGive( pActor, ulClient, SVCF_ONLYTHISCLIENT );
		}

		// Check and see if it's important that the client know the angle of the object.
		if ( pActor->angle != 0 )
			SERVERCOMMANDS_SetThingAngle( pActor, ulClient, SVCF_ONLYTHISCLIENT );

		// Spawned monster is a corpse.
		if (( pActor->health <= 0 ) && ( pActor->flags & MF_COUNTKILL ))
		{
			SERVERCOMMANDS_ThingIsCorpse( pActor, ulClient, SVCF_ONLYTHISCLIENT );

			// [Dusk/BB] Actor is not normally dead, let clients know the proper frame.
			if ( pActor->InState (pActor->FindState (NAME_Death)) == false )
				SERVERCOMMANDS_SetThingFrame( pActor, pActor->state, ulClient, SVCF_ONLYTHISCLIENT, false );
		}
	}

	// Tell clients the found/total item/secrets count.
	if ( GAMEMODE_GetCurrentFlags() & GMF_COOPERATIVE )
	{
		SERVERCOMMANDS_SetMapNumFoundItems( ulClient, SVCF_ONLYTHISCLIENT );
		SERVERCOMMANDS_SetMapNumTotalItems( ulClient, SVCF_ONLYTHISCLIENT );
		SERVERCOMMANDS_SetMapNumFoundSecrets( ulClient, SVCF_ONLYTHISCLIENT );
		SERVERCOMMANDS_SetMapNumTotalSecrets( ulClient, SVCF_ONLYTHISCLIENT );
	}

	// Also let the client know about any cameras set to textures.
	FCanvasTextureInfo::UpdateToClient( ulClient );

	// Send out any translations that have been edited since the start of the level.
	for ( ulIdx = 0; ulIdx < g_EditedTranslationList.Size( ); ulIdx++ )
	{
		if ( g_EditedTranslationList[ulIdx].ulType == DLevelScript::PCD_TRANSLATIONRANGE1 )
			SERVERCOMMANDS_CreateTranslation( g_EditedTranslationList[ulIdx].ulIdx, g_EditedTranslationList[ulIdx].ulStart, g_EditedTranslationList[ulIdx].ulEnd, g_EditedTranslationList[ulIdx].ulPal1, g_EditedTranslationList[ulIdx].ulPal2, ulClient, SVCF_ONLYTHISCLIENT );
		else if ( g_EditedTranslationList[ulIdx].ulType == DLevelScript::PCD_TRANSLATIONRANGE2 )
			SERVERCOMMANDS_CreateTranslation( g_EditedTranslationList[ulIdx].ulIdx, g_EditedTranslationList[ulIdx].ulStart, g_EditedTranslationList[ulIdx].ulEnd, g_EditedTranslationList[ulIdx].ulR1, g_EditedTranslationList[ulIdx].ulG1, g_EditedTranslationList[ulIdx].ulB1, g_EditedTranslationList[ulIdx].ulR2, g_EditedTranslationList[ulIdx].ulG2, g_EditedTranslationList[ulIdx].ulB2, ulClient, SVCF_ONLYTHISCLIENT );
		else
			SERVERCOMMANDS_CreateDesaturatedTranslation( g_EditedTranslationList[ulIdx].ulIdx, g_EditedTranslationList[ulIdx].ulStart, g_EditedTranslationList[ulIdx].ulEnd, g_EditedTranslationList[ulIdx].fR1, g_EditedTranslationList[ulIdx].fG1, g_EditedTranslationList[ulIdx].fB1, g_EditedTranslationList[ulIdx].fR2, g_EditedTranslationList[ulIdx].fG2, g_EditedTranslationList[ulIdx].fB2, ulClient, SVCF_ONLYTHISCLIENT );
	}

	// [AK] Send out any looping sounds that might be playing on an actor's sound channels.
	for ( ulIdx = 0; ulIdx < g_LoopingChannelList.Size(); ulIdx++ )
		SERVERCOMMANDS_SoundActor( g_LoopingChannelList[ulIdx].Actor, g_LoopingChannelList[ulIdx].EntChannel | g_LoopingChannelList[ulIdx].ChanFlags | CHAN_LOOP, S_GetName( g_LoopingChannelList[ulIdx].SoundID ), g_LoopingChannelList[ulIdx].Volume, g_LoopingChannelList[ulIdx].DistanceScale, ulClient, SVCF_ONLYTHISCLIENT, true );

	// [BB] If the sky differs from the standard sky, let the client know about it.
	if ( level.info 
	     && ( ( stricmp( level.skypic1, level.info->skypic1 ) != 0 )
	          || ( stricmp( level.skypic2, level.info->skypic2 ) != 0 ) )
	   )
	{
		SERVERCOMMANDS_SetMapSky( ulClient, SVCF_ONLYTHISCLIENT );
	}

	// [EP] If the sky scroll speed is changed, let the client know about it.
	if ( level.info && level.skyspeed1 != level.info->skyspeed1 )
		SERVERCOMMANDS_SetMapSkyScrollSpeed( /*isSky1 =*/ true );
	if ( level.info && level.skyspeed2 != level.info->skyspeed2 )
		SERVERCOMMANDS_SetMapSkyScrollSpeed( /*isSky1 =*/ false );

	// [BB]
	SERVERCOMMANDS_SetDefaultSkybox( ulClient, SVCF_ONLYTHISCLIENT ); 

	// [BB] Inform the client about the values of server mod cvars.
	SERVER_SyncServerModCVars ( ulClient );

	// [TP] Inform the client of the state of the join queue
	SERVERCOMMANDS_SyncJoinQueue( ulClient, SVCF_ONLYTHISCLIENT );

	// [AK] Inform the client of the medals that each player has earned.
	SERVERCOMMANDS_SyncPlayerMedalCounts( ulClient, SVCF_ONLYTHISCLIENT );

	// [BB] Let the client know that the full update is completed.
	SERVERCOMMANDS_FullUpdateCompleted( ulClient );
	// [BB] The client will let us know that it received the update.
	SERVER_GetClient ( ulClient )->bFullUpdateIncomplete = true;

	// [AK] Tell the client everything they need to know about custom player values.
	// This must be done after the client received the full update.
	if ( gameinfo.CustomPlayerData.CountUsed( ) > 0 )
	{
		TMap<FName, PlayerData>::Iterator it( gameinfo.CustomPlayerData );
		TMap<FName, PlayerData>::Pair *pair;

		while ( it.NextPair( pair ))
		{
			const PlayerValue DefaultVal = pair->Value.GetDefaultValue( );

			// [AK] First, tell them to reset everyone's values to default.
			SERVERCOMMANDS_ResetCustomPlayerValue( pair->Value, MAXPLAYERS, g_lCurrentClient, SVCF_ONLYTHISCLIENT );

			for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
			{
				// [AK] Ignore the client themselves, or invalid players.
				if (( ulIdx == static_cast<ULONG>( g_lCurrentClient )) || ( PLAYER_IsValidPlayer( ulIdx ) == false ))
					continue;

				// [AK] Don't bother sending out values that are already equal to the default value.
				if ( pair->Value.GetValue( ulIdx ) == DefaultVal )
					continue;

				SERVERCOMMANDS_SetCustomPlayerValue( pair->Value, ulIdx, g_lCurrentClient, SVCF_ONLYTHISCLIENT );
			}
		}
	}
}

//*****************************************************************************
//
void SERVER_WriteCommands( void )
{
	// Ping clients and stuff.
	SERVER_SendHeartBeat( );

	for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ++ulIdx )
	{
		// [BB] Only clients need to be informed about player movement.
		if ( SERVER_IsValidClient( ulIdx ) == false )
			continue;

		// [BB] If we requested the client to change his weapon, keep bugging him till the change is confirmed.
		// This is necessary because the client doesn't notice when the last packet is missing. He only
		// notices that he misses a packet when receiving a packet with a number higher than the last
		// received number + 1.
		if ( SERVER_GetClient ( ulIdx )->bWeaponChangeRequested && ( ( gametic - SERVER_GetClient ( ulIdx )->bLastWeaponChangeRequestTick ) > 2 ) )
			SERVERCOMMANDS_Nothing( ulIdx, true );

		// Don't need to update origin every tic. 
		// [BB] The client decides how often he wants to be updated.
		// The server sends origin and velocity of a 
		// player and the client always knows origin on the next tic.
		if (( gametic % players[ulIdx].userinfo.GetTicsPerUpdate() ) != 0 )
			continue;

		// [BB] You can be watching through the eyes of someone, even if you are not a spectator.
		// If this player is watching through the eyes of another player, send this
		// player some extra info about that player to make for a better watching
		// experience.
		if (( g_aClients[ulIdx].ulDisplayPlayer != ulIdx ) &&
			( g_aClients[ulIdx].ulDisplayPlayer <= MAXPLAYERS ) &&
			( players[g_aClients[ulIdx].ulDisplayPlayer].mo != NULL ))
		{
			SERVERCOMMANDS_UpdatePlayerExtraData( ulIdx, g_aClients[ulIdx].ulDisplayPlayer );
		}

		// See if any players need to be updated to clients.
		// [BB] Only necessary if we are in a level.
		if ( gamestate == GS_LEVEL )
		{
			for ( ULONG ulPlayer = 0; ulPlayer < MAXPLAYERS; ulPlayer++ )
			{
				if ( ( playeringame[ulPlayer] == false ) || players[ulPlayer].bSpectating )
					continue;

				// [BB] The consoleplayer on a client has to be moved differently.
				if ( ulPlayer == ulIdx )
					continue;

				SERVERCOMMANDS_MovePlayer( ulPlayer, ulIdx, SVCF_ONLYTHISCLIENT );
			}
		}

		// Spectators can move around freely, without us telling it what to do (lag-less).
		// [BB] We have to bug all clients who didn't finish receiving their full update with
		// a reliable packet. This way they notice if they didn't get the last packet(s) of
		// the full update even if there is nothing happening on the server that makes us send
		// more reliable packets.
		if ( players[ulIdx].bSpectating || SERVER_GetClient ( ulIdx )->bFullUpdateIncomplete ) 
		{
			// Don't send this to bots.
			if ((( gametic % ( players[ulIdx].userinfo.GetTicsPerUpdate() * TICRATE )) == 0 ) && ( players[ulIdx].bIsBot == false ) ) 
			{
				// Just send them one byte to let them know they're still alive.
				// [BB] Send this as reliable packet to those who didn't get the full update yet (see above)
				SERVERCOMMANDS_Nothing( ulIdx, SERVER_GetClient ( ulIdx )->bFullUpdateIncomplete );
			}

			// [BB] If it's not a spectator, we still have to move the local player.
			if ( players[ulIdx].bSpectating )
				continue;
		}

		SERVERCOMMANDS_MoveLocalPlayer( ulIdx );
	}

	// Once every four seconds, update each player's ping.
	if (( gametic % ( 4 * TICRATE )) == 0 )
	{
		for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
		{
			if ( SERVER_IsValidClient( ulIdx ) == false )
				continue;

			// Tell everyone this player's ping.
			SERVERCOMMANDS_UpdatePlayerPing( ulIdx );

			// Also, update the scoreboard.
			SERVERCONSOLE_UpdatePlayerInfo( ulIdx, UDF_PING );
		}

		// [K6] Also check for afk players
		if ( sv_afk2spec )
		{
			for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ++ulIdx )
			{
				// [BB] Don't kick dead spectators for inactivity.
				if ( ( SERVER_IsValidClient( ulIdx ) == false ) || ( players[ulIdx].bSpectating ) )
					continue;

				const int afkKickTick = ( g_aClients[ulIdx].lLastActionTic + ( sv_afk2spec * MINUTE * TICRATE ));
				if ( afkKickTick <= gametic )
				{
					FString specReason;
					specReason.Format ( "AFK for %d minute%s.", *sv_afk2spec, sv_afk2spec == 1 ? "" : "s" );
					SERVER_ForceToSpectate( ulIdx, specReason.GetChars() );
				}
				// [BB] Warn the player before forcing him to spectate, if that's going to happen in no more than 30 seconds.
				else if ( afkKickTick - 30 * TICRATE <= gametic )
				{
					const int secondsToKick = ( afkKickTick - gametic ) / TICRATE;
					if ( secondsToKick > 1 )
						SERVER_PrintfPlayer( ulIdx, "Warning: In %d seconds you will be forced to spectate due to inactivity!\n", secondsToKick );
				}
			}
		}
	}

	// Once every minute, update the level time.
	if (( gametic % ( 60 * TICRATE )) == 0 )
		SERVERCOMMANDS_SetMapTime( );
}

//*****************************************************************************
//
bool SERVER_IsValidClient( ULONG ulClient )
{
	// Don't transmit data to players not in the game, or bots.
	if (( ulClient >= MAXPLAYERS ) || ( playeringame[ulClient] == false ) || ( players[ulClient].pSkullBot ))
		return ( false );

	return ( true );
}

//*****************************************************************************
//
void SERVER_AdjustPlayersReactiontime( const ULONG ulPlayer )
{
	// [BB] After adjusting the reactiontime on the server, the reactiontime on
	// client and server should approximately end at the same time.
	if ( PLAYER_IsValidPlayerWithMo ( ulPlayer ) && ( players[ulPlayer].bIsBot == false ) )
		players[ulPlayer].mo->reactiontime += ( players[ulPlayer].ulPing * TICRATE / 1000 );
}

//*****************************************************************************
//
void SERVER_DisconnectClient( ULONG ulClient, bool bBroadcast, bool bSaveInfo, LEAVEREASON_e reason )
{
	const CLIENTSTATE_e OldState = g_aClients[ulClient].State;

	// [RK] Disconnectd players need their vote removed/cancelled.
	CALLVOTE_DisconnectedVoter( ulClient );

	// [AK] Clear all the saved chat messages this player said.
	CHAT_ClearChatMessages( ulClient );

	// [AK] Reset this player's custom values to their default values.
	PLAYER_ResetCustomValues( ulClient );

	// [BB] Morphed players need to be unmorphed before disconnecting.
	// [AK] Using MORPH_UNDOBYTIMEOUT ensures this succeeds when they're invulnerable.
	if ( players[ulClient].morphTics )
		P_UndoPlayerMorphWithoutFlash( &players[ulClient], &players[ulClient], MORPH_UNDOBYTIMEOUT, true );

	// If they're disconnecting while carrying an important item like a flag, etc.,
	// make sure they drop it before leaving.
	// [AK] This must be executed before telling the clients the player left.
	if ( players[ulClient].mo != nullptr )
		players[ulClient].mo->DropImportantItems( true );

	if ( bBroadcast )
	{
		// [BB] Only broadcast disconnects if we already announced the connect
		// (which we do when the player is spawned in SERVER_ConnectNewPlayer)
		if ( players[ulClient].userinfo.GetName() && ( SERVER_GetClient( ulClient )->State >= CLS_SPAWNED_BUT_NEEDS_AUTHENTICATION ) )
		{
			FString message;

			if ( ( gametic - g_aClients[ulClient].ulLastCommandTic ) == ( CLIENT_TIMEOUT * 35 ) )
				message.Format( "%s{ip} timed out.\n", players[ulClient].userinfo.GetName() );
			else
				message.Format( "client %s{ip} disconnected.\n", players[ulClient].userinfo.GetName() );

			server_PrintWithIP( message, g_aClients[ulClient].Address );
		}
		else
			Printf( "%s disconnected.\n", g_aClients[ulClient].Address.ToString() );
	}

	// Inform the other clients that this player has been disconnected.
	SERVERCOMMANDS_DisconnectPlayer( ulClient );

	// Potentially back up the player's score in the game, so that if he rejoins, he hasn't
	// lost everything.
	if ( bSaveInfo )
	{
		SavedPlayerInfo info;
		info.address = g_aClients[ulClient].Address;
		info.fragCount = players[ulClient].fragcount;
		info.pointCount = players[ulClient].lPointCount;
		info.winCount = players[ulClient].ulWins;
		info.deathCount = players[ulClient].ulDeathCount;
		info.time = players[ulClient].ulTime; // [RC] Save time
		info.name = players[ulClient].userinfo.GetName( );

		SERVER_SAVE_SaveInfo( info );
	}
	else
	{
		SavedPlayerInfo *info = SERVER_SAVE_GetSavedInfo( players[ulClient].userinfo.GetName( ), g_aClients[ulClient].Address );

		if ( info )
			SERVER_SAVE_ClearInfo( *info );
	}

	// If this player was eligible to get an assist, cancel that.
	TEAM_CancelAssistsOfPlayer ( ulClient );

	// Destroy the actor attached to the player.
	if ( players[ulClient].mo )
	{
		// [BB] Stop all scripts of the player that are still running.
		if ( !( zacompatflags & ZACOMPATF_DONT_STOP_PLAYER_SCRIPTS_ON_DISCONNECT ) )
			FBehavior::StaticStopMyScripts ( players[ulClient].mo );

		players[ulClient].mo->Destroy( );
		players[ulClient].mo = NULL;
	}
	// [BB] Destroy all voodoo dolls of that player.
	COOP_DestroyVoodooDollsOfPlayer ( ulClient );

	// [BB] Clear any cheats the player had. Note: This may not be done before the player dropped the important items!
	players[ulClient].cheats = players[ulClient].cheats2 = 0;

	g_aClients[ulClient].Address.Clear( );
	g_aClients[ulClient].State = CLS_FREE;
	g_aClients[ulClient].ulLastGameTic = 0;
	playeringame[ulClient] = false;

	// Run the disconnect scripts now that the player is leaving.
	// [AK] Only do this if the client is already spawned.
	if (( OldState >= CLS_SPAWNED_BUT_NEEDS_AUTHENTICATION ) && (( players[ulClient].bSpectating == false ) || ( players[ulClient].bDeadSpectator )))
		PLAYER_LeavesGame( ulClient );

	// [SB] Fire event scripts indicating this client disconnected.
	// GAMEEVENT_PLAYERCONNECT is only fired after their state reaches CLS_SPAWNED, so do the same here.
	if ( OldState >= CLS_SPAWNED )
		GAMEMODE_HandleEvent( GAMEEVENT_PLAYERLEAVESSERVER, nullptr, ulClient, reason );

	// Redo the scoreboard.
	SERVERCONSOLE_ReListPlayers( );

	// [RC] Update clients using the RCON utility.
	SERVER_RCON_UpdateInfo( SVRCU_PLAYERDATA );

	// Clear the client's buffers.
	g_aClients[ulClient].PacketBuffer.Clear();
	g_aClients[ulClient].UnreliablePacketBuffer.Clear();
	g_aClients[ulClient].SavedPackets.Clear();

	// Tell the join queue module that a player has left the game.
	JOINQUEUE_PlayerLeftGame( ulClient, true );

	// [BB] Zero out all the player information (like information about being in console).
	PLAYER_ResetPlayerData( &players[ulClient] );

	// If this player was the enemy of another bot, tell the bot.
	for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if (( playeringame[ulIdx] == false ) || ( players[ulIdx].pSkullBot == NULL ))
			continue;

		if ( players[ulIdx].pSkullBot->m_ulPlayerEnemy == ulClient )
			players[ulIdx].pSkullBot->m_ulPlayerEnemy = MAXPLAYERS;
	}

	// If nobody's left on the server, zero out the scores.
	if (( GAMEMODE_GetCurrentFlags() & GMF_PLAYERSONTEAMS ) && ( SERVER_CountPlayers( true ) == 0 ))
	{
		for ( ULONG i = 0; i < teams.Size( ); i++ )
		{
			TEAM_SetPointCount( i, 0, false );
			TEAM_SetFragCount( i, 0, false );
			TEAM_SetDeathCount( i, 0 );
			TEAM_SetWinCount( i, 0, false );
		}

		// Also, potentially restart the map after 5 minutes.
		g_lMapRestartTimer = TICRATE * 60 * 5;

		// If playing Domination reset ownership
		if ( domination )
			DOMINATION_Clear();
	}

	// If no one is left on the server and we're using a cvar lobby map,
	// reset to it after 30 seconds.
	if ( GAMEMODE_IsNextMapCvarLobby( ) &&
		 SERVER_CountPlayers( true ) == 0 )
	{
		g_lMapRestartTimer = TICRATE * 30;
	}
}

//*****************************************************************************
//
void SERVER_SendHeartBeat( void )
{
	ULONG	ulIdx;

	// Ping clients once every second.
	if ( gametic % TICRATE )
		return;

	SERVERCOMMANDS_Ping( I_MSTime( ));

	// Save the time we pinged them at. The client's ping can be calculated
	// subtracting the client's pong from this.
	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
		g_aClients[ulIdx].ulLastGameTic = I_MSTime( );
}

//*****************************************************************************
//
void STACK_ARGS SERVER_Printf( ULONG ulPrintLevel, const char *pszString, ... )
{
	va_list	 argptr;
	va_start( argptr, pszString );
	SERVER_VPrintf( ulPrintLevel, pszString, argptr, MAXPLAYERS );
	va_end( argptr );
}

//*****************************************************************************
//
void STACK_ARGS SERVER_Printf( const char *pszString, ... )
{
	va_list	 argptr;
	va_start( argptr, pszString );
	SERVER_VPrintf( PRINT_HIGH, pszString, argptr, MAXPLAYERS );
	va_end( argptr );
}

//*****************************************************************************
//
void STACK_ARGS SERVER_PrintfPlayer( ULONG ulPrintLevel, ULONG ulPlayer, const char *pszString, ... )
{
	va_list	 argptr;
	va_start( argptr, pszString );
	SERVER_VPrintf( ulPrintLevel, pszString, argptr, ulPlayer );
	va_end( argptr );
}

//*****************************************************************************
//
void STACK_ARGS SERVER_PrintfPlayer( ULONG ulPlayer, const char *pszString, ... )
{
	va_list	 argptr;
	va_start( argptr, pszString );
	SERVER_VPrintf( PRINT_HIGH, pszString, argptr, ulPlayer );
	va_end( argptr );
}

//*****************************************************************************
//
void SERVER_VPrintf( int printlevel, const char* format, va_list argptr, int playerToPrintTo )
{
	char buffer[1024];

#ifdef _MSC_VER
	_vsnprintf( buffer, sizeof buffer - 1, format, argptr );
#else
	vsnprintf( buffer, sizeof buffer - 1, format, argptr );
#endif

	ServerCommandFlags flags = 0;

	if ( playerToPrintTo != MAXPLAYERS )
	{
		flags |= SVCF_ONLYTHISCLIENT;
	}
	else
	{
		// [AK] Make sure we don't print the same message again to the RCON player.
		CONSOLE_ShouldPrintToRCONPlayer( false );

		// Print message locally in console window.
		Printf( "%s", buffer );
	}

	// Send the message out to clients for them to print.
	SERVERCOMMANDS_Print( buffer, printlevel, playerToPrintTo, flags );
}

//*****************************************************************************
//
void SERVER_UpdateSectors( ULONG ulClient )
{
	ULONG							ulIdx;
	sector_t						*pSector;
	FPolyObj						*pPoly;
	TThinkerIterator<DPolyAction>	PolyActionIterator;
	DPolyAction						*pPolyAction;
	TThinkerIterator<DFireFlicker>	FireFlickerIterator;
	DFireFlicker					*pFireFlicker;
	TThinkerIterator<DFlicker>		FlickerIterator;
	DFlicker						*pFlicker;
	TThinkerIterator<DLightFlash>	LightFlashIterator;
	DLightFlash						*pLightFlash;
	TThinkerIterator<DStrobe>		StrobeIterator;
	DStrobe							*pStrobe;
	TThinkerIterator<DGlow>			GlowIterator;
	DGlow							*pGlow;
	TThinkerIterator<DGlow2>		Glow2Iterator;
	DGlow2							*pGlow2;
	TThinkerIterator<DPhased>		PhasedIterator;
	DPhased							*pPhased;

	if ( SERVER_IsValidClient( ulClient ) == false )
		return;

	// [BB] Set all existing sector links.
	for ( ulIdx = 0; ulIdx < g_SectorLinkList.Size( ); ++ulIdx )
		SERVERCOMMANDS_SetSectorLink( g_SectorLinkList[ulIdx].ulSector, g_SectorLinkList[ulIdx].iArg1, g_SectorLinkList[ulIdx].iArg2, g_SectorLinkList[ulIdx].iArg3, ulClient, SVCF_ONLYTHISCLIENT );

	for ( ulIdx = 0; static_cast<signed> (ulIdx) < numsectors; ulIdx++ )
	{
		pSector = &sectors[ulIdx];

		// Check and see if flats need to be updated.
		if ( pSector->bFlatChange )
			SERVERCOMMANDS_SetSectorFlat( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );

		// Update the floor heights.
		if ( pSector->bFloorHeightChange )
			SERVERCOMMANDS_SetSectorFloorPlane( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );

		// Update the ceiling heights.
		if ( pSector->bCeilingHeightChange )
			SERVERCOMMANDS_SetSectorCeilingPlane( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );

		// Update the panning.
		if (( pSector->SavedCeilingXOffset != pSector->GetXOffset(sector_t::ceiling) ) ||
			( pSector->SavedCeilingYOffset != pSector->GetYOffset(sector_t::ceiling,false) ) ||
			( pSector->SavedFloorXOffset != pSector->GetXOffset(sector_t::floor) ) ||
			( pSector->SavedFloorYOffset != pSector->GetYOffset(sector_t::floor,false) ))
		{
			SERVERCOMMANDS_SetSectorPanning( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );
		}

		// Update the sector color.
		if (( pSector->SavedColorMap->Color.r != pSector->ColorMap->Color.r ) ||
			( pSector->SavedColorMap->Color.g != pSector->ColorMap->Color.g ) ||
			( pSector->SavedColorMap->Color.b != pSector->ColorMap->Color.b ) ||
			( pSector->SavedColorMap->Desaturate != pSector->ColorMap->Desaturate ))
		{
			SERVERCOMMANDS_SetSectorColor( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );
		}

		// Update the sector fade.
		if (( pSector->SavedColorMap->Fade.r != pSector->ColorMap->Fade.r ) ||
			( pSector->SavedColorMap->Fade.g != pSector->ColorMap->Fade.g ) ||
			( pSector->SavedColorMap->Fade.b != pSector->ColorMap->Fade.b ))
		{
			SERVERCOMMANDS_SetSectorFade( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );
		}

		// Update the sector's ceiling/floor rotation.
		if (( pSector->SavedCeilingAngle != pSector->GetAngle(sector_t::ceiling,false) ) ||
			( pSector->SavedFloorAngle != pSector->GetAngle(sector_t::floor,false) ))
		{
			SERVERCOMMANDS_SetSectorRotation( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );
		}

		// Update the sector's ceiling/floor scale.
		if (( pSector->SavedCeilingXScale != pSector->GetXScale(sector_t::ceiling) ) ||
			( pSector->SavedCeilingYScale != pSector->GetYScale(sector_t::ceiling) ) ||
			( pSector->SavedFloorXScale != pSector->GetXScale(sector_t::floor) ) ||
			( pSector->SavedFloorYScale != pSector->GetYScale(sector_t::floor) ))
		{
			SERVERCOMMANDS_SetSectorScale( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );
		}

		// Update the sector's friction.
		if (( pSector->SavedFriction != pSector->friction || pSector->SavedMoveFactor != pSector->movefactor ) &&
			( pSector->special & FRICTION_MASK ))
		{
			SERVERCOMMANDS_SetSectorFriction( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );
		}

		// Update the sector's angle/y-offset.
		if (( pSector->SavedBaseCeilingAngle != pSector->planes[sector_t::ceiling].xform.base_angle ) ||
			( pSector->SavedBaseCeilingYOffset != pSector->planes[sector_t::ceiling].xform.base_yoffs ) ||
			( pSector->SavedBaseFloorAngle != pSector->planes[sector_t::floor].xform.base_angle ) ||
			( pSector->SavedBaseFloorYOffset != pSector->planes[sector_t::floor].xform.base_yoffs ))
		{
			SERVERCOMMANDS_SetSectorAngleYOffset( ulIdx );
		}

		// Update the sector's gravity.
		if ( pSector->SavedGravity != pSector->gravity )
			SERVERCOMMANDS_SetSectorGravity( ulIdx );

		// Update the sector's light level.
		if ( pSector->bLightChange )
			SERVERCOMMANDS_SetSectorLightLevel( ulIdx, ulClient, SVCF_ONLYTHISCLIENT );

		// Update the sector's reflection.
		if (( pSector->SavedCeilingReflect != pSector->reflect[sector_t::ceiling] ) ||
			( pSector->SavedFloorReflect != pSector->reflect[sector_t::floor] ))
		{
			SERVERCOMMANDS_SetSectorReflection( ulIdx );
		}

		// Tell client to mark all discovered secret sectors.
		if ((pSector->special & SECRET_MASK) == 0 && pSector->secretsector)
			SERVERCOMMANDS_SecretMarkSectorFound( pSector, ulClient, SVCF_ONLYTHISCLIENT );
	}

	for ( ulIdx = 0; static_cast<signed> (ulIdx) <= po_NumPolyobjs; ulIdx++ )
	{
		pPoly = GetPolyobjByIndex( ulIdx );
		if ( pPoly == NULL )
			continue;

		// Tell client if the position has been changed.
		if ( pPoly->bMoved )
			SERVERCOMMANDS_SetPolyobjPosition( pPoly->tag, ulClient, SVCF_ONLYTHISCLIENT );

		// Tell client if the rotation has been changed.
		if ( pPoly->bRotated )
			SERVERCOMMANDS_SetPolyobjRotation( pPoly->tag, ulClient, SVCF_ONLYTHISCLIENT );
	}

	// [WS] Because poly objects can potentially have more than one thinker,
	// we need to iterate through every thinker and tell the client about it.
	while (( pPolyAction = PolyActionIterator.Next( )) != NULL )
		pPolyAction->UpdateToClient( ulClient );

	// Check for various sector light effects. If we find any, tell the client about them.
	while (( pFireFlicker = FireFlickerIterator.Next( )) != NULL )
		pFireFlicker->UpdateToClient( ulClient );
	while (( pFlicker = FlickerIterator.Next( )) != NULL )
		pFlicker->UpdateToClient( ulClient );
	while (( pLightFlash = LightFlashIterator.Next( )) != NULL )
		pLightFlash->UpdateToClient( ulClient );
	while (( pStrobe = StrobeIterator.Next( )) != NULL )
		pStrobe->UpdateToClient( ulClient );
	while (( pGlow = GlowIterator.Next( )) != NULL )
		pGlow->UpdateToClient( ulClient );
	while (( pGlow2 = Glow2Iterator.Next( )) != NULL )
		pGlow2->UpdateToClient( ulClient );
	while (( pPhased = PhasedIterator.Next( )) != NULL )
		pPhased->UpdateToClient( ulClient );
}

//*****************************************************************************
//
void SERVER_UpdateMovers( ULONG ulClient )
{
	DDoor								*pDoor;
	DPlat								*pPlat;
	DFloor								*pFloor;
	DElevator							*pElevator;
	DWaggleBase							*pWaggle;
	DPillar								*pPillar;
	DCeiling							*pCeiling;
	DScroller							*pScroller;
	TThinkerIterator<DDoor>				DoorIterator;
	TThinkerIterator<DPlat>				PlatIterator;
	TThinkerIterator<DFloor>			FloorIterator;
	TThinkerIterator<DElevator>			ElevatorIterator;
	TThinkerIterator<DWaggleBase>		WaggleIterator;
	TThinkerIterator<DPillar>			PillarIterator;
	TThinkerIterator<DCeiling>			CeilingIterator;
	TThinkerIterator<DScroller>			ScrollerIterator;

	// Tell the client about any active doors.
	while (( pDoor = DoorIterator.Next( )) != NULL )
		pDoor->UpdateToClient( ulClient );

	// Tell the client about any active plats.
	while (( pPlat = PlatIterator.Next( )) != NULL )
		pPlat->UpdateToClient( ulClient );

	// Tell the client about any active floors.
	while (( pFloor = FloorIterator.Next( )) != NULL )
		pFloor->UpdateToClient( ulClient );

	// Tell the client about any active elevators.
	while (( pElevator = ElevatorIterator.Next( )) != NULL )
		pElevator->UpdateToClient( ulClient );

	// Tell the client about any active waggles.
	while (( pWaggle = WaggleIterator.Next( )) != NULL )
		pWaggle->UpdateToClient( ulClient );

	// Tell the client about any active pillars.
	while (( pPillar = PillarIterator.Next( )) != NULL )
		pPillar->UpdateToClient( ulClient );

	// Tell the client about any active ceilings.
	while (( pCeiling = CeilingIterator.Next( )) != NULL )
		pCeiling->UpdateToClient( ulClient );

	// Tell the client about any active scrollers.
	while (( pScroller = ScrollerIterator.Next( )) != NULL )
		pScroller->UpdateToClient( ulClient );

	// [BB] Tell the client about any active pusher.
	DPusher *pPusher = NULL;
	TThinkerIterator<DPusher> PusherIterator;
	while (( pPusher = PusherIterator.Next( )) != NULL )
		pPusher->UpdateToClient( ulClient );
}

//*****************************************************************************
//
void SERVER_UpdateLines( ULONG ulClient )
{
	ULONG		ulLine;

	if ( SERVER_IsValidClient( ulClient ) == false )
		return;

	for ( ulLine = 0; ulLine < (ULONG)numlines; ulLine++ )
	{
		// Have any of the textures changed?
		if ( lines[ulLine].TexChangeFlags )
			SERVERCOMMANDS_SetLineTexture( ulLine, ulClient, SVCF_ONLYTHISCLIENT );

		// [AK] Check if we need to update this line's texture offsets or scale.
		for ( int side = 0; side <= 1; side++ )
		{
			// [AK] Don't update this side if it doesn't exist.
			if ( lines[ulLine].sidedef[side] == NULL )
				continue;

			for ( int position = side_t::top; position <= side_t::bottom; position++ )
			{
				side_t::part *texture = &lines[ulLine].sidedef[side]->textures[position];

				// [AK] If this texture's offsets were changed, send the update.
				if (( texture->xoffset != texture->SavedXOffset ) || ( texture->yoffset != texture->SavedYOffset ))
					SERVERCOMMANDS_SetLineTextureOffset( ulLine, side, position, ulClient, SVCF_ONLYTHISCLIENT );

				// [AK] If this texture's scale was changed, send the update.
				if (( texture->xscale != texture->SavedXScale ) || ( texture->yscale != texture->SavedYScale ))
					SERVERCOMMANDS_SetLineTextureScale( ulLine, side, position, ulClient, SVCF_ONLYTHISCLIENT );
			}
		}

		// Is the alpha of this line altered?
		if ( lines[ulLine].Alpha != lines[ulLine].SavedAlpha )
			SERVERCOMMANDS_SetLineAlpha( ulLine, ulClient, SVCF_ONLYTHISCLIENT );

		// Has the line's blocking status or the ML_ADDTRANS setting changed?
		if ( lines[ulLine].flags != lines[ulLine].SavedFlags )
			SERVERCOMMANDS_SetSomeLineFlags( ulLine, ulClient, SVCF_ONLYTHISCLIENT );
	}
}

//*****************************************************************************
//
void SERVER_UpdateSides( ULONG ulClient )
{
	ULONG		ulSide;

	if ( SERVER_IsValidClient( ulClient ) == false )
		return;

	for ( ulSide = 0; ulSide < (ULONG)numsides; ulSide++ )
	{
		// Have the side's flags changed?
		if ( sides[ulSide].Flags != sides[ulSide].SavedFlags )
			SERVERCOMMANDS_SetSideFlags( ulSide, ulClient, SVCF_ONLYTHISCLIENT );
	}
}

//*****************************************************************************
//
void SERVER_UpdateActorProperties( AActor *pActor, ULONG ulClient )
{
	// [WS] Sanity Check
	if ( !pActor )
		return;

	if ( SERVER_IsValidClient( ulClient ) == false )
		return;

	// Update the actor's speed if it's changed.
	if ( pActor->Speed != pActor->GetDefault( )->Speed )
		SERVERCOMMANDS_SetThingProperty( pActor, APROP_Speed, ulClient, SVCF_ONLYTHISCLIENT  );

	// [BB] Update the actor's RenderStyle if it's changed.
	if ( pActor->RenderStyle.AsDWORD != pActor->GetDefault( )->RenderStyle.AsDWORD )
		SERVERCOMMANDS_SetThingProperty( pActor, APROP_RenderStyle, ulClient, SVCF_ONLYTHISCLIENT  );

	// [BB] Update the actor's alpha if it's changed.
	if ( pActor->alpha != pActor->GetDefault( )->alpha )
		SERVERCOMMANDS_SetThingProperty( pActor, APROP_Alpha, ulClient, SVCF_ONLYTHISCLIENT  );

	// [WS] Update the player's jumpz if it's changed.
	if ( pActor->IsKindOf( RUNTIME_CLASS( APlayerPawn ) )
		&& static_cast<APlayerPawn *>( pActor )->JumpZ != static_cast<APlayerPawn *>( pActor->GetDefault( ) )->JumpZ )
		SERVERCOMMANDS_SetThingProperty( pActor, APROP_JumpZ, ulClient, SVCF_ONLYTHISCLIENT );

	// [AK] Update the actor's species if it's changed.
	if ( strcmp( pActor->Species, pActor->GetDefault()->Species ) != 0 )
		SERVERCOMMANDS_SetThingStringProperty( pActor, APROP_Species, ulClient, SVCF_ONLYTHISCLIENT );

	// [AK] Update the actor's name tag if it's changed.
	if (( pActor->Tag != NULL ) && (( pActor->GetDefault()->Tag == NULL ) || ( pActor->Tag->Compare( *pActor->GetDefault()->Tag ) != 0 )))
		SERVERCOMMANDS_SetThingStringProperty( pActor, APROP_NameTag, ulClient, SVCF_ONLYTHISCLIENT );

	// [BB] Update the actor's gravity if it's changed.
	if ( pActor->gravity != pActor->GetDefault( )->gravity )
		SERVERCOMMANDS_SetThingGravity ( pActor, ulClient, SVCF_ONLYTHISCLIENT );

	// [WS] Update the actor's see sound if it's changed.
	if ( strcmp( S_GetName( pActor->SeeSound ), S_GetName( pActor->GetDefault()->SeeSound ) ) != 0 )
		SERVERCOMMANDS_SetThingSound( pActor, ACTORSOUND_SEESOUND, S_GetName( pActor->SeeSound ), ulClient, SVCF_ONLYTHISCLIENT );

	// [WS] Update the actor's active sound if it's changed.
	if ( strcmp( S_GetName( pActor->ActiveSound ), S_GetName( pActor->GetDefault()->ActiveSound ) ) != 0 )
		SERVERCOMMANDS_SetThingSound( pActor, ACTORSOUND_ACTIVESOUND, S_GetName( pActor->ActiveSound ), ulClient, SVCF_ONLYTHISCLIENT );

	// [WS] Update the actor's attack sound if it's changed.
	if ( strcmp( S_GetName( pActor->AttackSound ), S_GetName( pActor->GetDefault()->AttackSound ) ) != 0 )
		SERVERCOMMANDS_SetThingSound( pActor, ACTORSOUND_ATTACKSOUND, S_GetName( pActor->AttackSound ), ulClient, SVCF_ONLYTHISCLIENT );

	// [WS] Update the actor's pain sound if it's changed.
	if ( strcmp( S_GetName( pActor->PainSound ), S_GetName( pActor->GetDefault()->PainSound ) ) != 0 )
		SERVERCOMMANDS_SetThingSound( pActor, ACTORSOUND_PAINSOUND, S_GetName( pActor->PainSound ), ulClient, SVCF_ONLYTHISCLIENT );

	// [WS] Update the actor's death sound if it's changed.
	if ( strcmp( S_GetName( pActor->DeathSound ), S_GetName( pActor->GetDefault()->DeathSound ) ) != 0 )
		SERVERCOMMANDS_SetThingSound( pActor, ACTORSOUND_DEATHSOUND, S_GetName( pActor->DeathSound ), ulClient, SVCF_ONLYTHISCLIENT );

	// [WS] Update the actor's reaction time if it's changed.
	if ( pActor->reactiontime != pActor->GetDefault()->reactiontime )
		SERVERCOMMANDS_SetThingReactionTime( pActor, ulClient, SVCF_ONLYTHISCLIENT );

	// [EP] Update the actor's scale if it's changed.
	SERVERCOMMANDS_UpdateThingScaleNotAtDefault ( pActor, ulClient, SVCF_ONLYTHISCLIENT );
}

//*****************************************************************************
//
void SERVER_ReconnectNewLevel( const char *pszMapName )
{
	ULONG	ulIdx;

	// Tell clients to reconnect, and that the upcoming map will be pszMapName.
	SERVERCOMMANDS_MapNew( pszMapName );

	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if ( playeringame[ulIdx] == false )
			continue;

		// Get rid of bots.
		if ( players[ulIdx].bIsBot )
		{
			BOTS_RemoveBot( ulIdx, false );
			continue;
		}

		// Make sure the commands we sent out get sent right away.
		SERVER_SendClientPacket( ulIdx, true );

		// Disconnect the client.
		SERVER_DisconnectClient( ulIdx, false, false, LEAVEREASON_RECONNECT );
	}
}

//*****************************************************************************
//
void SERVER_LoadNewLevel( const char *pszMapName )
{
	ULONG		ulIdx;

	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if ( SERVER_IsValidClient( ulIdx ) == false )
			continue;

		// Initialize this client's buffer.
		SERVER_SendClientPacket( ulIdx, true );
	}

	// Tell the client to authenticate his level.
	SERVERCOMMANDS_MapAuthenticate( pszMapName );

	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if ( SERVER_IsValidClient( ulIdx ) == false )
			continue;

		// Send out the packet and clear out the client's buffer.
		SERVER_SendClientPacket( ulIdx, true );
	}

	// [BB] The clients who are authenticated, but still didn't finish loading
	// the map are not covered by the code above and need special treatment.
	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if ( SERVER_GetClient( ulIdx )->State == CLS_AUTHENTICATED )
			SERVER_GetClient( ulIdx )->State = CLS_AUTHENTICATED_BUT_OUTDATED_MAP;
	}
}

//*****************************************************************************
//
void SERVER_KickAllPlayers( const char *pszReason )
{
	for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if ( SERVER_IsValidClient( ulIdx ) )
			SERVER_KickPlayer( ulIdx, pszReason ? pszReason : "No reason given." );
	}
}

//*****************************************************************************
//
void SERVER_KickPlayer( ULONG ulPlayer, const char *pszReason )
{
	ULONG		ulIdx;
	FString		kickString;
	FString		playerName;

	// Make sure the target is valid and applicable.
	if (( ulPlayer >= MAXPLAYERS ) || ( !playeringame[ulPlayer] ))
		return;

	playerName = players[ulPlayer].userinfo.GetName();
	V_RemoveColorCodes( playerName );

	// Build the full kick string.
	// [AK] Don't print this for the current RCON client, though.
	kickString.Format( TEXTCOLOR_ORANGE "%s was kicked from the server! Reason: %s\n", playerName.GetChars(), pszReason );
	CONSOLE_ShouldPrintToRCONPlayer( false );
	Printf( "%s", kickString.GetChars() );

	// Rebuild the string that will be displayed to clients. This time, color codes are allowed.
	kickString.Format( TEXTCOLOR_ORANGE "%s" TEXTCOLOR_ORANGE " was kicked from the server! Reason: %s\n", players[ulPlayer].userinfo.GetName(), pszReason );

	// Send the message out to all clients.
	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if ( SERVER_IsValidClient( ulIdx ) == false )
			continue;

		SERVER_PrintfPlayer( PRINT_HIGH, ulIdx, "%s", kickString.GetChars() );
	}

	// If we're kicking a bot, just remove him.
	if ( players[ulPlayer].bIsBot )
		BOTS_RemoveBot( ulPlayer, true );
	else
	{
		// Tell the player that he's been kicked.
		SERVERCOMMANDS_ConsolePlayerKicked( ulPlayer );
		SERVER_SendClientPacket( ulPlayer, true );
		g_aClients[ulPlayer].SavedPackets.ForceSendAll();
		g_aClients[ulPlayer].SavedPackets.ClearScheduling();

		// Tell the other players that this player has been kicked.
		SERVER_DisconnectClient( ulPlayer, true, false, LEAVEREASON_KICKED );
	}
}

//*****************************************************************************
//
void SERVER_ForceToSpectate( ULONG ulPlayer, const char *pszReason )
{
	// Make sure the target is valid and applicable.
	if ( PLAYER_IsValidPlayer ( ulPlayer ) == false )
	{
		Printf( "No such player!\n" );
		return;
	}

	// Already a spectator! This should not happen.
	if ( PLAYER_IsTrueSpectator( &players[ulPlayer] ))
	{
		Printf( "%s is already a spectator!\n", players[ulPlayer].userinfo.GetName() );
		return;
	}

	SERVER_Printf( PRINT_HIGH, TEXTCOLOR_ORANGE "%s" TEXTCOLOR_ORANGE " has been forced to spectate! Reason: %s\n",
		players[ulPlayer].userinfo.GetName(), pszReason );

	// Make this player a spectator.
	PLAYER_SetSpectator( &players[ulPlayer], true, false );

	// Send the message out to all clients.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVERCOMMANDS_PlayerIsSpectator( ulPlayer );
}

//*****************************************************************************
//
void SERVER_AddCommand( const char *pszCommand )
{
#ifdef NO_SERVER_GUI
	char* command = new char[strlen(pszCommand)+1];
	memcpy(command, pszCommand, strlen(pszCommand)+1);
	AddCommandString( command );
	delete[] command;
#else
	g_ServerCommandQueue.Push( pszCommand );
#endif
}

//*****************************************************************************
//
void SERVER_DeleteCommand( void )
{
#ifndef NO_SERVER_GUI
	AddCommandString( (char *)g_ServerCommandQueue[0].GetChars( ));
	g_ServerCommandQueue.Delete( 0 );
#endif
}

//*****************************************************************************
//
// [CK] Function overhaul to support moving on if all in game players are ready,
// and to handle spectators properly.
bool SERVER_IsEveryoneReadyToGoOn( void )
{
	ULONG	ulIdx;
	ULONG	ulClientCount = 0;
	ULONG	ulSpectatorCountReady = 0;
	bool	bIsOnlySpectators = true;

	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if ( SERVER_IsValidClient( ulIdx ) == false )
			continue;

		ulClientCount++;

		// Need to handle this differently by looking at who is not a
		// spectator with priority.
		// Note that for now, a player who has 0 lives is not considered a
		// spectator and will be treated as an active player.
		if ( PLAYER_IsTrueSpectator( &players[ulIdx] ) == false )
		{
			bIsOnlySpectators = false;

			// If there's a valid player in game who is not ready to go on, we
			// know we can't proceed.
			if (( players[ulIdx].statuses & PLAYERSTATUS_READYTOGOON ) == false )
				return ( false );
		}
		else
		{
			// Else if we're dealing with a spectator, we must handle it in a
			// different way.
			if ( players[ulIdx].statuses & PLAYERSTATUS_READYTOGOON )
				ulSpectatorCountReady++;
		}
	}

	// If there's no clients, we can safely move on.
	if ( ulClientCount == 0 )
		return ( true );

	// If it is all spectators, then our return value is based on if all of them
	// are ready.
	if ( bIsOnlySpectators == true )
		return ( ulClientCount == ulSpectatorCountReady );

	// If we got this far, that means there is actual players in the game and
	// all of them are ready to go on, which means we should.
	return ( true );
}

//*****************************************************************************
//
bool SERVER_IsPlayerVisible( ULONG ulPlayer, ULONG ulPlayer2 )
{
	// Can ulPlayer see ulPlayer2?
	if (( teamlms || lastmanstanding ) &&
		(( lmsspectatorsettings & LMS_SPF_VIEW ) == false ) &&
		( players[ulPlayer].bSpectating ) &&
		( g_aClients[ulPlayer].ulDisplayPlayer == ulPlayer ) &&
		( LASTMANSTANDING_GetState( ) == LMSS_INPROGRESS ))
	{
		return ( false );
	}

	// Passed all checks!
	return ( true );
}

//*****************************************************************************
//
bool SERVER_IsPlayerAllowedToKnowHealth( ULONG ulPlayer, ULONG ulPlayer2 )
{
	// [BB] Sanity check.
	if ( ( ulPlayer >= MAXPLAYERS ) || ( ulPlayer2 >= MAXPLAYERS ) )
		return ( false );

	// No bodies? Definitely not!
	if (( players[ulPlayer].mo == NULL ) || ( players[ulPlayer2].mo == NULL ))
		return ( false );

	// If these players's are not teammates, then disallow knowledge of health.
	// [JS] Unless ZADF_DONT_HIDE_STATS is enabled.
	if (( players[ulPlayer].mo->IsTeammate( players[ulPlayer2].mo ) == false ) && (( zadmflags & ZADF_DONT_HIDE_STATS ) == false ))
		return ( false );

	// Passed all checks!
	return ( true );
}

//*****************************************************************************
// 
// [RC] Is this player ignoring players at this address?
// Returns 0 (if not), -1 (if indefinitely), or the tics until expiration (if temporarily).
// [AK] Updated to return either if this address's chat messages or voice are ignored.
//
LONG SERVER_GetPlayerIgnoreTic( const unsigned int player, NETADDRESS_s address, const bool doVoice )
{
	// Remove all expired entries first.
	SERVER_GetClient( player )->UpdateCommRules( );

	std::list<ClientCommRule> &list = SERVER_GetClient( player )->commRules;

	// Search for entries with this address.
	for ( std::list<ClientCommRule>::iterator i = list.begin( ); i != list.end( ); i++ )
	{
		const bool ignored = doVoice ? i->ignoreVoice : i->ignoreChat;

		if (( i->address.CompareNoPort( address )) && ( ignored ))
		{
			const int unignoredGametic = doVoice ? i->unignoreVoiceGametic : i->unignoreChatGametic;

			if ( unignoredGametic != -1 )
				return unignoredGametic - gametic;
			else
				return -1;
		}
	}

	return 0;
}

//*****************************************************************************
//
LONG SERVER_AdjustDoorDirection( LONG lDirection )
{
	// Not a valid door direction.
	if (( lDirection < -1 ) || ( lDirection > 2 ))
		return ( INT_MIN );

	return ( lDirection + 1 );
}

//*****************************************************************************
//
LONG SERVER_AdjustFloorDirection( LONG lDirection )
{
	// Not a valid floor direction.
	if (( lDirection < -1 ) || ( lDirection > 1 ))
		return ( INT_MIN );

	return ( lDirection + 1 );
}

//*****************************************************************************
//
LONG SERVER_AdjustCeilingDirection( LONG lDirection )
{
	// Not a valid floor direction.
	if (( lDirection < -1 ) || ( lDirection > 1 ))
		return ( INT_MIN );

	return ( lDirection + 1 );
}

//*****************************************************************************
//
LONG SERVER_AdjustElevatorDirection( LONG lDirection )
{
	// Not a valid floor direction.
	if (( lDirection < -1 ) || ( lDirection > 1 ))
		return ( INT_MIN );

	return ( lDirection + 1 );
}

//*****************************************************************************
//
ULONG SERVER_GetMaxPacketSize( void )
{
	return ( g_ulMaxPacketSize );
}

//*****************************************************************************
//
const char *SERVER_GetMapMusic( void )
{
	return g_MapMusic;
}

//*****************************************************************************
//
int SERVER_GetMapMusicOrder( void )
{
	return g_MapMusicOrder;
}

//*****************************************************************************
//
IPList *SERVER_GetAdminList( void )
{
	return &g_AdminIPList;
}

//*****************************************************************************
//
const FString& SERVER_GetMasterBanlistVerificationString( void )
{
	return g_MasterBanlistVerificationString;
}
//*****************************************************************************
//
void SERVER_SetMapMusic( const char *pszMusic, int order )
{
	if ( pszMusic )
		g_MapMusic = pszMusic;
	else
		g_MapMusic = "";

	g_MapMusicOrder = order;
}

//*****************************************************************************
//
void SERVER_ResetInventory( ULONG ulClient, const bool bChangeClientWeapon, bool bGiveReverseOrder )
{
	AInventory	*pInventory;

	if (( ulClient >= MAXPLAYERS ) ||
		( playeringame[ulClient] == false ) ||
		( players[ulClient].mo == NULL ))
	{
		return;
	}

	// First, tell the client to delete his inventory.
	SERVERCOMMANDS_DestroyAllInventory( ulClient, ulClient, SVCF_ONLYTHISCLIENT );

	// [BB] Inform the client about his max health and max armor bonus.
	SERVERCOMMANDS_SetPlayerHealthAndMaxHealthBonus( ulClient, ulClient, SVCF_ONLYTHISCLIENT );
	SERVERCOMMANDS_SetPlayerArmorAndMaxArmorBonus( ulClient, ulClient, SVCF_ONLYTHISCLIENT );

	// Then, go through the client's inventory, and tell the client to give himself
	// each item.
	// [BB] To (mostly) keep the order of the inventory items (important for example when the player has different
	// weapons with the same SelectionOrder value) we need to hand them out in reverse order.
	TArray<AInventory *> inventory;
	// [BB] First but the stuff into a TArray.
	for ( pInventory = players[ulClient].mo->Inventory; pInventory != NULL; pInventory = pInventory->Inventory )
	{
		// [RK] Determine if we want to give the inventory backwards if true or fowards if false.
		if ( bGiveReverseOrder )
			inventory.Push( pInventory );
		else
			inventory.Insert( 0, pInventory );
	}
	// [BB] Then give them in reverse order.
	while ( inventory.Size() )
	{
		inventory.Pop( pInventory );

		if ( pInventory->IsKindOf( RUNTIME_CLASS( AAmmo )))
			continue;

		// [Dusk] Weapon holders need special treatment. While the server manages
		// weapon pieces, holders need to be synced as the client uses it for
		// weapon piece HUD display.
		if ( pInventory->IsKindOf( PClass::FindClass( "WeaponHolder" )) )
		{
			SERVERCOMMANDS_GiveWeaponHolder( ulClient, static_cast<AWeaponHolder *>( pInventory ), ulClient, SVCF_ONLYTHISCLIENT );
			// [BB] This item was taken care of, move to the next.
			continue;
		}

		if ( pInventory->IsKindOf( RUNTIME_CLASS( APowerup )))
		{
			// [BB] All clients need to be informed about some special iventory kinds.
			if ( pInventory->IsKindOf( PClass::FindClass( "PowerTerminatorArtifact" ))
				 || pInventory->IsA( PClass::FindClass( "PowerPossessionArtifact" )) )
				SERVERCOMMANDS_GivePowerup( ulClient, static_cast<APowerup *>( pInventory ) );
			else
				SERVERCOMMANDS_GivePowerup( ulClient, static_cast<APowerup *>( pInventory ), ulClient, SVCF_ONLYTHISCLIENT );

			// [BB] If it's a rune, we need to explicitly set its icon since it was set by the RuneGiver.
			if ( pInventory == pInventory->Owner->Rune )
				SERVERCOMMANDS_SetInventoryIcon( ulClient, pInventory, ulClient, SVCF_ONLYTHISCLIENT );
		}
		else
		{
			// [BB] We want to give things here, but giving an item with an amount of 0
			// will cause the client to destory the item on his side. For unknown reasons
			// some custom weapons only have an amount of zero after a map change
			// (Fist and NewPistol in KDiZD for example) and will be lost on the client
			// side. Raising the amount to one fixes the problem. This is only a workaraound,
			// one should identify the reason for the zero amount.
			if ( pInventory->IsKindOf( RUNTIME_CLASS( AWeapon )) && pInventory->Amount == 0 )
				pInventory->Amount = 1;

			// [BB] All clients need to be informed about some special iventory kinds.
			if ( pInventory->IsKindOf( RUNTIME_CLASS( ATeamItem )) )
				SERVERCOMMANDS_GiveInventory( ulClient, pInventory );
			else
				SERVERCOMMANDS_GiveInventory( ulClient, pInventory, ulClient, SVCF_ONLYTHISCLIENT );
			// [BB] The armor display has to be updated seperately, otherwise
			// the client thinks the armor is green and its amount is equal to 1.
			if ( (pInventory->IsKindOf( RUNTIME_CLASS( AArmor ))) )
				SERVERCOMMANDS_SetPlayerArmor( ulClient );
		}
	}

	for ( pInventory = players[ulClient].mo->Inventory; pInventory != NULL; pInventory = pInventory->Inventory )
	{
		if ( pInventory->IsKindOf( RUNTIME_CLASS( AAmmo )) == false )
			continue;

		SERVERCOMMANDS_GiveInventory( ulClient, pInventory, ulClient, SVCF_ONLYTHISCLIENT );
		// [BB] Clients also need to know the max amount.
		SERVERCOMMANDS_SetPlayerAmmoCapacity( ulClient, pInventory, ulClient, SVCF_ONLYTHISCLIENT );
	}
	// [BB]: After giving back the inventory, inform the player about which weapon he is using.
	// This at least partly fixes the "Using unknown weapon type" bug.
	if ( bChangeClientWeapon )
	{
		// [AK] After clearing the player's inventory, any weapons that we gave back
		// to them could've made them select it unintentionally. This is especially
		// true if they just morphed with NOMORPHLIMITATIONS enabled and they don't
		// have a morph weapon, but they were carrying other weapons before. If we're
		// going to tell them which weapon they're using, clear their weapon first.
		SERVERCOMMANDS_ClearConsoleplayerWeapon( ulClient );
		SERVERCOMMANDS_WeaponChange( ulClient, ulClient, SVCF_ONLYTHISCLIENT );
	}

	// [Dusk] Also inform the client about his Hexen armor values as
	// SERVERCOMMANDS_DestroyAllInventory clears it from the client.
	SERVERCOMMANDS_SyncHexenArmorSlots ( ulClient, ulClient, SVCF_ONLYTHISCLIENT );
}

//*****************************************************************************
//
void SERVER_AddEditedTranslation( ULONG ulTranslation, ULONG ulStart, ULONG ulEnd, ULONG ulPal1, ULONG ulPal2 )
{
	EDITEDTRANSLATION_s	Translation;

	Translation.ulIdx = ulTranslation;
	Translation.ulStart = ulStart;
	Translation.ulEnd = ulEnd;
	Translation.ulPal1 = ulPal1;
	Translation.ulPal2 = ulPal2;
	Translation.ulType = DLevelScript::PCD_TRANSLATIONRANGE1;

	g_EditedTranslationList.Push( Translation );
}

//*****************************************************************************
//
void SERVER_AddEditedTranslation( ULONG ulTranslation, ULONG ulStart, ULONG ulEnd, ULONG ulR1, ULONG ulG1, ULONG ulB1, ULONG ulR2, ULONG ulG2, ULONG ulB2 )
{
	EDITEDTRANSLATION_s	Translation;

	Translation.ulIdx = ulTranslation;
	Translation.ulStart = ulStart;
	Translation.ulEnd = ulEnd;
	Translation.ulR1 = ulR1;
	Translation.ulG1 = ulG1;
	Translation.ulB1 = ulB1;
	Translation.ulR2 = ulR2;
	Translation.ulG2 = ulG2;
	Translation.ulB2 = ulB2;
	Translation.ulType = DLevelScript::PCD_TRANSLATIONRANGE2;

	g_EditedTranslationList.Push( Translation );
}

//*****************************************************************************
//
void SERVER_AddEditedDesaturatedTranslation( ULONG ulTranslation, ULONG ulStart, ULONG ulEnd, float fR1, float fG1, float fB1, float fR2, float fG2, float fB2 )
{
	EDITEDTRANSLATION_s	Translation;

	Translation.ulIdx = ulTranslation;
	Translation.ulStart = ulStart;
	Translation.ulEnd = ulEnd;
	Translation.fR1 = fR1;
	Translation.fG1 = fG1;
	Translation.fB1 = fB1;
	Translation.fR2 = fR2;
	Translation.fG2 = fG2;
	Translation.fB2 = fB2;
	Translation.ulType = DLevelScript::PCD_TRANSLATIONRANGE3;

	g_EditedTranslationList.Push( Translation );
}

//*****************************************************************************
//
void SERVER_RemoveEditedTranslation( ULONG ulTranslation )
{
	const int numEntries = g_EditedTranslationList.Size();

	for ( int i = numEntries; i > 0; --i )
	{
		if ( g_EditedTranslationList[i-1].ulIdx == ulTranslation )
		{
			g_EditedTranslationList.Delete ( i - 1 );
		}
	}
}

//*****************************************************************************
//
bool SERVER_IsTranslationEdited( ULONG ulTranslation )
{
	for ( int i = 0; i < (int)g_EditedTranslationList.Size(); ++i )
	{
		if ( g_EditedTranslationList[i].ulIdx == ulTranslation )
			return true;
	}
	return false;
}

//*****************************************************************************
//
void SERVER_ClearEditedTranslations( void )
{
	g_EditedTranslationList.Clear( );
}

//*****************************************************************************
//
void SERVER_AddSectorLink( ULONG ulSector, int iArg1, int iArg2, int iArg3 )
{
	SECTORLINK_s sectorLink;

	sectorLink.ulSector = ulSector;
	sectorLink.iArg1 = iArg1;
	sectorLink.iArg2 = iArg2;
	sectorLink.iArg3 = iArg3;

	g_SectorLinkList.Push( sectorLink );
}

//*****************************************************************************
//
void SERVER_ClearSectorLinks( void )
{
	g_SectorLinkList.Clear( );
}

//*****************************************************************************
//
void SERVER_UpdateLoopingChannels( AActor *pActor, int channel, FSoundID soundid, float fVolume, float fAttenuation, bool bRemove )
{
	FSoundChan chan = { };

	chan.NextChan = nullptr;
	chan.PrevChan = nullptr;
	chan.Actor = pActor;
	chan.EntChannel = channel & 7;
	chan.ChanFlags = channel & ~7;
	chan.SoundID = soundid;
	chan.Volume = fVolume;
	chan.DistanceScale = fAttenuation;

	for ( unsigned int i = 0; i < g_LoopingChannelList.Size(); i++ )
	{
		if (( g_LoopingChannelList[i].Actor == pActor ) && ( g_LoopingChannelList[i].EntChannel == ( channel & 7 ) ))
		{
			if ( bRemove )
				g_LoopingChannelList.Delete( i );
			else
				g_LoopingChannelList[i] = chan;

			return;
		}
	}

	if ( bRemove == false )
		g_LoopingChannelList.Push( chan );
}

//*****************************************************************************
//
bool SERVER_IsChannelLooping( AActor *pActor, int channel, int soundid )
{
	for ( unsigned int i = 0; i < g_LoopingChannelList.Size(); i++ )
	{
		if (( g_LoopingChannelList[i].Actor == pActor ) &&
			( g_LoopingChannelList[i].EntChannel == channel ) && ( g_LoopingChannelList[i].SoundID == soundid ))
		{
			return true;
		}
	}

	return false;
}

//*****************************************************************************
//
void SERVER_ClearLoopingChannels( AActor *pActor )
{
	unsigned int i = 0;

	if ( pActor != NULL )
	{
		for ( unsigned int i = 0; i < g_LoopingChannelList.Size(); i++ )
		{
			if ( g_LoopingChannelList[i].Actor == pActor )
				g_LoopingChannelList.Delete( i-- );
		}

		return;
	}

	g_LoopingChannelList.Clear( );
}

//*****************************************************************************
//
void SERVER_ErrorCleanup( void )
{
	ULONG	ulIdx;
	char	szString[16];

	// Disconnect all the clients.
	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if ( SERVER_IsValidClient( ulIdx ))
			SERVER_KickPlayer( ulIdx, "Server encountered an error." );
	}

	// [BC] Remove all the bots from this game.
	BOTS_RemoveAllBots( false );

	FString map;
	// Reload the map, [BB] but make sure the current map is valid.
	if ( P_CheckMapData ( level.mapname ) )
		map = level.mapname;
	// [BB] Try the first map of the first episode as fallback.
	else if ( ( AllEpisodes.Size() > 0 ) && P_CheckMapData ( AllEpisodes[0].mEpisodeMap ) )
		map = AllEpisodes[0].mEpisodeMap;
	// [BB] If that still doesn't work try to use CalcMapName.
	else if ( P_CheckMapData ( CalcMapName ( 1, 1 ) ) )
		map = CalcMapName ( 1, 1 ).GetChars();
	// [BB] If we can't find a valid map, we have to error out.
	else
		I_FatalError ( "Can't determine a valid starting map." );
	sprintf( szString, "map %s", map.GetChars() );
	AddCommandString( szString );
}

//*****************************************************************************
//
// [Dusk] If sv_sharekeys is set, this passes all found keys to one player or
// all players.
//
void SERVER_SyncSharedKeys( int playerToSync, bool withmessage )
{
	if ((( zadmflags & ZADF_SHARE_KEYS ) == 0 ) || ( NETWORK_GetState() != NETSTATE_SERVER ))
		return;

	FString keylist;

	for ( unsigned int keyidx = 0; keyidx < g_keysFound.Size(); ++keyidx )
	{
		const PClass* keyclass = NETWORK_GetClassFromIdentification( g_keysFound[keyidx] );

		if ( keyclass == NULL )
		{
			Printf( "SERVER_SyncSharedKeys: shared keys: bad key class index %u! This shouldn't be happening!",
				g_keysFound[keyidx] );
			return;
		}

		if ( withmessage )
		{
			if ( keylist.IsNotEmpty() )
				keylist += ", ";

			// [TP] The actor class name needs to be provided as the default parameter. Otherwise,
			// GetTag will use GetClass(), which returns NULL for the default actor, to get the
			// type name. Providing the type name manually keeps this from happening.
			// (I think that the Class member of the default actor should be set when it is
			// initialized so that this hack isn't necessary, but this is not the case.)
			keylist += GetDefaultByType( keyclass )->GetTag( keyclass->TypeName );
		}

		for ( int player = 0; player < MAXPLAYERS; ++player )
		{
			// [Dusk] If playerToSync is MAXPLAYERS we sync everybody.
			if (( playerToSync != MAXPLAYERS ) && ( player != playerToSync ))
				continue;

			// [Dusk] See if the player should get this key
			if ( PLAYER_IsValidPlayerWithMo( player ) &&
				players[player].bSpectating == false &&
				players[player].mo->FindInventory( keyclass ) == NULL )
			{
				// [Dusk] Try give the key to the player.
				AInventory* newkey;
				if (( newkey = players[player].mo->GiveInventoryType( keyclass )) != NULL )
					SERVERCOMMANDS_GiveInventory( player, newkey );
			}
		}
	}

	// [Dusk] Hack: don't show the "no keys found yet" every time a survival round
	// starts. This is because Zandronum resets dmflags2 for some reason. So only
	// do this if we have some time in our clock.
	if (( withmessage ) && ( level.time > 0 ))
	{
		FString message;

		if ( keylist.IsNotEmpty() )
			message.Format( "Keys found: %s\n", keylist.GetChars() );
		else
			message = "No keys found yet.\n";

		if ( playerToSync != MAXPLAYERS )
			SERVER_PrintfPlayer( playerToSync, "%s", message.GetChars() );
		else
			SERVER_Printf( "%s", message.GetChars() );
	}
}

//*****************************************************************************
//
void SERVER_SyncServerModCVars ( const int PlayerToSync )
{
	FBaseCVar *cvar = CVars;

	while ( cvar )
	{
		if ( ( cvar->GetFlags() & CVAR_MOD ) && ( cvar->GetFlags() & CVAR_SERVERINFO ) )
			SERVERCOMMANDS_SetCVar ( *cvar, PlayerToSync, SVCF_ONLYTHISCLIENT );

		cvar = cvar->GetNext();
	}
}

//*****************************************************************************
//
void SERVER_IgnoreIP( NETADDRESS_s Address )
{
	g_floodProtectionIPQueue.addAddress( Address, g_GameTime / 1000 );
}

//*****************************************************************************
//
void SERVER_KillCheat( const char* what )
{
	if ( stricmp( what, "monsters" ) == 0 )
	{
		const int killcount = P_Massacre ();
		SERVER_Printf( "%d Monster%s Killed\n", killcount, killcount==1 ? "" : "s" );
	}
	else
	{
		// [TP] Handle kill [class] cheats. This is not a generic cheat so we can't just use
		// cht_DoCheat. Instead we build a DEM message and run that. Maybe we can use this
		// technique elsewhere to reduce delta?
		BYTE data[1024];
		BYTE* stream = &data[0];
		WriteString( what, &stream );
		stream = &data[0]; // Reset the bytestream for reading
		C_StartCapture(); // Capture the output so we can print it to clients
		Net_DoCommand( DEM_KILLCLASSCHEAT, &stream, 0 );
		SERVER_Printf( "%s", C_EndCapture() );
	}
}

//*****************************************************************************
//
void STACK_ARGS SERVER_PrintWarning( const char* format, ... )
{
	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
		return;

	if ( sv_showwarnings )
	{
		va_list args;
		va_start( args, format );
		VPrintf( PRINT_HIGH, format, args );
		va_end( args );
	}
}

//*****************************************************************************
//
// [AK] Lists all flag-type CVars belonging to the passed flagset which have
// been changed, then prints them to the console.
//
void SERVER_FlagsetChanged( FIntCVar& flagset, int maxflags )
{
	int value = static_cast<int>( flagset );
	int oldValue = flagset.GetPastValue( );

	// [AK] Only continue if we're the server, and the value actually changed from before.
	if (( NETWORK_GetState( ) != NETSTATE_SERVER ) || ( gamestate == GS_STARTUP ) || ( value == oldValue ))
		return;

	// [AK] Don't do anything with lmsallowedweapons unless we're playing (T)LMS.
	if (( &flagset == &lmsallowedweapons ) && ( lastmanstanding == false ) && ( teamlms == false ))
		return;

	FString result;
	int flagsChanged = 0;

	for ( int i = 0; i < 32; i++ )
	{
		unsigned int bit = ( 1 << i );

		// [AK] Don't continue if this bit hasn't changed.
		if ((( value ^ oldValue ) & bit ) == 0 )
			continue;

		for ( FBaseCVar* cvar = CVars; cvar; cvar = cvar->GetNext( ))
		{
			// [AK] Make sure that this CVar is a flag.
			if ( cvar->IsFlagCVar( ) == false )
				continue;

			FFlagCVar* flagCVar = static_cast<FFlagCVar*>( cvar );

			// [AK] Check if this CVar belongs to the flagset and matches the corresponding bit.
			if (( flagCVar->GetValueVar( ) == &flagset ) && ( flagCVar->GetBitVal( ) == bit ))
			{
				// [AK] Print the name of the CVar and its new value while we still can.
				if ( ++flagsChanged <= maxflags )
				{
					if ( flagsChanged > 1 )
						result += ", ";

					result.AppendFormat( "%s %s", flagCVar->GetName( ), ( value & bit ) ? "ON" : "OFF" );
				}

				break;
			}
		}
	}

	// [AK] If too many flags changed, just print how many flags were changed instead.
	// This way, we don't print an excessively long message to the console.
	if ( flagsChanged > maxflags )
		result.Format( "%d flags changed", flagsChanged );

	if ( result.IsNotEmpty( ))
		SERVER_Printf( "%s changed to: %d (%s)\n", flagset.GetName( ), value, result.GetChars( ));
	else
		SERVER_Printf( "%s changed to: %d\n", flagset.GetName( ), value );

	// [AK] We also need to tell the clients to update the changed flagset.
	if ( &flagset == &lmsspectatorsettings )
		SERVERCOMMANDS_SetLMSSpectatorSettings( );
	else if ( &flagset == &lmsallowedweapons )
		SERVERCOMMANDS_SetLMSAllowedWeapons( );
	else
		SERVERCOMMANDS_SetGameDMFlags( );
}

//*****************************************************************************
//
// [AK] Prints the name of a CVar and its new value to the console, sends an update
// to the clients, and possibly updates the server console's scoreboard.
//
void SERVER_SettingChanged( FBaseCVar &cvar, bool bUpdateConsole, int maxDecimals )
{
	// [AK] Only continue if we're the server.
	if (( NETWORK_GetState( ) != NETSTATE_SERVER ) || ( gamestate == GS_STARTUP ))
		return;

	FString result;

	// [AK] Store the CVar's value into a string so that we can print it.
	switch ( cvar.GetRealType( ))
	{
		case CVAR_Float:
			if ( maxDecimals <= 0 )
				result.Format( "%f", cvar.GetGenericRep( CVAR_Float ).Float );
			else
				result.Format( "%.*f", maxDecimals, cvar.GetGenericRep( CVAR_Float ).Float );
			break;

		case CVAR_String:
			result = cvar.GetGenericRep( CVAR_String ).String;
			break;

		default:
			result.Format( "%d", cvar.GetGenericRep( CVAR_Int ).Int );
			break;
	}

	SERVER_Printf( "%s changed to: %s\n", cvar.GetName( ), result.GetChars( ));
	SERVERCOMMANDS_SetGameModeLimits( );

	// [AK] Update the server console's score if necessary.
	if ( bUpdateConsole )
		SERVERCONSOLE_UpdateScoreboard( );
}

//*****************************************************************************
//*****************************************************************************
//
LONG SERVER_STATISTIC_GetTotalSecondsElapsed( void )
{
	return ( g_lTotalServerSeconds );
}

//*****************************************************************************
//
LONG SERVER_STATISTIC_GetTotalPlayers( void )
{
	return ( g_lTotalNumPlayers );
}

//*****************************************************************************
//
LONG SERVER_STATISTIC_GetMaxNumPlayers( void )
{
	return ( g_lMaxNumPlayers );
}

//*****************************************************************************
//
LONG SERVER_STATISTIC_GetTotalFrags( void )
{
	return ( g_lTotalNumFrags );
}

//*****************************************************************************
//
void SERVER_STATISTIC_AddToTotalFrags( void )
{
	g_lTotalNumFrags++;
}

//*****************************************************************************
//
QWORD SERVER_STATISTIC_GetTotalOutboundDataTransferred( void )
{
	return ( g_qwTotalOutboundDataTransferred );
}

//*****************************************************************************
//
LONG SERVER_STATISTIC_GetPeakOutboundDataTransfer( void )
{
	return ( g_lMaxOutboundDataTransfer );
}

//*****************************************************************************
//
void SERVER_STATISTIC_AddToOutboundDataTransfer( ULONG ulNumBytes )
{
	g_qwTotalOutboundDataTransferred += ulNumBytes;
	g_lCurrentOutboundDataTransfer += ulNumBytes;

	SERVERCONSOLE_UpdateStatistics( );
}

//*****************************************************************************
//
LONG SERVER_STATISTIC_GetCurrentOutboundDataTransfer( void )
{
	return ( g_lOutboundDataTransferLastSecond );
}

//*****************************************************************************
//
QWORD SERVER_STATISTIC_GetTotalInboundDataTransferred( void )
{
	return ( g_qwTotalInboundDataTransferred );
}

//*****************************************************************************
//
LONG SERVER_STATISTIC_GetPeakInboundDataTransfer( void )
{
	return ( g_lMaxInboundDataTransfer );
}

//*****************************************************************************
//
void SERVER_STATISTIC_AddToInboundDataTransfer( ULONG ulNumBytes )
{
	g_qwTotalInboundDataTransferred += ulNumBytes;
	g_lCurrentInboundDataTransfer += ulNumBytes;

	SERVERCONSOLE_UpdateStatistics( );
}

//*****************************************************************************
//
LONG SERVER_STATISTIC_GetCurrentInboundDataTransfer( void )
{
	return ( g_lInboundDataTransferLastSecond );
}

//*****************************************************************************
//
void SERVER_PrintCommand( LONG lCommand )
{
	const char	*pszString;

	if ( lCommand < 0 )
		return;

	if ( lCommand < NUM_CLIENTCONNECT_COMMANDS )
		pszString = GetStringCLCC ( static_cast<CLCC> ( lCommand ) );
	else
	{
		if (( sv_showcommands >= 2 ) && ( lCommand == CLC_CLIENTMOVE || lCommand == CLC_CLIENTMOVEBACKUP ))
			return;
		if (( sv_showcommands >= 3 ) && ( lCommand == CLC_PONG ))
			return;
		if (( sv_showcommands >= 4 ) && ( lCommand == CLC_SPECTATEINFO ))
			return;

		if ( ( lCommand - NUM_CLIENTCONNECT_COMMANDS ) < 0 )
			return;

		pszString = GetStringCLC ( static_cast<CLC> ( lCommand ) );
	}

	Printf( "%s\n", pszString );

	if ( debugfile )
		fprintf( debugfile, "%s\n", pszString );
}

//*****************************************************************************
//*****************************************************************************
//
void SERVER_ParsePacket( BYTESTREAM_s *pByteStream )
{
	bool	bPlayerKicked;
	LONG	lCommand;

	while ( 1 )	 
	{  
		lCommand = pByteStream->ReadByte();

		// End of message.
		if ( lCommand == -1 )
			break;

		// [BB] Option to print commands for debugging purposes.
		if ( sv_showcommands )
			SERVER_PrintCommand( lCommand );

		bPlayerKicked = false;
		switch ( lCommand )
		{
		case CLCC_ATTEMPTCONNECTION:

			// Client is trying to connect to the server, but is disconnected on his end.
			SERVER_SetupNewConnection( pByteStream, false );
			break;
		case CLCC_ATTEMPTAUTHENTICATION:

			// Client is attempting to authenticate his level.
			SERVER_AuthenticateClientLevel( pByteStream );
			break;
		case CLCC_REQUESTSNAPSHOT:

			// Client has gotten a connection from the server, and is sending userinfo.
			SERVER_ConnectNewPlayer( pByteStream );
			break;
		default:

			// [BB] Only authenticated clients are allowed to send the commands handled in
			// SERVER_ProcessCommand(). If we would let non authenticated clients do this,
			// this could be abused to keep non finished connections alive.
			if ( g_aClients[g_lCurrentClient].State < CLS_AUTHENTICATED )
			{
				// [AK] Allow non-authenticated clients to still send CLC_QUIT commands,
				// in case they left while connecting to the server. This way, their slot
				// can be freed so that another client may use it.
				if (( g_aClients[g_lCurrentClient].State > CLS_CHALLENGE ) && ( lCommand == CLC_QUIT ))
				{
					// [SB] But only print the message if sv_printconnectionmessages is enabled.
					if ( sv_printconnectionmessages )
						Printf( "Non-authenticated client (%s) disconnected.\n", NETWORK_GetFromAddress().ToString() );
				}
				else
				{
					// [BB] Under these special, rare circumstances valid clients can send illegal commands.
					// [SB] But only print the message if sv_printconnectionmessages is enabled.
					if ( g_aClients[g_lCurrentClient].State != CLS_AUTHENTICATED_BUT_OUTDATED_MAP && sv_printconnectionmessages )
						Printf( "Illegal command (%ld) from non-authenticated client (%s).\n", lCommand, NETWORK_GetFromAddress().ToString() );

					// [BB] Ignore the rest of the packet, it can't be valid.
					while ( pByteStream->ReadByte() != -1 );
					break;
				}
			}


			// This returns true if the player was kicked as a result.
			if ( SERVER_ProcessCommand( lCommand, pByteStream ))
				return;

			break;
		}
	}
}

//*****************************************************************************
//
bool SERVER_ProcessCommand( LONG lCommand, BYTESTREAM_s *pByteStream )
{
	// [BB] Almost all clients commands are considered to be activity and reset the afk timer.
	switch ( lCommand )
	{
	case CLC_QUIT:
	case CLC_CLIENTMOVE:
	case CLC_MISSINGPACKET:
	case CLC_PONG:
	case CLC_SPECTATE:
	case CLC_SPECTATEINFO:
	case CLC_CHANGEDISPLAYPLAYER:
	case CLC_AUTHENTICATELEVEL:
		break;
	default:
		g_aClients[g_lCurrentClient].lLastActionTic = gametic;
		break;
	}

	switch ( lCommand )
	{
	case CLC_USERINFO:

		// Client is sending us his userinfo.
		// [BB] Don't kick the client while he is still receiving the full update. During that time
		// the client sends the full userinfo, which may be spread over multiple commands.
		SERVER_GetUserInfo( pByteStream, ( SERVER_GetClient ( g_lCurrentClient )->bFullUpdateIncomplete == false ) );
		break;
	case CLC_QUIT:

		// Client has left the game.
		// [AK] Don't broadcast or save info for non-authenticated clients.
		if ( g_aClients[g_lCurrentClient].State < CLS_AUTHENTICATED )
			SERVER_DisconnectClient( g_lCurrentClient, false, false, LEAVEREASON_LEFT );
		else
			SERVER_DisconnectClient( g_lCurrentClient, true, true, LEAVEREASON_LEFT );

		break;
	case CLC_SETSTATUS:
		{
			const int statuses = pByteStream->ReadByte( );

			// [BB] If the client is flooding the server with commands, the client is
			// kicked and we don't need to handle the command.
			if ( server_CheckForClientMinorCommandFlood ( g_lCurrentClient ) == true )
				return ( true );

			const int oldStatuses = players[g_lCurrentClient].statuses;
			const int mask = PLAYERSTATUS_CHATTING | PLAYERSTATUS_INCONSOLE | PLAYERSTATUS_INMENU;

			// [AK] The only statuses the client needs to sync with the server are the
			// "chatting" and "in console/menu" ones. This is intended to prevent
			// malicious clients from changing the "lagging" or "ready to go" statuses
			// to pretend they're lagging or unready themselves on the intermission
			// screen. Reset these bits to zero, then set them to whatever they sent us.
			players[g_lCurrentClient].statuses &= ~mask;
			players[g_lCurrentClient].statuses |= ( statuses & mask );

			if ( players[g_lCurrentClient].statuses != oldStatuses )
				SERVERCOMMANDS_SetPlayerStatus( g_lCurrentClient, g_lCurrentClient, SVCF_SKIPTHISCLIENT );
		}
		break;
	case CLC_IGNORE:

		// Player whishes to ignore / unignore someone.
		return ( server_Ignore( pByteStream ) );
	case CLC_SAY:

		// Client is talking.
		return ( server_Say( pByteStream ));
	case CLC_CLIENTMOVE:
	case CLC_CLIENTMOVEBACKUP:
		{
			bool	bPlayerKicked;

			// Client is sending movement information.
			bPlayerKicked = server_ClientMove( pByteStream, lCommand == CLC_CLIENTMOVEBACKUP );

			if ( g_aClients[g_lCurrentClient].lLastMoveTick == gametic )
				g_aClients[g_lCurrentClient].lOverMovementLevel++;

			g_aClients[g_lCurrentClient].lLastMoveTick = gametic;
			return ( bPlayerKicked );
		}
		break;
	case CLC_MISSINGPACKET:

		// Client is missing a packet; it's our job to resend it!
		return ( server_MissingPacket( pByteStream ));
	case CLC_PONG:

		// Ping response from client.
		return ( server_UpdateClientPing( pByteStream ));
	case CLC_WEAPONSELECT:
	case CLC_WEAPONSELECTBACKUP:

		// Client has sent a weapon change.
		return ( server_WeaponSelect( pByteStream, lCommand == CLC_WEAPONSELECTBACKUP ));
	case CLC_TAUNT:

		// Client is taunting! Broadcast it to other clients.
		return ( server_Taunt( pByteStream ));
	case CLC_SPECTATE:

		// Client now wishes to spectate.
		return ( server_Spectate( pByteStream ));
	case CLC_REQUESTJOIN:

		// Client wishes to join the game after spectating.
		return ( server_RequestJoin( pByteStream ));
	case CLC_CHANGERCONSTATUS:

		// Client is attempting to gain remote control access to the server.
		return ( server_RequestRCON( pByteStream ));
	case CLC_RCONCOMMAND:

		// Client is sending a remote control command to the server.
		return ( server_RCONCommand( pByteStream ));
	case CLC_SUICIDE:

		// Client wishes to kill himself.
		return ( server_Suicide( pByteStream ));
	case CLC_CHANGETEAM:

		// Client wishes to change his team.
		return ( server_ChangeTeam( pByteStream ));
	case CLC_SPECTATEINFO:

		// Client is sending special spectator information.
		return ( server_SpectateInfo( pByteStream ));
	case CLC_GENERICCHEAT:

		// Client is attempting to use a cheat. Only legal if sv_cheats is enabled.
		return ( server_GenericCheat( pByteStream ));
	case CLC_GIVECHEAT:
	case CLC_TAKECHEAT:

		// Client is attempting to use the give cheat. Only legal if sv_cheats is enabled.
		return ( server_GiveCheat( pByteStream, lCommand == CLC_TAKECHEAT ));
	case CLC_SUMMONCHEAT:
	case CLC_SUMMONFRIENDCHEAT:
	case CLC_SUMMONFOECHEAT:

		// Client is attempting to use a summon cheat. Only legal if sv_cheats is enabled.
		return ( server_SummonCheat( pByteStream, lCommand ));
	case CLC_READYTOGOON:

		// Users can only toggle if they haven't yet, and we must be in intermission.
		if ( gamestate != GS_INTERMISSION || ( players[g_lCurrentClient].statuses & PLAYERSTATUS_READYTOGOON ))
			return ( false );

		// Toggle this player (specator)'s "ready to go on" status.
		// [RC] Now a permanent choice.
		PLAYER_SetStatus( &players[g_lCurrentClient], PLAYERSTATUS_READYTOGOON, true );

		return false;
	case CLC_CHANGEDISPLAYPLAYER:

		// Client is changing the player whose eyes he is looking through.
		return ( server_ChangeDisplayPlayer( pByteStream ));
	case CLC_AUTHENTICATELEVEL:

		// Client is attempting to authenticate his level.
		return ( server_AuthenticateLevel( pByteStream ));
	case CLC_CALLVOTE:

		// Client wishes to call a vote.
		return ( server_CallVote( pByteStream ));
	case CLC_VOTE:

		// [AK] Check if the client wishes to vote "yes" or "no" on the current vote.
		if ( !!pByteStream->ReadByte( ) == true )
			CALLVOTE_VoteYes( g_lCurrentClient );
		else
			CALLVOTE_VoteNo( g_lCurrentClient );
		return ( false );
	case CLC_INVENTORYUSEALL:

		// Client wishes to use all inventory items he has.
		return ( server_InventoryUseAll( pByteStream ));
	case CLC_INVENTORYUSE:

		// Client wishes to use a specfic inventory item he has.
		return ( server_InventoryUse( pByteStream ));
	case CLC_INVENTORYDROP:

		// Client wishes to drop a specfic inventory item he has.
		return ( server_InventoryDrop( pByteStream ));
	case CLC_PUKE:

		// [BB] Client wishes to puke a scipt.
		return ( server_Puke( pByteStream ));
	case CLC_ACSSENDSTRING:

		// [AK] Client sent us a string through an ACS script.
		return ( server_ReceiveACSString( pByteStream ));
	case CLC_MORPHEX:

		// [BB] Client wishes to morph to a certain class.
		return ( server_MorphCheat( pByteStream ));
	case CLC_FULLUPDATE:

		// [BB] The client just confirmed receiving the full update.
		SERVER_GetClient ( g_lCurrentClient )->bFullUpdateIncomplete = false;
		return ( false );
	case CLC_INFOCHEAT:

		// [TP] Client wishes to use the linetarget or info cheat on an actor.
		return ( server_InfoCheat( pByteStream ));

	case CLC_SRP_USER_REQUEST_LOGIN:
	case CLC_SRP_USER_START_AUTHENTICATION:
	case CLC_SRP_USER_PROCESS_CHALLENGE:

		return SERVER_ProcessSRPClientCommand( lCommand, pByteStream );
	case CLC_WARPCHEAT:

		{
			fixed_t x = pByteStream->ReadLong();
			fixed_t y = pByteStream->ReadLong();

			if ( sv_cheats || players[g_lCurrentClient].bSpectating )
			{
				if ( players[g_lCurrentClient].mo )
					P_TeleportMove( players[g_lCurrentClient].mo, x, y, ONFLOORZ, true );
			}
			else
			{
				SERVER_KickPlayer( g_lCurrentClient, "Attempted to cheat with sv_cheats being false!" );
				return ( true );
			}
		}
		break;
	case CLC_KILLCHEAT:

		// [TP] Client wishes to KILL
		{
			FString what = pByteStream->ReadString();

			if ( sv_cheats )
			{
				SERVER_KillCheat( what );
			}
			else
			{
				// [TP] Client screwed up and kills itself instead
				SERVER_KickPlayer( g_lCurrentClient, "Attempted to cheat with sv_cheats being false!" );
				return true;
			}
		}
		break;
	case CLC_SPECIALCHEAT:

		// [TP] Client wishes to execute a special.
		{
			unsigned int special = pByteStream->ReadByte();
			unsigned int argsSent = pByteStream->ReadByte();
			int args[5] = { 0, 0, 0, 0, 0 };

			// [TP] Ensure that the client does not send more than five arguments.
			if ( argsSent > countof( args ))
			{
				for ( unsigned int i = 0; i < argsSent; ++i )
					pByteStream->ReadLong();

				SERVER_KickPlayer( g_lCurrentClient, "Sent an invalid packet." );
				return true;
			}

			for ( unsigned int i = 0; i < argsSent; ++i )
				args[i] = pByteStream->ReadLong();

			if ( sv_cheats )
			{
				if ( PLAYER_IsValidPlayerWithMo( g_lCurrentClient ))
				{
					P_ExecuteSpecial( special, NULL, players[g_lCurrentClient].mo, false,
						args[0], args[1], args[2], args[3], args[4] );
				}
			}
			else
			{
				SERVER_KickPlayer( g_lCurrentClient, "Attempted to cheat with sv_cheats being false!" );
				return true;
			}
		}
		break;

	case CLC_SETWANTHIDEINFO:
		// [TP/AK] Player changes his stance on whether or not he wants his info to be displayed.
		{
			// [TP] If the client is flooding the server with commands, the client is
			// kicked and we don't need to handle the command.
			if ( server_CheckForClientMinorCommandFlood ( g_lCurrentClient ) == true )
				return ( true );

			CLIENT_s *const client = SERVER_GetClient( SERVER_GetCurrentClient());
			const int info = pByteStream->ReadByte();
			const bool value = !!pByteStream->ReadByte();

			if ( info == HIDEINFO_ACCOUNTNAME )
			{
				client->WantHideAccount = value;
				SERVERCOMMANDS_SetPlayerAccountName( g_lCurrentClient );
			}
			else if ( info == HIDEINFO_COUNTRY )
			{
				client->bWantHideCountry = value;

				if ( players[g_lCurrentClient].ulCountryIndex > 0 )
					SERVERCOMMANDS_SetPlayerCountry( g_lCurrentClient );
			}
		}
		return false;

	case CLC_SETVIDEORESOLUTION:
		// [TP] Client informs us of his video resolution.
		{
			// [TP] If the client is flooding the server with commands, the client is
			// kicked and we don't need to handle the command.
			if ( server_CheckForClientMinorCommandFlood ( g_lCurrentClient ) == true )
				return ( true );

			CLIENT_s *client = SERVER_GetClient( SERVER_GetCurrentClient() );
			client->ScreenWidth = pByteStream->ReadShort();
			client->ScreenHeight = pByteStream->ReadShort();
		}
		return false;

	// [TP] Client sets a CVar over RCON.
	case CLC_RCONSETCVAR:
		{
			FString cvarName = pByteStream->ReadString();
			FString cvarValue = pByteStream->ReadString();
			CLIENT_s &client = g_aClients[g_lCurrentClient];

			if ( client.bRCONAccess )
			{
				FBaseCVar *cvar = FindCVar( cvarName, NULL );

				if ( cvar != NULL )
				{
					CONSOLE_SetRCONPlayer( g_lCurrentClient );
					cvar->CmdSet( cvarValue );
					CONSOLE_SetRCONPlayer( MAXPLAYERS );
					Printf( "%s changes %s to \"%s\"\n", client.Address.ToString(),
						cvar->GetName(), cvarValue.GetChars() );
				}
				else
				{
					SERVER_PrintfPlayer( g_lCurrentClient, "No such CVar: %s", cvarName.GetChars() );
				}
			}
			else
			{
				// [TP] Trying to set a CVar while not an RCON admin is considered possible command flooding.
				if ( server_CheckForClientCommandFlood( g_lCurrentClient ))
					return true;
			}
		}
		break;

	case CLC_VOIPAUDIOPACKET:
		{
			const unsigned int frame = pByteStream->ReadLong( );
			const unsigned int length = pByteStream->ReadShort( );
			unsigned char *data = new unsigned char[length];

			pByteStream->ReadBuffer( data, length );

			// [AK] Only send out the VoIP audio packet if the player isn't ignored.
			if ( players[g_lCurrentClient].ignoreVoice.enabled == false )
				SERVERCOMMANDS_PlayerVoIPAudioPacket( g_lCurrentClient, frame, data, length );

			delete[] data;
		}
		break;

	case CLC_SETVOIPCHANNELVOLUME:
		{
			const unsigned int targetPlayer = pByteStream->ReadByte( );
			const float volume = clamp<float>( pByteStream->ReadFloat( ), 0.0f, 2.0f );

			if ( SERVER_IsValidClient( targetPlayer ))
			{
				NETADDRESS_s address = SERVER_GetClient( targetPlayer )->Address;
				std::list<ClientCommRule> &list = SERVER_GetClient( g_lCurrentClient )->commRules;

				for ( std::list<ClientCommRule>::iterator i = list.begin( ); i != list.end( ); i++ )
				{
					if ( i->address.CompareNoPort( address ))
					{
						i->VoIPChannelVolume = volume;

						if ( i->IsObsolete( ))
							list.erase( i );

						return false;
					}
				}

				if ( volume != 1.0f )
				{
					ClientCommRule entry( address );
					entry.VoIPChannelVolume = volume;

					list.push_back( entry );
				}
			}
		}
		break;

	// [SB] Strife conversation stuff
	case CLC_CONVERSATIONREPLY:
		{
			const int selection = pByteStream->ReadLong( );

			auto npc = players[g_lCurrentClient].ConversationNPC;
			if ( npc != nullptr && npc->Conversation != nullptr )
			{
				P_ConversationReply( g_lCurrentClient, npc->Conversation->ThisNodeNum, selection );
			}

			break;
		}

	case CLC_CONVERSATIONCLOSE:
		{
			P_ConversationClose( g_lCurrentClient );

			break;
		}
		
	default:

		Printf( PRINT_HIGH, "SERVER_ParseCommands: Unknown client message: %d\n", static_cast<int> (lCommand) );
        return ( true );
	}

	// Return false if the player was not kicked as a result of processing
	// this command.
	return ( false );
}


//*****************************************************************************
//
// [RC] Finds the first player (or, optionally, bot) with the given name; returns MAXPLAYERS if none were found.
ULONG SERVER_GetPlayerIndexFromName( const char *pszName, bool bIgnoreColors, bool bReturnBots )
{
	FString		playerName;

	for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if ( playeringame[ulIdx] == false )
			continue;

		// Optionally remove the color codes from the player name.
		if ( bIgnoreColors )
		{
			playerName = players[ulIdx].userinfo.GetName( );
			V_RemoveColorCodes( playerName );
		}

		if ( stricmp( bIgnoreColors ? playerName.GetChars( ) : players[ulIdx].userinfo.GetName( ), pszName ) == 0 )
		{
			if ( !players[ulIdx].bIsBot || bReturnBots )
				return ulIdx;
		}
	}

	// None found.
	return MAXPLAYERS;
}

//*****************************************************************************
//
LONG SERVER_GetCurrentClient( void )
{
	return ( g_lCurrentClient );
}

//*****************************************************************************
//*****************************************************************************
//
void SERVER_GiveInventoryToPlayer( const player_t *player, AInventory *pInventory )
{
	if ( (player == NULL) || (player->mo == NULL) || (NETWORK_GetState( ) != NETSTATE_SERVER) )
		return;

	SERVERCOMMANDS_GiveInventoryNotOverwritingAmount( player->mo, pInventory );
}

//*****************************************************************************
// [BB]
void SERVER_HandleWeaponStateJump( ULONG ulPlayer, FState *pState, LONG lPosition )
{
	if ( PLAYER_IsValidPlayer( ulPlayer ) == false )
		return;

	AWeapon *pReadyWeapon = players[ulPlayer].ReadyWeapon;

	if ( pReadyWeapon == NULL )
		return;

	// [BB] First set the weapon sprite of the player according to the jump.
	SERVERCOMMANDS_SetPlayerPSprite( ulPlayer, pState, lPosition );

	// [BB] During the time it takes for the server to instruct the client to do the jump
	// the client possible executes code pointers it shouldn't because the client just ignores
	// the jump when executing A_Jump. Typically this can mess the local ammo prediction done
	// by the client (KF7Soviet from Goldeneye64TCv12.pk3 gives a good example of this). As
	// workaround we resync the clients ammo count every time a jump is done. Not really nice,
	// but should get the job done.
	if ( pReadyWeapon->Ammo1 )
		SERVERCOMMANDS_GiveInventory( ulPlayer, static_cast<AInventory *>( pReadyWeapon->Ammo1 ));
	if ( pReadyWeapon->Ammo2 )
		SERVERCOMMANDS_GiveInventory( ulPlayer, static_cast<AInventory *>( pReadyWeapon->Ammo2 ));
}

//*****************************************************************************
// [BB]
void SERVER_SetThingNonZeroAngleAndVelocity( AActor *pActor )
{
	ULONG ulBits = 0;

	if ( pActor->angle != 0 )
		ulBits |= CM_ANGLE;
	if ( pActor->velx != 0 )
		ulBits |= CM_VELX;
	if ( pActor->vely != 0 )
		ulBits |= CM_VELY;
	if ( pActor->velz != 0 )
		ulBits |= CM_VELZ;

	if ( ulBits )
		SERVERCOMMANDS_MoveThingExact( pActor, ulBits );
}

//*****************************************************************************
// [Dusk] Updates a thing's velocity
void SERVER_UpdateThingVelocity( AActor *pActor, bool updateZ, bool updateXY )
{
	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
		return;

	// [BB] Unfortunately there are sync issues, if we don't also update the actual position.
	// Is there a way to fix this without sending the position?
	// [BB] There is no need to sync the player positions as SERVERCOMMANDS_MovePlayer
	// is called regularly. Furthermore, changing the position of a client's local player
	// messes up the player prediction and shouldn't be done.

	ULONG ulBits = updateXY ? (CM_VELX|CM_VELY) : 0;
	if ( updateZ )
		ulBits |= CM_VELZ;

	if ( !pActor->player ) {
		if ( updateXY )
			ulBits |= CM_X|CM_Y;
		if ( updateZ )
			ulBits |= CM_Z;
	}

	SERVERCOMMANDS_MoveThingExact( pActor, ulBits );
}

//*****************************************************************************
//
template <typename CommandType>
static bool server_ParseBufferedCommand ( BYTESTREAM_s *pByteStream )
{
	CommandType *cmd = new CommandType ( pByteStream );
	TArray<ClientCommand*> *buffer = &g_aClients[g_lCurrentClient].MoveCMDs;
	const ULONG ulClientTic = cmd->getClientTic( );
	const bool bIsMoveCMD = cmd->isMoveCmd( );

	// [AK] Ignore commands that are duplicates of commands we already received and/or processed.
	// This way, we won't process the exact same commands multiple times.
	if ( ulClientTic != 0 )
	{
		RingBuffer<ULONG, MAX_RECENT_COMMANDS> *recentCMDs;

		// [AK] Move commands and weapon select commands each have their own ring buffers, since
		// a movement and weapon select commands with the same gametic can co-exist.
		recentCMDs = bIsMoveCMD ? &g_aClients[g_lCurrentClient].recentMoveCMDs : &g_aClients[g_lCurrentClient].recentSelectCMDs;

		for ( unsigned int i = 0; i < MAX_RECENT_COMMANDS; i++ )
		{
			if ( recentCMDs->getOldestEntry( i ) == ulClientTic )
			{
				// [AK] Non-move (i.e. weapon select) commands with the same client gametic but
				// different weapon net ids are not duplicates, so don't delete them.
				if (( bIsMoveCMD ) || ( cmd->getWeaponNetworkIndex( ) == g_aClients[g_lCurrentClient].usLastWeaponNetworkIndex ))
				{
					delete cmd;
					return false;
				}
			}
		}

		// [AK] Save this gametic into the ring buffer for later use.
		recentCMDs->put( ulClientTic );
	}

	// [AK] Save the net id of the weapon sent last if this is a non-move (i.e. weapon select) command.
	if ( bIsMoveCMD == false )
		g_aClients[g_lCurrentClient].usLastWeaponNetworkIndex = cmd->getWeaponNetworkIndex( );

	if ( sv_useticbuffer )
	{
		if ( ulClientTic != 0 )
		{
			// [AK] It's possible that we were extrapolating this player's movement. If that's the case,
			// then we're going to store any incoming commands into a separate buffer until we receive
			// as many of them as we emulated during extrapolation. Once that happens, we'll use them to
			// backtrace the player's actual movement. 
			if (( sv_smoothplayers ) && ( g_aClients[g_lCurrentClient].ulExtrapolatedTics > 0 ))
			{
				ULONG ulNumLateMoveCMDs = 0;

				// [AK] Only count how many movement commands are in the buffer, weapon select command don't matter.
				for ( unsigned int i = 0; i < g_aClients[g_lCurrentClient].LateMoveCMDs.Size( ); i++ )
				{
					if ( g_aClients[g_lCurrentClient].LateMoveCMDs[i]->isMoveCmd( ))
						ulNumLateMoveCMDs++;
				}

				if ( ulNumLateMoveCMDs < g_aClients[g_lCurrentClient].ulExtrapolatedTics )
					buffer = &g_aClients[g_lCurrentClient].LateMoveCMDs;
			}

			for ( unsigned int i = 0; i < buffer->Size( ); i++ )
			{
				ULONG ulBufferClientTic = (*buffer)[i]->getClientTic( );

				// [AK] Reorganize the commands in case they arrived in the wrong order.
				if (( ulBufferClientTic != 0 ) && ( ulClientTic < ulBufferClientTic ))
				{
					buffer->Insert( i, cmd );
					return false;
				}
			}
		}

		buffer->Push( cmd );
		return false;
	}

	const bool retValue = cmd->process ( g_lCurrentClient );
	delete cmd;
	return retValue;
}

//*****************************************************************************
//
CLIENT_PLAYER_DATA_s::CLIENT_PLAYER_DATA_s ( player_t *player )
{
	ulSavedGametic = gametic;
	PositionData = MOVE_THING_DATA_s( player->mo );
	pMorphedPlayerClass = player->MorphedPlayerClass;
	reactionTime = player->mo->reactiontime;
	chickenPeck = player->chickenPeck;
	morphTics = player->morphTics;
	inventoryTics = player->inventorytics;
	jumpTics = player->jumpTics;
	turnTics = player->turnticks;
	crouching = player->crouching;
	crouchDirection = player->crouchdir;
	crouchFactor = player->crouchfactor;
	crouchOffset = player->crouchoffset;
	crouchViewDelta = player->crouchviewdelta;
	bTeleported = false;
}

//*****************************************************************************
//
void CLIENT_PLAYER_DATA_s::Restore ( player_t *player )
{
	// [AK] Set the actor's position. Despite the name of the function, the clients don't execute this
	// function here, but CLIENT_MoveThing adds checks upon calling AActor::SetOrigin that correct the
	// player's floorz value after they've been moved.
	CLIENT_MoveThing( player->mo, PositionData.x, PositionData.y, PositionData.z );

	// [AK] Set the player's velocity, orientation, and other data accordingly.
	player->mo->velx = PositionData.velx;
	player->mo->vely = PositionData.vely;
	player->mo->velz = PositionData.velz;
	player->mo->pitch = PositionData.pitch;
	player->mo->angle = PositionData.angle;
	player->mo->movedir = PositionData.movedir;
	player->mo->reactiontime = reactionTime;
	player->chickenPeck = chickenPeck;
	player->morphTics = morphTics;
	player->inventorytics = inventoryTics;
	player->jumpTics = jumpTics;
	player->turnticks = turnTics;
	player->crouching = crouching;
	player->crouchdir = crouchDirection;
	player->crouchfactor = crouchFactor;
	player->crouchoffset = crouchOffset;
	player->crouchviewdelta = crouchViewDelta;
}

//*****************************************************************************
//
void SERVER_HandleSkipCorrection( ULONG ulClient )
{
	CLIENT_s *pClient = &g_aClients[ulClient];
	ULONG ulNumMoveCMDs = 0;
	ULONG ulNumLateMoveCMDs = 0;
	FString debugMessage;

	// [AK] Don't handle the skip correction if it's supposed to be disabled.
	if ( sv_smoothplayers == 0 )
		return;

	// [AK] Make sure the player has a body before running the skip correction, and only run the skip
	// correction on them once per tic.
	if (( players[ulClient].mo == NULL ) || ( pClient->lLastMoveTickProcess == gametic ))
		return;

	// [AK] Count how many movement commands are inside the client's tic buffer.
	for ( unsigned int i = 0; i < pClient->MoveCMDs.Size( ); i++ )
	{
		if ( pClient->MoveCMDs[i]->isMoveCmd( ))
			ulNumMoveCMDs++;
	}

	// [AK] We might already have some late commands but count the ones that are movement commands.
	for ( unsigned int i = 0; i < pClient->LateMoveCMDs.Size( ); i++ )
	{
		if ( pClient->LateMoveCMDs[i]->isMoveCmd( ))
			ulNumLateMoveCMDs++;
	}

	// When a player is experiencing ping spikes or packet loss and we don't have any commands
	// left in their buffer, we will try to predict where they will be for at least the next few tics
	// until we start receiveing commands from them again. In case the player suffers from a ping spike, we
	// might eventually receive the commands we emulated. When this happens, we will move the player's body
	// to where they were before we started extrapolating, then backtrace their movement by processing all
	// these commands in the same tic.
	// If our prediction was correct, the player should move to about where we extrapolated them, but
	// sometimes we might be wrong. However, the skipping that may result from this discrepancy is usually
	// not nearly as bad than if the player was lagging without any skip correction in place.
	if (( players[ulClient].bSpectating == false ) && ( players[ulClient].playerstate == PST_LIVE ))
	{
		if (( sv_smoothplayers_debuginfo ) && ( pClient->ulExtrapolatedTics > 0 ))
		{
			if (( pClient->MoveCMDs.Size( ) > 0 ) || ( pClient->LateMoveCMDs.Size( ) > 0 ))
			{
				debugMessage.Format( "%d: %s (%d tics extrapolated, %d move commands, %d late commands).\n", gametic, players[ulClient].userinfo.GetName( ),
					static_cast<unsigned int>( pClient->ulExtrapolatedTics ), pClient->MoveCMDs.Size( ), pClient->LateMoveCMDs.Size( ));

				if ( sv_smoothplayers_debuginfo >= 2 )
				{
					if ( pClient->MoveCMDs.Size( ) > 0 )
					{
						debugMessage.AppendFormat( "-> Move gametics: " );
						for ( unsigned int i = 0; i < pClient->MoveCMDs.Size( ); i++ )
							debugMessage.AppendFormat( "%d%s", pClient->MoveCMDs[i]->getClientTic( ), i < pClient->MoveCMDs.Size( ) - 1 ? ", " : "\n" );
					}

					if ( pClient->LateMoveCMDs.Size( ) > 0 )
					{
						debugMessage.AppendFormat( "-> Late gametics: " );
						for ( unsigned int i = 0; i < pClient->LateMoveCMDs.Size( ); i++ )
							debugMessage.AppendFormat( "%d%s", pClient->LateMoveCMDs[i]->getClientTic( ), i < pClient->LateMoveCMDs.Size( ) - 1 ? ", " : "\n" );
					}
				}

				Printf( "%s", debugMessage.GetChars( ));
			}
		}

		// [AK] If we have enough late commands in the buffer, process them all immediately, so as long
		// as they hadn't morphed or unmorphed at any point during extrapolation.
		if (( pClient->OldData != NULL ) && ( pClient->ulExtrapolatedTics > 0 ) && ( pClient->ulExtrapolatedTics == ulNumLateMoveCMDs ))
			server_PerformBacktrace( ulClient, ulNumLateMoveCMDs );

		// [AK] If there are no movement commands left in the client's tic buffer then we'll keep processing
		// the last movement command we received from them, but we won't extrapolate more than we should.
		if (( ulNumMoveCMDs == 0 ) && ( pClient->LastMoveCMD != NULL ) && ( pClient->ulExtrapolatedTics < static_cast<ULONG>( sv_smoothplayers )))
		{
			// [AK] Save the player's current position, velocity, and orientation before we start extrapolating.
			if ( pClient->ulExtrapolatedTics++ == 0 )
			{
				if ( sv_smoothplayers_debuginfo )
				{
					Printf( "%d: starting extrapolation for %s (last gametic = %d).\n", gametic, players[ulClient].userinfo.GetName( ),
						pClient->LastMoveCMD->getClientTic( ));
				}

				pClient->OldData = new CLIENT_PLAYER_DATA_s( &players[ulClient] );
			}

			pClient->LastMoveCMD->process( ulClient );
		}
	}
	else if ( pClient->ulExtrapolatedTics > 0 )
	{
		// [AK] Reset the client's extrapolation data whenever they die or spectate.
		SERVER_ResetClientExtrapolation( ulClient );

		if ( sv_smoothplayers_debuginfo )
		{
			debugMessage.Format( "%d: aborting extrapolation of %s (", gametic, players[ulClient].userinfo.GetName( ));

			if ( players[ulClient].bSpectating )
				debugMessage.AppendFormat( "left game" );
			else
				debugMessage.AppendFormat( "died" );

			Printf( "%s).\n", debugMessage.GetChars( ));
		}
	}
}

//*****************************************************************************
//
bool SERVER_IsExtrapolatingPlayer( ULONG ulClient )
{
	// [AK] Only the server is allowed to extrapolate players.
	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
		return false;

	return (( SERVER_IsValidClient( ulClient )) && ( g_aClients[ulClient].ulExtrapolatedTics > 0 ));
}

//*****************************************************************************
//
bool SERVER_IsBacktracingPlayer( ULONG ulClient )
{
	// [AK] Only the server is allowed to backtrace players.
	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
		return false;

	return (( SERVER_IsValidClient( ulClient )) && ( g_aClients[ulClient].bIsBacktracing ));
}

//*****************************************************************************
//
void SERVER_ResetClientTicBuffer( ULONG ulClient )
{
	if ( SERVER_IsValidClient( ulClient ) == false )
		return;

	// [AK] Clear all stored commands in the tic buffer.
	for ( unsigned int i = 0; i < g_aClients[ulClient].MoveCMDs.Size( ); i++ )
		delete g_aClients[ulClient].MoveCMDs[i];

	g_aClients[ulClient].MoveCMDs.Clear( );

	// [AK] Also delete the last processed movement command.
	if ( g_aClients[ulClient].LastMoveCMD != NULL )
	{
		delete g_aClients[ulClient].LastMoveCMD;
		g_aClients[ulClient].LastMoveCMD = NULL;
	}
 
	SERVER_ResetClientExtrapolation( ulClient );
}

//*****************************************************************************
//
void SERVER_ResetClientExtrapolation( ULONG ulClient, bool bAfterBacktrace )
{
	bool bUpdatedLastMoveCMD = false;

	for ( int i = g_aClients[ulClient].LateMoveCMDs.Size( ) - 1; i >= 0; i-- )
	{
		if ( bAfterBacktrace )
		{
			if ( g_aClients[ulClient].LateMoveCMDs[i]->isMoveCmd( ))
			{
				// [AK] In case we didn't perform a backtrace, we still need to update the client's last move command
				// to the last late command we received from them and update their gametic.
				if ( bUpdatedLastMoveCMD == false )
				{
					delete g_aClients[ulClient].LastMoveCMD;
					g_aClients[ulClient].LastMoveCMD = static_cast<ClientMoveCommand *>( g_aClients[ulClient].LateMoveCMDs[i] );

					bUpdatedLastMoveCMD = true;
					continue;
				}
			}
			else
			{
				// [AK] If we still have any late weapon select commands from this client that haven't been processed,
				// move them all to the start of the their tic buffer.
				g_aClients[ulClient].MoveCMDs.Insert( 0, g_aClients[ulClient].LateMoveCMDs[i] );
				continue;
			}
		}

		delete g_aClients[ulClient].LateMoveCMDs[i];
	}

	g_aClients[ulClient].LateMoveCMDs.Clear( );

	if ( g_aClients[ulClient].OldData != NULL )
	{
		delete g_aClients[ulClient].OldData;
		g_aClients[ulClient].OldData = NULL;
	}

	g_aClients[ulClient].ulExtrapolatedTics = 0;
	g_aClients[ulClient].ExtrapolatedSpecials.Clear( );
	g_aClients[ulClient].bIsBacktracing = false;
}

//*****************************************************************************
//
void SERVER_DestroyActorIfClientsidedOnly( AActor *actor )
{
	if (( NETWORK_GetState( ) != NETSTATE_SERVER ) || ( actor == nullptr ))
		return;

	// [AK] This actor is clientsided only, but the server might've only spawned
	// it so that it could tell the clients to spawn it (e.g. the "summon" CCMD
	// or from a serversided ACS script). By this point, the server doesn't need
	// it anymore and it must be destroyed.
	if ( actor->NetworkFlags & NETFL_CLIENTSIDEONLY )
	{
		actor->ClearCounters( );
		actor->Destroy( );
		actor = nullptr;
	}
}

//*****************************************************************************
//
ClientCommRule::ClientCommRule( NETADDRESS_s address ) :
	address( address ),
	ignoreChat( false ),
	ignoreVoice( false ),
	unignoreChatGametic( 0 ),
	unignoreVoiceGametic( 0 ),
	VoIPChannelVolume( 1.0f ) { }

//*****************************************************************************
//
void ClientCommRule::SetIgnore( const bool doVoice, const bool ignore, const int unignoreTick )
{
	if ( doVoice )
	{
		ignoreVoice = ignore;
		unignoreVoiceGametic = unignoreTick;
	}
	else
	{
		ignoreChat = ignore;
		unignoreChatGametic = unignoreTick;
	}
}

//*****************************************************************************
//
bool ClientCommRule::IsObsolete( void ) const
{
	return (( ignoreChat == false ) && ( ignoreVoice == false ) && ( VoIPChannelVolume == 1.0f ));
}

//*****************************************************************************
//
static bool server_Ignore( BYTESTREAM_s *pByteStream )
{
	const unsigned int targetPlayer = pByteStream->ReadByte( );
	const bool ignore = !!pByteStream->ReadBit( );
	const bool doVoice = !!pByteStream->ReadBit( );
	const int ticks = pByteStream->ReadLong( );

	if ( !SERVER_IsValidClient( targetPlayer ))
		return false;

	const int unignoreTick = ( ticks == -1 ) ? ticks : ( gametic + ticks );
	NETADDRESS_s addressToIgnore = SERVER_GetClient( targetPlayer )->Address;
	std::list<ClientCommRule> &list = SERVER_GetClient( g_lCurrentClient )->commRules;

	for ( std::list<ClientCommRule>::iterator i = list.begin( ); i != list.end( ); i++ )
	{
		if ( i->address.CompareNoPort( addressToIgnore ))
		{
			i->SetIgnore( doVoice, ignore, unignoreTick );

			if ( i->IsObsolete( ))
				list.erase( i );

			return false;
		}
	}

	if ( ignore )
	{
		ClientCommRule entry( addressToIgnore );
		entry.SetIgnore( doVoice, true, unignoreTick );

		list.push_back( entry );
	}

	return false;
}

//*****************************************************************************
//
static bool server_CheckForClientCommandFlood( ULONG ulClient )
{
	// [TP] The client isn't flooding if we're not enforcing limits.
	if ( sv_limitcommands == false )
		return ( false );

	// [BB] If a client issues more commands in floodWindowLength seconds than
	// his commandInstances can hold, he is temporarily banned.
	const LONG floodWindowLength = 60;
	if ( g_aClients[ulClient].commandInstances.getOldestEntry() > 0 )
	{
		if ( ( gametic - g_aClients[ulClient].commandInstances.getOldestEntry() ) <= floodWindowLength * TICRATE )
		{
			SERVERBAN_BanPlayer( ulClient, "10min", "Client command flood.", 0 );
			return ( true );
		}
	}
	// [BB] If this is the last command he may do in the given time frame, warn him.
	if ( g_aClients[ulClient].commandInstances.getOldestEntry( 1 ) > 0 )
	{
		if ( ( gametic - g_aClients[ulClient].commandInstances.getOldestEntry( 1 ) ) <= floodWindowLength * TICRATE )
			SERVER_PrintfPlayer( ulClient, "Stop flooding the server with commands or you will be temporarily banned.\n" );
	}
	g_aClients[ulClient].commandInstances.put ( gametic );

	return ( false );
}

//*****************************************************************************
// [BB] Shares quite a bit of code with server_CheckForClientCommandFlood, but
// we probably don't win enough if it's rewritten to cut down the code duplication here.
static bool server_CheckForClientMinorCommandFlood( ULONG ulClient )
{
	// [TP] The client isn't flooding if we're not enforcing limits.
	if ( sv_limitcommands == false )
		return ( false );

	// [BB] If a client issues more commands in floodWindowLength seconds than
	// his commandInstances can hold, he is temporarily banned.
	const LONG floodWindowLength = 10;
	if ( g_aClients[ulClient].minorCommandInstances.getOldestEntry() > 0 )
	{
		if ( ( gametic - g_aClients[ulClient].minorCommandInstances.getOldestEntry() ) <= floodWindowLength * TICRATE )
		{
			SERVERBAN_BanPlayer( ulClient, "10min", "Client command flood.", 0 );
			return ( true );
		}
	}
	g_aClients[ulClient].minorCommandInstances.put ( gametic );

	return ( false );
}

static bool server_CheckForChatFlood( ULONG ulPlayer )
{
	// [TP] The client isn't flooding if we're not enforcing limits.
	if ( sv_limitcommands == false )
		return ( false );

	ULONG ulChatInstance = ( ++g_aClients[ulPlayer].ulLastChatInstance % MAX_CHATINSTANCE_STORAGE );
	g_aClients[ulPlayer].lChatInstances[ulChatInstance] = gametic;

	// Mute the player if this is the...

	// ...second time he has chatted in a 7 tick interval (~1/5 of a second, ~1/5 of a second chat interval).
	if ( ( g_aClients[ulPlayer].lChatInstances[ulChatInstance] ) -
		( g_aClients[ulPlayer].lChatInstances[( ulChatInstance + MAX_CHATINSTANCE_STORAGE - 1 ) % MAX_CHATINSTANCE_STORAGE] )
		<= 7 )
	{
		return true;
	}

	// ..third time he has chatted in a 42 tick interval (~1.5 seconds, ~.75 second chat interval).
	if ( ( g_aClients[ulPlayer].lChatInstances[ulChatInstance] ) -
		( g_aClients[ulPlayer].lChatInstances[( ulChatInstance + MAX_CHATINSTANCE_STORAGE - 2 ) % MAX_CHATINSTANCE_STORAGE] )
		<= 42 )
	{
		return true;
	}

	// ...fourth time he has chatted in a 105 tick interval (~3 seconds, ~1 second interval).
	if ( ( g_aClients[ulPlayer].lChatInstances[ulChatInstance] ) -
		( g_aClients[ulPlayer].lChatInstances[( ulChatInstance + MAX_CHATINSTANCE_STORAGE - 3 ) % MAX_CHATINSTANCE_STORAGE] )
		<= 105 )
	{
		return true;
	}

	return false;
}

//*****************************************************************************
//
static bool server_Say( BYTESTREAM_s *pByteStream )
{
	ULONG ulPlayer = g_lCurrentClient;
	ULONG ulReceiver = MAXPLAYERS;

	// Read in the chat mode (normal, team, etc.)
	ULONG ulChatMode = pByteStream->ReadByte();

	// [AK] If we're sending a private message to a player, get their index number.
	if ( ulChatMode == CHATMODE_PRIVATE_SEND )
		ulReceiver = pByteStream->ReadByte();

	// Read in the chat string.
	const char	*pszChatString = pByteStream->ReadString();

	if ( ulChatMode == CHATMODE_PRIVATE_SEND )
	{
		// [AK] Don't send the message if we disabled private messaging.
		if ( sv_allowprivatechat == PRIVATECHAT_OFF )
		{
			SERVER_PrintfPlayer( ulPlayer, "Private messages have been disabled by the server.\n" );
			return ( false );
		}

		// [AK] Don't let the client send a private message to themselves.
		if ( ulPlayer == ulReceiver )
		{
			SERVER_PrintfPlayer( ulPlayer, "You can't send private messages to yourself.\n" );
			return ( false );
		}

		// [AK] Don't let the client send a private message to invalid players or bots.
		if (( ulReceiver != MAXPLAYERS ) && (( PLAYER_IsValidPlayer( ulReceiver ) == false ) || ( players[ulReceiver].bIsBot )))
		{
			SERVER_PrintfPlayer( ulPlayer, "You can't send a private message to this player.\n" );
			return ( false );
		}

		// [AK] If the client is only allowed to send private messages to teammates, make sure
		// the receiver is a teammate of theirs.
		if ( CHAT_CanSendPrivateMessageTo( ulPlayer, ulReceiver ) == false )
		{
			SERVER_PrintfPlayer( ulPlayer, "You can only send private messages to teammates.\n" );
			return ( false );
		}
	}

	// [BB] If the client is flooding the server with commands, the client is
	// kicked and we don't need to handle the command.
	// Note: Despite the auto muting, this is necessary. Otherwise the client
	// could force the server to countlessly tell him that he is muted.
	if ( server_CheckForClientMinorCommandFlood ( g_lCurrentClient ) == true )
		return ( true );

	// [RC] Are this player's chats ignored?
	if ( players[ulPlayer].ignoreChat.enabled )
		return ( false );

	// Check for chat flooding.
	if ( server_CheckForChatFlood ( ulPlayer ) == true )
	{
		CHAT_IgnorePlayer( ulPlayer, false, 15 * TICRATE, "chatting too much" );
		return ( false );
	}
	// Or, relay the chat message onto clients.
	else
	{
		// [TP] Kick clients who send too long chat messages (the client is expected to restrict
		// the length of the messages by itself).
		if ( strlen ( pszChatString ) > MAX_CHATBUFFER_LENGTH )
		{
			SERVER_KickPlayer( ulPlayer, "Client sent an excessively long chat string" );
			return ( true );
		}

		SERVER_SendChatMessage( ulPlayer, ulChatMode, pszChatString, ulReceiver );
		return ( false );
	}
}

//*****************************************************************************
//
static bool server_ClientMove( BYTESTREAM_s *pByteStream, bool bSentBackup )
{
	// Don't timeout.
	g_aClients[g_lCurrentClient].ulLastCommandTic = gametic;

	// [BB] We don't process the movement command immediately, but store it
	// in a buffer. This way we can limit the amount of movement commands
	// we process for a player in a given tic to prevent the player from
	// seemingly teleporting in case too many movement commands arrive at once.
	if ( !bSentBackup )
		return server_ParseBufferedCommand<ClientMoveCommand>( pByteStream );

	// [AK] If we get to this point, that means the client also wanted to
	// send us backup movement commands. Get how many commands they sent us.
	ULONG ulNumSentCMDs = pByteStream->ReadByte( );
	bool result = false;

	// [AK] Parse all of the movement commands, but also remember if the client
	// needs to be kicked at all or not.
	for ( unsigned int i = 1; i <= ulNumSentCMDs; i++ )
	{
		if ( server_ParseBufferedCommand<ClientMoveCommand>( pByteStream ))
			result = true;
	}

	return result;
}

ClientMoveCommand::ClientMoveCommand ( BYTESTREAM_s *pByteStream )
{
	ticcmd_t *pCmd = &moveCmd.cmd;

	memset(pCmd, 0, sizeof(*pCmd));

	// Read in the client's gametic.
	moveCmd.ulGametic = pByteStream->ReadLong();

	// [CK] Read in the client's last and latest known server gametic.
	// [BB] Since the packets possibly arrive in wrong order, we can't
	// do reasonable sanity checks on the tic here. Instead this is done
	// when processing the command.
	moveCmd.ulServerGametic = pByteStream->ReadLong();

	// Read in the information the client is sending us.
	const ULONG ulBits = pByteStream->ReadByte();

	if ( ulBits & CLIENT_UPDATE_YAW )
		pCmd->ucmd.yaw = pByteStream->ReadShort();

	if ( ulBits & CLIENT_UPDATE_PITCH )
		pCmd->ucmd.pitch = pByteStream->ReadShort();

	if ( ulBits & CLIENT_UPDATE_ROLL )
		pCmd->ucmd.roll = pByteStream->ReadShort();

	if ( ulBits & CLIENT_UPDATE_BUTTONS )
		pCmd->ucmd.buttons = ( ulBits & CLIENT_UPDATE_BUTTONS_LONG ) ? pByteStream->ReadLong() : pByteStream->ReadByte();

	if ( ulBits & CLIENT_UPDATE_FORWARDMOVE )
		pCmd->ucmd.forwardmove = pByteStream->ReadShort();

	if ( ulBits & CLIENT_UPDATE_SIDEMOVE )
		pCmd->ucmd.sidemove = pByteStream->ReadShort();

	if ( ulBits & CLIENT_UPDATE_UPMOVE )
		pCmd->ucmd.upmove = pByteStream->ReadShort();

	// Always read in the angle and pitch.
	moveCmd.angle = pByteStream->ReadLong();
	moveCmd.pitch = pByteStream->ReadLong();

	// [BB] Extra scope to create a local variable.
	{
		const SDWORD check = pByteStream->ReadLong();
#ifdef LOG_SUSPICIOUS_CLIENTS
		const bool angleCheckFailed = ( ( pCmd->ucmd.yaw == 0 ) && ( pPlayer->mo->reactiontime == 0 ) && ( pPlayer->playerstate == PST_LIVE ) && ( pPlayer->mo ) && ( clientMoveCmd.angle != pPlayer->mo->angle ) && ( ( g_aClients[g_lCurrentClient].ulClientGameTic + 1 ) == ulGametic ) );
		// [BB] If the received checksum doesn't match the checksum of the received ticcmd,
		// something (e.g. an aimbot) most likely manipulated the ticcmd after it was generated.
		if ( check != NETWORK_Check ( pCmd ) )
		{
			if ( SERVER_GetClient( g_lCurrentClient )->bSuspicious == false )
			{
				Printf ( "Warning: Inconsistency in packet received from client %d (IP: %s, name: %s)\n", g_lCurrentClient, SERVER_GetClient( g_lCurrentClient )->Address.ToString(), players[g_lCurrentClient].userinfo.GetName() );
				SERVER_GetClient( g_lCurrentClient )->bSuspicious = true;
			}
			SERVER_GetClient( g_lCurrentClient )->ulNumConsistencyWarnings++;
		}
#endif
	}
	// [BB] If the client is attacking, he always sends the name of the weapon he's using.
	if ( pCmd->ucmd.buttons & BT_ATTACK )
		moveCmd.usWeaponNetworkIndex = pByteStream->ReadShort();
	else
		moveCmd.usWeaponNetworkIndex = 0;
}

bool ClientMoveCommand::process( const ULONG clientIndex ) const
{
	CLIENT_s *client = SERVER_GetClient( clientIndex );
	player_t *player = &players[clientIndex];
	ticcmd_t *cmd = &player->cmd;
	memcpy( cmd, &moveCmd, sizeof( ticcmd_t ));

	client->ulClientGameTic = moveCmd.ulGametic;
	// Important: We should only accept their update if it's less than or equal
	// to ours. The clients should never send a larger gametic unless they're
	// hacking the data or something has become corrupt.
	// We also will only accept newer gametics to prevent clients from going
	// back in time and attempting to cheat with stale states.
	if (( moveCmd.ulServerGametic <= static_cast<unsigned>( gametic )) && ( static_cast<unsigned>( client->lLastServerGametic ) < moveCmd.ulServerGametic ))
		client->lLastServerGametic = moveCmd.ulServerGametic; // [CK] Use the gametic from what we saw

	// If the client is attacking, he always sends the name of the weapon he's using.
	// [AK] Only do this when we're not extrapolating this player's movement.
	if (( cmd->ucmd.buttons & BT_ATTACK ) && ( SERVER_IsExtrapolatingPlayer( clientIndex ) == false ))
	{
		// If the name of the weapon the client is using doesn't match the name of the
		// weapon we think he's using, do something to rectify the situation.
		// [BB] Only do this if the client is fully spawned and authenticated.
		if (( client->State == CLS_SPAWNED ) && (( player->ReadyWeapon == nullptr ) || ( player->ReadyWeapon->GetClass( )->getActorNetworkIndex( ) != moveCmd.usWeaponNetworkIndex )))
		{
			// [BB] Directly after a map change this workaround seems to do more harm than good,
			// (client and server are possibly changing weapons and one of them is slightly ahead)
			// so don't use it when the level just started. The inventory reset that the server does
			// on the clients after a map change most likely has to do with this slight sync issues.
			// [BB] Do this anyway if the server thinks that the player doesn't have any weapon.
			// [AK] Don't do this if we're processing a move commmand sent by the client before they
			// respawned. Doing so causes their weapon to desync, especially at higher pings.
			if ((( level.maptime > 3 * TICRATE ) || (( client->State == CLS_SPAWNED ) && ( player->ReadyWeapon == nullptr ) && ( player->PendingWeapon == WP_NOCHANGE ))) &&
				( static_cast<int>( moveCmd.ulServerGametic - client->lastRespawnTick ) > 0 ))
			{
				const PClass *type = NETWORK_GetClassFromIdentification( moveCmd.usWeaponNetworkIndex );
				if (( type ) && ( type->IsDescendantOf( RUNTIME_CLASS( AWeapon ))))
				{
					if ( player->mo )
					{
						AInventory *inventory = player->mo->FindInventory( type );
						if ( inventory )
						{
							player->PendingWeapon = static_cast<AWeapon *>( inventory );
							// [BB] Since the client tells us that he is attacking with this weapon,
							// we can assume this to be client selected.
							player->bClientSelectedWeapon = true;

							// Update other spectators with this info.
							SERVERCOMMANDS_SetPlayerPendingWeapon( clientIndex, clientIndex, SVCF_SKIPTHISCLIENT );
						}
//						else if ( g_ulWeaponCheckGracePeriodTicks == 0 )
//						{
//							SERVER_KickPlayer( clientIndex, "Using unowned weapon." );
//							return ( true );
//						}
					}
				}
				else
				{
					if ( moveCmd.usWeaponNetworkIndex == 0 )
					{
						// [BB] For some reason the clients think he has no ready weapon,
						// but the server thinks he has one. Although this should not happen,
						// we make a workaround for this here. Just tell the client to bring
						// up the weapon, the server thinks he is using.
						SERVERCOMMANDS_WeaponChange( clientIndex, clientIndex, SVCF_ONLYTHISCLIENT );
					}
					else
					{
						SERVER_KickPlayer( clientIndex, "Using unknown weapon type." );
						return ( true );
					}
				}
			}
		}
	}

	// [BB] Instead of kicking players that send too many movement commands, we just ignore the excessive commands.
	// Note: The kick code is still there, but isn't triggered anymore since we are reducing lOverMovementLevel here.
	if ( client->lOverMovementLevel >= MAX_OVERMOVEMENT_LEVEL )
	{
		client->lOverMovementLevel = MAX_OVERMOVEMENT_LEVEL - 1;
		return false;
	}

	if ( gamestate == GS_LEVEL )
	{
		if ( player->mo )
		{
			// We already processed a movement command this tic.
			if ( client->lLastMoveTickProcess == gametic )
			{
				// [Leo] We have no choice left but to tick the body now.
				player->mo->Tick( );

				// [EP] Make sure that the server sets the proper player psprite settings before running the psprite-events from this client command.
				P_NewPspriteTick( player );
			}

			// [BB] Ignore the angle and pitch sent by the client if the client isn't authenticated yet.
			// In this case the client still sends these values based on the previous map.
			if ( client->State == CLS_SPAWNED )
			{
				player->mo->pitch = moveCmd.pitch;

				// [HYP] Lock angle if speed is above sr40
				if ( !sv_cheats && ( cmd->ucmd.sidemove > ( sidemove[1] << 8 ) || cmd->ucmd.sidemove < -( sidemove[1] << 8 )))
					cmd->ucmd.yaw = 0;
				else //only update angle if speed is at or below sr40, disregard angle changes for speeds above
					player->mo->angle = moveCmd.angle;
			}

			// Makes sure the pitch is valid (should we kick them if it's not?)
			if ( player->mo->pitch < ( -ANGLE_1 * 90 ))
				player->mo->pitch = -ANGLE_1*90;
			else if ( player->mo->pitch > ( ANGLE_1 * 90 ))
				player->mo->pitch = ( ANGLE_1 * 90 );

			P_PlayerThink( player );

			// P_PlayerThink was called this tic, this is used to tick the body afterwards.
			client->lLastMoveTickProcess = gametic;

			// [BB] We possibly process more than one move of this client per tic,
			// so we have to update oldbuttons (otherwise a door that just started to
			// open will be closed immediately again, looking as if it didn't move at all).
			player->oldbuttons = player->cmd.ucmd.buttons;
		}
	}

	// If the player is doing stuff, then obviously he is no longer chatting.
	// [RC] This actually isn't necessarily true. By using a joystick, a player can both move and chat.
	// I'm not going to change it though, because since they can move, they shouldn't be protected by the llama medal. Also, it'd confuse people.
	// [WS] I agree with Rive's statement above and we need the same treatment for the console status.
	if (( cmd->ucmd.buttons != 0 ) || ( cmd->ucmd.forwardmove != 0 ) || ( cmd->ucmd.sidemove != 0 ) || ( cmd->ucmd.upmove != 0 ))
	{
		// [K6/BB] The client is pressing a button, so not afk.
		client->lLastActionTic = gametic;

		PLAYER_SetStatus( player, PLAYERSTATUS_CHATTING | PLAYERSTATUS_INCONSOLE | PLAYERSTATUS_INMENU, false );
	}

	return ( false );
}

//*****************************************************************************
//
static bool server_MissingPacket( BYTESTREAM_s *pByteStream )
{
	LONG	lPacket;
	LONG	lLastPacket;

	// If this client just requested missing packets, ignore the request.
	if ( gametic <= ( g_aClients[g_lCurrentClient].lLastPacketLossTick + ( TICRATE / 4 )))
	{
		while ( pByteStream->ReadLong() != -1 )
			;

		return ( false );
	}

	// Keep reading in packets until we hit -1.
	lLastPacket = -1;
	while (( lPacket = pByteStream->ReadLong()) != -1 )
	{
		// The missing packet sequence must be sent to us in ascending order. If it's not,
		// the server could potentially go into an infinite loop, or be lagged heavily.
		if (( lPacket <= lLastPacket ) || ( lPacket < 0 ))
		{
			while ( pByteStream->ReadLong() != -1 )
				;

			SERVER_KickPlayer( g_lCurrentClient, "Invalid missing packet request." );
			return ( true );
		}
		lLastPacket = lPacket;

		// Send the packet from the saved packet archive.
		bool found = g_aClients[g_lCurrentClient].SavedPackets.SchedulePacket( lPacket );

		// We could not find the correct packet to resend to the client, so there's nothing we can do to save him.
		if ( found == false )
		{
			SERVER_KickPlayer( g_lCurrentClient, "Too many missed packets." );
			return ( true );
		}
	}

	// [AK] Increment the number of missing packets for this client.
	g_aClients[g_lCurrentClient].numMissingPackets++;

	// Mark this client as having requested missing packets.
	g_aClients[g_lCurrentClient].lLastPacketLossTick = gametic;

	return ( false );
}

//*****************************************************************************
//
static bool server_UpdateClientPing( BYTESTREAM_s *pByteStream )
{
	const unsigned int oldTime = pByteStream->ReadLong( );
	const unsigned int nowTime = I_MSTime( );

	// [BB] This ping information from the client doesn't make sense.
	if ( oldTime > nowTime )
		return false;

	ULONG currentPing = nowTime - oldTime;
	const ULONG ticLength = 1000 / TICRATE;
	player_t *p = &players[g_lCurrentClient];
	// [BB] Lag spike, reset the averaging.
	if ( labs (static_cast<int>(currentPing) - static_cast<int>(p->ulPing)) > 3.5 * ticLength )
	{
		p->ulPing = currentPing;
		p->ulPingAverages = 0;
	}
	else
	{
		p->ulPing = ( p->ulPingAverages * p->ulPing + currentPing ) / ( 1 + p->ulPingAverages );
		// [BB] The most recent ping measurement should always have a noticeable influence on the average ping.
		if ( p->ulPingAverages < 20 )
			p->ulPingAverages++;
	}

	// [AK] Don't let the client timeout.
	g_aClients[g_lCurrentClient].ulLastCommandTic = gametic;

	return ( false );
}

//*****************************************************************************
//
static bool server_WeaponSelect( BYTESTREAM_s *pByteStream, bool bSentBackup )
{
	// [BB] To keep weapon sync when buffering movement commands, the weapon 
	// select commands also need to be stored in the same buffer the keep
	// the proper order of the commands.
	// [AK] Also check if this is supposed to be a backup command.
	if ( bSentBackup )
		return server_ParseBufferedCommand<ClientBackupWeaponSelectCommand> ( pByteStream );
	else
		return server_ParseBufferedCommand<ClientWeaponSelectCommand> ( pByteStream );
}

ClientWeaponSelectCommand::ClientWeaponSelectCommand ( BYTESTREAM_s *pByteStream )
	// Read in the identification of the weapon the player is selecting.
	: usActorNetworkIndex ( pByteStream->ReadShort() ) { }

ClientBackupWeaponSelectCommand::ClientBackupWeaponSelectCommand ( BYTESTREAM_s *pByteStream )
	// [AK] Read in the gametic the client sent us.
	: ClientWeaponSelectCommand( pByteStream ), ulGametic ( pByteStream->ReadLong() ) { }


bool ClientWeaponSelectCommand::process( const ULONG ulClient ) const
{
	const PClass	*pType;
	AInventory		*pInventory;

	// If the player doesn't have a body, break out.
	if ( players[ulClient].mo == NULL )
		return ( false );

	// [AK] CLC_WEAPONSELECTBACKUP commands should all have non-zero client gametics,
	// so in these cases we'll update the gametic the client sent us with this.
	if ( getClientTic( ) != 0 )
		g_aClients[ulClient].ulClientGameTic = getClientTic( );

	// Try to find the class that corresponds to the name of the weapon the client
	// is sending us. If it doesn't exist, or the class isn't a type of weapon, boot
	// them.
	pType = NETWORK_GetClassFromIdentification( usActorNetworkIndex );
	if (( pType == NULL ) ||
		( pType->IsDescendantOf( RUNTIME_CLASS( AWeapon )) == false ))
	{
		SERVER_KickPlayer( ulClient, "Tried to switch to unknown weapon type." );
		return ( true );
	}

	pInventory = players[ulClient].mo->FindInventory( pType );
	if ( pInventory == NULL )
	{
//		SERVER_KickPlayer( ulClient, "Tried to select unowned weapon." );
//		return ( true );
		return ( false );
	}

	// [BB] Morph workaround: If the player is morphed, he can't change his weapon.
	// [Binary] Unless +NOMORPHLIMITATIONS is used
	if ( players[ulClient].morphTics && !( players[ulClient].mo && (players[ulClient].mo->PlayerFlags & PPF_NOMORPHLIMITATIONS) ) )
		return false;

	// [BB] Since the server is not giving the player a weapon while spawning, P_BringUpWeapon doesn't call A_Raise for
	// the player's spawn weapon. If the player doesn't have a weapon right now, assume that he just selects his spawn
	// weapon and call P_BringUpWeapon here.
	const bool bFirstWeaponSelect = ( players[ulClient].PendingWeapon == WP_NOCHANGE ) && ( players[ulClient].ReadyWeapon == NULL );

	// Finally, switch the player's pending weapon.
	players[ulClient].PendingWeapon = static_cast<AWeapon *>( pInventory );
	// [BB] Keep in mind that the client selected a weapon.
	players[ulClient].bClientSelectedWeapon = true;
	SERVER_GetClient ( ulClient )->bWeaponChangeRequested = false;

	// [BB] Tell the other clients about the change. This should fix the spectator bug and the railgun pistol sound bug.
	SERVERCOMMANDS_SetPlayerPendingWeapon( ulClient, ulClient, SVCF_SKIPTHISCLIENT );

	// [BB] Needs to be done after calling SERVERCOMMANDS_SetPlayerPendingWeapon because P_BringUpWeapon clears PendingWeapon to WP_NOCHANGE.
	if ( bFirstWeaponSelect )
	{
		P_BringUpWeapon ( &players[ulClient] );
		P_NewPspriteTick( &players[ulClient] );
	}

	return ( false );
}

//*****************************************************************************
//
static bool server_Taunt( BYTESTREAM_s *pByteStream )
{
	// Don't taunt if we're not in a level!
	if ( gamestate != GS_LEVEL )
		return ( false );

	// Spectating players and dead players cannot taunt.
	if (( players[g_lCurrentClient].bSpectating ) ||
		( players[g_lCurrentClient].health <= 0 ) ||
		( players[g_lCurrentClient].mo == NULL ) ||
		( zacompatflags & ZACOMPATF_DISABLETAUNTS ))
	{
		return ( false );
	}

	// [BB] If the client is flooding the server with commands, the client is
	// kicked and we don't need to handle the command.
	if ( server_CheckForClientMinorCommandFlood ( g_lCurrentClient ) == true )
		return ( true );

	SERVERCOMMANDS_PlayerTaunt( g_lCurrentClient, g_lCurrentClient, SVCF_SKIPTHISCLIENT );

	return ( false );
}

//*****************************************************************************
//
static bool server_Spectate( BYTESTREAM_s *pByteStream )
{
	ULONG	ulIdx;

	// [SB] The spectate and request join commands are now ratelimited
	if ( server_CheckForClientCommandFlood( g_lCurrentClient ) == true )
		return ( true );

	// Already a spectator!
	if ( PLAYER_IsTrueSpectator( &players[g_lCurrentClient] ))
	{
		// [BB] If the client is already a spectator but in the join queue he wants to leave the queue.
		if ( JOINQUEUE_GetPositionInLine ( g_lCurrentClient ) != -1 )
		{
			SERVER_PrintfPlayer( g_lCurrentClient, "You have been removed from the join queue.\n" );
			JOINQUEUE_RemovePlayerFromQueue ( g_lCurrentClient );
		}
		return ( false );
	}

	// Make this player a spectator.
	PLAYER_SetSpectator( &players[g_lCurrentClient], true, false );

	// Tell the other players to mark this player as a spectator.
	SERVERCOMMANDS_PlayerIsSpectator( g_lCurrentClient );

	// Tell this player everyone's weapon.
	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if (( playeringame[ulIdx] == false ) ||
			( players[ulIdx].ReadyWeapon == NULL ))
		{
			continue;
		}

		SERVERCOMMANDS_WeaponChange( ulIdx, g_lCurrentClient, SVCF_ONLYTHISCLIENT );
	}

	return ( false );
}

//*****************************************************************************
//
static bool server_RequestJoin( BYTESTREAM_s *pByteStream )
{
	FString		clientJoinPassword;
	ULONG		ulGametic;

	// [SB] The spectate and request join commands are now ratelimited
	if ( server_CheckForClientCommandFlood( g_lCurrentClient ) == true )
		return ( true );

	// Read in the join password.
	clientJoinPassword = pByteStream->ReadString();

	// [BB/Spleen] Read in the client's gametic.
	ulGametic = pByteStream->ReadLong();

	// Player can't rejoin game if he's not spectating!
	if (( playeringame[g_lCurrentClient] == false ) || ( players[g_lCurrentClient].bSpectating == false ))
		return ( false );

	// Player can't rejoin their LMS/survival game if they are dead.
	if (( GAMEMODE_GetCurrentFlags() & GMF_DEADSPECTATORS ) && ( players[g_lCurrentClient].bDeadSpectator ))
		return ( false );

	// If we're forcing a join password, prevent him from joining if it doesn't match.
	if ( server_CheckJoinPassword( clientJoinPassword ) == false )
		return ( false );

	// [BB] Possibly the player needs to login before joining.
	if ( server_CheckLogin ( g_lCurrentClient ) == false )
		return ( false );

	// If there aren't currently any slots available, just put the person in line.
	if ( GAMEMODE_PreventPlayersFromJoining() )
	{
		JOINQUEUE_AddPlayer( g_lCurrentClient, teams.Size() );
		return ( false );
	}

	// Everything's okay! Go ahead and join!
	PLAYER_SpectatorJoinsGame ( &players[g_lCurrentClient] );

	// [BB/Spleen] Set the client's gametic so that it doesn't think it's lagging.
	g_aClients[g_lCurrentClient].ulClientGameTic = ulGametic;

	// [BB] It's possible that you are watching through the eyes of someone else
	// upon joining. Doesn't hurt to reset it.
	g_aClients[g_lCurrentClient].ulDisplayPlayer = g_lCurrentClient;
	if ( ( GAMEMODE_GetCurrentFlags() & GMF_PLAYERSONTEAMS ) &&
		( !( GAMEMODE_GetCurrentFlags() & GMF_TEAMGAME ) || ( TemporaryTeamStarts.Size( ) == 0 ) ) )
	{
		players[g_lCurrentClient].bOnTeam = true;
		players[g_lCurrentClient].Team = TEAM_ChooseBestTeamForPlayer( );

		// If this player is on a team, tell all the other clients that a team has been selected
		// for him.
		if ( players[g_lCurrentClient].bOnTeam )
			SERVERCOMMANDS_SetPlayerTeam( g_lCurrentClient );
	}

	SERVER_Printf( "%s joined the game.\n", players[g_lCurrentClient].userinfo.GetName() );

	// Update this player's info on the scoreboard.
	SERVERCONSOLE_UpdatePlayerInfo( g_lCurrentClient, UDF_FRAGS );

	return ( false );
}

//*****************************************************************************
//
static bool server_RequestRCON( BYTESTREAM_s *pByteStream )
{
	const char	*pszUserPassword;
	const bool bIsLoggingIn = !!pByteStream->ReadByte(); // [AK]

	// [BB] If the client is flooding the server with commands, the client is
	// kicked and we don't need to handle the command.
	if ( server_CheckForClientCommandFlood ( g_lCurrentClient ) == true )
		return ( true );

	if ( bIsLoggingIn )
	{
		// If the user password matches our PW, and we have a PW set, give him RCON access.
		pszUserPassword = pByteStream->ReadString();

		if ( g_aClients[g_lCurrentClient].bRCONAccess )
		{
			SERVER_PrintfPlayer( g_lCurrentClient, "You already have RCON access.\n" );
			return ( false );
		}

		if (( strlen( sv_rconpassword ) > 0 ) && ( strcmp( sv_rconpassword, pszUserPassword ) == 0 ))
		{
			g_aClients[g_lCurrentClient].bRCONAccess = true;
			SERVER_PrintfPlayer( g_lCurrentClient, "RCON access granted.\n" );
			Printf( "RCON access for %s is granted!\n", players[g_lCurrentClient].userinfo.GetName() );

			SERVERCOMMANDS_RCONAccess( g_lCurrentClient );

			// [AK] After giving the client RCON access, send all non-default
			// server settings to them. The client resets these on their end first.
			for ( FBaseCVar *cvar = CVars; cvar != nullptr; cvar = cvar->GetNext( ))
			{
				// [AK] Ignore flag and mask CVars, use their respective flagsets instead.
				if (( cvar->IsFlagCVar( )) || ( cvar->IsMaskCVar( )))
					continue;

				if (( cvar->IsServerCVar( )) && (( cvar->GetFlags( ) & CVAR_ISDEFAULT ) == false ))
					SERVERCOMMANDS_SetCVar( *cvar, g_lCurrentClient, SVCF_ONLYTHISCLIENT );
			}
		}
		else
		{
			g_aClients[g_lCurrentClient].bRCONAccess = false;
			SERVER_PrintfPlayer( g_lCurrentClient, "Incorrect RCON password.\n" );
			Printf( "Incorrect RCON password attempt from %s.\n", players[g_lCurrentClient].userinfo.GetName() );
		}
	}
	else
	{
		if ( g_aClients[g_lCurrentClient].bRCONAccess == false )
		{
			SERVER_PrintfPlayer( g_lCurrentClient, "You don't have RCON access.\n" );
			return ( false );
		}

		g_aClients[g_lCurrentClient].bRCONAccess = false;
		SERVER_PrintfPlayer( g_lCurrentClient, "You have logged out of RCON.\n" );
		Printf( "%s has logged out of RCON.\n", players[g_lCurrentClient].userinfo.GetName() );

		SERVERCOMMANDS_RCONAccess( g_lCurrentClient );
	}

	return ( false );
}

//*****************************************************************************
//
static bool server_RCONCommand( BYTESTREAM_s *pByteStream )
{
	const char	*pszCommand;

	// Read in the command the user sent us.
	pszCommand = pByteStream->ReadString();

	// If they don't have RCON access, quit.
	if ( !g_aClients[g_lCurrentClient].bRCONAccess )
		return ( false );

	Printf( "-> %s (RCON by %s - %s)\n", pszCommand, players[g_lCurrentClient].userinfo.GetName(), SERVER_GetClient( g_lCurrentClient )->Address.ToString() );

	// Set the RCON player so that output displays on his end.
	CONSOLE_SetRCONPlayer( g_lCurrentClient );

	// Now, execute the player's command.
	C_DoCommand( pszCommand );

	// Turn off displaying output on his end.
	CONSOLE_SetRCONPlayer( MAXPLAYERS );

	return ( false );
}

//*****************************************************************************
//
static bool server_Suicide( BYTESTREAM_s *pByteStream )
{
	// [SB] Suicide client command is now flood checked.
	if ( server_CheckForClientMinorCommandFlood( g_lCurrentClient ) == true )
		return ( true );

	// Spectators cannot suicide.
	if ( players[g_lCurrentClient].bSpectating || playeringame[g_lCurrentClient] == false )
		return ( false );

	// Don't allow suiciding during a duel.
	if ( duel && ( DUEL_GetState( ) == DS_INDUEL ))
		return ( false );

	// If this player has tried to suicide recently, ignore the request.
	// [BB] Don't check this when the player never tried to suicide yet.
	if ( ( sv_limitcommands ) && ( gametic < static_cast<signed> ( g_aClients[g_lCurrentClient].ulLastSuicideTime + ( TICRATE * 10 )) ) && ( g_aClients[g_lCurrentClient].ulLastSuicideTime > 0 ) )
		return ( false );

	// [BB] The server may forbid suiciding completely.
	if ( dmflags2 & DF2_NOSUICIDE )
		return ( false );

	g_aClients[g_lCurrentClient].ulLastSuicideTime = gametic;

	cht_Suicide( &players[g_lCurrentClient] );

	return ( false );
}

//*****************************************************************************
//
static bool server_ChangeTeam( BYTESTREAM_s *pByteStream )
{
	LONG		lDesiredTeam;
	bool		bOnTeam, bAutoSelectTeam = false;
	FString		clientJoinPassword;
	ULONG		ulGametic;

	// [SB] Change team client command is now flood checked.
	if ( server_CheckForClientCommandFlood( g_lCurrentClient ) == true )
		return ( true );

	// Read in the join password.
	clientJoinPassword = pByteStream->ReadString();

	lDesiredTeam = pByteStream->ReadByte();
	if ( playeringame[g_lCurrentClient] == false )
		return ( false );

	// [AK] Read in the client's gametic.
	ulGametic = pByteStream->ReadLong();

	// Not in a level.
	// [BB] Still allow spectators to join the queue (that's what happens if you try to join during intermission).
	if ( ( gamestate != GS_LEVEL ) && ( ( players[g_lCurrentClient].bSpectating == false ) || ( gamestate != GS_INTERMISSION ) ) )
		return ( false );

	// Not a teamgame.
	if ( !( GAMEMODE_GetCurrentFlags() & GMF_PLAYERSONTEAMS ) )
		return ( false );

	// Player can't rejoin their LMS game if they are dead.
	if ( teamlms && ( players[g_lCurrentClient].bDeadSpectator ))
		return ( false );

	// [WS] "No team select" dmflag is set. Ignore this.
	if ( players[g_lCurrentClient].bSpectating && ( dmflags2 & DF2_NO_TEAM_SELECT ) )
		return ( false );

	// "No team change" dmflag is set. Ignore this.
	if ( players[g_lCurrentClient].bOnTeam && ( dmflags2 & DF2_NO_TEAM_SWITCH ))
		return ( false );

	// If this player has tried to change teams recently, ignore the request.
	// [BB] Don't check this when the player never tried to join yet, i.e. when g_aClients[g_lCurrentClient].ulLastChangeTeamTime == 0.
	if (( sv_limitcommands ) && !( ( lastmanstanding || teamlms ) && ( LASTMANSTANDING_GetState( ) == LMSS_COUNTDOWN ) ) && gametic < static_cast<signed> ( g_aClients[g_lCurrentClient].ulLastChangeTeamTime + ( TICRATE * 3 )) && ( g_aClients[g_lCurrentClient].ulLastChangeTeamTime > 0 ) )
		return ( false );

	g_aClients[g_lCurrentClient].ulLastChangeTeamTime = gametic;

	// If the team isn't valid, just pick the best team for the player to be on.
	if ( TEAM_CheckIfValid( lDesiredTeam ) == false )
	{
		lDesiredTeam = TEAM_ChooseBestTeamForPlayer( );
		bAutoSelectTeam = true;
	}

	// If the desired team matches our current team, break out.
	if (( players[g_lCurrentClient].bOnTeam ) && ( lDesiredTeam == static_cast<signed> (players[g_lCurrentClient].Team) ))
	{
		SERVER_PrintfPlayer( g_lCurrentClient, "You are already on the %s team!\n", TEAM_GetName( lDesiredTeam ));
		return ( false );
	}

	// [BB] Don't allow players to switch teams in the middle of a game with limited lives.
	if ( ( players[g_lCurrentClient].bSpectating == false ) && GAMEMODE_AreLivesLimited() && GAMEMODE_IsGameInProgressOrResultSequence() )
	{
		SERVER_PrintfPlayer( g_lCurrentClient, "You cannot switch teams in the middle of a match!\n" );
		return ( false );
	}

	// If we're forcing a join password, prevent him from joining if it doesn't match.
	if ( server_CheckJoinPassword( clientJoinPassword ) == false )
		return ( false );

	// [BB] Possibly the player needs to login before joining.
	if ( server_CheckLogin ( g_lCurrentClient ) == false )
		return ( false );

	// [BB] If this is a spectator and players are not allowed to join at the moment, put him in line.
	if ( PLAYER_IsTrueSpectator ( &players[g_lCurrentClient] ) && GAMEMODE_PreventPlayersFromJoining ( g_lCurrentClient ) )
	{
		// [RC] If the player chose to autoselect his team, postpone that until he actually joins.
		JOINQUEUE_AddPlayer( g_lCurrentClient, bAutoSelectTeam ? teams.Size() : lDesiredTeam );
		return ( false );
	}

	// If this player was eligible to get an assist, cancel that.
	TEAM_CancelAssistsOfPlayer ( static_cast<unsigned>( g_lCurrentClient ) );

	// Don't allow him to "take" flags or skulls with him. If he was carrying any,
	// spawn what he was carrying on the ground.
	players[g_lCurrentClient].mo->DropImportantItems( false );

	// [BB] Morphed players need to be unmorphed before changing teams.
	// [AK] Using MORPH_UNDOBYTIMEOUT ensures this succeeds when they're invulnerable.
	if ( players[g_lCurrentClient].morphTics )
		P_UndoPlayerMorphWithoutFlash( &players[g_lCurrentClient], &players[g_lCurrentClient], MORPH_UNDOBYTIMEOUT, true );

	// Save this. This will determine our message.
	bOnTeam = players[g_lCurrentClient].bOnTeam;

	// Set the new team.
	PLAYER_SetTeam( &players[g_lCurrentClient], lDesiredTeam, true );

	// [AK] Now that they're on the new team, send the client the current health and armor of their new teammates.
	// These commands invoke SERVER_IsPlayerAllowedToKnowHealth, so only their teammates' stats are updated.
	// This isn't necessary if ZADF_DONT_HIDE_STATS is enabled because the stats of all players should already be synced.
	if (( zadmflags & ZADF_DONT_HIDE_STATS ) == false )
	{
		for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
		{
			if ( ulIdx != static_cast<ULONG>( g_lCurrentClient ))
			{
				SERVERCOMMANDS_SetPlayerHealth( ulIdx, g_lCurrentClient, SVCF_ONLYTHISCLIENT );
				SERVERCOMMANDS_SetPlayerArmor( ulIdx, g_lCurrentClient, SVCF_ONLYTHISCLIENT );
			}
		}
	}

	// [AK] Set the client's gametic so that it doesn't think it's lagging.
	g_aClients[g_lCurrentClient].ulClientGameTic = ulGametic;

	// Player was on a team, so tell everyone that he's changing teams.
	if ( bOnTeam )
	{
		SERVER_Printf( "%s defected to the \034%s%s " TEXTCOLOR_NORMAL "team.\n", players[g_lCurrentClient].userinfo.GetName(), TEAM_GetTextColorName( players[g_lCurrentClient].Team ), TEAM_GetName( players[g_lCurrentClient].Team ));
	}
	// Otherwise, tell everyone he's joining a team.
	else
	{
		SERVER_Printf( "%s joined the \034%s%s " TEXTCOLOR_NORMAL "team.\n", players[g_lCurrentClient].userinfo.GetName(), TEAM_GetTextColorName( players[g_lCurrentClient].Team ), TEAM_GetName( players[g_lCurrentClient].Team ));
	}

	if ( players[g_lCurrentClient].mo )
	{
		SERVERCOMMANDS_DestroyThing( players[g_lCurrentClient].mo );

		players[g_lCurrentClient].mo->Destroy( );
		players[g_lCurrentClient].mo = NULL;
	}

	// Now respawn the player at the appropriate spot. Set the player state to
	// PST_REBORNNOINVENTORY so everything (weapons, etc.) is cleared.
	// [BB] If the player was a spectator, we have to set the state to
	// PST_ENTERNOINVENTORY. Otherwise the enter scripts are not executed.
	if ( players[g_lCurrentClient].bSpectating )
	{
		players[g_lCurrentClient].playerstate = PST_ENTERNOINVENTORY;

		// [AK] Reset the player's last move tick to zero so that the server
		// doesn't immediately assume they're missing packets because it doesn't
		// receive their movement commands right away, depending on their ping.
		g_aClients[g_lCurrentClient].lLastMoveTick = 0;
	}
	else
	{
		players[g_lCurrentClient].playerstate = PST_REBORNNOINVENTORY;
	}

	// Also, take away spectator status.
	players[g_lCurrentClient].bSpectating = false;
	players[g_lCurrentClient].bDeadSpectator = false;

	if ( GAMEMODE_GetCurrentFlags() & GMF_TEAMGAME )
		G_TeamgameSpawnPlayer( g_lCurrentClient, players[g_lCurrentClient].Team, true );
	else if ( GAMEMODE_GetCurrentFlags() & GMF_DEATHMATCH )
		G_DeathMatchSpawnPlayer( g_lCurrentClient, true );
	else
		G_CooperativeSpawnPlayer( g_lCurrentClient, true );

	// Tell the join queue that a player "sort of" left the game.
	JOINQUEUE_PlayerLeftGame( g_lCurrentClient, false );

	// Update this player's info on the scoreboard.
	SERVERCONSOLE_UpdatePlayerInfo( g_lCurrentClient, UDF_FRAGS );

	return ( false );
}

//*****************************************************************************
//
static bool server_SpectateInfo( BYTESTREAM_s *pByteStream )
{
	player_t	*pPlayer;
	ULONG		ulGametic;

	pPlayer = &players[g_lCurrentClient];

	// Read in the client's gametic.
	ulGametic = pByteStream->ReadLong();

	// [BB] Only spectators may send spectate info. Otherwise the player would get a free "tick".
	if ( pPlayer->bSpectating == false )
		return ( false );

	if ( gamestate == GS_LEVEL )
	{
		P_PlayerThink( pPlayer );
		if ( pPlayer->mo )
			pPlayer->mo->Tick( );
	}

	// Don't timeout.
	g_aClients[g_lCurrentClient].ulLastCommandTic = gametic;
	g_aClients[g_lCurrentClient].ulClientGameTic = ulGametic;

	return ( false );
}

//*****************************************************************************
//
static bool server_GenericCheat( BYTESTREAM_s *pByteStream )
{
	ULONG	ulCheat;

	// Read in the cheat.
	ulCheat = pByteStream->ReadByte();

	// If it's legal, do the cheat.
	if (( sv_cheats ) ||
		(( ulCheat == CHT_CHASECAM ) &&	((players[g_lCurrentClient].bSpectating) || (dmflags2 & DF2_CHASECAM) ))
		|| ( ( ulCheat == CHT_NOCLIP ) && players[g_lCurrentClient].bSpectating ) )
	{
		cht_DoCheat( &players[g_lCurrentClient], ulCheat );

		// Tell clients about this cheat. [BB] CHT_MORPH is completely server side.
		if ( ulCheat != CHT_MORPH )
		{
			// [BB] You only need to notify the client who wants to use chasecam about it.
			// If you tell it to all clients, it looks weird for a client spying someone with chasecam on.
			if ( ulCheat == CHT_CHASECAM )
				SERVERCOMMANDS_GenericCheat( g_lCurrentClient, ulCheat, g_lCurrentClient, SVCF_ONLYTHISCLIENT );
			else
				SERVERCOMMANDS_GenericCheat( g_lCurrentClient, ulCheat );
		}
	}
	// If not, boot their ass!
	// [BB] Due to timing issues, honest clients may try to use CHT_CHASECAM or CHT_NOCLIP because they
	// think they are still a spectator. Don't kick them for this.
	else if ( ( ulCheat != CHT_CHASECAM ) && ( ulCheat != CHT_NOCLIP ) )
	{
		SERVER_KickPlayer( g_lCurrentClient, "Attempted to cheat with sv_cheats being false!" );
		return ( true );
	}

	return ( false );
}

//*****************************************************************************
//
static bool server_GiveCheat( BYTESTREAM_s *pByteStream, bool take )
{
	const char	*pszItemName;
	ULONG		ulAmount;

	// Read in the item name.
	pszItemName = pByteStream->ReadString();

	// Read in the amount.
	ulAmount = pByteStream->ReadByte();

	// If it's legal, do the cheat.
	if ( sv_cheats )
	{
		( take ? cht_Take : cht_Give )( &players[g_lCurrentClient], pszItemName, ulAmount );
	}
	// If not, boot their ass!
	else
	{
		SERVER_KickPlayer( g_lCurrentClient, "Attempted to cheat with sv_cheats being false!" );
		return ( true );
	}

	return ( false );
}

//*****************************************************************************
//
static bool server_SummonCheat( BYTESTREAM_s *pByteStream, LONG lType )
{
	const char		*pszName;
	AActor			*pSource;
	const PClass	*pType;
	AActor			*pActor;

	// Read in the item name.
	pszName = pByteStream->ReadString();

	const bool bSetAngle = !!pByteStream->ReadByte();
	// [BB] The client only sends the angle, if it is supposed to be set.
	const SHORT sAngle = bSetAngle ? pByteStream->ReadShort() : 0;

	pSource = players[g_lCurrentClient].mo;
	if ( pSource == NULL )
		return ( false );

	// If it's legal, do the cheat.
	if ( sv_cheats )
	{
		pType = PClass::FindClass( pszName );
		if (( pType == NULL ) || ( pType->ActorInfo == NULL ))
		{
			Printf( "server_SummonCheat: Couldn't find class: %s\n", pszName );
			return ( false );
		}

		// If it's a missile, spawn it as a player missile.
		if ( GetDefaultByType( pType )->flags & MF_MISSILE )
		{
			pActor = P_SpawnPlayerMissile( pSource, pType );
			if ( pActor )
			{
				SERVERCOMMANDS_SpawnMissile( pActor );
				SERVER_DestroyActorIfClientsidedOnly( pActor );
			}
		}
		else
		{
			const AActor	 *pDef = GetDefaultByType( pType );
					
			pActor = Spawn( pType, pSource->x + FixedMul( pDef->radius * 2 + pSource->radius, finecosine[pSource->angle>>ANGLETOFINESHIFT] ),
						pSource->y + FixedMul( pDef->radius * 2 + pSource->radius, finesine[pSource->angle>>ANGLETOFINESHIFT] ),
						pSource->z + 8 * FRACUNIT, ALLOW_REPLACE );

			// [BB] If this is the summonfriend cheat, we have to make the monster friendly.
			if (pActor != NULL && lType != CLC_SUMMONCHEAT)
			{
				if (lType == CLC_SUMMONFRIENDCHEAT)
				{
					if (pActor->CountsAsKill()) 
					{
						level.total_monsters--;

						// [BB] The monster is friendly, so we need to correct the number of monsters in invasion mode.
						INVASION_UpdateMonsterCount( pActor, true );
					}

					pActor->FriendPlayer = static_cast<BYTE> ( g_lCurrentClient + 1 );
					pActor->flags |= MF_FRIENDLY;
					pActor->LastHeard = players[g_lCurrentClient].mo;
				}
				else
				{
					pActor->FriendPlayer = 0;
					pActor->flags &= ~MF_FRIENDLY;
				}
			}


			if ( pActor )
			{
				SERVERCOMMANDS_SpawnThing( pActor );

				if ( bSetAngle )
				{
					pActor->angle = pSource->angle - (ANGLE_1 * sAngle);
					// [BB] If the angle is not zero, we have to inform the clients.
					if ( pActor->angle != 0 )
						SERVERCOMMANDS_SetThingAngle( pActor );
				}

				SERVER_DestroyActorIfClientsidedOnly( pActor );
			}
		}
	}
	// If not, boot their ass!
	else
	{
		SERVER_KickPlayer( g_lCurrentClient, "Attempted to cheat with sv_cheats being false!" );
		return ( true );
	}

	return ( false );
}

//*****************************************************************************
//
static bool server_ChangeDisplayPlayer( BYTESTREAM_s *pByteStream )
{
	ULONG		ulDisplayPlayer;

	// Read in their display player.
	ulDisplayPlayer = pByteStream->ReadByte();
	if (( ulDisplayPlayer < MAXPLAYERS ) && playeringame[ulDisplayPlayer] )
		g_aClients[g_lCurrentClient].ulDisplayPlayer = ulDisplayPlayer;

	return ( false );
}

//*****************************************************************************
//
static bool server_AuthenticateLevel( BYTESTREAM_s *pByteStream )
{
	ULONG								ulState;
	ULONG								ulCountdownTicks;

	// [BB] Read the name of the map, the client is trying to authenticate.
	const FString mapnameString = pByteStream->ReadString();

	// [BB] The client sent us the authentication data of the wrong level.
	// This can happen if the map changes too quickly twice in a row: In that case
	// we get the authentication data for the first level from the client when
	// the server already loaded the second level.
	if ( stricmp ( mapnameString.GetChars(), level.mapname ) != 0 )
	{
		// [BB] This eats the authentication strings the client is sending, necessary
		// because we need to parse the packet completely.
		SERVER_PerformAuthenticationChecksum( pByteStream );

		// [BB] This authentication problem occures rarely. Make sure that a malicious client
		// can't abuse it for flooding.
		if ( server_CheckForClientCommandFlood ( g_lCurrentClient ) == true )
			return ( true );

		// [BB] Tell the client authenticate again.
		SERVERCOMMANDS_MapAuthenticate ( level.mapname, g_lCurrentClient, SVCF_ONLYTHISCLIENT );
		return ( false );
	}

	if ( SERVER_PerformAuthenticationChecksum( pByteStream ) == false )
	{
		SERVER_KickPlayer( g_lCurrentClient, "Level authentication failed." );
		return ( true );
	}

	// [BB] The client is already authenticated, no need to authenticate again.
	if ( SERVER_GetClient( g_lCurrentClient )->State == CLS_SPAWNED )
		return ( false );

	// [BB] Update the client's state according to the successful authenticaion.
	if ( SERVER_GetClient( g_lCurrentClient )->State == CLS_SPAWNED_BUT_NEEDS_AUTHENTICATION )
		SERVER_GetClient( g_lCurrentClient )->State = CLS_SPAWNED;

	// Now that the level has been authenticated, send all the level data for the client.

	// Send skill level.
	SERVERCOMMANDS_SetGameSkill( g_lCurrentClient, SVCF_ONLYTHISCLIENT );

	// Send special settings like teamplay and deathmatch.
	SERVERCOMMANDS_SetGameMode( g_lCurrentClient, SVCF_ONLYTHISCLIENT );

	// Send timelimit, fraglimit, etc.
	SERVERCOMMANDS_SetGameModeLimits( g_lCurrentClient, SVCF_ONLYTHISCLIENT );

	// If this is LMS, send the allowed weapons.
	if ( lastmanstanding || teamlms )
		SERVERCOMMANDS_SetLMSAllowedWeapons( g_lCurrentClient, SVCF_ONLYTHISCLIENT );

	// [BB] Due to ZADF_ALWAYS_APPLY_LMS_SPECTATORSETTINGS, this is necessary in all game modes.
	SERVERCOMMANDS_SetLMSSpectatorSettings( g_lCurrentClient, SVCF_ONLYTHISCLIENT );

	// If this is CTF or ST, tell the client whether or not we're in simple mode.
	if ( GAMEMODE_GetCurrentFlags() & GMF_USETEAMITEM )
		SERVERCOMMANDS_SetSimpleCTFSTMode( g_lCurrentClient, SVCF_ONLYTHISCLIENT );

	// Send the map name, and have the client load it.
	SERVERCOMMANDS_MapLoad( g_lCurrentClient, SVCF_ONLYTHISCLIENT );

	// Send the map music.
	SERVERCOMMANDS_SetMapMusic( SERVER_GetMapMusic( ), SERVER_GetMapMusicOrder( ), g_lCurrentClient, SVCF_ONLYTHISCLIENT );

	// If we're in a duel or LMS mode, tell him the state of the game mode.
	if ( duel || lastmanstanding || teamlms || possession || teampossession || survival || invasion )
	{
		if ( duel )
		{
			ulState = DUEL_GetState( );
			ulCountdownTicks = DUEL_GetCountdownTicks( );
		}
		else if ( survival )
		{
			ulState = SURVIVAL_GetState( );
			ulCountdownTicks = SURVIVAL_GetCountdownTicks( );
		}
		else if ( invasion )
		{
			ulState = INVASION_GetState( );
			ulCountdownTicks = INVASION_GetCountdownTicks( );
		}
		else if ( possession || teampossession )
		{
			ulState = POSSESSION_GetState( );
			if ( ulState == (PSNSTATE_e)PSNS_ARTIFACTHELD )
				ulCountdownTicks = POSSESSION_GetArtifactHoldTicks( );
			else
				ulCountdownTicks = POSSESSION_GetCountdownTicks( );
		}
		else
		{
			ulState = LASTMANSTANDING_GetState( );
			ulCountdownTicks = LASTMANSTANDING_GetCountdownTicks( );
		}

		SERVERCOMMANDS_SetGameModeState( ulState, ulCountdownTicks, g_lCurrentClient, SVCF_ONLYTHISCLIENT );

		// Also, if we're in invasion mode, tell the client what wave we're on.
		if ( invasion )
			SERVERCOMMANDS_SetInvasionWave( g_lCurrentClient, SVCF_ONLYTHISCLIENT );
	}

	// Tell the client of any lines that have been altered since the level start.
	SERVER_UpdateLines( g_lCurrentClient );

	// Tell the client of any sides that have been altered since the level start.
	SERVER_UpdateSides( g_lCurrentClient );

	// Tell the client of any sectors that have been altered since the level start.
	SERVER_UpdateSectors( g_lCurrentClient );

	// [BB] Tell the client of things derived from DMover and similar classes.
	SERVER_UpdateMovers( g_lCurrentClient );

	// [BB] When spawning a player and resetting its inventory, the client changes its weapon
	// several times. In order to keep weapon sync, tell the client not to send us his local
	// weapon changes. We will tell him to do so, once he has the full inventory and received
	// the full update.
	SERVERCOMMANDS_SetIgnoreWeaponSelect( g_lCurrentClient, true );

	// Tell client to spawn themselves (this doesn't happen in the full update).
	if ( players[g_lCurrentClient].mo != NULL )
	{
		SERVERCOMMANDS_SpawnPlayer( g_lCurrentClient, PST_REBORNNOINVENTORY, g_lCurrentClient, SVCF_ONLYTHISCLIENT );
		// [BB] The player body spawned on the client has the default flag values.
		// If the actual body doesn't have default values (e.g. the player had the
		// fly cheat before tha map change), tell the client about the actual values.
		SERVERCOMMANDS_UpdateThingFlagsNotAtDefaults ( players[g_lCurrentClient].mo, g_lCurrentClient, SVCF_ONLYTHISCLIENT );
		// [BB] Inform the player about its health, otherwise the client thinks that the local player has the default health.
		SERVERCOMMANDS_SetPlayerHealth( g_lCurrentClient, g_lCurrentClient, SVCF_ONLYTHISCLIENT );
		// [BB] It's possible that the MaxHealth property was changed dynamically with ACS, so send it.
		SERVERCOMMANDS_SetPlayerMaxHealth( g_lCurrentClient, g_lCurrentClient, SVCF_ONLYTHISCLIENT );
		// [WS] Inform the client about its player's properties.
		SERVER_UpdateActorProperties( players[g_lCurrentClient].mo, g_lCurrentClient );

		/* [BB] Does not work with the latest ZDoom changes. Check if it's still necessary.
		// If the client has weapon pieces, tell them.
		if ( players[g_lCurrentClient].pieces )
			SERVERCOMMANDS_SetPlayerPieces( g_lCurrentClient, g_lCurrentClient, SVCF_ONLYTHISCLIENT );
		*/

		// If the client is dead, tell them that.
		if ( players[g_lCurrentClient].mo->health <= 0 )
			SERVERCOMMANDS_ThingIsCorpse( players[g_lCurrentClient].mo, g_lCurrentClient, SVCF_ONLYTHISCLIENT );
	}

	// If this player travelled from the last level, we need to reset his inventory so that
	// he still has it on the client end.
	//if ( G_AllowTravel( ))
	// [BB] We have to do this in any case, because he could have gotten something during the time after
	// the map was started on the server, but before it was loaded on the client (APowerInvulnerable for
	// instance in DM games with respawn invulnerability).
	// [BB] Don't tell the client to change the weapon, we do this after the full update.
	SERVER_ResetInventory( g_lCurrentClient, false );
	// [BB] Make sure that the client doesn't bring up a weapon on from the inventory we just
	// gave him before we tell him which weapon to select after the full update.
	SERVERCOMMANDS_ClearConsoleplayerWeapon( g_lCurrentClient );
	// [BB] The client is now spawned and has its inventory back, so he may send us
	// weapon changes again.
	SERVERCOMMANDS_SetIgnoreWeaponSelect( g_lCurrentClient, false );

	// [AK] Update this player's own statuses.
	SERVERCOMMANDS_SetPlayerStatus( g_lCurrentClient, g_lCurrentClient, SVCF_ONLYTHISCLIENT );

	// Send a snapshot of the level.
	SERVER_SendFullUpdate( g_lCurrentClient );

	// [BB] If the player doesn't have a ReadyWeapon for some reason, try to give him
	// his starting weapon so that we can tell him to select it.
	if ( ( players[g_lCurrentClient].ReadyWeapon == NULL ) && players[g_lCurrentClient].mo )
	{
		AInventory *pInventory = players[g_lCurrentClient].mo->FindInventory ( players[g_lCurrentClient].StartingWeaponName );
		if ( pInventory && pInventory->IsKindOf( RUNTIME_CLASS( AWeapon ) ) )
			players[g_lCurrentClient].ReadyWeapon =  static_cast<AWeapon *>( pInventory );
	}
	// [BB] To sync the weapon state clear the player's weapon on the server now. Before this
	// tell the client which weapon he is using. This will also make him tell us that he is bringing
	// up a wepaon.
	SERVERCOMMANDS_WeaponChange( g_lCurrentClient, g_lCurrentClient, SVCF_ONLYTHISCLIENT );

	// [BB] If the client has a weapon, we just requested him to bring it up.
	if ( players[g_lCurrentClient].ReadyWeapon )
	{
			SERVER_GetClient ( g_lCurrentClient )->bWeaponChangeRequested = true;
			SERVER_GetClient ( g_lCurrentClient )->bLastWeaponChangeRequestTick = gametic;
	}

	// [BB] Clear the weapon on the server. It will be brought up when the client tells us.
	PLAYER_ClearWeapon( &players[g_lCurrentClient] );

	// If we need to start this client's enter scripts, do that now.
	if ( g_aClients[g_lCurrentClient].bRunEnterScripts )
	{
		FBehavior::StaticStartTypedScripts( SCRIPT_Enter, players[g_lCurrentClient].mo, true );
		g_aClients[g_lCurrentClient].bRunEnterScripts = false;
	}

	// [BL] To remove pistol fire-fights if the player is supposed to be unarmed clear his inventory
	if ( players[g_lCurrentClient].bUnarmed )
		SERVERCOMMANDS_DestroyAllInventory( g_lCurrentClient, g_lCurrentClient, SVCF_ONLYTHISCLIENT );

	// Finally, send out the packet.
	SERVER_SendClientPacket( g_lCurrentClient, true );

	return ( false );
}

//*****************************************************************************
//
static bool server_CallVote( BYTESTREAM_s *pByteStream )
{
	ULONG		ulVoteCmd;
	char		szCommand[128];

	// Read in the type of vote happening.
	ulVoteCmd = pByteStream->ReadByte();

	// Read in the parameters for the vote.
	FString		Parameters = pByteStream->ReadString();

	// Read in the reason for the vote.
	FString		Reason = pByteStream->ReadString();

	//==============
	// VERIFICATION
	//==============

	// [TP] Don't allow clients, who aren't fully in yet, call votes.
	if ( SERVER_IsValidClient( g_lCurrentClient ) == false )
		return ( false );

	// [BB] If the client is flooding the server with commands, the client is
	// kicked and we don't need to handle the command.
	if ( server_CheckForClientCommandFlood ( g_lCurrentClient ) == true )
		return ( true );

	// Don't allow votes if the server has them disabled.
	if ( sv_nocallvote == 1 )
	{
		SERVER_PrintfPlayer( g_lCurrentClient, "This server has disabled voting.\n" );
		return false;
	}

	// Also, don't allow spectator votes if the server has them disabled.
	if ( sv_nocallvote == 2 && players[g_lCurrentClient].bSpectating )
	{
		SERVER_PrintfPlayer( g_lCurrentClient, "This server requires spectators to join the game to vote.\n" );
		return false;
	}

	// Make sure we have the required number of voters.
	if ( static_cast<int>( CALLVOTE_CountNumEligibleVoters( )) < sv_minvoters )
	{
		SERVER_PrintfPlayer( g_lCurrentClient, "This server requires at least %d eligible voters to call a vote.\n", static_cast<int>( sv_minvoters ));
		return false;
	}

	// [BB] Further no votes, when not in a level, e.g. during intermission.
	if ( gamestate != GS_LEVEL )
	{
		SERVER_PrintfPlayer( g_lCurrentClient, "You can only vote during the game.\n" );
		return false;
	}

	// Check if the specific type of vote is allowed.
	bool bVoteAllowed = false;
	switch ( ulVoteCmd )
	{
	case VOTECMD_KICK:

		bVoteAllowed = !( sv_forbidvoteflags & FORBIDVOTE_KICK );
		sprintf( szCommand, "kick" );
		break;

	case VOTECMD_FORCETOSPECTATE:

		bVoteAllowed = !( sv_forbidvoteflags & FORBIDVOTE_FORCESPEC );
		sprintf( szCommand, "forcespec" );
		break;

	case VOTECMD_MAP:

		bVoteAllowed = !( sv_forbidvoteflags & FORBIDVOTE_MAP );
		sprintf( szCommand, "map" );
		break;
	case VOTECMD_CHANGEMAP:

		bVoteAllowed = !( sv_forbidvoteflags & FORBIDVOTE_CHANGEMAP );
		sprintf( szCommand, "changemap" );
		break;
	case VOTECMD_FRAGLIMIT:

		bVoteAllowed = !( sv_forbidvoteflags & FORBIDVOTE_FRAGLIMIT );
		sprintf( szCommand, "fraglimit" );
		break;
	case VOTECMD_TIMELIMIT:

		bVoteAllowed = !( sv_forbidvoteflags & FORBIDVOTE_TIMELIMIT );
		sprintf( szCommand, "timelimit" );
		break;
	case VOTECMD_WINLIMIT:

		bVoteAllowed = !( sv_forbidvoteflags & FORBIDVOTE_WINLIMIT );
		sprintf( szCommand, "winlimit" );
		break;
	case VOTECMD_DUELLIMIT:

		bVoteAllowed = !( sv_forbidvoteflags & FORBIDVOTE_DUELLIMIT );
		sprintf( szCommand, "duellimit" );
		break;
	case VOTECMD_POINTLIMIT:

		bVoteAllowed = !( sv_forbidvoteflags & FORBIDVOTE_POINTLIMIT );
		sprintf( szCommand, "pointlimit" );
		break;
	case VOTECMD_FLAG:

		bVoteAllowed = !( sv_forbidvoteflags & FORBIDVOTE_FLAG );
		sprintf( szCommand, "flag" );
		break;
	case VOTECMD_NEXTMAP:

		bVoteAllowed = !( sv_forbidvoteflags & FORBIDVOTE_NEXTMAP );
		sprintf( szCommand, "nextmap" );
		break;
	case VOTECMD_NEXTSECRET:

		bVoteAllowed = !( sv_forbidvoteflags & FORBIDVOTE_NEXTSECRET );
		sprintf( szCommand, "nextsecret" );
		break;
	case VOTECMD_RESETMAP:

		bVoteAllowed = !( sv_forbidvoteflags & FORBIDVOTE_RESETMAP );
		sprintf( szCommand, "resetmap" );
		break;
	default:

		{
			const VOTETYPE_s *customVoteType = CALLVOTE_GetCustomVoteTypeDefinition( ulVoteCmd );
			if ( customVoteType == nullptr )
			{
				return ( false );
			}
			else if ( customVoteType->forbidCvarName.IsEmpty() )
			{
				bVoteAllowed = true;
			}
			else
			{
				FBaseCVar* cvar = FindCVar( customVoteType->forbidCvarName, nullptr );
				bVoteAllowed = cvar && ( cvar->GetGenericRep( CVAR_Bool ).Bool == false );
			}

			// [TRSR] Perform any necessary pre-vote conversions.
			CALLVOTE_ConvertCustomVoteParameter( customVoteType, Parameters );

			// [TP] Put the name of the vote type into the command for the vote module to work with this
			// (we won't actually execute it as a command but run the script instead if and when the vote
			// does pass)
			// [TRSR] If the vote isn't allowed, we should use the display name for the error.
			snprintf( szCommand, sizeof szCommand, "%s", bVoteAllowed ? customVoteType->name.GetChars() : customVoteType->displayName.GetChars() );
		}
	}

	// Begin the vote, if that type is allowed.
	if ( bVoteAllowed )
	{
		// [AK] If we're changing a flag, first separate the CVar name
		// and the parameter from each other.
		if ( ulVoteCmd == VOTECMD_FLAG )
		{
			sprintf( szCommand, "%s", Parameters.Left( Parameters.IndexOf( ' ' )).GetChars( ));
			Parameters = Parameters.Right( Parameters.Len() - ( strlen( szCommand ) + 1 ));
		}

		CALLVOTE_BeginVote( szCommand, Parameters, Reason, g_lCurrentClient );
	}
	else
		SERVER_PrintfPlayer( g_lCurrentClient, "%s votes are disabled on this server.\n", szCommand );

	return ( false );
}

//*****************************************************************************
//
static bool server_InventoryUseAll( BYTESTREAM_s *pByteStream )
{
	if (gamestate == GS_LEVEL && !paused && PLAYER_IsValidPlayerWithMo(g_lCurrentClient) )
	{
		AInventory *item = players[g_lCurrentClient].mo->Inventory;

		while (item != NULL)
		{
			AInventory *next = item->Inventory;
			if (item->ItemFlags & IF_INVBAR && !(item->IsKindOf(RUNTIME_CLASS(APuzzleItem))))
			{
				players[g_lCurrentClient].mo->UseInventory (item);
			}
			item = next;
		}
	}
	return ( false );
}

//*****************************************************************************
//
static bool server_InventoryUse( BYTESTREAM_s *pByteStream )
{
	USHORT usActorNetworkIndex = pByteStream->ReadShort();

	if (gamestate == GS_LEVEL && !paused && PLAYER_IsValidPlayerWithMo(g_lCurrentClient) )
	{
		AInventory *item = players[g_lCurrentClient].mo->FindInventory (NETWORK_GetClassNameFromIdentification( usActorNetworkIndex));
		if (item != NULL)
		{
			players[g_lCurrentClient].mo->UseInventory (item);
		}
	}

	return ( false );
}

//*****************************************************************************
//
static bool server_InventoryDrop( BYTESTREAM_s *pByteStream )
{
	USHORT		usActorNetworkIndex = 0;
	AInventory	*pItem;
	
	// [SB] Drop inventory client command is now flood checked.
	if ( server_CheckForClientMinorCommandFlood( g_lCurrentClient ) == true )
		return ( true );

	usActorNetworkIndex = pByteStream->ReadShort();

	// [BB] The server may forbid dropping completely.
	if ( zadmflags & ZADF_NODROP )
		return ( false );

	if (gamestate == GS_LEVEL && !paused && PLAYER_IsValidPlayerWithMo(g_lCurrentClient) )
	{
		pItem = players[g_lCurrentClient].mo->FindInventory( NETWORK_GetClassNameFromIdentification( usActorNetworkIndex ));
		if ( pItem )
			players[g_lCurrentClient].mo->DropInventory( pItem );
	}

	return ( false );
}

//*****************************************************************************
//
static bool server_Puke( BYTESTREAM_s *pByteStream )
{
	const int scriptNetID = pByteStream->ReadLong();

	// [TP/BB] Resolve the script netid into a script number
	const int scriptNum = ( scriptNetID != NO_SCRIPT_NETID )
		? NETWORK_ACSScriptFromNetID ( scriptNetID )
		: -FName( pByteStream->ReadString() );

	ULONG ulArgn = pByteStream->ReadByte();

	// [BB] Valid clients don't send more than four args.
	if ( ulArgn > 4 )
	{
		SERVER_KickPlayer( g_lCurrentClient, "Sent a malformed packet!" );
		return true;
	}

	int arg[4] = { 0, 0, 0, 0 };
	for ( ULONG ulIdx = 0; ulIdx < ulArgn; ++ulIdx )
		arg[ulIdx] = pByteStream->ReadLong();
	bool bAlways = !!pByteStream->ReadByte();

	// [BB] A normal client checks if the script is pukeable and only requests to puke pukeable scripts.
	// Thus if the requested script is not pukeable, the client was tampered with.
	if ( ACS_IsScriptPukeable ( scriptNum ) == false )
	{
		// [BB] Trying to puke a non-pukeable script is treated as possible command flooding
		if ( server_CheckForClientCommandFlood ( g_lCurrentClient ) == true )
			return ( true );

		return ( false );
	}

	// [BB] Execute the script as if it was invoked by the puke command.
	P_StartScript (players[g_lCurrentClient].mo, NULL, scriptNum, level.mapname,
		arg, 4, ( bAlways ? ACS_ALWAYS : 0 ) | ACS_NET );

	return ( false );
}

//*****************************************************************************
//
static bool server_ReceiveACSString( BYTESTREAM_s *pByteStream )
{
	const int scriptNetID = pByteStream->ReadLong();

	// [AK] Resolve the script netid into a script number.
	const int scriptNum = ( scriptNetID != NO_SCRIPT_NETID )
		? NETWORK_ACSScriptFromNetID ( scriptNetID )
		: -FName( pByteStream->ReadString() );

	const char *string = pByteStream->ReadString();

	// [AK] A normal client checks if the script is pukeable and only requests to puke pukeable scripts.
	// Thus if the requested script is not pukeable, the client was tampered with.
	if ( ACS_IsScriptPukeable ( scriptNum ) == false )
	{
		// [AK] Trying to puke a non-pukeable script is treated as possible command flooding.
		if ( server_CheckForClientCommandFlood ( g_lCurrentClient ) == true )
			return ( true );

		return ( false );
	}

	int arg[4] = { GlobalACSStrings.AddString ( string ), 0, 0, 0 };
	P_StartScript( players[g_lCurrentClient].mo, NULL, scriptNum, NULL, arg, 4, ACS_ALWAYS | ACS_NET );

	return ( false );
}

//*****************************************************************************
//
static bool server_MorphCheat( BYTESTREAM_s *pByteStream )
{
	const char *pszMorphClass = pByteStream->ReadString();

	// If it's legal, do the moprh.
	if ( sv_cheats )
	{
		const char *msg = cht_Morph (players + g_lCurrentClient, PClass::FindClass (pszMorphClass), false);
		FString messageString = *msg != '\0' ? msg : "Morph failed.";
		SERVER_PrintfPlayer( g_lCurrentClient, "%s", messageString.GetChars() );
	}
	// If not, boot their ass!
	else
	{
		SERVER_KickPlayer( g_lCurrentClient, "Attempted to cheat with sv_cheats being false!" );
		return ( true );
	}

	return ( false );
}

//*****************************************************************************
//
static bool server_CheckJoinPassword( const FString& clientPassword )
{
	if (( sv_forcejoinpassword ) && ( FString( sv_joinpassword ).IsNotEmpty( )))
	{
		if ( clientPassword.CompareNoCase( sv_joinpassword ) != 0 )
		{
			// Tell the client that the password didn't match.
			SERVER_PrintfPlayer( g_lCurrentClient, "Incorrect join password.\n" );
			return false;
		}
	}

	return true;
}

//*****************************************************************************
//
static bool server_CheckLogin ( const ULONG ulClient )
{
	if ( sv_forcelogintojoin && ( g_aClients[ulClient].loggedIn == false ) )
	{
		SERVER_PrintfPlayer( ulClient, "You need to login before joining.\n" );
		return ( false );
	}

	return true;
}

//*****************************************************************************
//
// [TP] Client wishes to apply the linetarget or info cheat on a particularly
// interesting actor but cannot because they don't know the health of it.
// Let's help it out. :)
//
static bool server_InfoCheat( BYTESTREAM_s *pByteStream )
{
	unsigned short netID = pByteStream->ReadShort();
	AActor* linetarget = CLIENT_FindThingByNetID( netID );
	bool extended = !!pByteStream->ReadByte();

	// [TP] Except not if we don't allow cheats.
	if ( sv_cheats == false )
	{
		SERVER_KickPlayer( g_lCurrentClient, "Attempted to cheat with sv_cheats being false!" );
		return true;
	}

	if ( linetarget == NULL )
	{
		SERVER_PrintfPlayer( g_lCurrentClient,
			"The server couldn't find the actor you're pointing at! netid: %u\n", netID );
		return false;
	}

	SERVER_PrintfPlayer( g_lCurrentClient, "Target=%s, Health=%d, Spawnhealth=%d\n",
		linetarget->GetClass()->TypeName.GetChars(),
		linetarget->health,
		linetarget->GetDefault()->health);

	// [TP] The info cheat is exactly the same as linetarget but also calls PrintMiscActorInfo on
	// the actor. However, this prints the result to the console so to keep code changes to a minimum
	// the result must be captured.
	if ( extended )
	{
		C_StartCapture();
		PrintMiscActorInfo( linetarget );
		SERVER_PrintfPlayer( g_lCurrentClient, "%s", C_EndCapture() );
	}

	return false;
}

//*****************************************************************************
//
// [TP] Prints a message, with the IP substituted in for the local server and with it
// removed broadcasted to clients.
//
static void server_PrintWithIP( FString message, const NETADDRESS_s &address )
{
	// [TP] First print locally a version of this message with the IP address shown.
	// [AK] Don't print it for the current RCON client, though.
	FString localmessage = message;
	localmessage.Substitute( "{ip}", FString( " (" ) + address.ToString() + ")" );
	CONSOLE_ShouldPrintToRCONPlayer( false );
	Printf( "%s", localmessage.GetChars() );

	// [TP] Then print a version of the message without the IP address to clients.
	message.Substitute( "{ip}", "" );
	SERVERCOMMANDS_Print( message, PRINT_HIGH );
}

//*****************************************************************************
//
static void server_PerformBacktrace( ULONG ulClient, ULONG ulNumLateMoveCMDs )
{
	CLIENT_s *pClient = &g_aClients[ulClient];
	APlayerPawn *pmo = players[ulClient].mo;
	FString debugMessage;

	if ( server_ShouldPerformBacktrace( ulClient ))
	{
		debugMessage.Format( "%d: backtracing %s... ", gametic, players[ulClient].userinfo.GetName( ));

		// [AK] Hijack the unlagged's sector reconciliation for the backtrace too.
		int unlaggedIndex = pClient->OldData->ulSavedGametic % UNLAGGEDTICS;

		// [AK] Save the current sector ceiling/floor heights, then set them to whatever they
		// were on the gametic that we started extrapolating this player.
		for ( int i = 0; i < numsectors; i++ )
		{
			sectors[i].floorplane.backtraceRestoreD = sectors[i].floorplane.d;
			sectors[i].ceilingplane.backtraceRestoreD = sectors[i].ceilingplane.d;

			sectors[i].floorplane.d = sectors[i].floorplane.unlaggedD[unlaggedIndex];
			sectors[i].ceilingplane.d = sectors[i].ceilingplane.unlaggedD[unlaggedIndex];
		}

		CLIENT_PLAYER_DATA_s oldData( &players[ulClient] );
		pClient->OldData->Restore( &players[ulClient] );

		// [AK] Check if the player hasn't moved into a spot that's blocking them or something else.
		if ( P_TestMobjLocation( players[ulClient].mo ))
		{
			ULONG ulNumProcessedMoveCMDs = 0;
			fixed_t oldFloorZ = 0;

			pClient->bIsBacktracing = true;
			pClient->ulExtrapolatedTics = 0;

			for ( int tic = 0; tic < static_cast<int>( pClient->LateMoveCMDs.Size( )); tic++ )
			{
				// [AK] If this is a weapon select command, just process it and move onto the next late command.
				if ( pClient->LateMoveCMDs[tic]->isMoveCmd( ) == false )
				{
					pClient->LateMoveCMDs[tic]->process( ulClient );
					continue;
				}

				// [AK] This becomes the last movement command we received from the client.
				delete pClient->LastMoveCMD;
				pClient->LastMoveCMD = new ClientMoveCommand( *static_cast<ClientMoveCommand *>( pClient->LateMoveCMDs[tic] ));

				pClient->LastMoveCMD->process( ulClient );

				// [AK] Now we have to tick this player's body and set the proper psprite settings.
				pmo->Tick( );		
				P_NewPspriteTick( &players[ulClient] );

				pClient->lLastMoveTickProcess--;
				oldFloorZ = pmo->floorz;

				// [AK] Adjust the sector ceiling/floor heights for the next tic that we extrapolated the player.
				// We don't have to do this on the last tic that we extrapolated the player.
				if ( ++ulNumProcessedMoveCMDs < ulNumLateMoveCMDs )
				{
					unlaggedIndex = ( unlaggedIndex + 1 ) % UNLAGGEDTICS;

					for ( int i = 0; i < numsectors; i++ )
					{
						sectors[i].floorplane.d = sectors[i].floorplane.unlaggedD[unlaggedIndex];
						sectors[i].ceilingplane.d = sectors[i].ceilingplane.unlaggedD[unlaggedIndex];
					}

					// [AK] Make sure the player doesn't get stuck in the floor/ceiling in case they moved.
					server_FixZFromBacktrace( pmo, oldFloorZ );
				}
			}

			// [AK] Restore the sector ceiling/floor heights back to what they were before the backtrace.
			for ( int i = 0; i < numsectors; i++ )
			{
				sectors[i].floorplane.d = sectors[i].floorplane.backtraceRestoreD;
				sectors[i].ceilingplane.d = sectors[i].ceilingplane.backtraceRestoreD;
			}

			// [AK] As a final measure, fix the player's floorz/ceilingz and to ensure that they don't
			// get stuck in the floor/ceiling of whatever sector they're supposed to be in.
			server_FixZFromBacktrace( pmo, oldFloorZ );
			debugMessage += "accepted";
		}
		else
		{
			// [AK] Restore the sector ceiling/floor heights back to what they were before the backtrace.
			for ( int i = 0; i < numsectors; i++ )
			{
				sectors[i].floorplane.d = sectors[i].floorplane.backtraceRestoreD;
				sectors[i].ceilingplane.d = sectors[i].ceilingplane.backtraceRestoreD;
			}

			oldData.Restore( &players[ulClient] );
			debugMessage.AppendFormat( "not enough room" );
		}

		if ( sv_smoothplayers_debuginfo )
			Printf( "%s.\n", debugMessage.GetChars( ));
	}

	SERVER_ResetClientExtrapolation( ulClient, true );
}

//*****************************************************************************
//
static bool server_ShouldPerformBacktrace( ULONG ulClient )
{
	bool bShouldPerform = true;
	FString reason;

	if ( g_aClients[ulClient].OldData->pMorphedPlayerClass != players[ulClient].MorphedPlayerClass )
	{
		reason = "morphed during extrapolation";
		bShouldPerform = false;
	}
	else if ( g_aClients[ulClient].OldData->bTeleported )
	{
		reason = "teleported during extrapolation";
		bShouldPerform = false;
	}
	else if ( gametic - g_aClients[ulClient].OldData->ulSavedGametic >= UNLAGGEDTICS )
	{
		// [AK] Since the backtrace needs to hijack the unlagged's sector reconciliation, we don't want
		// to backtrace a player if the amount of ticks since we started extrapolating them is greater
		// than what the unlagged can support. A player might rarely lag out for more than 35 tics that
		// this could become an issue.
		reason = "lagged for too long";
		bShouldPerform = false;
	}

	// [AK] Explain why we didn't backtrace their movement.
	if (( bShouldPerform == false ) && ( sv_smoothplayers_debuginfo ))
		Printf( "%d: cannot backtrace %s (%s).\n", gametic, players[ulClient].userinfo.GetName( ), reason.GetChars( ));

	return bShouldPerform;
}

//*****************************************************************************
//
static void server_FixZFromBacktrace( APlayerPawn *pmo, fixed_t oldFloorZ )
{
	pmo->floorz = pmo->Sector->floorplane.ZatPoint( pmo->x, pmo->y );
	pmo->ceilingz = pmo->Sector->ceilingplane.ZatPoint( pmo->x, pmo->y );
	P_FindFloorCeiling( pmo, false );

	if ( pmo->z + pmo->height > pmo->ceilingz )
		pmo->z = pmo->ceilingz - pmo->height;

	if ( pmo->z < pmo->floorz )
	{
		pmo->z = pmo->floorz;
	}
	else if ( oldFloorZ != pmo->floorz )
	{
		// [AK] Check if the player should move with the floor if it moved, similarly to PIT_FloorDrop.
		if (( pmo->velz == 0 ) && (!( pmo->flags & MF_NOGRAVITY ) || ( pmo->z == oldFloorZ && !( pmo->flags & MF_NOLIFTDROP ))))
		{
			if (( pmo->flags & MF_NOGRAVITY) || ( pmo->flags5 & MF5_MOVEWITHSECTOR ) ||
				( pmo->Sector->Flags & SECF_FLOORDROP ) || ( pmo->z - pmo->floorz <= 9 * FRACUNIT ))
			{
				pmo->z = pmo->floorz;
			}
		}
		else if ( pmo->z != oldFloorZ && !( pmo->flags & MF_NOLIFTDROP ))
		{
			if (( pmo->flags & MF_NOGRAVITY ) && ( pmo->flags6 & MF6_RELATIVETOFLOOR ))
				pmo->z = pmo->z - oldFloorZ + pmo->floorz;
		}
	}
}

//*****************************************************************************
//
FString CLIENT_s::GetAccountName( void ) const
{
	if ( loggedIn )
	{
		return username;
	}
	// [BB] Anonymous players get an account name based on their player slot.
	else
	{
		FString result;
		result.Format ( "%td@localhost", this - g_aClients );
		return result;
	}
}

//*****************************************************************************
//
void CLIENT_s::UpdateCommRules( void )
{
	for ( std::list<ClientCommRule>::iterator i = commRules.begin( ); i != commRules.end( ); )
	{
		// [AK] Check if this address's chat messages are now unignored.
		if (( i->ignoreChat ) && ( i->unignoreChatGametic != -1 ) && ( i->unignoreChatGametic <= gametic ))
			i->SetIgnore( false, false, 0 );

		// [AK] The same goes for this address's VoIP audio packets.
		if (( i->ignoreVoice ) && ( i->unignoreVoiceGametic != -1 ) && ( i->unignoreVoiceGametic <= gametic ))
			i->SetIgnore( true, false, 0 );

		if ( i->IsObsolete( ))
			i = commRules.erase( i );
		else
			i++;
	}
}

//*****************************************************************************
// [SB] Used by the forcerename and forcerename_idx commands.
static void server_ForceRenamePlayer( ULONG playerIndex )
{
	// Make sure the target is valid and applicable.
	if ( PLAYER_IsValidPlayer ( playerIndex ) == false )
	{
		Printf( "No such player!\n" );
		return;
	}

	FString oldName( players[playerIndex].userinfo.GetName() );
	FString newName = PLAYER_GenerateUniqueName();

	players[playerIndex].userinfo.NameChanged( newName );
	SERVERCOMMANDS_SetPlayerUserInfo( playerIndex, { NAME_Name } );

	// Inform the player in question.
	FString message;
	message.Format( "A server administrator has forcibly changed your name. You have been renamed to '%s'.\n", newName.GetChars() );
	SERVERCOMMANDS_PrintMid( message, true, playerIndex, SVCF_ONLYTHISCLIENT );

	// and everyone else.
	SERVER_Printf( "%s is now known as %s\n", oldName.GetChars(), players[playerIndex].userinfo.GetName() );

	// Update clients using the RCON utility.
	SERVER_RCON_UpdateInfo( SVRCU_PLAYERDATA );
}

//*****************************************************************************
//	CONSOLE COMMANDS

CCMD( kick_idx )
{
	// [AK] This function may not be used by ConsoleCommand.
	if ( ACS_IsCalledFromConsoleCommand( ))
		return;

	// Only the server can boot players!
	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
		return;

	if ( argv.argc( ) < 2 )
	{
		Printf( "Usage: kick_idx <player index> [reason]\nYou can get the list of players and indexes with the ccmd playerinfo.\n" );
		return;
	}

	int playerIndex;
	if ( argv.SafeGetNumber( 1, playerIndex ) == false )
		return;

	// [BB] Validity checks are done in SERVER_KickPlayer.
	// If we provided a reason, give it.
	if ( argv.argc( ) >= 3 )
		SERVER_KickPlayer( playerIndex, argv[2] );
	else
		SERVER_KickPlayer( playerIndex, "None given." );
	return;
}

CCMD( kick_ip )
{
	// [AK] This function may not be used by ConsoleCommand.
	if ( ACS_IsCalledFromConsoleCommand( ))
		return;

	// Only the server can boot players!
	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
		return;

	if ( argv.argc( ) < 2 )
	{
		Printf( "Usage: kick_ip <ip:port> [reason]\nYou can get the list of players and indexes with the ccmd playerinfo.\n" );
		return;
	}

	ULONG ulIdx = SERVER_FindClientByAddress( NETADDRESS_s( argv[1] ));

	// [BB] Validity checks are done in SERVER_KickPlayer.
	// If we provided a reason, give it.
	if ( argv.argc( ) >= 3 )
		SERVER_KickPlayer( ulIdx, argv[2] );
	else
		SERVER_KickPlayer( ulIdx, "None given." );

	return;
}

CCMD( kick )
{
	ULONG		ulIdx;
	FString		playerName;

	// [AK] This function may not be used by ConsoleCommand.
	if ( ACS_IsCalledFromConsoleCommand( ))
		return;

	// Only the server can boot players!
	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
		return;

	if ( argv.argc( ) < 2 )
	{
		Printf( "Usage: kick <playername> [reason]\n" );
		return;
	}

	// Loop through all the players, and try to find one that matches the given name.
	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if ( playeringame[ulIdx] == false )
			continue;

		// Removes the color codes from the player name so it appears as the server sees it in the window.
		playerName = players[ulIdx].userinfo.GetName( );
		V_RemoveColorCodes( playerName );

		if ( playerName.CompareNoCase( argv[1] ) == 0 )
		{
			// If we provided a reason, give it.
			if ( argv.argc( ) >= 3 )
				SERVER_KickPlayer( ulIdx, argv[2] );
			else
				SERVER_KickPlayer( ulIdx, "None given." );

			return;
		}
	}

	// Didn't find a player that matches the name.
	Printf( "Unknown player: %s\n", argv[1] );
	return;
}

//*****************************************************************************
//
CCMD( forcespec_idx )
{
	// [AK] This function may not be used by ConsoleCommand.
	if ( ACS_IsCalledFromConsoleCommand( ))
		return;

	// Only the server can boot players!
	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
		return;

	if ( argv.argc( ) < 2 )
	{
		Printf( "Usage: forcespec_idx <player index> [reason]\nYou can get the list of players and indexes with the ccmd playerinfo.\n" );
		return;
	}

	int playerIndex;
	if ( argv.SafeGetNumber( 1, playerIndex ) == false )
		return;

	// [BB] Validity checks are done in SERVER_KickPlayerFromGame.
	// If we provided a reason, give it.
	if ( argv.argc( ) >= 3 )
		SERVER_ForceToSpectate( playerIndex, argv[2] );
	else
		SERVER_ForceToSpectate( playerIndex, "None given." );
	return;
}

//*****************************************************************************
//
CCMD( forcespec )
{
	// [AK] This function may not be used by ConsoleCommand.
	if ( ACS_IsCalledFromConsoleCommand( ))
		return;

	// Only the server can boot players!
	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
		return;

	if ( argv.argc( ) < 2 )
	{
		Printf( "Usage: forcespec <playername> [reason]\n" );
		return;
	}

	bool foundSpectators = false;

	// Loop through all the players, and try to find one that matches the given name.
	for ( int i = 0; i < MAXPLAYERS; i++ )
	{
		if ( playeringame[i] == false )
			continue;

		// Removes the color codes from the player name so it appears as the server sees it in the window.
		FString playerName = players[i].userinfo.GetName();
		V_RemoveColorCodes( playerName );

		if ( playerName.CompareNoCase( argv[1] ) != 0 )
			continue;

		// Already a spectator!
		if ( PLAYER_IsTrueSpectator( &players[i] ))
		{
			foundSpectators = true;
			continue;
		}

		// By now the player is known and valid - kick him from
		// game. If we provided a reason, give it.
		SERVER_ForceToSpectate( i, ( argv.argc( ) >= 3 ) ? argv[2] : "None given" );
		return;
	}

	if ( foundSpectators )
	{
		// [TP] Only matched player(s) were spectators.
		Printf( "%s is already a spectator!\n", argv[1]);
	}
	else
	{
		// Didn't find a player that matches the name.
		Printf( "Unknown player: %s\n", argv[1] );
	}
	return;
}

//*****************************************************************************
// [TP] These can't simply be aliases in keyconf.txt because then reasons would be restricted to one word only.
//
CCMD( kickfromgame )
{
	Cmd_forcespec( argv, who, key );
}

//*****************************************************************************
//
CCMD( kickfromgame_idx )
{
	Cmd_forcespec_idx( argv, who, key );
}

//*****************************************************************************
// [SB] Commands to forcibly change a player's name.
//
CCMD( forcerename_idx )
{
	// This function may not be used by ConsoleCommand.
	if ( ACS_IsCalledFromConsoleCommand( ))
		return;

	// Only the server can do this.
	if ( NETWORK_GetState() != NETSTATE_SERVER )
		return;

	if ( argv.argc() < 2 )
	{
		Printf( "Usage: forcerename_idx <player index>\nYou can get the list of players and indexes with the ccmd playerinfo.\n" );
		return;
	}

	int playerIndex;
	if ( argv.SafeGetNumber(1, playerIndex) == false )
		return;

	if ( playerIndex < 0 || playerIndex >= MAXPLAYERS )
		return;

	server_ForceRenamePlayer( playerIndex );
}

CCMD( forcerename )
{
	// This function may not be used by ConsoleCommand.
	if ( ACS_IsCalledFromConsoleCommand( ))
		return;

	// Only the server can do this.
	if ( NETWORK_GetState() != NETSTATE_SERVER )
		return;

	if ( argv.argc() < 2 )
	{
		Printf( "Usage: forcerename <playername>\n" );
		return;
	}

	// Loop through all the players, and try to find one that matches the given name.
	for ( ULONG idx = 0; idx < MAXPLAYERS; idx++ )
	{
		if ( playeringame[idx] == false )
			continue;

		// Removes the color codes from the player name so it appears as the server sees it in the window.
		FString playerName = players[idx].userinfo.GetName();
		V_RemoveColorCodes( playerName );

		if ( playerName.CompareNoCase( argv[1] ) == 0 )
		{
			server_ForceRenamePlayer( idx );

			return;
		}
	}

	// Didn't find a player that matches the name.
	Printf( "Unknown player: %s\n", argv[1] );
}

//*****************************************************************************
#ifdef	_DEBUG
CCMD( testchecksum )
{
	char	szString[1024];
	FString	MD5String;
	char	append[256];

	memset( szString, 0, 1024 );
	memset( append, 0, 256 );
		
	for ( int i = 1; i <= argv.argc( ) - 1; ++i )
	{
		sprintf( append, "%s ", argv[i] );
		strcat( szString, append );
	}
	
	CMD5Checksum::GetMD5( (BYTE *)szString, 1024, MD5String );
	Printf( "%s\n", MD5String.GetChars() );
}

//*****************************************************************************
CCMD( testchecksumonlevel )
{
	LONG		lBaseLumpNum;
	LONG		lCurLumpNum;
	LONG		lLumpSize;
	FString		Checksum;
	FWadLump	Data;
	BYTE		*pbData;

	// This is the lump number of the current map we're on.
	lBaseLumpNum = Wads.GetNumForName( level.mapname );

	// Get the vertex lump.
	lCurLumpNum = lBaseLumpNum + ML_VERTEXES;
	lLumpSize = Wads.LumpLength( lCurLumpNum );

	// Open the vertex lump, and dump the data from it into our data buffer.
	Data = Wads.OpenLumpNum( lCurLumpNum );
	pbData = new BYTE[lLumpSize];
	Data.Read( pbData, lLumpSize );

	// Perform the checksum on our buffer, and free it.
	CMD5Checksum::GetMD5( pbData, lLumpSize, Checksum );
	delete[] pbData;

	// Now, send the vertex checksum string.
	Printf( "Verticies: %s\n", Checksum.GetChars() );

	// Get the linedefs lump.
	lCurLumpNum = lBaseLumpNum + ML_LINEDEFS;
	lLumpSize = Wads.LumpLength( lCurLumpNum );

	// Open the linedefs lump, and dump the data from it into our data buffer.
	Data = Wads.OpenLumpNum( lCurLumpNum );
	pbData = new BYTE[lLumpSize];
	Data.Read( pbData, lLumpSize );

	// Perform the checksum on our buffer, and free it.
	CMD5Checksum::GetMD5( pbData, lLumpSize, Checksum );
	delete[] pbData;

	// Now, send the linedefs checksum string.
	Printf( "Linedefs: %s\n", Checksum.GetChars() );

	// Get the sidedefs lump.
	lCurLumpNum = lBaseLumpNum + ML_SIDEDEFS;
	lLumpSize = Wads.LumpLength( lCurLumpNum );

	// Open the sidedefs lump, and dump the data from it into our data buffer.
	Data = Wads.OpenLumpNum( lCurLumpNum );
	pbData = new BYTE[lLumpSize];
	Data.Read( pbData, lLumpSize );

	// Perform the checksum on our buffer, and free it.
	CMD5Checksum::GetMD5( pbData, lLumpSize, Checksum );
	delete[] pbData;

	// Now, send the sidedefs checksum string.
	Printf( "Sidedefs: %s\n", Checksum.GetChars() );

	// Get the sectors lump.
	lCurLumpNum = lBaseLumpNum + ML_SECTORS;
	lLumpSize = Wads.LumpLength( lCurLumpNum );

	// Open the sectors lump, and dump the data from it into our data buffer.
	Data = Wads.OpenLumpNum( lCurLumpNum );
	pbData = new BYTE[lLumpSize];
	Data.Read( pbData, lLumpSize );

	// Perform the checksum on our buffer, and free it.
	CMD5Checksum::GetMD5( pbData, lLumpSize, Checksum );
	delete[] pbData;

	// Now, send the sectors checksum string.
	Printf( "Sectors: %s\n", Checksum.GetChars() );
}

//*****************************************************************************
CCMD( trytocrashclient )
{
	ULONG	ulIdx;
	ULONG	ulIdx2;

	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
		{
			if ( SERVER_IsValidClient( ulIdx ) == false )
				continue;

			SERVER_CheckClientBuffer( ulIdx, 256, true );
			g_aClients[ulIdx].PacketBuffer.ByteStream.WriteByte( M_Random( ) % NUM_SERVER_COMMANDS );
			for ( ulIdx2 = 0; ulIdx2 < 512; ulIdx2++ )
				g_aClients[ulIdx].PacketBuffer.ByteStream.WriteByte( M_Random( ));

		}
	}
}
#endif	// _DEBUG

#ifdef CREATE_PACKET_LOG

//*****************************************************************************
// [RC] Logs a suspicious packet to the log.
static void	server_LogPacket( BYTESTREAM_s *pByteStream, NETADDRESS_s Address, const char *pszReason )
{
	FString		logLine;
	BYTE		*OriginalPosition = pByteStream->pbStream;

	// Already logged this one.
	if ( pByteStream->bPacketAlreadyLogged)
		return;

	pByteStream->bPacketAlreadyLogged = true;

	Printf( "Logging packet from %s, because: %s.\n", Address.ToString(), pszReason);

	// Log all further packets from this IP.
	if ( !g_HackerIPList.isIPInList( Address ) )
	{
		IPStringArray szAddress;
		szAddress.SetFrom( Address );
		std::string reason;
		reason = "Hacker";
		g_HackerIPList.addEntry( szAddress, "", pszReason, reason, SERVERBAN_ParseBanLength ( "perm" ) );
	}

	// Write the start of the log entry.
	logLine.Format("\nLogging packet from %s:", Address.ToString() );
	FString logfilename;
	time_t clock;
	struct tm *lt;
	time (&clock);
	lt = localtime (&clock);
	logLine.AppendFormat("\n\ton: %d/%d/%d, at %02d:%02d:%02d", lt->tm_mon + 1, lt->tm_mday, lt->tm_year + 1900, lt->tm_hour, lt->tm_min, lt->tm_sec); 
	logLine.AppendFormat("\n\treason: %s\n\n", pszReason);
	fputs( logLine , PacketLogFile );

	// Advance to the beginning of the packet.
	while ( pByteStream->pbStream != pByteStream->pbStreamBeginning )
		pByteStream->pbStream -= 1;

	// Next, log all the bytes, until the end.
	while ( pByteStream->pbStream != pByteStream->pbStreamEnd )
	{
		int Byte = *pByteStream->pbStream;
		pByteStream->pbStream += 1;		
		logLine.Format("%d ", Byte);

		fputs(logLine, PacketLogFile);
	}

	// Return the stream to where we were.
	while ( pByteStream->pbStream != OriginalPosition )
		pByteStream->pbStream -= 1;

	logLine.Format("\nEnd of packet.\n");
	fputs( logLine, PacketLogFile );
	fflush (PacketLogFile);
}
#endif
