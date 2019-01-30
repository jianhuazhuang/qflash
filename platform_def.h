
#ifndef __PLATFORM_DEF_H__
#define __PLATFORM_DEF_H__


#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <termios.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <linux/errno.h>
#include <iostream>
#include <vector>
#include <sys/time.h>
#include <fcntl.h>
#include <stdarg.h>
#include <pwd.h>
#include <grp.h>




using namespace std;


typedef unsigned int 	   uint32;
typedef bool  			   BOOL;
typedef bool    	 	   boolean;
typedef unsigned short	   uint16;		
typedef unsigned char         byte;

extern int g_hCom; 


#define WRITE_PACKET_LENGTH			1024 * 4

void dbg_time (const char *fmt, ...);

#endif /*__PLATFORM_DEF_H__*/

