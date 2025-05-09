//-----------------------------------------------------------------------------
//
// Zandronum Source
// Copyright (C) 2015 Teemu Piippo
// Copyright (C) 2015 Zandronum Development Team
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
// Filename: menu/multiplayermenu.cpp
//
//-----------------------------------------------------------------------------

// These directives must come before #include "optionmenuitems.h" line.
#define NO_IMP
#include <float.h>
#include "menu.h"
#include "c_dispatch.h"
#include "v_video.h"
#include "v_palette.h"
#include "d_event.h"
#include "c_bind.h"
#include "gi.h"
#include "optionmenuitems.h"

#include "team.h"
#include "gamemode.h"
#include "hardware.h"
#include "chat.h"
#include "cl_main.h"
#include "cl_demo.h"
#include "campaign.h"
#include "d_netinf.h"
#include "v_text.h"
#include "team.h"
#include "g_game.h"
#include "callvote.h"
#include "g_shared/pwo.h"
#include "deathmatch.h"
#include "duel.h"
#include "invasion.h"
#include "lastmanstanding.h"
#include "st_hud.h"
#include "voicechat.h"

static void M_StartSkirmishGame();
static void M_ClearBotSlots();
static void M_TextSizeScalarChanged();
static void M_CallKickVote();
static void M_CallMapVote();
static void M_CallLimitVote();
static void M_ExecuteIgnore();
static void M_JoinMenu();
static void M_JoinFromMenu();
static void M_DoJoinFromMenu();

CVAR ( Int, menu_botspawn0, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_botspawn1, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_botspawn2, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_botspawn3, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_botspawn4, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_botspawn5, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_botspawn6, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_botspawn7, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_botspawn8, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_botspawn9, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_botspawn10, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_botspawn11, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_botspawn12, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_botspawn13, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_botspawn14, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_botspawn15, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_teambotspawn0, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_teambotspawn1, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_teambotspawn2, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_teambotspawn3, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_teambotspawn4, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_teambotspawn5, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_teambotspawn6, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_teambotspawn7, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_teambotspawn8, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_teambotspawn9, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_teambotspawn10, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_teambotspawn11, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_teambotspawn12, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_teambotspawn13, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_teambotspawn14, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_teambotspawn15, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_teambotspawn16, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_teambotspawn17, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_teambotspawn18, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_teambotspawn19, -1, CVAR_ARCHIVE )
CVAR ( Int, menu_skirmishlevel, 0, CVAR_ARCHIVE )
CVAR ( Int, menu_skirmishgamemode, 0, CVAR_ARCHIVE )
CVAR ( Int, menu_skirmishmodifier, 0, CVAR_ARCHIVE )
CVAR ( Int, menu_callvotemap, 0, 0 )
CVAR ( Bool, menu_callvoteintermission, true, 0 )
CVAR ( Bool, menu_callvotenextsecret, false, 0 )
CVAR ( String, menu_callvotereason, "", 0 )
CVAR ( String, menu_callvoteflag, "", 0 )
CVAR ( Int, menu_callvotelimit, 0, 0 )
CVAR ( Float, menu_callvotevalue, 0, 0 )
CVAR ( Int, menu_callvoteplayer, 0, 0 )
CVAR ( Bool, menu_callvoteban, 0, 0 )
CVAR ( Int, menu_jointeamidx, 0, 0 )
CVAR ( Int, menu_joinclassidx, 0, 0 )
CVAR ( Int, menu_playerindex, 0, 0 )
CVAR ( Int, menu_ignoreduration, 0, 0 )
CVAR ( Bool, menu_ignoreaction, true, 0 )
CVAR ( Bool, menu_ignoretype, false, 0 )
CVAR ( Float, menu_voicevolume, 1.0f, 0 )
CVAR ( String, menu_authusername, 0, 0 )
CVAR ( String, menu_authpassword, 0, 0 )
CVAR ( Int, menu_skirmishskill, 0, CVAR_ARCHIVE )
CVAR ( Int, menu_skirmishbotskill, 0, CVAR_ARCHIVE )
CVAR ( Int, menu_skirmishtimelimit, 0, CVAR_ARCHIVE )
CVAR ( Int, menu_skirmishfraglimit, 0, CVAR_ARCHIVE )
CVAR ( Int, menu_skirmishpointlimit, 0, CVAR_ARCHIVE )
CVAR ( Int, menu_skirmishduellimit, 0, CVAR_ARCHIVE )
CVAR ( Int, menu_skirmishwinlimit, 0, CVAR_ARCHIVE )
CVAR ( Int, menu_skirmishwavelimit, 0, CVAR_ARCHIVE )
CVAR ( String, menu_rconpassword, 0, 0 )

