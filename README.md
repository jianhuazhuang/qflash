# quectel-qflash

Qflash[^1], a upgrade tool for quectel devices.

# file directory tree

> If firehose directory is not exist, firehose can not be used.
>
> If fastboot directory is not exist, fastboot can not be used.
>
> You can build fastboot, firehose independly and put the execute file (qfirehose or qfastboot) into subdir (firehose or fastboot) of Qflash to make it callable for Qflash.

```bash

root@ubuntu:# tree
.
├── Android.mk
├── Application.mk
├── atchannel.cpp
├── atchannel.h
├── at_tok.c
├── at_tok.cpp
├── at_tok.h
├── download.cpp
├── download.h
├── fastboot
│   ├── Android.mk
│   ├── engine.c
│   ├── fastboot.c
│   ├── fastboot.h
│   ├── Makefile
│   ├── protocol.c
│   ├── quectel_log.c
│   ├── quectel_log.h
│   ├── usb.h
│   ├── usb_linux_fastboot.c
│   └── util_linux.c
├── file.cpp
├── file.h
├── firehose
│   ├── Android.mk
│   ├── firehose_protocol.c
│   ├── Makefile
│   ├── qfirehose.c
│   ├── sahara_protocol.c
│   ├── sahara_protocol.h
│   ├── usb2tcp.c
│   ├── usb_linux.c
│   └── usb_linux.h
├── LICENSE
├── Makefile
├── md5.cpp
├── md5.h
├── os_linux.cpp
├── os_linux.h
├── platform_def.h
├── quectel_common.cpp
├── quectel_common.h
├── quectel_crc.cpp
├── quectel_crc.h
├── quectel_log.cpp
├── quectel_log.h
├── README.md
├── ReleaseNote.txt
├── ril-daemon.c
├── ril-daemon.cpp
├── serialif.cpp
├── serialif.h
├── tags
├── tinystr.cpp
├── tinystr.h
├── tinyxml.cpp
├── tinyxmlerror.cpp
├── tinyxml.h
├── tinyxmlparser.cpp
└── tool.sh
```



# Qflash call thirtd-party tool (firehose or fastboot)

> Qflash can call third-party update tools use exec() family of functions.

## Qflash help message

>  fastboot should can be called when using '-m 0' and '-m 2'

```bash

root@ubuntu:# ./QFlash -h
[02-14_16:19:37:608] QFlash Version: LTE_QFlash_Linux&Android_V1.4.8
[02-14_16:19:37:608] Builded: Feb 14 2019 16:19:28
[02-14_16:19:37:608]
[02-14_16:19:37:608] The CPU is little endian
[02-14_16:19:37:608]
[02-14_16:19:37:608] ./QFlash [fastboot|firehose] [options...]
[02-14_16:19:37:609] [protocol]
[02-14_16:19:37:609]     fastboot                                 Use fastboot upgrade protocol
[02-14_16:19:37:609]     firehose                                 Use firehose upgrade protocol
[02-14_16:19:37:609] [parameters]
[02-14_16:19:37:609]     -f [package_dir]                         Upgrade package directory path
[02-14_16:19:37:609]     -p [ttyUSBX]                             Diagnoise port, will auto-detect if not specified
[02-14_16:19:37:609]     -m [mode]                                Qflash upgrade method
[02-14_16:19:37:609]                                              method = 1 --> streaming download protocol
[02-14_16:19:37:609]                                              method = 0 --> fastboot download protocol
[02-14_16:19:37:609]                                              method = 2 --> fastboot download protocol (at command first)
[02-14_16:19:37:609]                                              method = 3 --> firehose download protocol
[02-14_16:19:37:609]     -s [size]                                Transport block size
[02-14_16:19:37:609]     -v                                       Verbose
[02-14_16:19:37:609]     -h                                       Help message
root@ubuntu:#
```
