
#ifndef __DOWNLOAD_H__
#define __DOWNLOAD_H__

#include <vector>
#include "quectel_common.h"

using namespace std;

#ifndef MAX_PATH
#define MAX_PATH 260
#endif




typedef void (*qdl_msg_cb)(int msgtype,const char *msg1,const char * msg2);
typedef void (*qdl_prog_cb)(int writesize,int size,int clear);
typedef int  (*qdl_log_cb)(const char *msg,...);
typedef void (*qdl_text_cb)(const char *msg,...);


typedef struct {
    char* name;
    char* img_name;
    char* partition_name;
} Ufile;


typedef enum {
    STATE_NORMAL_MODE ,
    STATE_DLOAD_MODE ,
    STATE_GOING_MODE ,
    STATE_SAHARA_MODE,
    STATE_UNSPECIFIED,
} target_current_state;

typedef enum {
    TARGET_PLATFORM_6270 ,
    TARGET_PLATFORM_6290 ,
    TARGET_PLATFORM_6085 ,
    TARGET_PLATFORM_9615 ,
} target_platform;

typedef void (*process_fun_t)(int writesize,int size,int clear);

typedef struct {
    target_current_state  TargetState;
    target_platform       TargetPlatform;

    process_fun_t	process_cb;

    int firehose_support;
    char *partition_complete_mbn;
    char *patch_xml;
    char *prog_nand_firehose_mbn;
    char *rawprogram_nand_update_xml;
    char *firehose_path;
    int update_method;
    int cache;
    int md5_check_enable;
    char *firmware_path;//save the path of the upgrade files
    char *ENPRG_path;
    char *NPRG_path;
    char *partition_path;
    char *diag_port;				//diagnose port name
    char *modem_port;				//modem port name

    char *contents_xml_path;
    vector<Ufile> ufile_list;

    module_platform_t	platform;
    char* prodct_model;
} download_context, *p_download_context;

extern download_context *QdlContext;
#define QDL_LOGFILE_NAME	"qdl.txt"

int process_streaming_fastboot_upgarde(download_context* );
int process_at_fastboot_upgrade(download_context* );

int ProcessInit(download_context *pQdlContext);
int save_log(char *fmt,...);
int ProcessUninit(download_context *pQdlContext);
void Processing(download_context *pQdlContext);

void free_ufile(Ufile);
void Resolve_port(char *chPort,int* nPort );
void log_info (const char *fmt, ...);
void dump_sys_info();

int process_firehose_upgrade(download_context* ctx_ptr);
#define DETECT_DEV_TIMEOUT				30
#endif /*__DOWNLOAD_H__*/
