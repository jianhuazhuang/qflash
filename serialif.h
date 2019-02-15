
#ifndef __SERIALIF_H__
#define __SERIALIF_H__

#include "platform_def.h"
#include <netinet/in.h>


extern unsigned char  dloadbuf[];
extern unsigned char g_Receive_Buffer[];
extern int g_Receive_Bytes;
extern unsigned char g_Transmit_Buffer[];
extern int g_Transmit_Length;

target_current_state send_sync(void);

/*-------begin-----------streaming download protocol-----------------------*/
int handle_hello(void);
int handle_security_mode(unsigned char trusted);
int handle_parti_tbl(unsigned char override);
int handle_reset(void);
int handle_openmulti(uint32 size,unsigned char* data);
int handle_write(FILE *fp,  unsigned char *data, uint32 size);
int handle_close(void);
int handle_quectel_download_flag(byte mode);

/*-------end-----------streaming download protocol-----------------------*/

/*------begin-------------sahara download protocol-------------------------*/


typedef struct {
    unsigned char cmd_code;
    unsigned char version;
    unsigned char reserved[2];
    unsigned char msm[4];
    unsigned char mobile_modle_number[4];
    unsigned char mobile_software_revision[1];
} __attribute__ ((packed))extended_build_id_response_t;


typedef struct {
    uint32 Command;
    uint32 Length;
    uint32 ImageID;
    uint32 Status;
} __attribute__((packed))end_of_image_transfer_packet_t;

typedef struct {
    uint32 Command;
    uint32 Length;
    uint32 VersionNumber;
    uint32 VersionCompatible;
    uint32 CommandPacketLength;
    uint32 Mode;
    uint32 Reserved[6];
} __attribute__((packed))hello_packet;


typedef struct {
    uint32 Command;
    uint32 Length;
    uint32 ImageID;
    uint32 DataOffset;
    uint32 DataLength;
} __attribute__((packed))read_data_packet_t;


typedef struct {
    uint32 Command;
    uint32 Length;
} __attribute__((packed))sahara_header_t;

/***********SHARA COMMAND*****************/
#define SUCESS_OR_ERROR_STATE 			0x00000000  //Host sets this field based on the Hello packet received; if target protocol matches host and no other errors, a success value is sent.

#define GET_HELLO_PACKET  				0x00000001   //the target sends a Hello packet
#define LOW_COMP_VERSON_NUM   			0x00000001  //Lowest compatible
#define SHARA_PROCTOL_VERSON_NUM  		0x00000002  //Version number of this protocol implementation
#define HELLO_RESPONSE_PACKET 			0x00000002  //the host sends a Hello Response packet
#define GET_DATA_PACKET  				0x00000003       //the target sends a Read Data packet
#define END_IMAGE_TRNSER_PACKET 		0x00000004  //the target sends an End of Image Transfer packet
#define DONE_RESONSE_PACKET 			0x00000006   //the target sends a Done Response packet
#define HELLO_RESPONSE_PACKET_LENGTH 	0x00000030 //Length of packet (in bytes)
#define RESET_PACKET 					0x00000007

typedef enum {
    SAHARA_NO_CMD_ID          = 0x00,
    SAHARA_HELLO_ID           = 0x01, // sent from target to host
    SAHARA_HELLO_RESP_ID      = 0x02, // sent from host to target
    SAHARA_READ_DATA_ID       = 0x03, // sent from target to host
    SAHARA_END_IMAGE_TX_ID    = 0x04, // sent from target to host
    SAHARA_DONE_ID            = 0x05, // sent from host to target
    SAHARA_DONE_RESP_ID       = 0x06, // sent from target to host
    SAHARA_RESET_ID           = 0x07, // sent from host to target
    SAHARA_RESET_RESP_ID      = 0x08, // sent from target to host
    SAHARA_MEMORY_DEBUG_ID    = 0x09, // sent from target to host
    SAHARA_MEMORY_READ_ID     = 0x0A, // sent from host to target
    SAHARA_CMD_READY_ID       = 0x0B, // sent from target to host
    SAHARA_CMD_SWITCH_MODE_ID = 0x0C, // sent from host to target
    SAHARA_CMD_EXEC_ID        = 0x0D, // sent from host to target
    SAHARA_CMD_EXEC_RESP_ID   = 0x0E, // sent from target to host
    SAHARA_CMD_EXEC_DATA_ID   = 0x0F, // sent from host to target
    SAHARA_64_BITS_MEMORY_DEBUG_ID	= 0x10, // sent from target to host
    SAHARA_64_BITS_MEMORY_READ_ID		= 0x11, // sent from host to target
    SAHARA_64_BITS_READ_DATA_ID		= 0x12,  // place all new commands above this
    SAHARA_LAST_CMD_ID,
    SAHARA_MAX_CMD_ID             = 0x7FFFFFFF // To ensure 32-bits wide
} boot_sahara_cmd_id;

typedef enum {
    SAHARA_MODE_IMAGE_TX_PENDING  = 0x0,
    SAHARA_MODE_IMAGE_TX_COMPLETE = 0x1,
    SAHARA_MODE_MEMORY_DEBUG      = 0x2,
    SAHARA_MODE_COMMAND           = 0x3,  // place all new commands above this
    SAHARA_MODE_LAST,
    SAHARA_MODE_MAX = 0x7FFFFFFF
} boot_sahara_mode;

int sahara_done();
int sahara_reset();
int kick_sahara_state_machine();
int transfer_nrpg_or_enpgr_file();
int send_sahara_hello_response_packet();
int send_sahara_do_packet();
int get_sahara_hello_packet();
int SendHelloPacket();
int SendDoPacket();
int SendHelloPacketTest(int emergency_mode);
int SendResetPacket();
int GetReadDataPacket(int *emergency_mode, int maxpkt = (1024 * 4));
/*------end-------------sahara download protocol-------------------------*/

/*------begin-------------module normal hdlc-------------------------*/

int switch_to_dload(void);
int retrieve_soft_revision();

/*------end-------------module normal hdlc-------------------------*/





/*-------------------common-------------------------*/

int ignore_dirty_data();
int ReadSAHARABuffer(int file, unsigned char * lpBuf, int dwToRead);

//hunter.lv add
//retrieve module soft revision

#ifdef FIREHOSE_ENABLE
int switch_emergency_download();
int transfer_prog_nand_firehose_file(char * filename);
#endif

#endif /*__SERIALIF_H__*/

