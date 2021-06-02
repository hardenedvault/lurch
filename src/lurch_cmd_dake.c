#include "lurch_cmd_dake.h"
#include "axc_dakes_intf.h"
#include "omemo_helper.h"
#include "libomemo_storage.h"
#include "lurch_api.h"
#include "lurch_util.h"
#include "lurch.h"

static const dake_cmd_item dake_cmd_list[];

void dake_cmd_print(PurpleConversation * conv_p, const char * msg, int iserror)
{
  purple_conversation_write(conv_p,
			    "lurch-dake",
			    msg,
			    PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_NO_LOG |
			    (iserror?PURPLE_MESSAGE_ERROR:0),
			    time((void *) 0));
}

PurpleCmdRet lurch_cmd_dake(PurpleConversation * conv_p,
			    const gchar * cmd,
			    gchar ** args,
			    gchar ** error,
			    void * data_p)
{
  const char* subcmd = args[0];
  {
    size_t i = 0;
    for (; dake_cmd_list[i].handler; i++) {
      if (0 == g_strcmp0(dake_cmd_list[i].cmd, subcmd)) {
	return dake_cmd_list[i].handler(conv_p, &args[1], error, data_p);
      }
    }
  }
  *error = g_strdup("Invalid sub-command!");
  return PURPLE_CMD_RET_FAILED;
}

static DF_dake_cmd_handler(help)
{
  static const char* const usage
    = "The following commands exist to interact with the dake interface:\n\n"
    " - '/dake idake <jid>': Initiate interactive dake with <jid>, or the peer\n"
    "   in the current conversation if omitted.\n"
    " - '/dake odake <jid>': Initiate offline dake with <jid>, or the peer in the\n"
    "   current conversation.\n"
    "\n"
    " - '/dake publish': (In arbitrary conversation of an account)\n"
    "   Publish bundle of the current instance of this account, allowing others\n"
    "   to leave offline message to you with odake.\n"
    " - '/dake delete-used': (In arbitrary conversation of an account)\n"
    "   Delete remain bundle once published by the current instance of this account.\n"
    " - '/dake purge': (In arbitrary conversation of an account)\n"
    "   Delete all bundle and device list of (all instances of) this account from server.\n"
    "\n"
    " - '/dake list <jid>': Show 'faux_devid (real_devid)' pairs for all instances\n"
    "   of <jid> (omitted for the peer in the current conversation) that have dake session with\n"
    "   own instance.\n"
    " - '/dake term <jid> <faux_devid>': Terminate dake session between own instance and\n"
    "   <jid>:<faux_devid>, <jid> could be '.' for for the peer in the current conversation.\n"
    "\n"
    " - '/dake help': Displays this message.";
  dake_cmd_print(conv_p, usage, FALSE);
  return PURPLE_CMD_RET_OK;
}

static DF_dake_cmd_handler(start_idake)
{
  int ret = 0;
  PurpleAccount* account = purple_conversation_get_account(conv_p);
  if (!purple_account_is_connected(account)) {
    *error = g_strdup_printf("account %s has not been connected!",
			     purple_account_get_name_for_display(account));
    return PURPLE_CMD_RET_FAILED;
  }
  PurpleConnection* gc = purple_conversation_get_gc(conv_p);
  if (0 != strncmp(purple_account_get_protocol_id(account), JABBER_PROTOCOL_ID, strlen(JABBER_PROTOCOL_ID))) {
    *error = g_strdup("incompatible protocol");
    return PURPLE_CMD_RET_FAILED;
  }
  JabberStream* js = (JabberStream*)purple_connection_get_protocol_data(gc);
  const char* to = args[0];
  if (!to) {
    if (PURPLE_CONV_TYPE_IM == purple_conversation_get_type(conv_p)) {
      to = purple_conversation_get_name(conv_p);
    } else {
      *error = g_strdup("no recipient specified!");
      return PURPLE_CMD_RET_FAILED;
    }
  }
  gchar* uname = lurch_util_uname_strip(purple_account_get_username(account));
  axc_context_dake_cache* cachectx = NULL;
  xmlnode* idake_node = NULL;
  const char* type = NULL;
  switch (purple_conversation_get_type(conv_p)) {
  case PURPLE_CONV_TYPE_IM:
    type = "chat";
    break;
  case PURPLE_CONV_TYPE_CHAT:
    type = "groupchat";
    break;
  default:
    type = "normal";
    break;
  }
  ret = cachectx_get_from_map(get_acc_axc_ctx_map(), uname, &cachectx);
  if (ret) {
    *error = g_strdup_printf("failed to get axc ctx for %s", uname);
    goto cleanup;
  }
  ret = lurch_dake_create_idake_msg(&idake_node, error, js, type, to, 0,
				    cachectx_get_faux_regid(cachectx),
				    (const uint8_t*)IDAKE_HINT, strlen(IDAKE_HINT));
  if (ret < 0) {
    goto cleanup;
  }

  purple_signal_emit(purple_plugins_find_with_id("prpl-jabber"),
		     "jabber-sending-xmlnode", gc, &idake_node);

 cleanup:
  xmlnode_free(idake_node);
  g_free(uname);
  if (ret < 0)
    return PURPLE_CMD_RET_FAILED;
  else
    return PURPLE_CMD_RET_OK;
}

