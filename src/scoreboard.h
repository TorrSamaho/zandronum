//-----------------------------------------------------------------------------
//
// Skulltag Source
// Copyright (C) 2002 Brad Carney
// Copyright (C) 2021-2023 Adam Kaminski
// Copyright (C) 2007-2023 Skulltag/Zandronum Development Team
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
// Filename: scoreboard.h
//
// Description: Contains scoreboard structures and prototypes
//
//-----------------------------------------------------------------------------

#ifndef __SCOREBOARD_H__
#define __SCOREBOARD_H__

#include <list>
#include <set>
#include "gamemode.h"
#include "teaminfo.h"
#include "tarray.h"
#include "v_text.h"
#include "i_system.h"

#include "scoreboard_enums.h"

//*****************************************************************************
//	DEFINES

//*****************************************************************************
//
// [AK] Column templates, either data or composite.
//
enum COLUMNTEMPLATE_e
{
	COLUMNTEMPLATE_UNKNOWN,
	COLUMNTEMPLATE_DATA,
	COLUMNTEMPLATE_COMPOSITE,
};

//*****************************************************************************
//
// [AK] What kind of content a data column uses, either text or graphic.
//
enum DATACONTENT_e
{
	DATACONTENT_UNKNOWN,
	DATACONTENT_TEXT,
	DATACONTENT_GRAPHIC,
};

//*****************************************************************************
//
// [AK] Margin types, either a header/footer, or a team or spectator header.
//
enum MARGINTYPE_e
{
	MARGINTYPE_HEADER_OR_FOOTER,
	MARGINTYPE_TEAM,
	MARGINTYPE_SPECTATOR,

	// [AK] This is only used to allow special values for certain margin commands
	// ( e.g. DrawString, DrawColor, or DrawTexture ) to be used in all margins.
	MARGINTYPE_ALL,
};

//*****************************************************************************
//
// [AK] What kind of stuff on the scoreboard the user wants to customize.
//
enum CustomizeScoreboardFlag
{
	CUSTOMIZE_TEXT =			1 << 0,
	CUSTOMIZE_BORDERS =			1 << 1,
	CUSTOMIZE_BACKGROUND =		1 << 2,
	CUSTOMIZE_ROWBACKGROUNDS =	1 << 3,
};

//*****************************************************************************
//	CLASSES

//*****************************************************************************
//
// [AK] PlayerValue
//
// Allows for easy storage of a player's value with different data types.
//
//*****************************************************************************

class PlayerValue
{
public:
	PlayerValue( void ) : DataType( DATATYPE_UNKNOWN ) { }
	PlayerValue( const PlayerValue &Other ) : DataType( DATATYPE_UNKNOWN ) { TransferValue( Other ); }

	~PlayerValue( void ) { DeleteString( ); }

	inline DATATYPE_e GetDataType( void ) const { return DataType; }
	template <typename Type> Type GetValue( void ) const;
	template <typename Type> void SetValue( Type NewValue );

	FString ToString( void ) const;
	void FromString( const char *pszString, const DATATYPE_e NewDataType );

	void operator= ( const PlayerValue &Other ) { TransferValue( Other ); }
	bool operator== ( const PlayerValue &Other ) const;

private:
	template <typename Type> struct Trait
	{
		static const DATATYPE_e DataType;
		static const Type Zero;
	};

	template <typename Type> Type RetrieveValue( void ) const;
	template <typename Type> void ModifyValue( Type NewValue );

	void TransferValue( const PlayerValue &Other );
	void DeleteString( void );

	DATATYPE_e DataType;
	union
	{
		int Int;
		bool Bool;
		float Float;
		const char *String;
		FTexture *Texture;
	};
};

//*****************************************************************************
//
// [AK] PlayerData
//
// An array of values for each player, used by custom columns to store data.
//
//*****************************************************************************

class PlayerData
{
public:
	PlayerData( FScanner &sc, BYTE NewIndex );

