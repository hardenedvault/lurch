#include <string.h>
#include "axc_dakes_intf.h"
#include "lurch.h"
#include "idake2session.h"
#include "lurch_util.h"

#include <glib.h>

extern void lurch_util_axc_log_func(int level, const char * msg, size_t len, void * user_data);

int cachectx_init_by_name(const char* name, axc_context_dake_cache** ctx_pp)
{
  int ret_val = 0;
  gchar * err_msg_dbg = NULL;

  axc_context_dake_cache* ctx_p = NULL;
  gchar * db_fn = NULL;

  ret_val = cachectx_create(&ctx_p);
  if (ret_val) {
    err_msg_dbg = g_strdup("failed to create axc context");
    goto cleanup;
  }

  db_fn = lurch_util_uname_get_db_fn(name, LURCH_DB_NAME_AXC);
  ret_val = axc_context_set_db_fn((axc_context*)ctx_p, db_fn, strlen(db_fn));
  if (ret_val) {
    err_msg_dbg = g_strdup_printf("failed to set axc db filename to %s", db_fn);
    goto cleanup;
  }

  if (purple_prefs_get_bool(LURCH_PREF_AXC_LOGGING)) {
      axc_context_set_log_func((axc_context*)ctx_p,
			       lurch_util_axc_log_func);
      axc_context_set_log_level((axc_context*)ctx_p,
				purple_prefs_get_int(LURCH_PREF_AXC_LOGGING_LEVEL));
  }

  cachectx_bind_backend(ctx_p, &axc_session_store_tmpl);
  if (false == cachectx_has_good_backend(ctx_p)) {
    err_msg_dbg = g_strdup("backend session store is invalid");
    ret_val = AXC_ERR;
    goto cleanup;
  }

  ret_val = axc_init_with_imp((axc_context*)ctx_p, &cachectx_sess_store_tmpl,
			      &axc_pre_key_store_tmpl, &axc_signed_pre_key_store_tmpl,
			      &axc_dakes_identity_key_store_tmpl, &axc_crypto_provider_tmpl);
  if (ret_val) {
    err_msg_dbg = g_strdup("failed to init axc context");
    goto cleanup;
  }

  if (purple_prefs_get_bool(LURCH_PREF_AXC_LOGGING)) {
    signal_context_set_log_function(axc_context_get_axolotl_ctx((axc_context*)ctx_p),
				    lurch_util_axc_log_func);
  }

  purple_debug_info("lurch", "%s: preparing installation for %s...\n", __func__, name);
  ret_val = lurch_axc_prepare(name, &ctx_p->base.base);
  if (ret_val) {
    err_msg_dbg = g_strdup_printf("failed to prepare axc");
    goto cleanup;
  }
  purple_debug_info("lurch", "%s: ...done\n", __func__);

  ret_val = signal_protocol_key_helper_generate_registration_id(&ctx_p->faux_regid, 1,
								ctx_p->base.base.axolotl_global_context_p);
  if (ret_val) {
    err_msg_dbg = g_strdup("failed to generate faux registration id");
    goto cleanup;
  }

  *ctx_pp = ctx_p;

cleanup:
  if (ret_val) {
    cachectx_destroy_all(&ctx_p->base.base);
  }
  if (err_msg_dbg) {
    purple_debug_error("lurch", "%s: %s (%i)\n", __func__, err_msg_dbg, ret_val);
    g_free(err_msg_dbg);
  }

  g_free (db_fn);
  return ret_val;
}

static GHashTable* acc_axc_ctx_map = NULL;

GHashTable* get_acc_axc_ctx_map(void)
{
  if (acc_axc_ctx_map == NULL) {
    acc_axc_ctx_map = g_hash_table_new_full(g_str_hash, g_str_equal,
					    g_free, (GDestroyNotify)cachectx_destroy_all);
  }
  return acc_axc_ctx_map;
}

void init_acc_axc_ctx_map(void)
{
  get_acc_axc_ctx_map();
}

void reset_acc_axc_ctx_map(void)
{
  if (acc_axc_ctx_map) {
    g_hash_table_remove_all(acc_axc_ctx_map);
    acc_axc_ctx_map = NULL;
  }
}

axc_context_dake_cache* query_axc_ctx_by_name(GHashTable* map, const char* uname)
{
  gchar* bname = lurch_util_uname_strip(uname);
  axc_context_dake_cache* ctx = (axc_context_dake_cache*)g_hash_table_lookup(map, uname);
  g_free(bname);
  return ctx;
}

void insert_axc_ctx_by_name(GHashTable* map, const char* uname, axc_context_dake_cache* ctx)
{
  gchar* bname = lurch_util_uname_strip(uname);
  g_hash_table_insert(map, bname, ctx);
}

/** Functions playing the same role as omemo_storage*(), but implemented via an extended
 *  signal_protocol_store_context.
 */

