#include "platform_def.h"
#include "download.h"
#include "quectel_log.h"
#include "file.h"
#include "serialif.h"
#include "os_linux.h"
#include "quectel_common.h"
#include "atchannel.h"
#include "ril-daemon.cpp"
#include <asm/byteorder.h>
#include <linux/un.h>
#include <linux/usbdevice_fs.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 20)
#include <linux/usb/ch9.h>
#else
#include <linux/usb_ch9.h>
#endif

extern int dump;
extern download_context *QdlContext;
extern int g_hCom; 
unsigned char boot_tmp_crc_table[1024*4] = {0};
unsigned char *boot_tmp = boot_tmp_crc_table;
//partition
#define MISC_PARTITION_LENGTH		(1024 * 4)
unsigned char msic_bytes[MISC_PARTITION_LENGTH] = {0};

int boot_tmp_datasize = sizeof(boot_tmp_crc_table);


static const char* not_support_at_fastboot = 
"AT+qfastboot execute failed.\n"
"May be current firmware not support this method.\n"
"Please try QFlash -f firmware_path -m [0,1]\n";

int do_streaming_download(download_context *pQdlContext);
int do_fastboot_download_direct(download_context *pQdlContext);
int do_fastboot_download(download_context *pQdlContext);

#ifdef PROGRESS_FILE_FAETURE
extern int constantly_schedule(const char *fileName);
#endif


int ProcessInit(download_context *pQdlContext) {
	int ret;
	memset(msic_bytes, 0xFF, MISC_PARTITION_LENGTH);
    if ((ret = image_read(pQdlContext)) != 1) {
    	if(-1 == ret)
    	{
    		//md5 check failed. do nothing.
    	}else
    	{
    		dbg_time("Parse file error\n");
    	}
        
        return 0;
    }
    return 1;
}

int ProcessUninit(download_context *pQdlContext) {
    image_close(pQdlContext);
    return 1;
}

int module_state(download_context *pQdlContext)
{
    dbg_time("Module status detect\n");
    int timeout = 10;
    while (timeout--) {
        pQdlContext->TargetState = send_sync();
        if (pQdlContext->TargetState == STATE_UNSPECIFIED) {
            if (timeout == 0) {
                dbg_time("Module status is unspecified, download failed!\n");
                return false;
            }
            sleep(2);
        } else {
            break;
        }
    }
    return true;
}

static int ql_pclose(FILE *iop)
{
	(void)fclose(iop);
	return 0;
}

static int do_flash_mbn(const char *partion, const char *filepath) {
    char *program = (char *) malloc(MAX_PATH + MAX_PATH + 32);
    int result;
    unsigned char *filebuf;
    uint32 filesize;
    FILE * fp = NULL;
    
    if (!program) {
        dbg_time("fail to malloc memory for %s %s\n", partion, filepath);
        return 0;
    }

    sprintf(program, "flash %s %s", partion, filepath);
    dbg_time("%s\n", program);
    if(strstr(filepath, "invalid-boot") != NULL)
	{
		filebuf = boot_tmp;
		filesize = boot_tmp_datasize;
	}
	else if(strstr(filepath, "misc") != NULL)
	{
		filebuf = msic_bytes;
		filesize = MISC_PARTITION_LENGTH;
	}
    else
    {
	    if (!partion || !filepath || !filepath[0] || access(filepath, R_OK)) 
	    {
	        free(program);
	        return 0;
	    }

#if 0
	    filebuf = open_file(filepath, &filesize);
	    if (filebuf == NULL) {
	        free(program);
	        return false;
	    }
#else
	    filebuf = (unsigned char *)malloc(4 * 1024);
	    if (filebuf == NULL) {
	        free(program);
	        return false;
	    }

	    fp = fopen(filepath, "r");
	    if (fp == NULL) {
	        dbg_time("%s(%s) failed to fopen errno: %d (%s)\n", __func__, filepath, errno, strerror(errno));
	        return 0;
	    }
	    
	    fseek(fp, 0, SEEK_END);
	    filesize = ftell(fp);
	    fseek(fp, 0, SEEK_SET);
#endif
    }

    strcpy(program, partion);
    result = handle_openmulti(strlen(partion) + 1, (unsigned char *)program);
    if (result == false) {
        dbg_time("%s open failed\n", partion);
    	fclose(fp); free(filebuf); filebuf = NULL;
        goto __fail;
    }

    sprintf(program, "sending '%s' (%dKB)", partion, (int)(filesize/1024));
    dbg_time("%s\n", program);

    result = handle_write(fp, filebuf, filesize);
    if(fp != NULL){
        fclose(fp); free(filebuf); filebuf = NULL;
	}
    if (result == false) {
        dbg_time("%s download failed\n", partion);
        goto __fail;
    }

    result = handle_close();
    if (result == false) {
        dbg_time("%s close failed.\n", partion);
        goto __fail;
    }

    dbg_time("OKAY\n");

    free(program);
    if(fp != NULL){
		free_file(filebuf, filesize);
	}
    return true;

__fail:
    free(program);
    if(fp!=NULL){
		free_file(filebuf, filesize);
	}
    return false;
}
#define FREE_SOURCE  do{if(program) free(program); if(line) free(line);}while(0);

