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

#include "common.h"
#include "functions.h"

void abspath(const char* relpath, const char* cwd, char* abspath)
{
	if(relpath[0] == '/')
	{
		// already absolute, just copy
		strcpy(abspath, relpath);
	}
	else
	{
		// relative, append to cwd and copy
		strcpy(abspath, cwd);
		
		if(cwd[strlen(cwd) - 1] != '/')
		{
			strcat(abspath, "/");
		}
		
		strcat(abspath, relpath);
	}
}

void strtoupper(char* str)
{
	do if(96 == (224 & *str)) *str &= 223;
	while(*str++);
}

int exists(const char* path)
{
	Lv2FsStat entry;
	return sysFsStat(path, &entry);
}

int is_dir(const char* path)
{
	Lv2FsStat entry;
	sysFsStat(path, &entry);
	return fis_dir(entry);
}

int ssplit(const char* str, char* left, int lmaxlen, char* right, int rmaxlen)
{
	int ios = strcspn(str, " ");
	int len = strlen(str);
	
	int lrange = (ios < lmaxlen ? ios : lmaxlen);
	strncpy(left, str, lrange);
	left[lrange] = '\0';
	
	if(ios < len)
	{
		int rrange = ((len - ios - 1) < rmaxlen ? (len - ios - 1) : rmaxlen);
		strncpy(right, str + ios + 1, rrange);
		right[rrange] = '\0';
		return 1;
	}
	
	right[0] = '\0';
	return 0;
}

int slisten(int port, int backlog)
{
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	
	int list_s = socket(AF_INET, SOCK_STREAM, 0);
	
	bind(list_s, (struct sockaddr *)&sa, sizeof(sa));
	listen(list_s, backlog);
	
	return list_s;
}

int sconnect(const char ipaddr[16], int port, int *sd)
{
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	sa.sin_addr.s_addr = inet_addr(ipaddr);
	
	return connect((*sd = socket(AF_INET, SOCK_STREAM, 0)), (struct sockaddr *)&sa, sizeof(sa));
}

void sclose(int *sd)
{
	if(*sd > -1)
	{
		shutdown(*sd, SHUT_RDWR);
		closesocket(*sd);
		*sd = -1;
	}
}

int fdtosd(int sd, Lv2FsFile fd, long long rest)
{
	int ret = -1;
	char *buf = malloc(BUFFER_SIZE);
	
	if(buf != NULL)
	{
		ret = 0;
		
		u64 read, pos;
		
		if(rest > 0)
		{
			sysFsLseek(fd, rest, SEEK_SET, &pos);
		}
		
		while(sysFsRead(fd, buf, BUFFER_SIZE, &read) == 0 && read > 0)
		{
			if(send(sd, buf, (size_t)read, 0) < (size_t)read)
			{
				ret = -1;
				break;
			}
		}
		
		free(buf);
	}
	
	return ret;
}

int sdtofd(Lv2FsFile fd, int sd, long long rest)
{
	int ret = -1;
	char *buf = malloc(BUFFER_SIZE);
	
	if(buf != NULL)
	{
		ret = 0;
		
		u64 read, written, pos;
		
		if(rest > 0)
		{
			sysFsLseek(fd, rest, SEEK_SET, &pos);
		}
		
		while((read = (u64)recv(sd, buf, BUFFER_SIZE, MSG_WAITALL)) > 0)
		{
			if(sysFsWrite(fd, buf, read, &written) != 0 || written < read)
			{
				ret = -1;
				break;
			}
		}
		
		free(buf);
	}
	
	return ret;
}
