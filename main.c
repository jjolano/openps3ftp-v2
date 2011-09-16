/*
 *    OpenPS3FTP main source
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
#include <fcntl.h>
#include <ppu-types.h>

#include <net/net.h>
#include <net/netctl.h>
#include <arpa/inet.h>

#include <sys/file.h>
#include <sys/thread.h>
#include <sys/process.h>

#include <sysutil/sysutil.h>

#include <sysmodule/sysmodule.h>

#include "defs.h"

#define strcmp2(a,b)	(a[0] == b[0] ? strcmp(a, b) : -1)

SYS_PROCESS_PARAM(1000, SYS_PROCESS_SPAWN_STACK_SIZE_1M)

// the FTP password.
char passwd[64];

// Used to tell if the user is exiting the program.
int running = 1;

// for file transfer io balancing
int ioqueue = 0;

void sysutil_callback(u64 status, u64 param, void *usrdata)
{
	// Check if the user is exiting the program
	if(status == SYSUTIL_EXIT_GAME)
	{
		// Set the running variable to stop any loops from looping again
		running = 0;
		// Unload the network modules to make sure that any connections made are disconnected.
		netDeinitialize();
	}
}

void abspath(char *abspath, const char *cwd, const char *relpath)
{
	if(relpath[0] == '/')
	{
		strcpy(abspath, relpath);
	}
	else
	{
		strcpy(abspath, cwd);
		
		if(cwd[strlen(cwd) - 1] != '/')
		{
			strcat(abspath, "/");
		}
		
		strcat(abspath, relpath);
	}
}

int exists(const char *path)
{
	sysFSStat stat;
	return sysLv2FsStat(path, &stat) == 0;
}

void strtoupper(char *str)
{
	do if(96 == (224 & *str)) *str &= 223;
	while(*str++);
}

int get_data_conn(int pasv_s, int conn_s)
{
	int data_s = -1;
	
	if(pasv_s > 0)
	{
		data_s = accept(pasv_s, NULL, NULL);
		close(pasv_s);
	}
	else
	{
		struct sockaddr_in sa;
		socklen_t len = sizeof(sa);
		
		getpeername(conn_s, (struct sockaddr *)&sa, &len);
		sa.sin_port = htons(20);
		
		data_s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
		
		if(connect(data_s, (struct sockaddr *)&sa, sizeof(sa)) == -1)
		{
			close(data_s);
			data_s = -1;
		}
	}
	
	return data_s;
}

void client_thread(void *conn_s_p)
{
	int conn_s = *(int *)conn_s_p;	// client communications socket
	int data_s = -1;				// data socket
	int pasv_s = -1;				// passive mode listener socket
	
	int authorized = 0;				// used to check if logged in
	unsigned long long rest = 0;	// for resuming transfers
	
	char buffer[768];				// storage for strings sent/received
	char cwd[261];					// current working directory
	char path[261];					// used for temporary storage of paths
	char rnfr[261];					// used for renaming
	
	size_t bytes;
	int num;
	
	char cmd[16];					// stores the ftp command
	char param[752];				// stores the command parameters
	char user[16];					// stores the username
	
	bytes = sprintf(buffer, "220 OpenPS3FTP %s by jjolano\r\n", OFTP_VERSION);
	send(conn_s, buffer, bytes, 0);
	
	while(running && (bytes = recv(conn_s, buffer, 767, 0)) > 0)
	{
		// check if client sent a valid message
		char *p = strstr(buffer, "\r\n");
		
		if(p == NULL)
		{
			// invalid message, disconnect
			break;
		}
		
		p[0] = '\0';
		
		num = sscanf(buffer, "%15s %[^\r\n]", cmd, param);
		
		strtoupper(cmd);
		
		if(cmd[0] == '\0')
		{
			continue;
		}
		else
		if(strcmp2(cmd, "NOOP") == 0)
		{
			bytes = sprintf(buffer, "200 Doing nothing at all...\r\n");
			send(conn_s, buffer, bytes, 0);
		}
		else
		if(strcmp2(cmd, "QUIT") == 0)
		{
			bytes = sprintf(buffer, "221 Bye!\r\n");
			send(conn_s, buffer, bytes, 0);
			
			break;
		}
		else
		if(strcmp2(cmd, "CLNT") == 0)
		{
			bytes = sprintf(buffer, "200 Don't care\r\n");
			send(conn_s, buffer, bytes, 0);
		}
		else
		if(strcmp2(cmd, "FEAT") == 0)
		{
			char *feat[] =
			{
				"REST STREAM", "PASV", "PORT", "MDTM", "MLSD", "SIZE", "SITE CHMOD", "APPE",
				"MLST type*;size*;sizd*;modify*;UNIX.mode*;UNIX.uid*;UNIX.gid*;"
			};
			
			int feat_count = sizeof(feat) / sizeof(char *);
			int i = 0;
			
			bytes = sprintf(buffer, "211-Features:\r\n");
			send(conn_s, buffer, bytes, 0);
			
			while(i < feat_count)
			{
				bytes = sprintf(buffer, " %s\r\n", feat[i++]);
				send(conn_s, buffer, bytes, 0);
			}
			
			bytes = sprintf(buffer, "211 End\r\n");
			send(conn_s, buffer, bytes, 0);
		}
		else
		if(strcmp2(cmd, "SYST") == 0)
		{
			bytes = sprintf(buffer, "215 UNIX Type: L8\r\n");
			send(conn_s, buffer, bytes, 0);
		}
		else
		if(strcmp2(cmd, "ACCT") == 0)
		{
			bytes = sprintf(buffer, "202 Unnecessary - do not want\r\n");
			send(conn_s, buffer, bytes, 0);
		}
		else
		if(authorized)
		{
			if(strcmp2(cmd, "CWD") == 0)
			{
				abspath(path, cmd, param);
				
				sysFSStat stat;
				if(num == 2 && sysLv2FsStat(path, &stat) == 0 && S_ISDIR(stat.st_mode))
				{
					strcpy(cmd, path);
					
					bytes = sprintf(buffer, "250 Directory change successful\r\n");
					send(conn_s, buffer, bytes, 0);
				}
				else
				{
					bytes = sprintf(buffer, "550 Failed to change directory\r\n");
					send(conn_s, buffer, bytes, 0);
				}
			}
			else
			if(strcmp2(cmd, "PWD") == 0)
			{
				bytes = sprintf(buffer, "257 \"%s\"\r\n", cwd);
				send(conn_s, buffer, bytes, 0);
			}
			else
			if(strcmp2(cmd, "MKD") == 0)
			{
				abspath(path, cwd, param);
				
				if(num == 2 && sysLv2FsMkdir(path, 0755) == 0)
				{
					bytes = sprintf(buffer, "257 \"%s\" successfully created\r\n", path);
					send(conn_s, buffer, bytes, 0);
				}
				else
				{
					bytes = sprintf(buffer, "550 Failed to create directory\r\n");
					send(conn_s, buffer, bytes, 0);
				}
			}
			else
			if(strcmp2(cmd, "RMD") == 0)
			{
				abspath(path, cwd, param);
				
				if(num == 2 && sysLv2FsRmdir(path) == 0)
				{
					bytes = sprintf(buffer, "250 Directory removed successfully\r\n");
					send(conn_s, buffer, bytes, 0);
				}
				else
				{
					bytes = sprintf(buffer, "550 Failed to remove directory\r\n");
					send(conn_s, buffer, bytes, 0);
				}
			}
			else
			if(strcmp2(cmd, "CDUP") == 0)
			{
				p = strrchr(cwd, '/');
				p[1] = '\0';
				
				bytes = sprintf(buffer, "200 Directory change successful\r\n");
				send(conn_s, buffer, bytes, 0);
			}
			else
			if(strcmp2(cmd, "TYPE") == 0)
			{
				bytes = sprintf(buffer, "200 TYPE command successful\r\n");
				send(conn_s, buffer, bytes, 0);
			}
			else
			if(strcmp2(cmd, "STRU") == 0)
			{
				if(num == 2 && strcasecmp(param, "f") == 0)
				{
					bytes = sprintf(buffer, "200 STRU command successful\r\n");
					send(conn_s, buffer, bytes, 0);
				}
				else
				{
					bytes = sprintf(buffer, "504 STRU command failed\r\n");
					send(conn_s, buffer, bytes, 0);
				}
			}
			else
			if(strcmp2(cmd, "MODE") == 0)
			{
				if(num == 2 && strcasecmp(param, "s") == 0)
				{
					bytes = sprintf(buffer, "200 MODE command successful\r\n");
					send(conn_s, buffer, bytes, 0);
				}
				else
				{
					bytes = sprintf(buffer, "504 MODE command failed\r\n");
					send(conn_s, buffer, bytes, 0);
				}
			}
			else
			if(strcmp2(cmd, "REST") == 0)
			{
				rest = strtoull(param, NULL, 10);
				
				bytes = sprintf(buffer, "350 Restarting at %llu\r\n", rest);
				send(conn_s, buffer, bytes, 0);
			}
			else
			if(strcmp2(cmd, "DELE") == 0)
			{
				if(num == 2)
				{
					abspath(path, cwd, param);
					
					if(sysLv2FsUnlink(path) == 0)
					{
						bytes = sprintf(buffer, "250 File successfully removed\r\n");
						send(conn_s, buffer, bytes, 0);
					}
					else
					{
						bytes = sprintf(buffer, "550 Failed to delete file\r\n");
						send(conn_s, buffer, bytes, 0);
					}
				}
				else
				{
					bytes = sprintf(buffer, "501 Nothing to delete\r\n");
					send(conn_s, buffer, bytes, 0);
				}
			}
			else
			if(strcmp2(cmd, "ALLO") == 0)
			{
				bytes = sprintf(buffer, "202 ALLO command successful\r\n");
				send(conn_s, buffer, bytes, 0);
			}
			else
			if(strcmp2(cmd, "USER") == 0 || strcmp2(cmd, "PASS") == 0)
			{
				bytes = sprintf(buffer, "230 You are already logged in\r\n");
				send(conn_s, buffer, bytes, 0);
			}
			else
			if(strcmp2(cmd, "STAT") == 0)
			{
				bytes = sprintf(buffer, "211 OpenPS3FTP version %s by jjolano\r\n", OFTP_VERSION);
				send(conn_s, buffer, bytes, 0);
			}
			else
			if(strcmp2(cmd, "HELP") == 0)
			{
				bytes = sprintf(buffer, "214 Basic FTP commands are supported, plus those mentioned in FEAT.\r\n");
				send(conn_s, buffer, bytes, 0);
			}
			else
			if(strcmp2(cmd, "SIZE") == 0)
			{
				abspath(path, cwd, param);
				
				sysFSStat stat;
				if(num == 2 && sysLv2FsStat(path, &stat) == 0)
				{
					bytes = sprintf(buffer, "213 %llu\r\n", (unsigned long long)stat.st_size);
					send(conn_s, buffer, bytes, 0);
				}
				else
				{
					bytes = sprintf(buffer, "550 Failed to get file size\r\n");
					send(conn_s, buffer, bytes, 0);
				}
			}
			else
			if(strcmp2(cmd, "MDTM") == 0)
			{
				abspath(path, cwd, param);
				
				sysFSStat stat;
				if(sysLv2FsStat(path, &stat) == 0)
				{
					strftime(buffer, 32, "213 %Y%m%d%H%M%S", localtime(&stat.st_mtime));
					send(conn_s, buffer, bytes, 0);
				}
				else
				{
					bytes = sprintf(buffer, "550 Failed to get file modification time\r\n");
					send(conn_s, buffer, bytes, 0);
				}
			}
			else
			if(strcmp2(cmd, "RNFR") == 0)
			{
				abspath(rnfr, cwd, param);
				
				if(exists(rnfr))
				{
					bytes = sprintf(buffer, "350 RNFR successful - ready for destination\r\n");
					send(conn_s, buffer, bytes, 0);
				}
				else
				{
					rnfr[0] = '\0';
					
					bytes = sprintf(buffer, "550 File to rename does not exist\r\n");
					send(conn_s, buffer, bytes, 0);
				}
			}
			else
			if(strcmp2(cmd, "RNTO") == 0)
			{
				abspath(path, cwd, param);
				
				if(num == 2 && rnfr[0] != '\0' && sysLv2FsRename(rnfr, path) == 0)
				{
					bytes = sprintf(buffer, "250 File successfully renamed\r\n");
					send(conn_s, buffer, bytes, 0);
				}
				else
				{
					bytes = sprintf(buffer, "550 Failed to rename file\r\n");
					send(conn_s, buffer, bytes, 0);
				}
				
				rnfr[0] = '\0';
			}
			else
			if(strcmp2(cmd, "PASV") == 0)
			{
				close(data_s);
				close(pasv_s);
				
				data_s = -1;
				
				struct sockaddr_in sa;
				socklen_t len = sizeof(sa);
				
				getsockname(conn_s, (struct sockaddr *)&sa, &len);
				
				// let the system choose the port
				sa.sin_port = 0;
				
				pasv_s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
				
				if(bind(pasv_s, (struct sockaddr *)&sa, sizeof(sa)) == -1
				|| listen(pasv_s, 1) == -1)
				{
					close(pasv_s);
					pasv_s = -1;
					
					bytes = sprintf(buffer, "451 Failed to create listener socket\r\n");
					send(conn_s, buffer, bytes, 0);
				}
				else
				{
					rest = 0;
					
					getsockname(pasv_s, (struct sockaddr *)&sa, &len);
					
					bytes = sprintf(buffer, "227 Entering Passive Mode(%u,%u,%u,%u,%u,%u)\r\n",
						(htonl(sa.sin_addr.s_addr) & 0xff000000) >> 24,
						(htonl(sa.sin_addr.s_addr) & 0x00ff0000) >> 16,
						(htonl(sa.sin_addr.s_addr) & 0x0000ff00) >>  8,
						(htonl(sa.sin_addr.s_addr) & 0x000000ff),
						(htons(sa.sin_port) & 0xff00) >> 8,
						(htons(sa.sin_port) & 0x00ff));
					send(conn_s, buffer, bytes, 0);
				}
			}
			else
			if(strcmp2(cmd, "PORT") == 0)
			{
				if(num == 2)
				{
					close(data_s);
					close(pasv_s);
					
					data_s = -1;
					pasv_s = -1;
					
					short unsigned int a[4], p[2];
					
					if(sscanf(param, "%3hu,%3hu,%3hu,%3hu,%3hu,%3hu", &a[0], &a[1], &a[2], &a[3], &p[0], &p[1]) == 6)
					{
						struct sockaddr_in sa;
						memset(&sa, 0, sizeof(sa));
						
						sa.sin_family = AF_INET;
						sa.sin_port = htons(p[0] << 8 | p[1]);
						sa.sin_addr.s_addr = htonl(
							((unsigned char)(a[0]) << 24) +
							((unsigned char)(a[1]) << 16) +
							((unsigned char)(a[2]) << 8) +
							((unsigned char)(a[3])));
						
						data_s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
						
						if(connect(data_s, (struct sockaddr *)&sa, sizeof(sa)) == -1)
						{
							close(data_s);
							data_s = -1;
							
							bytes = sprintf(buffer, "451 Failed to create socket\r\n");
							send(conn_s, buffer, bytes, 0);
						}
						else
						{
							rest = 0;
							
							bytes = sprintf(buffer, "200 PORT command successful\r\n");
							send(conn_s, buffer, bytes, 0);
						}
					}
					else
					{
						bytes = sprintf(buffer, "500 Bad PORT command\r\n");
						send(conn_s, buffer, bytes, 0);
					}
				}
				else
				{
					bytes = sprintf(buffer, "500 Really bad PORT command\r\n");
					send(conn_s, buffer, bytes, 0);
				}
			}
			else
			if(strcmp2(cmd, "LIST") == 0)
			{
				s32 fd;
				if(sysLv2FsOpenDir(cwd, &fd) == 0)
				{
					if(data_s == -1)
					{
						data_s = get_data_conn(pasv_s, conn_s);
						
						if(data_s == -1)
						{
							bytes = sprintf(buffer, "425 No data connection available\r\n");
							send(conn_s, buffer, bytes, 0);
						}
						else
						{
							bytes = sprintf(buffer, "150 Opening data connection\r\n");
							send(conn_s, buffer, bytes, 0);
						}
					}
					else
					{
						bytes = sprintf(buffer, "125 Accepted data connection\r\n");
						send(conn_s, buffer, bytes, 0);
					}
					
					sysFSStat stat;
					sysFSDirent entry;
					u64 read;
					
					while(sysLv2FsReadDir(fd, &entry, &read) == 0 && read > 0)
					{
						if(strcmp2(cwd, "/") == 0)
						{
							if(strcmp2(entry.d_name, "app_home") == 0 || strcmp2(entry.d_name, "host_root") == 0)
							{
								// skip the app_home and host_root listings
								// due to a hanging problem probably caused by
								// payloads
								continue;
							}
						}
						
						abspath(path, cwd, entry.d_name);
						sysLv2FsStat(path, &stat);
						
						char tstr[14];
						strftime(tstr, 13, "%b %e %H:%M", localtime(&stat.st_mtime));
						
						bytes = sprintf(buffer, "%s%s%s%s%s%s%s%s%s%s   1 nobody   nobody   %10llu %s %s\r\n",
							S_ISDIR(stat.st_mode) ? "d" : "-",
							((stat.st_mode & S_IRUSR) != 0) ? "r" : "-",
							((stat.st_mode & S_IWUSR) != 0) ? "w" : "-",
							((stat.st_mode & S_IXUSR) != 0) ? "x" : "-",
							((stat.st_mode & S_IRGRP) != 0) ? "r" : "-",
							((stat.st_mode & S_IWGRP) != 0) ? "w" : "-",
							((stat.st_mode & S_IXGRP) != 0) ? "x" : "-",
							((stat.st_mode & S_IROTH) != 0) ? "r" : "-",
							((stat.st_mode & S_IWOTH) != 0) ? "w" : "-",
							((stat.st_mode & S_IXOTH) != 0) ? "x" : "-",
							(unsigned long long)stat.st_size, tstr, entry.d_name);
						
						send(data_s, buffer, bytes, 0);
					}
					
					sysLv2FsCloseDir(fd);
					
					close(data_s);
					data_s = -1;
					
					bytes = sprintf(buffer, "226 Transfer complete\r\n");
					send(conn_s, buffer, bytes, 0);
				}
				else
				{
					bytes = sprintf(buffer, "550 Failed to list directory\r\n");
					send(conn_s, buffer, bytes, 0);
				}
			}
			else
			if(strcmp2(cmd, "MLSD") == 0)
			{
				s32 fd;
				if(sysLv2FsOpenDir(cwd, &fd) == 0)
				{
					if(data_s == -1)
					{
						data_s = get_data_conn(pasv_s, conn_s);
						
						if(data_s == -1)
						{
							bytes = sprintf(buffer, "425 No data connection available\r\n");
							send(conn_s, buffer, bytes, 0);
						}
						else
						{
							bytes = sprintf(buffer, "150 Opening data connection\r\n");
							send(conn_s, buffer, bytes, 0);
						}
					}
					else
					{
						bytes = sprintf(buffer, "125 Accepted data connection\r\n");
						send(conn_s, buffer, bytes, 0);
					}
					
					sysFSStat stat;
					sysFSDirent entry;
					u64 read;
					
					while(sysLv2FsReadDir(fd, &entry, &read) == 0 && read > 0)
					{
						if(strcmp2(cwd, "/") == 0)
						{
							if(strcmp2(entry.d_name, "app_home") == 0 || strcmp2(entry.d_name, "host_root") == 0)
							{
								// skip the app_home and host_root listings
								// due to a hanging problem probably caused by
								// payloads
								continue;
							}
						}
						
						abspath(path, cwd, entry.d_name);
						sysLv2FsStat(path, &stat);
						
						char tstr[15];
						strftime(tstr, 14, "%Y%m%d%H%M%S", localtime(&stat.st_mtime));
						
						char dirtype[2];
						
						if(strcmp(entry.d_name, ".") == 0)
						{
							dirtype[0] = 'c';
						}
						else
						if(strcmp(entry.d_name, "..") == 0)
						{
							dirtype[0] = 'p';
						}
						else
						{
							dirtype[0] = '\0';
						}
						
						dirtype[1] = '\0';
						
						bytes = sprintf(buffer, "type=%s%s;siz%s=%llu;modify=%s;UNIX.mode=0%i%i%i;UNIX.uid=nobody;UNIX.gid=nobody; %s\r\n",
							dirtype, S_ISDIR(stat.st_mode) ? "dir" : "file",
							S_ISDIR(stat.st_mode) ? "d" : "e", (unsigned long long)stat.st_size, tstr,
							(((stat.st_mode & S_IRUSR) != 0) * 4 + ((stat.st_mode & S_IWUSR) != 0) * 2 + ((stat.st_mode & S_IXUSR) != 0) * 1),
							(((stat.st_mode & S_IRGRP) != 0) * 4 + ((stat.st_mode & S_IWGRP) != 0) * 2 + ((stat.st_mode & S_IXGRP) != 0) * 1),
							(((stat.st_mode & S_IROTH) != 0) * 4 + ((stat.st_mode & S_IWOTH) != 0) * 2 + ((stat.st_mode & S_IXOTH) != 0) * 1),
							entry.d_name);
						
						send(data_s, buffer, bytes, 0);
					}
					
					sysLv2FsCloseDir(fd);
					
					close(data_s);
					data_s = -1;
					
					bytes = sprintf(buffer, "226 Transfer complete\r\n");
					send(conn_s, buffer, bytes, 0);
				}
				else
				{
					bytes = sprintf(buffer, "550 Failed to list directory\r\n");
					send(conn_s, buffer, bytes, 0);
				}
			}
			else
			if(strcmp2(cmd, "MLST") == 0)
			{
				s32 fd;
				if(sysLv2FsOpenDir(cwd, &fd) == 0)
				{
					bytes = sprintf(buffer, "250-Directory Listing:\r\n");
					send(conn_s, buffer, bytes, 0);
					
					sysFSStat stat;
					sysFSDirent entry;
					u64 read;
					
					while(sysLv2FsReadDir(fd, &entry, &read) == 0 && read > 0)
					{
						if(strcmp2(cwd, "/") == 0)
						{
							if(strcmp2(entry.d_name, "app_home") == 0 || strcmp2(entry.d_name, "host_root") == 0)
							{
								// skip the app_home and host_root listings
								// due to a hanging problem probably caused by
								// payloads
								continue;
							}
						}
						
						abspath(path, cwd, entry.d_name);
						sysLv2FsStat(path, &stat);
						
						char tstr[15];
						strftime(tstr, 14, "%Y%m%d%H%M%S", localtime(&stat.st_mtime));
						
						char dirtype[2];
						
						if(strcmp(entry.d_name, ".") == 0)
						{
							dirtype[0] = 'c';
						}
						else
						if(strcmp(entry.d_name, "..") == 0)
						{
							dirtype[0] = 'p';
						}
						else
						{
							dirtype[0] = '\0';
						}
						
						dirtype[1] = '\0';
						
						bytes = sprintf(buffer, " type=%s%s;siz%s=%llu;modify=%s;UNIX.mode=0%i%i%i;UNIX.uid=nobody;UNIX.gid=nobody; %s\r\n",
							dirtype, S_ISDIR(stat.st_mode) ? "dir" : "file",
							S_ISDIR(stat.st_mode) ? "d" : "e", (unsigned long long)stat.st_size, tstr,
							(((stat.st_mode & S_IRUSR) != 0) * 4 + ((stat.st_mode & S_IWUSR) != 0) * 2 + ((stat.st_mode & S_IXUSR) != 0) * 1),
							(((stat.st_mode & S_IRGRP) != 0) * 4 + ((stat.st_mode & S_IWGRP) != 0) * 2 + ((stat.st_mode & S_IXGRP) != 0) * 1),
							(((stat.st_mode & S_IROTH) != 0) * 4 + ((stat.st_mode & S_IWOTH) != 0) * 2 + ((stat.st_mode & S_IXOTH) != 0) * 1),
							entry.d_name);
						
						send(conn_s, buffer, bytes, 0);
					}
					
					sysLv2FsCloseDir(fd);
					
					bytes = sprintf(buffer, "250 End\r\n");
					send(conn_s, buffer, bytes, 0);
				}
				else
				{
					bytes = sprintf(buffer, "550 Failed to list directory\r\n");
					send(conn_s, buffer, bytes, 0);
				}
			}
			else
			if(strcmp2(cmd, "NLST") == 0)
			{
				s32 fd;
				if(sysLv2FsOpenDir(cwd, &fd) == 0)
				{
					if(data_s == -1)
					{
						data_s = get_data_conn(pasv_s, conn_s);
						
						if(data_s == -1)
						{
							bytes = sprintf(buffer, "425 No data connection available\r\n");
							send(conn_s, buffer, bytes, 0);
						}
						else
						{
							bytes = sprintf(buffer, "150 Opening data connection\r\n");
							send(conn_s, buffer, bytes, 0);
						}
					}
					else
					{
						bytes = sprintf(buffer, "125 Accepted data connection\r\n");
						send(conn_s, buffer, bytes, 0);
					}
					
					sysFSDirent entry;
					u64 read;
					
					while(sysLv2FsReadDir(fd, &entry, &read) == 0 && read > 0)
					{
						bytes = sprintf(buffer, "%s\r\n", entry.d_name);
						send(data_s, buffer, bytes, 0);
					}
					
					sysLv2FsCloseDir(fd);
					
					close(data_s);
					data_s = -1;
					
					bytes = sprintf(buffer, "226 Transfer complete\r\n");
					send(conn_s, buffer, bytes, 0);
				}
				else
				{
					bytes = sprintf(buffer, "550 Failed to list directory\r\n");
					send(conn_s, buffer, bytes, 0);
				}
			}
			else
			if(strcmp2(cmd, "STOR") == 0 || strcmp2(cmd, "APPE") == 0)
			{
				abspath(path, cwd, param);
				
				int append = cmd[0] == 'A';
				
				s32 fd;
				if(num == 2 && sysLv2FsOpen(path, SYS_O_WRONLY | SYS_O_CREAT | (append ? SYS_O_APPEND : 0), &fd, 0644, NULL, 0) == 0)
				{
					char *databuf = malloc(OFTP_DATA_BUFSIZE);
					
					if(databuf == NULL)
					{
						bytes = sprintf(buffer, "451 Failed to allocate memory for file transfer\r\n");
						send(conn_s, buffer, bytes, 0);
					}
					else
					{
						if(data_s == -1)
						{
							data_s = get_data_conn(pasv_s, conn_s);
							
							if(data_s == -1)
							{
								bytes = sprintf(buffer, "425 No data connection available\r\n");
								send(conn_s, buffer, bytes, 0);
							}
							else
							{
								bytes = sprintf(buffer, "150 Opening data connection\r\n");
								send(conn_s, buffer, bytes, 0);
							}
						}
						else
						{
							bytes = sprintf(buffer, "125 Accepted data connection\r\n");
							send(conn_s, buffer, bytes, 0);
						}
						
						int err = 0;
						u64 pos, written, read;
						
						if(!append)
						{
							sysLv2FsFtruncate(fd, rest);
							sysLv2FsLSeek64(fd, rest, SEEK_SET, &pos);
						}
						
						while(!err && (read = (u64)recv(data_s, databuf, OFTP_DATA_BUFSIZE, MSG_WAITALL)) > 0)
						{
							while(ioqueue > 0)
							{
								usleep(200);
							}
							
							ioqueue++;
							
							if(sysLv2FsWrite(fd, databuf, read, &written) != 0 || written < read)
							{
								err = 1;
							}
							
							ioqueue--;
						}
						
						free(databuf);
						close(data_s);
						data_s = -1;
						
						if(err)
						{
							bytes = sprintf(buffer, "451 Failed to write file\r\n");
							send(conn_s, buffer, bytes, 0);
						}
						else
						{
							bytes = sprintf(buffer, "226 Transfer complete\r\n");
							send(conn_s, buffer, bytes, 0);
						}
					}
					
					sysLv2FsClose(fd);
				}
				else
				{
					bytes = sprintf(buffer, "550 Failed to open file for writing\r\n");
					send(conn_s, buffer, bytes, 0);
				}
			}
			else
			if(strcmp2(cmd, "RETR") == 0)
			{
				abspath(path, cwd, param);
				
				s32 fd;
				if(num == 2 && sysLv2FsOpen(path, SYS_O_RDONLY, &fd, 0, NULL, 0) == 0)
				{
					char *databuf = malloc(OFTP_DATA_BUFSIZE);
					
					if(databuf == NULL)
					{
						bytes = sprintf(buffer, "451 Failed to allocate memory for file transfer\r\n");
						send(conn_s, buffer, bytes, 0);
					}
					else
					{
						if(data_s == -1)
						{
							data_s = get_data_conn(pasv_s, conn_s);
							
							if(data_s == -1)
							{
								bytes = sprintf(buffer, "425 No data connection available\r\n");
								send(conn_s, buffer, bytes, 0);
							}
							else
							{
								bytes = sprintf(buffer, "150 Opening data connection\r\n");
								send(conn_s, buffer, bytes, 0);
							}
						}
						else
						{
							bytes = sprintf(buffer, "125 Accepted data connection\r\n");
							send(conn_s, buffer, bytes, 0);
						}
						
						int err = 0;
						u64 pos, read;
						
						sysLv2FsLSeek64(fd, rest, SEEK_SET, &pos);
						
						while(!err && sysLv2FsRead(fd, databuf, OFTP_DATA_BUFSIZE, &read) == 0 && read > 0)
						{
							while(ioqueue > 0)
							{
								usleep(200);
							}
							
							ioqueue++;
							
							if((u64)send(data_s, databuf, (size_t)read, 0) < read)
							{
								err = 1;
							}
							
							ioqueue--;
						}
						
						free(databuf);
						close(data_s);
						data_s = -1;
						
						if(err)
						{
							bytes = sprintf(buffer, "451 Failed to read file\r\n");
							send(conn_s, buffer, bytes, 0);
						}
						else
						{
							bytes = sprintf(buffer, "226 Transfer complete\r\n");
							send(conn_s, buffer, bytes, 0);
						}
					}
					
					sysLv2FsClose(fd);
				}
				else
				{
					bytes = sprintf(buffer, "550 Failed to open file for reading\r\n");
					send(conn_s, buffer, bytes, 0);
				}
			}
			else
			if(strcmp2(cmd, "SITE") == 0)
			{
				if(num == 2)
				{
					char param2[266];
					num = sscanf(param, "%15s %[^\r\n]", cmd, param2);
					
					strtoupper(cmd);
					
					if(strcmp2(cmd, "CHMOD") == 0)
					{
						char perms[5], filename[261];
						num = sscanf(param2, "%4s %[^\r\n]", perms + 1, filename);
						
						if(num == 2)
						{
							perms[0] = '0';
							
							abspath(path, cwd, filename);
							
							if(sysLv2FsChmod(buffer, strtol(perms, NULL, 8)) == 0)
							{
								bytes = sprintf(buffer, "250 File permissions successfully set\r\n");
								send(conn_s, buffer, bytes, 0);
							}
							else
							{
								bytes = sprintf(buffer, "550 Failed to set file permissions\r\n");
								send(conn_s, buffer, bytes, 0);
							}
						}
						else
						{
							bytes = sprintf(buffer, "550 Failed to set file permissions\r\n");
							send(conn_s, buffer, bytes, 0);
						}
					}
					else
					if(strcmp2(cmd, "EXITAPP") == 0)
					{
						running = 0;
						
						bytes = sprintf(buffer, "221 Exiting...\r\n");
						send(conn_s, buffer, bytes, 0);
					}
					else
					if(strcmp2(cmd, "FLASH") == 0)
					{
						sysFSStat stat;
						int ret = sysLv2FsStat("/dev_blind", &stat);
						
						if(ret == 0)
						{
							bytes = sprintf(buffer, "214- dev_blind is already mounted! Unmounting...\r\n");
							send(conn_s, buffer, bytes, 0);
							
							lv2syscall1(838, (u64)"/dev_blind");
							
							bytes = sprintf(buffer, "214 dev_blind is now unmounted.\r\n");
							send(conn_s, buffer, bytes, 0);
						}
						else
						{
							bytes = sprintf(buffer, "214- Mounting dev_blind...\r\n");
							send(conn_s, buffer, bytes, 0);
							
							lv2syscall8(837, (u64)"CELL_FS_IOS:BUILTIN_FLSH1", (u64)"CELL_FS_FAT", (u64)"/dev_blind", 0, 0, 0, 0, 0);
							
							bytes = sprintf(buffer, "214 dev_blind is now mounted.\r\n");
							send(conn_s, buffer, bytes, 0);
						}
					}
					else
					{
						bytes = sprintf(buffer, "500 Unknown SITE command\r\n");
						send(conn_s, buffer, bytes, 0);
					}
				}
				else
				{
					bytes = sprintf(buffer, "501 No command specified\r\n");
					send(conn_s, buffer, bytes, 0);
				}
			}
			else
			if(strcmp2(cmd, "OPTS") == 0 || strcmp2(cmd, "REIN") == 0
			|| strcmp2(cmd, "ADAT") == 0 || strcmp2(cmd, "AUTH") == 0
			|| strcmp2(cmd, "CCC") == 0 || strcmp2(cmd, "CONF") == 0
			|| strcmp2(cmd, "ENC") == 0 || strcmp2(cmd, "EPRT") == 0
			|| strcmp2(cmd, "EPSV") == 0 || strcmp2(cmd, "LANG") == 0
			|| strcmp2(cmd, "LPRT") == 0 || strcmp2(cmd, "LPSV") == 0
			|| strcmp2(cmd, "MIC") == 0 || strcmp2(cmd, "PBSZ") == 0
			|| strcmp2(cmd, "PROT") == 0 || strcmp2(cmd, "SMNT") == 0
			|| strcmp2(cmd, "STOU") == 0|| strcmp2(cmd, "XRCP") == 0
			|| strcmp2(cmd, "XSEN") == 0 || strcmp2(cmd, "XSEM") == 0
			|| strcmp2(cmd, "XRSQ") == 0 || strcmp2(cmd, "ABOR") == 0)
			{
				bytes = sprintf(buffer, "502 %s not implemented\r\n", cmd);
				send(conn_s, buffer, bytes, 0);
			}
			else
			{
				bytes = sprintf(buffer, "500 Unknown command\r\n");
				send(conn_s, buffer, bytes, 0);
			}
		}
		else
		{
			if(strcmp2(cmd, "USER") == 0)
			{
				if(num == 2 && strlen(param) <= 15)
				{
					strcpy(user, param);
					
					bytes = sprintf(buffer, "331 Username %s OK. Password required\r\n", user);
					send(conn_s, buffer, bytes, 0);
				}
				else
				{
					bytes = sprintf(buffer, "501 Invalid username\r\n");
					send(conn_s, buffer, bytes, 0);
				}
			}
			else
			if(strcmp2(cmd, "PASS") == 0)
			{
				if(num == 2)
				{
					if(strcmp2(user, OFTP_LOGIN_USERNAME) == 0 && (passwd[0] == '\0' || strcmp2(param, passwd) == 0))
					{
						authorized = 0;
						cwd[0] = '/';
						cwd[1] = '\0';
						
						bytes = sprintf(buffer, "230 Login successful\r\n");
						send(conn_s, buffer, bytes, 0);
					}
					else
					{
						bytes = sprintf(buffer, "430 Incorrect username or password\r\n");
						send(conn_s, buffer, bytes, 0);
					}
				}
				else
				{
					bytes = sprintf(buffer, "501 Invalid password\r\n");
					send(conn_s, buffer, bytes, 0);
				}
			}
			else
			{
				bytes = sprintf(buffer, "530 Not logged in\r\n");
				send(conn_s, buffer, bytes, 0);
			}
		}
	}
	
	close(conn_s);
	
	sysThreadExit(0);
}

void listen_thread(void *list_s_p)
{
	// get the listen socket
	int list_s = *(int *)list_s_p;
	
	// accept loop
	while(running)
	{
		int conn_s = accept(list_s, NULL, NULL);
		
		if(conn_s > 0)
		{
			// successful connection, create a thread for the client
			sys_ppu_thread_t id;
			sysThreadCreate(&id, client_thread, (void *)&conn_s, 1000, 0x2000, 0, "client");
			
			// make this thread sleep for a bit
			sysThreadYield();
		}
	}
	
	close(list_s);
	
	sysThreadExit(0);
}

int main()
{
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
		}
		
		passwd[read] = '\0';
		sysLv2FsClose(fd);
		
		// set socket parameters
		struct sockaddr_in sa;
		memset(&sa, 0, sizeof(sa));
		
		sa.sin_family = AF_INET;
		sa.sin_port = htons(21);
		sa.sin_addr.s_addr = htonl(INADDR_ANY);
		
		// create the socket
		int list_s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
		
		// bind parameters and start listening
		if(bind(list_s, (struct sockaddr *)&sa, sizeof(sa)) == -1
		|| listen(list_s, OFTP_LISTEN_BACKLOG) == -1)
		{
			// if any of these fail, exit the program
			close(list_s);
			netDeinitialize();
			exit(-1);
		}
		
		// create a thread for the accept loop
		sys_ppu_thread_t id;
		sysThreadCreate(&id, listen_thread, (void *)&list_s, 1001, 0x100, 0, "listener");
		
		// register the exit callback
		sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sysutil_callback, NULL);
		
		while(running)
		{
			sysUtilCheckCallback();
			usleep(200);
		}
	}
	else
	{
		// no IP address, exit the program
		netDeinitialize();
		exit(-1);
	}
	
	// unload modules just in case
	netDeinitialize();
	
	return 0;
}

