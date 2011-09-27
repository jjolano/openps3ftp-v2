#ifndef __CONSOLE_H__
#define __CONSOLE_H__

#include <ppu-types.h>

// 8x16 fonts
// 0xaarrggbb, 32-bit
// alpha part probably doesn't work
#define FONT_COLOR_NONE		-1
#define FONT_COLOR_BLACK	0x00000000
#define FONT_COLOR_WHITE	0xffffffff
#define FONT_COLOR_RED		0x00ff0000
#define FONT_COLOR_GREEN	0x0000ff00
#define FONT_COLOR_BLUE		0x000000ff

// use this first before printing stuff
// sets up the font (back, colour) and screen (width, height) settings
void cSetDraw(u32 back, u32 colour, u16 width, u16 height);

// draws text at a certain position. \n is allowed
void cPosDrawText(uint32_t *bufptr, u16 x, u16 y, char *text);

#endif /* __CONSOLE_H__ */
