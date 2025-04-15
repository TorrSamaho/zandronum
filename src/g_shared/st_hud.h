//-----------------------------------------------------------------------------
//
// Skulltag Source
// Copyright (C) 2008 Benjamin Berkels
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
// Filename: st_hud.h
//
// Description: Contains extensions to the HUD code.
//
//-----------------------------------------------------------------------------

#ifndef __ST_HUD_H__
#define __ST_HUD_H__

#include "d_player.h"
#include "v_font.h"

//*****************************************************************************
//	PROTOTYPES

int		HUD_GetWidth( void );
int		HUD_GetHeight( void );
void	HUD_DrawTexture( FTexture *Img, int X, int Y, const bool Scale );
void	HUD_DrawTexture( FTexture *Img, int X, int Y );
void	HUD_DrawText( FFont* Font, int Normalcolor, int X, int Y, const char *String, const bool Scale );
void	HUD_DrawText( FFont* Font, int Normalcolor, int X, int Y, const char *String );
void	HUD_DrawTextAligned( int Normalcolor, int Y, const char *String, bool AlignLeft, const bool Scale );
void	HUD_DrawTextCentered( FFont* Font, int Normalcolor, int Y, const char *String, const bool Scale );
void	HUD_DrawTextClean( FFont* Font, int Normalcolor, int X, int Y, const char *String );
void	HUD_DrawTextCleanCentered( FFont *Font, int Normalcolor, int Y, const char *String );
bool	HUD_IsUsingNewHud( void );
bool	HUD_IsVisible( void );
bool	HUD_IsFullscreen( void );

void	HUD_Render( ULONG ulDisplayPlayer );
void	HUD_Refresh( unsigned int displayPlayer );
void	HUD_RefreshPlayerCounts( void );
void	HUD_ShouldRefreshBeforeRendering( void );
void	HUD_DrawTargetName( player_t *pPlayer );
void	HUD_DrawCoopInfo( void );
void	HUD_DrawStandardMessage( const char *pszMessage, EColorRange color, const bool bClearScreen = false, float fHoldTime = 3.0f, float fOutTime = 2.0f, const bool bInformClients = false );
void	HUD_DrawCNTRMessage( const char *pszMessage, EColorRange color, float fHoldTime = 3.0f, float fOutTime = 0.25f, const bool bInformClients = false, const ULONG ulPlayerExtra = MAXPLAYERS, const ULONG ulFlags = 0 );
void	HUD_DrawSUBSMessage( const char *pszMessage, EColorRange color, float fHoldTime = 3.0f, float fOutTime = 0.25f, const bool bInformClients = false, const ULONG ulPlayerExtra = MAXPLAYERS, const ULONG ulFlags = 0 );
void	HUD_PrepareToDrawFragMessage( player_t *pPlayer, AActor *pSource, FName MeansOfDeath );
void	HUD_ClearFragAndPlaceMessages( const bool bInformClients );
bool	HUD_ShouldDrawRank( ULONG ulPlayer );
bool	HUD_IsTied( ULONG ulPlayerNum );
bool	HUD_IsTied( void );
ULONG	HUD_GetViewPlayer( void );
ULONG	HUD_GetNumPlayers( void );
ULONG	HUD_GetNumSpectators( void );
ULONG	HUD_GetRank( void );
LONG	HUD_GetSpread( void );
void	HUD_SetRespawnTimeLeft( float fRespawnTime );
FString	HUD_SpellOrdinal( int ranknum, bool bColored = false );
FString HUD_BuildPointString( void );
FString HUD_BuildPlaceString( unsigned int player, unsigned int rank, bool isTied );

#endif	// __ST_HUD_H__
