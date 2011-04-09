/*
    This file is part of OpenPS3FTP.

    OpenPS3FTP is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenPS3FTP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with OpenPS3FTP.  If not, see <http://www.gnu.org/licenses/>.
*/

const char* TITLE	= "OpenPS3FTP";
const char* VERSION	= "2.2";
const char* AUTHOR	= "jjolano";

#include <assert.h>

#include <psl1ght/lv2/timer.h>

#include <sysutil/video.h>
#include <sysutil/events.h>

#include <rsx/gcm.h>
#include <rsx/reality.h>

#include <net/netctl.h>

#include <sys/thread.h>

#include <io/pad.h>

#include "common.h"
#include "functions.h"
#include "sconsole.h"

char userpass[64];
char statustext[128];

const char* PASSWD_FILE	= "/dev_hdd0/game/OFTP00001/USRDIR/passwd";

typedef struct {
	int height;
	int width;
	uint32_t *ptr;
	// Internal stuff
	uint32_t offset;
} buffer;

gcmContextData *context;
VideoResolution res;
int currentBuffer = 0;
buffer *buffers[2];

void waitFlip()
{
	// Block the PPU thread until the previous flip operation has finished.
	while (gcmGetFlipStatus() != 0)
		usleep(200);
	gcmResetFlipStatus();
}

void flip(s32 buffer)
{
	assert(gcmSetFlip(context, buffer) == 0);
	realityFlushBuffer(context);
	gcmSetWaitFlip(context);
}

void makeBuffer(int id, int size)
{
	buffer *buf = malloc(sizeof(buffer));
	buf->ptr = rsxMemAlign(16, size);
	assert(buf->ptr != NULL);

	assert(realityAddressToOffset(buf->ptr, &buf->offset) == 0);
	assert(gcmSetDisplayBuffer(id, buf->offset, res.width * 4, res.width, res.height) == 0);

	buf->width = res.width;
	buf->height = res.height;
	buffers[id] = buf;
}

void init_screen()
{
	void *host_addr = memalign(1024*1024, 1024*1024);
	assert(host_addr != NULL);

	context = realityInit(0x10000, 1024*1024, host_addr); 
	assert(context != NULL);

	VideoState state;
	assert(videoGetState(0, 0, &state) == 0);
	assert(state.state == 0);

	assert(videoGetResolution(state.displayMode.resolution, &res) == 0);

	VideoConfiguration vconfig;
	memset(&vconfig, 0, sizeof(VideoConfiguration));
	vconfig.resolution = state.displayMode.resolution;
	vconfig.format = VIDEO_BUFFER_FORMAT_XRGB;
	vconfig.pitch = res.width * 4;

	assert(videoConfigure(0, &vconfig, NULL, 0) == 0);
	assert(videoGetState(0, 0, &state) == 0); 

	s32 buffer_size = 4 * res.width * res.height;

	gcmSetFlipMode(GCM_FLIP_VSYNC);
	makeBuffer(0, buffer_size);
	makeBuffer(1, buffer_size);

	gcmResetFlipStatus();
	flip(1);
}

int exitapp = 0;
int drawstate = 0;
int ssactive = 0;

void sysevent_callback(u64 status, u64 param, void * userdata)
{
	switch(status)
	{
		case EVENT_REQUEST_EXITAPP:exitapp = 1;break;
		case EVENT_MENU_CLOSE:ssactive = 2;break;
		case EVENT_DRAWING_END:drawstate = 0;break;
	}
}

