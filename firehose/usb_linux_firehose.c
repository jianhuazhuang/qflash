#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <termios.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <linux/un.h>
#include <linux/usbdevice_fs.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 20)
#include <linux/usb/ch9.h>
#else
#include <linux/usb_ch9.h>
#endif
#include <asm/byteorder.h>

void dbg_time (const char *fmt, ...);

unsigned int inet_addr(const char *cp);

#define MIN(a,b)	 ((a) < (b)? (a) : (b))

//#define _XOPEN_SOURCE

#define MAX_USBFS_BULK_IN_SIZE (4 * 1024)
#define MAX_USBFS_BULK_OUT_SIZE (16 * 1024)
#define EC20_MAX_INF 10
struct ec20_usb_device
{
    char fname[64];
    int desc;
    __le16 idVendor;
    __le16 idProduct;
    __u8 bNumInterfaces;
    __u8 intr_ep[EC20_MAX_INF];
    __u8 bulk_ep_in[EC20_MAX_INF];
    __u8 bulk_ep_out[EC20_MAX_INF];
    __le16 wMaxPacketSize[EC20_MAX_INF];
    int control[EC20_MAX_INF][2];
    struct usbdevfs_urb bulk_in[EC20_MAX_INF];
    struct usbdevfs_urb bulk_out[EC20_MAX_INF];
};

static struct ec20_usb_device ec20_device;

static inline int badname(const char *name) {
    while(*name) {
        if(!isdigit(*name++)) return 1;
    }
    return 0;
}

