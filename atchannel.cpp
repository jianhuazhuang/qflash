/* //device/system/reference-ril/atchannel.c
**
** Copyright 2006, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include "atchannel.h"
#include "at_tok.h"

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
//#include <sys/timeb.h>
#include <stdarg.h>

FILE *g_log_file = NULL;
static pthread_mutex_t s_log_mutex = PTHREAD_MUTEX_INITIALIZER;
static char s_log_line[4096];

int logd(const char *fmt, ...) {

    int ret;
    va_list ap;
    struct tm *t;
    time_t tt;


    pthread_mutex_lock(&s_log_mutex);
    time(&tt);
    t = localtime(&tt);


    sprintf(s_log_line, "[%02d-%02d_%02d:%02d:%02d:%03d] ",
            t->tm_year + 1900,
            t->tm_mon+1,
            t->tm_mday,
            t->tm_hour,
            t->tm_min,
            t->tm_sec);


    va_start(ap, fmt);
    vsprintf(s_log_line + strlen(s_log_line), fmt, ap);
    va_end(ap);

    ret = printf("%s", s_log_line);
    if (g_log_file != NULL)
        fprintf(g_log_file, "%s", s_log_line);

    pthread_mutex_unlock(&s_log_mutex);

    return (ret);
}

#define NUM_ELEMS(x) (sizeof(x)/sizeof(x[0]))

#define MAX_AT_RESPONSE (8 * 1024)
#define HANDSHAKE_RETRY_COUNT 8
#define HANDSHAKE_TIMEOUT_MSEC 500 //250

static pthread_t s_tid_reader;
static int s_fd = -1;    /* fd of the AT channel */
static ATUnsolHandler s_unsolHandler;

/* for input buffering */

static char s_ATBuffer[MAX_AT_RESPONSE+1];
static char *s_ATBufferCur = s_ATBuffer;

static int s_readCount = 0;

#if AT_DEBUG
void  AT_DUMP(const char*  prefix, const char*  buff, int  len) {
    if (len < 0)
        len = strlen(buff);
    LOGD("%.*s", len, buff);
}
#endif

/*
 * for current pending command
 * these are protected by s_commandmutex
 */

static pthread_mutex_t s_commandmutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_commandcond = PTHREAD_COND_INITIALIZER;

static ATCommandType s_type;
static const char *s_responsePrefix = NULL;
static const char *s_smsPDU = NULL;
static const char *s_raw_data = NULL;
static size_t s_raw_len;
static ATResponse *sp_response = NULL;
static long long s_timeoutMsec = 5000;

static void (*s_onTimeout)(void) = NULL;
static void (*s_onReaderClosed)(void) = NULL;
static int s_readerClosed;

static void onReaderClosed();
static int writeCtrlZ (const char *s);
static int writeline (const char *s);
static int writeraw (const char *s, size_t len);

/** returns 1 if line starts with prefix, 0 if it does not */
static int strStartsWith(const char *line, const char *prefix) {
    for ( ; *line != '\0' && *prefix != '\0' ; line++, prefix++) {
        if (*line != *prefix) {
            return 0;
        }
    }

    return *prefix == '\0';
}

#ifndef USE_NP
static void setTimespecRelative(struct timespec *p_ts, long long msec) {
    struct timeval tv;

    msec = (msec / 1000 + 1) * 1000; //must larger than 1000, or will return EINVAL, i donot know why

    gettimeofday(&tv, (struct timezone *) NULL);

    /* what's really funny about this is that I know
       pthread_cond_timedwait just turns around and makes this
       a relative time again */
    p_ts->tv_sec = tv.tv_sec + (msec / 1000);
    p_ts->tv_nsec = (tv.tv_usec + (msec % 1000) * 1000L ) * 1000L;
}
#endif /*USE_NP*/

/** add an intermediate response to sp_response*/
static void addIntermediate(const char *line) {
    ATLine *p_new;

    p_new = (ATLine  *) malloc(sizeof(ATLine));

    p_new->line = strdup(line);

    /* note: this adds to the head of the list, so the list
       will be in reverse order of lines received. the order is flipped
       again before passing on to the command issuer */
    p_new->p_next = sp_response->p_intermediates;
    sp_response->p_intermediates = p_new;
}


