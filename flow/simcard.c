// simcard.c

#include "simcard.h"
#include "delay.h"
#include "ctlcenter.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>

#define PowerH_GPRS  GPIO_SetBits(GPIOC,GPIO_Pin_6)     //�ߵ�ƽ
#define PowerL_GPRS  GPIO_ResetBits(GPIOC,GPIO_Pin_6)   //�͵�ƽ

#define AT_TEST         "AT\n"
#define AT_ECHO_CLOSE   "ATE0\n"
#define AT_POWER_DOWN   "AT+CPOWD=1\n"

#define AT_QUERY_CCID   "AT+CCID\n"
#define AT_QUERY_CSQ    "AT+CSQ\n"

#define AT_CG_CLASS     "AT+CGCLASS=\"B\"\n"
#define AT_CG_DCONT     "AT+CGDCONT=1,\"IP\",\"CMNET\"\n"
#define AT_CG_ATT       "AT+CGATT=1\n"
#define AT_CIP_CSGP     "AT+CIPCSGP=1,\"CMNET\"\n"
#define AT_CIP_START    "AT+CIPSTART=\"TCP\",\"zjhtht.cn\",\"3002\"\n"
//#define AT_CIP_START    "AT+CIPSTART=\"TCP\",\"munie.ddns.net\",\"5964\"\n"
#define AT_CIP_SEND     "AT+CIPSEND\n"

unsigned char const CGQ_CMD[8]={0x01, 0x03, 0x00, 0x08, 0x00, 0x02, 0x45, 0xC9};

static bool send_command_base(struct usart_session *sess,
    const char * restrict cmd, const char * restrict res, int res_offset)
{
    usart_rfifo_skip(sess, RFIFOREST(sess));
    usart_sendstr_session(sess, cmd);
    delay(1000);

    int result = memcmp(RFIFOP(sess, res_offset), res, strlen(res));
    usart_rfifo_skip(sess, RFIFOREST(sess));

    if (result == 0)
        return true;
    else
        return false;
}

static void send_command(struct usart_session *sess, const char * restrict cmd, int next)
{
    usart_sendstr_session(sess, cmd);
    delay(next);
}

static void simcard_open()
{
    PowerH_GPRS;
    delay(600);
    PowerL_GPRS;
    delay(600);
    delay(1500);
}

static void connect(struct simcard *sim, struct usart_session *sess)
{
    send_command(sess, AT_CG_CLASS, 1000);
    send_command(sess, AT_CG_DCONT, 1000);
    send_command(sess, AT_CG_ATT, 1000);
    send_command(sess, AT_CIP_CSGP, 1000);
    send_command(sess, AT_CIP_START, 5000);
    usart_rfifo_skip(sess, RFIFOREST(sess));
}

// simcard ======================================================================

static void simcard_usart_parse(struct usart_session *sess)
{
    struct simcard *sim = (struct simcard *)((char *)sess - (size_t)(&((struct simcard *)0)->sess));

    for (int i = sess->rdata_pos; i < sess->rdata_size; i++) {
        if (memcmp(RFIFOP(sess, i), "!A1?", 4) == 0) {
            memcpy(ctlcenter->modbus->wdata, CGQ_CMD, 8);
            break;
        } else if (memcmp(RFIFOP(sess, i), "!A2?", 4) == 0) {
            break;
        } else if (memcmp(RFIFOP(sess, i), "!A3?", 4) == 0) {
            simcard_send_msg_to_center(sim, "/flow/state?ccid=%s&csq=%s&voltage=%s\r\n",
                sim->ccid, sim->csq, sim->voltage);
            break;
        }
    }

    usart_rfifo_skip(sess, RFIFOREST(sess));
}

void simcard_init(struct simcard *sim)
{
    usart_init(&sim->sess, USART1, simcard_usart_parse);
    usart_add(&sim->sess);
    struct usart_session *sess = &sim->sess;

    redo:
    send_command_base(sess, AT_ECHO_CLOSE, "OK", 2);
    if (send_command_base(sess, AT_ECHO_CLOSE, "OK", 2))
        goto out;

    send_command_base(sess, AT_POWER_DOWN, "OK", 2);
    delay(10 * 1000);
    simcard_open();
    goto redo;

    out:
    if (simcard_update_ccid(sim) == -1) goto redo;
    return;
}

void simcard_final(struct simcard *sim)
{
    usart_del(&sim->sess);
}

void simcard_send_msg_to_center(struct simcard *sim, const char * restrict msg, ...)
{
    struct usart_session *sess = &sim->sess;

    va_list arg;
    va_start(arg, msg);
    vsprintf(sess->wdata, msg, arg);
    va_end(arg);

    redo:
    send_command(sess, AT_CIP_SEND, 500);
    if (memcmp(sess->rdata + sess->rdata_size - 9, "\r\nERROR\r\n", 9) == 0) {
        connect(sim, sess);
        goto redo;
    }
    send_command(sess, sess->wdata, 0);
    send_command(sess, "\x1a\r\n", 0);
}

int simcard_update_ccid(struct simcard *sim)
{
    struct usart_session *sess = &sim->sess;
    
    // query ccid, retval: "\r\n8986...\r\n"
    send_command(sess, AT_QUERY_CCID, 1000);
    if (strstr(RFIFOP(sess, 2), "8986") == NULL)
        return -1;

    memcpy(sim->ccid, RFIFOP(sess, 2), 20);
    usart_rfifo_skip(sess, RFIFOREST(sess));
    return 0;
}

int simcard_update_csq(struct simcard *sim)
{
    struct usart_session *sess = &sim->sess;
    
    // query csq, retval: "\r\n+CSQ: 19,0\r\n"
    send_command(sess, AT_QUERY_CSQ, 1000);
    if (strstr(RFIFOP(sess, 2), "+CSQ") == NULL)
        return -1;
    
    memcpy(sim->csq, RFIFOP(sess, 2 + 6), 2);
    usart_rfifo_skip(sess, RFIFOREST(sess));
    return 0;
}

int simcard_update_gps(struct simcard *sim)
{
    return 0;
}