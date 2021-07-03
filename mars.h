/*
  Victor Luchits

  The MIT License (MIT)

  Copyright (c) 2021 Victor Luchits

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#ifndef _MARS_H
#define _MARS_H

#ifdef MARS

#include "marshw.h"

void Mars_Slave(void);

void Mars_Slave_R_WallPrep(void) ATTR_DATA_CACHE_ALIGN ATTR_OPTIMIZE_SIZE;
void Mars_Slave_R_SegCommands(void) ATTR_DATA_CACHE_ALIGN ATTR_OPTIMIZE_SIZE;
void Mars_Slave_R_DrawPlanes(void) ATTR_DATA_CACHE_ALIGN ATTR_OPTIMIZE_SIZE;
void Mars_Slave_R_DrawSprites(void) ATTR_DATA_CACHE_ALIGN ATTR_OPTIMIZE_SIZE;
void Mars_Slave_P_CheckSights(void) ATTR_DATA_CACHE_ALIGN ATTR_OPTIMIZE_SIZE;

void Mars_Slave_M_AnimateFire(void) ATTR_OPTIMIZE_EXTREME;
void Mars_Slave_InitSoundDMA(void);
void Mars_Slave_ReadSoundCmds(void) ATTR_DATA_CACHE_ALIGN ATTR_OPTIMIZE_SIZE;

static inline void Mars_R_BeginComputeSeg(void)
{
	while (MARS_SYS_COMM4 != 0) {};

	MARS_SYS_COMM4 = 1;
}

static inline void Mars_R_EndComputeSeg(void)
{
	while (MARS_SYS_COMM4 != 0);
}

static inline void Mars_R_BeginDrawPlanes(void)
{
	while (MARS_SYS_COMM4 != 0) {};
	MARS_SYS_COMM4 = 4;
}

static inline void Mars_R_EndDrawPlanes(void)
{
	while (MARS_SYS_COMM4 != 0);
}

static inline void Mars_R_BeginDrawSprites(void)
{
	while (MARS_SYS_COMM4 != 0) {};
	MARS_SYS_COMM4 = 5;
}

static inline void Mars_R_EndDrawSprites(void)
{
	while (MARS_SYS_COMM4 != 0);
}

static inline void Mars_P_BeginCheckSights(void)
{
	while (MARS_SYS_COMM4 != 0) {};
	MARS_SYS_COMM4 = 9;
}

static inline void Mars_P_EndCheckSights(void)
{
	while (MARS_SYS_COMM4 != 0);
}

static inline void Mars_CommSlaveClearCache(void)
{
	while (MARS_SYS_COMM4 != 0) {};
	MARS_SYS_COMM4 = 3;
	while (MARS_SYS_COMM4 != 0);
}

// r_phase8
static inline void Mars_R_ResetNextSprite(void)
{
	MARS_SYS_COMM6 = 0;
}

static inline void Mars_R_WaitNextSprite(int l)
{
	while (MARS_SYS_COMM6 != l) {}
}

static inline void Mars_R_AdvanceNextSprite(void)
{
	MARS_SYS_COMM6 = MARS_SYS_COMM6 + 1;
}

static inline void Mars_R_BeginWallPrep(void)
{
	while (MARS_SYS_COMM4 != 0);
	MARS_SYS_COMM6 = 1;
	MARS_SYS_COMM4 = 6;
}

static inline void Mars_R_StopBSP(void)
{
	MARS_SYS_COMM6 = 0;
}

static inline void Mars_R_EndWallPrep(void)
{
	while (MARS_SYS_COMM4 != 0);
}

static inline void Mars_InitSoundDMA(void)
{
	while (MARS_SYS_COMM4 != 0) {};
	MARS_SYS_COMM4 = 10;
	while (MARS_SYS_COMM4 != 0) {};
}

#endif 

#endif // _MARS_H
