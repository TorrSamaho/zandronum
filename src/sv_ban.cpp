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
// Filename: sv_ban.cpp
//
// Description: Support for banning IPs from the server.
//
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <errno.h>

#include "c_dispatch.h"
#include "doomstat.h"
#include "network.h"
#include "sv_ban.h"
#include "version.h"
#include "v_text.h"
#include "p_acs.h"

//--------------------------------------------------------------------------------------------------------------------------------------------------
//-- VARIABLES -------------------------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------

static	TArray<IPList>	g_ServerBans;
static	TArray<IPList>	g_ServerBanExemptions;

// [AK] Pending changes to the ban lists from the server console window.
static	TArray<IPList>	g_ServerConsoleBanUpdates;

static	IPList	g_MasterServerBans;
static	IPList	g_MasterServerBanExemptions;

static	ULONG	g_ulReParseTicker;

//--------------------------------------------------------------------------------------------------------------------------------------------------
//-- PROTOTYPES ------------------------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------

static	void	serverban_LoadFilesFromCVar( TArray<IPList> &lists, FStringCVar &cvar );
static	void	serverban_LoadBansAndBanExemptions( void );
static	void	serverban_KickBannedPlayers( void );
static	LONG	serverban_ExtractBanLength( FString fSearchString, const char *pszPattern );
static	time_t	serverban_CreateBanDate( LONG lAmount, ULONG ulUnitSize, time_t tNow );
static	void	serverban_ExecuteGetIPCmd( FCommandLine &argv, bool isIndexCmd );
static	void	serverban_ExecuteBanCmd( FCommandLine &argv, bool isIndexCmd );
static	void	serverban_ExecuteAddOrDelBanCmd( TArray<IPList> &lists, FCommandLine &argv, bool isDelCmd );
static	void	serverban_ListAddresses( const IPList &list );
static	void	serverban_ListFilesAndAddresses( const TArray<IPList> &lists );
static	void	serverban_RetrieveFileIndices( const TArray<IPList> &lists, FString &string );

//--------------------------------------------------------------------------------------------------------------------------------------------------
//-- CVARS -----------------------------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------

CVAR( Bool, sv_enforcebans, true, CVAR_ARCHIVE|CVAR_NOSETBYACS )
CVAR( Int, sv_banfilereparsetime, 0, CVAR_ARCHIVE|CVAR_NOSETBYACS )

//*****************************************************************************
//
CUSTOM_CVAR( Bool, sv_enforcemasterbanlist, true, CVAR_ARCHIVE|CVAR_NOSETBYACS|CVAR_SERVERINFO )
{
	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
		return;

	// [BB] If we are enforcing the master bans, make sure master bannded players are kicked now.
	if ( self == true )
		serverban_KickBannedPlayers( );
}

//*****************************************************************************
//
CUSTOM_CVAR( String, sv_banfile, "banlist.txt", CVAR_ARCHIVE|CVAR_NOSETBYACS|CVAR_SENSITIVESERVERSETTING )
{
	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
		return;

	serverban_LoadFilesFromCVar( g_ServerBans, sv_banfile );

	// Re-parse the file periodically.
	g_ulReParseTicker = sv_banfilereparsetime * TICRATE;
}

//*****************************************************************************
//
CUSTOM_CVAR( String, sv_banexemptionfile, "whitelist.txt", CVAR_ARCHIVE|CVAR_NOSETBYACS|CVAR_SENSITIVESERVERSETTING )
{
	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
		return;

	serverban_LoadFilesFromCVar( g_ServerBanExemptions, sv_banexemptionfile );
}

//--------------------------------------------------------------------------------------------------------------------------------------------------
//-- FUNCTIONS -------------------------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------

