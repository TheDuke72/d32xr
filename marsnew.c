/*
  Victor Luchits

  The MIT License (MIT)

  Copyright (c) 2021 Victor Luchits, Derek John Evans, id Software and ZeniMax Media

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


#include "32x.h"
#include "doomdef.h"
#include "mars.h"
#include "mars_ringbuf.h"
#include "r_local.h"
#include "wadbase.h"

const int COLOR_WHITE = 0x04;

short	*dc_colormaps;
const byte	*new_palette = NULL;

boolean	debugscreenactive = false;

int		lastticcount = 0;
int		lasttics = 0;

int 	debugmode = 0;

extern int 	cy;
extern int tictics, thinkertics, sighttics, basetics, latetics;

// framebuffer start is after line table AND a single blank line
static volatile pixel_t* framebuffer = &MARS_FRAMEBUFFER + 0x100 + 160;
static volatile pixel_t *framebufferend = &MARS_FRAMEBUFFER + 0x10000;

static int Mars_ConvGamepadButtons(int ctrl)
{
	int newc = 0;

	if (ctrl & SEGA_CTRL_LEFT)
		newc |= BT_LEFT;
	if (ctrl & SEGA_CTRL_RIGHT)
		newc |= BT_RIGHT;
	if (ctrl & SEGA_CTRL_UP)
		newc |= BT_UP;
	if (ctrl & SEGA_CTRL_DOWN)
		newc |= BT_DOWN;

	if (ctrl & SEGA_CTRL_A)
		newc |= BT_A | configuration[controltype][0];
	if (ctrl & SEGA_CTRL_B)
		newc |= BT_B | configuration[controltype][1];
	if (ctrl & SEGA_CTRL_C)
		newc |= BT_C | configuration[controltype][2] | BT_STRAFE;

	if (ctrl & SEGA_CTRL_X)
		newc |= BT_PWEAPN;
	if (ctrl & SEGA_CTRL_Y)
		newc |= BT_NWEAPN;
	if (ctrl & SEGA_CTRL_Z)
		newc |= BT_AUTOMAP;

	if (ctrl & SEGA_CTRL_MODE)
		newc |= BT_DEBUG;

	return newc;
}

// mostly exists to handle the 3-button controller situation
static int Mars_HandleStartHeld(unsigned *ctrl, const unsigned ctrl_start)
{
	int morebuttons = 0;
	boolean start = 0;
	static boolean prev_start = false;
	static int repeat = 0;
	static const int held_tics = 8;

	start = (*ctrl & ctrl_start) != 0;
	if (start ^ prev_start) {
		int prev_repeat = repeat;
		repeat = 0;

		// start button state changed
		if (prev_start) {
			prev_start = false;
			// quick key press and release
			if (prev_repeat < held_tics)
				return BT_OPTION;

			// key held for a while and then released
			return 0;
		}

		prev_start = true;
	}

	if (!start) {
		return 0;
	}

	repeat++;
	if (repeat < held_tics) {
		// suppress action buttons
		*ctrl = *ctrl & ~(SEGA_CTRL_A | SEGA_CTRL_B | SEGA_CTRL_C);
		return 0;
	}

	if (*ctrl & SEGA_CTRL_A) {
		*ctrl = *ctrl & ~SEGA_CTRL_A;
		morebuttons |= BT_PWEAPN;
	}
	else if (*ctrl & SEGA_CTRL_B) {
		*ctrl = *ctrl & ~SEGA_CTRL_B;
		morebuttons |= BT_NWEAPN;
	}
	if (*ctrl & SEGA_CTRL_C) {
		*ctrl = *ctrl & ~SEGA_CTRL_C;
		morebuttons |= BT_AUTOMAP;
	}

	return morebuttons;
}

static int Mars_ConvMouseButtons(int mouse)
{
	int ctrl = 0;
	if (mouse & SEGA_CTRL_LMB)
	{
		ctrl |= BT_ATTACK; // L -> B
		ctrl |= BT_LMBTN;
	}
	if (mouse & SEGA_CTRL_RMB)
	{
		ctrl |= BT_USE; // R -> C
		ctrl |= BT_RMBTN;
	}
	if (mouse & SEGA_CTRL_MMB)
	{
		ctrl |= BT_NWEAPN; // M -> Y
		ctrl |= BT_MMBTN;
	}
	if (mouse & SEGA_CTRL_STARTMB)
	{
		//ctrl |= BT_OPTION;
	}
	return ctrl;
}

void Mars_Slave(void)
{
	while (1)
	{
		int cmd;
		while ((cmd = MARS_SYS_COMM4) == 0);

		switch (cmd) {
		case 1:
			Mars_Slave_R_SegCommands();
			break;
		case 2:
			break;
		case 3:
			Mars_ClearCache();
			break;
		case 4:
			Mars_Slave_R_DrawPlanes();
			break;
		case 5:
			Mars_Slave_R_DrawSprites();
			break;
		case 6:
			Mars_Slave_R_WallPrep();
			break;
		case 7:
			break;
		case 8:
			Mars_Slave_M_AnimateFire();
			break;
		case 9:
			Mars_Slave_P_CheckSights();
			break;
		case 10:
			Mars_Slave_InitSoundDMA();
			break;
		default:
			break;
		}

		MARS_SYS_COMM4 = 0;
	}
}

int Mars_FRTCounter2Msec(int c)
{
	return FixedMul((unsigned)c << FRACBITS, mars_frtc2msec_frac) >> FRACBITS;
}

/* 
================ 
= 
= I_Init  
=
= Called after all other subsystems have been started
================ 
*/ 

