//-----------------------------------------------------------------------------
//
// Skulltag Source
// Copyright (C) 2002 Brad Carney
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
// Date created:  11/26/03
//
//
// Filename: browser.cpp
//
// Description: Contains browser global variables and functions
//
//-----------------------------------------------------------------------------

#include "networkheaders.h"
#include "browser.h"
#include "c_dispatch.h"
#include "cl_main.h"
#include "deathmatch.h"
#include "doomtype.h"
#include "gamemode.h"
#include "i_system.h"
#include "teaminfo.h"
#include "templates.h"
#include "version.h"

//*****************************************************************************
//	VARIABLES

// List of all parsed servers.
static	SERVER_t		g_BrowserServerList[MAX_BROWSER_SERVERS];

// Address of master server.
static	NETADDRESS_s	g_AddressMasterServer;

// Message buffer for sending messages to the master server.
static	NETBUFFER_s		g_MasterServerBuffer;

// Message buffer for sending messages to each individual server.
static	NETBUFFER_s		g_ServerBuffer;

// Port the master server is located on.
static	USHORT			g_usMasterPort;

// Are we waiting for master server response?
static	bool			g_bWaitingForMasterResponse;

// [CW] The amount of teams sent to us.
static ULONG			g_ulNumberOfTeams = 0;

//*****************************************************************************
//	PROTOTYPES

static	LONG	browser_GetNewListID( void );
static	LONG	browser_GetListIDByAddress( NETADDRESS_s Address );
static	void	browser_QueryServer( ULONG ulServer );

//*****************************************************************************
//	FUNCTIONS

void BROWSER_Construct( void )
{
	const char *pszPort;

	g_bWaitingForMasterResponse = false;

	// Setup our master server message buffer.
	g_MasterServerBuffer.Init( MAX_UDP_PACKET, BUFFERTYPE_WRITE );

	// Setup our server message buffer.
	g_ServerBuffer.Init( MAX_UDP_PACKET, BUFFERTYPE_WRITE );

	// Allow the user to specify which port the master server is on.
	pszPort = Args->CheckValue( "-masterport" );
    if ( pszPort )
    {
       g_usMasterPort = atoi( pszPort );
       Printf( PRINT_HIGH, "Alternate master server port: %d.\n", g_usMasterPort );
    }
	else 
	   g_usMasterPort = DEFAULT_MASTER_PORT;

	// Initialize the browser list.
	BROWSER_ClearServerList( );

	// Call BROWSER_Destruct() when Skulltag closes.
	atterm( BROWSER_Destruct );
}

//*****************************************************************************
//
void BROWSER_Destruct( void )
{
	// Free our local buffers.
	g_MasterServerBuffer.Free();
	g_ServerBuffer.Free();
}

//*****************************************************************************
//*****************************************************************************
//
bool BROWSER_IsActive( ULONG ulServer )
{
	if ( ulServer >= MAX_BROWSER_SERVERS )
		return ( false );

	return ( g_BrowserServerList[ulServer].ulActiveState == AS_ACTIVE );
}

//*****************************************************************************
//
bool BROWSER_IsLAN( ULONG ulServer )
{
	if ( ulServer >= MAX_BROWSER_SERVERS )
		return ( false );

	return ( g_BrowserServerList[ulServer].bLAN );
}

//*****************************************************************************
//
NETADDRESS_s BROWSER_GetAddress( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
	{
		NETADDRESS_s	Dummy;
		Dummy.Clear();
		return ( Dummy );
	}

	return ( g_BrowserServerList[ulServer].Address );
}

//*****************************************************************************
//
const char *BROWSER_GetHostName( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( " " );

	return ( g_BrowserServerList[ulServer].HostName.GetChars( ));
}

//*****************************************************************************
//
const char *BROWSER_GetWadURL( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( " " );

	return ( g_BrowserServerList[ulServer].WadURL.GetChars( ));
}

//*****************************************************************************
//
const char *BROWSER_GetEmailAddress( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( " " );

	return ( g_BrowserServerList[ulServer].EmailAddress.GetChars( ));
}

//*****************************************************************************
//
const char *BROWSER_GetMapname( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( " " );

	return ( g_BrowserServerList[ulServer].Mapname.GetChars( ));
}

//*****************************************************************************
//
LONG BROWSER_GetMaxClients( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( false );

	return ( g_BrowserServerList[ulServer].lMaxClients );
}

//*****************************************************************************
//
LONG BROWSER_GetNumPWADs( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( false );

	return ( g_BrowserServerList[ulServer].PWADNames.Size( ));
}