static void quectel_find_usb_device(struct ec20_usb_device *udev) {
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

static int usbfs_bulk_write(struct ec20_usb_device *udev, int bInterfaceNumber, const void *data, int len, int need_zlp) {
#if 1
    struct usbdevfs_urb bulk;
    struct usbdevfs_urb *urb = &bulk;
    int n = -1;
    
    //if (urb->type == 0) 
    {
        memset(urb, 0, sizeof(struct usbdevfs_urb));
        urb->type = USBDEVFS_URB_TYPE_BULK;
        urb->endpoint = udev->bulk_ep_out[bInterfaceNumber];
    }

    urb->status = -1;
    urb->buffer = (void *)data;
    urb->buffer_length = (len > MAX_USBFS_BULK_OUT_SIZE) ? MAX_USBFS_BULK_OUT_SIZE : len;
    urb->usercontext = urb;
    
    if ((len <= MAX_USBFS_BULK_OUT_SIZE) && need_zlp && (len%512) == 0) {
        //dbg_time("USBDEVFS_URB_ZERO_PACKET\n");
#ifndef USBDEVFS_URB_ZERO_PACKET
#define USBDEVFS_URB_ZERO_PACKET	0x40
#endif
        urb->flags = USBDEVFS_URB_ZERO_PACKET;
    } else {
        urb->flags = 0;
    }

    do {
        n = ioctl(udev->desc, USBDEVFS_SUBMITURB, urb);
    } while((n < 0) && (errno == EINTR));
    
    if (n != 0) {
        dbg_time("inf[%d] USBDEVFS_SUBMITURB %d/%d, errno = %d (%s)\n", bInterfaceNumber, n, urb->buffer_length, errno, strerror(errno));
        return -1;
    }
                
    do {
        urb = NULL;
        n = ioctl(udev->desc, USBDEVFS_REAPURB, &urb);
    } while((n < 0) && (errno == EINTR));

    if (n != 0) {
        dbg_time("inf[%d] ep_out %d/%d, errno = %d (%s)\n", bInterfaceNumber, n, urb->buffer_length, errno, strerror(errno));
    }
    
    //dbg_time("[ urb @%p status = %d, actual = %d ]\n", urb, urb->status, urb->actual_length);

    if (urb && urb->status == 0 && urb->actual_length)
        return urb->actual_length;

    return -1;
#else
    struct usbdevfs_bulktransfer bulk;
    int n = -1;
    
    bulk.ep = udev->bulk_ep_out[bInterfaceNumber];
    bulk.len = (len > MAX_USBFS_BULK_OUT_SIZE) ? MAX_USBFS_BULK_OUT_SIZE : len;
    bulk.data = (void *)data;
    bulk.timeout = 0;

    n = ioctl(udev->desc, USBDEVFS_BULK, &bulk);
    if (n != bulk.len) {
        dbg_time("inf[%d] ep_out %d/%d, errno = %d (%s)\n", bInterfaceNumber, n, bulk.len, errno, strerror(errno));
    }

    return n;
#endif
}

static int usbfs_bulk_read(struct ec20_usb_device *udev, int bInterfaceNumber, const void *data, int len) {
    struct usbdevfs_bulktransfer bulk;
    int n = -1;

    bulk.ep = udev->bulk_ep_in[bInterfaceNumber];
    bulk.len = (len > MAX_USBFS_BULK_IN_SIZE) ? MAX_USBFS_BULK_IN_SIZE : len;;
    bulk.data = (void *)data;
    bulk.timeout = 0;

    n = ioctl(udev->desc, USBDEVFS_BULK, &bulk);
    if( n < 0 ) {
        if (errno != ESHUTDOWN)
            dbg_time("inf[%d] ep_in %d/%d, errno = %d (%s)\n", bInterfaceNumber, n, bulk.len, errno, strerror(errno));
    }
    
    return n ;
}

static void* usb_bulk_read_thread(void* arg) {
    struct ec20_usb_device *udev = &ec20_device;
    int bInterfaceNumber = *((int *)arg);
    char *buf = malloc(MAX_USBFS_BULK_IN_SIZE);

    if (buf == NULL)
        return NULL;

    while(udev->desc) {
        int count = usbfs_bulk_read(udev, 0, buf, MAX_USBFS_BULK_IN_SIZE);
                
        if (count > 0 && udev->desc) {
            count = write(udev->control[bInterfaceNumber][1], buf, count);
        } else {
            break;
        }
    }

    free(buf);
    return NULL;
}

int qusb_open(char *port_name) {
    struct termios ios;
    int retval;
    int fd = -1;

    if (access(port_name, R_OK)) {
        quectel_find_usb_device(&ec20_device);
        if (ec20_device.desc) {
            pthread_t usb_thread_id;
            pthread_attr_t usb_thread_attr;
            static int s_bInterfaceNumber[EC20_MAX_INF];
            int bInterfaceNumber = 0;

            retval = ioctl(ec20_device.desc, USBDEVFS_CLAIMINTERFACE, &bInterfaceNumber);
            if(retval != 0) {
                dbg_time("fail to claim interface %d, errno: %d (%s)\n", bInterfaceNumber, errno, strerror(errno));
                return -1;
            }

            s_bInterfaceNumber[bInterfaceNumber] = bInterfaceNumber;
            pthread_attr_init(&usb_thread_attr);
            pthread_attr_setdetachstate(&usb_thread_attr, PTHREAD_CREATE_DETACHED);

            socketpair(AF_LOCAL, SOCK_STREAM, 0, (&ec20_device)->control[bInterfaceNumber]);
            fcntl((&ec20_device)->control[bInterfaceNumber][0],  F_SETFL, O_NONBLOCK);
            fcntl((&ec20_device)->control[bInterfaceNumber][1],  F_SETFL, O_NONBLOCK);
            pthread_create(&usb_thread_id, &usb_thread_attr, usb_bulk_read_thread, (void*)&s_bInterfaceNumber[bInterfaceNumber]);

            fd = (&ec20_device)->control[bInterfaceNumber][0];
        }
    }
    else if (!strncmp(port_name, "/dev/tty", strlen("/dev/tty"))) {
        fd = open (port_name, O_RDWR | O_SYNC | O_NONBLOCK);

        if (fd <= 0) {
            dbg_time("Device could not be open: Linux System Errno: %s", strerror (errno));
            return -1;
        }

        retval = tcgetattr (fd, &ios);
        if (-1 == retval) {
            dbg_time("ermio settings could not be fetched Linux System Error:%s", strerror (errno));
            return -1;
        }

        cfmakeraw (&ios);
        cfsetispeed(&ios, B115200);
        cfsetospeed(&ios, B115200);

        retval = tcsetattr (fd, TCSANOW, &ios);
        if (-1 == retval) {
            dbg_time("Device could not be configured: Linux System Errno: %s", strerror (errno));
        }
    }
    else {
        char *tcp_host = port_name;
        char *tcp_port = strchr(port_name, ':');
        struct sockaddr_in sockaddr;

        if (tcp_port == NULL)
            return -1;

        *tcp_port++ = '\0';
        if (atoi(tcp_port) < 1 || atoi(tcp_port) > 0xFFFF)
            return -1;

        dbg_time("tcp_host = %s, tcp_port = %d", tcp_host, atoi(tcp_port));

         fd = socket(AF_INET, SOCK_STREAM, 0);
        
        if (fd <= 0) {
            dbg_time("Device could not be socket: Linux System Errno: %s", strerror (errno));
            return -1;
        }
        
        memset(&sockaddr, 0, sizeof(sockaddr));
        sockaddr.sin_family = AF_INET;
        sockaddr.sin_addr.s_addr = inet_addr(tcp_host);
        sockaddr.sin_port = htons(atoi(tcp_port));

        if (connect(fd, (struct sockaddr *) &sockaddr, sizeof(sockaddr)) < 0) {
            dbg_time("Device could not be connect: Linux System Errno: %s", strerror (errno));
            return -1;
        }
    }

    return fd;
}

int qusb_close(int fd) {
    if (ec20_device.desc > 0) {
        int bInterfaceNumber = 0;
        int retval = ioctl(ec20_device.desc, USBDEVFS_RELEASEINTERFACE, &bInterfaceNumber);
        if(retval != 0) {
            dbg_time("fail to release interface %d, errno: %d (%s)\n", bInterfaceNumber, errno, strerror(errno));
        }
        close(ec20_device.control[bInterfaceNumber][0]);
        close(ec20_device.control[bInterfaceNumber][1]);
        close(ec20_device.desc);
        memset(&ec20_device, 0, sizeof(ec20_device));
    }
     return close(fd);
}

ssize_t qusb_read(int fd, void* pbuf, size_t size) {
    return read(fd, pbuf, size);
}

ssize_t qusb_write(int fd, const void*pbuf, size_t size, int need_zlp) {
    if (ec20_device.desc > 0) {
        return usbfs_bulk_write(&ec20_device, 0,  pbuf, size, need_zlp);
    }
    
    return write(fd, pbuf, size);
}
#if 1
int qusb_find_ec20(int *idVendor, int *idProduct, int *interfaceNum) {
    quectel_find_usb_device(&ec20_device);
    if (ec20_device.desc) {
        *idVendor = ec20_device.idVendor;
        *idProduct = ec20_device.idProduct;
        *interfaceNum = ec20_device.bNumInterfaces;
        close(ec20_device.desc);
        memset(&ec20_device, 0, sizeof(ec20_device));
        return 0;
    }

    return ENODEV;
}
#endif
