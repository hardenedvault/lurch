#ifndef _LURCH_CMD_DAKE_H_
#define _LURCH_CMD_DAKE_H_

#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <glib.h>
#include <purple.h>

#ifdef __cplusplus
extern "C" {
#endif
#if 0
}
#endif

PurpleCmdRet lurch_cmd_dake(PurpleConversation * conv_p,
			    const gchar * cmd,
			    gchar ** args,
			    gchar ** error,
			    void * data_p);

#define DF_dake_cmd_handler(f) PurpleCmdRet (f)(PurpleConversation* conv_p, \
						gchar** args,	\
						gchar** error, void* data_p)
typedef DF_dake_cmd_handler(dake_cmd_handler_ft);

typedef struct dake_cmd_item {
  const char* cmd;
  dake_cmd_handler_ft* handler;
} dake_cmd_item;

#if 0
{
#endif
#ifdef __cplusplus
}
#endif

#endif