CUSTOM_CVAR ( Int, menu_textsizescalar, 0, CVAR_NOINITCALL )
{
	M_TextSizeScalarChanged();
}

EXTERN_CVAR ( String, playerclass )

// [CW] The number of bot slots defined in the bot team setup menu.
enum
{
	MAX_BOTS_PER_TEAM = 5,
	MAX_BOTTEAM_SLOTS = MAX_TEAMS * MAX_BOTS_PER_TEAM,
};

static FIntCVar* const BotTeamSlots[MAX_BOTTEAM_SLOTS] =
{
	&menu_teambotspawn0,  &menu_teambotspawn1,  &menu_teambotspawn2,  &menu_teambotspawn3,
	&menu_teambotspawn4,  &menu_teambotspawn5,  &menu_teambotspawn6,  &menu_teambotspawn7,
	&menu_teambotspawn8,  &menu_teambotspawn9,  &menu_teambotspawn10, &menu_teambotspawn11,
	&menu_teambotspawn12, &menu_teambotspawn13, &menu_teambotspawn14, &menu_teambotspawn15,
	&menu_teambotspawn16, &menu_teambotspawn17, &menu_teambotspawn18, &menu_teambotspawn19,
};

static FIntCVar* const BotSlots[] =
{
	&menu_botspawn0,  &menu_botspawn1,  &menu_botspawn2,  &menu_botspawn3,
	&menu_botspawn4,  &menu_botspawn5,  &menu_botspawn6,  &menu_botspawn7,
	&menu_botspawn8,  &menu_botspawn9,  &menu_botspawn10, &menu_botspawn11,
	&menu_botspawn12, &menu_botspawn13, &menu_botspawn14, &menu_botspawn15,
};

static int g_LastRconAccessRequest = -1;

// =================================================================================================
//
// DBotSetupMenu
//
// Bot setup menu (for skirmish)
//
// =================================================================================================

class DBotSetupMenu : public DOptionMenu
{
	DECLARE_CLASS( DBotSetupMenu, DOptionMenu )

public:
	DBotSetupMenu(){}

	void AddBotSlot ( FOptionMenuDescriptor* desc, int slotId, FIntCVar* cvar )
	{
		FString label;
		label.Format( "Slot %d", slotId + 1 );
		FOptionMenuItem* it = new FOptionMenuItemOption ( label, cvar->GetName(),
			"ZA_Bots", NULL, 0 );
		desc->mItems.Push( it );
	}

	void Init ( DMenu* parent = NULL, FOptionMenuDescriptor* desc = NULL )
	{
		if ( desc != NULL )
		{
			desc->mItems.Clear();
			GAMEMODE_e mode = static_cast<GAMEMODE_e>( *menu_skirmishgamemode );

			if ( GAMEMODE_GetFlags( mode ) & GMF_PLAYERSONTEAMS )
			{
				for ( unsigned int teamId = 0; teamId < teams.Size(); ++teamId )
				{
					// Add a team name header
					desc->mItems.Push( new FOptionMenuItemStaticText( teams[teamId].Name, true ) );

					// Add bot slots
					for ( unsigned int i = 0; i < MAX_BOTS_PER_TEAM; ++i )
					{
						unsigned int slotId = ( teamId * MAX_BOTS_PER_TEAM ) + i;
						assert( slotId < MAX_BOTTEAM_SLOTS );
						AddBotSlot( desc, slotId, BotTeamSlots[slotId] );
					}
				}
			}
			else
			{
				for ( unsigned int i = 0; i < countof( BotSlots ); ++i )
					AddBotSlot( desc, i, BotSlots[i] );
			}

			// A few static options
			desc->mItems.Push( new FOptionMenuItemStaticText ( " ", true ));
			desc->mItems.Push( new FOptionMenuItemCommand ( "Clear List", "menu_clearbotslots" ));
			desc->mItems.Push( new FOptionMenuItemCommand ( "Start Game", "menu_startskirmish" ));
		}

		// DOptionMenu::Init expects items to be already filled so this comes after the for loop!
		Super::Init( parent, desc );
	}
};

IMPLEMENT_CLASS( DBotSetupMenu )

// =================================================================================================
//
// [TP] M_StartSkirmishGame
//
// Starts a skirmish game. Ported here from the old m_options.cpp
//
// =================================================================================================