static void startodake_bundle_cb(JabberStream * js_p, const char * from,
				 JabberIqType type, const char * id,
				 xmlnode * packet_p, gpointer data_p)
{
  int ret_val = 0;
  gchar * err_msg_conv = NULL;
  const char * err_msg_dbg = NULL;

  gchar * uname = NULL;
  gchar ** split = NULL;
  const char * device_id_str = NULL;
  axc_address addr = {0};
  axc_context_dake_cache * cachectx_p = NULL;
  xmlnode * pubsub_node_p = NULL;
  xmlnode * items_node_p = NULL;

  uname = lurch_util_uname_strip(purple_account_get_username(purple_connection_get_account(js_p->gc)));
  if (!from) {
    // own user
    from = uname;
  }

  PurpleConversation* conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_ANY, from,
								   purple_connection_get_account(js_p->gc));

  split = g_strsplit(id, "#", 3);
  device_id_str = split[1];

  purple_debug_info("lurch", "%s: %s received bundle update from %s:%s\n", __func__, uname, from, device_id_str);
  addr.name = from;
  addr.name_len = strnlen(from, JABBER_MAX_LEN_BARE);
  addr.device_id = strtol(device_id_str, (void *) 0, 10);

  ret_val = cachectx_get_from_map(get_acc_axc_ctx_map(), uname, &cachectx_p);
  if (ret_val) {
    err_msg_dbg = "failed to get axc ctx";
    goto cleanup;
  }

  if (type == JABBER_IQ_ERROR) {
    err_msg_conv = g_strdup_printf("The device %s owned by %s does not have a bundle and will be skipped. ",
                                   device_id_str, from);
  } else {
    pubsub_node_p = xmlnode_get_child(packet_p, "pubsub");
    if (!pubsub_node_p) {
      ret_val = LURCH_ERR;
      err_msg_dbg = "no <pubsub> node in response";
      goto cleanup;
    }

    items_node_p = xmlnode_get_child(pubsub_node_p, "items");
    if (!items_node_p) {
      ret_val = LURCH_ERR;
      err_msg_dbg = "no <items> node in response";
      goto cleanup;
    }

    ret_val = axc_dake_session_exists_initiated(&addr, &cachectx_p->base);
    if ((ret_val == SG_ERR_NO_SESSION) || !ret_val) {
      ret_val = lurch_dake_bundle_create_session(uname, from, items_node_p, &cachectx_p->base.base);
      if (ret_val) {
        err_msg_dbg = "failed to create a session";
        goto cleanup;
      }

      if (conv) {
	gchar* info = g_strdup_printf("odake session to %s:%i initiated", addr.name, addr.device_id);
	purple_conversation_write(conv, uname, info, PURPLE_MESSAGE_SYSTEM, time(NULL));
	g_free(info);
      }
    } else if (ret_val < 0) {
      err_msg_dbg = "failed to check if session exists";
      goto cleanup;
    }
  }

 cleanup:
  if (err_msg_conv) {
    purple_conv_present_error(from, purple_connection_get_account(js_p->gc), err_msg_conv);
    g_free(err_msg_conv);
  }
  if (err_msg_dbg) {
    purple_conv_present_error(from, purple_connection_get_account(js_p->gc), LURCH_ERR_STRING_ENCRYPT);
    purple_debug_error("lurch", "%s: %s (%i)\n", __func__, err_msg_dbg, ret_val);
  }

  g_free(uname);
  g_strfreev(split);
}