//*****************************************************************************
//
const char *BROWSER_GetPWADName( ULONG ulServer, ULONG ulWadIdx )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( " " );

	// [SB] Also check the index is valid.
	if ( ulWadIdx >= g_BrowserServerList[ulServer].PWADNames.Size())
		return ( " " );

	return ( g_BrowserServerList[ulServer].PWADNames[ulWadIdx].GetChars( ));
}

//*****************************************************************************
//
const char *BROWSER_GetIWADName( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( " " );

	return ( g_BrowserServerList[ulServer].IWADName.GetChars( ));
}

//*****************************************************************************
//
GAMEMODE_e BROWSER_GetGameMode( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( (GAMEMODE_e)false );

	return ( g_BrowserServerList[ulServer].GameMode );
}

//*****************************************************************************
//
LONG BROWSER_GetNumPlayers( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( false );

	return ( g_BrowserServerList[ulServer].lNumPlayers );
}

//*****************************************************************************
//
const char *BROWSER_GetPlayerName( ULONG ulServer, ULONG ulPlayer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( " " );

	if ( ulPlayer >= (ULONG)g_BrowserServerList[ulServer].lNumPlayers )
		return ( " " );

	return ( g_BrowserServerList[ulServer].Players[ulPlayer].Name.GetChars( ));
}

//*****************************************************************************
//
LONG BROWSER_GetPlayerFragcount( ULONG ulServer, ULONG ulPlayer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( false );

	if ( ulPlayer >= (ULONG)g_BrowserServerList[ulServer].lNumPlayers )
		return ( false );

	return ( g_BrowserServerList[ulServer].Players[ulPlayer].lFragcount );
}

//*****************************************************************************
//
LONG BROWSER_GetPlayerPing( ULONG ulServer, ULONG ulPlayer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( false );

	if ( ulPlayer >= (ULONG)g_BrowserServerList[ulServer].lNumPlayers )
		return ( false );

	return ( g_BrowserServerList[ulServer].Players[ulPlayer].lPing );
}

//*****************************************************************************
//
LONG BROWSER_GetPlayerSpectating( ULONG ulServer, ULONG ulPlayer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( false );

	if ( ulPlayer >= (ULONG)g_BrowserServerList[ulServer].lNumPlayers )
		return ( false );

	return ( g_BrowserServerList[ulServer].Players[ulPlayer].bSpectating );
}

//*****************************************************************************
//
LONG BROWSER_GetPing( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( false );

	return ( g_BrowserServerList[ulServer].lPing );
}

//*****************************************************************************
//
const char *BROWSER_GetVersion( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( " " );

	return ( g_BrowserServerList[ulServer].Version.GetChars( ));
}

//*****************************************************************************
// [SB]
const char *BROWSER_GetGameModeName( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( " " );

	// [SB] If the server didn't specify the name, use the name we know this mode by.
	if ( g_BrowserServerList[ulServer].GameModeName.IsEmpty( ))
		return ( GAMEMODE_GetName( g_BrowserServerList[ulServer].GameMode ));

	return ( g_BrowserServerList[ulServer].GameModeName.GetChars( ));
}

//*****************************************************************************
// [SB]
const char *BROWSER_GetGameModeShortName( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( " " );

	// [SB] If the server didn't specify the name, use the name we know this mode by.
	if ( g_BrowserServerList[ulServer].GameModeShortName.IsEmpty( ))
		return ( GAMEMODE_GetShortName( g_BrowserServerList[ulServer].GameMode ));

	return ( g_BrowserServerList[ulServer].GameModeShortName.GetChars( ));
}

//*****************************************************************************
//*****************************************************************************
//
void BROWSER_ClearServerList( void )
{
	ULONG	ulIdx;

	for ( ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		g_BrowserServerList[ulIdx].ulActiveState = AS_INACTIVE;

		g_BrowserServerList[ulIdx].Address.Clear();
	}
}

//*****************************************************************************
//
void BROWSER_DeactivateAllServers( void )
{
	ULONG	ulIdx;

	for ( ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		if ( g_BrowserServerList[ulIdx].ulActiveState == AS_ACTIVE )
			g_BrowserServerList[ulIdx].ulActiveState = AS_INACTIVE;
	}
}

