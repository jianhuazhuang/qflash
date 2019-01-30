#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <time.h>
#include <sys/time.h>

extern void dbg_time (const char *fmt, ...);


int sahara_main(const char *firehose_dir);
int firehose_main (const char *firehose_dir);
int qusb_find_ec20(int *idVendor, int *idProduct, int *interfaceNum);

#define error_return()  do {dbg_time("%s %s %d fail\n", __FILE__, __func__, __LINE__); return __LINE__; } while(0)
#if 1
int firehose_main_entry(int argc, char* argv[]){
#else
int main(int argc, char *argv[]) {
#endif
    int opt;
    int retval;
    char *firehose_dir = NULL;
    int idVendor = 0, idProduct = 0, interfaceNum = 0;
    
    optind = 1;
    while ( -1 != (opt = getopt(argc, argv, "f:"))) {
        switch (opt) {
            case 'f':
                firehose_dir = strdup(optarg);
            break;
            default:
            break;
        }
    }

    if (firehose_dir == NULL)
        error_return();

    if (access(firehose_dir, R_OK))
        error_return();

    opt = strlen(firehose_dir);
    if (firehose_dir[opt-1] == '/') {
        firehose_dir[opt-1] = '\0';
    }

    qusb_find_ec20(&idVendor, &idProduct, &interfaceNum);

    if (idVendor != 0x05c6 || idProduct != 0x9008 || interfaceNum != 1) {
        error_return();
    }
        
    retval = sahara_main(firehose_dir);
    if (!retval)
        retval = firehose_main(firehose_dir);
    return retval;
}
