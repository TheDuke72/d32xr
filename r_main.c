/* r_main.c */

#include "doomdef.h"
#include "r_local.h"
#ifdef MARS
#include "mars.h"
#endif

unsigned short screenWidth, screenHeight;
unsigned short centerX, centerY;
fixed_t centerXFrac, centerYFrac;
fixed_t stretch;
fixed_t stretchX;
fixed_t weaponScale;

/*===================================== */

/* */
/* subsectors */
/* */
subsector_t		**vissubsectors, **lastvissubsector;

/* */
/* walls */
/* */
viswall_t	*viswalls/*[MAXWALLCMDS]*/, *lastwallcmd;

/* */
/* planes */
/* */
visplane_t	*visplanes/*[MAXVISPLANES]*/, *lastvisplane;

#define NUM_VISPLANES_BUCKETS 128
static visplane_t** visplanes_hash;

/* */
/* sprites */
/* */
vissprite_t	*vissprites, *lastsprite_p, *vissprite_p;

/* */
/* openings / misc refresh memory */
/* */
unsigned short	*openings/*[MAXOPENINGS]*/, *lastopening;

/*===================================== */

#ifndef MARS
boolean		phase1completed;

pixel_t		*workingscreen;
#endif

#ifdef MARS
volatile pixel_t* viewportbuffer;

__attribute__((aligned(16)))
#endif
viewdef_t       vd;
player_t	*viewplayer;

int			framecount;		/* incremented every frame */

int			extralight;			/* bumped light from gun blasts */

/* */
/* sky mapping */
/* */
int			skytexture;


/* */
/* precalculated math */
/* */
angle_t		clipangle,doubleclipangle;

fixed_t *yslope/*[SCREENHEIGHT]*/ = NULL;
fixed_t* distscale/*[SCREENWIDTH]*/ = NULL;

unsigned char* viewangletox/*[FINEANGLES/2]*/ = NULL;

angle_t* xtoviewangle/*[SCREENWIDTH+1]*/ = NULL;

/* */
/* performance counters */
/* */
int t_ref_cnt = 0;
int t_ref_bsp[4], t_ref_prep[4], t_ref_segs[4], t_ref_planes[4], t_ref_sprites[4], t_ref_total[4];

r_texcache_t r_flatscache, r_wallscache;

void R_Setup(void) ATTR_DATA_CACHE_ALIGN ATTR_OPTIMIZE_SIZE;
void R_Cache(void) ATTR_DATA_CACHE_ALIGN;

#ifdef MARS
static void R_RenderPhase1(void) ATTR_DATA_CACHE_ALIGN ATTR_OPTIMIZE_SIZE __attribute__((noinline));
static void R_RenderPhases2To9(void) ATTR_DATA_CACHE_ALIGN ATTR_OPTIMIZE_SIZE __attribute__((noinline));
#endif

/*
===============================================================================
=
= R_PointToAngle
=
===============================================================================
*/


int SlopeDiv (unsigned num, unsigned den)
{
	unsigned ans;
	if (den < 512)
		return SLOPERANGE;
	ans = (num<<3)/(den>>8);
	return ans <= SLOPERANGE ? ans : SLOPERANGE;
}

angle_t R_PointToAngle2 (fixed_t x1, fixed_t y1, fixed_t x2, fixed_t y2)
{	
	int		x;
	int		y;
	
	x = x2 - x1;
	y = y2 - y1;
	
	if ( (!x) && (!y) )
		return 0;
	if (x>= 0)
	{	/* x >=0 */
		if (y>= 0)
		{	/* y>= 0 */
			if (x>y)
				return tantoangle[ SlopeDiv(y,x)];     /* octant 0 */
			else
				return ANG90-1-tantoangle[ SlopeDiv(x,y)];  /* octant 1 */
		}
		else
		{	/* y<0 */
			y = -y;
			if (x>y)
				return -tantoangle[SlopeDiv(y,x)];  /* octant 8 */
			else
				return ANG270+tantoangle[ SlopeDiv(x,y)];  /* octant 7 */
		}
	}
	else
	{	/* x<0 */
		x = -x;
		if (y>= 0)
		{	/* y>= 0 */
			if (x>y)
				return ANG180-1-tantoangle[ SlopeDiv(y,x)]; /* octant 3 */
			else
				return ANG90+ tantoangle[ SlopeDiv(x,y)];  /* octant 2 */
		}
		else
		{	/* y<0 */
			y = -y;
			if (x>y)
				return ANG180+tantoangle[ SlopeDiv(y,x)];  /* octant 4 */
			else
				return ANG270-1-tantoangle[ SlopeDiv(x,y)];  /* octant 5 */
		}
	}	
#ifndef LCC	
	return 0;
#endif
}