static void startodake_devlst_cb(JabberStream * js_p, const char * from, xmlnode * items_p)
{
  int ret_val = 0;
  int len = 0;
  gchar * err_msg_dbg = NULL;
  gchar * tempxml = NULL;
  gchar * uname = lurch_util_uname_strip(purple_account_get_username(purple_connection_get_account(js_p->gc)));
  omemo_devicelist * dl_in_p = NULL;
  GList* head = NULL;
  purple_debug_info("lurch", "%s: %s requesting device list update from %s\n", __func__, uname, from);

  if (!items_p) {
    gchar* info = g_strdup_printf("%s has not publish any device list yet. no odake session can be initiated\n", from);
    purple_conv_present_error(from, purple_connection_get_account(js_p->gc), info);
    g_free(info);
    goto cleanup;
  }

  tempxml = xmlnode_to_str(items_p, &len);
  ret_val = omemo_devicelist_import(tempxml, from, &dl_in_p);
  if (ret_val) {
    err_msg_dbg = g_strdup_printf("failed to import devicelist");
    goto cleanup;
  }
  head = omemo_devicelist_get_id_list(dl_in_p);
  {
    GList* cur = head;
    for (;cur; cur = cur->next) {
      ret_val = lurch_bundle_request_do(js_p, from, omemo_devicelist_list_data(cur),
					startodake_bundle_cb, NULL);
      if (ret_val) {
	err_msg_dbg = g_strdup_printf("failed to request bundle for %s:%u",
				      from, omemo_devicelist_list_data(cur));
      }
    }
  }
  cleanup:
  if (err_msg_dbg) {
    purple_debug_error("lurch", "%s: %s (%i)\n", __func__, err_msg_dbg, ret_val);
    g_free(err_msg_dbg);
  }
  g_free(tempxml);
  g_free(uname);
  omemo_devicelist_destroy(dl_in_p);
  g_list_free_full(head, free);
}

static DF_dake_cmd_handler(start_odake)
{
  int ret = 0;
  PurpleAccount* account = purple_conversation_get_account(conv_p);
  if (!purple_account_is_connected(account)) {
    *error = g_strdup_printf("account %s has not been connected!",
			     purple_account_get_name_for_display(account));
    return PURPLE_CMD_RET_FAILED;
  }
  PurpleConnection* gc = purple_conversation_get_gc(conv_p);
  if (0 != strncmp(purple_account_get_protocol_id(account), JABBER_PROTOCOL_ID, strlen(JABBER_PROTOCOL_ID))) {
    *error = g_strdup("incompatible protocol");
    return PURPLE_CMD_RET_FAILED;
  }
  JabberStream* js = (JabberStream*)purple_connection_get_protocol_data(gc);
  const char* to = args[0];
  if (!to) {
    if (PURPLE_CONV_TYPE_IM == purple_conversation_get_type(conv_p)) {
      to = purple_conversation_get_name(conv_p);
    } else {
      *error = g_strdup("no recipient specified!");
      return PURPLE_CMD_RET_FAILED;
    }
  }
  char* dl_ns = NULL;
  gchar* bare_to = lurch_util_uname_strip(to);
  ret = omemo_devicelist_get_pep_node_name(&dl_ns);
  if (ret) {
    purple_debug_error("lurch", "%s: failed to get devicelist pep node name (%i)\n", __func__, ret);
    goto cleanup;
  }

  jabber_pep_request_item(js, bare_to, dl_ns, NULL, startodake_devlst_cb);

 cleanup:
  free(dl_ns);
  g_free(bare_to);

  if (ret < 0)
    return PURPLE_CMD_RET_FAILED;
  else
    return PURPLE_CMD_RET_OK;
}

