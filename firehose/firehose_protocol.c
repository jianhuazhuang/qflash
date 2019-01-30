#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

extern void dbg_time (const char *fmt, ...);
#define error_return()  do {dbg_time("%s %s %d fail\n", __FILE__, __func__, __LINE__); return __LINE__; } while(0)

int qusb_open(char *port_name);
int qusb_close(int fd);
ssize_t qusb_read(int fd, void* pbuf, size_t size);
ssize_t qusb_write(int fd, const void*pbuf, size_t size, int need_zlp);

struct fh_configure_cmd {
    const char *type;
    const char *MemoryName;
    uint32_t Verbose;
    uint32_t AlwaysValidate;
    uint32_t MaxDigestTableSizeInBytes;
    uint32_t MaxPayloadSizeToTargetInBytes;
    uint32_t MaxPayloadSizeFromTargetInBytes ; 			//2048
    uint32_t MaxPayloadSizeToTargetInByteSupported;		//16k
    uint32_t ZlpAwareHost;
    uint32_t SkipStorageInit;
};

struct fh_erase_cmd {
    const char *type;
    uint32_t PAGES_PER_BLOCK;
    uint32_t SECTOR_SIZE_IN_BYTES;
    uint32_t num_partition_sectors;
    uint32_t physical_partition_number;
    uint32_t start_sector;
};

struct fh_program_cmd {
    const char *type;
    const char *filename;
    uint32_t PAGES_PER_BLOCK;
    uint32_t SECTOR_SIZE_IN_BYTES;
    uint32_t num_partition_sectors;
    uint32_t physical_partition_number;
    uint32_t start_sector;
};

struct fh_response_cmd {
    const char *type;
    const char *value;
    uint32_t rawmode;
};

struct fh_log_cmd {
    const char *type;
};

struct fh_cmd_header {
    const char *type;
};

struct fh_cmd {
    union {
        struct fh_cmd_header cmd;
        struct fh_configure_cmd cfg;
        struct fh_erase_cmd erase;
        struct fh_program_cmd program;
        struct fh_response_cmd response;
        struct fh_log_cmd log;
    };
};

static char fh_xml_tx_line[1024];
static char fh_xml_rx_line[1024];
static unsigned fh_cmd_count = 0;
struct fh_cmd fh_cmd_table[32];
struct fh_cmd fh_cfg_cmd;
static const char *fh_firehose_dir = NULL;
static int fh_port_fd = -1;
static int parse_conf_response = 1;				//whether parse confi response.
static int fh_rawmode = 0;

static const char * fh_xml_get_value(const char *xml_line, const char *key) {
    static char value[64];

    char *pchar = strstr(xml_line, key);
    char *pend;

    if (!pchar) {
        dbg_time("%s: no key %s in %s\n", __func__, key, xml_line);
        return NULL;
    }

    pchar += strlen(key);
    if (pchar[0] != '=' && pchar[1] != '"') {
        dbg_time("%s: no start %s in %s\n", __func__, "=\"", xml_line);
        return NULL;
    }

    pchar += strlen("=\"");
    pend = strstr(pchar, "\"");
    if (!pend) {
        dbg_time("%s: no end %s in %s\n", __func__, "\"", xml_line);
        return NULL;
    }
    
    strncpy(value, pchar, pend - pchar);
    value[pend - pchar] = '\0';

    //dbg_time("%s=%s\n", key, value);

    return value;
}

