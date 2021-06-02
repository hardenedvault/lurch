#ifndef _OMEMO_HELPER_H_
#define _OMEMO_HELPER_H_

#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <mxml.h>
#include <glib.h>
#include "libomemo.h"
#include "libomemo_defs.h"

#ifdef __cplusplus
extern "C" {
#endif
#if 0
}
#endif

#define DELAY_URN "urn:xmpp:delay"

struct omemo_message {
  mxml_node_t * message_node_p;
  mxml_node_t * header_node_p;
  mxml_node_t * payload_node_p;
  uint8_t * key_p;
  size_t key_len;
  uint8_t * iv_p;
  size_t iv_len;
  size_t tag_len; //tag is appended to key buf, i.e. tag_p = key_p + key_len
};

omemo_message* omemo_message_create_bare(void);
int omemo_message_init_key(omemo_message* msg_p, const omemo_crypto_provider * crypto_p);
int omemo_message_set_sender_devid(omemo_message* msg_p, uint32_t sender_device_id);
int omemo_message_set_plain_msg(omemo_message* msg_p, const char* pl_msg);
int omemo_message_pre_encrypt(omemo_message* msg_p, const omemo_crypto_provider * crypto_p);
int omemo_message_has_key(const omemo_message* msg_p);
#if 0
{
#endif
#ifdef __cplusplus
}
#endif

#endif