static int do_firehose(const char* cmd, const char* path)
{
	char *program = (char *) malloc(MAX_PATH + MAX_PATH + 32);
	char *line = (char *) malloc(MAX_PATH);
	char *self_path = (char *) malloc(MAX_PATH);
	FILE * fpin;
	unsigned char ok = 0;
	
	int self_count = 0;
	

	if (!program || !line || !self_path) {
		FREE_SOURCE
		return 0;
	}

	self_count = readlink("/proc/self/exe", self_path, MAX_PATH - 1);
	if (self_count > 0) {
		self_path[self_count] = 0;
	} else {
		FREE_SOURCE
		return 0;
	}
	
	if (!strcmp(cmd, "qfirehose")) {		
		sprintf(program, "%s qfirehose -f \"%s\"", self_path, path);
	} else {
		//sprintf(program, "%s fastboot %s", self_path, cmd);
		return -1;
	}

	dbg_time("%s\n", program);
	strcat(program, " 2>&1");
	fpin = popen(program, "r");

	if (!fpin) {
		dbg_time("popen failed\n");
		dbg_time("popen strerror: %s\n", strerror(errno));
		FREE_SOURCE
		return -1;
	}

	while (fgets(line, MAX_PATH - 1, fpin) != NULL) {
		dbg_time("%s", line);
		//sleep(1);
		if(strstr(line, "Requested POWER_RESET") != NULL)
		{
			ok = 1;
		}
	}
	
	ql_pclose(fpin);
	FREE_SOURCE
	return ok == 1? 0 : -1;
}
static int do_fastboot(const char *cmd, const char *partion, const char *filepath) {
	char *program = (char *) malloc(MAX_PATH + MAX_PATH + 32);
	char *line = (char *) malloc(MAX_PATH);
	char *self_path = (char *) malloc(MAX_PATH);
	FILE * fpin;
	
	int self_count = 0;
	int recv_okay = 0;
	int recv_9607 = 0;
	
//#define FREE_SOURCE  do{if(program) free(program); if(line) free(line);if(self_path) free(self_path);}while(0);

	if (!program || !line || !self_path) {
		dbg_time("fail to malloc memory for %s %s %s\n", cmd, partion, filepath);
		FREE_SOURCE
		return 0;
	}

	self_count = readlink("/proc/self/exe", self_path, MAX_PATH - 1);
	if (self_count > 0) {
		self_path[self_count] = 0;
	} else {
		dbg_time("fail to readlink /proc/self/exe for %s %s %s\n", cmd, partion, filepath);
		FREE_SOURCE
		return 0;
	}
	
	if (!strcmp(cmd, "flash")) {
		if (!partion || !partion[0] || !filepath || !filepath[0] || access(filepath, R_OK)) {
			FREE_SOURCE
			return 0;
		}
		sprintf(program, "%s fastboot %s %s \"%s\"", self_path, cmd, partion, filepath);
	    //sprintf(program, "%s fastboot %s %s %s 1>2 2>./rfastboot", self_path, cmd, partion, filepath);
		
	} else {
		sprintf(program, "%s fastboot %s", self_path, cmd);
		//sprintf(program, "%s fastboot %s 1>./rfastboot", self_path, cmd);
	}

	dbg_time("%s\n", program);
	strcat(program, " 2>&1");
	fpin = popen(program, "r");


	if (!fpin) {
		dbg_time("popen failed\n");
		dbg_time("popen strerror: %s\n", strerror(errno));
		FREE_SOURCE
		return 0;
	}

	while (fgets(line, MAX_PATH - 1, fpin) != NULL) {
		dbg_time("%s", line);
		if (strstr(line, "OKAY")) {
			recv_okay++;
		} else if (strstr(line, "fastboot")) {
			recv_9607++;
		}
	}
	
	ql_pclose(fpin);
	FREE_SOURCE
	if (!strcmp(cmd, "flash"))
	{	
		return (recv_okay == 2);
	}
	else if (!strcmp(cmd, "devices"))
	{
		return (recv_9607 == 1);
	}
   	else if (!strcmp(cmd, "continue"))
	{
		return (recv_okay == 1);
	}
   	else
	{
		return (recv_okay > 0);
	}
	
	return 0;
}