	DATATYPE_e GetDataType( void ) const { return DataType; }
	PlayerValue GetValue( const ULONG ulPlayer ) const;
	PlayerValue GetDefaultValue( void ) const;
	BYTE GetIndex( void ) const { return Index; }
	void SetValue( const ULONG ulPlayer, const PlayerValue &Value );
	void ResetToDefault( const ULONG ulPlayer, const bool bInformClients );

private:
	DATATYPE_e DataType;
	PlayerValue Val[MAXPLAYERS];
	BYTE Index;

	// [AK] The default value as a string. MAPINFO lumps are parsed before any
	// graphics are loaded, so if a custom column uses textures as data, then
	// this is why the value must be stored as a string.
	FString DefaultValString;
};

//*****************************************************************************
//
// [AK] ScoreColumn
//
// A base class for all column types (e.g. data or composite) that will appear
// on the scoreboard. Columns are responsible for updating themselves and
// drawing their contents when needed.
//
//*****************************************************************************

struct Scoreboard;

class ScoreColumn
{
public:
	ScoreColumn( const char *pszName );
	virtual ~ScoreColumn( void ) { }

	Scoreboard *GetScoreboard( void ) const { return pScoreboard; }
	const char *GetInternalName( void ) const { return InternalName.GetChars( ); }
	const char *GetDisplayName( void ) const { return DisplayName.Len( ) > 0 ? DisplayName.GetChars( ) : NULL; }
	const char *GetShortName( void ) const { return ShortName.Len( ) > 0 ? ShortName.GetChars( ) : NULL; }
	FBaseCVar *GetCVar( void ) const { return pCVar; }
	ULONG GetFlags( void ) const { return ulFlags; }
	ULONG GetSizing( void ) const { return ulSizing; }
	ULONG GetShortestWidth( void ) const { return ulShortestWidth; }
	ULONG GetWidth( void ) const { return ulWidth; }
	LONG GetRelX( void ) const { return lRelX; }
	LONG GetAlignmentPosition( ULONG ulContentWidth ) const;
	bool IsUsableInCurrentGame( void ) const { return bUsableInCurrentGame; }
	bool IsDisabled( void ) const { return bDisabled; }
	bool ShouldUseShortName( void ) const { return bUseShortName; }
	void Parse( FScanner &sc );
	void DrawHeader( const LONG lYPos, const ULONG ulHeight, const float fAlpha ) const;
	void DrawString( const char *pszString, FFont *pFont, const ULONG ulColor, const LONG lYPos, const ULONG ulHeight, const float fAlpha ) const;
	void DrawColor( const PalEntry color, const LONG lYPos, const ULONG ulHeight, const float fAlpha, const int clipWidth, const int clipHeight ) const;
	void DrawTexture( FTexture *texture, const LONG yPos, const ULONG height, const float alpha, const int clipWidth, const int clipHeight, const float scale ) const;

	virtual COLUMNTEMPLATE_e GetTemplate( void ) const { return COLUMNTEMPLATE_UNKNOWN; }
	virtual void ParseCommand( FScanner &sc, const COLUMNCMD_e Command, const FString CommandName );
	virtual void CheckIfUsable( void );
	virtual void Refresh( void );
	virtual void Update( void );
	virtual void DrawValue( const ULONG ulPlayer, const ULONG ulColor, const LONG lYPos, const ULONG ulHeight, const float fAlpha ) const = 0;

protected:
	virtual void SetScoreboard( Scoreboard *pNewScoreboard ) { pScoreboard = pNewScoreboard; }
	bool CanDrawForPlayer( const ULONG ulPlayer ) const;

	const FName InternalName;
	FString DisplayName;
	FString ShortName;
	HORIZALIGN_e Alignment;
	FBaseCVar *pCVar;
	LONG lMinCVarValue;
	LONG lMaxCVarValue;
	ULONG ulFlags;
	ULONG ulGameAndEarnTypeFlags;
	std::set<GAMEMODE_e> GameModeList;
	std::set<GAMEMODE_e> PriorityGameModeList;
	std::set<GAMEMODE_e> ForbiddenGameModeList;
	ULONG ulSizing;
	ULONG ulShortestWidth;
	ULONG ulShortestHeight;
	ULONG ulWidth;
	LONG lRelX;
	bool bUsableInCurrentGame;
	bool bDisabled;
	bool bUseShortName;

