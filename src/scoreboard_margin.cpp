//-----------------------------------------------------------------------------
//
// Zandronum Source
// Copyright (C) 2021-2023 Adam Kaminski
// Copyright (C) 2021-2023 Zandronum Development Team
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
// Filename: scoreboard_margin.cpp
//
// Description: Contains everything that controls the scoreboard's margins
// (i.e. the main header, team/spectator headers, and the footer).
//
//-----------------------------------------------------------------------------

#include <map>
#include <tuple>
#include "scoreboard.h"
#include "st_hud.h"

//*****************************************************************************
//	DEFINITIONS

// What kind of parameter is this?
#define PARAMETER_CONSTANT		0
// Which command can this parameter be used in?
#define COMMAND_CONSTANT		1
// Must this parameter be initialized?
#define MUST_BE_INITIALIZED		2

//*****************************************************************************
//
// [AK] All parameters used by DrawBaseCommand and its derivatives.
//
enum PARAMETER_e
{
	// The value used when drawing the contents.
	PARAMETER_VALUE,
	// How much the contents are offset horizontally.
	PARAMETER_XOFFSET,
	// How much the contents are offset vertically.
	PARAMETER_YOFFSET,
	// How the contents are aligned horizontally (left, center, or right).
	PARAMETER_HORIZALIGN,
	// How the contents are aligned vertically (top, center, or bottom).
	PARAMETER_VERTALIGN,
	// The transparency of the contents.
	PARAMETER_ALPHA,
	// What font to use when drawing a string.
	PARAMETER_FONT,
	// What text color to use when drawing a string.
	PARAMETER_TEXTCOLOR,
	// How large are the gaps between each separate line.
	PARAMETER_GAPSIZE,
	// The width of a color box.
	PARAMETER_WIDTH,
	// The height of a color box.
	PARAMETER_HEIGHT,

	NUM_PARAMETERS
};

//*****************************************************************************
//
// [AK] The command (DrawString, DrawColor, or DrawTexture) a parameter is intended for.
//
enum COMMAND_e
{
	COMMAND_ALL,
	COMMAND_STRING,
	COMMAND_COLOR,
	COMMAND_TEXTURE,
};

//*****************************************************************************
//	VARIABLES

// [AK] A map of all of the parameters used by DrawBaseCommand and its derivatives.
static const std::map<FName, std::tuple<PARAMETER_e, COMMAND_e, bool>> g_NamedParameters =
{
	{ "value",				{ PARAMETER_VALUE,			COMMAND_ALL,		true  }},
	{ "x",					{ PARAMETER_XOFFSET,		COMMAND_ALL,		false }},
	{ "y",					{ PARAMETER_YOFFSET,		COMMAND_ALL,		false }},
	{ "horizontalalign",	{ PARAMETER_HORIZALIGN,		COMMAND_ALL,		false }},
	{ "verticalalign",		{ PARAMETER_VERTALIGN,		COMMAND_ALL,		false }},
	{ "alpha",				{ PARAMETER_ALPHA,			COMMAND_ALL,		false }},
	{ "font",				{ PARAMETER_FONT,			COMMAND_STRING,		false }},
	{ "textcolor",			{ PARAMETER_TEXTCOLOR,		COMMAND_STRING,		false }},
	{ "gapsize",			{ PARAMETER_GAPSIZE,		COMMAND_STRING,		false }},
	{ "width",				{ PARAMETER_WIDTH,			COMMAND_COLOR,		true  }},
	{ "height",				{ PARAMETER_HEIGHT,			COMMAND_COLOR,		true  }},
};

//*****************************************************************************
//	CLASSES

//*****************************************************************************
//*****************************************************************************
//
// [AK] DrawBaseCommand
//
// An abstract class that is shared by all margin commands that are responsible
// for drawing something.
//
//*****************************************************************************
//*****************************************************************************

class DrawBaseCommand : public ScoreMargin::BaseCommand
{
public:
	DrawBaseCommand( ScoreMargin *pMargin, COMMAND_e Type ) : BaseCommand( pMargin ),
		Command( Type ),
		HorizontalAlignment( HORIZALIGN_LEFT ),
		VerticalAlignment( VERTALIGN_TOP ),
		lXOffset( 0 ),
		lYOffset( 0 ),
		fTranslucency( 1.0f ) { }