int BFastbootModel() {
	return do_fastboot("devices", NULL, NULL);
}



int downloadfastboot(download_context *pQdlContext) {
	int ret = 0;
	for (std::vector<Ufile>::iterator iter = pQdlContext->ufile_list.begin();iter!=pQdlContext->ufile_list.end();iter++) 
	{
		if(strcmp("0:MIBIB",((Ufile)*iter).name)!=0)
		{			
			strToLower((*iter).partition_name);
			ret = do_fastboot("flash", (*iter).partition_name, ((Ufile)*iter).img_name);
			if(1 != ret)
			{										
				dbg_time("fastboot flash error!, upgrade process interrupt.  exit!\n");
				return 1;				
			}
#ifdef PROGRESS_FILE_FAETURE			
			constantly_schedule(((Ufile)*iter).img_name);
#endif			
			transfer_statistics::getInstance()->set_write_bytes(get_file_size(((Ufile)*iter).img_name));
		}
			
	}
	do_fastboot("reboot", NULL, NULL);
	return 0;
}

static void ignore_sahara_stage_files(download_context *pQdlContext)
{
	for (std::vector<Ufile>::iterator iter = pQdlContext->ufile_list.begin();
		iter != pQdlContext->ufile_list.end();/*iter++*/) 
	{
		if(strcmp("0:MIBIB",((Ufile)*iter).name)!=0)
		{

			if(strstr(((Ufile)*iter).name,"0:aboot") || strstr(((Ufile)*iter).name,"0:SBL")   ||strstr(((Ufile)*iter).name,"0:RPM")  || strstr(((Ufile)*iter).name,"0:TZ") )
			{
				iter = pQdlContext->ufile_list.erase(iter);
			}
			else
			{
				iter++;
			}
		}
		else
		{
			iter++;
		}
	}       
}
/*
1. wait port disconnect
2. wait port connect
3. open
*/
static int close_and_reopen(int ioflush)
{
	closeport();
    if(wait_diag_port_disconnect(DETECT_DEV_TIMEOUT) == 0)
    {
    	dbg_time("Diagnose port disconnect\n");
    }
    else
    {
    	dbg_time("Warning: Diagnose port may be exist always.\n");
    }
    if(detect_diag_port_timeout(DETECT_DEV_TIMEOUT) == 0)
    {
    	sleep(1);
	    if(open_port_once(ioflush) != 0)
	    {
	        dbg_time("Start to open port, Failed!\n");
	        return false;
	    }
	    return 0;
    }else
    {
    	dbg_time("Can't find diagnose port. upgrade interrupt.\n");
    	return -1;
    }
    return -2;
}
/*
*/
static int close_and_reopen_without_wait(int ioflush, int disconnect_wait_timeout, int connect_wait_timeout)
{
/*
some other 9x07 platform, host send done packet to module, the module have not shutdown usb port,
the port (ttyUSB0) will not disconnect, so wait is wasted time. simple sleep 5 seconds
*/
	closeport();
	if(wait_diag_port_disconnect(disconnect_wait_timeout) == 0)
	{
		dbg_time("Diagnose port disconnect\n");
	}
	else
	{
		dbg_time("Warning: Diagnose port may be exist always.\n");
	}
	if(detect_diag_port_timeout(connect_wait_timeout) == 0)
	{
		sleep(1);
		if(open_port_once(ioflush) != 0)
		{
			dbg_time("Start to open port, Failed!\n");
			return false;
		}
		return 0;
	}else
	{
		dbg_time("Can't find diagnose port. upgrade interrupt.\n");
		return -1;
	}
	return -2;
}

#define QL_MAX_INF 10

struct ql_usb_device
{
    char fname[64];
    int desc;
    __le16 idVendor;
    __le16 idProduct;
    __u8 bNumInterfaces;
    __u8 intr_ep[QL_MAX_INF];
    __u8 bulk_ep_in[QL_MAX_INF];
    __u8 bulk_ep_out[QL_MAX_INF];
    __le16 wMaxPacketSize[QL_MAX_INF];
    int control[QL_MAX_INF][2];
    struct usbdevfs_urb bulk_in[QL_MAX_INF];
    struct usbdevfs_urb bulk_out[QL_MAX_INF];
};
static inline int badname(const char *name) {
    while(*name) {
        if(!isdigit(*name++)) return 1;
    }
    return 0;
}