//*****************************************************************************
//
void SERVERBAN_Tick( void )
{
	// [RC] Remove any old tempbans.
	for ( unsigned int i = 0; i < g_ServerBans.Size( ); i++ )
		g_ServerBans[i].removeExpiredEntries( );

	// Is it time to re-parse the ban lists?
	if ( g_ulReParseTicker && (( --g_ulReParseTicker ) == 0 ))
	{
		serverban_LoadBansAndBanExemptions( );

		// Parse again periodically.
		g_ulReParseTicker = sv_banfilereparsetime * TICRATE;
	}

	// [AK] Any pending changes to the ban lists that were made from the server
	// console window should be applied now.
	if ( g_ServerConsoleBanUpdates.Size( ) > 0 )
	{
		FString listString;
		UCVarValue val;

		for ( unsigned int i = 0; i < g_ServerConsoleBanUpdates.Size( ); i++ )
		{
			if ( i > 0 )
				listString += ';';

			listString += g_ServerConsoleBanUpdates[i].getFilename( );
		}

		val.String = listString.GetChars( );

		// [AK] Update sv_banfile so that the ban files are loaded.
		sv_banfile.SetGenericRep( val, CVAR_String );

		// [AK] Refresh all of the ban lists that are loaded now.
		for ( unsigned int i = 0; i < g_ServerConsoleBanUpdates.Size( ); i++ )
		{
			// Clear out the ban list, and then add all the bans in the ban list.
			SERVERBAN_ClearBans( i );

			for ( unsigned int j = 0; j < g_ServerConsoleBanUpdates[i].size( ); j++ )
			{
				const IPADDRESSBAN_s &entry = g_ServerConsoleBanUpdates[i].getVector( )[j];
				std::string message;

				SERVERBAN_GetBanList( )[i].addEntry( entry.szIP, "", entry.szComment, message, entry.tExpirationDate );
			}
		}

		// [AK] Kick any players that might be banned now.
		serverban_KickBannedPlayers( );
		g_ServerConsoleBanUpdates.Clear( );
	}
}

//*****************************************************************************
//
bool SERVERBAN_IsIPBanned( const IPStringArray &Address )
{
	// Is this address banned on the master server?
	if ( SERVERBAN_IsIPMasterBanned( Address ))
		return true;

	// If not, let the server decide.
	if (( sv_enforcebans ) && ( SERVERBAN_GetBanInformation( Address )))
	{
		bool foundInExemptions = false;

		for ( unsigned int j = 0; j < g_ServerBanExemptions.Size( ); j++ )
		{
			if ( g_ServerBanExemptions[j].isIPInList( Address ))
			{
				foundInExemptions = true;
				break;
			}
		}

		if ( foundInExemptions == false )
			return true;
	}

	return false;
}

//*****************************************************************************
//
bool SERVERBAN_IsIPBanned( const NETADDRESS_s &Address )
{
	IPStringArray convertedAddress;
	convertedAddress.SetFrom( Address );

	return SERVERBAN_IsIPBanned( convertedAddress );
}

//*****************************************************************************
//
bool SERVERBAN_IsIPMasterBanned( const IPStringArray &Address )
{
	return ( sv_enforcemasterbanlist && g_MasterServerBans.isIPInList( Address ) && !g_MasterServerBanExemptions.isIPInList( Address ));
}

//*****************************************************************************
//
bool SERVERBAN_IsIPMasterBanned( const NETADDRESS_s &Address )
{
	IPStringArray convertedAddress;
	convertedAddress.SetFrom( Address );

	return SERVERBAN_IsIPMasterBanned( convertedAddress );
}

//*****************************************************************************
//
IPADDRESSBAN_s *SERVERBAN_GetBanInformation( const IPStringArray &Address )
{
	// [AK] Find an entry comment in one of the ban files that corresponds
	// to the player's IP address, and include it with the ban reason.
	for ( unsigned int i = 0; i < g_ServerBans.Size( ); i++ )
	{
		unsigned int index = g_ServerBans[i].getFirstMatchingEntryIndex( Address );

		if ( index < g_ServerBans[i].size( ))
			return &g_ServerBans[i].getVector( )[index];
	}

	return nullptr;
}

//*****************************************************************************
//
IPADDRESSBAN_s *SERVERBAN_GetBanInformation( const NETADDRESS_s &Address )
{
	IPStringArray convertedAddress;
	convertedAddress.SetFrom( Address );

	return SERVERBAN_GetBanInformation( convertedAddress );
}

