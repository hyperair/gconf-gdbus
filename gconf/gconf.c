
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
#include "gconf-orbit.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <unistd.h>


/* Quick hack so I can mark strings */

#ifdef _ 
#warning "_ already defined"
#else
#define _(x) x
#endif

#ifdef N_ 
#warning "N_ already defined"
#else
#define N_(x) x
#endif

/* Returns TRUE if there was an error */
static gboolean gconf_handle_corba_exception(CORBA_Environment* ev, GConfError** err);

gboolean
gconf_key_check(const gchar* key, GConfError** err)
{
  gchar* why = NULL;

  if (!gconf_valid_key(key, &why))
    {
      if (err)
        *err = gconf_error_new(G_CONF_BAD_KEY, _("`%s': %s"),
                                key, why);
      g_free(why);
      return FALSE;
    }
  return TRUE;
}

/* 
 * GConfPrivate
 */

typedef struct _GConfEnginePrivate GConfEnginePrivate;

struct _GConfEnginePrivate {
  ConfigServer_Context context;
  guint refcount;
};

typedef struct _GConfCnxn GConfCnxn;

struct _GConfCnxn {
  guint client_id;
  CORBA_unsigned_long server_id; /* id returned from server */
  GConfEngine* conf;     /* conf we're associated with */
  GConfNotifyFunc func;
  gpointer user_data;
};

static GConfCnxn* gconf_cnxn_new(GConfEngine* conf, CORBA_unsigned_long server_id, GConfNotifyFunc func, gpointer user_data);
static void       gconf_cnxn_destroy(GConfCnxn* cnxn);
static void       gconf_cnxn_notify(GConfCnxn* cnxn, const gchar* key, GConfValue* value);

static ConfigServer gconf_get_config_server(gboolean start_if_not_found, GConfError** err);
static ConfigListener gconf_get_config_listener(void);

/* We'll use client-specific connection numbers to return to library
   users, so if gconfd dies we can transparently re-register all our
   listener functions.  */

typedef struct _CnxnTable CnxnTable;

struct _CnxnTable {
  /* Hash from server-returned connection ID to GConfCnxn */
  GHashTable* server_ids;
  /* Hash from our connection ID to GConfCnxn */
  GHashTable* client_ids;
};

static CnxnTable* ctable = NULL;

static CnxnTable* ctable_new(void);
static void       ctable_insert(CnxnTable* ct, GConfCnxn* cnxn);
static void       ctable_remove(CnxnTable* ct, GConfCnxn* cnxn);
static void       ctable_remove_by_client_id(CnxnTable* ct, guint client_id);
static GSList*    ctable_remove_by_conf(CnxnTable* ct, GConfEngine* conf);
static GConfCnxn* ctable_lookup_by_client_id(CnxnTable* ct, guint client_id);
static GConfCnxn* ctable_lookup_by_server_id(CnxnTable* ct, CORBA_unsigned_long server_id);


/*
 *  Public Interface
 */

GConfEngine*
gconf_engine_new            (void)
{
  GConfEnginePrivate* priv;

  priv = g_new0(GConfEnginePrivate, 1);

  priv->context = ConfigServer_default_context;
  priv->refcount = 1;
  
  return (GConfEngine*) priv;
}

GConfEngine*
gconf_engine_new_from_address(const gchar* address, GConfError** err)
{
  GConfEngine* gconf;
  GConfEnginePrivate* priv;
  CORBA_Environment ev;
  ConfigServer cs;
  ConfigServer_Context ctx;
  
  cs = gconf_get_config_server(TRUE, err);

  if (cs == CORBA_OBJECT_NIL)
    return NULL; /* Error should already be set */

  CORBA_exception_init(&ev);
  
  ctx = ConfigServer_get_context(cs, (gchar*)address, &ev);

  if (gconf_handle_corba_exception(&ev, err))
    {
      /* FIXME we could do better here... maybe respawn the server if needed... */
      return NULL;
    }
  
  if (ctx == ConfigServer_invalid_context)
    {
      if (err)
        *err = gconf_error_new(G_CONF_BAD_ADDRESS,
                                _("Server couldn't resolve the address `%s'"),
                                address);

      return NULL;
    }
  
  gconf = gconf_engine_new();
  
  priv = (GConfEnginePrivate*)gconf;

  priv->context = ctx;
  priv->refcount = 1;
  
  return gconf;
}

void
gconf_engine_ref             (GConfEngine* conf)
{
  GConfEnginePrivate* priv = (GConfEnginePrivate*)conf;

  g_return_if_fail(priv != NULL);
  g_return_if_fail(priv->refcount > 0);

  priv->refcount += 1;
}