/**
 * returns 1 if line is a final response indicating error
 * See 27.007 annex B
 * WARNING: NO CARRIER and others are sometimes unsolicited
 */
static const char * s_finalResponsesError[] = {
    "ERROR",
    "+CMS ERROR:",
    "+CME ERROR:",
    "NO CARRIER", /* sometimes! */
    "NO ANSWER",
    "NO DIALTONE",
};
static int isFinalResponseError(const char *line) {
    size_t i;

    for (i = 0 ; i < NUM_ELEMS(s_finalResponsesError) ; i++) {
        if (strStartsWith(line, s_finalResponsesError[i])) {
            return 1;
        }
    }

    return 0;
}

/**
 * returns 1 if line is a final response indicating success
 * See 27.007 annex B
 * WARNING: NO CARRIER and others are sometimes unsolicited
 */
static const char * s_finalResponsesSuccess[] = {
    "OK",
    "+QIND: \"FOTA\",\"END\",0",
    "CONNECT"       /* some stacks start up data on another channel */
};
static int isFinalResponseSuccess(const char *line) {
    size_t i;

    for (i = 0 ; i < NUM_ELEMS(s_finalResponsesSuccess) ; i++) {
        if (strStartsWith(line, s_finalResponsesSuccess[i])) {
            return 1;
        }
    }

    return 0;
}

#if 0
/**
 * returns 1 if line is a final response, either  error or success
 * See 27.007 annex B
 * WARNING: NO CARRIER and others are sometimes unsolicited
 */
static int isFinalResponse(const char *line) {
    return isFinalResponseSuccess(line) || isFinalResponseError(line);
}
#endif

/**
 * returns 1 if line is the first line in (what will be) a two-line
 * SMS unsolicited response
 */
static const char * s_smsUnsoliciteds[] = {
    "+CMT:",
    "+CDS:",
    "+CBM:",
    "+CMTI:"
};
static int isSMSUnsolicited(const char *line) {
    size_t i;

    for (i = 0 ; i < NUM_ELEMS(s_smsUnsoliciteds) ; i++) {
        if (strStartsWith(line, s_smsUnsoliciteds[i])) {
            return 1;
        }
    }

    return 0;
}


/** assumes s_commandmutex is held */
static void handleFinalResponse(const char *line) {
    sp_response->finalResponse = strdup(line);

    pthread_cond_signal(&s_commandcond);
}

static void handleUnsolicited(const char *line) {
    if (s_unsolHandler != NULL) {
        s_unsolHandler(line, NULL);
    }
}

static void processLine(const char *line) {
    pthread_mutex_lock(&s_commandmutex);

    if (sp_response == NULL) {
        /* no command pending */
        handleUnsolicited(line);
    } else if (s_raw_data != NULL && 0 == strcmp(line, "CONNECT")) {
        sleep(1); //for EC20
        writeraw(s_raw_data, s_raw_len);
        s_raw_data = NULL;
    } else if (isFinalResponseSuccess(line)) {
        sp_response->success = 1;
        handleFinalResponse(line);
    } else if (isFinalResponseError(line)) {
        sp_response->success = 0;
        handleFinalResponse(line);
    } else if (s_smsPDU != NULL && 0 == strcmp(line, "> ")) {
        // See eg. TS 27.005 4.3
        // Commands like AT+CMGS have a "> " prompt
        writeCtrlZ(s_smsPDU);
        s_smsPDU = NULL;
    } else switch (s_type) {
        case NO_RESULT:
            handleUnsolicited(line);
            break;
        case NUMERIC:
            if (sp_response->p_intermediates == NULL
                    && isdigit(line[0])
               ) {
                addIntermediate(line);
            } else {
                /* either we already have an intermediate response or
                   the line doesn't begin with a digit */
                handleUnsolicited(line);
            }
            break;
        case SINGLELINE:
            if (sp_response->p_intermediates == NULL
                    && strStartsWith (line, s_responsePrefix)
               ) {
                addIntermediate(line);
            } else {
                /* we already have an intermediate response */
                handleUnsolicited(line);
            }
            break;
        case MULTILINE:
            if (strStartsWith (line, s_responsePrefix)) {
                addIntermediate(line);
            } else {
                handleUnsolicited(line);
            }
            break;

        default: /* this should never be reached */
            LOGE("Unsupported AT command type %d\n", s_type);
            handleUnsolicited(line);
            break;
        }

    pthread_mutex_unlock(&s_commandmutex);
}


