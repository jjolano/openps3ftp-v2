// OpenPS3FTP functions
#pragma once

#define ssend(socket,str) send(socket, str, strlen(str), 0)
#define fis_dir(entry) S_ISDIR(entry.st_mode)

void abspath(const char* relpath, const char* cwd, char* abspath);
int exists(const char* path);
int is_dir(const char* path);
int ssplit(const char* str, char* left, int lmaxlen, char* right, int rmaxlen);

int slisten(int port, int backlog);
int sconnect(const char ipaddr[16], int port, int *sd);
void sclose(int *sd);

int fdtosd(int sd, Lv2FsFile fd, long long rest);
int sdtofd(Lv2FsFile fd, int sd, long long rest);