static void M_StartSkirmishGame()
{
	// [TP] Improved this sanity check
	if (( menu_skirmishlevel < 0 ) || ( (unsigned) menu_skirmishlevel >= wadlevelinfos.Size() ))
	{
		// Invalid level selected.
		return;
	}

	GAMEMODE_e mode = static_cast<GAMEMODE_e>( *menu_skirmishgamemode );
	MODIFIER_e modifier = static_cast<MODIFIER_e>( *menu_skirmishmodifier );

	// Tell the server we're leaving the game.
	if ( NETWORK_GetState( ) == NETSTATE_CLIENT )
	{
		// [AK] Don't let clients with RCON access start a new skirmish game.
		if ( CLIENT_HasRCONAccess( ))
		{
			Printf( "You can't start a skirmish game while you have RCON access. Use \"rcon_logout\" to log out first.\n" );
			M_ClearMenus( );
			return;
		}

		CLIENT_QuitNetworkGame( NULL );
	}

	NETWORK_SetState( NETSTATE_SINGLE );
	CAMPAIGN_DisableCampaign( );
	GAMEMODE_SetCurrentMode( mode );
	GAMEMODE_SetModifier( modifier );

	// [TP] Activate the desired skill levels
	{
		UCVarValue vval;
		vval.Int = menu_skirmishskill;
		gameskill.ForceSet( vval, CVAR_Int );
		vval.Int = menu_skirmishbotskill;
		botskill.ForceSet( vval, CVAR_Int );
	}

	timelimit = static_cast<float> ( menu_skirmishtimelimit );
	fraglimit = menu_skirmishfraglimit;
	pointlimit = menu_skirmishpointlimit;
	duellimit = menu_skirmishduellimit;
	winlimit = menu_skirmishwinlimit;
	wavelimit = menu_skirmishwavelimit;

	// [TP] Set default dmflags so that the gameplay is okay. ZDoom changed the gameplay settings
	// to set dmflags directly, so we cannot rely on that anymore.
	GAME_SetDefaultDMFlags();

	// [BB] In non-cooperative game modes we need to enable multiplayer emulation,
	// otherwise respawning and player class selection won't work properly.
	if (( GAMEMODE_GetCurrentFlags() & GMF_COOPERATIVE ) == 0 )
		NETWORK_SetState( NETSTATE_SINGLE_MULTIPLAYER );

	// Remove all the existing bots.
	BOTS_RemoveAllBots( false );

	// Potentially end playing demos.
	if ( demoplayback )
	{
		C_RestoreCVars();
		demoplayback = false;
		D_SetupUserInfo();
	}

	if ( CLIENTDEMO_IsPlaying() )
	{
		CLIENTDEMO_SetPlaying( false );
		D_SetupUserInfo();
	}

	// [BB] We may not call G_InitNew here, this causes crashes in the software renderer,
	// for example when going from D2IG03 to D2IG04 (both started from the skirmish menu).
	// Since G_InitNew calls BOTSPAWN_ClearTable() we have to call BOTSPAWN_BlockClearTable()
	// to protect the table. Not very elegant, but seems to work.
	G_DeferedInitNew( wadlevelinfos[menu_skirmishlevel].mapname );
	BOTSPAWN_ClearTable();
	BOTSPAWN_BlockClearTable();
	gamestate = gamestate == GS_FULLCONSOLE ? GS_HIDECONSOLE : gamestate;
	M_ClearMenus();

	// Initialize bot spawn times.
	if ( GAMEMODE_GetFlags( mode ) & GMF_PLAYERSONTEAMS )
	{
		for ( unsigned int i = 0; i < countof( BotTeamSlots ); ++i )
		{
			if ( *BotTeamSlots[i] < 0 )
				continue; // No bot selected

			unsigned int botId = **BotTeamSlots[i];

			if ( BOTINFO_GetRevealed( botId ) && ( BOTINFO_GetName( botId ) != NULL ))
			{
				unsigned int teamId = i / MAX_BOTS_PER_TEAM;
				FString botName = BOTINFO_GetName( botId );
				V_ColorizeString( botName );
				V_RemoveColorCodes( botName );

				if ( teamId < teams.Size() )
					BOTSPAWN_AddToTable( botName, TEAM_GetName( teamId ));
			}
		}
	}
	else
	{
		for ( unsigned int i = 0; i < countof( BotSlots ); ++i )
		{
			if ( *BotSlots[i] < 0 )
				continue; // No bot selected

			unsigned int botId = **BotSlots[i];

			if ( BOTINFO_GetRevealed( botId ) && ( BOTINFO_GetName( botId ) != NULL ))
			{
				FString botName = BOTINFO_GetName( botId );
				V_ColorizeString( botName );
				V_RemoveColorCodes( botName );
				BOTSPAWN_AddToTable( botName, NULL );
			}
		}
	}
}

// =================================================================================================
//
// [TP] M_ClearBotSlots
//
// Clears the bot slots. Duh.
//
// =================================================================================================

