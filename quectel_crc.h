#ifndef QUECTEL_CRC_H
#define QUECTEL_CRC_H


#define CRC_16_L_SEED           0xFFFF
#define CRC_TAB_SIZE    256             /* 2^CRC_TAB_BITS      */
#define CRC_16_L_POLYNOMIAL     0x8408

unsigned short crc_16_l_calc(unsigned char *buf_ptr, int len);
#endif