int axc_ext_save_idkey(axc_context* ctx, const char* user, uint32_t devid,
		       ec_public_key* idkey)
{
  axc_address addr = { user, strnlen(user, JABBER_MAX_LEN_BARE), devid };
  return signal_protocol_identity_save_identity(ctx->axolotl_store_context_p,
						&addr, idkey);
}

int axc_ext_del_id(axc_context* ctx, const char* user, uint32_t devid)
{
  axc_address addr = { user, strnlen(user, JABBER_MAX_LEN_BARE), devid };
  return signal_protocol_identity_save_identity(ctx->axolotl_store_context_p,
						&addr, NULL);
}

int axc_ext_user_get_dev_list(axc_context* ctx, const char* user,
			      omemo_devicelist ** dl_pp)
{
  int ret = 0;
  signal_int_list* int_lst = NULL;
  omemo_devicelist * dl_p = NULL;
  ret = omemo_devicelist_create(user, &dl_p);
  if (ret < 0) {
    goto cleanup;
  }

  ret = axc_get_devid_list_by_name(ctx->axolotl_store_context_p, &int_lst,
				   user, strnlen(user, JABBER_MAX_LEN_BARE));
  if (ret < 0) {
    goto cleanup;
  }

  {
    size_t i = 0;
    size_t l_size = ret;
    for (; i < l_size; i++) {
      ret = omemo_devicelist_add(dl_p, signal_int_list_at(int_lst, i));
      if (ret < 0) goto cleanup;
    }
  }

  *dl_pp = dl_p;
 cleanup:
  if (ret) {
    omemo_devicelist_destroy(dl_p);
  }
  signal_int_list_free(int_lst);
  return ret;
}

int cachectx_get_from_map(GHashTable* map, const char* name, axc_context_dake_cache** ctx_pp)
{
  int ret = 0;
  axc_context_dake_cache* ctx_p = query_axc_ctx_by_name(map, name);
  if (ctx_p == NULL) {
    ret = cachectx_init_by_name(name, &ctx_p);
    if (ret < 0) goto cleanup;
    insert_axc_ctx_by_name(map, name, ctx_p);
  }

  *ctx_pp = ctx_p;
 cleanup:
  return ret;
}

int dakectx_handle_idakemsg(axc_context_dake* ctx, const signal_protocol_address* addr,
			    const uint8_t* msg, size_t msg_len, const signal_buffer** lastauthmsg)
{
  if (!msg) {
    return  axc_Idake_start_for_addr(ctx, addr, lastauthmsg);
  }

  int ret = 0;
  Signaldakez__IdakeMessage* idakemsg = signaldakez__idake_message__unpack(NULL, msg_len, msg);
  if (!idakemsg) {
    ret = SG_ERR_INVALID_MESSAGE;
    goto cleanup;
  }

  ret = axc_Idake_handle_msg(ctx, idakemsg, addr, lastauthmsg);

 cleanup:
  signaldakez__idake_message__free_unpacked(idakemsg, 0);
  return ret;
}

GList* dakectx_get_active_fauxid_list_by_jid(axc_context_dake* ctx, const char* barejid)
{
  if (!barejid)
    return NULL;
  GList* l_fauxid = NULL;
  cl_node** curp = NULL;
  CL_FOREACH(curp, &ctx->l_authinfo) {
    auth_node* node = CL_CONTAINER_OF((*curp), auth_node, cl);
    if ((0 == strncmp(barejid, auth_node_get_addr(node)->name, auth_node_get_addr(node)->name_len))
	// Ignore incomplete handshakings
	&& (node->auth->authstate == IDAKE_AUTHSTATE_NONE)) {
      uint32_t* cur_fauxid_p = (uint32_t*)malloc(sizeof(uint32_t));
      if (!cur_fauxid_p) {
	g_list_free_full(l_fauxid, free);
	return NULL;
      }
      *cur_fauxid_p = auth_node_get_addr(node)->device_id;
      l_fauxid = g_list_append(l_fauxid, cur_fauxid_p);
    }
  }
  return l_fauxid;
}

GHashTable* dakectx_get_id_pairs_with_session(axc_context_dake* ctx, const char* barejid)
{
  if (!barejid)
    return NULL;
  cl_node** curp = NULL;
  GHashTable* id_table = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);
  CL_FOREACH(curp, &ctx->l_authinfo) {
    auth_node* node = CL_CONTAINER_OF((*curp), auth_node, cl);
    if ((0 == strncmp(barejid, auth_node_get_addr(node)->name, auth_node_get_addr(node)->name_len))
	// Ignore incomplete handshakings
	&& (node->auth->authstate == IDAKE_AUTHSTATE_NONE)) {
      g_hash_table_insert(id_table,
			  GINT_TO_POINTER(auth_node_get_auth(node)->regids[1]),
			  GINT_TO_POINTER(auth_node_get_addr(node)->device_id));
    }
  }
  if (!g_hash_table_size(id_table)) {
    g_hash_table_unref(id_table);
    id_table = NULL;
  }
  return id_table;
}
