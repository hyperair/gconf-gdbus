
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
  /* This object may be pointless... */
  gpointer dummy;

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

  return (GConf*) priv;
}

void         
g_conf_destroy        (GConf* conf)
{
  /* Remove all connections associated with this GConf */
  GSList* removed;
  GSList* tmp;
  CORBA_Environment ev;

  CORBA_exception_init(&ev);

  removed = ctable_remove_by_conf(ctable, conf);
  
  tmp = removed;
  while (tmp != NULL)
    {
      GConfCnxn* gcnxn = tmp->data;

      ConfigServer_remove_listener(g_conf_get_config_server(TRUE), 
                                   gcnxn->server_id,
                                   &ev);

      if (ev._major != CORBA_NO_EXCEPTION)
        {
          g_warning("Failure removing listener %u from the config server: %s",
                    (guint)gcnxn->server_id,
                    CORBA_exception_id(&ev));
          CORBA_exception_free(&ev);
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
    {
      g_warning("Couldn't get config server");
      return 0;
    }
  
  CORBA_exception_init(&ev);

  cl = g_conf_get_config_listener();
  
  /* Should have aborted the program in this case probably */
  g_return_val_if_fail(cl != CORBA_OBJECT_NIL, 0);

  id = ConfigServer_add_listener(cs, (gchar*)namespace_section, 
                                 cl, &ev);

  if (ev._major != CORBA_NO_EXCEPTION)
    {
      g_warning("Failure adding listener to the config server: %s",
                CORBA_exception_id(&ev));
      /* FIXME we could do better here... maybe respawn the server if needed... */
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
  GConfCnxn* gcnxn;
  CORBA_Environment ev;

  CORBA_exception_init(&ev);

  gcnxn = ctable_lookup_by_client_id(ctable, client_id);

  g_return_if_fail(gcnxn != NULL);

  ConfigServer_remove_listener(g_conf_get_config_server(TRUE), 
                               gcnxn->server_id,
                               &ev);

  if (ev._major != CORBA_NO_EXCEPTION)
    {
      g_warning("Failure removing listener from the config server: %s",
                CORBA_exception_id(&ev));
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
  GConfValue* val;
  ConfigValue* cv;
  CORBA_Environment ev;
  ConfigServer cs;

  if (!g_conf_valid_key(key))
    {
      g_warning("Invalid key `%s'", key);
      return NULL;
    }

  cs = g_conf_get_config_server(TRUE);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_warning("Couldn't get config server");
      return NULL;
    }

  CORBA_exception_init(&ev);
  
  cv = ConfigServer_lookup(cs, (gchar*)key, &ev);

  if (ev._major != CORBA_NO_EXCEPTION)
    {
      g_warning("Failure getting value from config server: %s",
                CORBA_exception_id(&ev));
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
  ConfigValue* cv;
  CORBA_Environment ev;
  ConfigServer cs;

  g_return_if_fail(value->type != G_CONF_VALUE_INVALID);

  if (!g_conf_valid_key(key))
    {
      g_warning("Invalid key `%s'", key);
      return;
    }

  cs = g_conf_get_config_server(TRUE);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_warning("Couldn't get config server");
      return;
    }

  cv = corba_value_from_g_conf_value(value);

  CORBA_exception_init(&ev);

  ConfigServer_set(cs,
                   (gchar*)key, cv,
                   &ev);

  CORBA_free(cv);

  if (ev._major != CORBA_NO_EXCEPTION)
    {
      g_warning("Failure sending value to config server: %s",
                CORBA_exception_id(&ev));
      /* FIXME we could do better here... maybe respawn the server if needed... */
      CORBA_exception_free(&ev);
    }
}

void         
g_conf_unset(GConf* conf, const gchar* key)
{
  CORBA_Environment ev;
  ConfigServer cs;

  if (!g_conf_valid_key(key))
    {
      g_warning("Invalid key `%s'", key);
      return;
    }

  cs = g_conf_get_config_server(TRUE);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_warning("Couldn't get config server");
      return;
    }

  CORBA_exception_init(&ev);

  ConfigServer_unset(cs,
                   (gchar*)key,
                   &ev);

  if (ev._major != CORBA_NO_EXCEPTION)
    {
      g_warning("Failure sending unset request to config server: %s",
                CORBA_exception_id(&ev));
      /* FIXME we could do better here... maybe respawn the server if needed... */
      CORBA_exception_free(&ev);
    }
}

GSList*      
g_conf_all_pairs(GConf* conf, const gchar* dir)
{
  GSList* pairs = NULL;
  ConfigServer_ValueList* values;
  ConfigServer_KeyList* keys;
  CORBA_Environment ev;
  ConfigServer cs;
  guint i;

  if (!g_conf_valid_key(dir))
    {
      g_warning("Invalid dir `%s'", dir);
      return NULL;
    }

  cs = g_conf_get_config_server(TRUE);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_warning("Couldn't get config server");
      return NULL;
    }

  CORBA_exception_init(&ev);
  
  ConfigServer_all_pairs(cs, (gchar*)dir, 
                         &keys, &values,
                         &ev);

  if (ev._major != CORBA_NO_EXCEPTION)
    {
      g_warning("Failure getting list of entries in `%s' from config server: %s",
                dir, CORBA_exception_id(&ev));
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
      GConfPair* pair;

      pair = 
        g_conf_pair_new(g_strdup(keys->_buffer[i]),
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
  GSList* subdirs = NULL;
  ConfigServer_KeyList* keys;
  CORBA_Environment ev;
  ConfigServer cs;
  guint i;

  if (!g_conf_valid_key(dir))
    {
      g_warning("Invalid dir `%s'", dir);
      return NULL;
    }

  cs = g_conf_get_config_server(TRUE);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_warning("Couldn't get config server");
      return NULL;
    }

  CORBA_exception_init(&ev);
  
  ConfigServer_all_dirs(cs, (gchar*)dir, 
                        &keys,
                        &ev);

  if (ev._major != CORBA_NO_EXCEPTION)
    {
      g_warning("Failure getting list of subdirs in `%s' from config server: %s",
                dir, CORBA_exception_id(&ev));
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
  CORBA_Environment ev;
  ConfigServer cs;

  cs = g_conf_get_config_server(TRUE);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_warning("Couldn't get config server");
      return;
    }

  CORBA_exception_init(&ev);

  ConfigServer_sync(cs, &ev);

  if (ev._major != CORBA_NO_EXCEPTION)
    {
      g_warning("Failure syncing config server: %s",
                CORBA_exception_id(&ev));
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
        g_warning("Pipe creation failed: %s", strerror(errno));

      pid = fork();
          
      if (pid < 0)
        {
          g_warning("gconfd fork failed: %s", strerror(errno));
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
              g_warning("Failed to exec gconfd: %s", strerror(errno));
       
              close(fds[1]);
              
              /* Return error to parent, but parent is currently lame
                 and doesn't check. */
              _exit(1);
            }
        }
          
      /* Parent - waitpid(), gconfd instantly forks anyway */
      if (waitpid(pid, &status, 0) != pid)
        {
          g_warning("waitpid() failed waiting for child in %s: %s",
                    __FUNCTION__, strerror(errno));
          return CORBA_OBJECT_NIL;
        }

      if (WIFEXITED(status))
        {
          if (WEXITSTATUS(status) != 0)
            {
              g_warning("spawned gconfd child exited abnormally, giving up on contacting it.");
              return CORBA_OBJECT_NIL;
            }
        }
      else
        {
          g_warning("spawned gconfd child didn't exit normally, things are looking grim.");
        }

      close(fds[1]);
      
      /* Wait for the child to send us a byte */
      {
        char c = '\0';

        if (read(fds[0], &c, 1) < 0)
          {
            g_warning("Error reading from pipe to gconfd: %s", strerror(errno));
            c = 'g'; /* suppress next error message */
          }

        if (c != 'g') /* g is the magic letter */
          {
            g_warning("gconfd sent us the wrong byte!");
          }

        close(fds[0]);
      }

      server = try_to_contact_server();
    }

  if (server == CORBA_OBJECT_NIL)
    g_warning("Giving up on gconfd server contact");

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

gboolean     
g_conf_init           ()
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
      
      poa = (PortableServer_POA)CORBA_ORB_resolve_initial_references(g_conf_get_orb(), "RootPOA", &ev);
      PortableServer_POAManager_activate(PortableServer_POA__get_the_POAManager(poa, &ev), &ev);
      PortableServer_POA_activate_object_with_id(poa,
                                                 &objid, &poa_listener_servant, &ev);
      
      listener = PortableServer_POA_servant_to_reference(poa,
                                                         &poa_listener_servant,
                                                         &ev);
      if (listener == CORBA_OBJECT_NIL) 
        {
          g_warning("Didn't get listener object ref: %s", CORBA_exception_id(&ev));
          return FALSE;
        }
    }

  ctable = ctable_new();

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
      printf(_("Config server (gconfd) is not running\n"));
      return;
    }

  CORBA_exception_init(&ev);

  ConfigServer_shutdown(cs, &ev);

  if (ev._major != CORBA_NO_EXCEPTION)
    {
      g_warning("Failure shutting down config server: %s",
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
    return FALSE;
  else
    return TRUE;
}

gboolean
g_conf_spawn_daemon(void)
{
  ConfigServer cs;

  cs = g_conf_get_config_server(TRUE);

  if (cs == CORBA_OBJECT_NIL)
    return FALSE; /* Failed to spawn */
  else
    return TRUE;
}
