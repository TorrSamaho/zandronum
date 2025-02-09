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
// Filename: network.cpp
//
// Description: Contains network definitions and functions not specifically
// related to the server or client.
//
//-----------------------------------------------------------------------------

#include "networkheaders.h"

// [BB] Special things necessary for NETWORK_GetLocalAddress() under Linux.
#ifdef __unix__
#include <net/if.h>
#define inaddrr(x) (*(struct in_addr *) &ifr->x[sizeof sa.sin_port])
#define IFRSIZE   ((int)(size * sizeof (struct ifreq)))
#ifdef __FreeBSD__
#include <machine/param.h>
#endif
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <ctype.h>
#include <math.h>
#include <set>
#include <vector>
#include <string>
#include "../GeoIP/GeoIP.h"

#include "c_console.h"
#include "c_dispatch.h"
#include "cl_demo.h"
#include "cl_main.h"
#include "doomtype.h"
#include "huffman.h"
#include "i_system.h"
#include "sv_main.h"
#include "m_random.h"
#include "network.h"
#include "sbar.h"
#include "v_video.h"
#include "version.h"
#include "g_level.h"
#include "p_lnspec.h"
#include "cmdlib.h"
#include "templates.h"
#include "p_acs.h"
#include "d_netinf.h"

#include "md5.h"
#include "network/sv_auth.h"
#include "doomerrors.h"

enum LumpAuthenticationMode {
	LAST_LUMP,
	ALL_LUMPS
};

// [AK] A structure for initializing lumps that are going to be authenticated.
struct AUTHENTICATELUMP_s
{
	std::string Name;
	LumpAuthenticationMode Mode;
	namespace_t NameSpace;

	// [AK] Determines what namespace is designated to this lump and returns a string of the namespace. By default,
	// the namespace is global, in case an invalid namespace is provided.
	FString GetNameSpace( FScanner &sc )
	{
		NameSpace = ns_global;
		sc.MustGetString( );

		if ( stricmp( sc.String, "sprite" ) == 0 )
			NameSpace = ns_sprites;
		else if ( stricmp( sc.String, "flat" ) == 0 )
			NameSpace = ns_flats;
		else if ( stricmp( sc.String, "texture" ) == 0 )
			NameSpace = ns_newtextures;
		else if ( stricmp( sc.String, "hires" ) == 0 )
			NameSpace = ns_hires;
		else if ( stricmp( sc.String, "voxel" ) == 0 )
			NameSpace = ns_voxels;
		else if ( stricmp( sc.String, "sound" ) == 0 )
			NameSpace = ns_sounds;
		else if ( stricmp( sc.String, "patch" ) == 0 )
			NameSpace = ns_patches;
		else if ( stricmp( sc.String, "graphics" ) == 0 )
			NameSpace = ns_graphics;
		else if ( stricmp( sc.String, "music" ) == 0 )
			NameSpace = ns_music;
		else if ( stricmp( sc.String, "global" ) != 0 )
			sc.ScriptMessage( "Invalid namespace \"%s\" was declared, switching to global instead.", sc.String );

		return ( NameSpace != ns_global ? sc.String : "global" );
	}

	// [AK] Comparison function, necessary for std::set. We're only concerned about a lump's name
	// and namespace. The authentication mode (LAST_LUMP or ALL_LUMPS) isn't really important to check.
	bool operator< ( const AUTHENTICATELUMP_s &Other ) const
	{
		if ( Name == Other.Name )
			return ( NameSpace < Other.NameSpace );

		return ( Name < Other.Name );
	}
};

// [BB] Implement the string table and the conversion functions for the SVC and SVC2 enums.
#include "network_enums.h"
#define GENERATE_ENUM_STRINGS  // Start string generation
#include "network_enums.h"
#undef GENERATE_ENUM_STRINGS   // Stop string generation

void SERVERCONSOLE_UpdateIP( NETADDRESS_s LocalAddress );

//*****************************************************************************
//	VARIABLES

static	TArray<NetworkPWAD>	g_PWADs;
static	TArray<NetworkPWAD>	g_AuthenticatedWADs; // [SB] All authenticated WAD files, including IWAD and engine PK3
static	FString		g_IWAD; // [RC/BB] Which IWAD are we using?

FString g_lumpsAuthenticationChecksum;
FString g_MapCollectionChecksum;

static TArray<LONG> g_LumpNumsToAuthenticate ( 0 );

// The current network state. Single player, client, server, etc.
static	LONG			g_lNetworkState = NETSTATE_SINGLE;

// Buffer that holds the data from the most recently received packet.
static	NETBUFFER_s		g_NetworkMessage;

// Network address that the most recently received packet came from.
static	NETADDRESS_s	g_AddressFrom;

// Our network socket.
static	SOCKET			g_NetworkSocket;

// Socket for listening for LAN games.
static	SOCKET			g_LANSocket;
// [BB] Did binding the LAN socket fail?
static	bool				g_bLANSocketInvalid = false;

// Our local port.
static	USHORT			g_usLocalPort;

// Buffer for the Huffman encoding.
static	UCHAR			g_ucHuffmanBuffer[131072];

// Our local address;
NETADDRESS_s	g_LocalAddress;

// [BB]
static	TArray<const PClass*> g_ActorNetworkIndexClassPointerMap;

// [BB]
static GeoIP * g_GeoIPDB = NULL;

// [BB]
extern int restart;

// [AK] Did we need to authenticate a lump that has a duplicate?
static bool g_bDuplicateLumpAuthenticated = false;

// [TP] Named ACS scripts share the name pool with all other names in the engine, which means named script numbers may
// differ wildly between systems, e.g. if the server and client have different vid_renderer values the names will
// already be off. So we create a special index of script names here.
static TArray<FName> g_ACSNameIndex;

// [SB] Hashes for Freedoom lumps that must be detected for Doom network compatibility.
static const std::vector<std::string> g_FreedoomPlayPalHashes = {
	"2e01ae6258f2a0fdad32125537efe1af", // Freedoom PLAYPAL hash
	"4804c7f34b5285c334a7913dd98fae16", // Freedoom 0.8-beta1 PLAYPAL hash
	"2e01ae6258f2a0fdad32125537efe1af", // Freedoom 0.11.3 PLAYPAL hash
	"7fe3ed884aff7774526ed9b61018f6fe", // Freedoom 0.12.0 PLAYPAL hash
	"c7ac0dbbebc979a2c948a4c9afd6b4af", // Freedoom 0.13.0 PLAYPAL hash
};

static const std::vector<std::string> g_FreedoomColormapHashes = {
	"bb535e66cae508e3833a5d2de974267b", // Freedoom COLORMAP hash
	"100c2c81afe87bb6dd1dbcadee9a7e58", // Freedoom 0.8-beta1 COLORMAP hash
	"4c7d4028a88f7929d9c553f65bb265ba", // Freedoom 0.9 COLORMAP hash
	"90d4527e1836e373f1cc6f2c9d5e3ba3", // Freedoom 0.12.0 COLORMAP hash
};

static const std::vector<std::string> g_FreedoomDehackedHashes = {
	"3c48ccc87e71d791ee3df64668b3fb42", // Freedoom 0.8-beta1
	"9de9ddd0bc435cb8572db76a13d3140f", // Freedoom 0.8
	"90e9007b1efc1e35eeacc99c5971a15b", // Freedoom 0.9
	"67b253fe502cbf269e2cd2f6b7e76f17", // Freedoom 0.10
	"61f49a1c915c7ccaea016b51441bef1d", // Freedoom 0.11.3
	"4004b707e3bbf28fe58a6d8282784fbc", // Freedoom Phase 1 0.12.0
	"a87016a0610d8023e6fdae013c8c001c", // Freedoom Phase 2 0.12.0
	"015a23f11718e5bc2fab7f3d6f946743", // Freedoom Phase 1 0.13.0
	"0fe45773c7b9eefd1ca07f1f89d34b76", // Freedoom Phase 2 0.13.0
};

//*****************************************************************************
//	PROTOTYPES

static	void			network_InitPWADList( void );
static	void			network_Error( const char *pszError );
static	SOCKET			network_AllocateSocket( void );
static	bool			network_BindSocketToPort( SOCKET Socket, ULONG ulInAddr, USHORT usPort, bool bReUse );
static	bool			network_GenerateLumpMD5HashAndWarnIfNeeded( const int LumpNum, const char *LumpName, FString &MD5Hash );
static	void			network_CheckIfDuplicateLump( const int LumpNum ); // [AK]
static	void			network_AddSpritesToList( std::set<AUTHENTICATELUMP_s> &list, const char *name, const std::set<char> frames, const LumpAuthenticationMode mode ); // [AK]
static	void			network_ParseLumpAuthenticationMode( FScanner &sc, LumpAuthenticationMode &mode );

//*****************************************************************************
//	FUNCTIONS