//*****************************************************************************
//
void SERVERBAN_ClearBans( unsigned int fileIndex )
{
	FILE *file = nullptr;

	if ( fileIndex >= g_ServerBans.Size( ))
	{
		Printf( "Error: file index is invalid.\n" );
		return;
	}

	// Clear out the existing bans in memory.
	g_ServerBans[fileIndex].clear( );

	// Export the cleared banlist.
	if (( file = fopen( g_ServerBans[fileIndex].getFilename( ), "w" )) != nullptr )
	{
		FString message;
		message.AppendFormat( "// This is a %s server IP list.\n// Format: 0.0.0.0 <mm/dd/yy> :optional comment\n\n", GAMENAME );
		fputs( message.GetChars( ), file );
		fclose( file );

		Printf( "Banlist file \"%s\" cleared.\n", g_ServerBans[fileIndex].getFilename( ));
	}
	else
	{
		Printf( "SERVERBAN_ClearBans: Could not open \"%s\" for writing: %s\n", g_ServerBans[fileIndex].getFilename( ), strerror( errno ));
	}
}

//*****************************************************************************
//
void SERVERBAN_ReadMasterServerBans( BYTESTREAM_s *pByteStream )
{	
	g_MasterServerBans.clear( );
	g_MasterServerBanExemptions.clear( );

	// Read the list of bans.
	for ( LONG i = 0, lNumEntries = pByteStream->ReadLong(); i < lNumEntries; i++ )
	{
		const char		*pszBan = pByteStream->ReadString();
		std::string		Message;

		g_MasterServerBans.addEntry( pszBan, "", "", Message, 0 );
	}

	// Read the list of exemptions.
	for ( LONG i = 0, lNumEntries = pByteStream->ReadLong(); i < lNumEntries; i++ )
	{
		const char		*pszBan = pByteStream->ReadString();
		std::string		Message;

		g_MasterServerBanExemptions.addEntry( pszBan, "", "", Message, 0 );
	}

	// [BB] If we are enforcing the master bans, make sure newly master bannded players are kicked now.
	if ( sv_enforcemasterbanlist )
		serverban_KickBannedPlayers( );

	// [BB] Inform the master that we received the banlist.
	SERVER_MASTER_SendBanlistReceipt();

	// Printf( "Imported %d bans, %d exceptions from the master.\n", g_MasterServerBans.size( ), g_MasterServerBanExemptions.size( ));
}