	//*************************************************************************
	//
	// [AK] Scans for any parameters until it reaches the end of the command.
	//
	//*************************************************************************

	virtual void Parse( FScanner &sc )
	{
		bool bParameterInitialized[NUM_PARAMETERS] = { false };

		do
		{
			sc.MustGetToken( TK_Identifier );
			auto parameter = g_NamedParameters.find( sc.String );

			// [AK] Make sure that the user entered a valid parameter.
			if ( parameter == g_NamedParameters.end( ))
				sc.ScriptError( "Unknown parameter '%s'.", sc.String );

			const PARAMETER_e ParameterConstant = std::get<PARAMETER_CONSTANT>( parameter->second );
			const COMMAND_e CommandConstant = std::get<COMMAND_CONSTANT>( parameter->second );

			// [AK] Make sure that the parameter can be used by this command.
			if (( CommandConstant != COMMAND_ALL ) && ( CommandConstant != Command ))
				sc.ScriptError( "Parameter '%s' cannot be used inside this command.", sc.String );

			// [AK] Don't allow the same parameter to be initialized more than once.
			if ( bParameterInitialized[ParameterConstant] )
				sc.ScriptError( "Parameter '%s' is already initialized.", sc.String );

			sc.MustGetToken( '=' );
			ParseParameter( sc, parameter->first, ParameterConstant );

			// [AK] This parameter has been initialized now, so mark it.
			bParameterInitialized[ParameterConstant] = true;

		} while ( sc.CheckToken( ',' ));

		sc.MustGetToken( ')' );

		// [AK] Throw an error if there are parameters that were supposed to be initialized, but aren't.
		for ( auto it = g_NamedParameters.begin( ); it != g_NamedParameters.end( ); it++ )
		{
			const COMMAND_e CommandConstant = std::get<COMMAND_CONSTANT>( it->second );

			// [AK] Skip parameters that aren't associated with this command.
			if (( CommandConstant != COMMAND_ALL ) && ( CommandConstant != Command ))
				continue;

			if (( std::get<MUST_BE_INITIALIZED>( it->second )) && ( bParameterInitialized[std::get<PARAMETER_CONSTANT>( it->second )] == false ))
				sc.ScriptError( "Parameter '%s' isn't initialized.", it->first.GetChars( ));
		}
	}

protected:
	template <typename EnumType>
	using SpecialValue = std::pair<EnumType, MARGINTYPE_e>;

	template <typename EnumType>
	using SpecialValueList = std::map<FName, SpecialValue<EnumType>, std::less<FName>, std::allocator<std::pair<FName, SpecialValue<EnumType>>>>;

	//*************************************************************************
	//
	// [AK] Parses any parameters that every draw command can have. Derived
	// classes can handle their own parameters by overriding this function.
	//
	//*************************************************************************

	virtual void ParseParameter( FScanner &sc, const FName ParameterName, const PARAMETER_e Parameter )
	{
		switch ( Parameter )
		{
			case PARAMETER_XOFFSET:
			case PARAMETER_YOFFSET:
			{
				sc.MustGetToken( TK_IntConst );

				if ( Parameter == PARAMETER_XOFFSET )
					lXOffset = sc.Number;
				else
					lYOffset = sc.Number;

				break;
			}

			case PARAMETER_HORIZALIGN:
			case PARAMETER_VERTALIGN:
			{
				sc.MustGetToken( TK_Identifier );

				if ( Parameter == PARAMETER_HORIZALIGN )
					HorizontalAlignment = static_cast<HORIZALIGN_e>( sc.MustGetEnumName( "alignment", "HORIZALIGN_", GetValueHORIZALIGN_e, true ));
				else
					VerticalAlignment = static_cast<VERTALIGN_e>( sc.MustGetEnumName( "alignment", "VERTALIGN_", GetValueVERTALIGN_e, true ));

				break;
			}

			case PARAMETER_ALPHA:
			{
				sc.MustGetToken( TK_FloatConst );
				fTranslucency = clamp( static_cast<float>( sc.Float ), 0.0f, 1.0f );

				break;
			}

			default:
				sc.ScriptError( "Couldn't process parameter '%s'.", ParameterName.GetChars( ));
		}

		// [AK] Don't offset to the left when aligned to the left, or to the right when aligned to the right.
		if ((( HorizontalAlignment == HORIZALIGN_LEFT ) || ( HorizontalAlignment == HORIZALIGN_RIGHT )) && ( lXOffset < 0 ))
			sc.ScriptError( "Can't have a negative x-offset when aligned to the left or right." );

		// [AK] Don't offset upward when aligned to the top, or downward when aligned to the bottom.
		if ((( VerticalAlignment == VERTALIGN_TOP ) || ( VerticalAlignment == VERTALIGN_BOTTOM )) && ( lYOffset < 0 ))
			sc.ScriptError( "Can't have a negative y-offset when aligned to the top or bottom." );
	}

