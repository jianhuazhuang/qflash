/*
 * Copyright (C) 2008 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>
#include <time.h>

#include <sys/time.h>
#include <sys/sysinfo.h>

#include "fastboot.h"

static char line[1024];

static const char * get_time(void) {
    static char time_buf[50];
    struct timeval  tv;
    time_t time;
    suseconds_t millitm;
    struct tm *ti;

    gettimeofday (&tv, NULL);

    time= tv.tv_sec;
    millitm = (tv.tv_usec + 500) / 1000;

    if (millitm == 1000) {
        ++time;
        millitm = 0;
    }

    ti = localtime(&time);
    sprintf(time_buf, "[%02d-%02d_%02d:%02d:%02d:%03d]", ti->tm_mon+1, ti->tm_mday, ti->tm_hour, ti->tm_min, ti->tm_sec, (int)millitm);
    return time_buf;
}

void dbg_time (const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    snprintf(line, sizeof(line), "%s ", get_time());
    vsnprintf(line + strlen(line), sizeof(line) - strlen(line), fmt, args);
    fprintf(stdout, "%s", line);
}

char cur_product[FB_RESPONSE_SZ + 1];

static usb_handle *usb = 0;
static const char *serial = 0;
static const char *product = 0;
static const char *cmdline = 0;
static int wipe_data = 0;
static unsigned short vendor_id = 0;
int endian_flag;


static unsigned base_addr = 0x10000000;

void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr,"error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr,"\n");
    va_end(ap);
    exit(1);
}

void get_my_path(char *path);

char *find_item(const char *item, const char *product)
{
    char *dir;
    char *fn;
    char path[PATH_MAX + 128];

    if(!strcmp(item,"boot")) {
        fn = "boot.img";
    } else if(!strcmp(item,"recovery")) {
        fn = "recovery.img";
    } else if(!strcmp(item,"system")) {
        fn = "system.img";
    } else if(!strcmp(item,"userdata")) {
        fn = "userdata.img";
    } else if(!strcmp(item,"info")) {
        fn = "android-info.txt";
    } else {
        fprintf(stderr,"unknown partition '%s'\n", item);
        return 0;
    }

    if(product) {
        get_my_path(path);
        sprintf(path + strlen(path),
                "../../../target/product/%s/%s", product, fn);
        return strdup(path);
    }

    dir = getenv("ANDROID_PRODUCT_OUT");
    if((dir == 0) || (dir[0] == 0)) {
        die("neither -p product specified nor ANDROID_PRODUCT_OUT set");
        return 0;
    }

    sprintf(path, "%s/%s", dir, fn);
    return strdup(path);
}

#define RAM_FIXED_VALUE		256
static int get_total_ram()
{
	int err;
	struct sysinfo s_info = {0};
	
	err = sysinfo(&s_info);
	if(err == 0)
	{
		return s_info.totalram >> 20;
	}else
	{//default use malloc
		return RAM_FIXED_VALUE + 50;
	}
}

extern int dump;

#ifdef _WIN32
void *load_file(const char *fn, unsigned *_sz, int *out_fd);
#else
void *load_file(const char *fn, unsigned *_sz, int *out_fd)
{
    char *data;
    int sz;
    int fd;

    data = 0;
    fd = open(fn, O_RDONLY);
    if(fd < 0) return 0;

    sz = lseek(fd, 0, SEEK_END);
    if(sz < 0) goto oops;

    if(lseek(fd, 0, SEEK_SET) != 0) goto oops;

#ifdef ANDROID
	//android platfrom ,use malloc
	data = (char*) malloc(sz);
#else
	//embedded platfrom, first detect total memory, low memory use file desciptor(openwrt)
#if 0
	if(RAM_FIXED_VALUE > get_total_ram())	
#else
	if(1)
#endif
	{
		//dbg_time("Small memory, use file descriptor.\n");
		data = 0;
	}else{		
    	data = (char*) malloc(sz);
    }
#endif
#if 0	//for test
	if(data)
	{
		free(data);
		data = 0;
	}
#endif
    if(data == 0) goto oops;

    if(read(fd, data, sz) != sz) goto oops;
    close(fd);

    if(_sz) *_sz = sz;
    return data;

oops:
	if(data == 0)
	{// have no enough memory for file
		*out_fd = fd;
		if(_sz) *_sz = sz;
		return 0;
	}else
	{
    	close(fd);
    }
    if(data != 0) free(data);
    return 0;
}
#endif

int match_fastboot_with_serial(usb_ifc_info *info, const char *local_serial)
{
    if(!(vendor_id && (info->dev_vendor == vendor_id)) &&
       (info->dev_vendor != 0x18d1) &&  // Google
       (info->dev_vendor != 0x8087) &&  // Intel
       (info->dev_vendor != 0x0451) &&
       (info->dev_vendor != 0x0502) &&
       (info->dev_vendor != 0x0fce) &&  // Sony Ericsson
       (info->dev_vendor != 0x05c6) &&  // Qualcomm
       (info->dev_vendor != 0x22b8) &&  // Motorola
       (info->dev_vendor != 0x0955) &&  // Nvidia
       (info->dev_vendor != 0x413c) &&  // DELL
       (info->dev_vendor != 0x2314) &&  // INQ Mobile
       (info->dev_vendor != 0x0b05) &&  // Asus
       (info->dev_vendor != 0x0bb4))    // HTC
            return -1;
    if(info->ifc_class != 0xff) return -1;
    if(info->ifc_subclass != 0x42) return -1;
    if(info->ifc_protocol != 0x03) return -1;
    // require matching serial number or device path if requested
    // at the command line with the -s option.
    if (local_serial && (strcmp(local_serial, info->serial_number) != 0 &&
                   strcmp(local_serial, info->device_path) != 0)) return -1;
    return 0;
}

int match_fastboot(usb_ifc_info *info)
{
    return match_fastboot_with_serial(info, serial);
}

int list_devices_callback(usb_ifc_info *info)
{
    if (match_fastboot(info) == 0) {
        char* serial = info->serial_number;
        if (!info->writable) {
            serial = "no permissions"; // like "adb devices"
        }
        if (!serial[0]) {
            serial = "????????????";
        }
        // output compatible with "adb devices"
        dbg_time("%s\tfastboot\n", serial);
    }

    return -1;
}
extern usb_handle *usb_open(ifc_match_func callback);

usb_handle *open_device(void)
{
    static usb_handle *usb = 0;
    int announce = 1;

    if(usb) return usb;

    for(;;) {
        usb = usb_open(match_fastboot);
        if(usb) return usb;
        if(announce) {
            announce = 0;
            //fprintf(stderr,"< waiting for device >\n");
            return NULL;
        }
        sleep(1);
    }
}

void list_devices(void) {
    // We don't actually open a USB device here,
    // just getting our callback called so we can
    // list all the connected devices.
    usb_open(list_devices_callback);
}

void usage(void)
{
    fprintf(stderr,
/*           1234567890123456789012345678901234567890123456789012345678901234567890123456 */
            "usage: fastboot [ <option> ] <command>\n"
            "\n"
            "commands:\n"
            "  update <filename>                        reflash device from update.zip\n"
            "  flashall                                 flash boot + recovery + system\n"
            "  flash <partition> [ <filename> ]         write a file to a flash partition\n"
            "  erase <partition>                        erase a flash partition\n"
            "  getvar <variable>                        display a bootloader variable\n"
            "  boot <kernel> [ <ramdisk> ]              download and boot kernel\n"
            "  flash:raw boot <kernel> [ <ramdisk> ]    create bootimage and flash it\n"
            "  devices                                  list all connected devices\n"
            "  continue                                 continue with autoboot\n"
            "  reboot                                   reboot device normally\n"
            "  reboot-bootloader                        reboot device into bootloader\n"
            "  help                                     show this help message\n"
            "\n"
            "options:\n"
            "  -w                                       erase userdata and cache\n"
            "  -s <serial number>                       specify device serial number\n"
            "  -p <product>                             specify product name\n"
            "  -c <cmdline>                             override kernel commandline\n"
            "  -i <vendor id>                           specify a custom USB vendor id\n"
            "  -b <base_addr>                           specify a custom kernel base address\n"
            "  -n <page size>                           specify the nand page size. default: 2048\n"
        );
}

