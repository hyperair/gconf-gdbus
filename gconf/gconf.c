
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


/*
 * Error handling
 */

static gchar* last_details = NULL;
static GConfErrNo last_errno = G_CONF_SUCCESS;

static const gchar* err_msgs[10] = {
  N_("Success"),
  N_("Failed"),
  N_("Configuration server couldn't be contacted"),
  N_("Permission denied"),
  N_("Couldn't resolve address for configuration source"),
  N_("Bad key or directory name"),
  N_("Parse error"),
  N_("Type mismatch"),
  N_("Key operation on directory"),
  N_("Directory operation on key")
};

static const int n_err_msgs = sizeof(err_msgs)/sizeof(err_msgs[0]);

void         
g_conf_clear_error(void)
{
  if (last_details)
    {
      g_free(last_details);
      last_details = NULL;
    }
  last_errno = G_CONF_SUCCESS;
}

void
g_conf_set_error(GConfErrNo en, const gchar* fmt, ...)
{
  gchar* details;
  va_list args;

  if (last_details != NULL)
    g_free(last_details);
    
  va_start (args, fmt);
  details = g_strdup_vprintf(fmt, args);
  va_end (args);

  last_details = g_strconcat(g_conf_strerror(en), ":\n ", details, NULL);

  last_errno = en;

  g_free(details);
}

const gchar* 
g_conf_error          (void)
{
  return last_details ? last_details : _("No error");
}

const gchar* 
g_conf_strerror       (GConfErrNo en)
{
  g_return_val_if_fail (en < n_err_msgs, NULL);

  return _(err_msgs[en]);    
}

GConfErrNo   
g_conf_errno          (void)
{
  return last_errno;
}

/* 
 * GConfPrivate
 */

typedef struct _GConfPrivate GConfPrivate;

struct _GConfPrivate {
  ConfigServer_Context context;

};

typedef struct _GConfCnxn GConfCnxn;

struct _GConfCnxn {
  guint client_id;
  CORBA_unsigned_long server_id; /* id returned from server */
  GConf* conf;     /* conf we're associated with */
  GConfNotifyFunc func;
  gpointer user_data;
};

static GConfCnxn* g_conf_cnxn_new(GConf* conf, CORBA_unsigned_long server_id, GConfNotifyFunc func, gpointer user_data);
static void       g_conf_cnxn_destroy(GConfCnxn* cnxn);
static void       g_conf_cnxn_notify(GConfCnxn* cnxn, const gchar* key, GConfValue* value);

static ConfigServer g_conf_get_config_server(gboolean start_if_not_found);
static ConfigListener g_conf_get_config_listener(void);

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
static GSList*    ctable_remove_by_conf(CnxnTable* ct, GConf* conf);
static GConfCnxn* ctable_lookup_by_client_id(CnxnTable* ct, guint client_id);
static GConfCnxn* ctable_lookup_by_server_id(CnxnTable* ct, CORBA_unsigned_long server_id);


/*
 *  Public Interface
 */

GConf*       
g_conf_new            (void)
{
  GConfPrivate* priv;

  priv = g_new0(GConfPrivate, 1);

  priv->context = ConfigServer_default_context;
  
  return (GConf*) priv;
}

GConf*
g_conf_new_from_address(const gchar* address)
{
  GConf* gconf;
  GConfPrivate* priv;
  CORBA_Environment ev;
  ConfigServer cs;
  ConfigServer_Context ctx;
  
  cs = g_conf_get_config_server(TRUE);

  if (cs == CORBA_OBJECT_NIL)
    return NULL; /* Error should already be set */

  CORBA_exception_init(&ev);
  
  ctx = ConfigServer_get_context(cs, (gchar*)address, &ev);

  if (ev._major != CORBA_NO_EXCEPTION)
    {
      /* FIXME we could do better here... maybe respawn the server if needed... */
      g_conf_set_error(G_CONF_NO_SERVER, _("CORBA error: %s"), CORBA_exception_id(&ev));

      CORBA_exception_free(&ev);

      return NULL;
    }
  
  if (ctx == ConfigServer_invalid_context)
    {
      g_conf_set_error(G_CONF_BAD_ADDRESS,
                       _("Server couldn't resolve the address `%s'"),
                       address);

      return NULL;
    }
  
  gconf = g_conf_new();
  
  priv = (GConfPrivate*)gconf;

  priv->context = ctx;
  
  return gconf;
}