static int fh_parse_xml_line(const char *xml_line, struct fh_cmd *fh_cmd) {
    const char *pchar = NULL;
    
    memset(fh_cmd, 0, sizeof( struct fh_cmd));
    if (strstr(xml_line, "<erase ")) {
        fh_cmd->erase.type = "erase";
        if ((pchar = fh_xml_get_value(xml_line, "PAGES_PER_BLOCK")))
            fh_cmd->erase.PAGES_PER_BLOCK = atoi(pchar);
        if ((pchar = fh_xml_get_value(xml_line, "SECTOR_SIZE_IN_BYTES")))
            fh_cmd->erase.SECTOR_SIZE_IN_BYTES = atoi(pchar);
        if ((pchar = fh_xml_get_value(xml_line, "num_partition_sectors")))
            fh_cmd->erase.num_partition_sectors = atoi(pchar);
        if ((pchar = fh_xml_get_value(xml_line, "physical_partition_number")))
            fh_cmd->erase.physical_partition_number = atoi(pchar);
        if ((pchar = fh_xml_get_value(xml_line, "start_sector")))
            fh_cmd->erase.start_sector = atoi(pchar);
        return 0;
    }
    else if (strstr(xml_line, "<program ")) {
        fh_cmd->program.type = "program";
        if ((pchar = fh_xml_get_value(xml_line, "filename")))
        {
        	fh_cmd->program.filename = strdup(pchar);
        	if(fh_cmd->program.filename[0] == '\0')
        	{//some fw version have blank program line, ignore it.
        		return -1;
        	}
        }
        if ((pchar = fh_xml_get_value(xml_line, "PAGES_PER_BLOCK")))
            fh_cmd->program.PAGES_PER_BLOCK = atoi(pchar);
        if ((pchar = fh_xml_get_value(xml_line, "SECTOR_SIZE_IN_BYTES")))
            fh_cmd->program.SECTOR_SIZE_IN_BYTES = atoi(pchar);
        if ((pchar = fh_xml_get_value(xml_line, "num_partition_sectors")))
            fh_cmd->program.num_partition_sectors = atoi(pchar);
        if ((pchar = fh_xml_get_value(xml_line, "physical_partition_number")))
            fh_cmd->program.physical_partition_number = atoi(pchar);
        if ((pchar = fh_xml_get_value(xml_line, "start_sector")))
            fh_cmd->program.start_sector = atoi(pchar);
        return 0;
    }
    else if (strstr(xml_line, "<response ")) {
        fh_cmd->response.type = "response";
        if ((pchar = fh_xml_get_value(xml_line, "value")))
            fh_cmd->response.value = strdup(pchar);

        if(parse_conf_response){
	        if ((pchar = fh_xml_get_value(xml_line, "MaxPayloadSizeFromTargetInBytes")))
	        {
	        	printf("{  MaxPayloadSizeFromTargetInBytes } = %d\n", atoi(strdup(pchar)));
	        	fh_cfg_cmd.cfg.MaxPayloadSizeFromTargetInBytes = atoi(strdup(pchar));
	        }
	        if ((pchar = fh_xml_get_value(xml_line, "MaxPayloadSizeToTargetInBytes")))
	        {    
	        	printf("{  MaxPayloadSizeToTargetInBytes } = %d\n", atoi(strdup(pchar)));
	        	fh_cfg_cmd.cfg.MaxPayloadSizeToTargetInBytes = atoi(strdup(pchar));
	        }
	        if ((pchar = fh_xml_get_value(xml_line, "MaxPayloadSizeToTargetInBytesSupported")))
	        {    
	        	printf("{  MaxPayloadSizeToTargetInBytesSupported } = %d\n", atoi(strdup(pchar)));
	        }  
	        parse_conf_response = 0;
        }
        if (fh_rawmode) {
            if ((pchar = fh_xml_get_value(xml_line, "rawmode"))) {
                fh_cmd->response.rawmode = !strcmp(pchar, "true");
            }
        }
        return 0;
    }
    else if (strstr(xml_line, "<log ")) {
        fh_cmd->program.type = "log";
        return 0;
    }

    error_return();
}

static int fh_parse_xml_file(const char *xml_file) {
    FILE *fp = fopen(xml_file, "rb");
    char *rx_buf = fh_xml_rx_line;
    
    if (fp < 0) {
        dbg_time("%s fail to fopen(%s), errno: %d (%s)\n", __func__, xml_file, errno, strerror(errno));
        error_return();
    }

    while (fgets(rx_buf, 1024, fp)) {
        char *xml_line = strstr(rx_buf, "<");
        if (xml_line && (strstr(xml_line, "<erase ") || strstr(xml_line, "<program "))) {
            if (!fh_parse_xml_line(xml_line, &fh_cmd_table[fh_cmd_count]))
                fh_cmd_count++;
        }
    }

    fclose(fp);

    return 0;
}

