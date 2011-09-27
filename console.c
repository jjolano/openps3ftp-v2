/*
 *    Console-like screen drawing functions
 *    Based on sconsole by Scognito (scognito@gmail.com)
 *    Copyright (C) 2011 John Olano (jjolano)
 *
 *    This file is part of OpenPS3FTP.
 *
 *    OpenPS3FTP is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    OpenPS3FTP is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with OpenPS3FTP.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "console.h"
#include "console_face.h"

typedef struct {
	u32 back;
	u32 colour;
	u16 width;
	u16 height;
} drawer;

drawer console;

void cSetDraw(u32 back, u32 colour, u16 width, u16 height)
{
	console.back = back;
	console.colour = colour;
	console.width = width;
	console.height = height;
}

void cPosDrawText(uint32_t *bufptr, u16 x, u16 y, char *text)
{
	u16 nx = x, ny = y;
	u16 tx = 0, ty = 0;
	int i;
	char c;
	
	// iterate through each character
	while((c = *text) != '\0')
	{
		// handle some characters
		switch(c)
		{
			case '\n':
			{
				ny += 24;
				break;
			}
			case ' ':
			{
				nx += 8;
				break;
			}
			default:
			{
				// change unsupported characters
				if(c < 32 || c > 129)
				{
					c = 128;
				}
				
				// write the actual character
				for(i = 0; i < (8 * 16); i++)
				{
					if(consoleFont[-32 + c][i] == 0)
					{
						// font background
						if(console.back != FONT_COLOR_NONE)
						{
							bufptr[(ny + ty) * console.width + nx + tx] = console.back;
						}
					}
					else
					{
						// font colour
						if(console.colour != FONT_COLOR_NONE)
						{
							bufptr[(ny + ty) * console.width + nx + tx] = console.colour;
						}
					}
					
					tx++;
					
					if(tx == 8)
					{
						tx = 0;
						ty++;
					}
				}
				
				ty = 0;
			}
		}
		
		// move to next character
		text++;
	}
}

