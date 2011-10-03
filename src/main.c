/*
 *    OpenPS3FTP main source - screen and thread init
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>

#include <io/pad.h>

#include <rsx/rsx.h>

#include <net/net.h>
#include <net/netctl.h>
#include <arpa/inet.h>

#include <sys/file.h>
#include <sys/thread.h>
#include <sys/process.h>

#include <sysutil/msg.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>

#include <sysmodule/sysmodule.h>

#include "defs.h"
#include "ftp.h"
#include "rsxutil.h"
#include "console.h"

#define MSGBOX_TYPE_OK		(MSG_DIALOG_NORMAL | MSG_DIALOG_BTN_TYPE_OK | MSG_DIALOG_DISABLE_CANCEL_ON)
#define MSGBOX_TYPE_BLANK	(MSG_DIALOG_NORMAL | MSG_DIALOG_DISABLE_CANCEL_ON)

SYS_PROCESS_PARAM(1000, SYS_PROCESS_SPAWN_STACK_SIZE_1M)

int running = 1;
char passwd[64];

// for the display
gcmContextData *context;
rsxBuffer buf[3];
int ibuf = 0;

// for message dialogs
int dialog_result = 0;

void dialog_handler(msgButton button, void *usrdata)
{
	// nothing
	dialog_result = 1;
}

void sysutil_callback(u64 status, u64 param, void *usrdata)
{
	// Check if the user is exiting the program
	if(status == SYSUTIL_EXIT_GAME)
	{
		// Set the running variable to stop any loops from looping again
		running = 0;
	}
}

int main(void)
{
	// register the exit callback
	sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sysutil_callback, NULL);
	
	// start rsx stuff
	void *host_addr = memalign(1024*1024, HOST_SIZE);
	context = screenInit(host_addr, HOST_SIZE);
	
	u16 width, height;
	getResolution(&width, &height);
	
	makeBuffer(&buf[0], width, height, 0);
	makeBuffer(&buf[1], width, height, 1);
	makeBuffer(&buf[2], width, height, 2);
	
	setRenderTarget(context, &buf[2]);
	
	// check first run
	if(sysLv2FsUnlink("/dev_hdd0/game/OFTP00001/USRDIR/firstrun") == 0)
	{
		msgDialogOpen2(MSGBOX_TYPE_OK, "OpenPS3FTP by jjolano\nLicensed under the GNU GPL version 3.\n\nIf this is your first time using OpenPS3FTP, please check out the README and ChangeLog in the zip archive before using this program.", dialog_handler, NULL, NULL);
		
		while(running && !dialog_result)
		{
			sysUtilCheckCallback();
			usleep(200);
		}
		
		msgDialogAbort();
	}
	
	msgDialogOpen2(MSGBOX_TYPE_BLANK, "Starting OpenPS3FTP...", dialog_handler, NULL, NULL);
	
	netInitialize();
	netCtlInit();
	
	union net_ctl_info info;
	
	// check if the system has an IP address
	if(netCtlGetInfo(NET_CTL_INFO_IP_ADDRESS, &info) == 0)
	{
		// read the password file
		s32 fd;
		u64 read = 0;
		
		if(sysLv2FsOpen(OFTP_PASSWORD_FILE, SYS_O_RDONLY | SYS_O_CREAT, &fd, 0660, NULL, 0) == 0)
		{
			sysLv2FsRead(fd, passwd, 63, &read);
			sysLv2FsClose(fd);
		}
		
		passwd[read] = '\0';
		
		// set socket parameters
		struct sockaddr_in sa;
		memset(&sa, 0, sizeof(sa));
		
		sa.sin_family = AF_INET;
		sa.sin_port = htons(21);
		sa.sin_addr.s_addr = htonl(INADDR_ANY);
		
		// create the socket
		int list_s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
		
		// bind parameters and start listening
		if(bind(list_s, (struct sockaddr *)&sa, sizeof(sa)) == 0
		&& listen(list_s, OFTP_LISTEN_BACKLOG) == 0)
		{
			// create a thread for the accept loop
			sys_ppu_thread_t id;
			sysThreadCreate(&id, listen_thread, (void *)&list_s, 1001, 0x100, 0, "listener");
			
			// start console text drawer
			cSetDraw(FONT_COLOR_BLACK, FONT_COLOR_WHITE, width, height);
			
			// initialize io pad
			ioPadInit(1);
			
			padInfo2 padinfo;
			padData paddata;
			
			// text to draw
			char text[256];
			sprintf(text, "OpenPS3FTP v%s by jjolano\n\nFTP Server IP: %s\n\nIf you like my work, please donate by PayPal: http://bit.ly/gmzGcI\n\nPress SELECT + START to exit. Thank you for using this program!", OFTP_VERSION, info.ip_address);
			
			// close the msgbox
			waitFlip();
			msgDialogAbort();
			
			while(running)
			{
				sysUtilCheckCallback();
				
				ioPadGetInfo2(&padinfo);
				
				if(padinfo.port_status[0])
				{
					ioPadGetData(0, &paddata);
					if(paddata.BTN_SELECT && paddata.BTN_START)
					{
						running = 0;
						break;
					}
				}
				
				
				waitFlip();
				ibuf = !ibuf;
				cPosDrawText(buf[ibuf].ptr, 50, 50, text);
				flip(context, ibuf);
			}
		}
		else
		{
			// abort on failure
			msgDialogAbort();
			
			msgDialogOpen2(MSGBOX_TYPE_OK, "fatal error: can't bind address to socket", dialog_handler, NULL, NULL);
			
			dialog_result = 0;
			while(running && !dialog_result)
			{
				sysUtilCheckCallback();
				usleep(200);
			}
			
			msgDialogAbort();
			
			close(list_s);
		}
	}
	else
	{
		msgDialogOpen2(MSGBOX_TYPE_OK, "fatal error: can't get ip address", dialog_handler, NULL, NULL);
		
		dialog_result = 0;
		while(running && !dialog_result)
		{
			sysUtilCheckCallback();
			usleep(200);
		}
		
		msgDialogAbort();
	}
	
	// unload modules - kills any remaining connections
	netDeinitialize();
	return 0;
}