void         
gconf_engine_unref        (GConfEngine* conf)
{
  GConfEnginePrivate* priv = (GConfEnginePrivate*)conf;
  
  g_return_if_fail(priv != NULL);
  g_return_if_fail(priv->refcount > 0);

  priv->refcount -= 1;
  
  if (priv->refcount == 0)
    {
      /* Remove all connections associated with this GConf */
      GSList* removed;
      GSList* tmp;
      CORBA_Environment ev;
      ConfigServer cs;


      cs = gconf_get_config_server(FALSE, NULL); /* don't restart it
                                                     if down, since
                                                     the new one won't
                                                     have the
                                                     connections to
                                                     remove */

      if (cs == CORBA_OBJECT_NIL)
        g_warning("Config server is down while destroying GConf %p", conf);

      CORBA_exception_init(&ev);

      removed = ctable_remove_by_conf(ctable, conf);
  
      tmp = removed;
      while (tmp != NULL)
        {
          GConfCnxn* gcnxn = tmp->data;

          if (cs != CORBA_OBJECT_NIL)
            {
              GConfError* err = NULL;
              
              ConfigServer_remove_listener(cs,
                                           priv->context,
                                           gcnxn->server_id,
                                           &ev);

              if (gconf_handle_corba_exception(&ev, &err))
                {
                  /* Don't set error because realistically this doesn't matter to 
                     clients */
                  g_warning("Failure removing listener %u from the config server: %s",
                            (guint)gcnxn->server_id,
                            err->str);
                }
            }

          gconf_cnxn_destroy(gcnxn);

          tmp = g_slist_next(tmp);
        }

      g_slist_free(removed);
    }
}

guint
gconf_notify_add(GConfEngine* conf,
                  const gchar* namespace_section, /* dir or key to listen to */
                  GConfNotifyFunc func,
                  gpointer user_data,
                  GConfError** err)
{
  GConfEnginePrivate* priv = (GConfEnginePrivate*)conf;
  ConfigServer cs;
  ConfigListener cl;
  gulong id;
  CORBA_Environment ev;
  GConfCnxn* cnxn;

  cs = gconf_get_config_server(TRUE, err);

  if (cs == CORBA_OBJECT_NIL)
    return 0;
  
  CORBA_exception_init(&ev);

  cl = gconf_get_config_listener();
  
  /* Should have aborted the program in this case probably */
  g_return_val_if_fail(cl != CORBA_OBJECT_NIL, 0);

  id = ConfigServer_add_listener(cs, priv->context,
                                 (gchar*)namespace_section, 
                                 cl, &ev);

  if (gconf_handle_corba_exception(&ev, err))
    {
      /* FIXME we could do better here... maybe respawn the server if needed... */
      return 0;
    }

  cnxn = gconf_cnxn_new(conf, id, func, user_data);

  ctable_insert(ctable, cnxn);

  printf("Received ID %u from server, and mapped to client ID %u\n",
         (guint)id, cnxn->client_id);
  
  return cnxn->client_id;
}

void         
gconf_notify_remove(GConfEngine* conf,
                     guint client_id)
{
  GConfEnginePrivate* priv = (GConfEnginePrivate*)conf;
  GConfCnxn* gcnxn;
  CORBA_Environment ev;
  ConfigServer cs;

  cs = gconf_get_config_server(TRUE, NULL);

  if (cs == CORBA_OBJECT_NIL)
    return;

  CORBA_exception_init(&ev);

  gcnxn = ctable_lookup_by_client_id(ctable, client_id);

  g_return_if_fail(gcnxn != NULL);

  ConfigServer_remove_listener(cs, priv->context,
                               gcnxn->server_id,
                               &ev);

  if (gconf_handle_corba_exception(&ev, NULL))
    {
      /* FIXME we could do better here... maybe respawn the server if needed... */
    }
  

  /* We want to do this even if the CORBA fails, so if we restart gconfd and 
     reinstall listeners we don't reinstall this one. */
  ctable_remove(ctable, gcnxn);

  gconf_cnxn_destroy(gcnxn);
}

GConfValue*  
gconf_get(GConfEngine* conf, const gchar* key, GConfError** err)
{
  GConfEnginePrivate* priv = (GConfEnginePrivate*)conf;
  GConfValue* val;
  ConfigValue* cv;
  CORBA_Environment ev;
  ConfigServer cs;

  if (!gconf_key_check(key, err))
    return NULL;

  cs = gconf_get_config_server(TRUE, err);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_return_val_if_fail(((err == NULL) || ((*err)->num == G_CONF_NO_SERVER)), NULL);
      return NULL;
    }

  CORBA_exception_init(&ev);
  
  cv = ConfigServer_lookup(cs, priv->context, (gchar*)key, &ev);

  if (gconf_handle_corba_exception(&ev, err))
    {
      /* FIXME we could do better here... maybe respawn the server if needed... */

      /* NOTE: don't free cvs since we got an exception! */
      return NULL;
    }
  else
    {
      val = gconf_value_from_corba_value(cv);
      CORBA_free(cv);

      return val;
    }
}