static struct ql_usb_device ql_device;
static void quectel_find_usb_device(struct ql_usb_device *udev) {
    const char *base = "/dev/bus/usb";
    char busname[64], devname[64];
    DIR *busdir , *devdir ;
    struct dirent *de;
    int fd ;

    busdir = opendir(base);
    if (busdir == 0) return;

    while ((de = readdir(busdir)) != 0) {
        if(badname(de->d_name)) continue;

        sprintf(busname, "%s/%s", base, de->d_name);
        devdir = opendir(busname);
        if(devdir == 0) continue;

        while ((de = readdir(devdir))) {
            unsigned char devdesc[1024];
            size_t desclength;
            size_t len;
            __u8 bInterfaceNumber = 0;

            if (badname(de->d_name)) continue;
            sprintf(devname, "%s/%s", busname, de->d_name);

            if ((fd = open(devname, O_RDWR | O_NOCTTY)) < 0) {
                continue;
            }

            desclength = read(fd, devdesc, sizeof(devdesc));

            len = 0;
            while (len < desclength) {
                struct usb_descriptor_header *h = (struct usb_descriptor_header *)(&devdesc[len]);

                if (h->bLength == sizeof(struct usb_device_descriptor) && h->bDescriptorType == USB_DT_DEVICE) {
                    struct usb_device_descriptor *device = (struct usb_device_descriptor *)h;

                    if (device->idVendor == 0x2c7c || (device->idVendor == 0x05c6 && device->idProduct == 0x9008)) {
                        udev->idVendor = device->idVendor;
                        udev->idProduct = device->idProduct;
                    } else {
                        break;
                    }
                    dbg_time("D: %s idVendor=%04x idProduct=%04x\n", devname, device->idVendor, device->idProduct);
                }
                else if (h->bLength == sizeof(struct usb_config_descriptor) && h->bDescriptorType == USB_DT_CONFIG) {
                    struct usb_config_descriptor *config = (struct usb_config_descriptor *)h;

                    dbg_time("C: %s bNumInterfaces: %d\n", devname, config->bNumInterfaces);
                    udev->bNumInterfaces = config->bNumInterfaces;
                }
                else if (h->bLength == sizeof(struct usb_interface_descriptor) && h->bDescriptorType == USB_DT_INTERFACE) {
                    struct usb_interface_descriptor *interface = (struct usb_interface_descriptor *)h;

                    dbg_time("I: If#= %d Alt= %d #EPs= %d Cls=%02x Sub=%02x Prot=%02x\n",
                        interface->bInterfaceNumber, interface->bAlternateSetting, interface->bNumEndpoints,
                        interface->bInterfaceClass, interface->bInterfaceSubClass, interface->bInterfaceProtocol);
                    bInterfaceNumber = interface->bInterfaceNumber;
                }
                else if (h->bLength == USB_DT_ENDPOINT_SIZE && h->bDescriptorType == USB_DT_ENDPOINT) {
                    struct usb_endpoint_descriptor *endpoint = (struct usb_endpoint_descriptor *)h;

                    dbg_time("E: Ad=%02x Atr=%02x MxPS= %d Ivl=%dms\n",
                        endpoint->bEndpointAddress, endpoint->bmAttributes, endpoint->wMaxPacketSize, endpoint->bInterval);

                    if ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK) {
                        if (endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK)
                            udev->bulk_ep_in[bInterfaceNumber] = endpoint->bEndpointAddress;
                        else
                            udev->bulk_ep_out[bInterfaceNumber] = endpoint->bEndpointAddress;
                        udev->wMaxPacketSize[bInterfaceNumber] = endpoint->wMaxPacketSize;
                    } else if ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT){
                        udev->intr_ep[bInterfaceNumber] = endpoint->bEndpointAddress;
                    }
                } else if ((h->bLength == 4 || h->bLength == 5)  && h->bDescriptorType == USB_DT_CS_INTERFACE) {
                
                } else {
                    dbg_time("unknow bLength=%d bDescriptorType=%d\n", h->bLength, h->bDescriptorType);
                    break;                        
                }

                len += h->bLength;
            }

            if (len == desclength) {
                strcpy(udev->fname, devname);
                udev->desc = fd;
                break;
            }
            
            close(fd);
        } // end of devdir while
        
        closedir(devdir);

        if (udev->desc) {
            break;
        }
    } //end of busdir while
    
    closedir(busdir);
}

