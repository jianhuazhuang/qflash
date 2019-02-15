#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>
#include <termios.h>
//#include <sys/signal.h>
#include <sys/wait.h>
#include "platform_def.h"

#include "atchannel.h"
#include "at_tok.h"

static pthread_mutex_t s_state_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_state_cond = PTHREAD_COND_INITIALIZER;
static int s_closed = 0;

static void waitForClose() {
    pthread_mutex_lock(&s_state_mutex);

    while (s_closed == 0) {
        pthread_cond_wait(&s_state_cond, &s_state_mutex);
    }

    pthread_mutex_unlock(&s_state_mutex);
}

/**
 * Called by atchannel when an unsolicited line appears
 * This is called on atchannel's reader thread. AT commands may
 * not be issued here
 */
static void onUnsolicited (const char *s, const char *sms_pdu) {
    if (s != NULL)
        LOGD("%s %s\n", __func__, s);
    if (sms_pdu != NULL)
        LOGD("%s %s\n", __func__, sms_pdu);
}

/* Called on command or reader thread */
static void onATReaderClosed() {
    LOGD("AT channel closed\n");
    at_close();
    s_closed = 1;
    pthread_mutex_lock(&s_state_mutex);
    pthread_cond_broadcast (&s_state_cond);
    pthread_mutex_unlock(&s_state_mutex);
}

/* Called on command thread */
static void onATTimeout() {
    LOGD("AT channel timeout; closing\n");
    at_close();
    s_closed = 1;
    pthread_mutex_lock(&s_state_mutex);
    pthread_cond_broadcast (&s_state_cond);
    pthread_mutex_unlock(&s_state_mutex);
}

static void clean_up_child_process(int signal_num) {
    /* clean up child process */
#ifndef WNOHANG
#define WNOHANG 0x00000001
#endif
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

static int getRadioState(void) {
    ATResponse *p_response = NULL;
    int err;
    char *line;
    int response[1];

    err = at_send_command_singleline("AT+CFUN?", "+CFUN:", &p_response);
    if (err < 0 || p_response->success == 0) {
        // assume radio is off
        goto error;
    }

    line = p_response->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, response);
    if (err < 0) goto error;

    at_response_free(p_response);

    LOGD("%s %d\n", __func__, response[0]);
    return response[0];

error:
    LOGE("%s return error\n", __func__);
    at_response_free(p_response);
    return -1;
}

static int getSignalStrength(void) {
    ATResponse *p_response = NULL;
    int err;
    char *line;
    int response[2];

    err = at_send_command_singleline("AT+CSQ", "+CSQ:", &p_response);
    if (err < 0 || p_response->success == 0) {
        goto error;
    }

    line = p_response->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &(response[0]));
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &(response[1]));
    if (err < 0) goto error;

    at_response_free(p_response);

    LOGD("%s %d, %d\n", __func__, response[0], response[1]);
    return response[0];

error:
    LOGE("%s return error\n", __func__);
    at_response_free(p_response);
    return -1;
}

static int getRegistrationState(void) {
    ATResponse *p_response = NULL;
    int err;
    char *line, *p;
    int response[4] = {-1, -1, -1, -1};
    int skip;
    int commas;

    err = at_send_command_singleline("AT+CGREG?", "+CGREG:", &p_response);
    if (err < 0 || p_response->success == 0) {
        goto error;
    }

    line = p_response->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0) goto error;

    /* Ok you have to be careful here
     * The solicited version of the CREG response is
     * +CREG: n, stat, [lac, cid]
     * and the unsolicited version is
     * +CREG: stat, [lac, cid]
     * The <n> parameter is basically "is unsolicited creg on?"
     * which it should always be
     *
     * Now we should normally get the solicited version here,
     * but the unsolicited version could have snuck in
     * so we have to handle both
     *
     * Also since the LAC and CID are only reported when registered,
     * we can have 1, 2, 3, or 4 arguments here
     *
     * finally, a +CGREG: answer may have a fifth value that corresponds
     * to the network type, as in;
     *
     *   +CGREG: n, stat [,lac, cid [,networkType]]
     */

    /* count number of commas */
    commas = 0;
    for (p = line ; *p != '\0' ; p++) {
        if (*p == ',') commas++;
    }

    switch (commas) {
    case 0: /* +CREG: <stat> */
        err = at_tok_nextint(&line, &response[0]);
        if (err < 0) goto error;
        response[1] = -1;
        response[2] = -1;
        break;

    case 1: /* +CREG: <n>, <stat> */
        err = at_tok_nextint(&line, &skip);
        if (err < 0) goto error;
        err = at_tok_nextint(&line, &response[0]);
        if (err < 0) goto error;
        response[1] = -1;
        response[2] = -1;
        if (err < 0) goto error;
        break;

    case 2: /* +CREG: <stat>, <lac>, <cid> */
        err = at_tok_nextint(&line, &response[0]);
        if (err < 0) goto error;
        err = at_tok_nexthexint(&line, &response[1]);
        if (err < 0) goto error;
        err = at_tok_nexthexint(&line, &response[2]);
        if (err < 0) goto error;
        break;

    case 3: /* +CREG: <n>, <stat>, <lac>, <cid> */
        err = at_tok_nextint(&line, &skip);
        if (err < 0) goto error;
        err = at_tok_nextint(&line, &response[0]);
        if (err < 0) goto error;
        err = at_tok_nexthexint(&line, &response[1]);
        if (err < 0) goto error;
        err = at_tok_nexthexint(&line, &response[2]);
        if (err < 0) goto error;
        break;

    /* special case for CGREG, there is a fourth parameter
     * that is the network type (unknown/gprs/edge/umts)
     */
    case 4: /* +CGREG: <n>, <stat>, <lac>, <cid>, <networkType> */
        err = at_tok_nextint(&line, &skip);
        if (err < 0) goto error;
        err = at_tok_nextint(&line, &response[0]);
        if (err < 0) goto error;
        err = at_tok_nexthexint(&line, &response[1]);
        if (err < 0) goto error;
        err = at_tok_nexthexint(&line, &response[2]);
        if (err < 0) goto error;
        err = at_tok_nexthexint(&line, &response[3]);
        if (err < 0) goto error;
        break;

    default:
        goto error;
    }

    at_response_free(p_response);

    LOGD("%s stat=%d, lac=%d, cid=%X, networkType=%X\n", __func__, response[0], response[1], response[2], response[3]);
    return response[0];

