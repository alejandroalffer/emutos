/*
 * screen.c - low-level screen routines
 *
 * Copyright (c) 2001-2003 EmuTOS development team
 *
 * Authors:
 *  MAD   Martin Doering
 *  THO   Thomas Huth
 *  LVL   Laurent Vogel
 *  joy   Petr Stehlik
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"  
#include "machine.h"
#include "screen.h"
#include "asm.h"
#include "tosvars.h"

/* private prototypes */

static void setphys(LONG addr);

/* determine monitor type, ... */

/* Define palette */

const static WORD dflt_palette[] = {
    RGB_WHITE, RGB_RED, RGB_GREEN, RGB_YELLOW,
    RGB_BLUE, RGB_MAGENTA, RGB_CYAN, RGB_LTGRAY,
    RGB_GRAY, RGB_LTRED, RGB_LTGREEN, RGB_LTYELLOW,
    RGB_LTBLUE, RGB_LTMAGENTA, RGB_LTCYAN, RGB_BLACK
};


/*
 * In the original TOS there used to be an early screen init, 
 * before memory configuration. This is not used here, and all is
 * done at the same time from C.
 */

void screen_init(void)
{
    volatile BYTE *rez_reg = (BYTE *) 0xffff8260;
    volatile WORD *col_regs = (WORD *) 0xffff8240;
    volatile struct {
        BYTE gpip;
    } *mfp = (void *) 0xfffffa01;
    WORD rez;
    WORD i;
    ULONG screen_size = 32000;  /* standard Shifter videoram size */
    ULONG screen_start;

    if (has_videl) {
        /* reset VIDEL on boot-up */
        /* first set the physbase to a safe memory */
        setphys(0x10000L);
        /* then change the resolution to 2-bit depth VGA */
        set_videl_vga640x480(2);

        /* set desired resolution (fetch it from NVRAM - TODO) */
        set_videl_vga640x480(1);

        /* detect real videoram size from the current resolution */
        screen_size = get_videl_width() / 8L * get_videl_height()
                      * get_videl_bpp();
    }
    else {
        *(BYTE *) 0xffff820a = 2;   /* sync-mode to 50 hz pal, internal sync */
    }

    for (i = 0; i < 16; i++) {
        col_regs[i] = dflt_palette[i];
    }

    rez = *rez_reg;

    rez &= 3;
    if (rez == 3) {
        rez = 2;
    }

    if ((mfp->gpip & 0x80) != 0) {
        /* color monitor */
        if (rez == 2)
            rez = 0;
    } else {
        if (rez < 2)
            rez = 2;
    }

    if (has_videl)   /* enforce mono mode on Videl */
        rez = 2;     /* FIXME: replace with proper screen depth detection */

    *rez_reg = rez;  /* on real Falcon this could cause trouble (?) */
    sshiftmod = rez;

    if (rez == 1) {
        col_regs[3] = col_regs[15];
    }
    else if (rez == 2) {
        col_regs[1] = col_regs[15];
    }

    /* videoram is placed just below the phystop */
    screen_start = (ULONG)phystop - screen_size;
    /* round down to 256 byte boundary */
    screen_start &= 0x00ffff00;
    /* set new v_bas_ad */
    v_bas_ad = (BYTE *)screen_start;
    /* correct phystop */
    setphys((LONG) v_bas_ad);
}

/* misc routines */

static void setphys(LONG addr)
{
    *(UBYTE *) 0xffff8201 = ((ULONG) addr) >> 16;
    *(UBYTE *) 0xffff8203 = ((ULONG) addr) >> 8;
    if (has_ste_shifter) {
        *(UBYTE *) 0xffff820d = ((ULONG) addr);
    }
}

/* xbios routines */

LONG physbase(void)
{
    LONG addr;

    addr = *(UBYTE *) 0xffff8201;
    addr <<= 8;
    addr += *(UBYTE *) 0xffff8203;
    addr <<= 8;
    if (has_ste_shifter) {
        addr += *(UBYTE *) 0xffff820D;
    }

    return (addr);
}

LONG logbase(void)
{
    return ((ULONG) v_bas_ad);
}

WORD getrez(void)
{
    return (*(UBYTE *) 0xffff8260);
}


void setscreen(LONG logLoc, LONG physLoc, WORD rez)
{
    if (logLoc >= 0) {
        v_bas_ad = (char *) logLoc;
    }
    if (physLoc >= 0) {
        screenpt = (char *) physLoc;
        /* will be set up at next VBL */
    }
    if (rez >= 0) {
        defshiftmod = rez;
    }
}

void setpalette(LONG palettePtr)
{
    /* next VBL will do this */
    colorptr = (WORD *) palettePtr;
}