static int qusb_find_emergency_port(int *idVendor, int *idProduct, int *interfaceNum) {
    quectel_find_usb_device(&ql_device);
    if (ql_device.desc) {
        *idVendor = ql_device.idVendor;
        *idProduct = ql_device.idProduct;
        *interfaceNum = ql_device.bNumInterfaces;
        close(ql_device.desc);
        memset(&ql_device, 0, sizeof(ql_device));
        return 0;
    }

    return ENODEV;
}
static FILE * ql_popen(const char *program, const char *type)
{
    FILE *iop;
    int pdes[2];
    pid_t pid;
    char *argv[20];
    int argc = 0;
    char *dup_program = strdup(program);
    char *pos = dup_program;
	
    while (*pos != '\0')
    {
        while (isblank(*pos)) *pos++ = '\0';
        if (*pos != '\0')
        {
            argv[argc++] = pos;
            while (*pos != '\0' && !isblank(*pos)) pos++;
        }
    }
    argv[argc++] = NULL;

    if (pipe(pdes) < 0) {
        return (NULL);
    }

    switch (pid = fork()) {
    case -1:            /* Error. */
        (void)close(pdes[0]);
        (void)close(pdes[1]);
        return (NULL);
        /* NOTREACHED */
    case 0:                /* Child. */
        {
        if (*type == 'r') {
            (void) close(pdes[0]);
            if (pdes[1] != STDOUT_FILENO) {
                (void)dup2(pdes[1], STDOUT_FILENO);
                (void)close(pdes[1]);
            }
        } else {
            (void)close(pdes[1]);
            if (pdes[0] != STDIN_FILENO) {
                (void)dup2(pdes[0], STDIN_FILENO);
                (void)close(pdes[0]);
            }
        }
        execvp(argv[0], argv);
        _exit(127);
        /* NOTREACHED */
        }
           break;
       default:
            free(dup_program);
       break;
    }

    /* Parent; assume fdopen can't fail. */
    if (*type == 'r') {
        iop = fdopen(pdes[0], type);
        (void)close(pdes[1]);
    } else {
        iop = fdopen(pdes[1], type);
        (void)close(pdes[0]);
    }
    return (iop);
}

void dump_sys_info()
{
	FILE * fpin = NULL;
	char line[MAX_PATH];

	//dump dmesg 
	fpin = ql_popen("dmesg", "r");
	if (!fpin) {
		dbg_time("popen failed\n");
		dbg_time("popen strerror: %s\n", strerror(errno));
		return;
	}

	while (fgets(line, MAX_PATH - 1, fpin) != NULL) {
		dbg_time("%s", line);		
	}
	ql_pclose(fpin);
	fpin = NULL;	
	
	//dump /sys/kernel/debug/usb/devices

	fpin = ql_popen("cat /sys/kernel/debug/usb/devices", "r");
	if (!fpin) {
		dbg_time("popen failed\n");
		dbg_time("popen strerror: %s\n", strerror(errno));
		return;
	}

	while (fgets(line, MAX_PATH - 1, fpin) != NULL) {
		dbg_time("%s", line);		
	}
	ql_pclose(fpin);
	
	fpin = NULL;
	
}

static void rmmod_qcserial()
{
	FILE * fpin = NULL;
	fpin = ql_popen("rmmod qcserial", "r");
	ql_pclose(fpin);
}
static void modprobe_qcserial()
{
	FILE * fpin = NULL;
	fpin = ql_popen("modprobe qcserial", "r");
	ql_pclose(fpin);
}
int process_firehose_upgrade(download_context* ctx_ptr)
{
	int ret = -1;
	int idVendor = 0, idProduct = 0, interfaceNum = 0;
	//first check in emergency mode.
	qusb_find_emergency_port(&idVendor, &idProduct, &interfaceNum);
	//rmmod_qcserial();
	if (idVendor != 0x05c6 || idProduct != 0x9008 || interfaceNum != 1) {
        //system have no emengency port or in normal mode.(Quectel DM)
        if(open_port_once(1) != 0)
	    {
	        dbg_time("Start to open port, Failed!\n");
	        return false;
	    }
	    ignore_dirty_data();
		ret = switch_emergency_download();
		if(ret != 0)
		{
			dbg_time("switch to firehose download mode failed.\n");
			return 0;
		}
		if( close_and_reopen_without_wait(0, 3, 10) != 0)
		{
			return -1;
		}
    }else
    {
    	//nothing ,just cann qfirehose -f path
    }
	
	if(do_firehose("qfirehose", ctx_ptr->firehose_path) == 0)
	{
		//modprobe_qcserial();
		return 1;
	}else
	{
		return 0;
	}
	//modprobe_qcserial();
	return 1;
}