	//*************************************************************************
	//
	// [AK] Checks for identifiers that correspond to "special" values. These
	// values can only be used in the margins they're intended for, which is
	// also checked. If no identifier was passed, then the value is assumed to
	// be "static", which is parsed in the form of a string.
	//
	//*************************************************************************

	template <typename EnumType>
	EnumType GetSpecialValue( FScanner &sc, const SpecialValueList<EnumType> &ValueList )
	{
		if ( sc.CheckToken( TK_Identifier ))
		{
			auto value = ValueList.find( sc.String );

			if ( value != ValueList.end( ))
			{
				const MARGINTYPE_e MarginType = value->second.second;

				// [AK] Throw an error if this value can't be used in the margin that the commands belongs to.
				if ( MarginType != pParentMargin->GetType( ))
					sc.ScriptError( "Special value '%s' can't be used inside a '%s' margin.", sc.String, pParentMargin->GetName( ));

				// [AK] Return the constant that corresponds to this special value.
				return value->second.first;
			}
			else
			{
				sc.ScriptError( "Unknown special value '%s'.", sc.String );
			}
		}

		sc.MustGetToken( TK_StringConst );

		// [AK] Throw a fatal error if an empty string was passed.
		if ( sc.StringLen == 0 )
			sc.ScriptError( "Got an empty string for a value." );

		// [AK] Return the constant that indicates the value as "static".
		return static_cast<EnumType>( -1 );
	}

	//*************************************************************************
	//
	// [AK] Determines the position to draw the contents on the screen.
	//
	//*************************************************************************

	TVector2<LONG> GetDrawingPosition( const ULONG ulWidth, const ULONG ulHeight ) const
	{
		const ULONG ulHUDWidth = HUD_GetWidth( );
		TVector2<LONG> result;

		// [AK] Get the x-position based on the horizontal alignment.
		if ( HorizontalAlignment == HORIZALIGN_LEFT )
			result.X = ( ulHUDWidth - pParentMargin->GetWidth( )) / 2 + lXOffset;
		else if ( HorizontalAlignment == HORIZALIGN_CENTER )
			result.X = ( ulHUDWidth - ulWidth ) / 2 + lXOffset;
		else
			result.X = ( ulHUDWidth + pParentMargin->GetWidth( )) / 2 - ulWidth - lXOffset;

		// [AK] Next, get the y-position based on the vertical alignment.
		if ( VerticalAlignment == VERTALIGN_TOP )
			result.Y = lYOffset;
		else if ( VerticalAlignment == VERTALIGN_CENTER )
			result.Y = ( pParentMargin->GetHeight( ) - ulHeight ) / 2 + lYOffset;
		else
			result.Y = pParentMargin->GetHeight( ) - ulHeight - lYOffset;

		return result;
	}

	//*************************************************************************
	//
	// [AK] Increases the margin's height to fit the contents, if necessary.
	//
	//*************************************************************************