void opf_clienthandler(u64 arg)
{
	int conn_s = (int)arg;
	int data_s = -1;
	
	int dataactive = -1;
	int loggedin = 0;
	long long rest = 0;
	
	char user[32];
	char rnfr[256];
	char cwd[256] = "/";
	char path[256];
	
	char buffer[384];
	size_t bytes;
	
	netSocketInfo p;
	netGetSockInfo(conn_s & ~SOCKET_FD_MASK, &p, 1);
	
	srand(conn_s);
	int p1 = (rand() % 251) + 4;
	int p2 = rand() % 256;
	
	char pasv_output[64];
	int pasv_len = sprintf(pasv_output, "227 Entering Passive Mode (%i,%i,%i,%i,%i,%i)\r\n", NIPQUAD(p.local_adr.s_addr), p1, p2);
	
	bytes = sprintf(buffer, "220-%s by %s\r\n220 Version %s\r\n", TITLE, AUTHOR, VERSION);
	send(conn_s, buffer, bytes, 0);
	
	while(exitapp == 0 && (bytes = recv(conn_s, buffer, 272, 0)) > 0)
	{
		buffer[bytes - 1] = '\0';
		buffer[bytes - 2] = '\0';
		
		char cmd[16], param[256];
		int split = ssplit(buffer, cmd, 15, param, 255);
		
		strtoupper(cmd);
		
		if(strcmp2(cmd, "QUIT") == 0)
		{
			ssend(conn_s, "221 Bye!\r\n");
			break;
		}
		else
		if(strcmp2(cmd, "CLNT") == 0)
		{
			ssend(conn_s, "200 Cool story, bro\r\n");
		}
		else
		if(strcmp2(cmd, "FEAT") == 0)
		{
			char *feat[] =
			{
				"REST STREAM", "PASV", "PORT", "MDTM", "MLSD", "SIZE", "SITE CHMOD", "APPE",
				"MLST type*;size*;modify*;UNIX.mode*;UNIX.uid*;UNIX.gid*;"
			};
			
			int feat_count = sizeof(feat) / sizeof(char *);
			
			ssend(conn_s, "211-Features:\r\n");
			
			for(int i = 0; i < feat_count; i++)
			{
				bytes = sprintf(buffer, " %s\r\n", feat[i]);
				send(conn_s, buffer, bytes, 0);
			}
			
			ssend(conn_s, "211 End\r\n");
		}
		else
		if(strcmp2(cmd, "SYST") == 0)
		{
			ssend(conn_s, "215 UNIX Type: L8\r\n");
		}
		else
		if(strcmp2(cmd, "NOOP") == 0)
		{
			ssend(conn_s, "200 NOOP command successful\r\n");
		}
		else
		if(loggedin == 1)
		{
			if(strcmp2(cmd, "CWD") == 0 || strcmp2(cmd, "XCWD") == 0)
			{
				abspath(param, cwd, path);
				
				if(is_dir(path))
				{
					strcpy(cwd, path);
					ssend(conn_s, "250 Directory change successful\r\n");
				}
				else
				{
					ssend(conn_s, "550 Cannot access directory\r\n");
				}
			}
			else
			if(strcmp2(cmd, "PWD") == 0 || strcmp2(cmd, "XPWD") == 0)
			{
				bytes = sprintf(buffer, "257 \"%s\" is the current directory\r\n", cwd);
				send(conn_s, buffer, bytes, 0);
			}
			else
			if(strcmp2(cmd, "MKD") == 0 || strcmp2(cmd, "XMKD") == 0)
			{
				if(split == 1)
				{
					abspath(param, cwd, path);
					
					if(sysFsMkdir(path, 0755) == 0)
					{
						bytes = sprintf(buffer, "257 \"%s\" was successfully created\r\n", path);
						send(conn_s, buffer, bytes, 0);
					}
					else
					{
						ssend(conn_s, "550 Cannot create directory\r\n");
					}
				}
			}
			else
			if(strcmp2(cmd, "RMD") == 0 || strcmp2(cmd, "XRMD") == 0)
			{
				if(split == 1)
				{
					abspath(param, cwd, path);
					
					if(sysFsRmdir(path) == 0)
					{
						ssend(conn_s, "250 Directory successfully removed\r\n");
					}
					else
					{
						ssend(conn_s, "550 Cannot remove directory\r\n");
					}
				}
				else
				{
					ssend(conn_s, "501 Insufficient parameters\r\n");
				}
			}
			else
			if(strcmp2(cmd, "CDUP") == 0 || strcmp2(cmd, "XCUP") == 0)
			{
				int c;
				int len = strlen(cwd) - 1;
				
				for(int i = len; i > 0; i--)
				{
					c = cwd[i];
					cwd[i] = '\0';
					
					if(c == '/' && i < len)
					{
						break;
					}
				}
				
				ssend(conn_s, "200 Directory change successful\r\n");
			}
			else
			if(strcmp2(cmd, "PASV") == 0)
			{
				sclose(&data_s);
				rest = 0;
				
				int list_s = slisten(getPort(p1, p2), 1);
				
				if(list_s > 0)
				{
					send(conn_s, pasv_output, pasv_len, 0);
					
					if((data_s = accept(list_s, NULL, NULL)) > 0)
					{
						dataactive = 1;
					}
					else
					{
						ssend(conn_s, "451 Data connection failed\r\n");
					}
					
					sclose(&list_s);
				}
				else
				{
					ssend(conn_s, "451 Cannot create data socket\r\n");
				}
			}
			else
			if(strcmp2(cmd, "PORT") == 0)
			{
				if(split == 1)
				{
					sclose(&data_s);
					rest = 0;
					
					char data[6][4];
					char *st = strtok(param, ",");
					
					int i = 0;
					while(st != NULL && i < 6)
					{
						strcpy(data[i++], st);
						st = strtok(NULL, ",");
					}
					
					if(i >= 6)
					{
						char ipaddr[16];
						sprintf(ipaddr, "%s.%s.%s.%s", data[0], data[1], data[2], data[3]);
						
						if(sconnect(ipaddr, getPort(atoi(data[4]), atoi(data[5])), &data_s) == 0)
						{
							dataactive = 1;
							ssend(conn_s, "200 PORT command successful\r\n");
						}
						else
						{
							dataactive = 0;
							ssend(conn_s, "451 Data connection failed\r\n");
						}
					}
					else
					{
						ssend(conn_s, "501 Insufficient connection info\r\n");
					}
				}
				else
				{
					ssend(conn_s, "501 Insufficient parameters\r\n");
				}
			}
			else
			if(strcmp2(cmd, "ABOR") == 0)
			{
				dataactive = 0;
				ssend(conn_s, "226 ABOR command successful\r\n");
			}
			else
			if(strcmp2(cmd, "LIST") == 0)
			{
				if(data_s > 0 || sconnect(inet_ntoa(p.remote_adr), 20, &data_s) == 0)
				{
					Lv2FsFile fd;
					if(sysFsOpendir(cwd, &fd) == 0)
					{
						Lv2FsDirent entry;
						u64 read;
						
						ssend(conn_s, "150 Accepted data connection\r\n");
						
						while(sysFsReaddir(fd, &entry, &read) == 0 && read > 0)
						{
							if(strcmp2(cwd, "/") == 0 && strncmp(entry.d_name, "dev_", 4) != 0)
							{
								continue;
							}
							
							abspath(entry.d_name, cwd, path);
							
							Lv2FsStat buf;
							sysFsStat(path, &buf);
							
							char tstr[16];
							strftime(tstr, 15, "%b %d %H:%M", localtime(&buf.st_mtime));
							
							bytes = sprintf(buffer, "%s%s%s%s%s%s%s%s%s%s   1 nobody   nobody       %llu %s %s\r\n",
								fis_dir(buf) ? "d" : "-", 
								((buf.st_mode & S_IRUSR) != 0) ? "r" : "-",
								((buf.st_mode & S_IWUSR) != 0) ? "w" : "-",
								((buf.st_mode & S_IXUSR) != 0) ? "x" : "-",
								((buf.st_mode & S_IRGRP) != 0) ? "r" : "-",
								((buf.st_mode & S_IWGRP) != 0) ? "w" : "-",
								((buf.st_mode & S_IXGRP) != 0) ? "x" : "-",
								((buf.st_mode & S_IROTH) != 0) ? "r" : "-",
								((buf.st_mode & S_IWOTH) != 0) ? "w" : "-",
								((buf.st_mode & S_IXOTH) != 0) ? "x" : "-",
								(unsigned long long)buf.st_size, tstr, entry.d_name);
							
							/*// EPLF output
							bytes = sprintf(buffer, "+up%i%i%i,m%lu,%s,s%llu,\t%s\r\n",
								(((buf.st_mode & S_IRUSR) != 0) * 4 + ((buf.st_mode & S_IWUSR) != 0) * 2 + ((buf.st_mode & S_IXUSR) != 0) * 1),
								(((buf.st_mode & S_IRGRP) != 0) * 4 + ((buf.st_mode & S_IWGRP) != 0) * 2 + ((buf.st_mode & S_IXGRP) != 0) * 1),
								(((buf.st_mode & S_IROTH) != 0) * 4 + ((buf.st_mode & S_IWOTH) != 0) * 2 + ((buf.st_mode & S_IXOTH) != 0) * 1),
								(unsigned long)buf.st_mtime,
								fis_dir(buf) ? "/" : "r",
								(unsigned long long)buf.st_size,
								entry.d_name);
							*/
							
							send(data_s, buffer, bytes, 0);
						}
						
						ssend(conn_s, "226 Transfer complete\r\n");
					}
					else
					{
						ssend(conn_s, "550 Cannot access directory\r\n");
					}
					
					sysFsClosedir(fd);
				}
				else
				{
					ssend(conn_s, "425 No data connection\r\n");
				}
				
				dataactive = 0;
			}
			else
			if(strcmp2(cmd, "MLSD") == 0)
			{
				if(data_s > 0 || sconnect(inet_ntoa(p.remote_adr), 20, &data_s) == 0)
				{
					Lv2FsFile fd;
					if(sysFsOpendir(cwd, &fd) == 0)
					{
						Lv2FsDirent entry;
						u64 read;
						
						ssend(conn_s, "150 Accepted data connection\r\n");
						
						while(sysFsReaddir(fd, &entry, &read) == 0 && read > 0)
						{
							if(strcmp2(cwd, "/") == 0 && strncmp(entry.d_name, "dev_", 4) != 0)
							{
								continue;
							}
							
							abspath(entry.d_name, cwd, path);
							
							Lv2FsStat buf;
							sysFsStat(path, &buf);
							
							char tstr[16];
							strftime(tstr, 15, "%Y%m%d%H%M%S", localtime(&buf.st_mtime));
							
							char dirtype[2];
							if(strcmp2(entry.d_name, ".") == 0)
							{
								dirtype[0] = 'c';
							}
							else
							if(strcmp2(entry.d_name, "..") == 0)
							{
								dirtype[0] = 'p';
							}
							else
							{
								dirtype[0] = '\0';
							}
							
							dirtype[1] = '\0';
							
							bytes = sprintf(buffer, "type=%s%s;siz%s=%llu;modify=%s;UNIX.mode=0%i%i%i;UNIX.uid=nobody;UNIX.gid=nobody; %s\r\n",
								dirtype,
								fis_dir(buf) ? "dir" : "file",
								fis_dir(buf) ? "d" : "e", (unsigned long long)buf.st_size, tstr,
								(((buf.st_mode & S_IRUSR) != 0) * 4 + ((buf.st_mode & S_IWUSR) != 0) * 2 + ((buf.st_mode & S_IXUSR) != 0) * 1),
								(((buf.st_mode & S_IRGRP) != 0) * 4 + ((buf.st_mode & S_IWGRP) != 0) * 2 + ((buf.st_mode & S_IXGRP) != 0) * 1),
								(((buf.st_mode & S_IROTH) != 0) * 4 + ((buf.st_mode & S_IWOTH) != 0) * 2 + ((buf.st_mode & S_IXOTH) != 0) * 1),
								entry.d_name);
							
							send(data_s, buffer, bytes, 0);
						}
						
						ssend(conn_s, "226 Transfer complete\r\n");
					}
					else
					{
						ssend(conn_s, "550 Cannot access directory\r\n");
					}
					
					sysFsClosedir(fd);
				}
				else
				{
					ssend(conn_s, "425 No data connection\r\n");
				}
				
				dataactive = 0;
			}
			else
			if(strcmp2(cmd, "MLST") == 0)
			{
				Lv2FsFile fd;
				if(sysFsOpendir(cwd, &fd) == 0)
				{
					Lv2FsDirent entry;
					u64 read;
					
					ssend(conn_s, "250-Directory Listing:\r\n");
					
					while(sysFsReaddir(fd, &entry, &read) == 0 && read > 0)
					{
						if(strcmp2(cwd, "/") == 0 && strncmp(entry.d_name, "dev_", 4) != 0)
						{
							continue;
						}
						
						abspath(entry.d_name, cwd, path);
						
						Lv2FsStat buf;
						sysFsStat(path, &buf);
						
						char tstr[16];
						strftime(tstr, 15, "%Y%m%d%H%M%S", localtime(&buf.st_mtime));
						
						char dirtype[2];
						if(strcmp2(entry.d_name, ".") == 0)
						{
							dirtype[0] = 'c';
						}
						else
						if(strcmp2(entry.d_name, "..") == 0)
						{
							dirtype[0] = 'p';
						}
						else
						{
							dirtype[0] = '\0';
						}
						
						dirtype[1] = '\0';
						
						bytes = sprintf(buffer, " type=%s%s;siz%s=%llu;modify=%s;UNIX.mode=0%i%i%i;UNIX.uid=nobody;UNIX.gid=nobody; %s\r\n",
							dirtype,
							fis_dir(buf) ? "dir" : "file",
							fis_dir(buf) ? "d" : "e", (unsigned long long)buf.st_size, tstr,
							(((buf.st_mode & S_IRUSR) != 0) * 4 + ((buf.st_mode & S_IWUSR) != 0) * 2 + ((buf.st_mode & S_IXUSR) != 0) * 1),
							(((buf.st_mode & S_IRGRP) != 0) * 4 + ((buf.st_mode & S_IWGRP) != 0) * 2 + ((buf.st_mode & S_IXGRP) != 0) * 1),
							(((buf.st_mode & S_IROTH) != 0) * 4 + ((buf.st_mode & S_IWOTH) != 0) * 2 + ((buf.st_mode & S_IXOTH) != 0) * 1),
							entry.d_name);
						
						send(conn_s, buffer, bytes, 0);
					}
					
					ssend(conn_s, "250 End\r\n");
				}
				else
				{
					ssend(conn_s, "550 Cannot access directory\r\n");
				}
				
				sysFsClosedir(fd);
			}
			else
			if(strcmp2(cmd, "NLST") == 0)
			{
				if(data_s > 0 || sconnect(inet_ntoa(p.remote_adr), 20, &data_s) == 0)
				{
					Lv2FsFile fd;
					if(sysFsOpendir(cwd, &fd) == 0)
					{
						Lv2FsDirent entry;
						u64 read;
						
						ssend(conn_s, "150 Accepted data connection\r\n");
						
						while(sysFsReaddir(fd, &entry, &read) == 0 && read > 0)
						{
							bytes = sprintf(buffer, "%s\r\n", entry.d_name);
							send(data_s, buffer, bytes, 0);
						}
						
						ssend(conn_s, "226 Transfer complete\r\n");
					}
					else
					{
						ssend(conn_s, "550 Cannot access directory\r\n");
					}
					
					sysFsClosedir(fd);
				}
				else
				{
					ssend(conn_s, "425 No data connection\r\n");
				}
				
				dataactive = 0;
			}
			else
			if(strcmp2(cmd, "STOR") == 0 || strcmp2(cmd, "APPE") == 0)
			{
				if(data_s > 0 || sconnect(inet_ntoa(p.remote_adr), 20, &data_s) == 0)
				{
					if(split == 1)
					{
						abspath(param, cwd, path);
						
						Lv2FsFile fd;
						if(sysFsOpen(path, LV2_O_WRONLY | LV2_O_CREAT | (strcmp2(cmd, "APPE") == 0 ? LV2_O_APPEND : (rest == 0 ? LV2_O_TRUNC : 0)), &fd, NULL, 0) == 0)
						{
							ssend(conn_s, "150 Accepted data connection\r\n");
							
							if(sdtofd(fd, data_s, (strcmp2(cmd, "APPE") == 0 ? 0 : rest)) == 0)
							{
								ssend(conn_s, "226 Transfer complete\r\n");
							}
							else
							{
								ssend(conn_s, "451 Transfer failed\r\n");
							}
						}
						else
						{
							ssend(conn_s, "550 Cannot open file\r\n");
						}
						
						sysFsClose(fd);
						
						if(strcmp2(cmd, "STOR") == 0)
						{
							sysFsChmod(path, 0644);
						}
					}
					else
					{
						ssend(conn_s, "501 No filename specified\r\n");
					}
				}
				else
				{
					ssend(conn_s, "425 No data connection\r\n");
				}
				
				dataactive = 0;
			}
			else
			if(strcmp2(cmd, "RETR") == 0)
			{
				if(data_s > 0 || sconnect(inet_ntoa(p.remote_adr), 20, &data_s) == 0)
				{
					if(split == 1)
					{
						abspath(param, cwd, path);
						
						Lv2FsFile fd;
						if(sysFsOpen(path, LV2_O_RDONLY, &fd, NULL, 0) == 0)
						{
							ssend(conn_s, "150 Accepted data connection\r\n");
							
							if(fdtosd(data_s, fd, rest) == 0)
							{
								ssend(conn_s, "226 Transfer complete\r\n");
							}
							else
							{
								ssend(conn_s, "451 Transfer failed\r\n");
							}
						}
						else
						{
							ssend(conn_s, "550 Cannot open file\r\n");
						}
						
						sysFsClose(fd);
					}
					else
					{
						ssend(conn_s, "501 No filename specified\r\n");
					}
				}
				else
				{
					ssend(conn_s, "425 No data connection\r\n");
				}
				
				dataactive = 0;
			}
			else
			if(strcmp2(cmd, "TYPE") == 0)
			{
				ssend(conn_s, "200 TYPE command successful\r\n");
			}
			else
			if(strcmp2(cmd, "STRU") == 0)
			{
				if(strcmp2(param, "F") == 0)
				{
					ssend(conn_s, "200 STRU command successful\r\n");
				}
				else
				{
					ssend(conn_s, "504 STRU command failed\r\n");
				}
			}
			else
			if(strcmp2(cmd, "MODE") == 0)
			{
				if(strcmp2(param, "S") == 0)
				{
					ssend(conn_s, "200 MODE command successful\r\n");
				}
				else
				{
					ssend(conn_s, "504 STRU command failed\r\n");
				}
			}
			else
			if(strcmp2(cmd, "REST") == 0)
			{
				if(split == 1)
				{
					long long temprest = atoll(param);
					
					if(temprest >= 0)
					{
						rest = temprest;
						ssend(conn_s, "350 REST command successful\r\n");
					}
					else
					{
						ssend(conn_s, "501 Invalid restart point\r\n");
					}
				}
				else
				{
					ssend(conn_s, "501 No restart point\r\n");
				}
			}
			else
			if(strcmp2(cmd, "DELE") == 0)
			{
				if(split == 1)
				{
					abspath(param, cwd, path);
					
					if(sysFsUnlink(path) == 0)
					{
						ssend(conn_s, "250 File successfully deleted\r\n");
					}
					else
					{
						ssend(conn_s, "550 Cannot delete file\r\n");
					}
				}
				else
				{
					ssend(conn_s, "501 No filename specified\r\n");
				}
			}
			else
			if(strcmp2(cmd, "RNFR") == 0)
			{
				if(split == 1)
				{
					abspath(param, cwd, path);
					
					if(exists(path) == 0)
					{
						strcpy(rnfr, path);
						ssend(conn_s, "350 RNFR accepted - ready for destination\r\n");
					}
					else
					{
						ssend(conn_s, "550 RNFR failed - file does not exist\r\n");
					}
				}
				else
				{
					ssend(conn_s, "501 No file specified\r\n");
				}
			}
			else
			if(strcmp2(cmd, "RNTO") == 0)
			{
				if(split == 1)
				{
					abspath(param, cwd, path);
					
					if(rename(rnfr, path) == 0)
					{
						ssend(conn_s, "250 File was successfully renamed or moved\r\n");
					}
					else
					{
						ssend(conn_s, "550 Cannot rename or move file\r\n");
					}
				}
				else
				{
					ssend(conn_s, "501 No file specified\r\n");
				}
			}
			else
			if(strcmp2(cmd, "SITE") == 0)
			{
				char param2[256];
				split = ssplit(param, cmd, 15, param2, 255);
				
				strtoupper(cmd);
				
				if(strcmp2(cmd, "CHMOD") == 0)
				{
					char param3[4], filename[256];
					split = ssplit(param2, param3, 3, filename, 255);
					
					if(split == 1)
					{
						char perms[5];
						sprintf(perms, "0%s", param3);
						
						abspath(filename, cwd, path);
						
						if(sysFsChmod(path, strtol(perms, NULL, 8)) == 0)
						{
							ssend(conn_s, "250 File permissions successfully set\r\n");
						}
						else
						{
							ssend(conn_s, "550 Cannot set file permissions\r\n");
						}
					}
					else
					{
						ssend(conn_s, "501 No filename specified\r\n");
					}
				}
				else
				if(strcmp2(cmd, "PASSWD") == 0)
				{
					if(split == 1)
					{
						Lv2FsFile fd;
						if(sysFsOpen(PASSWD_FILE, LV2_O_WRONLY | LV2_O_CREAT | LV2_O_TRUNC, &fd, NULL, 0) == 0)
						{
							u64 written;
							strcpy(userpass, param2);
							sysFsWrite(fd, param2, strlen(param2), &written);
							
							ssend(conn_s, "200 Password successfully set\r\n");
						}
						
						sysFsClose(fd);
						sysFsChmod(PASSWD_FILE, 0660);
					}
					else
					{
						ssend(conn_s, "501 No password given\r\n");
					}
				}
				else
				if(strcmp2(cmd, "EXITAPP") == 0)
				{
					exitapp = 1;
					ssend(conn_s, "221 Exiting...\r\n");
					break;
				}
				else
				if(strcmp2(cmd, "HELP") == 0)
				{
					ssend(conn_s, "214-Special commands:\r\n");
					ssend(conn_s, " SITE PASSWD <newpassword> - Change your password\r\n");
					ssend(conn_s, " SITE EXITAPP - Remote quit\r\n");
					ssend(conn_s, " SITE HELP - Show this message\r\n");
					ssend(conn_s, "214 End\r\n");
				}
				else
				{
					ssend(conn_s, "500 Unrecognized SITE command\r\n");
				}
			}
			else
			if(strcmp2(cmd, "SIZE") == 0)
			{
				if(split == 1)
				{
					abspath(param, cwd, path);
					
					Lv2FsStat buf;
					if(sysFsStat(path, &buf) == 0)
					{
						bytes = sprintf(buffer, "213 %llu\r\n", (unsigned long long)buf.st_size);
						send(conn_s, buffer, bytes, 0);
					}
					else
					{
						ssend(conn_s, "550 File does not exist\r\n");
					}
				}
				else
				{
					ssend(conn_s, "501 No file specified\r\n");
				}
			}
			else
			if(strcmp2(cmd, "MDTM") == 0)
			{
				if(split == 1)
				{
					abspath(param, cwd, path);
					
					Lv2FsStat buf;
					if(sysFsStat(path, &buf) == 0)
					{
						char tstr[21];
						strftime(tstr, 20, "213 %Y%m%d%H%M%S\r\n", localtime(&buf.st_mtime));
						send(conn_s, tstr, 20, 0);
					}
					else
					{
						ssend(conn_s, "550 File does not exist\r\n");
					}
				}
				else
				{
					ssend(conn_s, "501 No file specified\r\n");
				}
			}
			else
			if(strcmp2(cmd, "ALLO") == 0)
			{
				ssend(conn_s, "202 ALLO command successful\r\n");
			}
			else
			if(strcmp2(cmd, "USER") == 0 || strcmp2(cmd, "PASS") == 0)
			{
				ssend(conn_s, "230 You are already logged in\r\n");
			}
			else
			{
				ssend(conn_s, "500 Unrecognized command\r\n");
			}
			
			if(dataactive == 0)
			{
				sclose(&data_s);
				dataactive = -1;
			}
		}
		else
		{
			if(strcmp2(cmd, "USER") == 0)
			{
				if(split == 1)
				{
					strcpy(user, param);
					bytes = sprintf(buffer, "331 User %s OK. Password required\r\n", user);
					send(conn_s, buffer, bytes, 0);
				}
				else
				{
					ssend(conn_s, "501 Insufficient parameters\r\n");
				}
			}
			else
			if(strcmp2(cmd, "PASS") == 0)
			{
				if(split == 1)
				{
					#if DISABLE_PASS == 0
					if(strcmp2(user, DEFAULT_USER) == 0 && strcmp2(param, userpass) == 0)
					{
					#endif
						loggedin = 1;
						ssend(conn_s, "230 Login successful\r\n");
					#if DISABLE_PASS == 0
					}
					else
					{
						ssend(conn_s, "530 Invalid username or password\r\n");
					}
					#endif
				}
				else
				{
					ssend(conn_s, "501 Insufficient parameters\r\n");
				}
			}
			else
			{
				ssend(conn_s, "530 Not logged in\r\n");
			}
		}
	}
	
	sclose(&conn_s);
	sclose(&data_s);
	sys_ppu_thread_exit(0);	
}