gboolean
gconf_set(GConfEngine* conf, const gchar* key, GConfValue* value, GConfError** err)
{
  GConfEnginePrivate* priv = (GConfEnginePrivate*)conf;
  ConfigValue* cv;
  CORBA_Environment ev;
  ConfigServer cs;

  g_return_val_if_fail(value->type != G_CONF_VALUE_INVALID, FALSE);

  if (!gconf_key_check(key, err))
    return FALSE;

  cs = gconf_get_config_server(TRUE, err);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_return_val_if_fail(((err == NULL) || ((*err)->num == G_CONF_NO_SERVER)), FALSE);
      return FALSE;
    }

  cv = corba_value_from_gconf_value(value);

  CORBA_exception_init(&ev);

  ConfigServer_set(cs, priv->context,
                   (gchar*)key, cv,
                   &ev);

  CORBA_free(cv);

  if (gconf_handle_corba_exception(&ev, err))
    {
      /* FIXME we could do better here... maybe respawn the server if needed... */
      return FALSE;
    }

  g_return_val_if_fail(*err == NULL, FALSE);
  
  return TRUE;
}

gboolean
gconf_unset(GConfEngine* conf, const gchar* key, GConfError** err)
{
  GConfEnginePrivate* priv = (GConfEnginePrivate*)conf;
  CORBA_Environment ev;
  ConfigServer cs;

  if (!gconf_key_check(key, err))
    return FALSE;

  cs = gconf_get_config_server(TRUE, err);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_return_val_if_fail(((err == NULL) || ((*err)->num == G_CONF_NO_SERVER)), FALSE);
      return FALSE;
    }

  CORBA_exception_init(&ev);

  ConfigServer_unset(cs, priv->context,
                     (gchar*)key,
                     &ev);

  if (gconf_handle_corba_exception(&ev, err))
    {
      /* FIXME we could do better here... maybe respawn the server if needed... */
      return FALSE;
    }

  g_return_val_if_fail(*err == NULL, FALSE);
  
  return TRUE;
}

GSList*      
gconf_all_entries(GConfEngine* conf, const gchar* dir, GConfError** err)
{
  GConfEnginePrivate* priv = (GConfEnginePrivate*)conf;
  GSList* pairs = NULL;
  ConfigServer_ValueList* values;
  ConfigServer_KeyList* keys;
  CORBA_Environment ev;
  ConfigServer cs;
  guint i;

  if (!gconf_key_check(dir, err))
    return NULL;

  cs = gconf_get_config_server(TRUE, err);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_return_val_if_fail(((err == NULL) || ((*err)->num == G_CONF_NO_SERVER)), NULL);
      return NULL;
    }

  CORBA_exception_init(&ev);
  
  ConfigServer_all_entries(cs, priv->context,
                           (gchar*)dir, 
                           &keys, &values,
                           &ev);

  if (gconf_handle_corba_exception(&ev, err))
    {
      /* FIXME we could do better here... maybe respawn the server if needed... */

      return NULL;
    }
  
  if (keys->_length != values->_length)
    {
      g_warning("Received unmatched key/value sequences in %s",
                __FUNCTION__);
      return NULL;
    }

  i = 0;
  while (i < keys->_length)
    {
      GConfEntry* pair;

      pair = 
        gconf_entry_new(g_strdup(keys->_buffer[i]),
                        gconf_value_from_corba_value(&(values->_buffer[i])));
      
      pairs = g_slist_prepend(pairs, pair);
      
      ++i;
    }
  
  CORBA_free(keys);
  CORBA_free(values);

  return pairs;
}

GSList*      
gconf_all_dirs(GConfEngine* conf, const gchar* dir, GConfError** err)
{
  GConfEnginePrivate* priv = (GConfEnginePrivate*)conf;
  GSList* subdirs = NULL;
  ConfigServer_KeyList* keys;
  CORBA_Environment ev;
  ConfigServer cs;
  guint i;

  if (!gconf_key_check(dir, err))
    return NULL;

  cs = gconf_get_config_server(TRUE, err);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_return_val_if_fail(((err == NULL) || ((*err)->num == G_CONF_NO_SERVER)), NULL);
      return NULL;
    }

  CORBA_exception_init(&ev);
  
  ConfigServer_all_dirs(cs, priv->context,
                        (gchar*)dir, 
                        &keys,
                        &ev);


  if (gconf_handle_corba_exception(&ev, err))
    {
      /* FIXME we could do better here... maybe respawn the server if needed... */

      return NULL;
    }
  
  i = 0;
  while (i < keys->_length)
    {
      gchar* s;

      s = g_strdup(keys->_buffer[i]);
      
      subdirs = g_slist_prepend(subdirs, s);
      
      ++i;
    }
  
  CORBA_free(keys);

  return subdirs;
}

void 
gconf_sync(GConfEngine* conf, GConfError** err)
{
  GConfEnginePrivate* priv = (GConfEnginePrivate*)conf;
  CORBA_Environment ev;
  ConfigServer cs;

  cs = gconf_get_config_server(TRUE, err);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_return_if_fail(((err == NULL) || ((*err)->num == G_CONF_NO_SERVER)));
      return;
    }

  CORBA_exception_init(&ev);

  ConfigServer_sync(cs, priv->context, &ev);

  if (gconf_handle_corba_exception(&ev, err))  
    {
      /* FIXME we could do better here... maybe respawn the server if needed... */
    }
}