void NETWORK_Construct( USHORT usPort, bool bAllocateLANSocket )
{
	char			szString[128];
	ULONG			ulArg;
	USHORT			usNewPort;
	bool			bSuccess;

	// Initialize the Huffman buffer.
	HUFFMAN_Construct( );

	if ( !restart )
	{
#ifdef __WIN32__
		// [BB] Linux doesn't know WSADATA, so this may not be moved outside the ifdef.
		WSADATA			WSAData;
		if ( WSAStartup( 0x0101, &WSAData ))
			network_Error( "Winsock initialization failed!\n" );

		Printf( "Winsock initialization succeeded!\n" );
#endif

		ULONG ulInAddr = INADDR_ANY;
		const char* pszIPAddress = Args->CheckValue( "-useip" );
		// [BB] An IP was specfied. Check if it's valid and if it is, try to bind our socket to it.
		if ( pszIPAddress )
		{
			ULONG requestedIP = inet_addr( pszIPAddress );
			if ( requestedIP == INADDR_NONE )
			{
				sprintf( szString, "NETWORK_Construct: %s is not a valid IP address\n", pszIPAddress );
				network_Error( szString );
			}
			else
				ulInAddr = requestedIP;
		}

		g_usLocalPort = usPort;

		// Allocate a socket, and attempt to bind it to the given port.
		g_NetworkSocket = network_AllocateSocket( );
		// [BB] If we can't allocate a socket, sending / receiving net packets won't work.
		if ( g_NetworkSocket == INVALID_SOCKET )
			network_Error( "NETWORK_Construct: Couldn't allocate socket. You will not be able to host or join servers.\n" );
		else if ( network_BindSocketToPort( g_NetworkSocket, ulInAddr, g_usLocalPort, false ) == false )
		{
			bSuccess = true;
			bool bSuccessIP = true;
			usNewPort = g_usLocalPort;
			while ( network_BindSocketToPort( g_NetworkSocket, ulInAddr, ++usNewPort, false ) == false )
			{
				// Didn't find an available port. Oh well...
				if ( usNewPort == g_usLocalPort )
				{
					// [BB] We couldn't use the specified IP, so just try any.
					if ( ulInAddr != INADDR_ANY )
					{
						ulInAddr = INADDR_ANY;
						bSuccessIP = false;
						continue;
					}
					bSuccess = false;
					break;
				}
			}

			if ( bSuccess == false )
			{
				sprintf( szString, "NETWORK_Construct: Couldn't bind socket to port: %d\n", g_usLocalPort );
				network_Error( szString );
			}
			else if ( bSuccessIP == false )
			{
				sprintf( szString, "NETWORK_Construct: Couldn't bind socket to IP %s, using the default IP instead:\n", pszIPAddress );
				network_Error( szString );
			}
			else
			{
				Printf( "NETWORK_Construct: Couldn't bind to %d. Binding to %d instead...\n", g_usLocalPort, usNewPort );
				g_usLocalPort = usNewPort;
			}
		}

		ulArg = true;
		if ( ioctlsocket( g_NetworkSocket, FIONBIO, &ulArg ) == -1 )
			printf( "network_AllocateSocket: ioctl FIONBIO: %s", strerror( errno ));

		// If we're not starting a server, setup a socket to listen for LAN servers.
		if ( bAllocateLANSocket )
		{
			g_LANSocket = network_AllocateSocket( );
			if ( network_BindSocketToPort( g_LANSocket, ulInAddr, DEFAULT_BROADCAST_PORT, true ) == false )
			{
				sprintf( szString, "network_BindSocketToPort: Couldn't bind LAN socket to port: %d. You will not be able to see LAN servers in the browser.", DEFAULT_BROADCAST_PORT );
				network_Error( szString );
				// [BB] The socket won't work in this case, make sure not to use it.
				g_bLANSocketInvalid = true;
			}

			if ( ioctlsocket( g_LANSocket, FIONBIO, &ulArg ) == -1 )
				printf( "network_AllocateSocket: ioctl FIONBIO: %s", strerror( errno ));
		}

		// [BB] Get and save our local IP.
		if ( ( ulInAddr == INADDR_ANY ) || ( pszIPAddress == NULL ) )
			g_LocalAddress = NETWORK_GetLocalAddress( );
		// [BB] We are using a specified IP, so we don't need to figure out what IP we have, but just use the specified one.
		else
		{
			g_LocalAddress.LoadFromString( pszIPAddress );
			g_LocalAddress.usPort = htons ( NETWORK_GetLocalPort() );
		}

		// Print out our local IP address.
		Printf( "IP address %s\n", g_LocalAddress.ToString() );
	}

	// Init our read buffer.
	// [BB] Vortex Cortex pointed us to the fact that the smallest huffman code is only 3 bits
	// and it turns into 8 bits when it's decompressed. Thus we need to allocate a buffer that
	// can hold the biggest possible size we may get after decompressing (aka Huffman decoding)
	// the incoming UDP packet.
	g_NetworkMessage.Init( ((MAX_UDP_PACKET * 8) / 3 + 1), BUFFERTYPE_READ );
	g_NetworkMessage.Clear();

	// If hosting, update the server GUI.
	if( NETWORK_GetState() == NETSTATE_SERVER )
		SERVERCONSOLE_UpdateIP( g_LocalAddress );

	// [BB] Initialize the checksum of the non-map lumps that need to be authenticated when connecting a new player.
	// [AK] This is also a list of lumps that cannot be modified by AUTHINFO.
	std::set<AUTHENTICATELUMP_s> lumpsToAuthenticate = {
		{ "COLORMAP", LAST_LUMP, ns_global },
		{ "PLAYPAL", LAST_LUMP, ns_global },
		{ "HTICDEFS", ALL_LUMPS, ns_global },
		{ "HEXNDEFS", ALL_LUMPS, ns_global },
		{ "STRFDEFS", ALL_LUMPS, ns_global },
		{ "DOOMDEFS", ALL_LUMPS, ns_global },
		{ "GLDEFS", ALL_LUMPS, ns_global },
		{ "DECORATE", ALL_LUMPS, ns_global },
		{ "LOADACS", ALL_LUMPS, ns_global },
		{ "DEHACKED", ALL_LUMPS, ns_global },
		{ "GAMEMODE", ALL_LUMPS, ns_global },
		{ "MAPINFO", ALL_LUMPS, ns_global },
		{ "AUTHINFO", ALL_LUMPS, ns_global },
		{ "VOTEINFO", ALL_LUMPS, ns_global },
		{ "MEDALDEF", ALL_LUMPS, ns_global }
	};

	// [AK] Add all "ALLYA" and "ENEMA" sprites to the authentication list.
	network_AddSpritesToList( lumpsToAuthenticate, "ALLY", { 'A' }, LAST_LUMP );
	network_AddSpritesToList( lumpsToAuthenticate, "ENEM", { 'A' }, LAST_LUMP );

	// [AK] Parse any loaded AUTHINFO lumps, which might add new lumps to the authentication list.
	if ( Wads.CheckNumForName( "AUTHINFO" ) != -1 )
	{
		std::set<AUTHENTICATELUMP_s> customLumpsToAuthenticate;
		int currentLump, lastLump = 0;

		Printf( "Authenticating custom protected lumps.\n" );

		while (( currentLump = Wads.FindLump( "AUTHINFO", &lastLump )) != -1 )
		{
			FScanner sc( currentLump );

			while ( sc.GetString( ) )
			{
				std::set<AUTHENTICATELUMP_s> parsedLumps;
				FString nameSpaceText;
				bool adding = false;

				if ( stricmp( sc.String, "clearlumps" ) == 0 )
				{
					customLumpsToAuthenticate.clear( );
					continue;
				}
				else if (( stricmp( sc.String, "addlump" ) == 0 ) || ( stricmp( sc.String, "removelump" ) == 0 ))
				{
					AUTHENTICATELUMP_s authenticatingLump;

					adding = ( stricmp( sc.String, "addlump" ) == 0 );
					nameSpaceText = authenticatingLump.GetNameSpace( sc );

					sc.MustGetString( );
					authenticatingLump.Name = sc.String;

					// [AK] Parse the authentication mode if adding a new lump.
					if ( adding )
						network_ParseLumpAuthenticationMode( sc, authenticatingLump.Mode );

					parsedLumps.insert( authenticatingLump );
				}
				else if (( stricmp( sc.String, "addsprites" ) == 0 ) || ( stricmp( sc.String, "removesprites" ) == 0 ))
				{
					std::set<char> frames;
					LumpAuthenticationMode mode = LAST_LUMP;

					adding = ( stricmp( sc.String, "addsprites" ) == 0 );
					nameSpaceText = "sprite";

					sc.MustGetString( );

					// [AK] The sprite's name must be four characters long.
					if ( sc.StringLen != 4 )
						sc.ScriptError( "Invalid sprite name \"%s\". It must be 4 characters long.", sc.String );

					const FString spriteName = sc.String;
					sc.MustGetString( );

					// [AK] Parse the listed frames, unless "all" of them should be authenticated.
					if ( stricmp( sc.String, "all" ) != 0 )
					{
						for ( int i = 0; i < sc.StringLen; i++ )
						{
							sc.String[i] = toupper( sc.String[i] );

							if ( static_cast<unsigned>( sc.String[i] - 'A' ) >= MAX_SPRITE_FRAMES )
								sc.ScriptError( "Invalid sprite frame '%c'.", sc.String[i] );

							frames.insert( sc.String[i] );
						}
					}

					// [AK] Parse the authentication mode if adding new sprites.
					if ( adding )
						network_ParseLumpAuthenticationMode( sc, mode );

					network_AddSpritesToList( parsedLumps, spriteName.GetChars( ), frames, mode );
				}
				else
				{
					sc.ScriptError( "Unknown option '%s', on line %d in AUTHINFO.", sc.String, sc.Line );
				}

				for ( std::set<AUTHENTICATELUMP_s>::iterator it = parsedLumps.begin( ); it != parsedLumps.end( ); it++ )
				{
					// [AK] Engineside protected lumps like COLORMAP, PLAYPAL, DECORATE, etc. cannot be modified
					// or removed. Technically speaking, it shouldn't be possible to remove these lumps anyways
					// because they're in a separate list, but the user should at least be made aware of this.
					if ( lumpsToAuthenticate.find( *it ) != lumpsToAuthenticate.end( ))
					{
						sc.ScriptMessage( "\"%s\" is an engineside protected lump and cannot be %s.", it->Name.c_str( ), adding ? "modified" : "removed" );
						continue;
					}

					if ( adding )
					{
						auto result = customLumpsToAuthenticate.insert( *it );

						// [AK] If this lump is already on the list, just update the authentication mode (elements
						// within a set are constant, so the existing entry must be removed first, and the new entry
						// added in afterward). Also print a message to indicate that the lump was defined twice.
						if ( result.second == false )
						{
							customLumpsToAuthenticate.erase( *it );
							customLumpsToAuthenticate.insert( *it );

							sc.ScriptMessage( "\"%s\" in the %s namespace is already on the authentication list.", it->Name.c_str( ), nameSpaceText.GetChars( ));
						}
					}
					else
					{
						customLumpsToAuthenticate.erase( *it );
					}
				}
			}
		}

		// [AK] Append the new lumps to the authentication list.
		if ( customLumpsToAuthenticate.size( ) > 0 )
			lumpsToAuthenticate.insert( customLumpsToAuthenticate.begin( ), customLumpsToAuthenticate.end( ));
	}

	FString checksum, longChecksum;
	bool noProtectedLumpsAutoloaded = true;

	// [BB] All precompiled ACS libraries need to be authenticated. The only way to find all of them
	// at this point is to parse all LOADACS lumps.
	{
		int lump, lastlump = 0;
		while ((lump = Wads.FindLump ("LOADACS", &lastlump)) != -1)
		{
			FScanner sc(lump);
			while (sc.GetString())
			{
				NETWORK_AddLumpForAuthentication ( Wads.CheckNumForName (sc.String, ns_acslibrary) );
			}
		}
	}

	// [BB] First check the lumps that were marked for authentication while initializing. This
	// includes for example those lumps included by DECORATE lumps. It's much easier to mark those
	// lumps while the engine parses the DECORATE code than trying to find all included lumps from
	// the DECORATE lumps directly.
	for ( unsigned int i = 0; i < g_LumpNumsToAuthenticate.Size(); ++i )
	{
		if ( !network_GenerateLumpMD5HashAndWarnIfNeeded( g_LumpNumsToAuthenticate[i], Wads.GetLumpFullName (g_LumpNumsToAuthenticate[i]), checksum ) )
			noProtectedLumpsAutoloaded = false;
		longChecksum += checksum;

		// [TP] The wad that had this lump is no longer optional.
		Wads.LumpIsMandatory( g_LumpNumsToAuthenticate[i] );
	}

	for ( auto it = lumpsToAuthenticate.begin(); it != lumpsToAuthenticate.end(); it++ )
	{
		switch ( it->Mode ){
			case LAST_LUMP:
				int lump;

				// [AK] Check for the lump in the proper namespace, this isn't always limited to the global namespace.
				lump = Wads.CheckNumForName( it->Name.c_str(), it->NameSpace );

				// [BB] Possibly we find the COLORMAP lump only in the colormaps name space.
				if ( ( lump == -1 ) && ( it->Name.compare( "COLORMAP" ) == 0 ) )
					lump = Wads.CheckNumForName("COLORMAP", ns_colormaps);
				if ( lump == -1 )
				{
					Printf ( PRINT_BOLD, "Warning: Can't find lump %s for authentication!\n", it->Name.c_str() );
					continue;
				}
				if ( !network_GenerateLumpMD5HashAndWarnIfNeeded( lump, it->Name.c_str(), checksum ) )
					noProtectedLumpsAutoloaded = false;

				// [AK] Check if we're trying to authenticate a duplicate lump.
				network_CheckIfDuplicateLump( lump );

				// [TP] The wad that had this lump is no longer optional.
				Wads.LumpIsMandatory( lump );

				// [BB] To make Doom and Freedoom network compatible, substitue the Freedoom PLAYPAL/COLORMAP hash
				// by the corresponding Doom hash.
				// [SB] Use a list of the hashes instead of a long chain of conditions.
				// 4804c7f34b5285c334a7913dd98fae16 Doom PLAYPAL hash
				// 061a4c0f80aa8029f2c1bc12dc2e261e Doom COLORMAP hash
				if ( stricmp ( it->Name.c_str(), "PLAYPAL" ) == 0 && std::find( g_FreedoomPlayPalHashes.cbegin(), g_FreedoomPlayPalHashes.cend(), checksum.GetChars() ) != g_FreedoomPlayPalHashes.cend() )
					checksum = "4804c7f34b5285c334a7913dd98fae16";
				else if ( stricmp ( it->Name.c_str(), "COLORMAP" ) == 0 && std::find( g_FreedoomColormapHashes.cbegin(), g_FreedoomColormapHashes.cend(), checksum.GetChars() ) != g_FreedoomColormapHashes.cend() )
					checksum = "061a4c0f80aa8029f2c1bc12dc2e261e";

				longChecksum += checksum;
				break;

			case ALL_LUMPS:
				int workingLump, lastLump;
				lastLump = 0;

				// [AK] We need to find lumps across all namespaces, as this isn't always limited to the global namespace.
				while (( workingLump = Wads.FindLump( it->Name.c_str(), &lastLump, true )) != -1 )
				{
					// [AK] Skip this lump if it doesn't belong in the proper namespace.
					if ( Wads.GetLumpNamespace( workingLump ) != it->NameSpace )
						continue;

					if ( !network_GenerateLumpMD5HashAndWarnIfNeeded( workingLump, it->Name.c_str(), checksum ) )
						noProtectedLumpsAutoloaded = false;

					// [AK] Check if we're trying to authenticate a duplicate lump.
					network_CheckIfDuplicateLump( workingLump );

					// [BB] To make Doom and Freedoom network compatible, we need to ignore its DEHACKED lump.
					// Since this lump only changes some strings, this should cause no problems.
					// [SB] Use a list of the hashes instead of a long chain of conditions.
					if ( stricmp ( it->Name.c_str(), "DEHACKED" ) == 0 && std::find( g_FreedoomDehackedHashes.cbegin(), g_FreedoomDehackedHashes.cend(), checksum.GetChars() ) != g_FreedoomDehackedHashes.cend() )
						continue;

					// [TP] The wad that had this lump is no longer optional.
					Wads.LumpIsMandatory( workingLump );
					longChecksum += checksum;
				}
				break;
		}
	}
	CMD5Checksum::GetMD5( reinterpret_cast<const BYTE *>(longChecksum.GetChars()), longChecksum.Len(), g_lumpsAuthenticationChecksum );

	// [AK] If we needed to authenticate any duplicate lumps, either throw a fatal error if we're
	// the server, or just print a warning message that urges modders to fix them.
	if ( g_bDuplicateLumpAuthenticated )
	{
		FString message = "There are files containing duplicate lumps that require authentication. ";

		if ( NETWORK_GetState() == NETSTATE_SERVER )
		{
			message.AppendFormat( "Please resolve these issues before hosting again.\n" );
			I_FatalError( "%s", message.GetChars() );
		}

		message.AppendFormat( "These issues must be resolved if you plan on hosting these files in online games.\n" );
		Printf( TEXTCOLOR_RED "%s", message.GetChars() );
	}

	// [AK] We're done with authentication now, clear the duplicate lump lists to save memory.
	DuplicateLumps.Clear( );
	DuplicateLumpFilenames.Clear( );

	// [BB] Warn the user about problematic auto-loaded files.
	if ( noProtectedLumpsAutoloaded == false )
	{
		Printf ( PRINT_BOLD, "Warning: Above auto-loaded files contain protected lumps.\n" );
		if ( Args->CheckParm( "-host" ) )
			Printf ( PRINT_BOLD, "Clients without these files can't connect to this server.\n" );
		else
			Printf ( PRINT_BOLD, "You can't connect to servers that don't have these files loaded.\n" );
	}

	// [BB] Initialize the actor network class indices.
	g_ActorNetworkIndexClassPointerMap.Clear();
	for ( unsigned int i = 0; i < PClass::m_Types.Size(); i++ )
	{
		PClass* cls = PClass::m_Types[i];
		if ( (cls->IsDescendantOf(RUNTIME_CLASS(AActor)))
		     // [BB] The server only binaries don't know DynamicLight and derived classes.
		     && !(cls->IsDescendantOf(PClass::FindClass("DynamicLight"))) )
			cls->ActorNetworkIndex = 1 + g_ActorNetworkIndexClassPointerMap.Push ( cls );
		else
			cls->ActorNetworkIndex = 0;
	}

	// [BB] Initialize the GeoIP database.
	if( NETWORK_GetState() == NETSTATE_SERVER )
	{
#ifdef __unix__
		if ( FileExists ( "/usr/share/GeoIP/GeoIP.dat" ) )
		  g_GeoIPDB = GeoIP_open ( "/usr/share/GeoIP/GeoIP.dat", GEOIP_STANDARD );
		else if ( FileExists ( "/usr/local/share/GeoIP/GeoIP.dat" ) )
		  g_GeoIPDB = GeoIP_open ( "/usr/local/share/GeoIP/GeoIP.dat", GEOIP_STANDARD );
#endif
		if ( g_GeoIPDB == NULL )
			g_GeoIPDB = GeoIP_new ( GEOIP_STANDARD );
		if ( g_GeoIPDB != NULL )
			Printf( "GeoIP initialized.\n" );
		else
			Printf( "GeoIP initialization failed.\n" );
	}

	// [TP] Wads containing maps cannot be optional wads so check that now.
	for ( unsigned int i = 0; i < wadlevelinfos.Size(); i++ )
	{
		level_info_t& info = wadlevelinfos[i];
		MapData* mdata = NULL;

		// [TP] P_OpenMapData can throw an error in some cases with the cryptic error message
		// "'THINGS' not found in'. I don't think this is the case in recent ZDoom versions?
		try
		{
			if (( mdata = P_OpenMapData( info.mapname, false )) != NULL )
			{
				// [TP] The wad that had this map is no longer optional.
				Wads.LumpIsMandatory( mdata->lumpnum );
			}
		}
		catch ( CRecoverableError& e )
		{
			// [TP] Might as well warn the user now that we're here.
			Printf( "NETWORK_Construct: " TEXTCOLOR_RED "WARNING: Cannot open map %s: %s\n",
				info.mapname, e.GetMessage() );
		}

		delete mdata;
	}

	// [RC/BB] Init the list of PWADs.
	// [SB] Moved this here so that WADs containing maps are correctly marked as authenticated.
	network_InitPWADList( );

	// Call NETWORK_Destruct() when Skulltag closes.
	atterm( NETWORK_Destruct );

	Printf( "UDP Initialized.\n" );

	// [BB] Now that the network is initialized, set up what's necessary
	// to communicate with the authentication server.
	NETWORK_AUTH_Construct();
}

