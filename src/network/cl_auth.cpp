//-----------------------------------------------------------------------------
//
// Zandronum Source
// Copyright (C) 2014 Benjamin Berkels
// Copyright (C) 2014 Zandronum Development Team
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
// 3. Neither the name of the Zandronum Development Team nor the names of its
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
// Filename: cl_auth.cpp
//
//-----------------------------------------------------------------------------

#ifdef _WIN32
#include <windows.h>
#include <wincred.h>
#define USE_WINDOWS_DWORD
#endif

#include "c_dispatch.h"
#include "cl_main.h"
#include "cl_commands.h"
#include "network.h"
#include "cl_auth.h"
#include "srp.h"
#include "network_enums.h"
#include "gameconfigfile.h"
#include "version.h"
#include "cl_demo.h"

//*****************************************************************************
//	VARIABLES

static struct SRPUser* g_usr = NULL;
static FString g_password;

CUSTOM_CVAR( Bool, cl_hideaccount, false, CVAR_ARCHIVE )
{
	if ( NETWORK_GetState() == NETSTATE_CLIENT )
		CLIENTCOMMANDS_SetWantHideInfo( HIDEINFO_ACCOUNTNAME, self );
}

#ifdef ENABLE_AUTH_STORAGE
EXTERN_CVAR( Bool, cl_autologin )

CUSTOM_CVAR( String, login_default_user, "", CVAR_ARCHIVE | CVAR_NOINITCALL )
{
	// [AK] Log in automatically when setting a default username, if applicable.
	if (( NETWORK_GetState( ) == NETSTATE_CLIENT ) && ( CLIENT_IsLoggedIn( ) == false ) && ( cl_autologin ))
		CLIENT_RetrieveUserAndLogIn( self.GetGenericRep( CVAR_String ).String );
}
#endif

//*****************************************************************************
//	PROTOTYPES

//*****************************************************************************
//	FUNCTIONS

#ifdef _WIN32
//*****************************************************************************
//
bool client_SaveCredentials ( const FString &Username, const FString &Password )
{
	FString targetName = FString ( GAMENAME ) + "/" + Username;
	FString usernameCopy = Username;
	FString passwordCopy = Password;

	CREDENTIAL cred = { 0 };
	cred.Type = CRED_TYPE_GENERIC;
	cred.TargetName = targetName.LockBuffer();
	cred.CredentialBlobSize = passwordCopy.Len() + 1;
	cred.CredentialBlob = reinterpret_cast<BYTE*> ( passwordCopy.LockBuffer() );
	cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
	cred.UserName = usernameCopy.LockBuffer();

	BOOL ok = CredWrite (&cred, 0);
	if ( ok != TRUE )
		Printf ( "CredWrite() failed - errno %d\n", ::GetLastError () );
	targetName.UnlockBuffer();
	usernameCopy.UnlockBuffer();
	passwordCopy.UnlockBuffer();

	return ( ok == TRUE );
}

//*****************************************************************************
//
bool client_RetrieveCredentials ( const FString &Username, FString &Password )
{
	FString targetName = FString ( GAMENAME ) + "/" + Username;
	PCREDENTIAL pcred;
	BOOL ok = CredRead (targetName.GetChars(), CRED_TYPE_GENERIC, 0, &pcred);
	if ( ok != TRUE )
		Printf ( "CredRead() failed - errno %d\n", ::GetLastError () );
	else
	{
		if ( Username.Compare ( pcred->UserName ) != 0 )
			Printf ( "client_RetrieveCredentials username error\n" );
		else
			Password = reinterpret_cast<char *> ( pcred->CredentialBlob );
	}

	CredFree (pcred);
	return ( ok == TRUE );
}
#endif

//*****************************************************************************
//
void client_RequestLogin ( const char* Username, const char* Password )
{
	g_password = Password;
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_SRP_USER_REQUEST_LOGIN );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteString( Username );
}

//*****************************************************************************
//
void client_SRPStartAuthentication ( const char *Username )
{
    const unsigned char * bytesA = 0;
    int lenA = 0;
    const char * authUsername = NULL; 

	CLIENT_LogOut( );

	g_usr = srp_user_new( SRP_SHA256, SRP_NG_2048, Username, reinterpret_cast<const unsigned char*> ( g_password.GetChars() ), g_password.Len(), NULL, NULL, 1 );
	srp_user_start_authentication( g_usr, &authUsername, &bytesA, &lenA );
	if ( strcmp( authUsername, Username ) != 0 )
		Printf ( "Username problem when calling srp_user_start_authentication.\n" );

	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_SRP_USER_START_AUTHENTICATION );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteShort( lenA );
	for ( int i = 0; i < lenA; ++i )
		CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( bytesA[i] );
}