void I_Init (void) 
{	
	int	i;
	const byte	*doompalette;
	const byte 	*doomcolormap;

	doompalette = W_POINTLUMPNUM(W_GetNumForName("PLAYPALS"));
	I_SetPalette(doompalette);

	doomcolormap = W_POINTLUMPNUM(W_GetNumForName("COLORMAPS"));
	dc_colormaps = Z_Malloc(64*256, PU_STATIC, 0);

	for(i = 0; i < 32; i++) {
		const byte *sl1 = &doomcolormap[i*512];
		const byte *sl2 = &doomcolormap[i*512+256];
		byte *dl1 = (byte *)&dc_colormaps[i*256];
		byte *dl2 = (byte *)&dc_colormaps[i*256+128];
		D_memcpy(dl1, sl2, 256);
		D_memcpy(dl2, sl1, 256);
	}
}

void I_SetPalette(const byte* palette)
{
	mars_newpalette = palette;
}

void I_DrawSbar (void)
{
}

boolean	I_RefreshCompleted (void)
{
	return Mars_FramebuffersFlipped();
}

boolean	I_RefreshLatched (void)
{
	return true;
}


/* 
==================== 
= 
= I_WadBase  
=
= Return a pointer to the wadfile.  In a cart environment this will
= just be a pointer to rom.  In a simulator, load the file from disk.
==================== 
*/ 
 
byte *I_WadBase (void)
{
	return wadBase; 
}


/* 
==================== 
= 
= I_ZoneBase  
=
= Return the location and size of the heap for dynamic memory allocation
==================== 
*/ 
 
static char zone[0x30000] __attribute__((aligned(16)));
byte *I_ZoneBase (int *size)
{
	*size = sizeof(zone);
	return (byte *)zone;
}

int I_ReadControls(void)
{
	int ctrl;
	unsigned val;

	val = mars_controls;
	mars_controls = 0;

	ctrl = 0;
	ctrl |= Mars_HandleStartHeld(&val, SEGA_CTRL_START);
	ctrl |= Mars_ConvGamepadButtons(val);
	ctrl |= Mars_ConvGamepadButtons(val);
	return ctrl;
}