void         
g_conf_destroy        (GConf* conf)
{
  /* Remove all connections associated with this GConf */
  GSList* removed;
  GSList* tmp;
  CORBA_Environment ev;
  ConfigServer cs;
  GConfPrivate* priv = (GConfPrivate*)conf;

  cs = g_conf_get_config_server(FALSE); /* don't restart it if down, since
                                           the new one won't have the connections
                                           to remove */

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
          ConfigServer_remove_listener(cs,
                                       priv->context,
                                       gcnxn->server_id,
                                       &ev);
          
          if (ev._major != CORBA_NO_EXCEPTION)
            {
              /* Don't set error because realistically this doesn't matter to 
                 clients */
              g_warning("Failure removing listener %u from the config server: %s",
                        (guint)gcnxn->server_id,
                        CORBA_exception_id(&ev));
              CORBA_exception_free(&ev);
            }
        }

      g_conf_cnxn_destroy(gcnxn);

      tmp = g_slist_next(tmp);
    }

  g_slist_free(removed);
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
  GConfCnxn* cnxn;

  cs = g_conf_get_config_server(TRUE);

  if (cs == CORBA_OBJECT_NIL)
    return 0;
  
  CORBA_exception_init(&ev);

  cl = g_conf_get_config_listener();
  
  /* Should have aborted the program in this case probably */
  g_return_val_if_fail(cl != CORBA_OBJECT_NIL, 0);

  id = ConfigServer_add_listener(cs, priv->context,
                                 (gchar*)namespace_section, 
                                 cl, &ev);

  if (ev._major != CORBA_NO_EXCEPTION)
    {
      /* FIXME we could do better here... maybe respawn the server if needed... */
      g_conf_set_error(G_CONF_NO_SERVER, _("CORBA error: %s"), CORBA_exception_id(&ev));

      CORBA_exception_free(&ev);

      return 0;
    }

  cnxn = g_conf_cnxn_new(conf, id, func, user_data);

  ctable_insert(ctable, cnxn);

  printf("Received ID %u from server, and mapped to client ID %u\n",
         (guint)id, cnxn->client_id);
  
  return cnxn->client_id;
}

void         
g_conf_notify_remove(GConf* conf,
                     guint client_id)
{
  GConfPrivate* priv = (GConfPrivate*)conf;
  GConfCnxn* gcnxn;
  CORBA_Environment ev;
  ConfigServer cs;

  cs = g_conf_get_config_server(TRUE);

  if (cs == CORBA_OBJECT_NIL)
    return;

  CORBA_exception_init(&ev);

  gcnxn = ctable_lookup_by_client_id(ctable, client_id);

  g_return_if_fail(gcnxn != NULL);

  ConfigServer_remove_listener(cs, priv->context,
                               gcnxn->server_id,
                               &ev);

  if (ev._major != CORBA_NO_EXCEPTION)
    {
      g_conf_set_error(G_CONF_NO_SERVER, _("CORBA error: %s"), CORBA_exception_id(&ev));

      /* FIXME we could do better here... maybe respawn the server if needed... */
      CORBA_exception_free(&ev);
    }
  

  /* We want to do this even if the CORBA fails, so if we restart gconfd and 
     reinstall listeners we don't reinstall this one. */
  ctable_remove(ctable, gcnxn);

  g_conf_cnxn_destroy(gcnxn);
}

GConfValue*  
g_conf_get(GConf* conf, const gchar* key)
{
  GConfPrivate* priv = (GConfPrivate*)conf;
  GConfValue* val;
  ConfigValue* cv;
  CORBA_Environment ev;
  ConfigServer cs;

  if (!g_conf_valid_key(key))
    {
      g_conf_set_error(G_CONF_BAD_KEY, _("`%s'"), key);
      
      return NULL;
    }

  cs = g_conf_get_config_server(TRUE);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_assert(g_conf_errno() == G_CONF_NO_SERVER);
      return NULL;
    }

  CORBA_exception_init(&ev);
  
  cv = ConfigServer_lookup(cs, priv->context, (gchar*)key, &ev);

  if (ev._major != CORBA_NO_EXCEPTION)
    {
      g_conf_set_error(G_CONF_NO_SERVER, _("CORBA error: %s"), CORBA_exception_id(&ev));

      /* FIXME we could do better here... maybe respawn the server if needed... */
      CORBA_exception_free(&ev);

      CORBA_free(cv);

      return NULL;
    }
  else
    {
      val = g_conf_value_from_corba_value(cv);
      CORBA_free(cv);

      return val;
    }
}