//*****************************************************************************
//
void client_SRPUserProcessChallenge ( TArray<unsigned char> &Salt, TArray<unsigned char> &B )
{
    const unsigned char * bytesM = 0;
    int lenM = 0;

	srp_user_process_challenge( g_usr, &(Salt[0]), Salt.Size(), &(B[0]), B.Size(), &bytesM, &lenM );
	if ( ( bytesM == NULL ) || ( lenM == 0 ) )
		Printf ( "User SRP-6a safety check violation!\n" );
	else
	{
		CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_SRP_USER_PROCESS_CHALLENGE );
		CLIENT_GetLocalBuffer( )->ByteStream.WriteShort( lenM );
		for ( int i = 0; i < lenM; ++i )
			CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( bytesM[i] );
	}
}

//*****************************************************************************
//
void CLIENT_ProcessSRPServerCommand( LONG lCommand, BYTESTREAM_s *pByteStream )
{
	switch ( lCommand )
	{
	case SVC2_SRP_USER_START_AUTHENTICATION:
		{
			const FString username = pByteStream->ReadString();
			client_SRPStartAuthentication ( username.GetChars() );
		}
		break;
	case SVC2_SRP_USER_PROCESS_CHALLENGE:
		{
			const int lenSalt = pByteStream->ReadByte();
			TArray<unsigned char> salt;
			salt.Resize ( lenSalt ); 
			for ( int i = 0; i < lenSalt; ++i )
				salt[i] = pByteStream->ReadByte();
			const int lenB = pByteStream->ReadShort();
			TArray<unsigned char> bytesB;
			bytesB.Resize ( lenB ); 
			for ( int i = 0; i < lenB; ++i )
				bytesB[i] = pByteStream->ReadByte();

			client_SRPUserProcessChallenge ( salt, bytesB );
		}
		break;
	case SVC2_SRP_USER_VERIFY_SESSION:
		{
			const int lenHAMK = pByteStream->ReadShort();

			// [BB] The following crashes if g_usr is not properly initialized.
			if ( g_usr == NULL )
				break;

			TArray<unsigned char> bytesHAMK;
			// [BB] We may need to allocate more then lenHAMK!
			bytesHAMK.Resize ( srp_user_get_session_key_length( g_usr ) ); 
			for ( int i = 0; i < lenHAMK; ++i )
				bytesHAMK[i] = pByteStream->ReadByte();

			// [AK] Don't try to verify the session while playing a demo. The
			// authentication will always fail in this state.
			if ( CLIENTDEMO_IsPlaying() == false )
			{
				srp_user_verify_session( g_usr, &(bytesHAMK[0]) );

				if ( !srp_user_is_authenticated ( g_usr ) )
					Printf( "Server authentication failed!\n" );
				else
					Printf( "Login successful.\n" );
			}
		}
		break;

	default:
		Printf ( "Error: Received unknown SRP command '%d' from client %d.\n", static_cast<int>(lCommand), static_cast<int>(SERVER_GetCurrentClient()) );
		break;
	}
}

//*****************************************************************************
//
void CLIENT_LogOut( void )
{
	if ( g_usr != nullptr )
	{
		srp_user_delete( g_usr );
		g_usr = nullptr;
	}
}

//*****************************************************************************
//
bool CLIENT_IsLoggedIn( void )
{
	return (( g_usr != nullptr ) && ( srp_user_is_authenticated( g_usr )));
}

//*****************************************************************************
//
#ifdef ENABLE_AUTH_STORAGE
void CLIENT_RetrieveUserAndLogIn( const FString username )
{
	FString password;

	// [AK] Don't try to log in if the username is empty.
	if ( username.IsEmpty( ))
		return;

	if ( client_RetrieveCredentials( username, password ))
		client_RequestLogin( username.GetChars( ), password.GetChars( ));
	else
		Printf( "No password saved for user %s. Use login_add to save a password.\n", username.GetChars( ));
}
#endif

//*****************************************************************************
//	CONSOLE COMMANDS

CCMD( login )
{
	if ( NETWORK_GetState( ) != NETSTATE_CLIENT )
	{
		Printf( "You can't use login if you're not in an online game.\n" );
		return;
	}

	// [AK] There's no need to request another login if we're already logged in.
	if ( CLIENT_IsLoggedIn( ))
	{
		Printf( "You are already logged in.\n" );
		return;
	}

#ifdef ENABLE_AUTH_STORAGE
	if (argv.argc () <= 2)
	{
		FString username;
		if ( argv.argc () == 1 )
		{
			if ( strlen ( login_default_user ) == 0 )
			{
				Printf ( "No default username set. Use the variable login_default_user to set one.\n" );
				return;
			}
			else
				username = login_default_user;
		}
		else
			username = argv[1];

		CLIENT_RetrieveUserAndLogIn( username );
		return;
	}
#endif

	if ( argv.argc() == 3 )
		client_RequestLogin ( argv[1], argv[2] );
	else
		Printf ("Usage: login <username> <password>\n");
}

#ifdef ENABLE_AUTH_STORAGE
CCMD( login_add )
{
	if ( argv.argc() == 3 )
		client_SaveCredentials ( argv[1], argv[2] );
	else
		Printf ("Usage: login_add <username> <password>\n");
}
#endif