static int fh_fixup_program_cmd(struct fh_cmd *fh_cmd) {
    char full_path[256];
    char *pfile = strdup(fh_cmd->program.filename);
    char *ptmp;
    FILE *fp;
    long filesize = 0;

    while((ptmp = strchr(pfile, '\\'))) {
        *ptmp = '/';
    }    

   snprintf(full_path, sizeof(full_path), "%s/%s", fh_firehose_dir, pfile);
   if (access(full_path, R_OK)) {
        fh_cmd->program.num_partition_sectors = 0;
        dbg_time("fail to access %s, errno: %d (%s)\n", full_path, errno, strerror(errno));
        error_return();
   }

   fp = fopen(full_path, "rb");
   if (!fp) {
        fh_cmd->program.num_partition_sectors = 0;
        dbg_time("fail to fopen %s, errno: %d (%s)\n", full_path, errno, strerror(errno));
        error_return();
   }

   fseek(fp, 0, SEEK_END);
   filesize = ftell(fp);
   fclose(fp);

   if (filesize <= 0) {
        dbg_time("fail to ftell %s, errno: %d (%s)\n", full_path, errno, strerror(errno));
        fh_cmd->program.num_partition_sectors = 0;
        error_return();
   }

   fh_cmd->program.num_partition_sectors = filesize/fh_cmd->program.SECTOR_SIZE_IN_BYTES;
   if (filesize%fh_cmd->program.SECTOR_SIZE_IN_BYTES)
        fh_cmd->program.num_partition_sectors += 1;

    free(pfile);
    return 0;
}
static int fh_reset(){

	char *tx_buf = fh_xml_tx_line;
	int tx_len = 0;
    char *pstart, *pend;
    tx_buf[0] = '\0';
    dbg_time("reset command\n");
    snprintf(tx_buf + strlen(tx_buf), 1024, "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n");
    snprintf(tx_buf + strlen(tx_buf), 1024, "<data>\n");
    
    pstart = tx_buf + strlen(tx_buf);
    snprintf(tx_buf + strlen(tx_buf), 1024, 
            "<power value=\"reset\" />");
    snprintf(tx_buf + strlen(tx_buf), 1024, "\n</data>");
    pend = tx_buf + strlen(tx_buf);
    dbg_time("%.*s\n", (int)(pend - pstart),  pstart);
    tx_len = qusb_write(fh_port_fd, tx_buf, strlen(tx_buf), 1);
	if (tx_len == strlen(tx_buf))
        return 0;

    error_return();
}
static int fh_send_cmd(const struct fh_cmd *fh_cmd) {  
    char *tx_buf = fh_xml_tx_line;
    int tx_len = 0;
    char *pstart, *pend;
    
    tx_buf[0] = '\0';

    snprintf(tx_buf + strlen(tx_buf), 1024, "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n");
    snprintf(tx_buf + strlen(tx_buf), 1024, "<data>\n");

    pstart = tx_buf + strlen(tx_buf);
    if (strstr(fh_cmd->cmd.type, "erase")) {
        snprintf(tx_buf + strlen(tx_buf), 1024, 
            "<erase PAGES_PER_BLOCK=\"%d\" SECTOR_SIZE_IN_BYTES=\"%d\" num_partition_sectors=\"%d\" physical_partition_number=\"%d\" start_sector=\"%d\" />",  
            fh_cmd->erase.PAGES_PER_BLOCK, fh_cmd->erase.SECTOR_SIZE_IN_BYTES, fh_cmd->erase.num_partition_sectors,
            fh_cmd->erase.physical_partition_number, fh_cmd->erase.start_sector);
    }
    else if (strstr(fh_cmd->cmd.type, "program")) {
        snprintf(tx_buf + strlen(tx_buf), 1024,
            "<program PAGES_PER_BLOCK=\"%d\" SECTOR_SIZE_IN_BYTES=\"%d\" filename=\"%s\" num_partition_sectors=\"%d\"  physical_partition_number=\"%d\" start_sector=\"%d\" />",
            fh_cmd->program.PAGES_PER_BLOCK,  fh_cmd->program.SECTOR_SIZE_IN_BYTES,  fh_cmd->program.filename,
            fh_cmd->program.num_partition_sectors, fh_cmd->program.physical_partition_number,  fh_cmd->program.start_sector);
    }
    else if (strstr(fh_cmd->cmd.type, "configure")) {
        snprintf(tx_buf + strlen(tx_buf), 1024,  
            "<configure MemoryName=\"%s\" Verbose=\"%d\" AlwaysValidate=\"%d\" MaxDigestTableSizeInBytes=\"%d\" MaxPayloadSizeToTargetInBytes=\"%d\" MaxPayloadSizeFromTargetInBytes=\"%d\" MaxPayloadSizeToTargetInBytesSupported=\"%d\" ZlpAwareHost=\"%d\" SkipStorageInit=\"%d\" BuildId=\"1\" DateTime=\"1\"/>",
            fh_cmd->cfg.MemoryName, fh_cmd->cfg.Verbose, fh_cmd->cfg.AlwaysValidate,
            fh_cmd->cfg.MaxDigestTableSizeInBytes, 
            fh_cmd->cfg.MaxPayloadSizeToTargetInBytes,
            fh_cmd->cfg.MaxPayloadSizeFromTargetInBytes,
            fh_cmd->cfg.MaxPayloadSizeToTargetInByteSupported,
            fh_cmd->cfg.ZlpAwareHost, fh_cmd->cfg.SkipStorageInit);
    }
    pend = tx_buf + strlen(tx_buf);
    dbg_time("%.*s\n", (int)(pend - pstart),  pstart);
    
    snprintf(tx_buf + strlen(tx_buf), 1024, "\n</data>");

    tx_len = qusb_write(fh_port_fd, tx_buf, strlen(tx_buf), 1);

    if (tx_len == strlen(tx_buf))
        return 0;

    error_return();
}