//*****************************************************************************
//
void SERVERBAN_ReadMasterServerBanlistPart( BYTESTREAM_s *pByteStream )
{
	const ULONG ulPacketNum = pByteStream->ReadByte();

	// [BB] The implementation assumes that the packets arrive in the correct order.
	if ( ulPacketNum == 0 )
	{
		g_MasterServerBans.clear( );
		g_MasterServerBanExemptions.clear( );
	}

	while ( 1 )
	{
		const LONG lCommand = pByteStream->ReadByte();

		// [BB] End of packet (shouldn't be triggered for proper packets).
		if ( lCommand == -1 )
			break;

		switch ( lCommand )
		{
		case MSB_BAN:
		case MSB_BANEXEMPTION:
			{
				const char *pszBan = pByteStream->ReadString();
				std::string Message;

				if ( lCommand == MSB_BAN )
					g_MasterServerBans.addEntry( pszBan, "", "", Message, 0 );
				else
					g_MasterServerBanExemptions.addEntry( pszBan, "", "", Message, 0 );
			}
			break;

		case MSB_ENDBANLISTPART:
			return;

		case MSB_ENDBANLIST:
			{
				// [BB] If we are enforcing the master bans, make sure newly master bannded players are kicked now.
				if ( sv_enforcemasterbanlist )
					serverban_KickBannedPlayers( );

				// [BB] Inform the master that we received the banlist.
				SERVER_MASTER_SendBanlistReceipt();
			}
			return;
		}
	}
}
//*****************************************************************************
//
// Parses the given ban expiration string, returnining either the time_t, NULL for infinite, or -1 for an error.
time_t SERVERBAN_ParseBanLength( const char *szLengthString )
{
	time_t	tExpiration;
	time_t	tNow;

	FString	fInput = szLengthString;	

	// If the ban is permanent, use NULL.
	// [Dusk] Can't use NULL for time_t...
	if ( stricmp( szLengthString, "perm" ) == 0 )
		return 0;
	else
	{
		time( &tNow );
		tExpiration = 0;

		// Now we check for patterns in the string.

		// Minutes: covers "min", "minute", "minutes".
		if ( !tExpiration )
			tExpiration = serverban_CreateBanDate( serverban_ExtractBanLength( fInput, "min" ), MINUTE, tNow );

		// Hours: covers "hour", "hours".
		if ( !tExpiration )
			tExpiration = serverban_CreateBanDate( serverban_ExtractBanLength( fInput, "hour" ), HOUR, tNow );

		// Hours: covers "hr", "hrs".
		if ( !tExpiration )
			tExpiration = serverban_CreateBanDate( serverban_ExtractBanLength( fInput, "hr" ), HOUR, tNow );

		// Days: covers "day", "days".
		if ( !tExpiration )
			tExpiration = serverban_CreateBanDate( serverban_ExtractBanLength( fInput, "day" ), DAY, tNow );

		// Days: covers "dy", "dys".
		if ( !tExpiration )
			tExpiration = serverban_CreateBanDate( serverban_ExtractBanLength( fInput, "dy" ), DAY, tNow );

		// Weeks: covers "week", "weeks".
		if ( !tExpiration )
			tExpiration = serverban_CreateBanDate( serverban_ExtractBanLength( fInput, "week" ), WEEK, tNow );

		// Weeks: covers "wk", "wks".
		if ( !tExpiration )
			tExpiration = serverban_CreateBanDate( serverban_ExtractBanLength( fInput, "wk" ), WEEK, tNow );

		// Months work a bit differently, since we don't have an arbitrary number of days to move.
		if ( !tExpiration )
		{
			LONG lAmount = serverban_ExtractBanLength( fInput, "mon" );

			if ( lAmount > 0 )
			{
				// Create a new time structure based on the current time.
				struct tm	*pTimeInfo = localtime( &tNow );

				// Move the month forward, and stitch it into a new time.
				pTimeInfo->tm_mon += lAmount;
				tExpiration = mktime( pTimeInfo );
			}
		}

		// So do years (because of leap years).
		if ( !tExpiration )
		{
			LONG lAmount = serverban_ExtractBanLength( fInput, "year" );
			if ( lAmount <= 0 )
				lAmount = serverban_ExtractBanLength( fInput, "yr" );
			if ( lAmount <= 0 )
				lAmount = serverban_ExtractBanLength( fInput, "decade" ) * 10; // :)

			if ( lAmount > 0 )
			{
				// Create a new time structure based on the current time.
				struct tm	*pTimeInfo = localtime( &tNow );

				// Move the year forward, and stitch it into a new time.
				pTimeInfo->tm_year += lAmount;
				tExpiration = mktime( pTimeInfo );
			}
		}
	}

	if ( !tExpiration )
		return -1;

	return tExpiration;
}

//*****************************************************************************
//
TArray<IPList> &SERVERBAN_GetBanList( void )
{
	return g_ServerBans;
}

//*****************************************************************************
//
void SERVERBAN_BanPlayer( unsigned int player, const char *length, const char *reason, unsigned int fileIndex )
{
	// Make sure the target is valid and applicable.
	if (( player >= MAXPLAYERS ) || ( !playeringame[player] ) || players[player].bIsBot )
	{
		Printf("Error: bad player index, or player is a bot.\n");
		return;
	}

	SERVERBAN_BanAddress( SERVER_GetClient( player )->Address.ToString( ), length, reason, fileIndex );
}

//*****************************************************************************
//
void SERVERBAN_BanAddress( const char *address, const char *length, const char *reason, unsigned int fileIndex )
{
	// [AK] Added sanity checks to ensure the address and ban length are valid.
	if (( address == nullptr ) || ( length == nullptr ))
		return;

	// [RC] Read the ban length.
	const time_t expiration = SERVERBAN_ParseBanLength( length );
	std::string message;

	if ( expiration == -1 )
	{
		Printf( "Error: couldn't read that length. Try something like " TEXTCOLOR_RED "6day" TEXTCOLOR_NORMAL " or " TEXTCOLOR_RED "\"5 hours\"" TEXTCOLOR_NORMAL ".\n" );
		return;
	}
	else if ( fileIndex >= g_ServerBans.Size( ))
	{
		Printf( "Error: file index is invalid.\n" );
		return;
	}

	// [AK] Get the index of the player who has this address.
	NETADDRESS_s convertedAddress;
	const int player = convertedAddress.LoadFromString( address ) ? SERVER_FindClientByAddress( convertedAddress ) : -1;

	// Add the ban.
	if ( player != -1 )
	{
		// Removes the color codes from the player name, for the ban record.
		FString playerName = players[player].userinfo.GetName( );
		V_RemoveColorCodes( playerName );

		g_ServerBans[fileIndex].addEntry( address, playerName.GetChars( ), reason, message, expiration );
	}
	else
	{
		g_ServerBans[fileIndex].addEntry( address, nullptr, reason, message, expiration );
	}

	Printf( "addban: %s", message.c_str( ));

	// Kick the player.
	// [RC] serverban_KickBannedPlayers would cover this, but we want the
	// messages to be distinct so there's no confusion.
	if ( player != -1 )
		SERVER_KickPlayer( player, reason != nullptr ? reason : "" );

	// Kick any players using the newly-banned address.
	serverban_KickBannedPlayers( );
}