static DF_dake_cmd_handler(pub_bundle)
{
  int ret = 0;
  PurpleAccount* account = purple_conversation_get_account(conv_p);
  if (!purple_account_is_connected(account)) {
    *error = g_strdup_printf("account %s has not been connected!",
			     purple_account_get_name_for_display(account));
    return PURPLE_CMD_RET_FAILED;
  }
  PurpleConnection* gc = purple_conversation_get_gc(conv_p);
  if (0 != strncmp(purple_account_get_protocol_id(account), JABBER_PROTOCOL_ID, strlen(JABBER_PROTOCOL_ID))) {
    *error = g_strdup("incompatible protocol");
    return PURPLE_CMD_RET_FAILED;
  }
  JabberStream* js = (JabberStream*)purple_connection_get_protocol_data(gc);
  gchar* uname = lurch_util_uname_strip(purple_account_get_username(account));
  char* dl_ns = NULL;
  ret = omemo_devicelist_get_pep_node_name(&dl_ns);
  if (ret) {
    purple_debug_error("lurch", "%s: failed to get devicelist pep node name (%i)\n", __func__, ret);
    goto cleanup;
  }

  jabber_pep_request_item(js, uname, dl_ns, NULL, lurch_pep_own_devicelist_request_handler);

 cleanup:
  g_free(uname);
  free(dl_ns);

  if (ret)
    return PURPLE_CMD_RET_FAILED;
  else
    return PURPLE_CMD_RET_OK;
}

static DF_dake_cmd_handler(delete_used_bundle)
{
  int ret = 0;
  PurpleAccount* account = purple_conversation_get_account(conv_p);
  if (!purple_account_is_connected(account)) {
    *error = g_strdup_printf("account %s has not been connected!",
			     purple_account_get_name_for_display(account));
    return PURPLE_CMD_RET_FAILED;
  }
  PurpleConnection* gc = purple_conversation_get_gc(conv_p);
  if (0 != strncmp(purple_account_get_protocol_id(account), JABBER_PROTOCOL_ID, strlen(JABBER_PROTOCOL_ID))) {
    *error = g_strdup("incompatible protocol");
    return PURPLE_CMD_RET_FAILED;
  }
  JabberStream* js = (JabberStream*)purple_connection_get_protocol_data(gc);
  gchar* uname = lurch_util_uname_strip(purple_account_get_username(purple_connection_get_account(gc)));
  gchar* db_fn_omemo = lurch_util_uname_get_db_fn(uname, LURCH_DB_NAME_OMEMO);
  omemo_devicelist* odl = NULL;
  ret = omemo_storage_user_devicelist_retrieve(uname, db_fn_omemo, &odl);
  if (ret) {
    *error = g_strdup("failed to get own device id list");
    goto cleanup;
  }
  GList* used_faux_dl = omemo_devicelist_get_id_list(odl);
  omemo_devicelist_destroy(odl);
  lurch_delete_used_bundle(js, used_faux_dl);
  lurch_delete_faux_ids(uname, used_faux_dl);
  g_list_free_full(used_faux_dl, free);

 cleanup:
  g_free(uname);
  g_free(db_fn_omemo);

  if (ret)
    return PURPLE_CMD_RET_FAILED;
  else
    return PURPLE_CMD_RET_OK;
}

static DF_dake_cmd_handler(purge_all_bundle)
{
  int ret = 0;
  PurpleAccount* account = purple_conversation_get_account(conv_p);
  if (!purple_account_is_connected(account)) {
    *error = g_strdup_printf("account %s has not been connected!",
			     purple_account_get_name_for_display(account));
    return PURPLE_CMD_RET_FAILED;
  }
  PurpleConnection* gc = purple_conversation_get_gc(conv_p);
  if (0 != strncmp(purple_account_get_protocol_id(account), JABBER_PROTOCOL_ID, strlen(JABBER_PROTOCOL_ID))) {
    *error = g_strdup("incompatible protocol");
    return PURPLE_CMD_RET_FAILED;
  }
  JabberStream* js = (JabberStream*)purple_connection_get_protocol_data(gc);
  gchar* uname
    = lurch_util_uname_strip(purple_account_get_username(account));
  gchar* db_fn_omemo = lurch_util_uname_get_db_fn(uname, LURCH_DB_NAME_OMEMO);
  purple_debug_info("lurch", "%s: purging all bundle published by account %s\n", __func__, uname);
  jabber_pep_request_item(js, uname, OMEMO_DEVICELIST_PEP_NODE, NULL, lurch_pep_own_devicelist_purge);
  purple_debug_info("lurch", "%s: deleting device list published by account %s\n", __func__, uname);
  jabber_pep_delete_node(js, OMEMO_NS OMEMO_NS_SEPARATOR BUNDLE_PEP_NAME);
  jabber_pep_delete_node(js, OMEMO_DEVICELIST_PEP_NODE);
  {
    omemo_devicelist* faux_dl_p = NULL;
    ret = omemo_storage_user_devicelist_retrieve(uname, db_fn_omemo, &faux_dl_p);
    if (ret) {
      *error = g_strdup("failed to get own device id list");
      goto cleanup;
    }
    GList* l_faux_id = omemo_devicelist_get_id_list(faux_dl_p);
    omemo_devicelist_destroy(faux_dl_p);
    lurch_delete_faux_ids(uname, l_faux_id);
    g_list_free_full(l_faux_id, free);
  }

 cleanup:
  g_free(uname);
  g_free(db_fn_omemo);

  if (ret)
    return PURPLE_CMD_RET_FAILED;
  else
    return PURPLE_CMD_RET_OK;
}

