#ifndef __FILE_H__
#define __FILE_H__
#include "platform_def.h"
#include "download.h"


unsigned char * open_file(const char *filepath,uint32 *filesize);
void free_file(unsigned char *filebuf,uint32 filesize);
extern int image_read(download_context *ctx);
extern int image_close(download_context *ctx);


#endif /*__FILE_H__*/