//*****************************************************************************
//
void SERVERBAN_UpdateBansFromServerConsole( const TArray<IPList> &lists )
{
	g_ServerConsoleBanUpdates = lists;
}

//*****************************************************************************
//
static void serverban_LoadFilesFromCVar( TArray<IPList> &lists, FStringCVar &cvar )
{
	FString cvarValue = cvar.GetGenericRep( CVAR_String ).String;
	FString filename;

	// [AK] The CVar's value should never be empty. At least one banfile is
	// needed, so revert back to its default value if this happens.
	if ( cvarValue.Len( ) == 0 )
	{
		const UCVarValue defaultVal = cvar.GetGenericRepDefault( CVAR_String );

		Printf( "No filename(s) provided for \"%s\". Reverting to \"%s\" instead.", cvar.GetName( ), defaultVal.String );
		cvar.SetGenericRep( defaultVal, CVAR_String );
		return;
	}

	lists.Clear( );

	for ( unsigned int i = 0; i <= cvarValue.Len( ); i++ )
	{
		if (( i < cvarValue.Len( )) && ( cvarValue[i] != ';' ))
		{
			filename += cvarValue[i];
		}
		else if ( filename.Len( ) > 0 )
		{
			lists.Push( IPList( ));

			if ( lists.Last( ).clearAndLoadFromFile( filename.GetChars( )) == false )
				Printf( "%s", lists.Last( ).getErrorMessage( ));

			filename.Truncate( 0 );
		}
	}
}

//*****************************************************************************
//
static void serverban_LoadBansAndBanExemptions( void )
{
	serverban_LoadFilesFromCVar( g_ServerBans, sv_banfile );
	serverban_LoadFilesFromCVar( g_ServerBanExemptions, sv_banexemptionfile );

	// Kick any players using a banned address.
	serverban_KickBannedPlayers( );
}

//*****************************************************************************
//
// [RC] Refresher method. Kicks any players who are playing under a banned IP.
//
static void serverban_KickBannedPlayers( void )
{
	for ( unsigned int i = 0; i < MAXPLAYERS; i++ )
	{
		if ( SERVER_GetClient( i )->State == CLS_FREE )
			continue;

		if ( SERVERBAN_IsIPBanned( SERVER_GetClient( i )->Address ))
		{
			IPADDRESSBAN_s *entry = SERVERBAN_GetBanInformation( SERVER_GetClient( i )->Address );
			FString reason = "IP is now banned";

			// [AK] Find an entry comment that corresponds to the player's IP
			// address, and include it with the ban reason.
			if (( entry != nullptr ) && ( strlen( entry->szComment ) != 0 ))
				reason.AppendFormat( " - %s", entry->szComment );

			SERVER_KickPlayer( i, reason.GetChars( ));
		}
	}
}

//*****************************************************************************
//
// [RC] Helper method for serverban_ParseLength.
static LONG serverban_ExtractBanLength( FString fSearchString, const char *pszPattern )
{
	// Look for the pattern (e.g, "min").
	LONG lIndex = fSearchString.IndexOf( pszPattern );

	if ( lIndex > 0 )
	{
		// Extract the number preceding it ("45min" becomes 45).
		return atoi( fSearchString.Left( lIndex ));
	}
	else
		return 0;
}

//*****************************************************************************
//
// [RC] Helper method for serverban_ParseLength.
static time_t serverban_CreateBanDate( LONG lAmount, ULONG ulUnitSize, time_t tNow )
{
	// Convert to a time in the future (45 MINUTEs becomes 2,700 seconds).
	if ( lAmount > 0 )
		return tNow + ulUnitSize * lAmount;

	// Not found, or bad format.
	else
		return 0;
}