static void fill_id_pair (gpointer key, gpointer value, gpointer user_data)
{
  int ik = GPOINTER_TO_INT(key);
  int iv = GPOINTER_TO_INT(value);
  GString* buf = (GString*)user_data;
  g_string_append_printf(buf, "%d (%d); ", iv, ik);
}

static DF_dake_cmd_handler(list_session)
{
  int ret = 0;
  PurpleAccount* account = purple_conversation_get_account(conv_p);
  if (0 != strncmp(purple_account_get_protocol_id(account), JABBER_PROTOCOL_ID, strlen(JABBER_PROTOCOL_ID))) {
    *error = g_strdup("incompatible protocol");
    return PURPLE_CMD_RET_FAILED;
  }
  const char* to = args[0];
  if (!to) {
    if (PURPLE_CONV_TYPE_IM == purple_conversation_get_type(conv_p)) {
      to = purple_conversation_get_name(conv_p);
    } else {
      *error = g_strdup("no recipient specified!");
      return PURPLE_CMD_RET_FAILED;
    }
  }
  gchar* uname = lurch_util_uname_strip(purple_account_get_username(account));
  axc_context_dake_cache* cachectx = NULL;
  ret = cachectx_get_from_map(get_acc_axc_ctx_map(), uname, &cachectx);
  if (ret) {
    *error = g_strdup_printf("failed to get axc ctx for %s", uname);
    goto cleanup;
  }

  gchar* bare_peername = jabber_get_bare_jid(to);
  GHashTable* id_table_with_session = dakectx_get_id_pairs_with_session(&cachectx->base, bare_peername);
  g_free(bare_peername);
  if (id_table_with_session) {
    GString* buf = g_string_new(NULL);
    g_hash_table_foreach(id_table_with_session, fill_id_pair, (gpointer)buf);
    gchar* str_id_list = g_string_free(buf, FALSE);

    dake_cmd_print(conv_p, str_id_list, FALSE);
    g_free(str_id_list);
  }
  g_hash_table_unref(id_table_with_session);

 cleanup:
  g_free(uname);
  if (ret < 0)
    return PURPLE_CMD_RET_FAILED;
  else
    return PURPLE_CMD_RET_OK;
}