static char *strip(char *s)
{
    int n;
    while(*s && isspace(*s)) s++;
    n = strlen(s);
    while(n-- > 0) {
        if(!isspace(s[n])) break;
        s[n] = 0;
    }
    return s;
}

#define MAX_OPTIONS 32
static int setup_requirement_line(char *name)
{
    char *val[MAX_OPTIONS];
    const char **out;
    char *prod = NULL;
    unsigned n, count;
    char *x;
    int invert = 0;

    if (!strncmp(name, "reject ", 7)) {
        name += 7;
        invert = 1;
    } else if (!strncmp(name, "require ", 8)) {
        name += 8;
        invert = 0;
    } else if (!strncmp(name, "require-for-product:", 20)) {
        // Get the product and point name past it
        prod = name + 20;
        name = strchr(name, ' ');
        if (!name) return -1;
        *name = 0;
        name += 1;
        invert = 0;
    }

    x = strchr(name, '=');
    if (x == 0) return 0;
    *x = 0;
    val[0] = x + 1;

    for(count = 1; count < MAX_OPTIONS; count++) {
        x = strchr(val[count - 1],'|');
        if (x == 0) break;
        *x = 0;
        val[count] = x + 1;
    }

    name = strip(name);
    for(n = 0; n < count; n++) val[n] = strip(val[n]);

    name = strip(name);
    if (name == 0) return -1;

        /* work around an unfortunate name mismatch */
    if (!strcmp(name,"board")) name = "product";

    out = malloc(sizeof(char*) * count);
    if (out == 0) return -1;

    for(n = 0; n < count; n++) {
        out[n] = strdup(strip(val[n]));
        if (out[n] == 0) return -1;
    }

    fb_queue_require(prod, name, invert, n, out);
    return 0;
}

