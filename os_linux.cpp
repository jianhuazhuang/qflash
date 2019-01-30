

#define __OS_LINUX_CPP_H__

#include "platform_def.h"
#include "os_linux.h"
#include "os_linux.h"
#include "download.h"
#include "quectel_common.h"
#include "quectel_log.h"

#define MAX_TRACE_LENGTH      (256)
#define MAX_PATH 260
const char PORT_NAME_PREFIX[] = "/dev/ttyUSB";
static char log_trace[MAX_TRACE_LENGTH];
int g_default_port = 0;
int endian_flag = 0; 
int dump = 0;

#ifdef PROGRESS_FILE_FAETURE
unsigned long g_cumulation_files_size = -1;
unsigned long g_total_files_size = -1;
#endif

static download_context s_QdlContext;
download_context *QdlContext = &s_QdlContext;


extern "C" int fastboot_main(int argc, char **argv);
extern "C" int firehose_main_entry(int argc, char **argv);



int retrieve_diag_port(download_context* ctx_ptr, int auto_detect);

void show_log(const char *msg, ...)
{
    va_list ap;
        
    va_start(ap, msg);
    vsnprintf(log_trace, MAX_TRACE_LENGTH, msg, ap);
    va_end(ap);
    
    dbg_time("%s\n", log_trace);
}
void prog_log(int writesize,int size,int clear)
{
	
	unsigned long long tmp=(unsigned long long)writesize * 100;
	unsigned int progress = tmp/ size;
    if(progress==100)
    {
        printf( "progress : %d%% finished\n", progress);
    }
    else
    {
        printf( "progress : %d%% finished\r", progress);
    }
}

void qdl_msg_log(int msgtype,char *msg1,char * msg2)
{
}

static int config_uart(int fd, int ioflush)
{
	struct termios tio;
    struct termios settings;
    int retval;
    memset(&tio,0,sizeof(tio));
    tio.c_iflag=0;
    tio.c_oflag=0;
    tio.c_cflag=CS8|CREAD|CLOCAL;           // 8n1, see termios.h for more information
    tio.c_lflag=0;
    tio.c_cc[VMIN]=1;
    tio.c_cc[VTIME]=5;
    cfsetospeed(&tio,B115200);            // 115200 baud
    cfsetispeed(&tio,B115200);            // 115200 baud
    tcsetattr(fd, TCSANOW, &tio);
	retval = tcgetattr (fd, &settings);
	if(-1 == retval)
	{
		return 1;
	}    
	cfmakeraw (&settings);
	settings.c_cflag |= CREAD | CLOCAL;
	if(ioflush)
	{
		tcflush(fd, TCIOFLUSH);
	}
	retval = tcsetattr (fd, TCSANOW, &settings);
	if(-1 == retval)
	{
		return 1;
	}
    return 0;
}

int openport(int ioflush)
{
    int tmp_port;
    tmp_port = g_default_port;
    int retry = 6;
    char pc_comport[32]; 
    
	//first close it if it opened
    if(g_hCom != 0)
    {
    	close(g_hCom);
    	g_hCom = 0;
    }
start_probe_port:
    memset(pc_comport,0,sizeof(pc_comport));
    sprintf(pc_comport, "%s%d", PORT_NAME_PREFIX, tmp_port);
    if(access(pc_comport, F_OK))
    {
        tmp_port++;
        retry--;
        if(retry > 0)
            goto start_probe_port;
        else
            return 1;
    }
    dbg_time("Start to open com port: %s\n", pc_comport);
    //g_hCom = (HANDLE) open(pc_comport, O_RDWR | O_NOCTTY);
    g_hCom = open (pc_comport, O_RDWR | O_SYNC);
    if(g_hCom < 0)
    {
        g_hCom = 0;
        return false;
    }
    else
    {
    	config_uart((int)g_hCom, ioflush) ;
    }
    return 0;
}