/*
===============================================================================
=
= R_PointOnSide
=
= Returns side 0 (front) or 1 (back)
===============================================================================
*/

int	R_PointOnSide (int x, int y, node_t *node)
{
	fixed_t	dx,dy;
	fixed_t	left, right;

	if (!node->dx)
	{
		if (x <= node->x)
			return node->dy > 0;
		return node->dy < 0;
	}
	if (!node->dy)
	{
		if (y <= node->y)
			return node->dx < 0;
		return node->dx > 0;
	}
	
	dx = (x - node->x);
	dy = (y - node->y);
	
	left = (node->dy>>FRACBITS) * (dx>>FRACBITS);
	right = (dy>>FRACBITS) * (node->dx>>FRACBITS);
	
	if (right < left)
		return 0;		/* front side */
	return 1;			/* back side */
}



/*
==============
=
= R_PointInSubsector
=
==============
*/

struct subsector_s *R_PointInSubsector (fixed_t x, fixed_t y)
{
	node_t	*node;
	int		side, nodenum;
	
	if (!numnodes)				/* single subsector is a special case */
		return subsectors;
		
	nodenum = numnodes-1;

	while (! (nodenum & NF_SUBSECTOR) )
	{
		node = &nodes[nodenum];
		side = R_PointOnSide (x, y, node);
		nodenum = node->children[side];
	}
	
	return &subsectors[nodenum & ~NF_SUBSECTOR];
	
}

/*============================================================================= */

const int screenSizes[][2] = {
	{128, 144},
	{128, 160},
	{160, 180},
};

/*
================
=
= R_SetScreenSize
=
================
*/
void R_SetScreenSize(int size)
{
	int width, height;
	const int numSizes = sizeof(screenSizes) / sizeof(screenSizes[0]);

	while (!I_RefreshCompleted())
		;

	size %= numSizes;

	width = screenSizes[size][0];
	height = screenSizes[size][1];

	screenWidth = width;
	screenHeight = height;
	weaponScale = 24 * FRACUNIT;

#ifdef MARS
	if (0)
	if ((MARS_VDP_DISPMODE & MARS_NTSC_FORMAT) == 0)
	{
		/* correct aspect ratio on PAL */
		screenHeight = (height * 576) / 480;
		weaponScale = 32 * FRACUNIT;
	}
#endif

	centerX = screenWidth / 2;
	centerY = screenHeight / 2;

	centerXFrac = centerX * FRACUNIT;
	centerYFrac = centerY * FRACUNIT;

	/* proper screen size would be 160*100, stretched to 224 is 2.2 scale */
	stretch = (fixed_t)((160.0f / width) * ((float)height / 180.0f) * 2.2f * FRACUNIT);
	stretchX = stretch * centerX;

	R_InitMathTables();

	clipangle = xtoviewangle[0];
	doubleclipangle = clipangle * 2;
}

/*
==============
=
= R_Init
=
==============
*/

void R_Init (void)
{
D_printf ("R_InitData\n");
	R_InitData ();
D_printf ("Done\n");

	R_SetScreenSize(0);

	framecount = 0;
	viewplayer = &players[0];

	R_InitTexCache(&r_flatscache, numflats);

	R_InitTexCache(&r_wallscache, numtextures);
}

