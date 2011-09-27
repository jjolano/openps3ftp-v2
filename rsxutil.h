/*
 * Copyright (C) Youness Alaoui (KaKaRoTo)
 *
 * This software is distributed under the terms of the GNU General Public
 * License ("GPL") version 3, as published by the Free Software Foundation.
 */

#ifndef __RSXUTIL_H__
#define __RSXUTIL_H__

#include <sysutil/video.h> // videoState, videoResolution, videoGetState, videoGetResolution
#include <rsx/rsx.h> // gcmGetFlipStatus, gcmResetFlipStatus, rsxMemalign, rsxAddressToOffset, gcmSetDisplayBuffer, rsxFree

#define CB_SIZE		0x100000
#define HOST_SIZE	(32*1024*1024)
#define blurt	printf ("%s:%s():#%d ::\n", __FILE__, __func__, __LINE__);

typedef struct
{
  int id;
  int height;
  int width;
  uint32_t *ptr;
  // Internal stuff
  uint32_t offset;
} rsxBuffer;


/* Block the PPU thread untill the previous flip operation has finished. */
void waitFlip (void);
/* Flip a buffer onto the screen. Returns TRUE on success */
int flip (gcmContextData *context, s32 buffer);
/* Create a buffer to draw into and assign it to @id. Returns NULL on error */
int makeBuffer (rsxBuffer * buffer, u16 width, u16 height, int id);
/* Get current screen resolution. returns TRUE on success */
int getResolution (u16 *width, u16 *height);
/* Initilize the RSX properly. Returns NULL on error */
gcmContextData *screenInit (void *host_addr, u32 size);
/* Sets the target buffer to render to */
void setRenderTarget(gcmContextData *context, rsxBuffer *buffer);

gcmContextData *initScreen (void *host_addr, u32 size);
#endif /* __RSXUTIL_H__ */