void opf_connectionhandler(u64 arg)
{
	union net_ctl_info info;
	
	strcpy(statustext, "No Network Connection");
	while(netCtlGetInfo(NET_CTL_INFO_IP_ADDRESS, &info) < 0 && exitapp == 0)
	{
		usleep(100000);
	}
	
	int list_s = slisten(LISTEN_PORT, 5);
	
	if(list_s > 0)
	{
		u64 conn_s;
		sys_ppu_thread_t id;
		
		sprintf(statustext, "FTP Server Active - IP: %s Port: %i", info.ip_address, LISTEN_PORT);
		
		while(exitapp == 0)
		{
			if((conn_s = (u64)accept(list_s, NULL, NULL)) > 0)
			{
				sys_ppu_thread_create(&id, opf_clienthandler, conn_s, 1500, 0x2000, 0, "FTP Client");
			}
		}
		
		sclose(&list_s);
	}
	
	sys_ppu_thread_exit(0);
}

void opf_screensaver(u64 arg)
{
	u64 sec, nsec, sec_old, nsec_old;
	lv2GetCurrentTime(&sec_old, &nsec_old);
	
	while(exitapp == 0)
	{
		sys_ppu_thread_yield();
		
		switch(ssactive)
		{
			case 0:
				lv2GetCurrentTime(&sec, &nsec);
				
				if((sec - sec_old) >= 60)
				{
					ssactive = 1;
					drawstate = 0;
				}
			break;
			case 2:
				if((sec - sec_old) >= 60)
				{
					ssactive = 0;
					drawstate = 0;
				}
				
				lv2GetCurrentTime(&sec_old, &nsec_old);
			break;
		}
		
		usleep(100000);
	}
	
	sys_ppu_thread_exit(0);
}