static DF_dake_cmd_handler(term_session)
{
  int ret = 0;
  PurpleAccount* account = purple_conversation_get_account(conv_p);
  if (0 != strncmp(purple_account_get_protocol_id(account), JABBER_PROTOCOL_ID, strlen(JABBER_PROTOCOL_ID))) {
    *error = g_strdup("incompatible protocol");
    return PURPLE_CMD_RET_FAILED;
  }
  const char* to = args[0];
  if (!to) {
    *error = g_strdup("no recipient specified!");
    return PURPLE_CMD_RET_FAILED;
  }

  if ((0 == g_strcmp0(to, "."))
      && (PURPLE_CONV_TYPE_IM == purple_conversation_get_type(conv_p))) {
      to = purple_conversation_get_name(conv_p);
  }

  if (!args[1]) {
    *error = g_strdup("no device id specified!");
    return PURPLE_CMD_RET_FAILED;
  }
  gchar* bare_to = jabber_get_bare_jid(to);
  unsigned long devid = strtoul(args[1], NULL, 0);
  if ((devid == 0) || (devid > UINT32_MAX)) {
    *error = g_strdup("invalid device id!");
    return PURPLE_CMD_RET_FAILED;
  }

  gchar* uname = lurch_util_uname_strip(purple_account_get_username(account));
  axc_context_dake_cache* cachectx = NULL;
  ret = cachectx_get_from_map(get_acc_axc_ctx_map(), uname, &cachectx);
  if (ret) {
    *error = g_strdup_printf("failed to get axc ctx for %s", uname);
    goto cleanup;
  }

  signal_protocol_address addr = { bare_to, strlen(bare_to), (uint32_t)devid };
  uint32_t real_devid = 0;
  ret = axc_context_dake_get_real_regid(&cachectx->base, &addr, &real_devid);
  if (ret < 0) {
    *error = g_strdup_printf("unable to find real device id of %s:%i", addr.name, addr.device_id);
    goto cleanup;
  }

  if (purple_account_is_connected(account)) {
    purple_debug_info("dake", "inform  %s:%i (%i) that the session is going to be terminate",
		      addr.name, addr.device_id, real_devid);
    PurpleConnection* gc = purple_conversation_get_gc(conv_p);
    JabberStream* js = (JabberStream*)purple_connection_get_protocol_data(gc);
    const char* type = NULL;
    switch (purple_conversation_get_type(conv_p)) {
    case PURPLE_CONV_TYPE_IM:
      type = "chat";
      break;
    case PURPLE_CONV_TYPE_CHAT:
      type = "groupchat";
      break;
    default:
      type = "normal";
      break;
    }
    xmlnode* msgnode = jabber_create_message_on_stream(js, type, to);
    omemo_message* omsg = NULL;
    do {
      if (!msgnode) {
	*error = g_strdup_printf("failed to create message for %s", to);
	ret = SG_ERR_INVAL;
	break;
      }
      xmlnode* body = xmlnode_new_child(msgnode, "body");
      xmlnode_insert_data(body, TERM_HINT, -1);
      int len = 0;
      gchar* tempxml = xmlnode_to_str(msgnode, &len);
      ret = omemo_message_prepare_encryption(tempxml, cachectx_get_faux_regid(cachectx),
					     &crypto, OMEMO_STRIP_ALL, &omsg);
      g_free(tempxml);
      if (ret) {
	*error = g_strdup_printf("failed to construct omemo message");
	break;
      }

      lurch_addr* a = malloc(sizeof(lurch_addr));
      if (!a) {
	ret = LURCH_ERR_NOMEM;
	*error = g_strdup_printf("failed make up an address list");
	break;
      }
      a->jid = g_strdup(addr.name);
      a->device_id = addr.device_id;
      GList* addr_l = g_list_prepend(NULL, a);
      ret = lurch_msg_finalize_encryption(js, &cachectx->base.base, omsg, addr_l, &msgnode);
      omsg = NULL;
      if (ret) {
	*error = g_strdup_printf("failed to finalize encryption");
	break;
      }
      purple_signal_emit(purple_plugins_find_with_id("prpl-jabber"),
		       "jabber-sending-xmlnode", gc, &msgnode);
    } while (0);
    xmlnode_free(msgnode);
    omemo_message_destroy(omsg);
  }

  ret = axc_dake_terminate_session(&addr, &cachectx->base);
  if (ret < 0) {
    *error = g_strdup_printf("failed to terminate session for %s:%i (%i)",
			     addr.name, addr.device_id, real_devid);
  } else {
    gchar* info = g_strdup_printf("dake session to %s:%i (%i) terminated",
				  addr.name, addr.device_id, real_devid);
    dake_cmd_print(conv_p, info, FALSE);
    g_free(info);
  }

 cleanup:
  g_free(bare_to);
  g_free(uname);
  if (ret < 0)
    return PURPLE_CMD_RET_FAILED;
  else
    return PURPLE_CMD_RET_OK;
}

static const dake_cmd_item dake_cmd_list[] = {
  { NULL, help },
  { "help", help },
  { "idake", start_idake },
  { "odake", start_odake },
  { "publish", pub_bundle },
  { "delete-used", delete_used_bundle },
  { "purge", purge_all_bundle },
  { "list", list_session },
  { "term", term_session },
  { NULL, NULL },
};
