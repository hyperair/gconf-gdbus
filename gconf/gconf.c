
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

#include "GConf.h"
#include "gconf.h"
#include "gconf-internals.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <unistd.h>


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

  /* FIXME FIXME change this to be an array; we'll use client-specific 
     connection numbers to return to library users, so if gconfd dies
     we can transparently re-register all our listener functions.
  */

  GHashTable* connections;
};

static GConfPrivate* global_gconf = NULL;

typedef struct _GConfCnxn GConfCnxn;

struct _GConfCnxn {
  CORBA_long cnxn; /* id returned from server */
  GConfNotifyFunc func;
  gpointer user_data;
};

static GConfCnxn* g_conf_cnxn_new(GConfNotifyFunc func, gpointer user_data);
static void       g_conf_cnxn_destroy(GConfCnxn* cnxn);
static void       g_conf_cnxn_notify(GConfCnxn* cnxn, GConf* conf, const gchar* key, GConfValue* value);

static ConfigServer g_conf_get_config_server(void);
static ConfigListener g_conf_get_config_listener(void);
static CORBA_ORB g_conf_get_orb(void);
/*
 *  Public Interface
 */

GConf*
g_conf_global_conf    (void)
{
  if (global_gconf == NULL)
    {
      global_gconf = g_new0(GConfPrivate, 1);

      global_gconf->connections = g_hash_table_new(g_int_hash, g_int_equal);
    }
  
  return (GConf*)global_gconf;
}

guint
g_conf_notify_add(GConf* conf,
                  const gchar* namespace_section, /* dir or key to listen to */
                  GConfNotifyFunc func,
                  gpointer user_data)
{
  GConfPrivate* priv = (GConfPrivate*)conf;
  ConfigServer cs;
  ConfigListener cl;
  gulong id;
  CORBA_Environment ev;

  cs = g_conf_get_config_server();

  if (cs == CORBA_OBJECT_NIL)
    {
      g_warning("Couldn't get config server");
      return 0;
    }
  
  CORBA_exception_init(&ev);

  cl = g_conf_get_config_listener();
  
  /* Should have aborted the program in this case probably */
  g_return_val_if_fail(cl != CORBA_OBJECT_NIL, 0);

  id = ConfigServer_add_listener(cs, namespace_section, 
                                 cl, &ev);

  if (ev._major != CORBA_NO_EXCEPTION)
    {
      g_warning("Failure adding listener to the config server: %s",
                CORBA_exception_id(&ev));
      /* FIXME we could do better here... maybe respawn the server if needed... */
      CORBA_exception_free(&ev);
      return 0;
    }

  g_hash_table_insert(priv->connections, &id,
                      g_conf_cnxn_new(func, user_data));
  
  return id;
}

void         
g_conf_notify_remove(GConf* conf,
                     guint cnxn)
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

/*
 * Connection maintenance
 */

static GConfCnxn* 
g_conf_cnxn_new(GConfNotifyFunc func, gpointer user_data)
{
  GConfCnxn* cnxn;
  
  cnxn = g_new0(GConfCnxn, 1);

  cnxn->func = func;
  cnxn->user_data = user_data;

  return cnxn;
}

static void      
g_conf_cnxn_destroy(GConfCnxn* cnxn)
{
  g_free(cnxn);
}

static void       
g_conf_cnxn_notify(GConfCnxn* cnxn, GConf* conf, 
                   const gchar* key, GConfValue* value)
{
  (*cnxn->func)(conf, key, value, cnxn->user_data);
}

/*
 *  CORBA glue
 */

ConfigServer   server = CORBA_OBJECT_NIL;

static ConfigServer
try_to_contact_server(void)
{
  /* This writing-IOR-to-file crap is a temporary hack. */
  gchar* ior;

  g_return_val_if_fail(server == CORBA_OBJECT_NIL, server);
  
  ior = g_conf_read_server_ior();
  
  if (ior != NULL)
    {
      CORBA_Environment ev;

      CORBA_exception_init(&ev);

      /* May well fail, could be a stale IOR */
      server = CORBA_ORB_string_to_object(g_conf_get_orb(), ior, &ev);

      /* So try to ping server */
      if (server != CORBA_OBJECT_NIL)
        {
          ConfigServer_ping(server, &ev);

          if (ev._major != CORBA_NO_EXCEPTION)
            {
              server = CORBA_OBJECT_NIL;
              CORBA_exception_free(&ev);
            }
        }
    }

  return server;
}

