#ifndef __OS_LINUX_H__
#define __OS_LINUX_H__
#include "download.h"

void qdl_flush_fifo(int fd, int tx_flush, int rx_flush,int rx_tcp_flag);

int openport(int ioflush);
int closeport();
int WriteABuffer(int file, const unsigned char * lpBuf, int dwToWrite);
int ReadABuffer(int file, unsigned char * lpBuf, int dwToRead, int timeout = 2);


void show_log(char *msg, ...);
void qdl_sleep(int millsec);
int qdl_pre_download(download_context *pQdlContext);
void qdl_post_download(download_context *pQdlContext, int result);
int  qdl_start_download(download_context *pQdlContext);
extern int g_default_port;
extern int endian_flag;

#endif  /*TARGET_OS_LINUX*/