	void EnsureContentFitsInMargin( const ULONG ulHeight )
	{
		if ( ulHeight > 0 )
		{
			LONG lAbsoluteOffset = abs( lYOffset );

			// [AK] Double the y-offset if the content is aligned to the center.
			if ( VerticalAlignment == VERTALIGN_CENTER )
				lAbsoluteOffset *= 2;

			const LONG lHeightDiff = lAbsoluteOffset + ulHeight - pParentMargin->GetHeight( );

			if ( lHeightDiff > 0 )
				pParentMargin->IncreaseHeight( lHeightDiff );
		}
	}

	const COMMAND_e Command;
	HORIZALIGN_e HorizontalAlignment;
	VERTALIGN_e VerticalAlignment;
	LONG lXOffset;
	LONG lYOffset;
	float fTranslucency;
};

//*****************************************************************************
//	FUNCTIONS

//*****************************************************************************
//
// [AK] ScoreMargin::BaseCommand::BaseCommand
//
// Initializes a margin command.
//
//*****************************************************************************

ScoreMargin::BaseCommand::BaseCommand( ScoreMargin *pMargin ) : pParentMargin( pMargin )
{
	// [AK] This should never happen, but throw a fatal error if it does.
	if ( pParentMargin == NULL )
		I_Error( "ScoreMargin::BaseCommand: parent margin is NULL." );
}

//*****************************************************************************
//
// [AK] ScoreMargin::ScoreMargin
//
// Initializes a margin's members to their default values.
//
//*****************************************************************************

ScoreMargin::ScoreMargin( MARGINTYPE_e MarginType, const char *pszName ) :
	Type( MarginType ),
	Name( pszName ),
	ulWidth( 0 ),
	ulHeight( 0 ) { }

//*****************************************************************************
//
// [AK] ScoreMargin::Refresh
//
// Updates the margin's width and height, then refreshes its command list.
//
//*****************************************************************************

void ScoreMargin::Refresh( const ULONG ulDisplayPlayer, const ULONG ulNewWidth )
{
	// [AK] If there's no commands, then don't do anything.
	if ( Commands.Size( ) == 0 )
		return;

	// [AK] Never accept a width of zero, throw a fatal error if this happens.
	if ( ulNewWidth == 0 )
		I_Error( "ScoreMargin::Refresh: tried assigning a width of zero to '%s'.", GetName( ));

	ulWidth = ulNewWidth;
	ulHeight = 0;

	for ( unsigned int i = 0; i < Commands.Size( ); i++ )
		Commands[i]->Refresh( ulDisplayPlayer );
}

//*****************************************************************************
//
// [AK] ScoreMargin::Render
//
// Draws all commands that are defined inside the margin.
//
//*****************************************************************************

void ScoreMargin::Render( const ULONG ulDisplayPlayer, const ULONG ulTeam, LONG &lYPos, const float fAlpha ) const
{
	// [AK] If this is supposed to be a team header, then we can't draw for invalid teams!
	if ( Type == MARGINTYPE_TEAM )
	{
		if (( ulTeam == NO_TEAM ) || ( ulTeam >= teams.Size( )))
			I_Error( "ScoreMargin::Render: '%s' can't be drawn for invalid teams.", GetName( ));
	}
	// [AK] Otherwise, if this is a non-team header, then we can't draw for any specific team!
	else if ( ulTeam != NO_TEAM )
	{
		I_Error( "ScoreMargin::Render: '%s' must not be drawn for any specific team.", GetName( ));
	}

	// [AK] If there's no commands, or the width or height are zero, then we can't draw anything.
	if (( Commands.Size( ) == 0 ) || ( ulWidth == 0 ) || ( ulHeight == 0 ))
		return;

	for ( unsigned int i = 0; i < Commands.Size( ); i++ )
		Commands[i]->Draw( ulDisplayPlayer, ulTeam, lYPos, fAlpha );

	lYPos += ulHeight;
}

//*****************************************************************************
//
// [AK] ScoreMargin::ClearCommands
//
// Deletes all of the margin's commands from memory.
//
//*****************************************************************************

void ScoreMargin::ClearCommands( void )
{
	for ( unsigned int i = 0; i < Commands.Size( ); i++ )
	{
		delete Commands[i];
		Commands[i] = NULL;
	}

	Commands.Clear( );
}