WORD setcolor(WORD colorNum, WORD color)
{
    WORD rez = getrez();
    WORD max;
    WORD mask;
    WORD *palette = (WORD *) 0xffff8240;
    switch (rez) {
    case 0:
        max = 15;
        break;
    case 1:
        max = 3;
        break;
    case 2:
        max = 1;
        break;
    default:
        max = 0;
    }
    if (has_ste_shifter) {
        mask = 0xfff;
    } else {
        mask = 0x777;
    }
    if (colorNum >= 0 && colorNum <= max) {
        if (color == -1) {
            return palette[colorNum] & mask;
        } else {
            palette[colorNum] = color;
            return color & mask;
        }
    } else {
        return 0;
    }
}


void vsync(void)
{
    WORD old_sr;
    LONG a;

    old_sr = set_sr(0x2300);    /* allow VBL interrupt */
    a = frclock;
    while (frclock == a) {
#if USE_STOP_INSN_TO_FREE_HOST_CPU
        stop2300();
#else
        ;
#endif
    }
    set_sr(old_sr);
}

/*
 * functions for VIDEL programming
 */

UWORD get_videl_bpp()
{
    UWORD f_shift = *(UWORD *)0xff8266;
    UWORD st_shift = *(UWORD *)0xff8260;
    /* to get bpp, we must examine f_shift and st_shift.
     * f_shift is valid if any of bits no. 10, 8 or 4
     * is set. Priority in f_shift is: 10 ">" 8 ">" 4, i.e.
     * if bit 10 set then bit 8 and bit 4 don't care...
     * If all these bits are 0 get display depth from st_shift
     * (as for ST and STE)
     */
    int bits_per_pixel = 1;
    if (f_shift & 0x400)         /* 2 colors */
        bits_per_pixel = 1;
    else if (f_shift & 0x100)    /* hicolor */
        bits_per_pixel = 16;
    else if (f_shift & 0x010)    /* 8 bitplanes */
        bits_per_pixel = 8;
    else if (st_shift == 0)
        bits_per_pixel = 4;
    else if (st_shift == 0x100)
        bits_per_pixel = 2;
    else                         /* if (st_shift == 0x200) */
        bits_per_pixel = 1;

    return bits_per_pixel;
}

UWORD get_videl_width()
{
    return (*(UWORD *)0xff8210) * 16 / get_videl_bpp();
}

UWORD get_videl_height()
{
    UWORD vdb = *(UWORD *)0xff82a8;
    UWORD vde = *(UWORD *)0xff82aa;
    UWORD vmode = *(UWORD *)0xff82c2;

    /* visible y resolution:
     * Graphics display starts at line VDB and ends at line
     * VDE. If interlace mode off unit of VC-registers is
     * half lines, else lines.
     */
    UWORD yres = vde - vdb;
    if (!(vmode & 0x02))        /* interlace */
        yres >>= 1;
    if (vmode & 0x01)           /* double */
        yres >>= 1;

    return yres;
}

/*
 * this routine can set VIDEL to 1,2,4 or 8 bitplanes mode
 * on VGA in 640x480 resolution
 */
void set_videl_vga640x480(int bitplanes)
{
        char *videlregs = (char *)0xff8200;
    int hdb[4] = {115, 138, 163, 171 };
    int hde[4] = {80, 107, 124, 132 };
    int lwidth[4] = {40, 80, 160, 320 };
    int fsm[4] = { 0x400, 0, 0, 16 };
    int vmd[4] = { 8, 4, 8, 8};
    int idx;

    switch(bitplanes) {
        case 1: idx = 0; break;
        case 2: idx = 1; break;
        case 4: idx = 2; break;
        case 8: idx = 3; break;
        default: idx = 0;
    }

    videlregs[0x0a] = 2;
    videlregs[0x82] = 0;
    videlregs[0x83] = 198;
    videlregs[0x84] = 0;
    videlregs[0x85] = 141;
    videlregs[0x86] = 0;
    videlregs[0x87] = 21;
    videlregs[0x88] = 2;
    videlregs[0x89] = hdb[idx];
    videlregs[0x8a] = 0;
    videlregs[0x8b] = hde[idx];
    videlregs[0x8c] = 0;
    videlregs[0x8d] = 150;
    videlregs[0xa2] = 4;
    videlregs[0xa3] = 25;
    videlregs[0xa4] = 3;
    videlregs[0xa5] = 255;
    videlregs[0xa6] = 0;
    videlregs[0xa7] = 63;
    videlregs[0xa8] = 0;
    videlregs[0xa9] = 63;
    videlregs[0xaa] = 3;
    videlregs[0xab] = 255;
    videlregs[0xac] = 4;
    videlregs[0xad] = 21;
    videlregs[0x0e] = 0;
    videlregs[0x0f] = 0;
    videlregs[0x10] = lwidth[idx] >> 8;
    videlregs[0x11] = lwidth[idx] & 0xff;
    videlregs[0xc2] = 0;
    videlregs[0xc3] = vmd[idx];
    videlregs[0xc0] = 1;
    videlregs[0xc1] = 134;
    videlregs[0x66] = 0;
    videlregs[0x67] = 0;

    videlregs[0x66] = fsm[idx] >> 8;
    videlregs[0x67] = fsm[idx] & 0xff;
}