//*****************************************************************************
//
// [AK] Helper function for executing the "getIP" and "getIP_idx" CCMDs.
//
static void serverban_ExecuteGetIPCmd( FCommandLine &argv, bool isIndexCmd )
{
	int playerIndex = MAXPLAYERS;

	// [AK] This function may not be used by ConsoleCommand.
	if ( ACS_IsCalledFromConsoleCommand( ))
		return;

	// Only the server can look this up.
	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
		return;

	if ( argv.argc( ) < 2 )
	{
		FString message;
		message.Format( "Usage: %s <player %s>\nDescription: Returns the player's IP address", argv[0], isIndexCmd ? "index" : "name" );

		// [AK] Add extra information for the index version of the command.
		if ( isIndexCmd )
			message.AppendFormat( ", via their index. You can get the list of players' indexes via the \"playerinfo\" CCMD" );

		Printf( "%s.\n", message.GetChars( ));
		return;
	}

	// Look up the player, and make sure they're valid.
	if ( argv.GetPlayerFromArg( playerIndex, 1, isIndexCmd, true ))
		Printf( "%s's IP is: %s\n", players[playerIndex].userinfo.GetName( ), SERVER_GetClient( playerIndex )->Address.ToString( ));
}

//*****************************************************************************
//
// [AK] Helper function for executing the "ban" and "ban_idx" CCMDs.
//
static void serverban_ExecuteBanCmd( FCommandLine &argv, bool isIndexCmd )
{
	int playerIndex = MAXPLAYERS;

	// [AK] This function may not be used by ConsoleCommand.
	if ( ACS_IsCalledFromConsoleCommand( ))
		return;

	// Only the server can ban players!
	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
		return;

	if ( argv.argc( ) < 3 )
	{
		FString message;
		message.Format( "Usage: %s <player %s> <duration> [reason] [file index]\nDescription: Bans the player", argv[0], isIndexCmd ? "index" : "name" );

		if ( isIndexCmd )
			message += ", via their index,";

		message += " for the given duration (\"perm\" for a permanent ban). ";

		if ( isIndexCmd )
			message += "To see the list of players' indexes, try the \"playerinfo\" CCMD. ";

		serverban_RetrieveFileIndices( g_ServerBans, message );
		Printf( "%s", message.GetChars( ));
		return;
	}

	// Look up the player, and make sure they're valid.
	if ( argv.GetPlayerFromArg( playerIndex, 1, isIndexCmd, true ))
		SERVERBAN_BanPlayer( playerIndex, argv[2], ( argv.argc( ) >= 4 ) ? argv[3] : nullptr, ( argv.argc( ) >= 5 ) ? atoi( argv[4] ) : 0 );
}

//*****************************************************************************
//
// [AK] Helper function for executing the "delban", "addbanexemption" and "delbanexemption" CCMDs.
// Note that the "addban" CCMD works differently, so this can't be used for it.
//
static void serverban_ExecuteAddOrDelBanCmd( TArray<IPList> &lists, FCommandLine &argv, bool isDelCmd )
{
	std::string message;

	// [AK] This function may not be used by ConsoleCommand.
	if ( ACS_IsCalledFromConsoleCommand( ))
		return;

	if ( argv.argc( ) < 2 )
	{
		FString message;
		message.Format( "Usage: %s <IP address>%s [file index]\n", argv[0], isDelCmd ? "" : " [comment]" );

		serverban_RetrieveFileIndices( lists, message );
		Printf( "%s", message.GetChars( ));
		return;
	}

	const int fileIndexArg = isDelCmd ? 2 : 3;
	const unsigned int fileIndex = ( argv.argc( ) >= fileIndexArg + 1 ) ? atoi( argv[fileIndexArg] ) : 0;

	if ( fileIndex >= lists.Size( ))
	{
		Printf( "Error: file index is invalid.\n" );
		return;
	}

	if ( isDelCmd )
		lists[fileIndex].removeEntry( argv[1], message );
	else
		lists[fileIndex].addEntry( argv[1], nullptr, ( argv.argc( ) >= 3 ) ? argv[2] : nullptr, message, 0 );

	Printf( "%s: %s", argv[0], message.c_str( ));
}