int main()
{
	netInitialize();
	netCtlInit();
	
	sys_ppu_thread_t id;
	sys_ppu_thread_create(&id, opf_connectionhandler, 0, 1500, 0x400, 0, "FTP Server");
	
	sysRegisterCallback(EVENT_SLOT0, sysevent_callback, NULL);
	
	#if DISABLE_PASS == 0
	Lv2FsFile fd;
	if(sysFsOpen(PASSWD_FILE, LV2_O_RDONLY, &fd, NULL, 0) == 0)
	{
		u64 read;
		sysFsRead(fd, userpass, 63, &read);
	}
	else
	{
		strcpy(userpass, DEFAULT_PASS);
	}
	
	sysFsClose(fd);
	#endif
	
	init_screen();
	ioPadInit(7);
	sconsoleInit(FONT_COLOR_BLACK, FONT_COLOR_WHITE, res.width, res.height);
	
	PadInfo padinfo;
	PadData paddata;
	int i;
	
	char toptext[128];
	sprintf(toptext, "%s v%s by %s", TITLE, VERSION, AUTHOR);
	
	int wf_mnt = (
		exists("/dev_blind") == 0 ||
		exists("/dev_rwflash") == 0 || 
		exists("/dev_fflash") == 0 || 
		exists("/dev_Alejandro") == 0 ||
		exists("/dev_dragon") == 0
		);
	
	sys_ppu_thread_create(&id, opf_screensaver, 0, 1500, 0x400, 0, "Screen Saver");
	
	while(exitapp == 0)
	{
		sysCheckCallback();
		ioPadGetInfo(&padinfo);
		
		for(i = 0; i < MAX_PADS; i++)
		{
			if(padinfo.status[i])
			{
				ioPadGetData(i, &paddata);
				
				if(paddata.BTN_LEFT || paddata.BTN_UP || paddata.BTN_RIGHT || paddata.BTN_DOWN
				|| paddata.BTN_CROSS || paddata.BTN_SQUARE || paddata.BTN_CIRCLE || paddata.BTN_TRIANGLE)
				{
					ssactive = 2;
					break;
				}
				
				if(paddata.BTN_SELECT && paddata.BTN_START)
				{
					exitapp = 1;
					break;
				}
			}
		}
		
		if(drawstate == 0)
		{
			s32 buffer_size = 4 * res.width * res.height;
			
			for(i = 0; i < 2; i++)
			{
				memset(buffers[i]->ptr, 0, buffer_size);
				
				if(ssactive != 1)
				{
					print(50, 50, toptext, buffers[i]->ptr);
					print(50, 90, statustext, buffers[i]->ptr);
					
					print(50, 150, "Like this homebrew? Support the developer: http://bit.ly/gB8CJo", buffers[i]->ptr);
					print(50, 190, "Follow @dashhacks on Twitter and win free stuff!", buffers[i]->ptr);
					print(50, 230, "Visit my homebrew site: http://jjolano.ps3crunch.com", buffers[i]->ptr);
					
					print(50, 280, "Press SELECT + START to exit.", buffers[i]->ptr);
					
					if(wf_mnt)
					{
						print(50, 340, "Warning: writable dev_flash mount detected.", buffers[i]->ptr);
					}
				}
			}
			
			drawstate = -1;
		}
		
		waitFlip();
		flip(currentBuffer);
		currentBuffer = !currentBuffer;
	}
	
	netDeinitialize();
	return 0;
}