static void setup_requirements(char *data, unsigned sz)
{
    char *s;

    s = data;
    while (sz-- > 0) {
        if(*s == '\n') {
            *s++ = 0;
            if (setup_requirement_line(data)) {
                die("out of memory");
            }
            data = s;
        } else {
            s++;
        }
    }
}

void queue_info_dump(void)
{
    fb_queue_notice("--------------------------------------------");
    fb_queue_display("version-bootloader", "Bootloader Version...");
    fb_queue_display("version-baseband",   "Baseband Version.....");
    fb_queue_display("serialno",           "Serial Number........");
    fb_queue_notice("--------------------------------------------");
}

#define skip(n) do { argc -= (n); argv += (n); } while (0)
#define require(n) do { if (argc < (n)) {usage(); exit(1);}} while (0)

int do_oem_command(int argc, char **argv)
{
    int i;
    char command[256];
    if (argc <= 1) return 0;

    command[0] = 0;
    while(1) {
        strcat(command,*argv);
        skip(1);
        if(argc == 0) break;
        strcat(command," ");
    }

    fb_queue_command(command,"");
    return 0;
}
int checkCPU()
{
	short int test = 0x1234;
	if(*((char *)&test)==0x12)
	{
		return 1;
	}
	return 0;
}


#ifdef USE_FASTBOOT
int fastboot_main(int argc, char **argv)
#else
int main(int argc, char **argv)
#endif
{
    int wants_wipe = 0;
    int wants_reboot = 0;
    int wants_reboot_bootloader = 0;
    void *data;
    unsigned sz;
    unsigned page_size = 2048;
    int status;
    int fd;

    /******************check the CPU is Big endian**************************************************/
	if(checkCPU())
	{
		endian_flag = 1;

	}

    skip(1);
    if (argc == 0) {
        usage();
        return 1;
    }

    if (!strcmp(*argv, "devices")) {
        list_devices();
        return 0;
    }

    if (!strcmp(*argv, "help")) {
        usage();
        return 0;
    }


    serial = getenv("ANDROID_SERIAL");

    while (argc > 0) {
        if(!strcmp(*argv, "-w")) {
            wants_wipe = 1;
            skip(1);
        } else if(!strcmp(*argv, "-b")) {
            require(2);
            base_addr = strtoul(argv[1], 0, 16);
            skip(2);
        } else if(!strcmp(*argv, "-n")) {
            require(2);
            page_size = (unsigned)strtoul(argv[1], NULL, 0);
            if (!page_size) die("invalid page size");
            skip(2);
        } else if(!strcmp(*argv, "-s")) {
            require(2);
            serial = argv[1];
            skip(2);
        } else if(!strcmp(*argv, "-p")) {
            require(2);
            product = argv[1];
            skip(2);
        } else if(!strcmp(*argv, "-c")) {
            require(2);
            cmdline = argv[1];
            skip(2);
        } else if(!strcmp(*argv, "-i")) {
            char *endptr = NULL;
            unsigned long val;

            require(2);
            val = strtoul(argv[1], &endptr, 0);
            if (!endptr || *endptr != '\0' || (val & ~0xffff))
                die("invalid vendor id '%s'", argv[1]);
            vendor_id = (unsigned short)val;
            skip(2);
        } else if(!strcmp(*argv, "getvar")) {
            require(2);
            fb_queue_display(argv[1], argv[1]);
            skip(2);
        } else if(!strcmp(*argv, "erase")) {
            require(2);
            fb_queue_erase(argv[1]);
            skip(2);
        } else if(!strcmp(*argv, "signature")) {
            require(2);
            data = load_file(argv[1], &sz, 0);
            if (data == 0) die("could not load '%s'", argv[1]);
            if (sz != 256) die("signature must be 256 bytes");
            fb_queue_download("signature", data, sz);
            fb_queue_command("signature", "installing signature");
            skip(2);
        } else if(!strcmp(*argv, "reboot")) {
            wants_reboot = 1;
            skip(1);
        } else if(!strcmp(*argv, "reboot-bootloader")) {
            wants_reboot_bootloader = 1;
            skip(1);
        } else if (!strcmp(*argv, "continue")) {
            fb_queue_command("continue", "resuming boot");
            skip(1);
        } else if(!strcmp(*argv, "flash")) {
            char *pname = argv[1];
            char *fname = 0;
            require(2);
            if (argc > 2) {
                fname = argv[2];
                skip(3);
            } else {
                fname = find_item(pname, product);
                skip(2);
            }
            if (fname == 0) die("cannot determine image filename for '%s'", pname);
            fd = 0;
            data = load_file(fname, &sz, &fd);
            if (data == 0 && fd == 0) die("cannot load '%s'\n", fname);
            fb_queue_flash(pname, data, sz, fd);
        } else if(!strcmp(*argv, "oem")) {
            argc = do_oem_command(argc, argv);
        } else {
            usage();
            return 1;
        }
    }

    if (wants_wipe) {
        fb_queue_erase("userdata");
        fb_queue_erase("cache");
    }
    if (wants_reboot) {
        fb_queue_reboot();
    } else if (wants_reboot_bootloader) {
        fb_queue_command("reboot-bootloader", "rebooting into bootloader");
    }

    usb = open_device();
    if(NULL == usb)
    {
    	die("cannot open device");
    }

    status = fb_execute_queue(usb);
    fb_lqueue_destroy();
    return (status) ? 1 : 0;
}