//*****************************************************************************
//
void BROWSER_AddServerToList( const NETADDRESS_s &Address )
{
	const ULONG ulServer = browser_GetNewListID( );
	if ( ulServer >= MAX_BROWSER_SERVERS )
		I_Error( "BROWSER_GetServerList: Server limit exceeded (>=%d servers)", MAX_BROWSER_SERVERS );

	// This server is now active.
	g_BrowserServerList[ulServer].ulActiveState = AS_WAITINGFORREPLY;

	// Set the server address.
	g_BrowserServerList[ulServer].Address = Address;
}

//*****************************************************************************
// [BB] Returns true if the server list packet was terminated by MSC_ENDSERVERLIST,
// else it returns false.
bool BROWSER_GetServerList( BYTESTREAM_s *pByteStream )
{
	// No longer waiting for a master server response.
	g_bWaitingForMasterResponse = false;

	while ( true )
	{
		const LONG lCommand = pByteStream->ReadByte();

		switch ( lCommand )
		{
		case MSC_SERVER:
			{
				// Read in address information.
				NETADDRESS_s serverAddress;
				serverAddress.ReadFromStream ( pByteStream );

				BROWSER_AddServerToList ( serverAddress );
			}
			break;

		case MSC_SERVERBLOCK:
			{
				// Read in address information.
				NETADDRESS_s serverAddress;
				ULONG ulPorts = 0;
				while (( ulPorts = pByteStream->ReadByte() ))
				{
					serverAddress.ReadFromStream ( pByteStream, false );
					for ( ULONG ulIdx = 0; ulIdx < ulPorts; ++ulIdx )
					{
						serverAddress.usPort = htons( pByteStream->ReadShort());
						BROWSER_AddServerToList ( serverAddress );
					}
				}

			}
			break;

		case MSC_ENDSERVERLISTPART:
			return false;

		case MSC_ENDSERVERLIST:
			return true;

		default:

			Printf( "Unknown server list command from master server: %d\n", static_cast<int> (lCommand) );
			return false;
		}
	}
}

//*****************************************************************************
//
void M_BuildServerList( void );