gboolean
gconf_dir_exists(GConfEngine *conf, const gchar *dir, GConfError** err)
{
  GConfEnginePrivate* priv = (GConfEnginePrivate*)conf;
  CORBA_Environment ev;
  ConfigServer cs;
  CORBA_boolean server_ret;

  g_return_val_if_fail(dir != NULL, FALSE);
  
  if (!gconf_key_check(dir, err))
    return FALSE;
  
  cs = gconf_get_config_server(TRUE, err);
  
  if (cs == CORBA_OBJECT_NIL)
    {
      g_return_val_if_fail(((err == NULL) || ((*err)->num == G_CONF_NO_SERVER)), FALSE);
      return FALSE;
    }
  
  CORBA_exception_init(&ev);
  
  server_ret = ConfigServer_dir_exists(cs, priv->context,
                                       (gchar*)dir, &ev);
  
  if (gconf_handle_corba_exception(&ev, err))  
    {
      /* FIXME we could do better here... maybe respawn the server if needed... */
    }

  return (server_ret == CORBA_TRUE);
}

/*
 * Connection maintenance
 */

static GConfCnxn* 
gconf_cnxn_new(GConfEngine* conf, CORBA_unsigned_long server_id, GConfNotifyFunc func, gpointer user_data)
{
  GConfCnxn* cnxn;
  static guint next_id = 1;
  
  cnxn = g_new0(GConfCnxn, 1);

  cnxn->conf = conf;
  cnxn->server_id = server_id;
  cnxn->client_id = next_id;
  cnxn->func = func;
  cnxn->user_data = user_data;

  ++next_id;

  return cnxn;
}

static void      
gconf_cnxn_destroy(GConfCnxn* cnxn)
{
  g_free(cnxn);
}

static void       
gconf_cnxn_notify(GConfCnxn* cnxn,
                   const gchar* key, GConfValue* value)
{
  (*cnxn->func)(cnxn->conf, cnxn->client_id, key, value, cnxn->user_data);
}

/*
 *  CORBA glue
 */

static ConfigServer   server = CORBA_OBJECT_NIL;

/* errors in here should be G_CONF_NO_SERVER */
static ConfigServer
try_to_contact_server(GConfError** err)
{
  /* This writing-IOR-to-file crap is a temporary hack. */
  gchar* ior;

  g_return_val_if_fail(server == CORBA_OBJECT_NIL, server);
  
  ior = gconf_read_server_ior(err);

  if (ior == NULL)
    return CORBA_OBJECT_NIL;
  
  if (ior != NULL)
    {
      CORBA_Environment ev;

      CORBA_exception_init(&ev);

      /* May well fail, could be a stale IOR */
      server = CORBA_ORB_string_to_object(gconf_get_orb(), ior, &ev);

      /* So try to ping server */
      if (server != CORBA_OBJECT_NIL)
        {
          ConfigServer_ping(server, &ev);

          if (ev._major != CORBA_NO_EXCEPTION)
            {
              server = CORBA_OBJECT_NIL;
              if (err)
                *err = gconf_error_new(G_CONF_NO_SERVER, _("Pinging the server failed, CORBA error: %s"),
                                        CORBA_exception_id(&ev));
              CORBA_exception_free(&ev);
            }
        }
      else
        {
          if (err)
            *err = gconf_error_new(G_CONF_NO_SERVER, _("Failed to convert server IOR to an object reference"));
        }
    }
      
  return server;
}

/* All errors set in here should be G_CONF_NO_SERVER; should
   only set errors if start_if_not_found is TRUE */
static ConfigServer
gconf_get_config_server(gboolean start_if_not_found, GConfError** err)
{
  if (server != CORBA_OBJECT_NIL)
    return server;
  
  server = try_to_contact_server(err);

  if (!start_if_not_found)
    return server;

  if (server == CORBA_OBJECT_NIL)
    {
      pid_t pid;
      int fds[2];
      int status;

      if (pipe(fds) < 0)
        {
          if (err)
            *err = gconf_error_new(G_CONF_NO_SERVER, _("Failed to create pipe to server: %s"),
                                    strerror(errno));
          return CORBA_OBJECT_NIL;
        }

      pid = fork();
          
      if (pid < 0)
        {
          if (err)
            *err = gconf_error_new(G_CONF_NO_SERVER, _("gconfd fork failed: %s"), 
                                    strerror(errno));
          return CORBA_OBJECT_NIL;
        }
          
      if (pid == 0)
        {
          gchar buf[20];

          close(fds[0]);

          g_snprintf(buf, 20, "%d", fds[1]);
          
          /* Child. Exec... */
          if (execlp("gconfd", "gconfd", buf, NULL) < 0)
            {
              /* in the child, don't want to set error */
              g_warning(_("Failed to exec gconfd: %s"), strerror(errno));
       
              close(fds[1]);
              
              _exit(1);
            }
        }
          
      /* Parent - waitpid(), gconfd instantly forks anyway */
      if (waitpid(pid, &status, 0) != pid)
        {
          if (err)
            *err = gconf_error_new(G_CONF_NO_SERVER, 
                                    _("waitpid() failed waiting for child in %s: %s"),
                                    __FUNCTION__, strerror(errno));
          close(fds[1]);
          return CORBA_OBJECT_NIL;
        }

      if (WIFEXITED(status))
        {
          if (WEXITSTATUS(status) != 0)
            {
              if (err)
                *err = gconf_error_new(G_CONF_NO_SERVER, 
                                        _("spawned server returned error code, giving up on contacting it."));
              close(fds[1]);
              return CORBA_OBJECT_NIL;
            }
        }
      else
        {
          if (err)
            *err = gconf_error_new(G_CONF_NO_SERVER, _("spawned gconfd child didn't exit normally, can't contact server."));
          close(fds[1]);
          return CORBA_OBJECT_NIL;
        }

      close(fds[1]);
      
      /* Wait for the child to send us a byte */
      {
        char c = '\0';

        if (read(fds[0], &c, 1) < 0)
          {
            /* Not a fatal error */
            g_warning("Error reading from pipe to gconfd: %s", strerror(errno));
            c = 'g'; /* suppress next error message */
          }

        if (c != 'g') /* g is the magic letter */
          {
            /* not fatal either */
            g_warning("gconfd sent us the wrong byte!");
          }

        close(fds[0]);
      }

      server = try_to_contact_server(err);
    }

  return server;
}


