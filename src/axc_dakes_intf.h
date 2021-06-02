#ifndef _AXC_DAKES_INTF_H_
#define _AXC_DAKES_INTF_H_

#include "cachectx.h"
#include "libomemo.h"
#include <purple.h>

#ifdef __cplusplus
extern "C" {
#endif
#if 0
}
#endif

#define IDAKE_HINT "?idk??"
#define TERM_HINT "?term?"

int cachectx_init_by_name(const char* name, axc_context_dake_cache** ctx_pp);
GHashTable* get_acc_axc_ctx_map(void);
void init_acc_axc_ctx_map(void);
void reset_acc_axc_ctx_map(void);
axc_context_dake_cache* query_axc_ctx_by_name(GHashTable* map, const char* uname);
void insert_axc_ctx_by_name(GHashTable* map, const char* uname, axc_context_dake_cache* ctx);
int axc_ext_save_idkey(axc_context* ctx, const char* user, uint32_t devid,
		       ec_public_key* idkey);
int axc_ext_del_id(axc_context* ctx, const char* user, uint32_t devid);
int axc_ext_user_get_dev_list(axc_context* ctx, const char* user,
			      omemo_devicelist ** dl_pp);

int cachectx_get_from_map(GHashTable* map, const char* name, axc_context_dake_cache** ctx_pp);

int dakectx_handle_idakemsg(axc_context_dake* ctx, const signal_protocol_address* addr,
			    const uint8_t* msg, size_t msg_len, const signal_buffer** lastauthmsg);

//Returned list is in the same type of what omemo_devicelist_get_id_list() returns.
GList* dakectx_get_active_fauxid_list_by_jid(axc_context_dake* ctx, const char* barejid);

//Returns a GHashTable of real and faux devid pairs of "barejid's" instances with completed session.
GHashTable* dakectx_get_id_pairs_with_session(axc_context_dake* ctx, const char* barejid);

#if 0
{
#endif
#ifdef __cplusplus
}
#endif

#endif