void BROWSER_ParseServerQuery( BYTESTREAM_s *pByteStream, bool bLAN )
{
	GAMEMODE_e	GameMode = GAMEMODE_COOPERATIVE;
	ULONG		ulIdx;
	LONG		lServer;
	ULONG		ulFlags;
	ULONG 		ulFlags2;
	bool		bResortList = true;

	lServer = browser_GetListIDByAddress( NETWORK_GetFromAddress( ));

	// If this is a LAN server and it's already on the list, there's no
	// need to resort the server list.
	if ( bLAN && ( lServer != -1 ))
		bResortList = false;

	// If this is a LAN server, and it doesn't exist on the server list, add it.
	if (( lServer == -1 ) && bLAN )
		lServer = browser_GetNewListID( );

	// [BB] If we didn't find the server, the SERVER_LAUNCHER_CHALLENGE came from an
	// address we didn't query, so just ignore whatever was sent to us.
	if ( lServer == -1 )
	{
		while ( 1 )
		{
			if ( pByteStream->ReadByte() == -1 )
				return;
		}
	}

	// This server is now active.
	g_BrowserServerList[lServer].ulActiveState = AS_ACTIVE;

	// Is this a LAN server?
	g_BrowserServerList[lServer].bLAN = bLAN;

	// We heard back from this server, so calculate ping right away.
	if ( bLAN )
	{
		// If this is a LAN server, the IP address has not be set up yet.
		g_BrowserServerList[lServer].Address = NETWORK_GetFromAddress( );
		g_BrowserServerList[lServer].lPing = 0;
	}
	else
		g_BrowserServerList[lServer].lPing = I_MSTime( ) - g_BrowserServerList[lServer].lMSTime;

	// Read in the time we sent to the server.
	pByteStream->ReadLong();

	// Read in the version.
	g_BrowserServerList[lServer].Version = pByteStream->ReadString();

	// If the version doesn't match ours, remove it from the list.
	{
		// [BB] Get rid of a trailing 'M' that indicates local source changes.
		FString ourVersion = GetVersionStringRev();
		if ( ourVersion[ourVersion.Len()-1] == 'M' )
			ourVersion = ourVersion.Left ( ourVersion.Len()-1 );

		// [BB] Check whether the server version starts with our version.
		if ( g_BrowserServerList[lServer].Version.IndexOf ( ourVersion ) != 0 )
		{
			g_BrowserServerList[lServer].ulActiveState = AS_INACTIVE;
			while ( 1 )
			{
				if ( pByteStream->ReadByte() == -1 )
					return;
			}
		}
	}

	// Read in the data that will be sent to us.
	ulFlags = pByteStream->ReadLong();

	// Read the server name.
	if ( ulFlags & SQF_NAME )
		g_BrowserServerList[lServer].HostName = pByteStream->ReadString();

	// Read the website URL.
	if ( ulFlags & SQF_URL )
		g_BrowserServerList[lServer].WadURL = pByteStream->ReadString();

	// Read the host's e-mail address.
	if ( ulFlags & SQF_EMAIL )
		g_BrowserServerList[lServer].EmailAddress = pByteStream->ReadString();

	if ( ulFlags & SQF_MAPNAME )
		g_BrowserServerList[lServer].Mapname = pByteStream->ReadString();
	if ( ulFlags & SQF_MAXCLIENTS )
		g_BrowserServerList[lServer].lMaxClients = pByteStream->ReadByte();

	// Maximum slots.
	if ( ulFlags & SQF_MAXPLAYERS )
		pByteStream->ReadByte();

	// Read in the PWAD information.
	if ( ulFlags & SQF_PWADS )
	{
		ULONG ulNumPWADs = static_cast<ULONG>( pByteStream->ReadByte() );
		if ( ulNumPWADs > 0 )
		{
			g_BrowserServerList[lServer].PWADNames.Resize( ulNumPWADs );
			for ( ulIdx = 0; ulIdx < ulNumPWADs; ulIdx++ )
				g_BrowserServerList[lServer].PWADNames[ulIdx] = pByteStream->ReadString();
		}
	}

	if ( ulFlags & SQF_GAMETYPE )
	{
		g_BrowserServerList[lServer].GameMode = (GAMEMODE_e)pByteStream->ReadByte();
		pByteStream->ReadByte();
		pByteStream->ReadByte();
	}

	// Game name.
	if ( ulFlags & SQF_GAMENAME )
		pByteStream->ReadString();

	// Read in the IWAD name.
	if ( ulFlags & SQF_IWAD )
		g_BrowserServerList[lServer].IWADName = pByteStream->ReadString();

	// Force password.
	if ( ulFlags & SQF_FORCEPASSWORD )
		pByteStream->ReadByte();

	// Force join password.
	if ( ulFlags & SQF_FORCEJOINPASSWORD )
		pByteStream->ReadByte();

	// Game skill.
	if ( ulFlags & SQF_GAMESKILL )
		pByteStream->ReadByte();

	// Bot skill.
	if ( ulFlags & SQF_BOTSKILL )
		pByteStream->ReadByte();

	if ( ulFlags & SQF_DMFLAGS )
	{
		// DMFlags.
		pByteStream->ReadLong();

		// DMFlags2.
		pByteStream->ReadLong();

		// Compatflags.
		pByteStream->ReadLong();
	}

	if ( ulFlags & SQF_LIMITS )
	{
		// Fraglimit.
		pByteStream->ReadShort();

		// Timelimit.
		if ( pByteStream->ReadShort())
		{
			// Time left.
			pByteStream->ReadShort();
		}

		// Duellimit.
		pByteStream->ReadShort();

		// Pointlimit.
		pByteStream->ReadShort();

		// Winlimit.
		pByteStream->ReadShort();
	}

	// Team damage scale.
	if ( ulFlags & SQF_TEAMDAMAGE )
		pByteStream->ReadFloat();

	// [CW] Deprecated!
	if ( ulFlags & SQF_TEAMSCORES )
	{
		// Blue score.
		pByteStream->ReadShort();

		// Red score.
		pByteStream->ReadShort();
	}

	// Read in the number of players.
	if ( ulFlags & SQF_NUMPLAYERS )
		g_BrowserServerList[lServer].lNumPlayers = pByteStream->ReadByte();

	if ( ulFlags & SQF_PLAYERDATA )
	{
		if ( g_BrowserServerList[lServer].lNumPlayers > 0 )
		{
			for ( ulIdx = 0; ulIdx < (ULONG)g_BrowserServerList[lServer].lNumPlayers; ulIdx++ )
			{
				// Read in this player's name.
				g_BrowserServerList[lServer].Players[ulIdx].Name = pByteStream->ReadString();

				// Read in "fragcount" (could be frags, points, etc.)
				g_BrowserServerList[lServer].Players[ulIdx].lFragcount = pByteStream->ReadShort();

				// Read in the player's ping.
				g_BrowserServerList[lServer].Players[ulIdx].lPing = pByteStream->ReadShort();

				// Read in whether or not the player is spectating.
				g_BrowserServerList[lServer].Players[ulIdx].bSpectating = !!pByteStream->ReadByte();

				// Read in whether or not the player is a bot.
				g_BrowserServerList[lServer].Players[ulIdx].bIsBot = !!pByteStream->ReadByte();

				if ( GAMEMODE_GetFlags( g_BrowserServerList[lServer].GameMode ) & GMF_PLAYERSONTEAMS )
				{
					// Team.
					pByteStream->ReadByte();
				}

				// Time.
				pByteStream->ReadByte();
			}
		}
	}

	// [CW] Read in the number of the teams.
	// [BB] Make sure that the number is valid!
	if ( ulFlags & SQF_TEAMINFO_NUMBER )
		g_ulNumberOfTeams = clamp ( pByteStream->ReadByte(), 2, MAX_TEAMS );

	// [CW] Read in the name of the teams.
	if ( ulFlags & SQF_TEAMINFO_NAME )
	{
		for ( ULONG ulIdx = 0; ulIdx < g_ulNumberOfTeams; ulIdx++ )
			pByteStream->ReadString();
	}

	// [CW] Read in the color of the teams.
	if ( ulFlags & SQF_TEAMINFO_COLOR )
	{
		for ( ULONG ulIdx = 0; ulIdx < g_ulNumberOfTeams; ulIdx++ )
			pByteStream->ReadLong();
	}

	// [CW] Read in the score of the teams.
	if ( ulFlags & SQF_TEAMINFO_SCORE )
	{
		for ( ULONG ulIdx = 0; ulIdx < g_ulNumberOfTeams; ulIdx++ )
			pByteStream->ReadShort();
	}

	// [BB] Testing server and what's the binary name?
	if ( ulFlags & SQF_TESTING_SERVER )
	{
		pByteStream->ReadByte();
		pByteStream->ReadString();
	}

	// [BB] MD5 sum of the main data file (skulltag.wad / skulltag_data.pk3).
	if ( ulFlags & SQF_DATA_MD5SUM )
		pByteStream->ReadString();

	// [BB] All dmflags and compatflags.
	if ( ulFlags & SQF_ALL_DMFLAGS )
	{
		const ULONG ulNumFlags = pByteStream->ReadByte();
		for ( ULONG ulIdx = 0; ulIdx < ulNumFlags; ulIdx++ )
			pByteStream->ReadLong();
	}

	// [BB] Get special security settings like sv_enforcemasterbanlist.
	if ( ulFlags & SQF_SECURITY_SETTINGS )
		pByteStream->ReadByte();

	// [TP] Optional wads
	if ( ulFlags & SQF_OPTIONAL_WADS )
	{
		for ( int i = pByteStream->ReadByte(); i > 0; --i )
			pByteStream->ReadByte();
	}

	// [TP] Dehacked patches
	if ( ulFlags & SQF_DEH )
	{
		for ( int i = pByteStream->ReadByte(); i > 0; --i )
			pByteStream->ReadString();
	}

	// [SB] Extended server info
	if ( ulFlags & SQF_EXTENDED_INFO )
	{
		ulFlags2 = pByteStream->ReadLong();

		// [SB] PWAD hashes
		if ( ulFlags2 & SQF2_PWAD_HASHES )
		{
			for ( int i = pByteStream->ReadByte(); i > 0; --i )
				pByteStream->ReadString();
		}

		// [SB] Server country code
		if ( ulFlags2 & SQF2_COUNTRY )
		{
			char code[3];
			pByteStream->ReadBuffer( code, 3 );
		}

		// [SB] Game mode names
		if ( ulFlags2 & SQF2_GAMEMODE_NAME )
		{
			g_BrowserServerList[lServer].GameModeName = pByteStream->ReadString();
		}

		if ( ulFlags2 & SQF2_GAMEMODE_SHORTNAME )
		{
			g_BrowserServerList[lServer].GameModeShortName = pByteStream->ReadString();
		}
	}

	// Now that this server has been read in, resort the servers in the menu.
	if ( bResortList )
		M_BuildServerList( );
}