void
g_conf_set(GConf* conf, const gchar* key, GConfValue* value)
{
  GConfPrivate* priv = (GConfPrivate*)conf;
  ConfigValue* cv;
  CORBA_Environment ev;
  ConfigServer cs;

  g_return_if_fail(value->type != G_CONF_VALUE_INVALID);

  if (!g_conf_valid_key(key))
    {
      g_conf_set_error(G_CONF_BAD_KEY, _("`%s'"), key);
      return;
    }

  cs = g_conf_get_config_server(TRUE);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_assert(g_conf_errno() == G_CONF_NO_SERVER);
      return;
    }

  cv = corba_value_from_g_conf_value(value);

  CORBA_exception_init(&ev);

  ConfigServer_set(cs, priv->context,
                   (gchar*)key, cv,
                   &ev);

  CORBA_free(cv);

  if (ev._major != CORBA_NO_EXCEPTION)
    {
      g_conf_set_error(G_CONF_NO_SERVER, _("CORBA error: %s"), CORBA_exception_id(&ev));

      /* FIXME we could do better here... maybe respawn the server if needed... */
      CORBA_exception_free(&ev);
    }
}

void         
g_conf_unset(GConf* conf, const gchar* key)
{
  GConfPrivate* priv = (GConfPrivate*)conf;
  CORBA_Environment ev;
  ConfigServer cs;

  if (!g_conf_valid_key(key))
    {
      g_conf_set_error(G_CONF_BAD_KEY, _("`%s'"), key);
      return;
    }

  cs = g_conf_get_config_server(TRUE);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_assert(g_conf_errno() == G_CONF_NO_SERVER);
      return;
    }

  CORBA_exception_init(&ev);

  ConfigServer_unset(cs, priv->context,
                     (gchar*)key,
                     &ev);

  if (ev._major != CORBA_NO_EXCEPTION)
    {
      g_conf_set_error(G_CONF_NO_SERVER, _("CORBA error: %s"), CORBA_exception_id(&ev));

      /* FIXME we could do better here... maybe respawn the server if needed... */
      CORBA_exception_free(&ev);
    }
}

GSList*      
g_conf_all_entries(GConf* conf, const gchar* dir)
{
  GConfPrivate* priv = (GConfPrivate*)conf;
  GSList* pairs = NULL;
  ConfigServer_ValueList* values;
  ConfigServer_KeyList* keys;
  CORBA_Environment ev;
  ConfigServer cs;
  guint i;

  if (!g_conf_valid_key(dir))
    {
      g_conf_set_error(G_CONF_BAD_KEY, _("`%s'"), dir);
      return NULL;
    }

  cs = g_conf_get_config_server(TRUE);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_assert(g_conf_errno() == G_CONF_NO_SERVER);
      return NULL;
    }

  CORBA_exception_init(&ev);
  
  ConfigServer_all_entries(cs, priv->context,
                           (gchar*)dir, 
                           &keys, &values,
                           &ev);

  if (ev._major != CORBA_NO_EXCEPTION)
    {
      g_conf_set_error(G_CONF_NO_SERVER, _("CORBA error: %s"), CORBA_exception_id(&ev));

      /* FIXME we could do better here... maybe respawn the server if needed... */
      CORBA_exception_free(&ev);

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
        g_conf_entry_new(g_strdup(keys->_buffer[i]),
                        g_conf_value_from_corba_value(&(values->_buffer[i])));
      
      pairs = g_slist_prepend(pairs, pair);
      
      ++i;
    }
  
  CORBA_free(keys);
  CORBA_free(values);

  return pairs;
}