int closeport()
{
    close(g_hCom);
    g_hCom = 0;
    usleep(1000 * 100);
    return 0;
}

int WriteABuffer(int file, const unsigned char * lpBuf, int dwToWrite)
{
	int written = 0;
	char buff[128] = {0};
	char buff_tmp[4] = {0};
	if(dwToWrite <= 0)
		return dwToWrite;
	written = write(file, lpBuf, dwToWrite);
	if(written!=dwToWrite)
	{
		dbg_time("%d,%d\n",written,dwToWrite);
	}
	if(written < 0)   
	{
		dbg_time("write strerror: %s\n", strerror(errno));
		return 0;
	}
	else
	{
		if(dump)
		{
			strcat(buff, "tx: ");
			for(int i = 0; i < 32 && i < written; i++)
			{
				sprintf(buff_tmp, "%02x ", lpBuf[i]);
				strcat(buff, buff_tmp);
			}
			dbg_time("%s\n", buff);
		}
	}
	
	return written;
}

int ReadABuffer(int file, unsigned char * lpBuf, int dwToRead, int timeout)
{

	int read_len = 0;
	char buff[128] = {0};
	char buff_tmp[4] = {0};

	if(dwToRead <= 0)
	return 0;

	fd_set rd_set;
	FD_ZERO(&rd_set);
	FD_SET(file, &rd_set);
	struct timeval timeout1;
	timeout1.tv_sec = timeout;
	timeout1.tv_usec = 0;
	int selectResult = select(file + 1, &rd_set, NULL, NULL, &timeout1);
	if(0 == selectResult)
	{
		dbg_time("Timeout Occured, No response or command came from the target!\n");
		return 0;
	}
	if( selectResult < 0)
	{
		dbg_time("select returned error : %s\n", strerror(errno));
		return 0;
	}
	if(selectResult < 0)
	{
		dbg_time("select set failed\n");
		return 0;
	}
	read_len = read(file, lpBuf, dwToRead);
	if(0 == read_len)
	{
		dbg_time("zero length packet received or hardware connection went off.\n");
		return 0;
	}
	else if( read_len < 0)
	{
		if(EAGAIN == errno)
		{
			usleep(1000);
			return 0;
		}else
		{
			dbg_time("read file descriptor returned error :%s, error code %d", strerror(errno), read_len);
			return 0;
		}
	}
	//read ok
    {
    	extern int dump;
    	if(dump)
		{
			strcat(buff, "rx: ");
			for(int i = 0; i < 32 && i < read_len; i++)
			{
				sprintf(buff_tmp, "%02x ", lpBuf[i]);
				strcat(buff, buff_tmp);
			}
			dbg_time("%s\n", buff);
		}
    }
    return read_len;
}

void qdl_flush_fifo(int fd, int tx_flush, int rx_flush,int rx_tcp_flag)
{
	if(tx_flush)
		tcflush(fd, TCOFLUSH);

	if(rx_flush)
		tcflush(fd, TCIFLUSH);
}

void qdl_sleep(int millsec)
{
    int second = millsec / 1000;
    if(millsec % 1000)
        second += 1;
    sleep(second);
}

static int os_ready(download_context *ctx_ptr)
{
	if (ctx_ptr->update_method == 0 && !detect_adb()) {
		return 0;	
	}
	if(0 == ctx_ptr->update_method || 1 == ctx_ptr->update_method)
    {
    	//auto detect diagnose port
		if(detect_diag_port(&ctx_ptr->diag_port) == 0)
		{		
			Resolve_port(ctx_ptr->diag_port, &g_default_port);
			if(g_default_port == -1)
			{
				dbg_time("Auto detect quectel diagnose port failed!");
				return -1;
			}else
			{
				dbg_time("Auto detect quectel diagnose port = %s\n", ctx_ptr->diag_port);
				return 0;
			}
		}else
		{
			dbg_time("Cannot find Quectel diagnoese and adb port.\n");
			return -2;
		}
    }else
    {
    	if(detect_modem_port(&ctx_ptr->modem_port) == 0)
		{		
			dbg_time("Auto detect Quectel modem port = %s\n", ctx_ptr->modem_port);
			return 0;
		}else
		{
			dbg_time("Auto detect Quectel modem port failed.\n");
			return false;
		}
    }
    return 1;
}