int process_at_fastboot_upgrade(download_context* ctx_ptr)
{
	int modem_fd;
	ATResponse *p_response = NULL;
	int err;
	char dev_path[MAX_PATH];

#if 0
	if(detect_modem_port(&ctx_ptr->modem_port) == 0)
	{		
		dbg_time("Auto detect Quectel modem port = %s\n", ctx_ptr->modem_port);
		sleep(1);
	}else
	{
		dbg_time("Auto detect Quectel modem port failed.\n");
		return false;
	}
#endif	
	sprintf(dev_path, "/dev/%s", ctx_ptr->modem_port);
	modem_fd = serial_open(dev_path);
	if (modem_fd < 0)
	{
		dbg_time("Fail to open %s, errrno : %d (%s)\n", dev_path, errno, strerror(errno));
		return false;
	}
	
	at_set_on_reader_closed(onATReaderClosed);
	at_set_on_timeout(onATTimeout, 15000);

	at_send_command("ATE0Q0V1", NULL);
	err = at_send_command_multiline("ATI;+CSUB;+CVERSION", "\0", &p_response);
	if (err < 0 || p_response == NULL || p_response->success == 0) {
		dbg_time("Fail to send cmd  ATI, errrno : %d (%s)\n", errno, strerror(errno));
		return false;
	} 
	if (!err && p_response && p_response->success) {
		ATLine *p_cur = p_response->p_intermediates;
		while (p_cur) {
			p_cur = p_cur->p_next;
		}
	}
	at_response_free(p_response);
	if(AT_ERROR_CHANNEL_CLOSED == at_send_command("AT+qfastboot", NULL))
	{
		close(modem_fd);			
		dbg_time("going to fastboot modle ...\n");
		sleep(3);
	}else
	{
		dbg_time("%s\n",not_support_at_fastboot);
	}

	if(wait_adb(DETECT_DEV_TIMEOUT) == 0)
	{
		sleep(3);
		if(do_fastboot_download_direct(ctx_ptr) != 0)
		{
			return false;
		}else
		{
			return true;
		}
	}else
	{
		dbg_time("Can't find adb port, upgrade failed.\n");
		return false;
	}
	return false;
}

int vertifyAllnum(char* ch)
{
    int re=1;
    int i;
    for (i=0;i<strlen(ch);i++)
    {
        if(isdigit(*(ch+i))==0)
        {
            return 0;
        }
    }
    return re;
}


void Resolve_port(char *chPort,int* nPort )
{
    *nPort=-1;
    char string[7];
    char chPortNum[10];
    strncpy(string,chPort,(sizeof("ttyUSB")-1));
    string[(sizeof("ttyUSB")-1)]='\0';

    if(strlen(chPort)<sizeof("ttyUSB**"))
    {
        if(strcmp(string,"ttyUSB")==0)
        {
            memset(chPortNum,0,sizeof(chPortNum));
            memcpy(chPortNum,chPort+(sizeof("ttyUSB")-1),(strlen(chPort)-(sizeof("ttyUSB")-1)));
            if(vertifyAllnum(chPortNum)&&*chPortNum!=0)
            {
                *nPort=atoi(chPortNum);
            }
        }
    }
}