static void M_ClearBotSlots()
{
	GAMEMODE_e mode = static_cast<GAMEMODE_e>( *menu_skirmishgamemode );

	if ( GAMEMODE_GetFlags( mode ) & GMF_PLAYERSONTEAMS )
	{
		for ( unsigned int i = 0; i < countof( BotTeamSlots ); ++i )
			*BotTeamSlots[i] = -1;
	}
	else
	{
		for ( unsigned int i = 0; i < countof( BotSlots ); ++i )
			*BotSlots[i] = -1;
	}
}

// =================================================================================================
//
//
//
//
// =================================================================================================

static void M_TextSizeScalarChanged()
{
	int mode = 0;
	int width;
	int height;
	bool letterBox;

	// [BB] check true
	if ( Video != NULL )
	{
		Video->StartModeIterator( 8, true );
		while ( Video->NextMode( &width, &height, &letterBox ))
		{
			if ( mode == menu_textsizescalar )
			{
				con_virtualwidth = width;
				con_virtualheight = height;
				break;
			}

			mode++;
		}
	}
}

// =================================================================================================
//
// DTextScalingMenu
//
// It is the text scaling menu.
//
// =================================================================================================

class DTextScalingMenu : public DOptionMenu
{
	DECLARE_CLASS( DTextScalingMenu, DOptionMenu )

public:
	DTextScalingMenu() {}

	void Init ( DMenu* parent = NULL, FOptionMenuDescriptor* desc = NULL )
	{
		Super::Init( parent, desc );

		int width;
		int height;
		bool letterBox;
		int numModes = 0;
		int textsizescalar = 0;
		bool bFoundExactMatch = false;

		Video->StartModeIterator( 8, true );
		while ( Video->NextMode( &width, &height, &letterBox ))
		{
			// [AK] Don't change the slider if we already found a mode that matches the virtual screen's size.
			if (( bFoundExactMatch == false ) && ( width <= con_virtualwidth ) && ( height <= con_virtualheight ))
			{
				if (( width == con_virtualwidth ) && ( height == con_virtualheight ))
					bFoundExactMatch = true;

				textsizescalar = numModes;
			}

			numModes++;
		}
		// [BB] We may not invoke the callback of menu_textsizescalar above since it changes con_virtualwidth.
		menu_textsizescalar = textsizescalar;

		// [TP] Update the maximum of the menu_textsizescalar slider.
		FOptionMenuItem* it = desc->GetItem( "menu_textsizescalar" );
		if ( it )
			it->SetValue( FOptionMenuSliderBase::SLIDER_MAXIMUM, numModes - 1);
	}

	void Drawer()
	{
		Super::Drawer();

		// [TP] Ported here from the old m_options.cpp
		HUD_DrawTextCleanCentered( SmallFont, CR_WHITE, 96, "This is clean text." );
		HUD_DrawTextCentered( SmallFont, CR_WHITE, static_cast<int>( screen->GetHeight() / 200.0f ) * 112, "This is unscaled text.", false );

		float yscale = con_virtualheight / 200.0f;
		HUD_DrawTextCentered( SmallFont, CR_WHITE, static_cast<int>( 128 * yscale ) - SmallFont->GetHeight() / 2, "This is scaled text.", true );
	}
};

IMPLEMENT_CLASS( DTextScalingMenu )

// =================================================================================================
//
//
//
//
//
// =================================================================================================

class DVotingSetupMenu : public DOptionMenu
{
	DECLARE_CLASS( DVotingSetupMenu, DOptionMenu )
	TArray<FOptionMenuItem*> mItems;

public:
	void Init ( DMenu* parent, FOptionMenuDescriptor* desc )
	{
		mItems.Reserve( desc->mItems.Size() );

		for ( unsigned int i = 0; i < desc->mItems.Size(); ++i )
			mItems[i] = desc->mItems[i];

		UpdateItems();
		Super::Init( parent, desc );
	}

	void UpdateItems()
	{
		for ( unsigned int i = 0; i < mItems.Size(); ++i )
		{
			if ( mItems[i]->GetAction( NULL ) != NAME_sv_nocallvote )
				mItems[i]->SetDisabled( sv_nocallvote == 1 );
		}
	}

	void CVarChanged ( FBaseCVar* cvar )
	{
		if ( cvar == &sv_nocallvote )
			UpdateItems();
	}
};

IMPLEMENT_CLASS( DVotingSetupMenu )

// =================================================================================================
//
// DWeaponSetupMenu
//
// The weapon setup menu calls PWO code to fill in the PWO items.
//
// =================================================================================================

EXTERN_CVAR ( Int, switchonpickup )