static int fh_recv_cmd(struct fh_cmd *fh_cmd, unsigned timeout) {  
    struct pollfd pollfds[] = {{fh_port_fd, POLLIN, 0}};
    int ret;
    char *rx_buf = fh_xml_rx_line;
    char *xml_line;
    char *pend;

    if (timeout == 0)
        timeout = 3000;
    memset(fh_cmd, 0, sizeof(struct fh_cmd));
    memset(rx_buf, 0, 1024);
    
    do {
        ret = poll(pollfds, 1, timeout);
    } while (ret < 0 && errno == EINTR);
    
    if (ret <= 0) {
        dbg_time("fail to fh_recv_cmd, errno: %d (%s)\n", errno, strerror(errno));
        error_return();
    }
    	
    ret = qusb_read(fh_port_fd, rx_buf, 1024);
    if (ret <= 0) {
        dbg_time("fail to qusb_read, errno: %d (%s)\n", errno, strerror(errno));
        error_return();
    }
    //rx_buf[ret] = 0;
	printf("rx_buf = {%s}\n", rx_buf);
    xml_line = strstr(rx_buf, "<response ");
    if (xml_line) {
        fh_parse_xml_line(xml_line, fh_cmd);
        pend = strchr(xml_line, '\n');
        dbg_time("%.*s\n", (int)(pend -xml_line),  xml_line);
    }
    else {
        xml_line = strstr(rx_buf, "<log ");
        if (xml_line) {
            fh_parse_xml_line(xml_line, fh_cmd);
            pend = strchr(xml_line, '\n');
            dbg_time("%.*s\n", (int)(pend -xml_line),  xml_line);
        }
    }

    if (fh_cmd->cmd.type)
        return 0;

    error_return();
}

static int fh_wait_response_cmd(struct fh_cmd *fh_cmd, unsigned timeout) { 
    while (1) {
        int ret = fh_recv_cmd(fh_cmd, timeout);

        if (ret !=0)
            error_return();

        if (strstr(fh_cmd->cmd.type, "log"))
            continue;

        return 0;
    }

    error_return();
}

