/*
 *    client.c: handles clients, queries, and data
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
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <fcntl.h>
#include <ppu-types.h>

#include <net/net.h>
#include <net/netctl.h>
#include <arpa/inet.h>

#include <sys/file.h>
#include <sys/thread.h>

#include "defines.h"
#include "client.h"
#include "functions.h"

void client_thread(void *conn_s_p)
{
	int conn_s = *(int *)conn_s_p;	// control connection socket
	int data_s = -1;		// data connection socket
	int pasv_s = -1;		// pasv listener socket
	
	int authorized = 0;		// 1 if successfully logged in
	long long rest = 0;		// for resuming transfers
	
	char temp[2048];		// temporary storage of strings
	char expectcmd[16];		// for commands like RNFR, USER, etc.
	char cwd[2048] = "/\0";		// current working directory
	
	size_t bytes;
	int itemp;
	
	char cmd[16];
	char param[2032];
	char user[16];
	char rnfr[2032];
	
	// "random" port number generator for passive mode
	srand(conn_s);
	int p1 = (rand() % 251) + 4;
	int p2 = rand() % 256;
	
	bytes = sprintf(temp, "220 OpenPS3FTP %s by jjolano\r\n", OFTP_VERSION);
	send(conn_s, temp, bytes, 0);
	
	while(appstate != 1 && (bytes = recv(conn_s, temp, 2047, 0)) > 0)
	{
		itemp = strpos(temp, '\r');
		
		/*
		char *p = strchr(temp, '\r');
		*p = '\0';
		p = strchr(temp, '\n');
		*p = '\0';
		*/
		
		// check if client sent a valid message
		if(itemp > -1)
		{
			temp[itemp] = '\0';
			temp[itemp + 1] = '\0';
		}
		else
		{
			break;
		}
		
		// get command and parameter
		itemp = strsplit(temp, cmd, 15, param, 2031);
		
		strtoupper(cmd);
		
		// check expected command
		if(!is_empty(expectcmd) && strcmp2(cmd, expectcmd) != 0)
		{
			cmd[0] = '\0';
			
			bytes = ftpresp(temp, 503, "Bad command sequence");
			send(conn_s, temp, bytes, 0);
		}
		
		expectcmd[0] = '\0';
		
		// parse commands
		if(is_empty(cmd))
		{
			continue;
		}
		else
		if(strcmp2(cmd, "NOOP") == 0)
		{
			bytes = ftpresp(temp, 200, "NOOP command successful");
			send(conn_s, temp, bytes, 0);
		}
		else
		if(strcmp2(cmd, "QUIT") == 0)
		{
			bytes = ftpresp(temp, 221, "Goodbye");
			send(conn_s, temp, bytes, 0);
			break;
		}
		else
		if(strcmp2(cmd, "CLNT") == 0)
		{
			bytes = ftpresp(temp, 200, "Cool story, bro");
			send(conn_s, temp, bytes, 0);
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
			
			bytes = sprintf(temp, "211-Features:\r\n");
			send(conn_s, temp, bytes, 0);
			
			for(; i < feat_count; i++)
			{
				bytes = sprintf(temp, " %s\r\n", feat[i]);
				send(conn_s, temp, bytes, 0);
			}
			
			bytes = ftpresp(temp, 211, "End");
			send(conn_s, temp, bytes, 0);
		}
		else
		if(strcmp2(cmd, "SYST") == 0)
		{
			bytes = ftpresp(temp, 215, "UNIX Type: L8");
			send(conn_s, temp, bytes, 0);
		}
		else
		if(strcmp2(cmd, "ACCT") == 0)
		{
			bytes = ftpresp(temp, 502, "Command not implemented");
			send(conn_s, temp, bytes, 0);
		}
		else
		if(authorized == 1)
		{
			// logged in
			if(strcmp2(cmd, "CWD") == 0 || strcmp2(cmd, "XCWD") == 0)
			{
				abspath(param, cwd, temp);
				
				if(is_dir(temp))
				{
					strcpy(cwd, temp);
					
					bytes = ftpresp(temp, 250, "Directory change successful");
				}
				else
				{
					bytes = ftpresp(temp, 550, "Cannot access directory");
				}
				
				send(conn_s, temp, bytes, 0);
			}
			else
			if(strcmp2(cmd, "PWD") == 0 || strcmp2(cmd, "XPWD") == 0)
			{
				bytes = sprintf(temp, "257 \"%s\" is the current directory\r\n", cwd);
				send(conn_s, temp, bytes, 0);
			}
			else
			if(strcmp2(cmd, "MKD") == 0 || strcmp2(cmd, "XMKD") == 0)
			{
				if(itemp == 1)
				{
					abspath(param, cwd, temp);
					
					if(sysLv2FsMkdir(temp, 0755) == 0)
					{
						bytes = sprintf(temp, "257 \"%s\" was successfully created\r\n", temp);
					}
					else
					{
						bytes = ftpresp(temp, 550, "Cannot create directory");
					}
				}
				
				send(conn_s, temp, bytes, 0);
			}
			else
			if(strcmp2(cmd, "RMD") == 0 || strcmp2(cmd, "XRMD") == 0)
			{
				if(itemp == 1)
				{
					abspath(param, cwd, temp);
					
					if(sysLv2FsRmdir(temp) == 0)
					{
						bytes = ftpresp(temp, 250, "Directory successfully removed");
					}
					else
					{
						bytes = ftpresp(temp, 550, "Cannot access directory");
					}
				}
				
				send(conn_s, temp, bytes, 0);
			}
			else
			if(strcmp2(cmd, "CDUP") == 0 || strcmp2(cmd, "XCUP") == 0)
			{
				int len = strlen(cwd) - 1;
				int c, i = len;
				
				for(; i > 0; i--)
				{
					c = cwd[i];
					cwd[i] = '\0';
					
					if(c == '/' && i < len)
					{
						break;
					}
				}
				
				bytes = ftpresp(temp, 200, "Directory change successful");
				send(conn_s, temp, bytes, 0);
			}
			else
			if(strcmp2(cmd, "PASV") == 0)
			{
				closesocket(data_s);
				closesocket(pasv_s);
				
				data_s = -1;
				
				rest = 0;
				
				struct sockaddr_in sa;
				memset(&sa, 0, sizeof(sa));
				
				sa.sin_family = AF_INET;
				sa.sin_port = htons(ftp_port(p1, p2));
				sa.sin_addr.s_addr = htonl(INADDR_ANY);
				
				pasv_s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
				
				if(bind(pasv_s, (struct sockaddr *)&sa, sizeof(sa)) == -1
				|| listen(pasv_s, 1) == -1)
				{
					closesocket(pasv_s);
					pasv_s = -1;
					
					bytes = ftpresp(temp, 451, "Failed to create PASV socket");
				}
				else
				{
					char pasv_ipaddr[16];
					strcpy(pasv_ipaddr, ipaddr);
					
					strreplace(pasv_ipaddr, '.', ',');
					
					bytes = sprintf(temp, "227 Entering Passive Mode (%s,%i,%i)\r\n", pasv_ipaddr, p1, p2);
				}
				
				send(conn_s, temp, bytes, 0);
			}
			else
			if(strcmp2(cmd, "PORT") == 0)
			{
				if(itemp == 1)
				{
					closesocket(data_s);
					closesocket(pasv_s);
					
					data_s = -1;
					pasv_s = -1;
					
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
						char port_ipaddr[16];
						sprintf(port_ipaddr, "%s.%s.%s.%s", data[0], data[1], data[2], data[3]);
						
						struct sockaddr_in sa;
						memset(&sa, 0, sizeof(sa));
						
						sa.sin_family = AF_INET;
						sa.sin_port = htons(ftp_port(atoi(data[4]), atoi(data[5])));
						sa.sin_addr.s_addr = inet_addr(port_ipaddr);
						
						data_s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
						
						if(connect(data_s, (struct sockaddr *)&sa, sizeof(sa)) == -1)
						{
							closesocket(data_s);
							data_s = -1;
							
							bytes = ftpresp(temp, 451, "Failed to create PORT socket");
						}
						else
						{
							bytes = ftpresp(temp, 200, "PORT command successful");
						}
					}
					else
					{
						bytes = ftpresp(temp, 501, "Invalid PORT connection information");
					}
				}
				else
				{
					bytes = ftpresp(temp, 501, "No PORT connection information");
				}
				
				send(conn_s, temp, bytes, 0);
			}
			else
			if(strcmp2(cmd, "ABOR") == 0)
			{
				closesocket(data_s);
				closesocket(pasv_s);
				
				data_s = -1;
				pasv_s = -1;
				
				bytes = ftpresp(temp, 225, "ABOR command successful");
				send(conn_s, temp, bytes, 0);
			}
			else
			if(strcmp2(cmd, "LIST") == 0)
			{
				s32 fd;
				
				if(sysLv2FsOpenDir(cwd, &fd) == 0)
				{
					if(data_s == -1)
					{
						if(pasv_s > 0)
						{
							// passive
							data_s = accept(pasv_s, NULL, NULL);
							
							closesocket(pasv_s);
							pasv_s = -1;
						}
						else
						{
							// legacy
							struct sockaddr_in sa;
							socklen_t len = sizeof(sa);
							
							getpeername(conn_s, (struct sockaddr *)&sa, &len);
							sa.sin_port = htons(20);
							
							data_s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
							
							if(connect(data_s, (struct sockaddr *)&sa, sizeof(sa)) == -1)
							{
								closesocket(data_s);
								data_s = -1;
							}
						}
					
						if(data_s == -1)
						{
							bytes = ftpresp(temp, 425, "No data connection");
							send(conn_s, temp, bytes, 0);
							
							continue;
						}
						else
						{
							bytes = ftpresp(temp, 150, "Opening data connection");
							send(conn_s, temp, bytes, 0);
						}
					}
					else
					{
						bytes = ftpresp(temp, 125, "Accepted data connection");
						send(conn_s, temp, bytes, 0);
					}
					
					sysFSStat stat;
					sysFSDirent entry;
					u64 read;
					
					while(sysLv2FsReadDir(fd, &entry, &read) == 0 && read > 0)
					{
						if(strcmp2(cwd, "/") == 0
						&& (strcmp2(entry.d_name, "app_home") == 0
						|| strcmp2(entry.d_name, "host_root") == 0))
						{
							continue;
						}
						
						abspath(entry.d_name, cwd, temp);
						sysLv2FsStat(temp, &stat);
						
						char tstr[14];
						strftime(tstr, 13, "%b %e %H:%M", localtime(&stat.st_mtime));
						
						bytes = sprintf(temp, "%s%s%s%s%s%s%s%s%s%s   1 nobody   nobody   %10llu %s %s\r\n",
							fis_dir(stat) ? "d" : "-", 
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
						
						send(data_s, temp, bytes, 0);
					}
					
					bytes = ftpresp(temp, 226, "Transfer complete");
				}
				else
				{
					bytes = ftpresp(temp, 550, "Cannot access directory");
				}
				
				sysLv2FsCloseDir(fd);
				
				send(conn_s, temp, bytes, 0);
				
				closesocket(data_s);
				data_s = -1;
			}
			else
			if(strcmp2(cmd, "MLSD") == 0)
			{
				s32 fd;
				
				if(sysLv2FsOpenDir(cwd, &fd) == 0)
				{
					if(data_s == -1)
					{
						if(pasv_s > 0)
						{
							// passive
							data_s = accept(pasv_s, NULL, NULL);
							
							closesocket(pasv_s);
							pasv_s = -1;
						}
						else
						{
							// legacy
							struct sockaddr_in sa;
							socklen_t len = sizeof(sa);
							
							getpeername(conn_s, (struct sockaddr *)&sa, &len);
							sa.sin_port = htons(20);
							
							data_s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
							
							if(connect(data_s, (struct sockaddr *)&sa, sizeof(sa)) == -1)
							{
								closesocket(data_s);
								data_s = -1;
							}
						}
						
						if(data_s == -1)
						{
							bytes = ftpresp(temp, 425, "No data connection");
							send(conn_s, temp, bytes, 0);
							
							continue;
						}
						else
						{
							bytes = ftpresp(temp, 150, "Opening data connection");
							send(conn_s, temp, bytes, 0);
						}
					}
					else
					{
						bytes = ftpresp(temp, 125, "Accepted data connection");
						send(conn_s, temp, bytes, 0);
					}
					
					sysFSStat stat;
					sysFSDirent entry;
					u64 read;
					
					while(sysLv2FsReadDir(fd, &entry, &read) == 0 && read > 0)
					{
						if(strcmp2(cwd, "/") == 0
						&& (strcmp2(entry.d_name, "app_home") == 0
						|| strcmp2(entry.d_name, "host_root") == 0))
						{
							continue;
						}
						
						abspath(entry.d_name, cwd, temp);
						sysLv2FsStat(temp, &stat);
						
						char tstr[15];
						strftime(tstr, 14, "%Y%m%d%H%M%S", localtime(&stat.st_mtime));
						
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
						
						bytes = sprintf(temp, "type=%s%s;siz%s=%llu;modify=%s;UNIX.mode=0%i%i%i;UNIX.uid=nobody;UNIX.gid=nobody; %s\r\n",
							dirtype, fis_dir(stat) ? "dir" : "file",
							fis_dir(stat) ? "d" : "e", (unsigned long long)stat.st_size, tstr,
							(((stat.st_mode & S_IRUSR) != 0) * 4 + ((stat.st_mode & S_IWUSR) != 0) * 2 + ((stat.st_mode & S_IXUSR) != 0) * 1),
							(((stat.st_mode & S_IRGRP) != 0) * 4 + ((stat.st_mode & S_IWGRP) != 0) * 2 + ((stat.st_mode & S_IXGRP) != 0) * 1),
							(((stat.st_mode & S_IROTH) != 0) * 4 + ((stat.st_mode & S_IWOTH) != 0) * 2 + ((stat.st_mode & S_IXOTH) != 0) * 1),
							entry.d_name);
						
						send(data_s, temp, bytes, 0);
					}
					
					bytes = ftpresp(temp, 226, "Transfer complete");
				}
				else
				{
					bytes = ftpresp(temp, 550, "Cannot access directory");
				}
				
				sysLv2FsCloseDir(fd);
				
				send(conn_s, temp, bytes, 0);
				
				closesocket(data_s);
				data_s = -1;
			}
			else
			if(strcmp2(cmd, "MLST") == 0)
			{
				s32 fd;
				
				if(sysLv2FsOpenDir(cwd, &fd) == 0)
				{
					bytes = sprintf(temp, "250-Directory Listing:\r\n");
					send(conn_s, temp, bytes, 0);
					
					sysFSStat stat;
					sysFSDirent entry;
					u64 read;
					
					while(sysLv2FsReadDir(fd, &entry, &read) == 0 && read > 0)
					{
						if(strcmp2(cwd, "/") == 0
						&& (strcmp2(entry.d_name, "app_home") == 0
						|| strcmp2(entry.d_name, "host_root") == 0))
						{
							continue;
						}
						
						abspath(entry.d_name, cwd, temp);
						sysLv2FsStat(temp, &stat);
						
						char tstr[15];
						strftime(tstr, 14, "%Y%m%d%H%M%S", localtime(&stat.st_mtime));
						
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
						
						bytes = sprintf(temp, " type=%s%s;siz%s=%llu;modify=%s;UNIX.mode=0%i%i%i;UNIX.uid=nobody;UNIX.gid=nobody; %s\r\n",
							dirtype, fis_dir(stat) ? "dir" : "file",
							fis_dir(stat) ? "d" : "e", (unsigned long long)stat.st_size, tstr,
							(((stat.st_mode & S_IRUSR) != 0) * 4 + ((stat.st_mode & S_IWUSR) != 0) * 2 + ((stat.st_mode & S_IXUSR) != 0) * 1),
							(((stat.st_mode & S_IRGRP) != 0) * 4 + ((stat.st_mode & S_IWGRP) != 0) * 2 + ((stat.st_mode & S_IXGRP) != 0) * 1),
							(((stat.st_mode & S_IROTH) != 0) * 4 + ((stat.st_mode & S_IWOTH) != 0) * 2 + ((stat.st_mode & S_IXOTH) != 0) * 1),
							entry.d_name);
						
						send(conn_s, temp, bytes, 0);
					}
					
					bytes = ftpresp(temp, 250, "End");
				}
				else
				{
					bytes = ftpresp(temp, 550, "Cannot access directory");
				}
				
				sysLv2FsCloseDir(fd);
				
				send(conn_s, temp, bytes, 0);
			}
			else
			if(strcmp2(cmd, "NLST") == 0)
			{
				s32 fd;
				
				if(sysLv2FsOpenDir(cwd, &fd) == 0)
				{
					if(data_s == -1)
					{
						if(pasv_s > 0)
						{
							// passive
							data_s = accept(pasv_s, NULL, NULL);
							
							closesocket(pasv_s);
							pasv_s = -1;
						}
						else
						{
							// legacy
							struct sockaddr_in sa;
							socklen_t len = sizeof(sa);
							
							getpeername(conn_s, (struct sockaddr *)&sa, &len);
							sa.sin_port = htons(20);
							
							data_s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
							
							if(connect(data_s, (struct sockaddr *)&sa, sizeof(sa)) == -1)
							{
								closesocket(data_s);
								data_s = -1;
							}
						}
						
						if(data_s == -1)
						{
							bytes = ftpresp(temp, 425, "No data connection");
							send(conn_s, temp, bytes, 0);
							
							continue;
						}
						else
						{
							bytes = ftpresp(temp, 150, "Opening data connection");
							send(conn_s, temp, bytes, 0);
						}
					}
					else
					{
						bytes = ftpresp(temp, 125, "Accepted data connection");
						send(conn_s, temp, bytes, 0);
					}
					
					sysFSDirent entry;
					u64 read;
					
					while(sysLv2FsReadDir(fd, &entry, &read) == 0 && read > 0)
					{
						bytes = sprintf(temp, "%s\r\n", entry.d_name);
						send(data_s, temp, bytes, 0);
					}
					
					bytes = ftpresp(temp, 226, "Transfer complete");
				}
				else
				{
					bytes = ftpresp(temp, 550, "Cannot access directory");
				}
				
				sysLv2FsCloseDir(fd);
				
				send(conn_s, temp, bytes, 0);
				
				closesocket(data_s);
				data_s = -1;
			}
			else
			if(strcmp2(cmd, "STOR") == 0)
			{
				if(itemp == 1)
				{
					abspath(param, cwd, temp);
					
					s32 fd;
					
					if(sysLv2FsOpen(temp, SYS_O_WRONLY | SYS_O_CREAT, &fd, 0644, NULL, 0) == 0)
					{
						if(data_s == -1)
						{
							if(pasv_s > 0)
							{
								// passive
								data_s = accept(pasv_s, NULL, NULL);
								
								closesocket(pasv_s);
								pasv_s = -1;
							}
							else
							{
								// legacy
								struct sockaddr_in sa;
								socklen_t len = sizeof(sa);
								
								getpeername(conn_s, (struct sockaddr *)&sa, &len);
								sa.sin_port = htons(20);
								
								data_s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
								
								if(connect(data_s, (struct sockaddr *)&sa, sizeof(sa)) == -1)
								{
									closesocket(data_s);
									data_s = -1;
								}
							}
							
							if(data_s == -1)
							{
								bytes = ftpresp(temp, 425, "No data connection");
								send(conn_s, temp, bytes, 0);
								
								continue;
							}
							else
							{
								bytes = ftpresp(temp, 150, "Opening data connection");
								send(conn_s, temp, bytes, 0);
							}
						}
						else
						{
							bytes = ftpresp(temp, 125, "Accepted data connection");
							send(conn_s, temp, bytes, 0);
						}
						
						char *databuf = malloc(OFTP_DATA_BUFSIZE);
						
						if(databuf == NULL)
						{
							bytes = ftpresp(temp, 451, "Cannot allocate memory");
						}
						else
						{
							int err = 0;
							u64 pos, written, read;
							
							sysLv2FsFtruncate(fd, rest);
							sysLv2FsLSeek64(fd, rest, SEEK_SET, &pos);
							
							while((read = (u64)recv(data_s, databuf, OFTP_DATA_BUFSIZE, MSG_WAITALL)) > 0)
							{
								if(sysLv2FsWrite(fd, databuf, read, &written) != 0 || written < read)
								{
									err = 1;
									break;
								}
							}
							
							sysLv2FsFsync(fd);
							free(databuf);
							
							if(err == 1)
							{
								bytes = ftpresp(temp, 451, "Block write error");
							}
							else
							{
								bytes = ftpresp(temp, 226, "Transfer complete");
							}
						}
					}
					else
					{
						bytes = ftpresp(temp, 550, "Cannot access file");
					}
					
					sysLv2FsClose(fd);
				}
				else
				{
					bytes = ftpresp(temp, 501, "No file specified");
				}
				
				send(conn_s, temp, bytes, 0);
				
				closesocket(data_s);
				data_s = -1;
			}
			else
			if(strcmp2(cmd, "APPE") == 0)
			{
				if(itemp == 1)
				{
					abspath(param, cwd, temp);
					
					s32 fd;
					
					if(sysLv2FsOpen(temp, SYS_O_WRONLY | SYS_O_CREAT | SYS_O_APPEND, &fd, 0644, NULL, 0) == 0)
					{
						if(data_s == -1)
						{
							if(pasv_s > 0)
							{
								// passive
								data_s = accept(pasv_s, NULL, NULL);
								
								closesocket(pasv_s);
								pasv_s = -1;
							}
							else
							{
								// legacy
								struct sockaddr_in sa;
								socklen_t len = sizeof(sa);
								
								getpeername(conn_s, (struct sockaddr *)&sa, &len);
								sa.sin_port = htons(20);
								
								data_s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
								
								if(connect(data_s, (struct sockaddr *)&sa, sizeof(sa)) == -1)
								{
									closesocket(data_s);
									data_s = -1;
								}
							}
							
							if(data_s == -1)
							{
								bytes = ftpresp(temp, 425, "No data connection");
								send(conn_s, temp, bytes, 0);
								
								continue;
							}
							else
							{
								bytes = ftpresp(temp, 150, "Opening data connection");
								send(conn_s, temp, bytes, 0);
							}
						}
						else
						{
							bytes = ftpresp(temp, 125, "Accepted data connection");
							send(conn_s, temp, bytes, 0);
						}
						
						char *databuf = malloc(OFTP_DATA_BUFSIZE);
						
						if(databuf == NULL)
						{
							bytes = ftpresp(temp, 451, "Cannot allocate memory");
						}
						else
						{
							int err = 0;
							u64 written, read;
							
							while((read = (u64)recv(data_s, databuf, OFTP_DATA_BUFSIZE, MSG_WAITALL)) > 0)
							{
								if(sysLv2FsWrite(fd, databuf, read, &written) != 0 || written < read)
								{
									err = 1;
									break;
								}
							}
							
							sysLv2FsFsync(fd);
							free(databuf);
							
							if(err == 1)
							{
								bytes = ftpresp(temp, 451, "Block write error");
							}
							else
							{
								bytes = ftpresp(temp, 226, "Transfer complete");
							}
						}
					}
					else
					{
						bytes = ftpresp(temp, 550, "Cannot access file");
					}
					
					sysLv2FsClose(fd);
				}
				else
				{
					bytes = ftpresp(temp, 501, "No file specified");
				}
				
				send(conn_s, temp, bytes, 0);
				
				closesocket(data_s);
				data_s = -1;
			}
			else
			if(strcmp2(cmd, "RETR") == 0)
			{
				if(itemp == 1)
				{
					abspath(param, cwd, temp);
					
					s32 fd;
					
					if(sysLv2FsOpen(temp, SYS_O_RDONLY, &fd, 0, NULL, 0) == 0)
					{
						if(data_s == -1)
						{
							if(pasv_s > 0)
							{
								// passive
								data_s = accept(pasv_s, NULL, NULL);
								
								closesocket(pasv_s);
								pasv_s = -1;
							}
							else
							{
								// legacy
								struct sockaddr_in sa;
								socklen_t len = sizeof(sa);
								
								getpeername(conn_s, (struct sockaddr *)&sa, &len);
								sa.sin_port = htons(20);
								
								data_s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
								
								if(connect(data_s, (struct sockaddr *)&sa, sizeof(sa)) == -1)
								{
									closesocket(data_s);
									data_s = -1;
								}
							}
							
							if(data_s == -1)
							{
								bytes = ftpresp(temp, 425, "No data connection");
								send(conn_s, temp, bytes, 0);
								
								continue;
							}
							else
							{
								bytes = ftpresp(temp, 150, "Opening data connection");
								send(conn_s, temp, bytes, 0);
							}
						}
						else
						{
							bytes = ftpresp(temp, 125, "Accepted data connection");
							send(conn_s, temp, bytes, 0);
						}
						
						char *databuf = malloc(OFTP_DATA_BUFSIZE);
						
						if(databuf == NULL)
						{
							bytes = ftpresp(temp, 451, "Cannot allocate memory");
						}
						else
						{
							int err = 0;
							u64 pos, read;
							
							sysLv2FsLSeek64(fd, rest, SEEK_SET, &pos);
							
							while(sysLv2FsRead(fd, databuf, OFTP_DATA_BUFSIZE, &read) == 0 && read > 0)
							{
								if(send(data_s, databuf, (size_t)read, 0) < (size_t)read)
								{
									err = 1;
									break;
								}
							}
							
							free(databuf);
							
							if(err == 1)
							{
								bytes = ftpresp(temp, 451, "Block read/send error");
							}
							else
							{
								bytes = ftpresp(temp, 226, "Transfer complete");
							}
						}
					}
					else
					{
						bytes = ftpresp(temp, 550, "Cannot access file");
					}
					
					sysLv2FsClose(fd);
				}
				else
				{
					bytes = ftpresp(temp, 501, "No file specified");
				}
				
				send(conn_s, temp, bytes, 0);
				
				closesocket(data_s);
				data_s = -1;
			}
			else
			if(strcmp2(cmd, "TYPE") == 0)
			{
				bytes = ftpresp(temp, 200, "TYPE command successful");
				send(conn_s, temp, bytes, 0);
			}
			else
			if(strcmp2(cmd, "STRU") == 0)
			{
				if(strcmp2(cmd, "F") == 0)
				{
					bytes = ftpresp(temp, 200, "STRU command successful");
				}
				else
				{
					bytes = ftpresp(temp, 504, "STRU command failed");
				}
				
				send(conn_s, temp, bytes, 0);
			}
			else
			if(strcmp2(cmd, "MODE") == 0)
			{
				if(strcmp2(cmd, "S") == 0)
				{
					bytes = ftpresp(temp, 200, "MODE command successful");
				}
				else
				{
					bytes = ftpresp(temp, 504, "MODE command failed");
				}
				
				send(conn_s, temp, bytes, 0);
			}
			else
			if(strcmp2(cmd, "REST") == 0)
			{
				if(itemp == 1)
				{
					rest = atoll(param);
					
					if(rest >= 0)
					{
						bytes = sprintf(temp, "350 Restarting at %llu\r\n", (unsigned long long)rest);
					}
					else
					{
						rest = 0;
						bytes = ftpresp(temp, 501, "Invalid restart point");
					}
				}
				else
				{
					bytes = sprintf(temp, "350 Restarting at %llu\r\n", (unsigned long long)rest);
				}
				
				send(conn_s, temp, bytes, 0);
			}
			else
			if(strcmp2(cmd, "DELE") == 0)
			{
				if(itemp == 1)
				{
					abspath(param, cwd, temp);
					
					if(sysLv2FsUnlink(temp) == 0)
					{
						bytes = ftpresp(temp, 250, "File successfully removed");
					}
					else
					{
						bytes = ftpresp(temp, 550, "Cannot remove file");
					}
				}
				else
				{
					bytes = ftpresp(temp, 501, "No file specified");
				}
				
				send(conn_s, temp, bytes, 0);
			}
			else
			if(strcmp2(cmd, "RNFR") == 0)
			{
				if(itemp == 1)
				{
					abspath(param, cwd, rnfr);
					
					if(exists(rnfr) == 0)
					{
						strcpy(expectcmd, "RNTO");
						bytes = ftpresp(temp, 350, "RNFR accepted - ready for destination");
					}
					else
					{
						rnfr[0] = '\0';
						bytes = ftpresp(temp, 550, "RNFR failed - file does not exist");
					}
				}
				else
				{
					bytes = ftpresp(temp, 501, "No file specified");
				}
				
				send(conn_s, temp, bytes, 0);
			}
			else
			if(strcmp2(cmd, "RNTO") == 0)
			{
				if(itemp == 1)
				{
					abspath(param, cwd, temp);
					
					if(sysLv2FsRename(rnfr, temp) == 0)
					{
						bytes = ftpresp(temp, 250, "File successfully renamed");
					}
					else
					{
						bytes = ftpresp(temp, 550, "Cannot rename file");
					}
				}
				else
				{
					bytes = ftpresp(temp, 501, "No file specified");
				}
				
				send(conn_s, temp, bytes, 0);
			}
			else
			if(strcmp2(cmd, "SITE") == 0)
			{
				if(itemp == 1)
				{
					char param2[2016];
					itemp = strsplit(param, cmd, 15, param2, 2015);
					
					strtoupper(cmd);
					
					if(strcmp2(cmd, "CHMOD") == 0)
					{
						if(itemp == 1)
						{
							char perms[5], filename[2014];
							itemp = strsplit(param2, perms + 1, 3, filename, 2013);
							
							if(itemp == 1)
							{
								perms[0] = '0';
								
								abspath(filename, cwd, temp);
								
								if(sysLv2FsChmod(temp, strtol(perms, NULL, 8)) == 0)
								{
									bytes = ftpresp(temp, 250, "Successfully set file permissions");
								}
								else
								{
									bytes = ftpresp(temp, 550, "Cannot set file permissions");
								}
							}
							else
							{
								bytes = ftpresp(temp, 501, "Invalid CHMOD command syntax");
							}
						}
						else
						{
							bytes = ftpresp(temp, 501, "Invalid CHMOD command syntax");
						}
					}
					else
					if(strcmp2(cmd, "EXITAPP") == 0)
					{
						appstate = 1;
						
						bytes = ftpresp(temp, 221, "Exiting...");
					}
					else
					{
						bytes = ftpresp(temp, 504, "Unrecognized SITE command");
					}
				}
				else
				{
					bytes = ftpresp(temp, 501, "No SITE command specified");
				}
				
				send(conn_s, temp, bytes, 0);
			}
			else
			if(strcmp2(cmd, "SIZE") == 0)
			{
				if(itemp == 1)
				{
					abspath(param, cwd, temp);
					
					sysFSStat stat;
					if(sysLv2FsStat(temp, &stat) == 0)
					{
						bytes = sprintf(temp, "213 %llu\r\n", (unsigned long long)stat.st_size);
					}
					else
					{
						bytes = ftpresp(temp, 550, "Cannot access file");
					}
				}
				else
				{
					bytes = ftpresp(temp, 550, "No file specified");
				}
				
				send(conn_s, temp, bytes, 0);
			}
			else
			if(strcmp2(cmd, "MDTM") == 0)
			{
				if(itemp == 1)
				{
					abspath(param, cwd, temp);
					
					sysFSStat stat;
					if(sysLv2FsStat(temp, &stat) == 0)
					{
						char tstr[15];
						strftime(tstr, 14, "%Y%m%d%H%M%S", localtime(&stat.st_mtime));
						bytes = ftpresp(temp, 213, tstr);
					}
					else
					{
						bytes = ftpresp(temp, 550, "Cannot access file");
					}
				}
				else
				{
					bytes = ftpresp(temp, 550, "No file specified");
				}
				
				send(conn_s, temp, bytes, 0);
			}
			else
			if(strcmp2(cmd, "ALLO") == 0)
			{
				bytes = ftpresp(temp, 202, "ALLO command successful");
				send(conn_s, temp, bytes, 0);
			}
			else
			if(strcmp2(cmd, "USER") == 0 || strcmp2(cmd, "PASS") == 0)
			{
				bytes = ftpresp(temp, 230, "You are already logged in");
				send(conn_s, temp, bytes, 0);
			}
			else
			if(strcmp2(cmd, "OPTS") == 0 || strcmp2(cmd, "HELP") == 0
			|| strcmp2(cmd, "REIN") == 0 || strcmp2(cmd, "ADAT") == 0
			|| strcmp2(cmd, "AUTH") == 0 || strcmp2(cmd, "CCC") == 0
			|| strcmp2(cmd, "CONF") == 0 || strcmp2(cmd, "ENC") == 0
			|| strcmp2(cmd, "EPRT") == 0 || strcmp2(cmd, "EPSV") == 0
			|| strcmp2(cmd, "LANG") == 0 || strcmp2(cmd, "LPRT") == 0
			|| strcmp2(cmd, "LPSV") == 0 || strcmp2(cmd, "MIC") == 0
			|| strcmp2(cmd, "PBSZ") == 0 || strcmp2(cmd, "PROT") == 0
			|| strcmp2(cmd, "SMNT") == 0 || strcmp2(cmd, "STOU") == 0
			|| strcmp2(cmd, "XRCP") == 0 || strcmp2(cmd, "XSEN") == 0
			|| strcmp2(cmd, "XSEM") == 0 || strcmp2(cmd, "XRSQ") == 0
			|| strcmp2(cmd, "STAT") == 0)
			{
				bytes = ftpresp(temp, 502, "Command not implemented");
				send(conn_s, temp, bytes, 0);
			}
			else
			{
				bytes = ftpresp(temp, 500, "Unrecognized command");
				send(conn_s, temp, bytes, 0);
			}
		}
		else
		{
			// not logged in
			if(strcmp2(cmd, "USER") == 0)
			{
				if(itemp == 1 && strlen(param) <= 15)
				{
					strcpy(user, param);
					strcpy(expectcmd, "PASS");
					
					bytes = sprintf(temp, "331 Username %s OK. Password required\r\n", user);
				}
				else
				{
					bytes = ftpresp(temp, 501, "Invalid username");
				}
				
				send(conn_s, temp, bytes, 0);
			}
			else
			if(strcmp2(cmd, "PASS") == 0)
			{
				if(itemp == 1)
				{
					if(strcmp2(user, OFTP_LOGIN_USERNAME) == 0 && (is_empty(passwd) || strcmp2(param, passwd) == 0))
					{
						authorized = 1;
						
						bytes = ftpresp(temp, 230, "Successfully logged in");
					}
					else
					{
						bytes = ftpresp(temp, 430, "Invalid username or password");
					}
					
					send(conn_s, temp, bytes, 0);
				}
				else
				{
					bytes = ftpresp(temp, 501, "Invalid password");
					send(conn_s, temp, bytes, 0);
				}
			}
			else
			{
				bytes = ftpresp(temp, 530, "Not logged in");
				send(conn_s, temp, bytes, 0);
			}
		}
	}
	
	closesocket(conn_s);
	closesocket(data_s);
	closesocket(pasv_s);
	
	sysThreadExit(0);
}