GSList*      
g_conf_all_dirs(GConf* conf, const gchar* dir)
{
  GConfPrivate* priv = (GConfPrivate*)conf;
  GSList* subdirs = NULL;
  ConfigServer_KeyList* keys;
  CORBA_Environment ev;
  ConfigServer cs;
  guint i;

  if (!g_conf_valid_key(dir))
    {
      g_conf_set_error(G_CONF_BAD_KEY, _("`%s'"), dir);
      return NULL;
    }

  cs = g_conf_get_config_server(TRUE);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_assert(g_conf_errno() == G_CONF_NO_SERVER);
      return NULL;
    }

  CORBA_exception_init(&ev);
  
  ConfigServer_all_dirs(cs, priv->context,
                        (gchar*)dir, 
                        &keys,
                        &ev);

  if (ev._major != CORBA_NO_EXCEPTION)
    {
      g_conf_set_error(G_CONF_NO_SERVER, _("CORBA error: %s"), CORBA_exception_id(&ev));

      /* FIXME we could do better here... maybe respawn the server if needed... */
      CORBA_exception_free(&ev);

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
g_conf_sync(GConf* conf)
{
  GConfPrivate* priv = (GConfPrivate*)conf;
  CORBA_Environment ev;
  ConfigServer cs;

  cs = g_conf_get_config_server(TRUE);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_assert(g_conf_errno() == G_CONF_NO_SERVER);
      return;
    }

  CORBA_exception_init(&ev);

  ConfigServer_sync(cs, priv->context, &ev);

  if (ev._major != CORBA_NO_EXCEPTION)
    {
      g_conf_set_error(G_CONF_NO_SERVER, _("CORBA error: %s"), CORBA_exception_id(&ev));

      /* FIXME we could do better here... maybe respawn the server if needed... */
      CORBA_exception_free(&ev);
    }
}

/*
 * Connection maintenance
 */

static GConfCnxn* 
g_conf_cnxn_new(GConf* conf, CORBA_unsigned_long server_id, GConfNotifyFunc func, gpointer user_data)
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
g_conf_cnxn_destroy(GConfCnxn* cnxn)
{
  g_free(cnxn);
}

static void       
g_conf_cnxn_notify(GConfCnxn* cnxn,
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
              g_conf_set_error(G_CONF_NO_SERVER, _("Pinging the server failed, CORBA error: %s"),
                               CORBA_exception_id(&ev));
              CORBA_exception_free(&ev);
            }
        }
      else 
        g_conf_set_error(G_CONF_NO_SERVER, _("Failed to convert server IOR to an object reference"));
    }
  else
    g_conf_set_error(G_CONF_NO_SERVER, _("Failed to read the server's IOR"));

  return server;
}

/* All errors set in here should be G_CONF_NO_SERVER; should
   only set errors if start_if_not_found is TRUE */