//*****************************************************************************
//
void NETWORK_Destruct( void )
{
	// Free the network message buffer.
	g_NetworkMessage.Free();

	// [BB] Delete the GeoIP database.
	GeoIP_delete ( g_GeoIPDB );
	g_GeoIPDB = NULL;

	// [BB] This needs to be cleared since we assume it to be empty during a restart.
	g_LumpNumsToAuthenticate.Clear();
}

//*****************************************************************************
//
int NETWORK_GetPackets( void )
{
	LONG				lNumBytes;
	INT					iDecodedNumBytes = sizeof(g_ucHuffmanBuffer);
	sockaddr			SocketFrom;
	INT					iSocketFromLength;

	iSocketFromLength = sizeof( SocketFrom );

	// [BB] If the socket is invalid, there is no point in trying to use it.
	if ( g_NetworkSocket == INVALID_SOCKET )
		return ( 0 );

#ifdef	WIN32
	lNumBytes = recvfrom( g_NetworkSocket, (char *)g_ucHuffmanBuffer, sizeof( g_ucHuffmanBuffer ), 0, &SocketFrom, &iSocketFromLength );
#else
	lNumBytes = recvfrom( g_NetworkSocket, (char *)g_ucHuffmanBuffer, sizeof( g_ucHuffmanBuffer ), 0, &SocketFrom, (socklen_t *)&iSocketFromLength );
#endif

	// If the number of bytes returned is -1, an error has occured.
	if ( lNumBytes == -1 ) 
	{ 
#ifdef __WIN32__
		errno = WSAGetLastError( );

		if ( errno == WSAEWOULDBLOCK )
			return ( false );

		// Connection reset by peer. Doesn't mean anything to the server.
		if ( errno == WSAECONNRESET )
			return ( false );

		if ( errno == WSAEMSGSIZE )
		{
			Printf( "NETWORK_GetPackets:  WARNING! Oversized packet from %s\n", g_AddressFrom.ToString() );
			return ( false );
		}

		Printf( "NETWORK_GetPackets: WARNING!: Error #%d: %s\n", errno, strerror( errno ));
		return ( false );
#else
		if ( errno == EWOULDBLOCK )
			return ( false );

		if ( errno == ECONNREFUSED )
			return ( false );

		Printf( "NETWORK_GetPackets: WARNING!: Error #%d: %s\n", errno, strerror( errno ));
		return ( false );
#endif
	}

	// No packets or an error, so don't process anything.
	if ( lNumBytes <= 0 )
		return ( 0 );

	// Record this for our statistics window.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVER_STATISTIC_AddToInboundDataTransfer( lNumBytes );

	// If the number of bytes we're receiving exceeds our buffer size, ignore the packet.
	if ( lNumBytes >= static_cast<LONG>(g_NetworkMessage.ulMaxSize) )
		return ( 0 );

	// Store the IP address of the sender.
	g_AddressFrom.LoadFromSocketAddress( SocketFrom );

	// Decode the huffman-encoded message we received.
	// [BB] Communication with the auth server is not Huffman-encoded.
	if ( g_AddressFrom.Compare( NETWORK_AUTH_GetCachedServerAddress() ) == false )
	{
		HUFFMAN_Decode( g_ucHuffmanBuffer, (unsigned char *)g_NetworkMessage.pbData, lNumBytes, &iDecodedNumBytes );
		g_NetworkMessage.ulCurrentSize = iDecodedNumBytes;
	}
	else
	{
		// [BB] We don't need to decode, so we just copy the data.
		// Not very efficient, but this keeps the changes at a minimum for now.
		memcpy ( g_NetworkMessage.pbData, g_ucHuffmanBuffer, lNumBytes );
		g_NetworkMessage.ulCurrentSize = lNumBytes;
	}
	g_NetworkMessage.ByteStream.pbStream = g_NetworkMessage.pbData;
	g_NetworkMessage.ByteStream.pbStreamEnd = g_NetworkMessage.ByteStream.pbStream + g_NetworkMessage.ulCurrentSize;
	g_NetworkMessage.ByteStream.bitBuffer = NULL;
	g_NetworkMessage.ByteStream.bitShift = -1;

	return ( g_NetworkMessage.ulCurrentSize );
}