//*****************************************************************************
//
void BROWSER_QueryMasterServer( void )
{
	// We are currently waiting to hear back from the master server.
	g_bWaitingForMasterResponse = true;

	// Setup the master server IP.
	g_AddressMasterServer.LoadFromString( masterhostname );
	g_AddressMasterServer.SetPort( g_usMasterPort );

	// Clear out the buffer, and write out launcher challenge.
	g_MasterServerBuffer.Clear();
	g_MasterServerBuffer.ByteStream.WriteLong( LAUNCHER_MASTER_CHALLENGE );
	g_MasterServerBuffer.ByteStream.WriteShort( MASTER_SERVER_VERSION );

	// Send the master server our packet.
//	NETWORK_LaunchPacket( &g_MasterServerBuffer, g_AddressMasterServer, true );
	NETWORK_LaunchPacket( &g_MasterServerBuffer, g_AddressMasterServer );
}

//*****************************************************************************
//
bool BROWSER_WaitingForMasterResponse( void )
{
	return ( g_bWaitingForMasterResponse );
}

//*****************************************************************************
//
void BROWSER_QueryAllServers( void )
{
	ULONG	ulIdx;

	for ( ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		if ( g_BrowserServerList[ulIdx].ulActiveState == AS_WAITINGFORREPLY )
			browser_QueryServer( ulIdx );
	}
}