class DWeaponSetupMenu : public DOptionMenu
{
	DECLARE_CLASS( DWeaponSetupMenu, DOptionMenu )
	TArray<FOptionMenuItem*> mPWOItems;

public:
	void Init ( DMenu* parent, FOptionMenuDescriptor* desc )
	{
		static bool needPWOItems = true;
		static unsigned int PWOStartIndex = 0;

		if (( desc != NULL ) && needPWOItems )
		{
			PWOStartIndex = desc->mItems.Size();
			PWO_FillMenu ( *desc );
			needPWOItems = false;
		}

		// Mark down what our PWO items are so we can alter them later
		mPWOItems.Reserve( desc->mItems.Size() - PWOStartIndex );

		for ( unsigned int i = 0; i < desc->mItems.Size() - PWOStartIndex; ++i )
		{
			mPWOItems[i] = desc->mItems[PWOStartIndex + i];
			mPWOItems[i]->SetDisabled( switchonpickup != 3 );
		}

		Super::Init( parent, desc );
	}

	void CVarChanged ( FBaseCVar* cvar )
	{
		if ( cvar == &switchonpickup )
		{
			for ( unsigned int i = 0; i < mPWOItems.Size(); ++i )
				mPWOItems[i]->SetDisabled( switchonpickup != 3 );
		}
	}
};

IMPLEMENT_CLASS( DWeaponSetupMenu )

// =================================================================================================
//
// [AK] DVoiceChatMenu
//
// The voice chat options menu, which initializes the record driver list.
//
// =================================================================================================

class DVoiceChatMenu : public DOptionMenu
{
	DECLARE_CLASS( DVoiceChatMenu, DOptionMenu )

public:
	void Init( DMenu *parent, FOptionMenuDescriptor *desc )
	{
		FOptionValues **opt = OptionValues.CheckKey( "ZA_RecordDrivers" );

		if ( opt != nullptr )
		{
			TArray<FString> recordDriverList;
			VOIPController::GetInstance( ).RetrieveRecordDrivers( recordDriverList );

			RefreshRecordDriverList( *opt, recordDriverList );
		}

		Super::Init( parent, desc );
	}

	virtual void Ticker( void )
	{
		Super::Ticker( );

		FOptionValues **opt = OptionValues.CheckKey( "ZA_RecordDrivers" );
		TArray<FString> recordDriverList;

		VOIPController::GetInstance( ).RetrieveRecordDrivers( recordDriverList );

		if ( opt != nullptr )
		{
			unsigned int numRecordDrivers = ( *opt )->mValues.Size( );

			// [AK] "None" doesn't count as a record driver.
			if ( stricmp(( *opt )->mValues[0].Text, "None" ) == 0 )
				numRecordDrivers = 0;

			// [AK] Refresh the list of record drivers if any got added or removed.
			if ( numRecordDrivers != recordDriverList.Size( ))
				RefreshRecordDriverList( *opt, recordDriverList );
		}

		// [AK] Stop the microphone test if we're not recording (e.g. the device was disconnected).
		if (( VOIPController::GetInstance( ).IsRecording( ) == false ) && ( VOIPController::GetInstance( ).IsTestingMicrophone( )))
		{
			VOIPController::GetInstance( ).SetMicrophoneTest( false );

			// [AK] If no record drivers are connected, then the microphone test bar shouldn't be selected.
			if ( recordDriverList.Size( ) == 0 )
			{
				FOptionMenuItem *it = mDesc->GetItem( "MicTestBar" );

				if (( it != nullptr ) && ( it == mDesc->mItems[mDesc->mSelectedItem] ))
					mDesc->mSelectedItem = FirstSelectable( );
			}
		}
	}

	virtual void Close( void )
	{
		// [AK] Stop testing the microphone when we exit the menu.
		if ( VOIPController::GetInstance( ).IsTestingMicrophone( ))
			VOIPController::GetInstance( ).SetMicrophoneTest( false );

		Super::Close( );
	}

private:
	void RefreshRecordDriverList( FOptionValues *opt, TArray<FString> recordDriverList )
	{
		FOptionValues::Pair pair;

		if ( opt == nullptr )
			return;

		opt->mValues.Clear( );

		if ( recordDriverList.Size( ) > 0 )
		{
			for ( unsigned int i = 0; i < recordDriverList.Size( ); i++ )
			{
				pair.Value = i;
				pair.Text = recordDriverList[i];
				opt->mValues.Push( pair );
			}
		}
		else
		{
			pair.Value = 0;
			pair.Text = "None";
			opt->mValues.Push( pair );
		}

		if ( static_cast<unsigned>( voice_recorddriver ) >= opt->mValues.Size( ))
			voice_recorddriver = opt->mValues.Size( ) - 1;
	}
};

