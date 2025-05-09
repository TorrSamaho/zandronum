/*
** c_console.h
**
**---------------------------------------------------------------------------
** Copyright 1998-2006 Randy Heit
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#ifndef __C_CONSOLE__
#define __C_CONSOLE__

#include <stdarg.h>
#include "basictypes.h"
#include "tarray.h" // [TP]
#include "zstring.h" // [TP]

struct event_t;

#define C_BLINKRATE			(TICRATE/2)

typedef enum cstate_t 
{
	c_up=0, c_down=1, c_falling=2, c_rising=3
} 
constate_e;

extern constate_e ConsoleState;
extern int ConBottom;

// Initialize the console
void C_InitConsole (int width, int height, bool ingame);
void C_DeinitConsole ();
void C_InitConback();

// Adjust the console for a new screen mode
void C_NewModeAdjust (void);

void C_Ticker (void);

void AddToConsole (int printlevel, const char *string);
int PrintString (int printlevel, const char *string);
int VPrintf (int printlevel, const char *format, va_list parms) GCCFORMAT(2);

void C_DrawConsole (bool hw2d);
void C_ToggleConsole (void);
void C_FullConsole (void);
void C_HideConsole (void);
void C_AdjustBottom (void);
void C_FlushDisplay (void);

// [BC] New function prototypes.
void CONSOLE_SetRCONPlayer( ULONG ulPlayer );
ULONG CONSOLE_GetRCONPlayer( void ); // [AK]
void CONSOLE_ShouldPrintToRCONPlayer( bool enable ); // [AK]

void C_InitTicker (const char *label, unsigned int max, bool showpercent=true);
void C_SetTicker (unsigned int at, bool forceUpdate=false);

class FFont;
void C_MidPrint (FFont *font, const char *message);
void C_MidPrintBold (FFont *font, const char *message);
void C_MOTDPrint (FString message); // [AK]

bool C_Responder (event_t *ev);

void C_AddTabCommand (const char *name);
void C_RemoveTabCommand (const char *name);
void C_ClearTabCommands();		// Removes all tab commands

// [TP] For RCON clients
class FString;
TArray<FString> C_GetTabCompletes (const FString& part);

// [TP]
void C_StartCapture();
const char* C_EndCapture();
bool C_IsCapturing();

// [AK]
unsigned int C_GetMessageLevel();
bool C_ShouldForceInterpolation();
void C_UpdateVirtualScreen();

// [AK] Externs to global variables used for text scaling and the virtual screen.
extern bool g_bScale;
extern float g_fXScale, g_fYScale, g_rXScale, g_rYScale;
extern ULONG g_ulTextHeight;

#endif
