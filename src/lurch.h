#ifndef __LURCH_H
# define __LURCH_H

#include <glib.h>
#include "jabber.h"
#include "pep.h"

# define LURCH_VERSION "0.7.0-1317-dev"
# define LURCH_AUTHOR "*author1317*"

#define JABBER_PROTOCOL_ID "prpl-jabber"

// see https://www.ietf.org/rfc/rfc3920.txt
#define JABBER_MAX_LEN_NODE 1023
#define JABBER_MAX_LEN_DOMAIN 1023
#define JABBER_MAX_LEN_BARE JABBER_MAX_LEN_NODE + JABBER_MAX_LEN_DOMAIN + 1

#define LURCH_ERR_STRING_ENCRYPT "There was an error encrypting the message and it was not sent. " \
                                 "You can try again, or try to find the problem by looking at the debug log."
#define LURCH_ERR_STRING_DECRYPT "There was an error decrypting an OMEMO message addressed to this device. " \
                                 "See the debug log for details."

#ifdef _WIN32
#define strnlen(a, b) (MIN(strlen(a), (b)))
#endif

typedef struct lurch_addr {
  char * jid;
  uint32_t device_id;
} lurch_addr;

extern const omemo_crypto_provider crypto;

int lurch_axc_prepare(const char * uname, axc_context * axc_ctx_p);

xmlnode* jabber_create_message_on_stream(JabberStream* js,
					 const char* type,
					 const char* to);

int lurch_dake_create_idake_msg(xmlnode** idakemsg_node_pp,
				gchar** err_msg_pp,
				JabberStream* js,
				const char* type,
				const char* to,
				uint32_t to_devid,
				uint32_t s_devid,
				const uint8_t* data,
				size_t len);

int lurch_dake_bundle_create_session(const char * uname,
				     const char * from,
				     const xmlnode * items_p,
				     axc_context * axc_ctx_p);

int lurch_bundle_request_do(JabberStream * js_p,
			    const char * to,
			    uint32_t device_id,
			    JabberIqCallback bundle_request_cb,
			    gpointer data_p);

void lurch_pep_own_devicelist_request_handler(JabberStream * js_p, const char * from, xmlnode * items_p);
void lurch_pep_own_devicelist_remove_faux_id(JabberStream * js_p, const char * from, xmlnode * items_p);
void lurch_delete_used_bundle(JabberStream* js_p, const GList* used_faux_devid);
void lurch_pep_own_devicelist_purge(JabberStream * js_p, const char * from, xmlnode * items_p);
void lurch_delete_faux_ids(const char* uname, const GList* l_id_to_del);

void lurch_addr_list_destroy_func(gpointer data);
int lurch_msg_finalize_encryption(JabberStream * js_p, axc_context * axc_ctx_p, omemo_message * om_msg_p, GList * addr_l_p, xmlnode ** msg_stanza_pp);
#endif /* __LURCH_H */