#ifndef MIN
#define MIN(a,b)	 ((a) < (b)? (a) : (b))
#endif

static int fh_send_rawmode_image(const struct fh_cmd *fh_cmd) {
    char full_path[256];
    char *pfile = strdup(fh_cmd->program.filename);
    char *ptmp;
    FILE *fp;
    size_t filesize, filesend;
    const int persize = 1024 * 16;
    void *pbuf = malloc(fh_cfg_cmd.cfg.MaxPayloadSizeToTargetInBytes);

    if (pbuf == NULL)
        error_return();
    
    while((ptmp = strchr(pfile, '\\'))) {
        *ptmp = '/';
    }

    snprintf(full_path, sizeof(full_path), "%s/%s", fh_firehose_dir, pfile);
    dbg_time("send %s\n", full_path);
    fp = fopen(full_path, "rb");
    if (!fp) {
        dbg_time("fail to fopen %s, errno: %d (%s)\n", full_path, errno, strerror(errno));
        error_return();
    }

    fseek(fp, 0, SEEK_END);
    filesize = ftell(fp);
    filesend = 0;
    fseek(fp, 0, SEEK_SET);        

    while (filesend < filesize) {
        size_t reads = fread(pbuf, 1, MIN(fh_cfg_cmd.cfg.MaxPayloadSizeToTargetInBytes, persize), fp);
        if (reads > 0) {
            if (reads % fh_cmd->program.SECTOR_SIZE_IN_BYTES) {
                memset(pbuf + reads, 0, fh_cmd->program.SECTOR_SIZE_IN_BYTES - (reads % fh_cmd->program.SECTOR_SIZE_IN_BYTES));
                reads +=  fh_cmd->program.SECTOR_SIZE_IN_BYTES - (reads % fh_cmd->program.SECTOR_SIZE_IN_BYTES);
            }
            size_t writes = qusb_write(fh_port_fd, pbuf, reads, 1/*(filesend + reads) == filesize*/);
            //usleep(10000);
            //dbg_time("%s send fail reads=%zd, writes=%zd\n", __func__, reads, writes);
            //dbg_time("%s send fail filesend=%zd, filesize=%zd\n", __func__, filesend, filesize);
        
            if (reads != writes) {
                dbg_time("%s send fail reads=%zd, writes=%zd\n", __func__, reads, writes);
                break;
            }
            filesend += reads;
        } else {
            break;
        }
    }

    fclose(fp);   
    free(pfile);
    free(pbuf);

    if (filesend >= filesize)
        return 0;

    error_return();
}
static int retrieve_rawprogram_nand_xml_filename(const char* path, char** xmlfile)
{
	DIR *pdir;
	struct dirent* ent = NULL;
	pdir = opendir(path);
	
	if(pdir)
	{
		while((ent = readdir(pdir)) != NULL)
		{
			if(!strncmp(ent->d_name, "rawprogram", 4))
			{
				*xmlfile = strdup(ent->d_name);
				closedir(pdir);
				return 0;
			}						
		}
		
	}else
	{
		return 1;
	}
	return 1;
}