/**
 * Returns a pointer to the end of the next line
 * special-cases the "> " SMS prompt
 *
 * returns NULL if there is no complete line
 */
static char * findNextEOL(char *cur) {
    if (cur[0] == '>' && cur[1] == ' ' && cur[2] == '\0') {
        /* SMS prompt character...not \r terminated */
        return cur+2;
    }

    // Find next newline
    while (*cur != '\0' && *cur != '\r' && *cur != '\n') cur++;

    return *cur == '\0' ? NULL : cur;
}


/**
 * Reads a line from the AT channel, returns NULL on timeout.
 * Assumes it has exclusive read access to the FD
 *
 * This line is valid only until the next call to readline
 *
 * This function exists because as of writing, android libc does not
 * have buffered stdio.
 */

static const char *readline() {
    ssize_t count;

    char *p_read = NULL;
    char *p_eol = NULL;
    char *ret;

    /* this is a little odd. I use *s_ATBufferCur == 0 to
     * mean "buffer consumed completely". If it points to a character, than
     * the buffer continues until a \0
     */
    if (*s_ATBufferCur == '\0') {
        /* empty buffer */
        s_ATBufferCur = s_ATBuffer;
        *s_ATBufferCur = '\0';
        p_read = s_ATBuffer;
    } else {   /* *s_ATBufferCur != '\0' */
        /* there's data in the buffer from the last read */

        // skip over leading newlines
        while (*s_ATBufferCur == '\r' || *s_ATBufferCur == '\n')
            s_ATBufferCur++;

        p_eol = findNextEOL(s_ATBufferCur);

        if (p_eol == NULL) {
            /* a partial line. move it up and prepare to read more */
            size_t len;

            len = strlen(s_ATBufferCur);

            memmove(s_ATBuffer, s_ATBufferCur, len + 1);
            p_read = s_ATBuffer + len;
            s_ATBufferCur = s_ATBuffer;
        }
        /* Otherwise, (p_eol !- NULL) there is a complete line  */
        /* that will be returned the while () loop below        */
    }

    while (p_eol == NULL) {
        if (0 == MAX_AT_RESPONSE - (p_read - s_ATBuffer)) {
            LOGE("ERROR: Input line exceeded buffer\n");
            /* ditch buffer and start over again */
            s_ATBufferCur = s_ATBuffer;
            *s_ATBufferCur = '\0';
            p_read = s_ATBuffer;
        }

        do {
            count = read(s_fd, p_read,
                         MAX_AT_RESPONSE - (p_read - s_ATBuffer));
        } while (count < 0 && errno == EINTR);

        if (count > 0) {
            AT_DUMP( "<< ", p_read, count );
            s_readCount += count;

            p_read[count] = '\0';

            // skip over leading newlines
            while (*s_ATBufferCur == '\r' || *s_ATBufferCur == '\n')
                s_ATBufferCur++;

            p_eol = findNextEOL(s_ATBufferCur);
            p_read += count;
        } else if (count <= 0) {
            /* read error encountered or EOF reached */
            if(count == 0) {
                LOGD("atchannel: EOF reached\n");
            } else {
                LOGD("atchannel: read error %s\n", strerror(errno));
            }
            return NULL;
        }
    }

    /* a full line in the buffer. Place a \0 over the \r and return */

    ret = s_ATBufferCur;
    *p_eol = '\0';
    s_ATBufferCur = p_eol + 1; /* this will always be <= p_read,    */
    /* and there will be a \0 at *p_read */

    LOGD("AT< %s\n", ret);
    return ret;
}


static void onReaderClosed() {
    LOGE("%s\n", __func__);
    if (s_onReaderClosed != NULL && s_readerClosed == 0) {

        pthread_mutex_lock(&s_commandmutex);

        s_readerClosed = 1;

        pthread_cond_signal(&s_commandcond);

        pthread_mutex_unlock(&s_commandmutex);

        s_onReaderClosed();
    }
}