/*
==============
=
= R_SetupTextureCaches
=
==============
*/
void R_SetupTextureCaches(void)
{
	int i;
	int numcflats, numcwalls;
	int zonefree, cachezonesize;
	void *margin;

	const int zonemargin = 4*1024;
	const int flatblocksize = sizeof(memblock_t) + sizeof(texcacheblock_t) + 64*64 + 32;
	const int texblocksize = sizeof(memblock_t) + sizeof(texcacheblock_t) + 64*128 + 32;

	// reset pointers from previous level
	for (i = 0; i < numtextures; i++)
		textures[i].data = NULL;
	for (i = 0; i < numflats; i++)
		flatpixels[i] = NULL;

	// functioning texture cache requires at least 8kb of ram
	zonefree = Z_LargestFreeBlock(mainzone);
	if (zonefree < zonemargin+flatblocksize)
		goto nocache;

	// see how many flats we can store
	cachezonesize = zonefree - zonemargin - 128; // give the main zone some slack

	numcflats = cachezonesize / flatblocksize;
	if (numcflats > 4)
		numcflats = 4;

	numcwalls = (cachezonesize - numcflats*flatblocksize) / texblocksize;
	if (numcwalls <= 0 && numcflats > 2)
	{
		numcflats /= 2;
		numcwalls = (cachezonesize - numcflats * flatblocksize) / texblocksize;
	}
	if (numcwalls < 0)
		numcwalls = 0;

	if (numcflats + numcwalls == 0)
	{
nocache:
		R_InitTexCacheZone(&r_flatscache, 0);
		R_InitTexCacheZone(&r_wallscache, 0);
		return;
	}
	
	margin = Z_Malloc(zonemargin, PU_LEVEL, 0);

	R_InitTexCacheZone(&r_flatscache, numcflats * flatblocksize);

	R_InitTexCacheZone(&r_wallscache, numcwalls * texblocksize);

	Z_Free(margin);
}

/*============================================================================= */

#ifndef MARS
int shadepixel;
extern	int	workpage;
extern	pixel_t	*screens[2];	/* [SCREENWIDTH*SCREENHEIGHT];  */
#endif

/*
==================
=
= R_Setup
=
==================
*/

void R_Setup (void)
{
	int 		i;
	int		damagecount, bonuscount;
	player_t *player;
#ifdef JAGUAR
	int		shadex, shadey, shadei;
#endif
	unsigned short  *tempbuf;
#ifdef MARS
	int		palette = 0;
	static int	curpalette = 0;
#endif

/* */
/* set up globals for new frame */
/* */
#ifndef MARS
	workingscreen = screens[workpage];

	*(pixel_t  **)0xf02224 = workingscreen;	/* a2 base pointer */
	*(int *)0xf02234 = 0x10000;				/* a2 outer loop add (+1 y) */
	*(int *)0xf0226c = *(int *)0xf02268 = 0;		/* pattern compare */
#endif

	framecount++;	
	validcount[0]++;
	validcount[1]++;
		
	viewplayer = player = &players[displayplayer];
	vd.viewx = player->mo->x;
	vd.viewy = player->mo->y;
	vd.viewz = player->viewz;
	vd.viewangle = player->mo->angle;

	vd.viewsin = finesine(vd.viewangle>>ANGLETOFINESHIFT);
	vd.viewcos = finecosine(vd.viewangle>>ANGLETOFINESHIFT);
		
	player = &players[consoleplayer];

	damagecount = player->damagecount;
	bonuscount = player->bonuscount;
	
#ifdef JAGUAR
	extralight = player->extralight << 6;

/* */
/* calc shadepixel */
/* */
	if (damagecount)
		damagecount += 10;
	if (bonuscount)
		bonuscount += 2;
	damagecount >>= 2;
	shadex = (bonuscount>>1) + damagecount;
	shadey = (bonuscount>>1) - damagecount;
	shadei = (bonuscount + damagecount)<<2;

	shadei += player->extralight<<3;

/* */
/* pwerups */
/* */
	if (player->powers[pw_invulnerability] > 60
	|| (player->powers[pw_invulnerability]&4) )
	{
		shadex -= 8;
		shadei += 32;
	}

	if (player->powers[pw_ironfeet] > 60
	|| (player->powers[pw_ironfeet]&4) )
		shadey += 7;

	if (player->powers[pw_strength] 
	&& (player->powers[pw_strength]< 64) )
		shadex += (8 - (player->powers[pw_strength]>>3) );


/* */
/* bound and store shades */
/* */
	if (shadex > 7)
		shadex = 7;
	else if (shadex < -8)
		shadex = -8;
	if (shadey > 7)
		shadey = 7;
	else if (shadey < -8)
		shadey = -8;
	if (shadei > 127)
		shadei = 127;
	else if (shadei < -128)
		shadei = -128;
		
	shadepixel = ((shadex<<12)&0xf000) + ((shadey<<8)&0xf00) + (shadei&0xff);
#endif

#ifdef MARS
	//extralight = player->extralight << 3;
	viewportbuffer = (volatile pixel_t*)I_ViewportBuffer();

	palette = 0;

	i = 0;
	if (player->powers[pw_strength] > 0)
		i = 12 - player->powers[pw_strength] / 64;
	if (i < damagecount)
		i = damagecount;

	if (gamepaused)
		palette = 14;
	else if (i)
	{
		palette = (i + 7) / 8;
		if (palette > 7)
			palette = 7;
		palette += 1;
	}
	else if (bonuscount)
	{
		palette = (bonuscount + 7) / 8;
		if (palette > 3)
			palette = 3;
		palette += 9;
	}
	else if (player->powers[pw_ironfeet] > 60
	|| (player->powers[pw_ironfeet]&4) )
		palette = 13;


	if (palette != curpalette) {
		curpalette = palette;
		I_SetPalette(W_POINTLUMPNUM(W_GetNumForName("PLAYPALS"))+palette*768);
	}
#endif

	tempbuf = (unsigned short *)I_WorkBuffer();

	tempbuf = (unsigned short*)(((intptr_t)tempbuf + 3) & ~3);
	visplanes = (void*)tempbuf;
	tempbuf += sizeof(*visplanes) * MAXVISPLANES;

/* */
/* plane filling */
/*	 */
	tempbuf = (unsigned short *)(((intptr_t)tempbuf+1)&~1);
	tempbuf++; // padding
	for (i = 0; i < MAXVISPLANES; i++) {
#ifdef MARS
		visplanes[i].open = *((unsigned short **)((intptr_t)&tempbuf | 0x20000000));
#else
		visplanes[i].open = tempbuf;
#endif
		visplanes[i].runopen = true;
		tempbuf += screenWidth+2;
	}

	tempbuf = (unsigned short*)(((intptr_t)tempbuf + 3) & ~3);
	visplanes_hash = (visplane_t**)tempbuf;
	tempbuf += sizeof(visplane_t *) * NUM_VISPLANES_BUCKETS;

	for (i = 0; i < NUM_VISPLANES_BUCKETS; i++)
		visplanes_hash[i] = NULL;

	tempbuf = (unsigned short*)(((intptr_t)tempbuf + 15) & ~15);
	viswalls = (viswall_t*)tempbuf;
	tempbuf += sizeof(*viswalls)*MAXWALLCMDS/sizeof(*tempbuf);

	tempbuf = (unsigned short*)(((intptr_t)tempbuf + 3) & ~3);
	vissubsectors = (void*)tempbuf;
	tempbuf += sizeof(*vissubsectors) * MAXVISSSEC;

/*	 */
/* clear sprites */
/* */
	tempbuf = (unsigned short *)(((intptr_t)tempbuf+3)&~3);
	vissprites = (void *)tempbuf;
	tempbuf += sizeof(*vissprites)*MAXVISSPRITES/sizeof(*tempbuf);
	vissprite_p = vissprites;

	visplanes[0].runopen = false;
	lastvisplane = visplanes + 1;		/* visplanes[0] is left empty */
	lastwallcmd = viswalls;			/* no walls added yet */
	lastvissubsector = vissubsectors;	/* no subsectors visible yet */

#ifndef MARS
	phasetime[0] = samplecount;
#endif

	R_SetupTexCacheFrame(&r_flatscache);
	R_SetupTexCacheFrame(&r_wallscache);
}