error:
    LOGE("%s return error\n", __func__);
    at_response_free(p_response);
    return -1;
}

static int getOperator(void) {
    int err;
    int i;
    int skip;
    ATLine *p_cur;
    char *response[3];
    ATResponse *p_response = NULL;

    memset(response, 0, sizeof(response));

    err = at_send_command_multiline("AT+COPS=3,0;+COPS?;+COPS=3,1;+COPS?;+COPS=3,2;+COPS?", "+COPS:", &p_response);

    /* we expect 3 lines here:
     * +COPS: 0,0,"T - Mobile"
     * +COPS: 0,1,"TMO"
     * +COPS: 0,2,"310170"
     */

    if (err != 0) goto error;

    for (i = 0, p_cur = p_response->p_intermediates; p_cur != NULL; p_cur = p_cur->p_next, i++) {
        char *line = p_cur->line;

        err = at_tok_start(&line);
        if (err < 0) goto error;

        err = at_tok_nextint(&line, &skip);
        if (err < 0) goto error;

        // If we're unregistered, we may just get
        // a "+COPS: 0" response
        if (!at_tok_hasmore(&line)) {
            response[i] = NULL;
            continue;
        }

        err = at_tok_nextint(&line, &skip);
        if (err < 0) goto error;

        // a "+COPS: 0, n" response is also possible
        if (!at_tok_hasmore(&line)) {
            response[i] = NULL;
            continue;
        }

        err = at_tok_nextstr(&line, &(response[i]));
        if (err < 0) goto error;
    }

    if (i != 3) {
        /* expect 3 lines exactly */
        goto error;
    }

    LOGD("%s %s, %s, %s\n", __func__, response[0], response[1], response[2]);
    at_response_free(p_response);

    return 0;
error:
    LOGE("requestOperator must not return error when radio is on");
    at_response_free(p_response);
    return 0;
}

static int getRemoteIP(char address[]) {
    ATResponse *p_response = NULL;
    int err;
    char *line;
    int response[2];

    address[0] = '\0';
    err = at_send_command_singleline("AT+CGPADDR=1", "+CGPADDR:", &p_response);
    if (err < 0 || p_response == NULL || p_response->success == 0) {
        goto error;
    }

    line = p_response->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &(response[0]));
    if (err < 0) goto error;

    err = at_tok_nextstr(&line, &address);
    if (err < 0) goto error;

    at_response_free(p_response);

    LOGD("%s %s\n", __func__, address);
    return response[0];

error:
    LOGE("%s return error\n", __func__);
    at_response_free(p_response);
    return -1;
}

extern "C" int at_open(int fd, ATUnsolHandler h);

static int serial_open(const char *device_path) {
    int fd = -1;
    int ret;

    while  (fd < 0) {
        fd = open (device_path, O_RDWR);
        if ( fd >= 0 ) {
            /* disable echo on serial ports */
            struct termios  ios;
            memset(&ios, 0, sizeof(ios));
            tcgetattr( fd, &ios );
            cfmakeraw(&ios);
            ios.c_lflag = 0;  /* disable ECHO, ICANON, etc... */
            cfsetispeed(&ios, B115200);
            cfsetospeed(&ios, B115200);
            tcsetattr( fd, TCSANOW, &ios );
            LOGD("open device %s correctly\n", device_path);
            tcflush(fd, TCIOFLUSH);
        }

        if (fd < 0) {
            LOGE("open device %s error for %s\n", device_path, strerror(errno));
            sleep(1);
        }
    }

    ret = at_open(fd, onUnsolicited);
    if (ret < 0) {
        LOGE ("AT error %d on at_open\n", ret);
    }
    s_closed = 0;

    return fd;
}