ConfigListener listener = CORBA_OBJECT_NIL;

static void 
notify(PortableServer_Servant servant, 
       CORBA_unsigned_long cnxn,
       CORBA_char* key, 
       ConfigValue* value,
       CORBA_Environment *ev);

static PortableServer_ServantBase__epv base_epv = {
  NULL,
  NULL,
  NULL
};

static POA_ConfigListener__epv listener_epv = { NULL, notify };
static POA_ConfigListener__vepv poa_listener_vepv = { &base_epv, &listener_epv };
static POA_ConfigListener poa_listener_servant = { NULL, &poa_listener_vepv };

static void 
notify(PortableServer_Servant servant, 
       CORBA_unsigned_long server_id,
       CORBA_char* key, 
       ConfigValue* value,
       CORBA_Environment *ev)
{
  GConfCnxn* cnxn;
  GConfValue* gvalue;

  printf("Client GConf library received notify for ID %u key `%s'\n", 
         (guint)server_id, key);

  cnxn = ctable_lookup_by_server_id(ctable, server_id);
  
  if (cnxn == NULL)
    {
      g_warning("Client received notify for unknown connection ID %u", (guint)server_id);
      return;
    }

  gvalue = gconf_value_from_corba_value(value);

  gconf_cnxn_notify(cnxn, key, gvalue);

  if (gvalue != NULL)
    gconf_value_destroy(gvalue);
}

static ConfigListener 
gconf_get_config_listener(void)
{
  return listener;
}

static gboolean have_initted = FALSE;

gboolean     
gconf_init           (GConfError** err)
{
  static CORBA_ORB orb = CORBA_OBJECT_NIL;

  if (have_initted)
    {
      g_warning("Attempt to init GConf a second time");
      return FALSE;
    }

  orb = gconf_get_orb();

  if (orb == CORBA_OBJECT_NIL)
    {
      /* warn instead of error, since it indicates an application bug 
         (app should have checked errors after initting the orb) 
      */
      g_warning("Failed to get orb (perhaps orb wasn't set/initted?)");
      return FALSE;
    }

  if (listener == CORBA_OBJECT_NIL)
    {
      CORBA_Environment ev;
      PortableServer_ObjectId objid = {0, sizeof("ConfigListener"), "ConfigListener"};
      PortableServer_POA poa;

      CORBA_exception_init(&ev);
      POA_ConfigListener__init(&poa_listener_servant, &ev);
      
      if (ev._major != CORBA_NO_EXCEPTION)
        {
          if (err)
            *err = gconf_error_new(G_CONF_FAILED, _("Failed to init the listener servant: %s"),
                                    CORBA_exception_id(&ev));
          return FALSE;
        }

      poa = (PortableServer_POA)CORBA_ORB_resolve_initial_references(gconf_get_orb(), "RootPOA", &ev);

      if (ev._major != CORBA_NO_EXCEPTION)
        {
          if (err)
            *err = gconf_error_new(G_CONF_FAILED, _("Failed to resolve the root POA: %s"),
                                    CORBA_exception_id(&ev));
          return FALSE;
        }


      PortableServer_POAManager_activate(PortableServer_POA__get_the_POAManager(poa, &ev), &ev);

      if (ev._major != CORBA_NO_EXCEPTION)
        {
          if (err)
            *err = gconf_error_new(G_CONF_FAILED, _("Failed to activate the POA Manager: %s"),
                                    CORBA_exception_id(&ev));
          return FALSE;
        }

      PortableServer_POA_activate_object_with_id(poa,
                                                 &objid, &poa_listener_servant, &ev);

      if (ev._major != CORBA_NO_EXCEPTION)
        {
          if (err)
            *err = gconf_error_new(G_CONF_FAILED, _("Failed to activate the listener servant: %s"),
                                    CORBA_exception_id(&ev));
          return FALSE;
        }

      
      listener = PortableServer_POA_servant_to_reference(poa,
                                                         &poa_listener_servant,
                                                         &ev);

      if (listener == CORBA_OBJECT_NIL || ev._major != CORBA_NO_EXCEPTION)
        {
          if (err)
            *err = gconf_error_new(G_CONF_FAILED, _("Failed get object reference for the listener servant: %s"),
                                    CORBA_exception_id(&ev));
          return FALSE;
        }
    }

  ctable = ctable_new();

  return TRUE;
}