//*****************************************************************************
//
int NETWORK_GetLANPackets( void )
{
	// [BB] If we know that there is a problem with the socket don't try to use it.
	if ( g_bLANSocketInvalid )
		return 0;

	LONG				lNumBytes;
	INT					iDecodedNumBytes = sizeof(g_ucHuffmanBuffer);
	sockaddr			SocketFrom;
	INT					iSocketFromLength;

    iSocketFromLength = sizeof( SocketFrom );

#ifdef	WIN32
	lNumBytes = recvfrom( g_LANSocket, (char *)g_ucHuffmanBuffer, sizeof( g_ucHuffmanBuffer ), 0, &SocketFrom, &iSocketFromLength );
#else
	lNumBytes = recvfrom( g_LANSocket, (char *)g_ucHuffmanBuffer, sizeof( g_ucHuffmanBuffer ), 0, &SocketFrom, (socklen_t *)&iSocketFromLength );
#endif

	// If the number of bytes returned is -1, an error has occured.
    if ( lNumBytes == -1 ) 
    { 
#ifdef __WIN32__
        errno = WSAGetLastError( );

        if ( errno == WSAEWOULDBLOCK )
            return ( false );

		// Connection reset by peer. Doesn't mean anything to the server.
		if ( errno == WSAECONNRESET )
			return ( false );

        if ( errno == WSAEMSGSIZE )
		{
             Printf( "NETWORK_GetPackets:  WARNING! Oversized packet from %s\n", g_AddressFrom.ToString() );
             return ( false );
        }

        Printf( "NETWORK_GetPackets: WARNING!: Error #%d: %s\n", errno, strerror( errno ));
		return ( false );
#else
        if ( errno == EWOULDBLOCK )
            return ( false );

        if ( errno == ECONNREFUSED )
            return ( false );

        Printf( "NETWORK_GetPackets: WARNING!: Error #%d: %s\n", errno, strerror( errno ));
        return ( false );
#endif
    }

	// No packets or an error, dont process anything.
	if ( lNumBytes <= 0 )
		return ( 0 );

	// Record this for our statistics window.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVER_STATISTIC_AddToInboundDataTransfer( lNumBytes );

	// If the number of bytes we're receiving exceeds our buffer size, ignore the packet.
	if ( lNumBytes >= static_cast<LONG>(g_NetworkMessage.ulMaxSize) )
		return ( 0 );

	// Store the IP address of the sender.
	g_AddressFrom.LoadFromSocketAddress( SocketFrom );

	// Decode the huffman-encoded message we received.
	// [BB] Communication with the auth server is not Huffman-encoded.
	if ( g_AddressFrom.Compare( NETWORK_AUTH_GetCachedServerAddress() ) == false )
	{
		HUFFMAN_Decode( g_ucHuffmanBuffer, (unsigned char *)g_NetworkMessage.pbData, lNumBytes, &iDecodedNumBytes );
		g_NetworkMessage.ulCurrentSize = iDecodedNumBytes;
	}
	else 
	{
		// [BB] We don't need to decode, so we just copy the data.
		// Not very efficient, but this keeps the changes at a minimum for now.
		memcpy ( g_NetworkMessage.pbData, g_ucHuffmanBuffer, lNumBytes );
		g_NetworkMessage.ulCurrentSize = lNumBytes;
	}
	g_NetworkMessage.ByteStream.pbStream = g_NetworkMessage.pbData;
	g_NetworkMessage.ByteStream.pbStreamEnd = g_NetworkMessage.ByteStream.pbStream + g_NetworkMessage.ulCurrentSize;

	return ( g_NetworkMessage.ulCurrentSize );
}