static ConfigServer
g_conf_get_config_server(void)
{
  if (server != CORBA_OBJECT_NIL)
    return server;
  
  server = try_to_contact_server();

  if (server == CORBA_OBJECT_NIL)
    {
      int attempts = 0;
      printf("spawning gconfd from this client...\n");

      while (attempts < 2 && server == CORBA_OBJECT_NIL)
        {
          /* Let's try launching it, sleeping a bit, and restarting. */
          
          pid_t pid;

          pid = fork();
          
          if (pid < 0)
            {
              g_warning("gconfd fork failed: %s", strerror(errno));
              return CORBA_OBJECT_NIL;
            }
          
          if (pid == 0)
            {
              /* Child. Exec... */
              if (execlp("gconfd", NULL) < 0)
                {
                  g_warning("Failed to exec gconfd: %s", strerror(errno));
                }
              
              /* Return error to parent, but parent is currently lame
                 and doesn't check. */
              _exit(1);
            }
          
          /* Parent - waitpid(), gconfd instantly forks anyway */
          if (waitpid(pid, NULL, 0) != pid)
            {
              g_warning("waitpid() failed waiting for child in %s: %s",
                        __FUNCTION__, strerror(errno));
              return CORBA_OBJECT_NIL;
            }

          /* Sleep - OK, this is lame. Should do something more
             determininistic and less "hope it works." I'll fix it
             later. */
          {
            struct timeval tv;
            
            tv.tv_sec = 0;
            tv.tv_usec = 5000;
            
            select(0, NULL, NULL, NULL, &tv);
          }

          server = try_to_contact_server();

          ++attempts;
        }
    }

  if (server == CORBA_OBJECT_NIL)
    g_warning("Giving up on gconfd server contact");

  return server;
}


ConfigListener listener = CORBA_OBJECT_NIL;

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

/* Broken as hell, need an init function, bleh */
static ConfigListener 
g_conf_get_config_listener(void)
{
  if (listener == CORBA_OBJECT_NIL)
    {
      CORBA_Environment ev;
      PortableServer_ObjectId objid = {0, sizeof("ConfigListener"), "ConfigListener"};
      PortableServer_POA poa;

      CORBA_exception_init(&ev);
      POA_ConfigListener__init(&poa_listener_servant, &ev);
      
      poa = (PortableServer_POA)CORBA_ORB_resolve_initial_references(g_conf_get_orb(), "RootPOA", &ev);
      PortableServer_POAManager_activate(PortableServer_POA__get_the_POAManager(poa, &ev), &ev);
      PortableServer_POA_activate_object_with_id(poa,
                                                 &objid, &poa_listener_servant, &ev);
      
      listener = PortableServer_POA_servant_to_reference(poa,
                                                         &poa_listener_servant,
                                                         &ev);
      if (listener == CORBA_OBJECT_NIL) 
        {
          g_warning("Didn't get listener object ref");
          return CORBA_OBJECT_NIL;
        }
    }

  return listener;
}

/* Broken, we should have an init function, and let the user pass this
   in anyway because they might already have the ORB from Gnome, plus
   this function will retry forever, etc., anyway, it's hosed.  */
static CORBA_ORB orb = CORBA_OBJECT_NIL;

static CORBA_ORB 
g_conf_get_orb(void)
{
  if (orb == CORBA_OBJECT_NIL)
    {
      CORBA_Environment ev;
      gchar* fake_argv[] = { "gconf", NULL };
      int fake_argc = 1;

      CORBA_exception_init(&ev);
      
      orb = CORBA_ORB_init(&fake_argc, fake_argv, "orbit-local-orb", &ev);

      if (ev._major != CORBA_NO_EXCEPTION)
        {
          g_warning("Failed to init orb: %s",
                    CORBA_exception_id(&ev));
        }

      if (orb == CORBA_OBJECT_NIL)
        g_warning("Failed to get orb");

      CORBA_exception_free(&ev);
    }

  return orb;
}