gboolean
gconf_is_initialized (void)
{
  return (have_initted);
}

/* 
 * Ampersand and <> are not allowed due to the XML backend; shell
 * special characters aren't allowed; others are just in case we need
 * some magic characters someday.  hyphen, underscore, period, colon
 * are allowed as separators. % disallowed to avoid printf confusion.
 */

/* Key/dir validity is exactly the same, except that '/' must be a dir, 
   but we are sort of ignoring that for now. */

static const gchar invalid_chars[] = "\"$&<>,+=#!()'|{}[]?~`;%\\";

gboolean     
gconf_valid_key      (const gchar* key, gchar** why_invalid)
{
  const gchar* s = key;
  gboolean just_saw_slash = FALSE;

  /* Key must start with the root */
  if (*key != '/')
    {
      if (why_invalid != NULL)
        *why_invalid = g_strdup(_("Must begin with a slash (/)"));
      return FALSE;
    }
  
  /* Root key is a valid dir */
  if (*key == '/' && key[1] == '\0')
    return TRUE;

  while (*s)
    {
      if (just_saw_slash)
        {
          /* Can't have two slashes in a row, since it would mean
           * an empty spot.
           * Can't have a period right after a slash,
           * because it would be a pain for filesystem-based backends.
           */
          if (*s == '/' || *s == '.')
            {
              if (why_invalid != NULL)
                {
                  if (*s == '/')
                    *why_invalid = g_strdup(_("Can't have two slashes (/) in a row"));
                  else
                    *why_invalid = g_strdup(_("Can't have a period (.) right after a slash (/)"));
                }
              return FALSE;
            }
        }

      if (*s == '/')
        {
          just_saw_slash = TRUE;
        }
      else
        {
          const gchar* inv = invalid_chars;

          just_saw_slash = FALSE;

          while (*inv)
            {
              if (*inv == *s)
                {
                  if (why_invalid != NULL)
                    *why_invalid = g_strdup_printf(_("`%c' is an invalid character in key/directory names"), *s);
                  return FALSE;
                }
              ++inv;
            }
        }

      ++s;
    }

  /* Can't end with slash */
  if (just_saw_slash)
    {
      if (why_invalid != NULL)
        *why_invalid = g_strdup(_("Key/directory may not end with a slash (/)"));
      return FALSE;
    }
  else
    return TRUE;
}

/*
 * Table of connections 
 */ 

static gint
corba_unsigned_long_equal (gconstpointer v1,
                           gconstpointer v2)
{
  return *((const CORBA_unsigned_long*) v1) == *((const CORBA_unsigned_long*) v2);
}

static guint
corba_unsigned_long_hash (gconstpointer v)
{
  /* for our purposes we can just assume 32 bits are significant */
  return (guint)(*(const CORBA_unsigned_long*) v);
}

static CnxnTable* 
ctable_new(void)
{
  CnxnTable* ct;

  ct = g_new(CnxnTable, 1);

  ct->server_ids = g_hash_table_new(corba_unsigned_long_hash, corba_unsigned_long_equal);  
  ct->client_ids = g_hash_table_new(g_int_hash, g_int_equal);

  return ct;
}

static void       
ctable_insert(CnxnTable* ct, GConfCnxn* cnxn)
{
  g_hash_table_insert(ct->server_ids, &cnxn->server_id, cnxn);
  g_hash_table_insert(ct->client_ids, &cnxn->client_id, cnxn);
}

static void       
ctable_remove(CnxnTable* ct, GConfCnxn* cnxn)
{
  g_hash_table_remove(ct->server_ids, &cnxn->server_id);
  g_hash_table_remove(ct->client_ids, &cnxn->client_id);
}

static void       
ctable_remove_by_client_id(CnxnTable* ct, guint client_id)
{
  GConfCnxn* cnxn;

  cnxn = ctable_lookup_by_client_id(ct, client_id);

  g_return_if_fail(cnxn != NULL);

  ctable_remove(ctable, cnxn);
}

struct RemoveData {
  GSList* removed;
  GConfEngine* conf;
  gboolean save_removed;
};

static gboolean
remove_by_conf(gpointer key, gpointer value, gpointer user_data)
{
  struct RemoveData* rd = user_data;
  GConfCnxn* cnxn = value;
  
  if (cnxn->conf == rd->conf)
    {
      if (rd->save_removed)
        rd->removed = g_slist_prepend(rd->removed, cnxn);

      return TRUE;  /* remove this one */
    }
  else 
    return FALSE; /* or not */
}