	// [AK] A pointer to a scoreboard, if this column is inside its column order list.
	Scoreboard *pScoreboard;

	// [AK] Let the Scoreboard struct have access to this class's protected members.
	friend struct Scoreboard;

private:
	void FixClipRectSize( const int clipWidth, const int clipHeight, const ULONG ulHeight, int &fixedWidth, int &fixedHeight ) const;
};

//*****************************************************************************
//
// [AK] DataScoreColumn
//
// A column of data, this supports all the native types (e.g. frags, points,
// (wins, etc.) and handles the player's values.
//
//*****************************************************************************

class CompositeScoreColumn;

class DataScoreColumn : public ScoreColumn
{
public:
	DataScoreColumn( COLUMNTYPE_e Type, const char *pszName ) :
		ScoreColumn( pszName ),
		NativeType( Type ),
		ulMaxLength( 0 ),
		lClipRectWidth( 0 ),
		lClipRectHeight( 0 ),
		textureScale( 1.0f ),
		pCompositeColumn( NULL ) { }

	CompositeScoreColumn *GetCompositeColumn( void ) const { return pCompositeColumn; }
	COLUMNTYPE_e GetNativeType( void ) const { return NativeType; }
	DATACONTENT_e GetContentType( void ) const;
	FString GetValueString( const PlayerValue &Value ) const;

	virtual COLUMNTEMPLATE_e GetTemplate( void ) const { return COLUMNTEMPLATE_DATA; }
	virtual DATATYPE_e GetDataType( void ) const;
	virtual ULONG GetValueWidthOrHeight( const PlayerValue &Value, const bool bGetHeight ) const;
	virtual PlayerValue GetValue( const ULONG ulPlayer ) const;
	virtual void ParseCommand( FScanner &sc, const COLUMNCMD_e Command, const FString CommandName );
	virtual void Refresh( void );
	virtual void Update( void );
	virtual void DrawValue( const ULONG ulPlayer, const ULONG ulColor, const LONG lYPos, const ULONG ulHeight, const float fAlpha ) const;

protected:
	const COLUMNTYPE_e NativeType;
	FString PrefixText;
	FString SuffixText;
	FString TrueText;
	FString FalseText;
	ULONG ulMaxLength;
	LONG lClipRectWidth;
	LONG lClipRectHeight;
	float textureScale;

	// [AK] The composite column that this column belongs to, if there is one.
	CompositeScoreColumn *pCompositeColumn;

	// [AK] Let the CompositeScoreColumn class have access to this class's protected members.
	friend class CompositeScoreColumn;
};

//*****************************************************************************
//
// [AK] A separate class to handle the country flag column type.
//
class CountryFlagScoreColumn : public DataScoreColumn
{
public:
	CountryFlagScoreColumn( FScanner &sc, const char *pszName );

	virtual ULONG GetValueWidthOrHeight( const PlayerValue &Value, const bool bGetHeight ) const;
	virtual PlayerValue GetValue( const ULONG ulPlayer ) const;
	virtual void DrawValue( const ULONG ulPlayer, const ULONG ulColor, const LONG lYPos, const ULONG ulHeight, const float fAlpha ) const;

	// [AK] The "CTRYFLAG" texture is supposed to be a 16x16 grid of country flag icons.
	const static int NUM_FLAGS_PER_SIDE = 16;

private:
	FTexture *pFlagIconSet;
	ULONG ulFlagWidth;
	ULONG ulFlagHeight;
};

//*****************************************************************************
//
// [AK] CompositeScoreColumn
//
// A column consisting of more than one data column that are tucked underneath
// its header. The headers of the data sub-columns are never shown.
//
//*****************************************************************************

class CompositeScoreColumn : public ScoreColumn
{
public:
	CompositeScoreColumn( const char *pszName ) : ScoreColumn( pszName ), ulGapBetweenSubColumns( 0 ) { }