int process_streaming_fastboot_upgarde(download_context *ctx_ptr)
{
	int sync_timeout=15;
	int timeout = 10;
	int get_hello_packet = 0;
	int re;
	int direct_fastboot = 0;
	int ret = 0;
	int emergency_mode = 0;
	int emergency_diag_port = 0;
	
    if (ctx_ptr->update_method == 0 && !detect_adb()) {
		if(!do_fastboot_download_direct(ctx_ptr))
		{
			return true;
		}else
		{
			return false;
		}		
	}
__normal_download_:	
    //open port without ioflush    
    if(open_port_once(0) != 0)
    {
        dbg_time("Start to open port, Failed!\n");
        return false;
    }
    if(!is_emergency_diag_port())
    {
    	emergency_diag_port = 1;
    }else
    {
    	emergency_diag_port = 0;
    }
    emergency_diag_port == 1?dbg_time("Use emergency diag port\n"):dbg_time("Use normal diag port\n");
	dbg_time("Get sahara hello packet!\n");
	if(get_sahara_hello_packet() == 0)
	{
		/*
		Note: some kernel , the kernel will send 5E and other byte to ttyUSB0, module will response with hello packet.
		read it and clear rx buffer.
		*/
		get_hello_packet = 1;
		sleep(3);
		ignore_dirty_data();
		goto sahara_get_hello;
	}else
	{
		dbg_time("Get sahara hello packet failed.\n");
		if(1 == emergency_diag_port)
			goto sahara_get_hello;
	}

	dbg_time("Detect module status!\n");
 
    if (module_state(ctx_ptr) == 0)
    {
        return false;
    }

    if (ctx_ptr->TargetState == STATE_NORMAL_MODE) {

		retrieve_soft_revision();    
        dbg_time("Switch to PRG status\n");
        if (switch_to_dload() != 0) {
            dbg_time("Switch to PRG status failed\n");
            return false;
        }
        
        if( close_and_reopen(0) != 0)
        {
        	return false;
        }
    }
    else if(ctx_ptr->TargetState == STATE_SAHARA_MODE)
    {
        goto sahara_download;
    }
    else if (ctx_ptr->TargetState == STATE_GOING_MODE)
    {
        goto stream_download;
    }
    else
    {
        dbg_time("Get sahara hello packet failed!\n");
        return false;
    }
    
sahara_download:
	dbg_time("Try get sahara hello packet!\n");
	if(get_sahara_hello_packet() == 0)
	{
		dbg_time("Get sahara hello packet successfully!\n");
	}else
	{
		dbg_time("Get sahara hello packet failed!\n");
	}
    //2.send hello response packet

sahara_get_hello:
    
	if(get_hello_packet == 1)
	{
		get_hello_packet = 0;
		dbg_time("Send sahara hello response packet(1)!\n");
		if(SendHelloPacketTest(emergency_diag_port)==false)
		{
			dbg_time("Send sahara hello response packet failed!\n");
			return false;
		}
	}
    else{
		dbg_time("Send sahara hello response packet(2)!\n");
		if(SendHelloPacketTest(emergency_diag_port)==false)
		{
			dbg_time("Send sahara hello response packet failed!\n");
			return false;
		}
	}

    dbg_time("Start Read Data!\n");
    if(ctx_ptr->platform == platform_9x06)
    {
    	re = GetReadDataPacket(&emergency_mode, 1024);
    }else
    {
    	re = GetReadDataPacket(&emergency_mode, 1024 * 4);
    }
	
	if(re == 2)
	{
		get_hello_packet = 1;
		goto sahara_get_hello;
	}

    if(re == false)
    {
        return false;
    }
    
    dbg_time("Send sahara do packet!\n");
    if(send_sahara_do_packet() != 0)
    {
        dbg_time("Send Do packet failed!\n");
        return false;
    }

    //////////////////////////////////////////////////////////////////////////////////////////
    
    dbg_time("Module Status Detection\n");
    emergency_mode == 1?dbg_time("Upgrade in emergency mode\n"):dbg_time("Upgrade in normal mode\n");
    /*
    QFlash send do packet. 
    Module Actions(Normal):
    1. ttyUSB disconnect.
    2. run NPRG.mbn
    3. ttyUSB connect.    
    Module Actions(Emergency Mode):
    1. run ENPRG.mbn
    */
    if( emergency_mode == 1)
    {
    	if(close_and_reopen_without_wait(1, 5, 30) != 0)
    		return false;
    }else
    {
    	if(close_and_reopen_without_wait(1, 15, 30) != 0)
    		return false;
    }    

    if (module_state(ctx_ptr) == 0)
    {
        return false;
    }

stream_download:
    if (ctx_ptr->TargetState == STATE_GOING_MODE) {
        dbg_time("Start to download firmware\n");
        if (handle_hello() == false) {
            dbg_time("Send hello command fail\n");
            return false;
        } 
		/*
		hello packet will set dload flag in module, when upgrade interrup, restart module,module will enter dm(quectel sbl)
		*/
        if (handle_security_mode(1) == false) {
            dbg_time("Send trust command fail\n");
            return false;
        }

        if (handle_parti_tbl(0) == false) {
            dbg_time("----------------------------------\n");
            dbg_time("Detect partition mismatch.\n");
            dbg_time("Download parition with override.\n");
            dbg_time("----------------------------------\n");
            if(handle_parti_tbl(1) == false)
            {
            	dbg_time("override failed. \n");	
            	return false;
            }
            /*
            partition is not match, the download flag will be clear, so set it again, reset will clear it
            */
            if(handle_quectel_download_flag(1) == false)
            {
            	dbg_time("Set Quectel download flag failed\n");
            }else
            {
            	dbg_time("Set Quectel download flag successfully\n");
            }
            
        }
        dump = 0;

        if(ctx_ptr->update_method == 0)  //fastboot module
		{		           
            if(do_fastboot_download(ctx_ptr) != 0)
            {
            	return false;
            }
		}
        else if(ctx_ptr->update_method == 1)
        {
			if(do_streaming_download(ctx_ptr) != 0)
			{
				return false;
			}
        }

    }else if(STATE_NORMAL_MODE == ctx_ptr->TargetState)
    {
    	dbg_time("Module in normal state, do download again.\n");
    	closeport();
    	//return false;
    	goto __normal_download_;
    }else
    {
    	dbg_time("Module state is invalid, upgrade failed.\n");
    	return false;
    }

    dbg_time("The device restart...\n");
	dbg_time("Welcome to use the Quectel module!!!\n");
    return true;
}

