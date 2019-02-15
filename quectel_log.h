#ifndef QUECTEL_LOG_H
#define QUECTEL_LOG_H

const char * get_time(void);

#if defined(DEBUG)
#define log_print(fmt, arg...) do{ dprintf(0, "[%s %s:%04d] "fmt, get_time(), __FILE__, __LINE__, ##arg); }while(0);
#else
#define log_print(fmt, arg...) do{ dprintf(0, "[%s] "fmt, get_time(), ##arg); }while(0);
#endif
#define log_debug(fmt, arg...)      {if(debug) log_print("\033[47;30m""Debug: "fmt"\033[0m", ##arg);}
#define log_warn(fmt, arg...)       {log_print("\033[40;33m""Warn: "fmt"\033[0m", ##arg);}
#define log_error(fmt, arg...)      {log_print("\033[40;31m""Error: "fmt"\033[0m", ##arg);}
#define log_fatal(fmt, arg...)      {log_print("\033[41;37m""Fatal: "fmt"\033[0m", ##arg); exit(1);}
#define log_info(fmt, arg...)       {log_print("\033[40;37m"fmt"\033[0m", ##arg);}
#define log_verbose(fmt, arg...)    {if(verbose) log_print("\033[40;37m"fmt"\033[0m", ##arg);}

#endif //QUECTEL_LOG_H