static ConfigServer
g_conf_get_config_server(gboolean start_if_not_found)
{
  if (server != CORBA_OBJECT_NIL)
    return server;
  
  server = try_to_contact_server();

  if (!start_if_not_found)
    return server;

  if (server == CORBA_OBJECT_NIL)
    {
      pid_t pid;
      int fds[2];
      int status;

      printf("spawning gconfd from this client...\n");

      if (pipe(fds) < 0)
        {
          g_conf_set_error(G_CONF_NO_SERVER, _("Failed to create pipe to server: %s"),
                           strerror(errno));
          return CORBA_OBJECT_NIL;
        }

      pid = fork();
          
      if (pid < 0)
        {
          g_conf_set_error(G_CONF_NO_SERVER, _("gconfd fork failed: %s"), 
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
              
              /* Return error to parent, but parent is currently lame
                 and doesn't check. */
              _exit(1);
            }
        }
          
      /* Parent - waitpid(), gconfd instantly forks anyway */
      if (waitpid(pid, &status, 0) != pid)
        {
          g_conf_set_error(G_CONF_NO_SERVER, 
                           _("waitpid() failed waiting for child in %s: %s"),
                           __FUNCTION__, strerror(errno));
          close(fds[1]);
          return CORBA_OBJECT_NIL;
        }

      if (WIFEXITED(status))
        {
          if (WEXITSTATUS(status) != 0)
            {
              g_conf_set_error(G_CONF_NO_SERVER, 
                               _("spawned server returned error code, giving up on contacting it."));
              close(fds[1]);
              return CORBA_OBJECT_NIL;
            }
        }
      else
        {
          g_conf_set_error(G_CONF_NO_SERVER, _("spawned gconfd child didn't exit normally, can't contact server."));
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

      server = try_to_contact_server();
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

  gvalue = g_conf_value_from_corba_value(value);

  g_conf_cnxn_notify(cnxn, key, gvalue);

  if (gvalue != NULL)
    g_conf_value_destroy(gvalue);
}

static ConfigListener 
g_conf_get_config_listener(void)
{
  return listener;
}

static gboolean have_initted = FALSE;
static gchar* global_appname = NULL;

const gchar*
g_conf_global_appname(void)
{
  if (global_appname)
    return global_appname;
  else
    return "";
}

gboolean     
g_conf_init           (const gchar* appname)
{
  static CORBA_ORB orb = CORBA_OBJECT_NIL;

  if (have_initted)
    {
      g_warning("Attempt to init GConf a second time");
      return FALSE;
    }

  orb = g_conf_get_orb();

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
          g_conf_set_error(G_CONF_FAILED, _("Failed to init the listener servant: %s"),
                           CORBA_exception_id(&ev));
          return FALSE;
        }

      poa = (PortableServer_POA)CORBA_ORB_resolve_initial_references(g_conf_get_orb(), "RootPOA", &ev);

      if (ev._major != CORBA_NO_EXCEPTION)
        {
          g_conf_set_error(G_CONF_FAILED, _("Failed to resolve the root POA: %s"),
                           CORBA_exception_id(&ev));
          return FALSE;
        }


      PortableServer_POAManager_activate(PortableServer_POA__get_the_POAManager(poa, &ev), &ev);

      if (ev._major != CORBA_NO_EXCEPTION)
        {
          g_conf_set_error(G_CONF_FAILED, _("Failed to activate the POA Manager: %s"),
                           CORBA_exception_id(&ev));
          return FALSE;
        }

      PortableServer_POA_activate_object_with_id(poa,
                                                 &objid, &poa_listener_servant, &ev);

      if (ev._major != CORBA_NO_EXCEPTION)
        {
          g_conf_set_error(G_CONF_FAILED, _("Failed to activate the listener servant: %s"),
                           CORBA_exception_id(&ev));
          return FALSE;
        }

      
      listener = PortableServer_POA_servant_to_reference(poa,
                                                         &poa_listener_servant,
                                                         &ev);

      if (listener == CORBA_OBJECT_NIL || ev._major != CORBA_NO_EXCEPTION)
        {
          g_conf_set_error(G_CONF_FAILED, _("Failed get object reference for the listener servant: %s"),
                           CORBA_exception_id(&ev));
          return FALSE;
        }
    }

  ctable = ctable_new();

  global_appname = g_strdup(appname);
  
  return TRUE;
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
g_conf_valid_key      (const gchar* key)
{
  const gchar* s = key;
  gboolean just_saw_slash = FALSE;

  /* Key must start with the root */
  if (*key != '/')
    return FALSE;
  
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
            return FALSE;
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
                return FALSE;
              ++inv;
            }
        }

      ++s;
    }

  /* Can't end with slash */
  if (just_saw_slash)
    return FALSE;
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
  GConf* conf;
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
ctable_remove_by_conf(CnxnTable* ct, GConf* conf)
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
g_conf_shutdown_daemon(void)
{
  CORBA_Environment ev;
  ConfigServer cs;

  cs = g_conf_get_config_server(FALSE); /* Don't want to spawn it if it's already down */

  if (cs == CORBA_OBJECT_NIL)
    {
      g_assert(g_conf_errno() == G_CONF_NO_SERVER);
      return;
    }

  CORBA_exception_init(&ev);

  ConfigServer_shutdown(cs, &ev);

  if (ev._major != CORBA_NO_EXCEPTION)
    {
      g_conf_set_error(G_CONF_FAILED, _("Failure shutting down config server: %s"),
                       CORBA_exception_id(&ev));
      /* FIXME we could do better here... maybe respawn the server if needed... */
      CORBA_exception_free(&ev);
    }
}