int I_ReadMouse(int* pmx, int *pmy)
{
	int mval, ctrl;
	static int oldmval = 0;
	unsigned val;

	*pmx = *pmy = 0;
	if (!mousepresent)
		return 0;

	mval = Mars_PollMouse(mars_mouseport);
	switch (mval)
	{
	case -2:
		// timeout - return old buttons and no deltas
		mval = oldmval & 0x00F70000;
		break;
	case -1:
		// no mouse
		mousepresent = false;
		oldmval = 0;
		return 0;
	default:
		oldmval = mval;
		break;
	}

	val = Mars_ParseMousePacket(mval, pmx, pmy);

	ctrl = 0;
	//ctrl |= Mars_HandleStartHeld(&val, SEGA_CTRL_STARTMB);
	ctrl |= Mars_ConvMouseButtons(val);
	return ctrl;
}

int	I_GetTime (void)
{
	return Mars_GetTicCount();
}

int I_GetFRTCounter(void)
{
	return Mars_GetFRTCounter();
}

/*
====================
=
= I_TempBuffer
=
= return a pointer to a 64k or so temp work buffer for level setup uses
= (non-displayed frame buffer)
=
====================
*/
byte	*I_TempBuffer (void)
{
	byte *w = I_WorkBuffer();
	int *p, *p_end = (int*)framebufferend;

	// clear the buffer so the fact that 32x ignores 0-byte writes goes unnoticed
	// the buffer cannot be re-used without clearing it again though
	for (p = (int*)w; p < p_end; p++)
		*p = 0;

	return w;
}

byte 	*I_WorkBuffer (void)
{
	while (!I_RefreshCompleted());
	return (byte *)(framebuffer + 320 / 2 * 223);
}

pixel_t	*I_FrameBuffer (void)
{
	return (pixel_t *)framebuffer;
}

pixel_t* I_OverwriteBuffer(void)
{
	return (pixel_t*)&MARS_OVERWRITE_IMG + 0x100 + 160;
}

pixel_t	*I_ViewportBuffer (void)
{
	volatile pixel_t *viewportbuffer = framebuffer;
	if (screenWidth < 160)
		viewportbuffer += (224-screenHeight)*320/4+(320-screenWidth*2)/4;
	return (pixel_t *)viewportbuffer;
}

void I_ClearFrameBuffer (void)
{
	int* p = (int*)framebuffer;
	int* p_end = (int*)(framebuffer + 320 * 223 / 2);
	while (p < p_end)
		*p++ = 0;
}

void I_DebugScreen(void)
{
	if (debugmode == 3)
		I_ClearFrameBuffer();
}

void I_ClearWorkBuffer(void)
{
	int *p = (int *)I_WorkBuffer();
	int *p_end = (int *)framebufferend;
	while (p < p_end)
		*p++ = 0;
}

/*=========================================================================== */

/*
====================
=
= I_Update
=
= Display the current framebuffer
= If < 1/15th second has passed since the last display, busy wait.
= 15 fps is the maximum frame rate, and any faster displays will
= only look ragged.
=
= When displaying the automap, use full resolution, otherwise use
= wide pixels
====================
*/
extern int t_ref_bsp[4], t_ref_prep[4], t_ref_segs[4], t_ref_planes[4], t_ref_sprites[4], t_ref_total[4];