int do_streaming_download(download_context *pQdlContext)
{
	int ret = 0;
	ret = do_flash_mbn("0:misc", "misc");
	if(ret != 1)
	{
		return -1;
	}
	for (std::vector<Ufile>::iterator iter = pQdlContext->ufile_list.begin();iter != pQdlContext->ufile_list.end();iter++)  
    {  
        if(strcmp("0:MIBIB",((Ufile)*iter).name)!=0)
		{
			//gettimeofday(&start,NULL);
			ret = do_flash_mbn(((Ufile)*iter).name, ((Ufile)*iter).img_name);
			//gettimeofday(&end,NULL);
			if(ret==false)
			{
				dbg_time("down file:%s is faliled\n",((Ufile)*iter).name);
				return -2;
			}
		}
    }

    if (handle_reset() == false) {
        dbg_time("Send reset command failed\n");
        return -1;
    }
    return 0;
}
int do_fastboot_download_direct(download_context *pQdlContext)
{
	ignore_sahara_stage_files(pQdlContext);
	if(downloadfastboot(pQdlContext) != 0)
	{
		return 1;
	}
	return 0;
}
int do_fastboot_download(download_context *pQdlContext)
{
	int ret = 0;

	ret = do_flash_mbn("0:misc", "misc");
	if(ret != 1)
	{
		return -1;
	}
	for (std::vector<Ufile>::iterator iter = pQdlContext->ufile_list.begin();
            		iter!=pQdlContext->ufile_list.end();/*iter++*/) 
    {
    	if(strcmp("0:MIBIB",((Ufile)*iter).name)!=0)
		{
		
			if(strstr(((Ufile)*iter).name,"0:TZ") || strstr(((Ufile)*iter).name,"0:RPM") || strstr(((Ufile)*iter).name,"0:SBL") || strstr(((Ufile)*iter).name,"0:aboot"))
			{
				ret = do_flash_mbn(((Ufile)*iter).name, ((Ufile)*iter).img_name);
#ifdef PROGRESS_FILE_FAETURE				
				constantly_schedule(((Ufile)*iter).img_name);
#endif				
		    	if(ret != 1)
		    	{
		    		return -1;
		    	}
			    free_ufile((*iter));			    
			    iter = pQdlContext->ufile_list.erase(iter);
			}								
	        else
	        {
	        	iter++;
	        }
		}else
	    {
	    	iter++;
	    }
    }            

    dbg_time("Change to fastboot mode...\n"); 
    do_flash_mbn("0:boot", "invalid-boot\n");  //write invalid boot for run fastboot
    sleep(1);
	if (handle_reset() == false) {
		dbg_time("Warning: reset response not received.\n");					
	}
	closeport();
	if(wait_adb(DETECT_DEV_TIMEOUT) == 0)
	{
		if(downloadfastboot(pQdlContext) != 0)
		{
			return -3;
		}else
		{
			//upgrade success
			return 0;
		}
	}else
	{
		dbg_time("Can't find adb port, upgrade failed.\n");
		return 1;
	}
	return 0;
}

void free_ufile(Ufile ufile)
{
	if( ufile.name != NULL)
	{
		free(ufile.name);
	}
	if(ufile.img_name != NULL)
	{
		free(ufile.img_name);
	}
	if(ufile.partition_name != NULL)
	{
		free(ufile.partition_name);
	}
}