gboolean
g_conf_ping_daemon(void)
{
  ConfigServer cs;

  cs = g_conf_get_config_server(FALSE);

  if (cs == CORBA_OBJECT_NIL)
    return FALSE; /* FIXME this means an error was set, but I guess it
                     doesn't matter; errno works the same way (random
                     error setting). */
  else
    return TRUE;
}

gboolean
g_conf_spawn_daemon(void)
{
  ConfigServer cs;

  cs = g_conf_get_config_server(TRUE);

  if (cs == CORBA_OBJECT_NIL)
    return FALSE; /* Failed to spawn, error should be set */
  else
    return TRUE;
}

/*
 * Sugar functions 
 */

gdouble      
g_conf_get_float (GConf* conf, const gchar* key,
                  gdouble deflt)
{
  GConfValue* val;

  val = g_conf_get(conf, key);

  if (val == NULL)
    return deflt;
  else
    {
      gdouble retval;
      
      if (val->type != G_CONF_VALUE_FLOAT)
        {
          g_conf_set_error(G_CONF_TYPE_MISMATCH, _("Expected float, got %s"),
                           g_conf_value_type_to_string(val->type));
          g_conf_value_destroy(val);
          return deflt;
        }

      retval = g_conf_value_float(val);

      g_conf_value_destroy(val);

      return retval;
    }
}

gint         
g_conf_get_int   (GConf* conf, const gchar* key,
                  gint deflt)
{
  GConfValue* val;

  val = g_conf_get(conf, key);

  if (val == NULL)
    return deflt;
  else
    {
      gint retval;

      if (val->type != G_CONF_VALUE_INT)
        {
          g_conf_set_error(G_CONF_TYPE_MISMATCH, _("Expected int, got %s"),
                           g_conf_value_type_to_string(val->type));
          g_conf_value_destroy(val);
          return deflt;
        }

      retval = g_conf_value_int(val);

      g_conf_value_destroy(val);

      return retval;
    }
}

gchar*       
g_conf_get_string(GConf* conf, const gchar* key,
                  const gchar* deflt)
{
  GConfValue* val;

  val = g_conf_get(conf, key);

  if (val == NULL)
    return deflt ? g_strdup(deflt) : NULL;
  else
    {
      gchar* retval;

      if (val->type != G_CONF_VALUE_STRING)
        {
          g_conf_set_error(G_CONF_TYPE_MISMATCH, _("Expected string, got %s"),
                           g_conf_value_type_to_string(val->type));
          g_conf_value_destroy(val);
          return deflt ? g_strdup(deflt) : NULL;
        }

      retval = g_conf_value_string(val);

      /* This is a cheat; don't copy */
      val->d.string_data = NULL; /* don't delete the string */

      g_conf_value_destroy(val);

      return retval;
    }
}

gboolean     
g_conf_get_bool  (GConf* conf, const gchar* key,
                  gboolean deflt)
{
  GConfValue* val;

  val = g_conf_get(conf, key);

  if (val == NULL)
    return deflt;
  else
    {
      gboolean retval;

      if (val->type != G_CONF_VALUE_BOOL)
        {
          g_conf_set_error(G_CONF_TYPE_MISMATCH, _("Expected bool, got %s"),
                           g_conf_value_type_to_string(val->type));
          g_conf_value_destroy(val);
          return deflt;
        }

      retval = g_conf_value_bool(val);

      g_conf_value_destroy(val);

      return retval;
    }
}

GConfSchema* 
g_conf_get_schema  (GConf* conf, const gchar* key)
{
  GConfValue* val;

  val = g_conf_get(conf, key);

  if (val == NULL)
    return NULL;
  else
    {
      GConfSchema* retval;

      if (val->type != G_CONF_VALUE_SCHEMA)
        {
          g_conf_set_error(G_CONF_TYPE_MISMATCH, _("Expected schema, got %s"),
                           g_conf_value_type_to_string(val->type));
          g_conf_value_destroy(val);
          return NULL;
        }

      retval = g_conf_value_schema(val);

      /* This is a cheat; don't copy */
      val->d.schema_data = NULL; /* don't delete the schema */

      g_conf_value_destroy(val);

      return retval;
    }
}