//
// Check for a matching visplane in the visplanes array, or set up a new one
// if no compatible match can be found.
//
int R_PlaneHash(fixed_t height, unsigned flatnum, unsigned lightlevel) {
	return ((((unsigned)height >> 8) + lightlevel) ^ flatnum) & (NUM_VISPLANES_BUCKETS - 1);
}

void R_MarkOpenPlane(visplane_t* pl)
{
	int i;
	unsigned short* open = pl->open;
	for (i = 0; i < screenWidth / 4; i++)
	{
		*open++ = OPENMARK;
		*open++ = OPENMARK;
		*open++ = OPENMARK;
		*open++ = OPENMARK;
	}
}

visplane_t* R_FindPlane(visplane_t* ignore, int hash, fixed_t height, 
	unsigned flatnum, unsigned lightlevel, int start, int stop)
{
	visplane_t *check, *tail;

	tail = visplanes_hash[hash];
	for (check = tail; check; check = check->next)
	{
		if (check == ignore)
			continue;

		if (height == check->height && // same plane as before?
			flatnum == check->flatnum &&
			lightlevel == check->lightlevel)
		{
			if (check->open[start] == OPENMARK)
			{
				// found a plane, so adjust bounds and return it
				if (start < check->minx) // in range of the plane?
					check->minx = start; // mark the new edge
				if (stop > check->maxx)
					check->maxx = stop;  // mark the new edge
				return check; // use the same one as before
			}
		}
	}

	if (lastvisplane == visplanes + MAXVISPLANES)
		return visplanes;

	// make a new plane
	check = lastvisplane;
	++lastvisplane;

	check->height = height;
	check->flatnum = flatnum;
	check->lightlevel = lightlevel;
	check->minx = start;
	check->maxx = stop;

	if (check->runopen)
	{
		R_MarkOpenPlane(check);
		check->runopen = false;
	}

	check->next = tail;
	visplanes_hash[hash] = check;

	return check;
}

