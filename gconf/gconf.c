
/* GConf
 * Copyright (C) 1999 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "gconf.h"
#include "GConf.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Error handling
 */

static gchar* last_error = NULL;

const gchar* 
g_conf_error(void)
{
  return last_error;
}

gboolean 
g_conf_error_pending  (void)
{
  return last_error != NULL;
}

void
g_conf_set_error(const gchar* str)
{
  if (last_error != NULL)
    g_free(last_error);
 
  last_error = g_strdup(str);
}

/* 
 * GConfPrivate
 */

typedef struct _GConfPrivate GConfPrivate;

struct _GConfPrivate {
  GSList* sources;
  
};

/*
 *  CORBA glue and notifier list
 */

static GConfPrivate* global_gconf = NULL;

ConfigListener listener = CORBA_OBJECT_NIL;
ConfigServer   server = CORBA_OBJECT_NIL;

static void 
notify(PortableServer_Servant servant, 
       CORBA_long cnxn,
       const CORBA_char* key, 
       const CORBA_any* value,
       CORBA_Environment *ev);

PortableServer_ServantBase__epv base_epv = {
  NULL,
  NULL,
  NULL
};

POA_ConfigListener__epv listener_epv = { NULL, notify };
POA_ConfigListener__vepv poa_listener_vepv = { &base_epv, &listener_epv };
POA_ConfigListener poa_listener_servant = { NULL, &poa_listener_vepv };

static void 
notify(PortableServer_Servant servant, 
       CORBA_long cnxn,
       const CORBA_char* key, 
       const CORBA_any* value,
       CORBA_Environment *ev)
{
  
  
}

static ConfigServer
g_conf_get_config_server(void)
{
  if (server != CORBA_OBJECT_NIL)
    return server;
  else
    {
      /* Have to obtain the server, bleh */
      /* For now, this involves reading the server info file. */
      
    }
}

/*
 *  Public Interface
 */

GConf*
g_conf_global_conf    (void)
{
  
  return (GConf*)global_gconf;
}

void         
g_conf_notify_add(GConf* conf,
                  const gchar* namespace_section, /* dir or key to listen to */
                  GConfNotifyFunc func,
                  gpointer user_data)
{
  

}

void         
g_conf_notify_remove(GConf* conf,
                     const gchar* namespace_section)
{
  

}

GConfValue*  
g_conf_lookup(GConf* conf, const gchar* key)
{

  return NULL;
}

void
g_conf_set(GConf* conf, const gchar* key, GConfValue* value)
{
  

}