static const char* platfrom2str(module_platform_t t)
{
	switch(t)
	{
		case platform_9x06:
			return "9X06";
		case platform_9x07:
			return "9X07";
		case platform_9x45:
			return "9X45";
		case platform_unknown:
			return "Unknown";
		default:
		return "Unknown";
	}
	return "";
}

int qdl_pre_download(download_context *ctx_ptr) {
	char *product_model = NULL;
	int ret;
    time_t tm;
    int result;
    
    time(&tm);    
    show_log("Module upgrade tool, %s", ctime(&tm));

    if(os_ready(ctx_ptr) != 0)
    {
    	result = 0;
    	goto err_exit;
    }    	
    
    //load image and others files
    result = ProcessInit(ctx_ptr);    
    if(platform_unknown == ctx_ptr->platform)
    {
    	result = 0;
    }else
    {
    	dbg_time("module platform : %s\n", platfrom2str(ctx_ptr->platform));
    }

	ret = get_product_model(&ctx_ptr->prodct_model);
	dbg_time("product model = %s", ctx_ptr->prodct_model);
    
    if (result) {
    	switch(ctx_ptr->update_method)
    	{
    	case 0:
    	case 1:
    		result = process_streaming_fastboot_upgarde(ctx_ptr);
    		break;
    	case 2:
    		result = process_at_fastboot_upgrade(ctx_ptr);
    		break;
		case 3:
			result = process_firehose_upgrade(ctx_ptr);
			break;
    	default:
    		dbg_time("unknown upgrade method, please contact Quectel\n");
    		break;
    	}
    }
err_exit:    
	qdl_post_download(ctx_ptr, result);
	return result == 1 ? 0 : 1;
}

void qdl_post_download(download_context *pQdlContext, int result)
{
    time_t tm;
    time(&tm);
    if(g_hCom != 0)
        closeport();
    if(result == 1)
    {
        dbg_time("Upgrade module successfully, %s\n", ctime(&tm));
    }
    else
    {
    	dump_sys_info();
        dbg_time("Upgrade module unsuccessfully, %s\n", ctime(&tm));
    }
    ProcessUninit(pQdlContext);
}

int qdl_start_download(download_context *pQdlContext) {
	pQdlContext->process_cb = upgrade_process;
    return qdl_pre_download(pQdlContext);
}

void get_duration(double start)
{
	dbg_time("THE TOTAL DOWNLOAD TIME IS %.3f s\n",(get_now() - start));
}

void usage()
{
    dbg_time("Qflash [options]\n");
    dbg_time(" -f=[firmware path]                           Firmware path.\n");
    dbg_time(" -p=[tty device]                              TTY device.\n");
    dbg_time(" -s=[size]                                    Size of cache.\n");
    dbg_time(" -m=[0|1|2|3]                                 Mode, upgrade protocol:\n");
    dbg_time("                                              1 for stream download protocol\n");
    dbg_time("                                              0 for fastboot download protocol\n");
    dbg_time("                                              2 for fastboot download protocol (at command first)\n");
    dbg_time("                                              3 for firehose download protocol\n");
    dbg_time(" -v                                           Verbose.\n");
    dbg_time(" -s                                           Size of cache.\n");
    dbg_time(" -h                                           Show this message.\n");
}