IMPLEMENT_CLASS( DVoiceChatMenu )

// =================================================================================================
//
// [AK] DPlayerListMenu
//
// The player list menu, allowing the user to (un)ignore another player's chat messages or voice,
// or adjust the volume of their VoIP channel.
//
// =================================================================================================

class DPlayerListMenu : public DOptionMenu
{
	DECLARE_CLASS( DPlayerListMenu, DOptionMenu )

public:
	void Init( DMenu *parent, FOptionMenuDescriptor *desc )
	{
		// [AK] Set the volume slider to the selected player's VoIP channel volume. If no valid
		// player is selected, then reset the slider back to default.
		if ( PLAYER_IsValidPlayer( menu_playerindex ))
			menu_voicevolume = VOIPController::GetInstance( ).GetChannelVolume( menu_playerindex );
		else
			menu_voicevolume.ResetToDefault( );

		Super::Init( parent, desc );
	}

	void CVarChanged( FBaseCVar *cvar )
	{
		if ( PLAYER_IsValidPlayer( menu_playerindex ) == false )
			return;

		// [AK] If the selected player has changed, set the volume slider to their VoIP channel volume.
		if ( cvar == &menu_playerindex )
			menu_voicevolume = VOIPController::GetInstance( ).GetChannelVolume( menu_playerindex );
		// [AK] If the volume slider has changed, update the selected player's VoIP channel volume.
		else if (( cvar == &menu_voicevolume ) && ( mDesc->mItems[mDesc->mSelectedItem]->GetAction( nullptr ) == FName( "menu_voicevolume" )))
			VOIPController::GetInstance( ).SetChannelVolume( menu_playerindex, menu_voicevolume, true );
	}
};

IMPLEMENT_CLASS( DPlayerListMenu )

// =================================================================================================
//
// [TRSR/AK] DCallVoteMenu
//
// The call vote menu, which automatically adds the list of eligible custom vote menus.
//
// =================================================================================================

class DCallVoteMenu : public DOptionMenu
{
	DECLARE_CLASS( DCallVoteMenu, DOptionMenu )

public:
	void Init( DMenu *parent, FOptionMenuDescriptor *desc )
	{
		const TArray<VOTETYPE_s> &votes = CALLVOTE_GetCustomVotes( );

		for ( unsigned int i = 0; i < votes.Size( ); i++ )
		{
			if ( votes[i].menu.IsNotEmpty( ))
			{
				FOptionMenuItem *it = new FOptionMenuItemSubmenu( votes[i].menuName.GetChars( ), votes[i].menu.GetChars( ));
				mCustomVoteItems.Push( it );
				desc->mItems.Push( it );
			}
		}

		Super::Init( parent, desc );
	}

	virtual void Close( void )
	{
		// [AK] When this menu closes, any custom vote menus that were added when it was opened must
		// be removed from its descriptor and deleted now. Otherwise, the custom vote menus will be
		// duplicated when this menu is opened again.
		for ( unsigned int i = 0; i < mCustomVoteItems.Size( ); i++ )
		{
			for ( unsigned int j = 0; j < mDesc->mItems.Size( ); j++ )
			{
				if ( mDesc->mItems[j] == mCustomVoteItems[i] )
				{
					mDesc->mItems.Delete( j );
					break;
				}
			}

			delete mCustomVoteItems[i];
			mCustomVoteItems[i] = nullptr;
		}

		mCustomVoteItems.Clear( );
		Super::Close( );
	}

private:
	TDeletingArray<FOptionMenuItem *> mCustomVoteItems;
};

IMPLEMENT_CLASS( DCallVoteMenu )

// =================================================================================================
//
//
//
//
//
// =================================================================================================

static void M_CallKickVote()
{
	if ( PLAYER_IsValidPlayer( menu_callvoteplayer ))
	{
		FString name = players[menu_callvoteplayer].userinfo.GetName();
		V_RemoveColorCodes( name );
		V_EscapeBacklashes( name );

		FString command;
		command.Format( "callvote %s \"%s\" \"%s\"",
			menu_callvoteban ? "kick" : "forcespec",
			name.GetChars(),
			*menu_callvotereason );
		C_DoCommand( command );
		M_ClearMenus();
	}
}

// =================================================================================================
//
//
//
//
//
// =================================================================================================

static void M_CallMapVote()
{
	if (( menu_callvotemap >= 0 )
		&& ( static_cast<unsigned int>( *menu_callvotemap ) < wadlevelinfos.Size() ) )
	{
		FString command;
		command.Format( "callvote %s \"%s\" \"%s\"",
			menu_callvoteintermission ? "changemap" : "map",
			wadlevelinfos[menu_callvotemap].mapname,
			*menu_callvotereason );
		C_DoCommand( command );
		M_ClearMenus();
	}
}