//*****************************************************************************
//
LONG BROWSER_CalcNumServers( void )
{
	ULONG	ulIdx;
	ULONG	ulNumServers;

	ulNumServers = 0;
	for ( ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		if ( g_BrowserServerList[ulIdx].ulActiveState == AS_ACTIVE )
			ulNumServers++;
	}

	return ( ulNumServers );
}

//*****************************************************************************
//*****************************************************************************
//
static LONG browser_GetNewListID( void )
{
	ULONG	ulIdx;

	for ( ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		if ( g_BrowserServerList[ulIdx].ulActiveState == AS_INACTIVE )
			return ( ulIdx );
	}

	return ( -1 );
}

//*****************************************************************************
//
static LONG browser_GetListIDByAddress( NETADDRESS_s Address )
{
	ULONG	ulIdx;

	for ( ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		if ( g_BrowserServerList[ulIdx].Address.Compare( Address ))
			return ( ulIdx );
	}

	return ( -1 );
}

//*****************************************************************************
//
static void browser_QueryServer( ULONG ulServer )
{
	// Don't query a server that we're already connected to.
	if (( NETWORK_GetState( ) == NETSTATE_CLIENT ) &&
		( g_BrowserServerList[ulServer].Address.Compare( CLIENT_GetServerAddress() )))
	{
		return;
	}

	// Clear out the buffer, and write out launcher challenge.
	// [SB] Added extended flags that we want.
	g_ServerBuffer.Clear();
	g_ServerBuffer.ByteStream.WriteLong( LAUNCHER_SERVER_CHALLENGE );
	g_ServerBuffer.ByteStream.WriteLong( SQF_NAME|SQF_URL|SQF_EMAIL|SQF_MAPNAME|SQF_MAXCLIENTS|SQF_PWADS|SQF_GAMETYPE|SQF_IWAD|SQF_NUMPLAYERS|SQF_PLAYERDATA|SQF_EXTENDED_INFO );
	g_ServerBuffer.ByteStream.WriteLong( I_MSTime( ));
	g_ServerBuffer.ByteStream.WriteLong( SQF2_GAMEMODE_NAME|SQF2_GAMEMODE_SHORTNAME );

	// Send the server our packet.
	NETWORK_LaunchPacket( &g_ServerBuffer, g_BrowserServerList[ulServer].Address );

	// Keep track of the time we queried this server at.
	g_BrowserServerList[ulServer].lMSTime = I_MSTime( );
}

//*****************************************************************************
//	CONSOLE VARIABLES/COMMANDS

//*****************************************************************************
//

CCMD( dumpserverlist )
{
	ULONG	ulIdx;

	for ( ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		if ( g_BrowserServerList[ulIdx].ulActiveState != AS_ACTIVE )
			continue;

		Printf( "\nServer #%d\n----------------\n", static_cast<unsigned int> (ulIdx) );
		Printf( "Name: %s\n", g_BrowserServerList[ulIdx].HostName.GetChars() );
		Printf( "Address: %s\n", g_BrowserServerList[ulIdx].Address.ToString() );
		Printf( "Gametype: %d (%s)\n", g_BrowserServerList[ulIdx].GameMode, BROWSER_GetGameModeName(ulIdx) );
		Printf( "Num PWADs: %d\n", static_cast<int> (g_BrowserServerList[ulIdx].PWADNames.Size()) );
		Printf( "Players: %d/%d\n", static_cast<int> (g_BrowserServerList[ulIdx].lNumPlayers), static_cast<int> (g_BrowserServerList[ulIdx].lMaxClients) );
		Printf( "Ping: %d\n", static_cast<int> (g_BrowserServerList[ulIdx].lPing) );
	}
}