/* We return a list of the removed GConfCnxn */
static GSList*      
ctable_remove_by_conf(CnxnTable* ct, GConfEngine* conf)
{
  guint client_ids_removed;
  guint server_ids_removed;
  struct RemoveData rd;

  rd.removed = NULL;
  rd.conf = conf;
  rd.save_removed = TRUE;
  
  client_ids_removed = g_hash_table_foreach_remove(ct->server_ids, remove_by_conf, &rd);

  rd.save_removed = FALSE;

  server_ids_removed = g_hash_table_foreach_remove(ct->client_ids, remove_by_conf, &rd);

  g_assert(client_ids_removed == server_ids_removed);
  g_assert(client_ids_removed == g_slist_length(rd.removed));

  return rd.removed;
}

static GConfCnxn* 
ctable_lookup_by_client_id(CnxnTable* ct, guint client_id)
{
  return g_hash_table_lookup(ctable->client_ids, &client_id);
}

static GConfCnxn* 
ctable_lookup_by_server_id(CnxnTable* ct, CORBA_unsigned_long server_id)
{
  return g_hash_table_lookup(ctable->server_ids, &server_id);
}


/*
 * Daemon control
 */

void          
gconf_shutdown_daemon(GConfError** err)
{
  CORBA_Environment ev;
  ConfigServer cs;

  cs = gconf_get_config_server(FALSE, err); /* Don't want to spawn it if it's already down */

  if (cs == CORBA_OBJECT_NIL)
    {
      g_return_if_fail(((err == NULL) || ((*err)->num == G_CONF_NO_SERVER)));
      return;
    }

  CORBA_exception_init(&ev);

  ConfigServer_shutdown(cs, &ev);

  if (ev._major != CORBA_NO_EXCEPTION)
    {
      if (err)
        *err = gconf_error_new(G_CONF_FAILED, _("Failure shutting down config server: %s"),
                                CORBA_exception_id(&ev));
      /* FIXME we could do better here... maybe respawn the server if needed... */
      CORBA_exception_free(&ev);
    }
}

gboolean
gconf_ping_daemon(void)
{
  ConfigServer cs;
  
  cs = gconf_get_config_server(FALSE, NULL); /* ignore error, since whole point is to see if server is reachable */

  if (cs == CORBA_OBJECT_NIL)
    return FALSE;
  else
    return TRUE;
}

gboolean
gconf_spawn_daemon(GConfError** err)
{
  ConfigServer cs;

  cs = gconf_get_config_server(TRUE, err);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_return_val_if_fail(err == NULL || *err != NULL, FALSE);
      return FALSE; /* Failed to spawn, error should be set */
    }
  else
    return TRUE;
}

/*
 * Sugar functions 
 */

gdouble      
gconf_get_float (GConfEngine* conf, const gchar* key,
                  gdouble deflt, GConfError** err)
{
  GConfValue* val;

  val = gconf_get(conf, key, err);

  if (val == NULL)
    return deflt;
  else
    {
      gdouble retval;
      
      if (val->type != G_CONF_VALUE_FLOAT)
        {
          if (err)
            *err = gconf_error_new(G_CONF_TYPE_MISMATCH, _("Expected float, got %s"),
                                    gconf_value_type_to_string(val->type));
          gconf_value_destroy(val);
          return deflt;
        }

      retval = gconf_value_float(val);

      gconf_value_destroy(val);

      return retval;
    }
}

gint         
gconf_get_int   (GConfEngine* conf, const gchar* key,
                  gint deflt, GConfError** err)
{
  GConfValue* val;

  val = gconf_get(conf, key, err);

  if (val == NULL)
    return deflt;
  else
    {
      gint retval;

      if (val->type != G_CONF_VALUE_INT)
        {
          if (err)
            *err = gconf_error_new(G_CONF_TYPE_MISMATCH, _("Expected int, got %s"),
                                    gconf_value_type_to_string(val->type));
          gconf_value_destroy(val);
          return deflt;
        }

      retval = gconf_value_int(val);

      gconf_value_destroy(val);

      return retval;
    }
}

gchar*       
gconf_get_string(GConfEngine* conf, const gchar* key,
                  const gchar* deflt, GConfError** err)
{
  GConfValue* val;

  val = gconf_get(conf, key, err);

  if (val == NULL)
    return deflt ? g_strdup(deflt) : NULL;
  else
    {
      gchar* retval;

      if (val->type != G_CONF_VALUE_STRING)
        {
          if (err)
            *err = gconf_error_new(G_CONF_TYPE_MISMATCH, _("Expected string, got %s"),
                                    gconf_value_type_to_string(val->type));
          gconf_value_destroy(val);
          return deflt ? g_strdup(deflt) : NULL;
        }

      retval = gconf_value_string(val);

      /* This is a cheat; don't copy */
      val->d.string_data = NULL; /* don't delete the string */

      gconf_value_destroy(val);

      return retval;
    }
}

gboolean     
gconf_get_bool  (GConfEngine* conf, const gchar* key,
                  gboolean deflt, GConfError** err)
{
  GConfValue* val;

  val = gconf_get(conf, key, err);

  if (val == NULL)
    return deflt;
  else
    {
      gboolean retval;

      if (val->type != G_CONF_VALUE_BOOL)
        {
          if (err)
            *err = gconf_error_new(G_CONF_TYPE_MISMATCH, _("Expected bool, got %s"),
                                    gconf_value_type_to_string(val->type));
          gconf_value_destroy(val);
          return deflt;
        }

      retval = gconf_value_bool(val);

      gconf_value_destroy(val);

      return retval;
    }
}