	virtual COLUMNTEMPLATE_e GetTemplate( void ) const { return COLUMNTEMPLATE_COMPOSITE; }
	virtual void ParseCommand( FScanner &sc, const COLUMNCMD_e Command, const FString CommandName );
	virtual void CheckIfUsable( void );
	virtual void Refresh( void );
	virtual void Update( void );
	virtual void DrawValue( const ULONG ulPlayer, const ULONG ulColor, const LONG lYPos, const ULONG ulHeight, const float fAlpha ) const;

protected:
	virtual void SetScoreboard( Scoreboard *pNewScoreboard );
	void ClearSubColumns( void );

	TArray<DataScoreColumn *> SubColumns;
	ULONG ulGapBetweenSubColumns;

private:
	ULONG GetRowWidthOrHeight( const ULONG ulPlayer, const bool bGetHeight ) const;
	ULONG GetSubColumnWidth( const ULONG ulSubColumn, const ULONG ulValueWidth ) const;
};

//*****************************************************************************
//
// [AK] ScoreMargin
//
// Draws the main header, footer, and all of the team/spectator headers using
// a variety of commands that are parsed from the SCORINFO lumps.
//
//*****************************************************************************

class ScoreMargin
{
public:
	// [AK] A base class for all margin commands in SCORINFO.
	class BaseCommand
	{
	public:
		BaseCommand( ScoreMargin *pMargin, BaseCommand *pParentCommand );
		virtual ~BaseCommand( void ) { }

		BaseCommand *GetParentCommand( void ) const { return pParentCommand; }

		virtual void Parse( FScanner &sc ) = 0;
		virtual void Refresh( const ULONG ulDisplayPlayer ) = 0;
		virtual void Draw( const ULONG ulDisplayPlayer, const ULONG ulTeam, const LONG lYPos, const float fAlpha, const LONG lXOffsetBonus ) const = 0;

		// [AK] By default, a margin command isn't a block (i.e. multi-line or row) element.
		virtual bool IsBlockElement( void ) const { return false; }

		// [AK] By default, a margin command also isn't a flow control command.
		virtual bool IsFlowControl( void ) const { return false; }

	protected:
		ScoreMargin *const pParentMargin;
		BaseCommand *const pParentCommand;
	};

	// [AK] A block of margin commands in-between braces.
	class CommandBlock
	{
	public:
		~CommandBlock( void ) { Clear( ); }

		void ParseBlock( FScanner &sc, ScoreMargin *margin, BaseCommand *parentCommand, const bool clearCommands );
		void ParseCommand( FScanner &sc, ScoreMargin *pMargin, BaseCommand *pParentCommand, const bool bOnlyFlowControl );
		void Clear( void );
		void Refresh( const ULONG ulDisplayPlayer );
		void Draw( const ULONG ulDisplayPlayer, const ULONG ulTeam, const LONG lYPos, const float fAlpha, const LONG lXOffsetBonus = 0 ) const;

		inline bool HasCommands( void ) const { return ( Commands.Size( ) > 0 ); }

	private:
		TArray<BaseCommand *> Commands;
	};

	ScoreMargin( MARGINTYPE_e MarginType, const char *pszName );

	MARGINTYPE_e GetType( void ) const { return Type; }
	const char *GetName( void ) const { return Name.GetChars( ); }
	ULONG GetWidth( void ) const { return ulWidth; }
	ULONG GetHeight( void ) const { return ulHeight; }
	int GetRelX( void ) const { return relX; }
	void IncreaseHeight( ULONG ulExtraHeight ) { ulHeight += ulExtraHeight; }
	void Parse( FScanner &sc );
	void Refresh( const ULONG displayPlayer, const ULONG newWidth, const int newRelX );
	void Render( const ULONG ulDisplayPlayer, const ULONG ulTeam, LONG &lYPos, const float fAlpha ) const;
	void ClearCommands( void ) { Block.Clear( ); }

	// [AK] Indicates that this margin is drawing for no team.
	const static unsigned int NO_TEAM = UCHAR_MAX;

private:
	CommandBlock Block;
	const MARGINTYPE_e Type;
	const FName Name;
	ULONG ulWidth;
	ULONG ulHeight;
	int relX;
};

//*****************************************************************************
//
// [AK] Scoreboard
//
// Contains all properties and columns on the scoreboard. The scoreboard is
// responsible for updating itself and the positions of all active columns,
// sorting players based on a predefined rank order list, and finally drawing
// everything on the screen when it needs to be rendered.
//
//*****************************************************************************

