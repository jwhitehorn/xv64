/* vga.c
 * Copyright (c) 2020 Jason Whitehorn
 * Copyright (c) 2019 Benson Chau, ngreenwald3, DannyFannyPack, tmaddali, DanielWygant
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */


#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "vga.h"


#define peekb(S,O)   *(uint8 *)(16uL * (S) + (O))
#define pokeb(S,O,V) *(uint8 *)(16uL * (S) + (O)) = (V)

static uint32 get_fb_seg() {
	uint32 seg;

	outb(VGA_GRAPHICS_ADDR_REG, 6);
	seg = inb(VGA_GRAPHICS_DATA_REG);
	seg >>= 2;
	seg &= 3;
	switch(seg)
	{
	case 0:
	case 1:
		seg = 0xA000;
		break;
	case 2:
		seg = 0xB000;
		break;
	case 3:
		seg = 0xB800;
		break;
	}
	return seg;
}

static void vpokeb(uint32 off, uint32 val) {
	pokeb(get_fb_seg(), off, val);
}

static unsigned vpeekb(uint32 off) {
	return peekb(get_fb_seg(), off);
}

static void set_plane(uint16 p) {
	uint8 pmask;

	p &= 3;
	pmask = 1 << p;
  /* set read plane */
	outb(VGA_GRAPHICS_ADDR_REG, 4);
	outb(VGA_GRAPHICS_DATA_REG, p);
  /* set write plane */
	outb(VGA_SEQUENCER_ADDR_REG, 2);
	outb(VGA_SEQUENCER_DATA_REG, pmask);
}

static void vga_write_regset(uchar *mode, uint len, uint addr_reg, uint data_reg) {
    int i;
    for (i = 0; i < len; i++) {
        outb(addr_reg, i);
        outb(data_reg, mode[i]);
    }
}

void vga_write_regs(uchar *mode) {
    // Write misc, seq, crtc, graphics controller,
    // attribute regs.

    // Write miscellaneous regs
    outb(VGA_MISC_REG, mode[0]);
    mode++;

    // Write sequencer regs
    vga_write_regset(mode, VGA_SEQ_LEN, VGA_SEQUENCER_ADDR_REG, VGA_SEQUENCER_DATA_REG);
    mode += VGA_SEQ_LEN;

    // Unlock & write CRTC regs
    // Enables vertical retracing:
    outb(VGA_CRTC_ADDR_REG, 0x03);
    outb(VGA_CRTC_DATA_REG, inb(VGA_CRTC_DATA_REG) | 0x80);
    // disables CRTC Registers Protect Enable, so 0x00h-0x07h are writable.
    outb(VGA_CRTC_ADDR_REG, 0x11);
    outb(VGA_CRTC_DATA_REG, inb(VGA_CRTC_DATA_REG) & ~0x80);
    // modify mode contents to reflect our unlocking changes
    mode[0x03] |= 0x80;
    mode[0x11] &= ~0x80;
    vga_write_regset(mode, VGA_CRTC_LEN, VGA_CRTC_ADDR_REG, VGA_CRTC_DATA_REG);
    mode += VGA_CRTC_LEN;

    // Write graphics controller regs
    vga_write_regset(mode, VGA_GRAPHICS_LEN, VGA_GRAPHICS_ADDR_REG, VGA_GRAPHICS_DATA_REG);
    mode += VGA_GRAPHICS_LEN;

    // Write attribute controller regs
    vga_write_regset(mode, VGA_ATTR_LEN, VGA_ATTR_ADDR_REG, VGA_ATTR_DATA_REG);
    mode += VGA_ATTR_LEN;

    // Supposed to enable VGA display, no idea how or why.
    // Can't find the docs for why we need to set the VGA attribute
    // index register to 0x20?
    inb(VGA_INSTAT_READ);
    outb(VGA_ATTR_ADDR_REG, 0x20);
}

void vga_setpixel4p(uint32 x, uint32 y, uint32 c) {
	uint32 wd_in_bytes, off, mask, p, pmask;
  uint32 WIDTH = 640;

	wd_in_bytes = WIDTH / 8;
	off = wd_in_bytes * y + x / 8;
	x = (x & 7) * 1;
	mask = 0x80 >> x;
	pmask = 1;
	for(p = 0; p < 4; p++){
		set_plane(p);
		if(pmask & c)
			vpokeb(off, vpeekb(off) | mask);
		else
			vpokeb(off, vpeekb(off) & ~mask);
		pmask <<= 1;
	}
}