//*****************************************************************************
//
// [AK] Helper function for listing addresses via CCMDs (e.g. "viewbanlist").
//
static void serverban_ListAddresses( const IPList &list )
{
	for ( unsigned int i = 0; i < list.size( ); i++ )
		Printf( "%s", list.getEntryAsString( i ).c_str( ));
}

//*****************************************************************************
//
// [AK] Helper function for listing addresses from multiple files.
//
static void serverban_ListFilesAndAddresses( const TArray<IPList> &lists )
{
	for ( unsigned int i = 0; i < lists.Size( ); i++ )
	{
		if ( lists[i].size( ) == 0 )
			continue;

		// [AK] Print the name of the file too.
		Printf( "From \"%s\": \n", lists[i].getFilename( ));
		serverban_ListAddresses( lists[i] );
	}
}

//*****************************************************************************
//
// [AK] Helper function for listing ban (exemption) files indices.
//
static void serverban_RetrieveFileIndices( const TArray<IPList> &lists, FString &string )
{
	if ( lists.Size( ) == 0 )
	{
		string += "No files are available\n";
	}
	else
	{
		string += "File indices are:\n";

		for ( unsigned int i = 0; i < lists.Size( ); i++ )
			string.AppendFormat( "%u. %s%s\n", i, lists[i].getFilename( ), i == 0 ? " (default)" : "" );
	}
}

//--------------------------------------------------------------------------------------------------------------------------------------------------
//-- CCMDS -----------------------------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------

CCMD( getIP )
{
	serverban_ExecuteGetIPCmd( argv, false );
}

//*****************************************************************************
//
CCMD( getIP_idx )
{
	serverban_ExecuteGetIPCmd( argv, true );
}

//*****************************************************************************
//
CCMD( ban )
{
	serverban_ExecuteBanCmd( argv, false );
}

//*****************************************************************************
//
CCMD( ban_idx )
{
	serverban_ExecuteBanCmd( argv, true );
}

//*****************************************************************************
//
CCMD( addban )
{
	// [AK] This function may not be used by ConsoleCommand.
	if ( ACS_IsCalledFromConsoleCommand( ))
		return;

	if ( argv.argc( ) < 3 )
	{
		FString message = "Usage: addban <IP address> <duration> [comment] [file index]\nDescription: bans the given IP address. ";
		serverban_RetrieveFileIndices( g_ServerBans, message );

		Printf( "%s", message.GetChars( ));
		return;
	}

	SERVERBAN_BanAddress( argv[1], argv[2], ( argv.argc( ) >= 4 ) ? argv[3] : nullptr, ( argv.argc( ) >= 5 ) ? atoi( argv[4] ) : 0 );
}

//*****************************************************************************
//
CCMD( delban )
{
	serverban_ExecuteAddOrDelBanCmd( g_ServerBans, argv, true );
}

//*****************************************************************************
//
CCMD( addbanexemption )
{
	serverban_ExecuteAddOrDelBanCmd( g_ServerBanExemptions, argv, false );
}

//*****************************************************************************
//
CCMD( delbanexemption )
{
	serverban_ExecuteAddOrDelBanCmd( g_ServerBanExemptions, argv, true );
}

//*****************************************************************************
//
CCMD( viewbanlist )
{
	serverban_ListFilesAndAddresses( g_ServerBans );
}

//*****************************************************************************
//
CCMD( viewbanexemptionlist )
{
	serverban_ListFilesAndAddresses( g_ServerBanExemptions );
}

//*****************************************************************************
//
CCMD( viewmasterbanlist )
{
	serverban_ListAddresses( g_MasterServerBans );
}

//*****************************************************************************
//
CCMD( viewmasterexemptionbanlist )
{
	serverban_ListAddresses( g_MasterServerBanExemptions );
}

//*****************************************************************************
//
CCMD( clearbans )
{
	// [AK] This function may not be used by ConsoleCommand.
	if ( ACS_IsCalledFromConsoleCommand( ))
		return;

	SERVERBAN_ClearBans(( argv.argc( ) >= 2 ) ? atoi( argv[1] ) : 0 );
}

//*****************************************************************************
//
CCMD( reloadbans )
{
	// [AK] This function may not be used by ConsoleCommand.
	if ( ACS_IsCalledFromConsoleCommand( ))
		return;

	serverban_LoadBansAndBanExemptions( );
}