static void *readerLoop(void *arg) {
    for (;;) {
        const char * line;

        line = readline();

        if (line == NULL) {
            break;
        }

        if(isSMSUnsolicited(line)) {
            char *line1;
            const char *line2;

            // The scope of string returned by 'readline()' is valid only
            // till next call to 'readline()' hence making a copy of line
            // before calling readline again.
            line1 = strdup(line);
            line2 = readline();

            if (line2 == NULL) {
                break;
            }

            if (s_unsolHandler != NULL) {
                s_unsolHandler (line1, line2);
            }
            free(line1);
        } else {
            processLine(line);
        }
    }

    onReaderClosed();

    return NULL;
}

/**
 * Sends string s to the radio with a \r appended.
 * Returns AT_ERROR_* on error, 0 on success
 *
 * This function exists because as of writing, android libc does not
 * have buffered stdio.
 */
static int writeline (const char *s) {
    size_t cur = 0;
    size_t len = strlen(s);
    ssize_t written;
    static char at_command[64];

    if (s_fd < 0 || s_readerClosed > 0) {
        return AT_ERROR_CHANNEL_CLOSED;
    }

    LOGD("AT> %s\n", s);

    AT_DUMP( ">> ", s, strlen(s) );

#if 1 //send '\r' maybe fail via USB controller: Intel Corporation 7 Series/C210 Series Chipset Family USB xHCI Host Controller (rev 04)
    if (len < (sizeof(at_command) - 1)) {
        strcpy(at_command, s);
        at_command[len++] = '\r';
        s = (const char *)at_command;
    }
#endif

    /* the main string */
    while (cur < len) {
        do {
            written = write (s_fd, s + cur, len - cur);
        } while (written < 0 && errno == EINTR);

        if (written < 0) {
            return AT_ERROR_GENERIC;
        }

        cur += written;
    }

#if 1 //Quectel send '\r' maybe fail via USB controller: Intel Corporation 7 Series/C210 Series Chipset Family USB xHCI Host Controller (rev 04)
    if (s == (const char *)at_command) {
        return 0;
    }
#endif

    /* the \r  */

    do {
        written = write (s_fd, "\r" , 1);
    } while ((written < 0 && errno == EINTR) || (written == 0));

    if (written < 0) {
        return AT_ERROR_GENERIC;
    }

    return 0;
}
static int writeCtrlZ (const char *s) {
    size_t cur = 0;
    size_t len = strlen(s);
    ssize_t written;

    if (s_fd < 0 || s_readerClosed > 0) {
        return AT_ERROR_CHANNEL_CLOSED;
    }

    LOGD("AT> %s^Z\n", s);

    AT_DUMP( ">* ", s, strlen(s) );

    /* the main string */
    while (cur < len) {
        do {
            written = write (s_fd, s + cur, len - cur);
        } while (written < 0 && errno == EINTR);

        if (written < 0) {
            return AT_ERROR_GENERIC;
        }

        cur += written;
    }

    /* the ^Z  */

    do {
        written = write (s_fd, "\032" , 1);
    } while ((written < 0 && errno == EINTR) || (written == 0));

    if (written < 0) {
        return AT_ERROR_GENERIC;
    }

    return 0;
}

static int writeraw (const char *s, size_t len) {
    size_t cur = 0;
    ssize_t written;

    if (s_fd < 0 || s_readerClosed > 0) {
        return AT_ERROR_CHANNEL_CLOSED;
    }

    /* the main string */
    while (cur < len) {
        do {
            written = write (s_fd, s + cur, len - cur);
        } while (written < 0 && errno == EINTR);

        if (written < 0) {
            return AT_ERROR_GENERIC;
        }

        cur += written;
    }

    if (written < 0) {
        return AT_ERROR_GENERIC;
    }

    return 0;
}

static void clearPendingCommand() {
    if (sp_response != NULL) {
        at_response_free(sp_response);
    }

    sp_response = NULL;
    s_responsePrefix = NULL;
    s_smsPDU = NULL;
}


/**
 * Starts AT handler on stream "fd'
 * returns 0 on success, -1 on error
 */