GConfSchema* 
gconf_get_schema  (GConfEngine* conf, const gchar* key, GConfError** err)
{
  GConfValue* val;

  val = gconf_get(conf, key, err);

  if (val == NULL)
    return NULL;
  else
    {
      GConfSchema* retval;

      if (val->type != G_CONF_VALUE_SCHEMA)
        {
          if (err)
            *err = gconf_error_new(G_CONF_TYPE_MISMATCH, _("Expected schema, got %s"),
                                    gconf_value_type_to_string(val->type));
          gconf_value_destroy(val);
          return NULL;
        }

      retval = gconf_value_schema(val);

      /* This is a cheat; don't copy */
      val->d.schema_data = NULL; /* don't delete the schema */

      gconf_value_destroy(val);

      return retval;
    }
}

/*
 * Setters
 */

static gboolean
error_checked_set(GConfEngine* conf, const gchar* key,
                  GConfValue* gval, GConfError** err)
{
  GConfError* my_err = NULL;
  
  gconf_set(conf, key, gval, &my_err);

  gconf_value_destroy(gval);
  
  if (my_err != NULL)
    {
      if (err)
        *err = my_err;
      return FALSE;
    }
  else
    return TRUE;
}

gboolean
gconf_set_float   (GConfEngine* conf, const gchar* key,
                    gdouble val, GConfError** err)
{
  GConfValue* gval;

  gval = gconf_value_new(G_CONF_VALUE_FLOAT);

  gconf_value_set_float(gval, val);

  return error_checked_set(conf, key, gval, err);
}

gboolean
gconf_set_int     (GConfEngine* conf, const gchar* key,
                    gint val, GConfError** err)
{
  GConfValue* gval;

  gval = gconf_value_new(G_CONF_VALUE_INT);

  gconf_value_set_int(gval, val);

  return error_checked_set(conf, key, gval, err);
}

gboolean
gconf_set_string  (GConfEngine* conf, const gchar* key,
                    const gchar* val, GConfError** err)
{
  GConfValue* gval;

  gval = gconf_value_new(G_CONF_VALUE_STRING);

  gconf_value_set_string(gval, val);

  return error_checked_set(conf, key, gval, err);
}

gboolean
gconf_set_bool    (GConfEngine* conf, const gchar* key,
                    gboolean val, GConfError** err)
{
  GConfValue* gval;

  gval = gconf_value_new(G_CONF_VALUE_BOOL);

  gconf_value_set_bool(gval, val);

  return error_checked_set(conf, key, gval, err);
}

gboolean
gconf_set_schema  (GConfEngine* conf, const gchar* key,
                    GConfSchema* val, GConfError** err)
{
  GConfValue* gval;

  gval = gconf_value_new(G_CONF_VALUE_SCHEMA);

  gconf_value_set_schema(gval, val);

  return error_checked_set(conf, key, gval, err);
}

/* CORBA Util */

/* Set GConfErrNo from an exception, free exception, etc. */

static GConfErrNo
corba_errno_to_gconf_errno(ConfigErrorType corba_err)
{
  switch (corba_err)
    {
    case ConfigFailed:
      return G_CONF_FAILED;
      break;
    case ConfigNoPermission:
      return G_CONF_NO_PERMISSION;
      break;
    case ConfigBadAddress:
      return G_CONF_BAD_ADDRESS;
      break;
    case ConfigBadKey:
      return G_CONF_BAD_KEY;
      break;
    case ConfigParseError:
      return G_CONF_PARSE_ERROR;
      break;
    case ConfigCorrupt:
      return G_CONF_CORRUPT;
      break;
    case ConfigTypeMismatch:
      return G_CONF_TYPE_MISMATCH;
      break;
    case ConfigIsDir:
      return G_CONF_IS_DIR;
      break;
    case ConfigIsKey:
      return G_CONF_IS_KEY;
      break;
    case ConfigOverridden:
      return G_CONF_OVERRIDDEN;
      break;
    default:
      g_assert_not_reached();
      return G_CONF_SUCCESS; /* warnings */
      break;
    }
}

static gboolean
gconf_handle_corba_exception(CORBA_Environment* ev, GConfError** err)
{
  switch (ev->_major)
    {
    case CORBA_NO_EXCEPTION:
      CORBA_exception_free(ev);
      return FALSE;
      break;
    case CORBA_SYSTEM_EXCEPTION:
      if (err)
        *err = gconf_error_new(G_CONF_NO_SERVER, _("CORBA error: %s"),
                                CORBA_exception_id(ev));
      CORBA_exception_free(ev);
      return TRUE;
      break;
    case CORBA_USER_EXCEPTION:
      {
        ConfigException* ce;

        ce = CORBA_exception_value(ev);

        if (err)
          *err = gconf_error_new(corba_errno_to_gconf_errno(ce->err_no),
                                  ce->message);
        CORBA_exception_free(ev);
        return TRUE;
      }
      break;
    default:
      g_assert_not_reached();
      return TRUE;
      break;
    }
}