//*****************************************************************************
//
NETADDRESS_s NETWORK_GetFromAddress( void )
{
	return ( g_AddressFrom );
}

//*****************************************************************************
//
void NETWORK_LaunchPacket( NETBUFFER_s *pBuffer, NETADDRESS_s Address )
{
	LONG				lNumBytes;
	INT					iNumBytesOut = sizeof(g_ucHuffmanBuffer);

	pBuffer->ulCurrentSize = pBuffer->CalcSize();

	// Nothing to do.
	if ( pBuffer->ulCurrentSize == 0 )
		return;

	// Convert the IP address to a socket address.
	struct sockaddr_in SocketAddress;
	Address.ToSocketAddress( reinterpret_cast<sockaddr&>(SocketAddress) );

	// [BB] Communication with the auth server is not Huffman-encoded.
	if ( Address.Compare( NETWORK_AUTH_GetCachedServerAddress() ) == false )
		HUFFMAN_Encode( (unsigned char *)pBuffer->pbData, g_ucHuffmanBuffer, pBuffer->ulCurrentSize, &iNumBytesOut );
	else
	{
		// [BB] We don't need to encode, so we just copy the data.
		// Not very efficient, but this keeps the changes at a minimum for now.
		memcpy ( g_ucHuffmanBuffer, pBuffer->pbData, pBuffer->ulCurrentSize );
		iNumBytesOut = pBuffer->ulCurrentSize;
	}

	lNumBytes = sendto( g_NetworkSocket, (const char*)g_ucHuffmanBuffer, iNumBytesOut, 0, reinterpret_cast<sockaddr*>(&SocketAddress), sizeof( SocketAddress ));

	// If sendto returns -1, there was an error.
	if ( lNumBytes == -1 )
	{
#ifdef __WIN32__
		INT	iError = WSAGetLastError( );

		// Wouldblock is silent.
		if ( iError == WSAEWOULDBLOCK )
			return;

		switch ( iError )
		{
		case WSAEACCES:

			Printf( "NETWORK_LaunchPacket: Error #%d, WSAEACCES: Permission denied for address: %s\n", iError, Address.ToString() );
			return;
		case WSAEAFNOSUPPORT:

			Printf( "NETWORK_LaunchPacket: Error #%d, WSAEAFNOSUPPORT: Address %s incompatible with the requested protocol\n", iError, Address.ToString() );
			return;
		case WSAEADDRNOTAVAIL:

			Printf( "NETWORK_LaunchPacket: Error #%d, WSAEADDRENOTAVAIL: Address %s not available\n", iError, Address.ToString() );
			return;
		case WSAEHOSTUNREACH:

			Printf( "NETWORK_LaunchPacket: Error #%d, WSAEHOSTUNREACH: Address %s unreachable\n", iError, Address.ToString() );
			return;				
		default:

			Printf( "NETWORK_LaunchPacket: Error #%d\n", iError );
			return;
		}
#else
	if ( errno == EWOULDBLOCK )
return;

          if ( errno == ECONNREFUSED )
              return;

		Printf( "NETWORK_LaunchPacket: %s\n", strerror( errno ));
		Printf( "NETWORK_LaunchPacket: Address %s\n", Address.ToString() );

#endif
	}

	// Record this for our statistics window.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVER_STATISTIC_AddToOutboundDataTransfer( lNumBytes );
}

//*****************************************************************************
//
NETADDRESS_s NETWORK_GetLocalAddress( void )
{
	char				szBuffer[512];
	struct sockaddr_in	SocketAddress;
	int					iNameLength;

#ifndef __WINE__
	gethostname( szBuffer, 512 );
#endif
	szBuffer[512-1] = 0;

	// Convert the host name to our local 
	bool ok;
	NETADDRESS_s Address ( szBuffer, &ok );

	iNameLength = sizeof( SocketAddress );
#ifndef	WIN32
	if ( getsockname ( g_NetworkSocket, (struct sockaddr *)&SocketAddress, (socklen_t *)&iNameLength) == -1 )
#else
	if ( getsockname ( g_NetworkSocket, (struct sockaddr *)&SocketAddress, &iNameLength ) == -1 )
#endif
	{
		Printf( "NETWORK_GetLocalAddress: Error getting socket name: %s", strerror( errno ));
	}

#ifdef __unix__
	// [BB] The "gethostname -> gethostbyname" trick didn't reveal the local IP.
	// Now we need to resort to something more complicated.
	if ( ok == false )
	{
#ifndef __FreeBSD__
		unsigned char      *u;
		int                size  = 1;
		struct ifreq       *ifr;
		struct ifconf      ifc;
		struct sockaddr_in sa;
		
		ifc.ifc_len = IFRSIZE;
		ifc.ifc_req = NULL;
		
		do {
			++size;
			/* realloc buffer size until no overflow occurs  */
			if (NULL == (ifc.ifc_req = (ifreq*)realloc(ifc.ifc_req, IFRSIZE)))
			{
				fprintf(stderr, "Out of memory.\n");
				exit(EXIT_FAILURE);
			}
			ifc.ifc_len = IFRSIZE;
			if (ioctl(g_NetworkSocket, SIOCGIFCONF, &ifc))
			{
				perror("ioctl SIOCFIFCONF");
				exit(EXIT_FAILURE);
			}
		} while  (IFRSIZE <= ifc.ifc_len);
		
		ifr = ifc.ifc_req;
		for (;(char *) ifr < (char *) ifc.ifc_req + ifc.ifc_len; ++ifr)
		{
		
			if (ifr->ifr_addr.sa_data == (ifr+1)->ifr_addr.sa_data)
			{
				continue;  /* duplicate, skip it */
			}
		
			if (ioctl(g_NetworkSocket, SIOCGIFFLAGS, ifr))
			{
				continue;  /* failed to get flags, skip it */
			}
		
			Printf("Found interface %s", ifr->ifr_name);
			Printf(" with IP address: %s\n", inet_ntoa(inaddrr(ifr_addr.sa_data)));
			*(int *)&Address.abIP = *(int *)&inaddrr(ifr_addr.sa_data);
			if ( Address.abIP[0] != 127 )
			{
				Printf ( "Using IP address of interface %s as local address.\n", ifr->ifr_name );
				break;
			}
		}
		if ( ifc.ifc_req != NULL )
			free ( ifc.ifc_req );
#else
		struct ifreq       *ifr;
		struct ifconf      ifc;
		bzero(&ifc, sizeof(ifc));
		unsigned int n = 1;
		struct ifreq *lifr;
		ifr = (ifreq*)calloc( ifc.ifc_len, sizeof(*ifr) );
		do
		{
			n *= 2;
			ifr = (ifreq*)realloc( ifr, PAGE_SIZE * n );
			bzero( ifr, PAGE_SIZE * n );
			ifc.ifc_req = ifr;
			ifc.ifc_len = n * PAGE_SIZE;
		} while( ( ioctl( g_NetworkSocket, SIOCGIFCONF, &ifc ) == -1 ) || ( ifc.ifc_len >= ( (n-1) * PAGE_SIZE)) );
		
		lifr = (struct ifreq *)&ifc.ifc_buf[ifc.ifc_len];
		
		while (ifr < lifr)
		{
			struct sockaddr *sa = &ifr->ifr_ifru.ifru_addr;
			if( AF_INET == sa->sa_family )
			{
				struct sockaddr_in dummysa;
				in_addr inAddr = *(struct in_addr *) &ifr->ifr_addr.sa_data[sizeof dummysa.sin_port];
	
				Printf("Found interface %s", ifr->ifr_name);
				Printf(" with IP address: %s\n", inet_ntoa(inAddr));
				*(int *)&Address.abIP = *(int *)&inAddr;
				if ( Address.abIP[0] != 127 )
				{
					Printf ( "Using IP address of interface %s as local address.\n", ifr->ifr_name );
					break;
				}
			 }
	 	ifr = (struct ifreq *)(((char *)ifr) + _SIZEOF_ADDR_IFREQ(*ifr));
 		}
#endif
	}
#endif

	Address.usPort = SocketAddress.sin_port;
	return ( Address );
}

//*****************************************************************************
//
NETADDRESS_s	NETWORK_GetCachedLocalAddress( void )
{
	return g_LocalAddress;
}

