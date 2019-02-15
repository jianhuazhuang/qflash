#ifndef QUECTEL_COMM_H
#define QUECTEL_COMM_H

#define LOCKFILE	"/var/run/QFlash.pid"
#define return_val_if_fail(cond, val) {if(!(cond)) return (val);}
#define ret_if_fail(cond) {if(!(cond)) return;}

#define ARRAY_SIZE(u) (sizeof(u)/sizeof(u[0]))

enum usb_speed {
    usb_highspeed,
    usb_fullspeed,
    usb_superspeed
};

enum module_platform_t {
    platform_9x06 = 0,
    platform_9x07,
    platform_9x45,
    platform_9x65,
    platform_unknown
};


module_platform_t get_module_platform(const char* nprg_filename);
int get_product_model(char ** product_model);
int q_port_detect(char** pp_diag_port, int interface);
int checkCPU();
int probe_quectel_speed(enum usb_speed* speed);
void strToLower(char* src);
void strToUpper(char* src);

int detect_adb();
int detect_diag_port();
int detect_diag_port(char **diag_port);
int detect_modem_port(char **modem_port);

int wait_diag_port_disconnect(int timeout /*s*/);
int wait_adb(int timeout);

int is_emergency_diag_port();

int detect_diag_port_timeout(int timeout);
int open_port_once(int ioflush);
#ifndef MAX_PATH
#define MAX_PATH 260
#endif

int open_port_once(const char* dev);

void upgrade_process(int writesize,int size,int clear);
int already_running(const char *filename);

int show_user_group_name();
double get_now();


class transfer_statistics {
  public:
    static transfer_statistics* getInstance();

    void set_total(long long all_files_size/*kb*/);
    void set_write_bytes(long long transfer_bytes);
    int get_percent();


  private:
    transfer_statistics();
    transfer_statistics(const transfer_statistics&);
    transfer_statistics& operator=(const transfer_statistics&);

    static transfer_statistics* instance;
    long long m_all_files_bytes;			//all bytes
    long long m_transfer_bytes;				//current transfer bytes

};

unsigned long get_file_size(const char* filename);

#endif