int firehose_main (const char *firehose_dir) {
    unsigned x;
    char full_path[256];
    char *xml_file;// = "rawprogram_nand_p4K_b256K_update.xml";
    struct fh_cmd fh_rx_cmd;
    
    fh_firehose_dir = firehose_dir;
	if(retrieve_rawprogram_nand_xml_filename(fh_firehose_dir, &xml_file) != 0)
	{
		dbg_time("retrieve rawprogram namd file failed.\n");
		error_return();
	}
    snprintf(full_path, sizeof(full_path), "%s/%s", fh_firehose_dir, xml_file);
    if (access(full_path, R_OK)) {
        dbg_time("fail to access %s, errno: %d (%s)\n", full_path, errno, strerror(errno));
        error_return();
    }

    fh_parse_xml_file(full_path);

    if (fh_cmd_count == 0)
        error_return();

    for (x = 0; x < fh_cmd_count; x++) {
        struct fh_cmd *fh_cmd = &fh_cmd_table[x];
        
        if (strstr(fh_cmd->cmd.type, "program")) {
            fh_fixup_program_cmd(fh_cmd);
            if (fh_cmd->program.num_partition_sectors == 0)
                error_return();
        }
    }

    fh_port_fd = qusb_open("/dev/ttyUSB0");
    if (fh_port_fd <= 0) {
        error_return();
    }
    
    fh_recv_cmd(&fh_rx_cmd, 3000);

    fh_cfg_cmd.cfg.type = "configure";
    fh_cfg_cmd.cfg.MemoryName = "NAND";
    fh_cfg_cmd.cfg.Verbose = 0;
    fh_cfg_cmd.cfg.AlwaysValidate = 0;
    fh_cfg_cmd.cfg.MaxDigestTableSizeInBytes = 2048;
    #if 0
    fh_cfg_cmd.cfg.MaxPayloadSizeToTargetInBytes = 16*1024;//MaxPayloadSizeToTargetInBytes
    #else
    fh_cfg_cmd.cfg.MaxPayloadSizeToTargetInBytes = 16*1024;
    #endif
    fh_cfg_cmd.cfg.MaxPayloadSizeFromTargetInBytes = 2*1024;
    fh_cfg_cmd.cfg.MaxPayloadSizeToTargetInByteSupported = 16*1024;
    fh_cfg_cmd.cfg.ZlpAwareHost = 0;
    fh_cfg_cmd.cfg.SkipStorageInit = 0;

	parse_conf_response = 1;
    fh_send_cmd(&fh_cfg_cmd);
    if (fh_wait_response_cmd(&fh_rx_cmd, 3000) != 0)
        error_return();

     if (strcmp(fh_rx_cmd.response.value, "ACK") != 0 && strcmp(fh_rx_cmd.response.value, "NAK") != 0 )
        error_return();   
    for (x = 0; x < fh_cmd_count; x++) {
        const struct fh_cmd *fh_cmd = &fh_cmd_table[x];
        
        if (strstr(fh_cmd->cmd.type, "erase")) {
            fh_send_cmd(fh_cmd);
            if (fh_wait_response_cmd(&fh_rx_cmd, 3000) != 0)
                error_return();
             if (strcmp(fh_rx_cmd.response.value, "ACK"))
                error_return(); 
        }
    }

    fh_rawmode = 1;
    for (x = 0; x < fh_cmd_count; x++) {
        const struct fh_cmd *fh_cmd = &fh_cmd_table[x];
        
        if (strstr(fh_cmd->cmd.type, "program")) {
redo1:        
            fh_send_cmd(fh_cmd);
            if (fh_wait_response_cmd(&fh_rx_cmd, 5000) != 0) {
                dbg_time("fh_wait_response_cmd fail\n");
                error_return();
             }
             if (strcmp(fh_rx_cmd.response.value, "ACK")) {
                dbg_time("response should be ACK\n");
                error_return();
             }
             if (fh_rx_cmd.response.rawmode != 1) {
                dbg_time("response should be rawmode true\n");
                error_return();
                //goto redo1;
             }
redo2:             
			 //usleep(1000);
             if (fh_send_rawmode_image(fh_cmd)) {
                dbg_time("fh_send_rawmode_image fail\n");
                error_return();
             }
             if (fh_wait_response_cmd(&fh_rx_cmd, 15000) != 0) {
                dbg_time("fh_wait_response_cmd fail\n");
                error_return();
             }
             if (strcmp(fh_rx_cmd.response.value, "ACK")) {
                dbg_time("response should be ACK\n");
                error_return();
             }
             if (fh_rx_cmd.response.rawmode != 0) {
                dbg_time("response should be rawmode false\n");
				//goto redo2;                
                error_return();
             }
             //exit(-1);
             //usleep()
        }
    }
	fh_reset();
	if (fh_wait_response_cmd(&fh_rx_cmd, 3000) != 0)
        error_return();
    qusb_close(fh_port_fd);
    return 0;
}