//*****************************************************************************
//
NETBUFFER_s *NETWORK_GetNetworkMessageBuffer( void )
{
	return ( &g_NetworkMessage );
}

//*****************************************************************************
//
USHORT NETWORK_ntohs( ULONG ul )
{
	return ( ntohs( (u_short)ul ));
}

//*****************************************************************************
//
USHORT NETWORK_GetLocalPort( void )
{
	return ( g_usLocalPort );
}

//*****************************************************************************
// [BB] 
bool NETWORK_IsGeoIPAvailable ( void )
{
	return ( g_GeoIPDB != NULL );
}

//*****************************************************************************
// [BB/AK]
ULONG NETWORK_GetCountryIndexFromAddress( NETADDRESS_s Address )
{
	const char *addressString = Address.ToStringNoPort();

	// [AK] IP addresses ranging between 172.16.0.0 to 172.31.255.255 are also
	// private and should be treated as LAN.
	if ((( Address.abIP[0] == 172 ) && ( Address.abIP[1] >= 16 ) && ( Address.abIP[1] <= 31 )) ||
		( strnicmp( "10.", addressString, 3 ) == 0 ) ||
		( strnicmp( "192.168.", addressString, 8 ) == 0 ) ||
		( strnicmp( "127.", addressString, 4 ) == 0 ))
	{
		return COUNTRYINDEX_LAN;
	}

	if ( NETWORK_IsGeoIPAvailable() == false )
		return 0;

	return GeoIP_id_by_addr ( g_GeoIPDB, addressString );
}

//*****************************************************************************
// [AK] Helper function to reduce code duplication.
const char *network_GetCountryStringFromIndex( ULONG ulIndex, const char *( *funcName )( int ))
{
	if ( ulIndex == COUNTRYINDEX_LAN )
		return "LAN";

	// [AK] Invalid indices (e.g. those greater than COUNTRYINDEX_LAN) should always return NULL.
	const char *pszString = funcName( ulIndex );
	return (( pszString == NULL ) || ( strlen( pszString ) == 0 )) ? "N/A" : pszString;
}

//*****************************************************************************
// [AK]
const char *NETWORK_GetCountryCodeFromIndex( ULONG ulIndex, bool bGetAlpha3 )
{
	return network_GetCountryStringFromIndex( ulIndex, bGetAlpha3 ? GeoIP_code3_by_id : GeoIP_code_by_id );
}

//*****************************************************************************
// [AK]
const char *NETWORK_GetCountryNameFromIndex( ULONG ulIndex )
{
	return network_GetCountryStringFromIndex( ulIndex, GeoIP_name_by_id );
}

//*****************************************************************************
//
const TArray<NetworkPWAD>& NETWORK_GetPWADList( void )
{
	return g_PWADs;
}

//*****************************************************************************
//
const TArray<NetworkPWAD>& NETWORK_GetAuthenticatedWADsList( void )
{
	return g_AuthenticatedWADs;
}

//*****************************************************************************
//
const char *NETWORK_GetIWAD( void )
{
	return g_IWAD.GetChars( );
}

//*****************************************************************************
//
void NETWORK_AddLumpForAuthentication( const LONG LumpNumber )
{
	if ( LumpNumber == -1 )
		return;

	// [AK] Check if we're trying to authenticate a duplicate lump.
	network_CheckIfDuplicateLump( LumpNumber );

	g_LumpNumsToAuthenticate.Push ( LumpNumber );
}

//*****************************************************************************
//
void NETWORK_GenerateLumpMD5Hash( const int LumpNum, FString &MD5Hash )
{
	const int lumpSize = Wads.LumpLength (LumpNum);
	BYTE *pbData = new BYTE[lumpSize];

	FWadLump lump = Wads.OpenLumpNum (LumpNum);

	// Dump the data from the lump into our data buffer.
	lump.Read (pbData, lumpSize);

	// Perform the checksum on our buffer, and free it.
	CMD5Checksum::GetMD5( pbData, lumpSize, MD5Hash );
	delete[] pbData;
}

//*****************************************************************************
//
bool network_GenerateLumpMD5HashAndWarnIfNeeded( const int LumpNum, const char *LumpName, FString &MD5Hash )
{
	NETWORK_GenerateLumpMD5Hash( LumpNum, MD5Hash );

	int wadNum = Wads.GetParentWad( Wads.GetWadnumFromLumpnum ( LumpNum ));

	// [BB] Check whether the containing file was loaded automatically.
	if ( ( wadNum >= 0 ) && Wads.GetLoadedAutomatically ( wadNum ) )
	{
		Printf ( PRINT_BOLD, "%s contains protected lump %s\n", Wads.GetWadFullName( wadNum ), LumpName );
		return false;
	}
	else
		return true;

}

//*****************************************************************************
//
void network_CheckIfDuplicateLump( const int LumpNum )
{
	const char *lumpName = Wads.GetLumpFullName( LumpNum );

	for ( unsigned int i = 0; i < DuplicateLumps.Size(); i++ )
	{
		// [AK] Check if the name of this lump matches that of any duplicate lumps on the list.
		// We'll also need to check if these lumps match the same file.
		if ( DuplicateLumps[i].CompareNoCase( lumpName ) == 0 )
		{
			FString fullPath = Wads.GetLumpFullPath( LumpNum );
			FString fileName = fullPath.Left( fullPath.Len() - strlen( lumpName ) - 1 );

			// [AK] If the lumps belong to the same file, print a message into the console and
			// remove it from the list.
			if ( DuplicateLumpFilenames[i].CompareNoCase( fileName ) == 0 )
			{
				Printf( TEXTCOLOR_YELLOW "%s contains duplicate protected lump %s\n", fileName.GetChars(), lumpName );
				g_bDuplicateLumpAuthenticated = true;

				DuplicateLumps.Delete( i );
				DuplicateLumpFilenames.Delete( i-- );
			}
		}
	}
}

//*****************************************************************************
//
void network_AddSpritesToList( std::set<AUTHENTICATELUMP_s> &list, const char *name, const std::set<char> frames, const LumpAuthenticationMode mode )
{
	char lumpName[9];

	if ( name == nullptr )
		return;

	// [AK] Search all of the loaded sprite lumps. Doing this isn't efficient,
	// but since this only happens during startup, it shouldn't be a problem.
	for ( int lump = 0; lump < Wads.GetNumLumps( ); lump++ )
	{
		if ( Wads.GetLumpNamespace( lump ) != ns_sprites )
			continue;

		Wads.GetLumpName( lumpName, lump );
		lumpName[sizeof( lumpName ) - 1] = 0;

		// [AK] Skip lumps that aren't using the sprite's name.
		if ( strnicmp( lumpName, name, 4 ) != 0 )
			continue;

		bool lumpIsValidSprite = false;

		// [AK] Verify that the lump uses the proper naming convention for sprites.
		// The frame and rotation (e.g. XXXXA1 or XXXXA2A8) must be valid.
		for ( unsigned int i = 4; i <= 6; i += 2 )
		{
			if ( static_cast<unsigned>( lumpName[i] - 'A' ) < MAX_SPRITE_FRAMES )
			{
				if ( R_IsCharUsuableAsSpriteRotation( lumpName[i + 1] ))
				{
					lumpIsValidSprite = true;
					break;
				}
			}
		}

		if ( lumpIsValidSprite == false )
			continue;

		// [AK] If only certain frames of the sprite should be authenticated,
		// check that the lump's name uses at least one the desired frames.
		if (( frames.size( ) > 0 ) && ( frames.find( lumpName[4] ) == frames.end( )) && ( frames.find( lumpName[6] ) == frames.end( )))
			continue;

		AUTHENTICATELUMP_s authenticatingLump = { lumpName, mode, ns_sprites };
		const auto result = list.insert( authenticatingLump );

		// [AK] If this lump is already on the list, just update the authentication
		// mode. Note that elements within a set are constant, so the mode can't
		// be updated directly. The existing entry must be removed first, and
		// the new entry added in afterward.
		if (( result.second == false ) && ( result.first->Mode != mode ))
		{
			list.erase( authenticatingLump );
			list.insert( authenticatingLump );
		}
	}
}

//*****************************************************************************
//
void network_ParseLumpAuthenticationMode( FScanner &sc, LumpAuthenticationMode &mode )
{
	sc.MustGetString( );

	if ( stricmp( sc.String, "last" ) == 0 )
		mode = LAST_LUMP;
	else if ( stricmp( sc.String, "all" ) == 0 )
		mode = ALL_LUMPS;
	else
		sc.ScriptError( "Unknown authentication mode \"%s\". It must be either \"last\" or \"all\".", sc.String );
}