void I_Update(void)
{
	int i;
	int sec;
	int ticcount;
	char buf[32];
	static int fpscount = 0;
	static int prevsec = 0;
	static int framenum = 0;
	boolean NTSC = (MARS_VDP_DISPMODE & MARS_NTSC_FORMAT) != 0;
	const int ticwait = (demoplayback ? 3 : 2); // demos were recorded at 15-20fps
	const int refreshHZ = (NTSC ? 60 : 50);

	debugscreenactive = debugmode != 0;

	if ((ticbuttons[consoleplayer] & BT_DEBUG) && !(oldticbuttons[consoleplayer] & BT_DEBUG))
	{
		extern int clearscreen;
		debugmode = (debugmode + 1) % 4;
		clearscreen = 2;
	}
	if (debugmode == 1)
	{
		D_snprintf(buf, sizeof(buf), "fps:%2d", fpscount);
		I_Print8(200, 5, buf);
	}
	else if (debugmode > 1)
	{
		int line = 5;
		unsigned t_ref_bsp_avg = 0;
		unsigned t_ref_segs_avg = 0;
		unsigned t_ref_planes_avg = 0;
		unsigned t_ref_sprites_avg = 0;
		unsigned t_ref_total_avg = 0;

		for (i = 0; i < 4; i++)
		{
			t_ref_bsp_avg += t_ref_bsp[i];
			t_ref_segs_avg += t_ref_segs[i];
			t_ref_planes_avg += t_ref_planes[i];
			t_ref_sprites_avg += t_ref_sprites[i];
			t_ref_total_avg += t_ref_total[i];
		}
		t_ref_bsp_avg >>= 2;
		t_ref_segs_avg >>= 2;
		t_ref_planes_avg >>= 2;
		t_ref_sprites_avg >>= 2;
		t_ref_total_avg >>= 2;

		D_snprintf(buf, sizeof(buf), "fps:%2d", fpscount);
		I_Print8(200, line++, buf);
		D_snprintf(buf, sizeof(buf), "tcs:%d", lasttics);
		I_Print8(200, line++, buf);
		D_snprintf(buf, sizeof(buf), "t:%d/%d/%d", 
			Mars_FRTCounter2Msec(tictics),
			Mars_FRTCounter2Msec(sighttics), Mars_FRTCounter2Msec(basetics));
		I_Print8(200, line++, buf);

		line++;

		D_snprintf(buf, sizeof(buf), "b:%2d", Mars_FRTCounter2Msec(t_ref_bsp_avg));
		I_Print8(200, line++, buf);
		D_snprintf(buf, sizeof(buf), "w:%2d %2d", Mars_FRTCounter2Msec(t_ref_segs_avg), lastwallcmd - viswalls);
		I_Print8(200, line++, buf);
		D_snprintf(buf, sizeof(buf), "p:%2d %2d", Mars_FRTCounter2Msec(t_ref_planes_avg), lastvisplane - visplanes - 1);
		I_Print8(200, line++, buf);
		D_snprintf(buf, sizeof(buf), "s:%2d %2d", Mars_FRTCounter2Msec(t_ref_sprites_avg), lastsprite_p - vissprites);
		I_Print8(200, line++, buf);
		D_snprintf(buf, sizeof(buf), "t:%2d", Mars_FRTCounter2Msec(t_ref_total_avg));
		I_Print8(200, line++, buf);
	}

	Mars_FlipFrameBuffers(false);

	/* */
	/* wait until on the third tic after last display */
	/* */
	do
	{
		ticcount = I_GetTime();
	} while (ticcount - lastticcount < ticwait);

	lasttics = ticcount - lastticcount;
	lastticcount = ticcount;

	sec = ticcount / refreshHZ; // FIXME: add proper NTSC vs PAL rate detection
	if (sec != prevsec) {
		static int prevsecframe;
		fpscount = (framenum - prevsecframe) / (sec - prevsec);
		prevsec = sec;
		prevsecframe = framenum;
	}
	framenum++;

	cy = 1;
}

void DoubleBufferSetup (void)
{
	int i;

	while (!I_RefreshCompleted())
		;

	for (i = 0; i < 2; i++) {
		I_ClearFrameBuffer();
		Mars_FlipFrameBuffers(true);
	}
}

void UpdateBuffer (void) {
	Mars_FlipFrameBuffers(true);
}

void ReadEEProm (void)
{
	maxlevel = 24;
}

void WriteEEProm (void)
{
	maxlevel = 24;
}

unsigned I_NetTransfer (unsigned ctrl)
{
	return 0;
}

void I_NetSetup (void)
{
}