EXTERN_CVAR( Int, sb_customizeflags )

struct Scoreboard
{
	// [AK] Template class for properties that can be customized in-game.
	template <typename Type, typename CVar> struct CustomizableProperty
	{
		CustomizableProperty( const CVar &cvar, CustomizeScoreboardFlag flag, Type initial ) :
			cvar( cvar ), flag( flag ), value( initial ) { }

		void operator= ( const Type other ) { value = other; }
		operator Type( ) const { return ( sb_customizeflags & flag ) ? static_cast<Type>( cvar ) : value; }

		const CVar &cvar;
		const CustomizeScoreboardFlag flag;
		Type value;
	};

	// [AK] Specialized template class for font properties.
	struct CustomizableFont : public CustomizableProperty<FFont *, FStringCVar>
	{
		CustomizableFont( const FStringCVar &cvar, CustomizeScoreboardFlag flag, FFont *initial ) :
			CustomizableProperty( cvar, flag, initial ) { }

		operator FFont *( ) const
		{
			if ( sb_customizeflags & flag )
			{
				FFont *customFont = V_GetFont( cvar );

				// [AK] If the CVar value is invalid, use the SCORINFO value.
				if ( customFont != nullptr )
					return customFont;
			}

			return value;
		}
	};

	// [AK] Specialized template class for text color properties.
	struct CustomizableTextColor : public CustomizableProperty<EColorRange, FIntCVar>
	{
		CustomizableTextColor( const FIntCVar &cvar, CustomizeScoreboardFlag flag, EColorRange initial ) :
			CustomizableProperty( cvar, flag, initial ) { }

		operator EColorRange( ) const { return ( sb_customizeflags & flag ) ? static_cast<EColorRange>( cvar.GetGenericRep( CVAR_Int ).Int ) : value; }
	};

	enum LOCALROW_COLOR_e
	{
		LOCALROW_COLOR_INGAME,
		LOCALROW_COLOR_INDEMO,

		NUM_LOCALROW_COLORS
	};

	enum BORDER_COLOR_e
	{
		BORDER_COLOR_LIGHT,
		BORDER_COLOR_DARK,

		NUM_BORDER_COLORS
	};

	enum ROWBACKGROUND_COLOR_e
	{
		ROWBACKGROUND_COLOR_LIGHT,
		ROWBACKGROUND_COLOR_DARK,
		ROWBACKGROUND_COLOR_LOCAL,

		NUM_ROWBACKGROUND_COLORS
	};

	LONG lRelX;
	LONG lRelY;
	ULONG ulWidth;
	ULONG ulHeight;
	ULONG ulFlags;
	CustomizableFont headerFont;
	CustomizableFont rowFont;
	CustomizableTextColor headerColor;
	CustomizableTextColor rowColor;
	CustomizableTextColor localRowColors[NUM_LOCALROW_COLORS];
	FTexture *pBorderTexture;
	CustomizableProperty<PalEntry, FColorCVar> borderColors[NUM_BORDER_COLORS];
	CustomizableProperty<PalEntry, FColorCVar> backgroundColor;
	CustomizableProperty<PalEntry, FColorCVar> rowBackgroundColors[NUM_ROWBACKGROUND_COLORS];
	PalEntry TeamRowBackgroundColors[MAX_TEAMS][NUM_ROWBACKGROUND_COLORS];
	CustomizableProperty<float, FFloatCVar> backgroundAmount;
	CustomizableProperty<float, FFloatCVar> rowBackgroundAmount;
	CustomizableProperty<float, FFloatCVar> deadRowBackgroundAmount;
	float fContentAlpha;
	float fDeadTextAlpha;
	ULONG ulBackgroundBorderSize;
	ULONG ulGapBetweenHeaderAndRows;
	ULONG ulGapBetweenColumns;
	ULONG ulGapBetweenRows;
	ULONG ulColumnPadding;
	LONG lHeaderHeight;
	LONG lRowHeight;
	unsigned int headerHeightToUse;
	unsigned int rowHeightToUse;
	unsigned int totalScrollHeight;
	unsigned int visibleScrollHeight;
	int minClipRectY;
	int maxClipRectY;

