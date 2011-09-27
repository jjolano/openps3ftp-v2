#ifndef _opfdefs_included
#define _opfdefs_included

// You can change the path in OFTP_PASSWORD_FILE so that the password will stay even when the program is deleted.

#define OFTP_VERSION		"v3.0"
#define OFTP_PASSWORD_FILE	"/dev_hdd0/game/OFTP00001/USRDIR/passwd"
#define OFTP_LOGIN_USERNAME	"root"
#define OFTP_DATA_BUFSIZE	32768
#define OFTP_LISTEN_BACKLOG	32

// shared variables
extern int running;
extern char passwd[64];
#endif