void R_BSP (void);
void R_WallPrep (void);
void R_SpritePrep (void);
boolean R_LatePrep (void);
void R_Cache (void);
void R_SegCommands (void);
void R_DrawPlanes (void);
void R_Sprites (void);
void R_Update (void);

/*
==============
=
= R_RenderView
=
==============
*/

extern	boolean	debugscreenactive;

#ifndef MARS
int		phasetime[9] = {1,2,3,4,5,6,7,8,9};

extern	ref1_start;
extern	ref2_start;
extern	ref3_start;
extern	ref4_start;
extern	ref5_start;
extern	ref6_start;
extern	ref7_start;
extern	ref8_start;

void R_RenderPlayerView(void)
{
	/* make sure its done now */
#if defined(JAGUAR)
	while (!I_RefreshCompleted())
		;
#endif

	/* */
	/* initial setup */
	/* */
	if (debugscreenactive)
		I_DebugScreen();

	R_Setup();

#ifndef JAGUAR
	R_BSP();

	R_WallPrep();
	R_SpritePrep();
	/* the rest of the refresh can be run in parallel with the next game tic */
	if (R_LatePrep())
		R_Cache();

	R_SegCommands();

	R_DrawPlanes();

	R_Sprites();

	R_Update();
#else

	/* start the gpu running the refresh */
	phasetime[1] = 0;
	phasetime[2] = 0;
	phasetime[3] = 0;
	phasetime[4] = 0;
	phasetime[5] = 0;
	phasetime[6] = 0;
	phasetime[7] = 0;
	phasetime[8] = 0;
	gpufinished = zero;
	gpucodestart = (int)&ref1_start;

#endif
}

#else

static void R_RenderPhase1(void)
{
	Mars_R_BeginWallPrep();

	t_ref_bsp[t_ref_cnt] = I_GetFRTCounter();
	R_BSP();
	t_ref_bsp[t_ref_cnt] = I_GetFRTCounter() - t_ref_bsp[t_ref_cnt];

	Mars_R_StopBSP();
}

static void R_RenderPhases2To9(void)
{
	unsigned short openings_[MAXOPENINGS];

	openings = openings_;

	t_ref_prep[t_ref_cnt] = I_GetFRTCounter();
	R_SpritePrep();
	Mars_R_EndWallPrep();
	/* the rest of the refresh can be run in parallel with the next game tic */
	if (R_LatePrep())
		R_Cache();
	t_ref_prep[t_ref_cnt] = I_GetFRTCounter() - t_ref_prep[t_ref_cnt];

	t_ref_segs[t_ref_cnt] = I_GetFRTCounter();
	R_SegCommands ();
	t_ref_segs[t_ref_cnt] = I_GetFRTCounter() - t_ref_segs[t_ref_cnt];

	t_ref_planes[t_ref_cnt] = I_GetFRTCounter();
	R_DrawPlanes ();
	t_ref_planes[t_ref_cnt] = I_GetFRTCounter() - t_ref_planes[t_ref_cnt];

	t_ref_sprites[t_ref_cnt] = I_GetFRTCounter();
	R_Sprites ();
	t_ref_sprites[t_ref_cnt] = I_GetFRTCounter() - t_ref_sprites[t_ref_cnt];
}

void R_RenderPlayerView(void)
{
#ifdef MARS
	while (!I_RefreshCompleted())
		;
#endif

	t_ref_cnt = (t_ref_cnt + 1) & 3;

	/* */
	/* initial setup */
	/* */
	if (debugscreenactive)
		I_DebugScreen();

	t_ref_total[t_ref_cnt] = I_GetFRTCounter();

	R_Setup();

	R_RenderPhase1();

	R_RenderPhases2To9();

	R_Update();

	t_ref_total[t_ref_cnt] = I_GetFRTCounter() - t_ref_total[t_ref_cnt];
}

#endif