// =================================================================================================
//
//
//
//
//
// =================================================================================================

static void M_CallLimitVote()
{
	static const char* votetypes[] =
	{
		"fraglimit",
		"timelimit",
		"winlimit",
		"duellimit",
		"pointlimit",
	};

	if (( menu_callvotelimit >= 0 ) && ( unsigned ( menu_callvotelimit ) < countof( votetypes )))
	{
		FString command;
		command.Format( "callvote %s %s \"%s\"",
			votetypes[menu_callvotelimit],
			menu_callvotevalue.GetGenericRep( CVAR_String ).String,
			*menu_callvotereason );
		C_DoCommand( command );
		M_ClearMenus();
	}
}

// =================================================================================================
//
//
//
//
//
// =================================================================================================

static void M_CallFlagVote()
{
	const char *flagName = menu_callvoteflag.GetGenericRep( CVAR_String ).String;

	if ( strlen( flagName ) > 0 )
	{
		FString command;
		command.Format( "callvote %s %s \"%s\"",
			flagName,
			menu_callvotevalue.GetGenericRep( CVAR_String ).String,
			*menu_callvotereason );
		C_DoCommand( command );
		M_ClearMenus();
	}
}

// =================================================================================================
//
//
//
//
//
// =================================================================================================

static void M_CallNextMapVote()
{
	FString command;
	command.Format("callvote %s \"%s\"",
		menu_callvotenextsecret ? "nextsecret" : "nextmap",
		*menu_callvotereason);
	C_DoCommand(command);
	M_ClearMenus();
}

// =================================================================================================
//
//
//
//
//
// =================================================================================================

static void M_CallResetMapVote()
{
	FString command;
	command.Format( "callvote resetmap \"%s\"",
		*menu_callvotereason );
	C_DoCommand( command );
	M_ClearMenus();
}

//=================================================================================================
//
// [TP] M_ExecuteIgnore
//
// Ignores or unignores a player, depending what the user selected from the menu.
//
//==================================================================================================

static void M_ExecuteIgnore()
{
	if ( PLAYER_IsValidPlayer( menu_playerindex ) )
	{
		FString command;

		if ( menu_ignoreaction )
		{
			// Ignore a player
			command.Format( "%signore_idx %d %d", menu_ignoretype ? "voice_" : "", *menu_playerindex, *menu_ignoreduration );
		}
		else
		{
			// Unignore a player
			command.Format( "%sunignore_idx %d", menu_ignoretype ? "voice_" : "", *menu_playerindex );
		}

		C_DoCommand( command );
	}
}

//=============================================================================
//
// [TP] M_JoinMenu
//
// Activates the "join game" menu that displays when the player presses space
// to join while spectating.
//
//=============================================================================

static void M_JoinMenu()
{
	if ( GAMEMODE_GetCurrentFlags() & GMF_DEADSPECTATORS
		&& players[consoleplayer].bDeadSpectator )
	{
		Printf( "You cannot rejoin the game until the round is over!\n" );
		return;
	}

	M_StartControlPanel( true );

	// ST/CTF/domination without a selection room, or another team game.
	// [EP] Use TEAM_ShouldJoinTeam here.
	if ( TEAM_ShouldJoinTeam() )
	{
		M_SetMenu( "ZA_JoinTeamMenu", -1 );
	}
	else
	{
		M_SetMenu( "ZA_JoinMenu", -1 );
	}
}

//==================================================================================================
//
// [TP] M_JoinFromMenu
//
// Joins from the "join menu" team (unless we decide to choose a class instead).
//
//==================================================================================================

static void M_JoinFromMenu()
{
	// [TP/EP] This also stems from m_options.cpp
	if ( PlayerClasses.Size() > 1 )
	{
		M_SetMenu( "ZA_SelectClassMenu", 1 );
	}
	else
		M_DoJoinFromMenu();
}

//==================================================================================================
//
// [TP] M_DoJoinFromMenu
//
// Joins from the "join menu" team. For real.
//
//==================================================================================================

static void M_DoJoinFromMenu()
{
	M_ClearMenus();

	// ST/CTF/domination without a selection room, or another team game.
	// [EP] Use TEAM_ShouldJoinTeam also here.
	if ( TEAM_ShouldJoinTeam() )
	{
		if ( static_cast<unsigned>( menu_jointeamidx ) == teams.Size() )
		{
			C_DoCommand( "team random" );
		}
		else
		{
			char command[1024];
			mysnprintf( command, sizeof command, "team \"%s\"",
				teams[menu_jointeamidx].Name.GetChars() );
			AddCommandString( command );
		}
	}
	else
	{
		C_DoCommand( "join" );
	}
}