int at_open(int fd, ATUnsolHandler h) {
    int ret;
    pthread_attr_t attr;

    s_fd = fd;
    s_unsolHandler = h;
    s_readerClosed = 0;

    s_responsePrefix = NULL;
    s_smsPDU = NULL;
    sp_response = NULL;

    pthread_attr_init (&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    ret = pthread_create(&s_tid_reader, &attr, readerLoop, &attr);

    if (ret < 0) {
        LOGE("readerLoop create fail!\n");
        perror ("pthread_create\n");
        return -1;
    }

    return 0;
}

/* FIXME is it ok to call this from the reader and the command thread? */
void at_close() {
    LOGE("at_close\n");
    if (s_fd >= 0) {
        close(s_fd);
    }
    s_fd = -1;

    pthread_mutex_lock(&s_commandmutex);

    s_readerClosed = 1;

    pthread_cond_signal(&s_commandcond);

    pthread_mutex_unlock(&s_commandmutex);

    /* the reader thread should eventually die */
}

static ATResponse * at_response_new() {
    return (ATResponse *) calloc(1, sizeof(ATResponse));
}

void at_response_free(ATResponse *p_response) {
    ATLine *p_line;

    if (p_response == NULL) return;

    p_line = p_response->p_intermediates;

    while (p_line != NULL) {
        ATLine *p_toFree;

        p_toFree = p_line;
        p_line = p_line->p_next;

        free(p_toFree->line);
        free(p_toFree);
    }

    free (p_response->finalResponse);
    free (p_response);
}

/**
 * The line reader places the intermediate responses in reverse order
 * here we flip them back
 */
static void reverseIntermediates(ATResponse *p_response) {
    ATLine *pcur,*pnext;

    pcur = p_response->p_intermediates;
    p_response->p_intermediates = NULL;

    while (pcur != NULL) {
        pnext = pcur->p_next;
        pcur->p_next = p_response->p_intermediates;
        p_response->p_intermediates = pcur;
        pcur = pnext;
    }
}

/**
 * Internal send_command implementation
 * Doesn't lock or call the timeout callback
 *
 * timeoutMsec == 0 means infinite timeout
 */
static int at_send_command_full_nolock (const char *command, ATCommandType type,
                                        const char *responsePrefix, const char *smspdu,
                                        long long timeoutMsec, ATResponse **pp_outResponse) {
    int err = 0;
#ifndef USE_NP
    struct timespec ts;
#endif /*USE_NP*/

    if(sp_response != NULL) {
        err = AT_ERROR_COMMAND_PENDING;
        goto error;
    }

    if (command != NULL)
        err = writeline (command);

    if (err < 0) {
        goto error;
    }

    s_type = type;
    s_responsePrefix = responsePrefix;
    s_smsPDU = smspdu;
    sp_response = at_response_new();
    if (s_timeoutMsec == 0)
        timeoutMsec = s_timeoutMsec;

#ifndef USE_NP
    if (timeoutMsec != 0) {
        setTimespecRelative(&ts, timeoutMsec);
    }
#endif /*USE_NP*/

    while (sp_response->finalResponse == NULL && s_readerClosed == 0) {
        if (timeoutMsec != 0) {
#ifdef USE_NP
            err = pthread_cond_timeout_np(&s_commandcond, &s_commandmutex, timeoutMsec);
#else
            err = pthread_cond_timedwait(&s_commandcond, &s_commandmutex, &ts);
#endif /*USE_NP*/
        } else {
            err = pthread_cond_wait(&s_commandcond, &s_commandmutex);
        }

        if (err == ETIMEDOUT) {
            err = AT_ERROR_TIMEOUT;
            goto error;
        }
    }

    if (pp_outResponse == NULL) {
        at_response_free(sp_response);
    } else {
        /* line reader stores intermediate responses in reverse order */
        reverseIntermediates(sp_response);
        *pp_outResponse = sp_response;
    }

    sp_response = NULL;

    if(s_readerClosed > 0) {
        err = AT_ERROR_CHANNEL_CLOSED;
        goto error;
    }

    err = 0;
error:
    clearPendingCommand();

    return err;
}

/**
 * Internal send_command implementation
 *
 * timeoutMsec == 0 means infinite timeout
 */
static int at_send_command_full (const char *command, ATCommandType type,
                                 const char *responsePrefix, const char *smspdu,
                                 long long timeoutMsec, ATResponse **pp_outResponse) {
    int err;

    if (0 != pthread_equal(s_tid_reader, pthread_self())) {
        /* cannot be called from reader thread */
        return AT_ERROR_INVALID_THREAD;
    }

    pthread_mutex_lock(&s_commandmutex);

    err = at_send_command_full_nolock(command, type,
                                      responsePrefix, smspdu,
                                      timeoutMsec, pp_outResponse);

    pthread_mutex_unlock(&s_commandmutex);

    if (err == AT_ERROR_TIMEOUT && s_onTimeout != NULL) {
        s_onTimeout();
    }

    return err;
}


/**
 * Issue a single normal AT command with no intermediate response expected
 *
 * "command" should not include \r
 * pp_outResponse can be NULL
 *
 * if non-NULL, the resulting ATResponse * must be eventually freed with
 * at_response_free
 */
int at_send_command (const char *command, ATResponse **pp_outResponse) {
    int err;

    err = at_send_command_full (command, NO_RESULT, NULL,
                                NULL, 0, pp_outResponse);

    return err;
}


int at_send_command_singleline (const char *command,
                                const char *responsePrefix,
                                ATResponse **pp_outResponse) {
    int err;

    err = at_send_command_full (command, SINGLELINE, responsePrefix,
                                NULL, 0, pp_outResponse);

    if (err == 0 && pp_outResponse != NULL
            && (*pp_outResponse)->success > 0
            && (*pp_outResponse)->p_intermediates == NULL
       ) {
        /* successful command must have an intermediate response */
        at_response_free(*pp_outResponse);
        *pp_outResponse = NULL;
        return AT_ERROR_INVALID_RESPONSE;
    }

    return err;
}


int at_send_command_numeric (const char *command,
                             ATResponse **pp_outResponse) {
    int err;

    err = at_send_command_full (command, NUMERIC, NULL,
                                NULL, 0, pp_outResponse);

    if (err == 0 && pp_outResponse != NULL
            && (*pp_outResponse)->success > 0
            && (*pp_outResponse)->p_intermediates == NULL
       ) {
        /* successful command must have an intermediate response */
        at_response_free(*pp_outResponse);
        *pp_outResponse = NULL;
        return AT_ERROR_INVALID_RESPONSE;
    }

    return err;
}


int at_send_command_sms (const char *command,
                         const char *pdu,
                         const char *responsePrefix,
                         ATResponse **pp_outResponse) {
    int err;

    err = at_send_command_full (command, SINGLELINE, responsePrefix,
                                pdu, 0, pp_outResponse);

    if (err == 0 && pp_outResponse != NULL
            && (*pp_outResponse)->success > 0
            && (*pp_outResponse)->p_intermediates == NULL
       ) {
        /* successful command must have an intermediate response */
        at_response_free(*pp_outResponse);
        *pp_outResponse = NULL;
        return AT_ERROR_INVALID_RESPONSE;
    }

    return err;
}

int at_send_command_multiline (const char *command,
                               const char *responsePrefix,
                               ATResponse **pp_outResponse) {
    int err;

    err = at_send_command_full (command, MULTILINE, responsePrefix,
                                NULL, 0, pp_outResponse);

    return err;
}

int at_send_command_raw (const char *command,
                         const char *raw_data, unsigned int raw_len,
                         const char *responsePrefix,
                         ATResponse **pp_outResponse) {
    int err;

    s_raw_data = raw_data;
    s_raw_len = raw_len;
    err = at_send_command_full (command, SINGLELINE, responsePrefix,
                                NULL, 0, pp_outResponse);

    return err;
}

/** This callback is invoked on the command thread */
void at_set_on_timeout(void (*onTimeout)(void), long long timeoutMsec) {
    s_onTimeout = onTimeout;
    if (timeoutMsec != 0)
        s_timeoutMsec = timeoutMsec;
}

/**
 *  This callback is invoked on the reader thread (like ATUnsolHandler)
 *  when the input stream closes before you call at_close
 *  (not when you call at_close())
 *  You should still call at_close()
 */
void at_set_on_reader_closed(void (*onClose)(void)) {
    s_onReaderClosed = onClose;
}