extern int writeFile(double updatePercent);
int main(int argc, char *argv[]) {
    extern proto_name_t *proto_list;
	int auto_detect_diag_port = 1;
	double start_time, end_time;
	//need_check_fw_version = 0;	
	if ((argc > 1) && (!strcmp(argv[1], "fastboot"))) {
		return fastboot_main(argc - 1, argv + 1);
	}
	if ((argc > 1) && (!strcmp(argv[1], "qfirehose"))) {
		return firehose_main_entry(argc - 1, argv + 1);
	}
	/*build V1.4.7*/
	dbg_time("QFlash Version: LTE_QFlash_Linux&Android_V1.4.8\n"); 
	dbg_time("Builded at: %s %s\n", __DATE__,__TIME__);

	download_context *ctx_ptr = &s_QdlContext;
	memset(ctx_ptr, 0, sizeof(download_context));
    ctx_ptr->firmware_path = NULL;
    ctx_ptr->cache = 1024;
    ctx_ptr->update_method = 0;			//use fastboot method default
    ctx_ptr->md5_check_enable = 0;
    ctx_ptr->platform = platform_unknown;
    ctx_ptr->prodct_model = NULL;
    g_default_port = 0;
	int bFile = 0;
	int opt;
	
	if(checkCPU())
	{
		dbg_time("The CPU is Big endian\n");
		endian_flag = 1;
	}
	else
	{
		dbg_time("The CPU is little endian\n");
	}
#ifdef ANDROID	
	show_user_group_name();
#endif
	while((opt=getopt(argc,argv,"f:p:m:s:vh"))>0)
	{
		switch (opt) {
        case 'f':
            bFile=1;
            if(access(optarg,F_OK)==0) {
                if (optarg[0] != '/') {
                    char cwd[MAX_PATH] = {0};
                    getcwd(cwd, sizeof(cwd));
                    asprintf(&ctx_ptr->firmware_path, "%s/%s", cwd, optarg);      
                } else {
                    asprintf(&ctx_ptr->firmware_path, "%s", optarg);           
                }
            } else {
                dbg_time("Error:Folder does not exist\n");
                return 0;
            }
            break;        
        case 'p':
        	auto_detect_diag_port = 0;
            Resolve_port(optarg, &g_default_port);
            if (g_default_port == -1) {
                dbg_time("Error:Port format error\n");
                return 0;
            }
            break;
       case 'm':
       		/*
       		method = 1 --> streaming download protocol
       		method = 0 --> fastboot download protocol
       		method = 2 --> fastboot download protocol (at command first)
       		method = 3 --> firehose download protocol
       		*/
            if(	atoi(optarg) == 0 ||
            	atoi(optarg) == 1 ||
            	atoi(optarg) == 2 ||
            	atoi(optarg) == 3
           	)
            {
                ctx_ptr->update_method = atoi(optarg);
                //dbg_time("Upgrade protocol %s update_method %d\n", proto_list[atoi(optarg)].proto_name, ctx_ptr->update_method);
                //dbg_time("Upgrade protocol %d \n", proto_list[0].proto);
                //dbg_time("Upgrade protocol %s \n", proto_list[1].proto_name);
                //dbg_time("Upgrade protocol %s \n", proto_list[2].proto_name);
                //dbg_time("Upgrade protocol %s \n", proto_list[3].proto_name);
            }
            else
            {
                dbg_time("Error:Unknow upgrade protocol %s\n", optarg);
                return 0;
            }
            break;
        case 's':
            if(atoi(optarg) >= 128 && atoi(optarg) <= 1204)
            {
                ctx_ptr->cache = atoi(optarg);
            }
            else
            {
                dbg_time("Error:Transport block size format error\n");
                return 0;
            }
            break;	
		case 'v':
			{
				dump = 1;
			}
			break;
		case 'h':
            usage();
           return 0;
        default:
           return 0;
        }
	}

	if(bFile == 0)
    {
        dbg_time("Error:Missing file parameter\n");
        return 0;
    }   
#ifdef PROGRESS_FILE_FAETURE
	writeFile(0.0);
#endif
	start_time = get_now();
	if(0 == qdl_start_download(ctx_ptr))
	{		
		//get duration when upgrade successfully
		get_duration(start_time);
	}
	return 0;
}