// =================================================================================================
//
//
//
//
//
// =================================================================================================

static void M_AutoSelect()
{
	M_ClearMenus();
	C_DoCommand( "team autoselect" );
}

// =================================================================================================
//
//
//
//
// =================================================================================================

void M_RconAccessGranted()
{
	// [TP] We got RCON access. If this was done from the menu, forward the user to the server setup menu.
	if (( g_LastRconAccessRequest > 0 ) && ( g_LastRconAccessRequest > gametic - 10 * TICRATE ))
		M_SetMenu( "ZA_ServerSetupMenu" );
}

// =================================================================================================
//
//
//
//
// =================================================================================================

void M_SetLastRconAccessRequest( int tic )
{
	g_LastRconAccessRequest = tic;
}

// =================================================================================================
//
//
//
//
// =================================================================================================

CCMD ( menu_startskirmish )
{
	M_StartSkirmishGame();
}

CCMD ( menu_clearbotslots )
{
	M_ClearBotSlots();
}

CCMD ( menu_joingame )
{
	M_JoinFromMenu();
}

CCMD ( menu_joingamewithclass )
{
	// [SB] The random option isn't valid when NoRandomPlayerClass is enabled in MAPINFO.
	if ( menu_joinclassidx >= 0
		&& static_cast<unsigned>( menu_joinclassidx ) < PlayerClasses.Size() + ( gameinfo.norandomplayerclass ? 0 : 1 ) )
	{
		if ( !gameinfo.norandomplayerclass && static_cast<unsigned>( menu_joinclassidx ) == PlayerClasses.Size() )
			playerclass = "Random";
		else
			playerclass = GetPrintableDisplayName( PlayerClasses[menu_joinclassidx].Type );

		// [AK] It's not enough to change the playerclass CVar to the new class's name.
		// The local player's userinfo must be updated too.
		players[consoleplayer].userinfo.PlayerClassChanged( playerclass );

		M_DoJoinFromMenu();
	}
}

CCMD ( menu_ignore )
{
	M_ExecuteIgnore();
}

CCMD ( menu_spectate )
{
	M_ClearMenus();

	if ( gamestate == GS_LEVEL )
		C_DoCommand( "spectate" );
	else
		M_StartMessage( "You must be in a game to spectate.\n\npress a key.", 1 );
}

CCMD ( menu_join )
{
	if ( players[consoleplayer].bSpectating == false )
	{
		M_StartMessage( "You must be a spectator to join.\n\npress a key.", 1 );
		return;
	}

	M_JoinMenu();
}

// [RC] Moved switch team to the Multiplayer menu
CCMD ( menu_changeteam )
{
	// Clear the menus, and send the changeteam command.
	M_ClearMenus();

	if ( gamestate == GS_LEVEL )
		C_DoCommand( "changeteam" );
	else
		M_StartMessage( "You must be in a game to switch teams.\n\npress a key.", 1 );
}

CCMD ( menu_callkickvote )
{
	M_CallKickVote();
}

CCMD ( menu_callmapvote )
{
	M_CallMapVote();
}

CCMD ( menu_calllimitvote )
{
	M_CallLimitVote();
}

CCMD ( menu_callflagvote )
{
	M_CallFlagVote();
}

CCMD ( menu_callnextmapvote )
{
	M_CallNextMapVote();
}

CCMD ( menu_callresetmapvote )
{
	M_CallResetMapVote();
}

CCMD ( menu_autoselect )
{
	M_AutoSelect();
}

CCMD ( menu_disconnect )
{
	if ( NETWORK_GetState() == NETSTATE_CLIENT )
	{
		M_ClearMenus();
		C_DoCommand( "disconnect" );
	}
	else
		M_StartMessage( "You must be in a netgame to disconnect.\n\npress a key.", 1 );
}

CCMD ( menu_login )
{
	if ( NETWORK_GetState() == NETSTATE_CLIENT )
	{
		M_ClearMenus();

		FString command;
		command.Format( "login \"%s\" \"%s\"", *menu_authusername, *menu_authpassword );
		C_DoCommand( command );
	}
	else
		M_StartMessage( "You must be in a netgame to log in.\n\npress a key.", 1 );
}

CCMD ( menu_rconlogin )
{
	M_ClearMenus();

	if ( NETWORK_GetState() == NETSTATE_CLIENT )
	{
		FString command;
		command.Format ("send_password \"%s\"", *menu_rconpassword );
		C_DoCommand( command );
		M_SetLastRconAccessRequest( gametic );
	}
}