//*****************************************************************************
// [Dusk] Gets a checksum of every map loaded.
FString NETWORK_MapCollectionChecksum( )
{
	FString longSum, fullSum;
	for( unsigned i = 0; i < wadlevelinfos.Size( ); i++ )
	{
		char* mname = wadlevelinfos[i].mapname;
		FString sum;

		// [BB] P_OpenMapData may throw an exception, so make sure that mname is a valid map.
		if ( P_CheckIfMapExists ( mname ) == false )
			continue;

		MapData* mdata = P_OpenMapData( mname, false );
		if ( !mdata )
			continue;

		BYTE BSum[16];
		mdata->GetChecksum( BSum );
		for (ULONG j = 0; j < sizeof( BSum ); j++)
			sum.AppendFormat ("%02X", BSum[j]);

		longSum += sum;
		delete mdata;
	}

	CMD5Checksum::GetMD5( reinterpret_cast<const BYTE *>( longSum.GetChars( ) ),
		longSum.Len( ), fullSum );
	return fullSum;
}

//*****************************************************************************
// [Dusk] Generates and stores the map collection checksum
void NETWORK_MakeMapCollectionChecksum( )
{
	if ( g_MapCollectionChecksum.IsEmpty( ) )
		g_MapCollectionChecksum = NETWORK_MapCollectionChecksum( );
}

//*****************************************************************************
// [TP]
static int STACK_ARGS namesort( const void* p1, const void* p2 )
{
	FName n1 = *reinterpret_cast<const FName*>( p1 );
	FName n2 = *reinterpret_cast<const FName*>( p2 );
	return stricmp( n1, n2 );
}

//*****************************************************************************
// [TP] Generates a name index of scripts for the below conversions. Done at map load.
void NETWORK_MakeScriptNameIndex()
{
	g_ACSNameIndex = FBehavior::StaticGetAllScriptNames();
	qsort( &g_ACSNameIndex[0], g_ACSNameIndex.Size(), sizeof g_ACSNameIndex[0], namesort );

	// Remove duplicates if there are any
	for ( unsigned int i = 1; i < g_ACSNameIndex.Size(); )
	{
		if ( g_ACSNameIndex[i - 1] == g_ACSNameIndex[i] )
			g_ACSNameIndex.Delete( i );
		else
			++i;
	}
}

//*****************************************************************************
// [TP] Converts a network representation of a script to a local script number.
int NETWORK_ACSScriptFromNetID( int netid )
{
	if ( netid < 0 )
	{
		unsigned idx = -netid - 2;
		if ( idx < g_ACSNameIndex.Size() )
		{
			return -g_ACSNameIndex[idx];
		}
		else
		{
			return 0;
		}
	}
	else
	{
		return netid;
	}
}

//*****************************************************************************
// [TP] Converts a script number to a network representation.
int NETWORK_ACSScriptToNetID( int script )
{
	if ( script < 0 )
	{
		FName name = FName ( ENamedName ( -script ));
		for ( int i = 0; i < (signed) g_ACSNameIndex.Size(); ++i )
		{
			if ( g_ACSNameIndex[i] == name )
			{
				return -i - 2;
			}
		}

		return NO_SCRIPT_NETID;
	}
	else
	{
		return script;
	}
}

//*****************************************************************************
//
FName NETWORK_ReadName( BYTESTREAM_s* bytestream )
{
	SDWORD index = bytestream->ReadShort();

	if ( index == -1 )
		return FName( bytestream->ReadString());
	else
		return FName( static_cast<ENamedName>( index ));
}

//*****************************************************************************
//
void NETWORK_WriteName( BYTESTREAM_s* bytestream, FName name )
{
	// Predefined names are the same on both the server and client in any case, so we can index those
	// with a short instead of a string. However, the ones that don't eat up 2 more bytes.
	// So, use this if most of the time the name is predefined.
	if ( name.IsPredefined() )
	{
		bytestream->WriteShort( name );
	}
	else
	{
		bytestream->WriteShort( -1 );
		bytestream->WriteString( name );
	}
}

//*****************************************************************************
// [TP]
void STACK_ARGS NETWORK_Printf( const char* format, ... )
{
	va_list argptr;
	va_start( argptr, format );

	if ( NETWORK_GetState() == NETSTATE_SERVER )
		SERVER_VPrintf( PRINT_HIGH, format, argptr, MAXPLAYERS );
	else
		VPrintf( PRINT_HIGH, format, argptr );

	va_end( argptr );
}

//*****************************************************************************
// [TP]
CCMD( dumpacsnetids )
{
	for ( int i = 0; i < (signed) g_ACSNameIndex.Size(); ++i )
	{
		FName name = g_ACSNameIndex[i];
		Printf( "%d: \"%s\" (%d)\n", -i - 2, name.GetChars(), name.GetIndex() );
	}
}

// [CW]
//*****************************************************************************
//
const char *NETWORK_GetClassNameFromIdentification( USHORT usActorNetworkIndex )
{
	if ( (usActorNetworkIndex == 0) || (usActorNetworkIndex > g_ActorNetworkIndexClassPointerMap.Size()) )
		return NULL;
	else
		return g_ActorNetworkIndexClassPointerMap[usActorNetworkIndex-1]->TypeName.GetChars( );
}

// [CW]
//*****************************************************************************
//
const PClass *NETWORK_GetClassFromIdentification( USHORT usActorNetworkIndex )
{
	if ( (usActorNetworkIndex == 0) || (usActorNetworkIndex > g_ActorNetworkIndexClassPointerMap.Size()) )
		return NULL;
	else
		return g_ActorNetworkIndexClassPointerMap[usActorNetworkIndex-1];
}

//*****************************************************************************
//
bool NETWORK_InClientMode( )
{
	return ( NETWORK_GetState( ) == NETSTATE_CLIENT ) || ( CLIENTDEMO_IsPlaying( ) == true );
}

//*****************************************************************************
//
bool NETWORK_IsConsolePlayerOrNotInClientMode( const player_t *pPlayer )
{
	// [BB] Not in client mode, so just return true.
	if ( NETWORK_InClientMode() == false )
		return true;

	// [BB] A null pointer is obviously not the console player.
	if ( pPlayer == NULL )
		return false;

	return ( pPlayer == &players[consoleplayer] );
}

//*****************************************************************************
//
bool NETWORK_IsConsolePlayer( const AActor *pActor )
{
	// [AK] The server can't ever be a consoleplayer, so return false.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		return false;

	if ( ( pActor == NULL ) || ( pActor->player == NULL ) )
		return false;

	return ( pActor->player == &players[consoleplayer] );
}

//*****************************************************************************
//
bool NETWORK_IsConsolePlayerOrSpiedByConsolePlayerOrNotInClientMode( const player_t *pPlayer )
{
	if ( NETWORK_IsConsolePlayerOrNotInClientMode ( pPlayer ) )
		return true;

	return ( pPlayer && ( pPlayer->mo->CheckLocalView( consoleplayer ) ) );
}

//*****************************************************************************
//
bool NETWORK_IsActorClientHandled( const AActor *pActor )
{
	// [BB] Sanity check
	if ( pActor == NULL )
		return false;

	return ( ( pActor->NetworkFlags & NETFL_CLIENTSIDEONLY ) || ( pActor->NetID == 0 ) );
}

//*****************************************************************************
//
bool NETWORK_InClientModeAndActorNotClientHandled( const AActor *pActor )
{
	return ( NETWORK_InClientMode() && ( NETWORK_IsActorClientHandled ( pActor ) == false ) );
}

//*****************************************************************************
//
bool NETWORK_IsClientPredictedSpecial( const int Special )
{
	return ( ( Special == ThrustThing ) || ( Special == ThrustThingZ ) );
}

//*****************************************************************************
//
SDWORD NETWORK_Check ( ticcmd_t *pCmd )
{
	FString string;
	string.AppendFormat ( "%d%d%d", pCmd->ucmd.pitch, pCmd->ucmd.yaw, pCmd->ucmd.roll );
	FString hash;
	CMD5Checksum::GetMD5( reinterpret_cast<const BYTE*>(string.GetChars()), string.Len(), hash );

	return hash[2] + ( hash[0] << 8 ) + ( hash[5] << 16 ) + ( hash[7] << 24 );
}

//*****************************************************************************
//
int NETWORK_AttenuationFloatToInt ( const float fAttenuation )
{
	if ( fAttenuation == ATTN_NONE )
		return ATTN_INT_NONE;
	else if ( fAttenuation == ATTN_NORM )
		return ATTN_INT_NORM;
	else if ( fAttenuation == ATTN_IDLE )
		return ATTN_INT_IDLE;
	else if ( fAttenuation == ATTN_STATIC )
		return ATTN_INT_STATIC;
	else if ( fAttenuation > 0.f )
		return ATTN_INT_COUNT + clamp<int>( static_cast<int> ( fAttenuation * 25.f ), 0, 255 - ATTN_INT_COUNT );
	else {
		Printf( "NETWORK_AttenuationFloatToInt: Negative attenuation value: %f\n", fAttenuation );
		return 255; // Don't let the clients hear it, it could be dangerous.
	}
}

//*****************************************************************************
//
float NETWORK_AttenuationIntToFloat ( const int iAttenuation )
{
	switch (iAttenuation)
	{
	case ATTN_INT_NONE:
		return ATTN_NONE;

	case ATTN_INT_NORM:
		return ATTN_NORM;

	case ATTN_INT_IDLE:
		return ATTN_IDLE;

	case ATTN_INT_STATIC:
		return ATTN_STATIC;
	default:
		break;
	}
	return float( iAttenuation - ATTN_INT_COUNT ) / 25.f;
}

