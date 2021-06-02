#include "omemo_helper.h"

/**
 * Helps basic sanity checking of received XML.
 *
 * @param node_p Pointer to the current node.
 * @param next_node_func The function that returns the next node (e.g. child or sibling)
 * @param expected_name The name of the expected node.
 * @param next_node_pp Will be set to a pointer to the next node if it is the right one.
 * @return 0 on success, negative on error (specifically, OMEMO_ERR_MALFORMED_XML)
 */
static int expect_next_node(mxml_node_t * node_p, mxml_node_t * (*next_node_func)(mxml_node_t * node_p), char * expected_name, mxml_node_t ** next_node_pp) {
  mxml_node_t * next_node_p = next_node_func(node_p);
  if (!next_node_p) {
    return OMEMO_ERR_MALFORMED_XML;
  }

  const char * element_name = mxmlGetElement(next_node_p);
  if (!element_name) {
    return OMEMO_ERR_MALFORMED_XML;
  }

  if (strncmp(mxmlGetElement(next_node_p), expected_name, strlen(expected_name))) {
    return OMEMO_ERR_MALFORMED_XML;
  }
  *next_node_pp = next_node_p;
  return 0;
}

omemo_message* omemo_message_create_bare(void)
{
  omemo_message* msg_p = malloc(sizeof(omemo_message));
  do {
    if (!msg_p) break;
    memset(msg_p, 0, sizeof(omemo_message));
    msg_p->header_node_p = mxmlNewElement(MXML_NO_PARENT, HEADER_NODE_NAME);
  } while(0);
  return msg_p;
}

int omemo_message_init_key(omemo_message* msg_p, const omemo_crypto_provider * crypto_p)
{
  if (!msg_p || !msg_p->header_node_p) {
    return OMEMO_ERR_NULL;
  }
  int ret_val = 0;
  uint8_t * iv_p = NULL;
  gchar * iv_b64 = NULL;
  mxml_node_t * header_node_p = NULL;
  mxml_node_t * iv_node_p = NULL;
  uint8_t * key_p = NULL;

  ret_val = crypto_p->random_bytes_func(&iv_p, OMEMO_AES_GCM_IV_LENGTH, crypto_p->user_data_p);
  if (ret_val) {
    goto cleanup;
  }
  msg_p->iv_p = iv_p;
  msg_p->iv_len = OMEMO_AES_GCM_IV_LENGTH;
  iv_b64 = g_base64_encode(iv_p, OMEMO_AES_GCM_IV_LENGTH);
  iv_node_p = mxmlNewElement(header_node_p, IV_NODE_NAME);
  (void) mxmlNewOpaque(iv_node_p, iv_b64);

  ret_val = crypto_p->random_bytes_func(&key_p, OMEMO_AES_128_KEY_LENGTH + OMEMO_AES_GCM_TAG_LENGTH, crypto_p->user_data_p);
  if (ret_val) {
    goto cleanup;
  }

  msg_p->key_p = key_p;
  msg_p->key_len = OMEMO_AES_128_KEY_LENGTH;
  msg_p->tag_len = 0;

 cleanup:
  g_free(iv_b64);
  return ret_val;
}

int omemo_message_set_sender_devid(omemo_message* msg_p, uint32_t sender_device_id)
{
  if (!msg_p || !msg_p->header_node_p) {
    return OMEMO_ERR_NULL;
  }
  int ret_val = 0;
  gchar* device_id_string = g_strdup_printf("%u", sender_device_id);

  mxmlElementSetAttr(msg_p->header_node_p, HEADER_NODE_SID_ATTR_NAME, device_id_string);

  g_free(device_id_string);
  return ret_val;
}

int omemo_message_set_plain_msg(omemo_message* msg_p, const char* pl_msg)
{
  if (!msg_p || !msg_p->header_node_p) {
    return OMEMO_ERR_NULL;
  }
  int ret_val = 0;
  mxml_node_t * msg_node_p = NULL;

  msg_node_p = mxmlLoadString((void *) 0, pl_msg, MXML_OPAQUE_CALLBACK);
  if (!msg_node_p) {
    ret_val = OMEMO_ERR_MALFORMED_XML;
    goto cleanup;
  }
  msg_p->message_node_p = msg_node_p;

 cleanup:
  return ret_val;
}

int omemo_message_pre_encrypt(omemo_message* msg_p, const omemo_crypto_provider * crypto_p)
{
  if (!msg_p || !msg_p->header_node_p || !msg_p->message_node_p || !msg_p->key_p || !msg_p->iv_p ) {
    return OMEMO_ERR_NULL;
  }
  int ret_val = 0;
  mxml_node_t * body_node_p = NULL;
  const char * msg_text = NULL;
  uint8_t * ct_p = NULL;
  size_t ct_len = 0;
  gchar * payload_b64 = NULL;
  mxml_node_t * payload_node_p = NULL;
  uint8_t * tag_p = NULL;

  body_node_p = mxmlFindPath(msg_p->message_node_p, BODY_NODE_NAME);
  if (!body_node_p)
    goto cleanup;

  msg_text = mxmlGetOpaque(body_node_p);
  if (!msg_text) {
    ret_val = OMEMO_ERR_MALFORMED_XML;
    goto cleanup;
  }


  ret_val = crypto_p->aes_gcm_encrypt_func((uint8_t *) msg_text, strlen(msg_text),
					   msg_p->iv_p, msg_p->iv_len,
					   msg_p->key_p, msg_p->key_len,
					   OMEMO_AES_GCM_TAG_LENGTH,
					   crypto_p->user_data_p,
					   &ct_p, &ct_len,
					   &tag_p);
  if (ret_val) {
    goto cleanup;
  }

  msg_p->tag_len = OMEMO_AES_GCM_TAG_LENGTH;
  memcpy(msg_p->key_p + msg_p->key_len, tag_p, msg_p->tag_len);

  ret_val = expect_next_node(body_node_p, mxmlGetParent, BODY_NODE_NAME, &body_node_p);
  if (ret_val) {
    goto cleanup;
  }

  mxmlRemove(body_node_p);

  payload_b64 = g_base64_encode(ct_p, ct_len);
  payload_node_p = mxmlNewElement(MXML_NO_PARENT, PAYLOAD_NODE_NAME);
  (void) mxmlNewOpaque(payload_node_p, payload_b64);
  msg_p->payload_node_p = payload_node_p;

 cleanup:
  free(ct_p);
  g_free(payload_b64);
  free(tag_p);

  return ret_val;
}

int omemo_message_has_key(const omemo_message* msg_p)
{
  if (!msg_p || !msg_p->header_node_p ) return false;
  mxml_node_t * iv_node_p = mxmlFindPath(msg_p->header_node_p,
					 IV_NODE_NAME );
  return ((msg_p->key_p) && (msg_p->iv_p)
	  && iv_node_p && mxmlGetOpaque(iv_node_p));
}