	Scoreboard( void );

	void Parse( FScanner &sc );
	void Refresh( const unsigned int displayPlayer, const int minYPos );
	void Render( const unsigned int displayPlayer, const int minYPos, const float alpha );
	void DrawBorder( const EColorRange Color, LONG &lYPos, const float fAlpha, const bool bReverse ) const;
	void DrawRowBackground( const PalEntry color, int x, int y, int width, int height, const float fAlpha ) const;
	void DrawRowBackground( const PalEntry color, const int y, const float fAlpha ) const;
	void UpdateTeamRowBackgroundColors( void );
	void RemoveInvalidColumnsInRankOrder( void );
	void ClearColumnsAndMargins( void );
	bool ShouldSeparateTeams( void ) const;
	bool CheckFlag( const SCOREBOARDFLAG_e flag, const CustomizeScoreboardFlag customizeFlag, const bool customizeValue ) const;

private:
	struct PlayerComparator
	{
		PlayerComparator( Scoreboard *pOtherScoreboard ) : pScoreboard( pOtherScoreboard ) { }
		bool operator( )( const int &arg1, const int &arg2 ) const;

		const Scoreboard *pScoreboard;
	};

	ULONG ulPlayerList[MAXPLAYERS];
	TArray<ScoreColumn *> ColumnOrder;
	TArray<DataScoreColumn *> RankOrder;
	ScoreMargin MainHeader;
	ScoreMargin TeamHeader;
	ScoreMargin SpectatorHeader;
	ScoreMargin Footer;
	LONG lLastRefreshTick;
	int currentScrollOffset;
	int interpolateScrollOffset;

	void AddColumnToList( FScanner &sc, const bool bAddToRankOrder );
	void RemoveColumnFromList( FScanner &sc, const bool bRemoveFromRankOrder );
	void UpdateWidth( void );
	void UpdateHeight( const unsigned int displayPlayer, const int minYPos );
	void DrawRow( const ULONG ulPlayer, const ULONG ulDisplayPlayer, LONG &lYPos, const float fAlpha, bool &bUseLightBackground ) const;

	// [AK] This function needs access to Scoreboard::currentScrollOffset.
	friend bool SCOREBOARD_ShouldInterpolateOnIntermission( void );
};

//*****************************************************************************
//	PROTOTYPES

void			SCOREBOARD_Construct( void );
void			SCOREBOARD_Destruct( void );
void			SCOREBOARD_ParseFont( FScanner &sc, FFont *&font );
void			SCOREBOARD_ParseTextColor( FScanner &sc, EColorRange &color );
void			SCOREBOARD_Reset( void );
void			SCOREBOARD_Render( const unsigned int displayPlayer, const int minYPos = 0 );
void STACK_ARGS SCOREBOARD_DrawString( FFont *font, const int color, const int x, const int y, const char *string, ... );
void			SCOREBOARD_DrawColor( const PalEntry color, const float alpha, int left, int top, int width, int height );
void STACK_ARGS SCOREBOARD_DrawTexture( FTexture *texture, const int x, const int y, const float scale, ... );
bool			SCOREBOARD_ShouldDrawBoard( void );
bool			SCOREBOARD_ShouldInterpolateOnIntermission( void );
bool			SCOREBOARD_AdjustVerticalClipRect( int &clipTop, int &clipHeight );
int				SCOREBOARD_CenterAlign( const int biggerSize, const int smallerSize );
void			SCOREBOARD_ConvertVirtualCoordsToReal( int &left, int &top, int &width, int &height );
void			SCOREBOARD_BuildLimitStrings( std::list<FString> &lines );
FString			SCOREBOARD_BuildChampionString( void );
ScoreColumn		*SCOREBOARD_GetColumn( FName Name, const bool bMustBeUsable );
LONG			SCOREBOARD_GetLeftToLimit( void );
void			SCOREBOARD_SetNextLevel( const char *pszMapName );

//*****************************************************************************
//	EXTERNAL CONSOLE VARIABLES

EXTERN_CVAR( Bool, cl_showscoreleft )

#endif // __SCOREBOARD_H__