//*****************************************************************************
//*****************************************************************************
//
LONG NETWORK_GetState( void )
{
	return ( g_lNetworkState );
}

//*****************************************************************************
//
void NETWORK_SetState( LONG lState )
{
	if ( lState >= NUM_NETSTATES || lState < 0 )
		return;

	// [BB] A client may have renamed itself while being disconnected in full console mode.
	if ( ( gamestate == GS_FULLCONSOLE ) && ( lState == NETSTATE_CLIENT ) && ( g_lNetworkState != NETSTATE_CLIENT ) )
		D_SetupUserInfo();

	g_lNetworkState = lState;

	// [BB] Limit certain CVARs like turbo on clients. Needs to be done here in addition
	// to where the CVAR is implemented, because the check over there is not
	// applied when the network state changes.
	if ( g_lNetworkState == NETSTATE_CLIENT )
	{
		CLIENT_LimitProtectedCVARs();
	}

	// Alert the status bar that multiplayer status has changed.
	if (( g_lNetworkState != NETSTATE_SERVER ) &&
		( StatusBar ) &&
		( screen ))
	{
		StatusBar->MultiplayerChanged( );
	}
}

//*****************************************************************************
//*****************************************************************************
//
//*****************************************************************************
// [RC]
static void network_InitPWADList( void )
{
	g_PWADs.Clear();

	// Find the IWAD index.
	ULONG ulNumPWADs = 0, ulRealIWADIdx = 0;
	for ( ULONG ulIdx = 0; Wads.GetWadName( ulIdx ) != NULL; ulIdx++ )
	{
		if ( Wads.GetLoadedAutomatically( ulIdx ) == false ) // Since WADs can now be loaded within pk3 files, we have to skip over all the ones automatically loaded. To my knowledge, the only way to do this is to skip wads that have a colon in them.
		{
			if ( ulNumPWADs == FWadCollection::IWAD_FILENUM )
			{
				ulRealIWADIdx = ulIdx;
				break;
			}

			ulNumPWADs++;
		}
	}

	g_IWAD = Wads.GetWadName( ulRealIWADIdx );

	// Collect all the PWADs into a list.
	for ( ULONG ulIdx = 0; Wads.GetWadName( ulIdx ) != NULL; ulIdx++ )
	{
		// [SB] Skip nested WADs, they can't be checksummed and only their parents matter anyway. 
		if ( Wads.GetParentWad( ulIdx ) != ulIdx )
		{
			continue;
		}

		const bool bIsIwad = ( ulIdx == ulRealIWADIdx );
		const bool bIsBaseWad = ( stricmp( Wads.GetWadName( ulIdx ), BASEWAD ) == 0 ); // [SB] Corrected to use BASEWAD instead of GAMENAMELOWERCASE ".pk3"

		char MD5Sum[33];
		MD5SumOfFile ( Wads.GetWadFullName( ulIdx ), MD5Sum );

		NetworkPWAD pwad;
		pwad.name = Wads.GetWadName( ulIdx );
		pwad.checksum = MD5Sum;
		pwad.wadnum = ulIdx;

		// Skip the IWAD, zandronum.pk3, files that were automatically loaded from subdirectories (such as skin files), and WADs loaded automatically within pk3 files.
		// [BB] The latter are marked as being loaded automatically.
		if ( !bIsIwad && !bIsBaseWad && !Wads.GetLoadedAutomatically( ulIdx ) )
		{
			g_PWADs.Push( pwad );
		}

		// [SB] Only add files that contain protected lumps or levels.
		if ( Wads.WadContainsAuthenticatedLumps( ulIdx ) )
		{
			g_AuthenticatedWADs.Push( pwad );
		}
	}
}

void network_Error( const char *pszError )
{
	Printf( TEXTCOLOR_GREEN "%s\n", pszError );
}

//*****************************************************************************
//
static SOCKET network_AllocateSocket( void )
{
	SOCKET	Socket;

	// Allocate a socket.
	Socket = socket( PF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if ( Socket == INVALID_SOCKET )
	{
		static char network_AllocateSocket[] = "network_AllocateSocket: Couldn't create socket!";
		network_Error( network_AllocateSocket );
	}

	return ( Socket );
}

//*****************************************************************************
//
bool network_BindSocketToPort( SOCKET Socket, ULONG ulInAddr, USHORT usPort, bool bReUse )
{
	int		iErrorCode;
	struct sockaddr_in address;

	// setsockopt needs an int, bool won't work
	int		enable = 1;

	memset (&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = ulInAddr;
	address.sin_port = htons( usPort );

	// Allow the network socket to broadcast.
	setsockopt( Socket, SOL_SOCKET, SO_BROADCAST, (const char *)&enable, sizeof( enable ));
	if ( bReUse )
		setsockopt( Socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&enable, sizeof( enable ));

	iErrorCode = bind( Socket, (sockaddr *)&address, sizeof( address ));
	if ( iErrorCode == SOCKET_ERROR )
		return ( false );

	return ( true );
}


#ifndef	WIN32
extern int	stdin_ready;
extern int	do_stdin;
#endif

// [BB] We only need this for the server console input under Linux.
void I_DoSelect (void)
{
#ifdef		WIN32
/*
    struct timeval   timeout;
    fd_set           fdset;

    FD_ZERO(&fdset);
    FD_SET(g_NetworkSocket, &fdset);
    timeout.tv_sec = 0;
    timeout.tv_usec = 1000000 / TICRATE;
    if (select (static_cast<int>(g_NetworkSocket)+1, &fdset, NULL, NULL, &timeout) == -1)
        return;
*/
#else
    struct timeval   timeout;
    fd_set           fdset;

    FD_ZERO(&fdset);
    if (do_stdin)
    	FD_SET(0, &fdset);

    FD_SET(g_NetworkSocket, &fdset);
    timeout.tv_sec = 0;
    timeout.tv_usec = 1000000 / TICRATE;
    if (select (static_cast<int>(g_NetworkSocket)+1, &fdset, NULL, NULL, &timeout) == -1)
        return;

    stdin_ready = FD_ISSET(0, &fdset);
#endif
} 

//*****************************************************************************
// [BB] Let Skulltag's existing code use ZDoom's MD5 code.
void CMD5Checksum::GetMD5(const BYTE* pBuf, UINT nLength, FString &OutString)
{
	MD5Context md5;
	BYTE readbuf[16];
	char MD5SumFull[33];
	char *MD5Sum = MD5SumFull;
	md5.Update(pBuf, nLength);
	md5.Final(readbuf);
	for(int j = 0; j < 16; ++j)
	{
		mysnprintf(MD5Sum, sizeof(MD5Sum), "%02x", readbuf[j]);
		++++MD5Sum;
	}
	*MD5Sum = 0;
	OutString = MD5SumFull;
}

//*****************************************************************************
//	CONSOLE COMMANDS

CCMD( ip )
{
	NETADDRESS_s	LocalAddress;

	// The network module isn't initialized in these cases.
	if (( NETWORK_GetState( ) != NETSTATE_SERVER ) &&
		( NETWORK_GetState( ) != NETSTATE_CLIENT ))
	{
		return;
	}

	LocalAddress = NETWORK_GetLocalAddress( );

	Printf( PRINT_HIGH, "IP address is %s\n", LocalAddress.ToString() );
}

//*****************************************************************************
//
CCMD( netstate )
{
	switch ( g_lNetworkState )
	{
	case NETSTATE_SINGLE:

		Printf( "Game being run as in SINGLE PLAYER.\n" );
		break;
	case NETSTATE_SINGLE_MULTIPLAYER:

		Printf( "Game being run as in MULTIPLAYER EMULATION.\n" );
		break;
	case NETSTATE_CLIENT:

		Printf( "Game being run as a CLIENT.\n" );
		break;
	case NETSTATE_SERVER:

		Printf( "Game being run as a SERVER.\n" );
		break;
	}
}

//*****************************************************************************
//
#if BUILD_ID != BUILD_RELEASE
CCMD( dumpnetclassids )
{
	for ( unsigned int i = 0; i < g_ActorNetworkIndexClassPointerMap.Size(); ++i )
		Printf ( "%d - %s\n", i, g_ActorNetworkIndexClassPointerMap[i]->TypeName.GetChars() );
}
#endif

#ifdef	_DEBUG
// DEBUG FUNCTION!
void NETWORK_FillBufferWithShit( BYTESTREAM_s *pByteStream, ULONG ulSize )
{
	ULONG	ulIdx;

	for ( ulIdx = 0; ulIdx < ulSize; ulIdx++ )
		pByteStream->WriteByte( M_Random( ));

//	g_NetworkMessage.Clear();
}

CCMD( fillbufferwithshit )
{
	// Fill the packet with 1k of SHIT!
	NETWORK_FillBufferWithShit( &g_NetworkMessage.ByteStream, 1024 );
}

CCMD( testnetstring )
{
	if ( argv.argc( ) < 2 )
		return;

//	Printf( "%s: %d", argv[1], gethostbyname( argv[1] ));
	Printf( "%s: %d\n", argv[1], inet_addr( argv[1] ));
	if ( inet_addr( argv[1] ) == INADDR_NONE )
		Printf( "FAIL!\n" );
}
#endif	// _DEBUG
