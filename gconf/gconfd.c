
/* GConf
 * Copyright (C) 1999 Red Hat Inc.
 * Developed by Havoc Pennington, some code in here borrowed from 
 * gnome-name-server and libgnorba (Elliot Lee)
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


/*
 * This is the per-user configuration daemon.
 * (has debug crap in it now)
 */

#include <config.h>

#include "gconf-internals.h"
#include "gconf-sources.h"
#include "gconf-listeners.h"
#include "gconf-locale.h"
#include <orb/orbit.h>

#include "GConf.h"

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <syslog.h>
#include <time.h>
#include <liboaf/liboaf.h>


/* Quick hack so I can mark strings */
/* Please don't mark LOG_DEBUG syslog messages */

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

/* This makes hash table safer when debugging */
#ifndef GCONF_ENABLE_DEBUG
#define safe_g_hash_table_insert g_hash_table_insert
#else
static void
safe_g_hash_table_insert(GHashTable* ht, gpointer key, gpointer value)
{
  gpointer oldkey = NULL, oldval = NULL;

  if (g_hash_table_lookup_extended(ht, key, &oldkey, &oldval))
    {
      gconf_log(GCL_WARNING, "Hash key `%s' is already in the table!",
                (gchar*) key);
      return;
    }
  else
    {
      g_hash_table_insert(ht, key, value);
    }
}
#endif

/*
 * Declarations
 */

/* return TRUE if the exception was set, clear err if needed */
static gboolean gconf_set_exception(GConfError** err, CORBA_Environment* ev);

static void gconf_main(void);
static void gconf_main_quit(void);

static GConfLocaleList* locale_cache_lookup(const gchar* locale);
static void             locale_cache_expire(void);
static void             locale_cache_drop  (void);

/* fast_cleanup() nukes the info file,
   and is theoretically re-entrant.
*/
static void fast_cleanup(void);

typedef struct _GConfContext GConfContext;

struct _GConfContext {
  ConfigServer_Context context;
  GConfListeners* listeners;
  GConfSources* sources;
  gchar* saved_address; /* if sources and listeners are NULL, then this is a
                           "dormant" context removed from the cache
                           and has to be re-instated.
                        */
  GTime last_access;
  guint sync_idle;
  guint sync_timeout;
};

static GConfContext* context_new(GConfSources* sources);
static void          context_destroy(GConfContext* ctx);
static CORBA_unsigned_long context_add_listener(GConfContext* ctx,
                                                ConfigListener who,
                                                const gchar* where);
static void          context_remove_listener(GConfContext* ctx,
                                             CORBA_unsigned_long cnxn);

static GConfValue*   context_query_value(GConfContext* ctx,
                                         const gchar* key,
                                         const gchar** locales,
                                         gboolean use_schema_default,
                                         GConfError** err);

static void          context_set(GConfContext* ctx, const gchar* key,
                                 GConfValue* value, ConfigValue* cvalue,
                                 GConfError** err);
static void          context_unset(GConfContext* ctx, const gchar* key,
                                   const gchar* locale,
                                   GConfError** err);
static gboolean      context_dir_exists(GConfContext* ctx, const gchar* dir,
                                        GConfError** err);
static void          context_remove_dir(GConfContext* ctx, const gchar* dir,
                                        GConfError** err);
static GSList*       context_all_entries(GConfContext* ctx, const gchar* dir,
                                         const gchar** locales,
                                         GConfError** err);
static GSList*       context_all_dirs(GConfContext* ctx, const gchar* dir,
                                      GConfError** err);
static void          context_set_schema(GConfContext* ctx, const gchar* key,
                                        const gchar* schema_key,
                                        GConfError** err);
static void          context_sync(GConfContext* ctx,
                                  GConfError** err);
static void          context_hibernate(GConfContext* ctx);
static void          context_awaken(GConfContext* ctx, GConfError** err);

static void                 init_contexts(void);
static void                 shutdown_contexts(void);
static void                 set_default_context(GConfContext* ctx);
static ConfigServer_Context register_context(GConfContext* ctx);
static void                 unregister_context(ConfigServer_Context ctx);
static GConfContext*        lookup_context(ConfigServer_Context ctx, GConfError** err);
static ConfigServer_Context lookup_context_id_from_address(const gchar* address);
static void                 sleep_old_contexts(void);

/* 
 * CORBA goo
 */

static ConfigServer server = CORBA_OBJECT_NIL;

static ConfigServer_Context
gconfd_get_context(PortableServer_Servant servant, CORBA_char * address,
                   CORBA_Environment* ev);

static CORBA_unsigned_long 
gconfd_add_listener(PortableServer_Servant servant, ConfigServer_Context ctx,
                    CORBA_char * where, 
                    ConfigListener who, CORBA_Environment *ev);
static void 
gconfd_remove_listener(PortableServer_Servant servant,
                       ConfigServer_Context ctx,
                       CORBA_unsigned_long cnxn,
                       CORBA_Environment *ev);
static ConfigValue* 
gconfd_lookup(PortableServer_Servant servant, ConfigServer_Context ctx,
              CORBA_char * key, 
              CORBA_Environment *ev);

static ConfigValue* 
gconfd_lookup_with_locale(PortableServer_Servant servant, ConfigServer_Context ctx,
                          CORBA_char * key,
                          CORBA_char * locale,
                          CORBA_boolean use_schema_default,
                          CORBA_Environment *ev);

static void
gconfd_set(PortableServer_Servant servant, ConfigServer_Context ctx,
           CORBA_char * key, 
           ConfigValue* value, CORBA_Environment *ev);

static void 
gconfd_unset(PortableServer_Servant servant,
             ConfigServer_Context ctx,
             CORBA_char * key, 
             CORBA_Environment *ev);

static void 
gconfd_unset_with_locale(PortableServer_Servant servant,
                         ConfigServer_Context ctx,
                         CORBA_char * key,
                         CORBA_char * locale,
                         CORBA_Environment *ev);

static CORBA_boolean
gconfd_dir_exists(PortableServer_Servant servant,
                  ConfigServer_Context ctx,
                  CORBA_char * dir,
                  CORBA_Environment *ev);

static void 
gconfd_remove_dir(PortableServer_Servant servant,
                  ConfigServer_Context ctx,
                  CORBA_char * dir, 
                  CORBA_Environment *ev);

static void 
gconfd_all_entries (PortableServer_Servant servant,
                    ConfigServer_Context ctx,
                    CORBA_char * dir,
                    CORBA_char * locale,
                    ConfigServer_KeyList ** keys, 
                    ConfigServer_ValueList ** values, CORBA_Environment * ev);

static void 
gconfd_all_dirs (PortableServer_Servant servant,
                 ConfigServer_Context ctx,
                 CORBA_char * dir, 
                 ConfigServer_KeyList ** keys, CORBA_Environment * ev);

static void 
gconfd_set_schema (PortableServer_Servant servant,
                   ConfigServer_Context ctx,
                   CORBA_char * key,
                   CORBA_char* schema_key, CORBA_Environment * ev);

static void 
gconfd_sync(PortableServer_Servant servant,
            ConfigServer_Context ctx,
            CORBA_Environment *ev);

static CORBA_long
gconfd_ping(PortableServer_Servant servant, CORBA_Environment *ev);

static void
gconfd_shutdown(PortableServer_Servant servant, CORBA_Environment *ev);

static PortableServer_ServantBase__epv base_epv = {
  NULL,
  NULL,
  NULL
};

static POA_ConfigServer__epv server_epv = { 
  NULL,
  gconfd_get_context,
  gconfd_add_listener, 
  gconfd_remove_listener, 
  gconfd_lookup,
  gconfd_lookup_with_locale, 
  gconfd_set,
  gconfd_unset,
  gconfd_unset_with_locale,
  gconfd_dir_exists,
  gconfd_remove_dir,
  gconfd_all_entries,
  gconfd_all_dirs,
  gconfd_set_schema,
  gconfd_sync,
  gconfd_ping,
  gconfd_shutdown
};

static POA_ConfigServer__vepv poa_server_vepv = { &base_epv, &server_epv };
static POA_ConfigServer poa_server_servant = { NULL, &poa_server_vepv };

static ConfigServer_Context
gconfd_get_context(PortableServer_Servant servant, CORBA_char * address,
                   CORBA_Environment* ev)
{
  ConfigServer_Context ctx;
  GConfSources* sources;
  gchar* addresses[] = { address, NULL };
  GConfError* error = NULL;
  
  ctx = lookup_context_id_from_address(address);

  if (ctx != ConfigServer_invalid_context)
    return ctx;

  sources = gconf_sources_new_from_addresses(addresses, &error);

  if (gconf_set_exception(&error, ev))
    {
      g_return_val_if_fail(sources == NULL, ConfigServer_invalid_context);
      return ConfigServer_invalid_context;
    }
  
  if (sources == NULL)
    return ConfigServer_invalid_context;

  ctx = register_context(context_new(sources));
  
  return ctx;
}

static CORBA_unsigned_long
gconfd_add_listener(PortableServer_Servant servant,
                    ConfigServer_Context ctx,
                    CORBA_char * where, 
                    const ConfigListener who, CORBA_Environment *ev)
{
  GConfContext* gcc;
  GConfError* error = NULL;
  
  gcc = lookup_context(ctx, &error);
  
  if (gcc != NULL)
    {
      g_return_val_if_fail(error == NULL, 0);
      return context_add_listener(gcc, who, where);
    }
  else
    {
      gconf_set_exception(&error, ev);
      return 0;
    }
}

static void 
gconfd_remove_listener(PortableServer_Servant servant,
                       ConfigServer_Context ctx,
                       CORBA_unsigned_long cnxn, CORBA_Environment *ev)
{
  GConfContext* gcc;
  GConfError* error = NULL;
  
  gcc = lookup_context(ctx, &error);  
  
  if (gcc != NULL)
    {
      g_return_if_fail(error == NULL);
      context_remove_listener(gcc, cnxn);
    }
  else
    {
      gconf_set_exception(&error, ev);
    }
}

static ConfigValue*
gconfd_lookup(PortableServer_Servant servant,
              ConfigServer_Context ctx,
              CORBA_char * key, 
              CORBA_Environment *ev)
{
  /* CORBA_char* normally can't be NULL but we cheat here */
  return gconfd_lookup_with_locale(servant, ctx, key, NULL, TRUE, ev);
}

static ConfigValue* 
gconfd_lookup_with_locale(PortableServer_Servant servant, ConfigServer_Context ctx,
                          CORBA_char * key,
                          CORBA_char * locale,
                          CORBA_boolean use_schema_default,
                          CORBA_Environment *ev)
{
  GConfValue* val;
  GConfContext* gcc;
  GConfError* error = NULL;
  GConfLocaleList* locale_list;
  
  gcc = lookup_context(ctx, &error);

  if (gconf_set_exception(&error, ev))
    return invalid_corba_value();

  locale_list = locale_cache_lookup(locale);
  
  val = context_query_value(gcc, key, locale_list->list,
                            use_schema_default, &error);

  gconf_locale_list_unref(locale_list);
  
  if (val != NULL)
    {
      ConfigValue* cval = corba_value_from_gconf_value(val);

      gconf_value_destroy(val);

      g_return_val_if_fail(error == NULL, cval);
      
      return cval;
    }
  else
    {
      gconf_set_exception(&error, ev);

      return invalid_corba_value();
    }
}

static void
gconfd_set(PortableServer_Servant servant,
           ConfigServer_Context ctx,
           CORBA_char * key, 
           ConfigValue* value, CORBA_Environment *ev)
{
  gchar* str;
  GConfValue* val;
  GConfContext* gcc;
  GConfError* error = NULL;
  
  if (value->_d == InvalidVal)
    {
      gconf_log(GCL_ERR, _("Received invalid value in set request"));
      return;
    }

  val = gconf_value_from_corba_value(value);

  if (val == NULL)
    {
      gconf_log(GCL_ERR, _("Couldn't make sense of CORBA value received in set request for key `%s'"), key);
      return;
    }
      
  str = gconf_value_to_string(val);

  gconf_log(GCL_DEBUG, "Received request to set key `%s' to `%s'", key, str);

  g_free(str);

  gcc = lookup_context(ctx, &error);

  if (gconf_set_exception(&error, ev))
    return;
  
  context_set(gcc, key, val, value, &error);

  gconf_set_exception(&error, ev);

  gconf_value_destroy(val);
}

static void 
gconfd_unset_with_locale(PortableServer_Servant servant,
                         ConfigServer_Context ctx,
                         CORBA_char * key,
                         CORBA_char * locale,
                         CORBA_Environment *ev)
{
  GConfContext* gcc;
  GConfError* error = NULL;
  
  gcc = lookup_context(ctx, &error);

  if (gconf_set_exception(&error, ev))
    return;
  
  context_unset(gcc, key, locale, &error);

  gconf_set_exception(&error, ev);
}

static void 
gconfd_unset(PortableServer_Servant servant,
             ConfigServer_Context ctx,
             CORBA_char * key, 
             CORBA_Environment *ev)
{
  /* This is a cheat, since CORBA_char* isn't normally NULL */
  gconfd_unset_with_locale(servant, ctx, key, NULL, ev);
}

static CORBA_boolean
gconfd_dir_exists(PortableServer_Servant servant,
                  ConfigServer_Context ctx,
                  CORBA_char *dir,
                  CORBA_Environment *ev)
{
  GConfContext *gcc;
  CORBA_boolean retval;
  GConfError* error = NULL;
  
  gcc = lookup_context(ctx, &error);

  if (gconf_set_exception(&error, ev))
    return CORBA_FALSE;
  
  retval = context_dir_exists(gcc, dir, &error) ? CORBA_TRUE : CORBA_FALSE;

  gconf_set_exception(&error, ev);

  return retval;
}


static void 
gconfd_remove_dir(PortableServer_Servant servant,
                  ConfigServer_Context ctx,
                  CORBA_char * dir, 
                  CORBA_Environment *ev)
{  
  GConfContext* gcc;
  GConfError* error = NULL;
  
  gcc = lookup_context(ctx, &error);

  if (gconf_set_exception(&error, ev))
    return;

  g_assert(gcc != NULL);
  
  context_remove_dir(gcc, dir, &error);

  gconf_set_exception(&error, ev);
}

static void 
gconfd_all_entries (PortableServer_Servant servant,
                    ConfigServer_Context ctx,
                    CORBA_char * dir,
                    CORBA_char * locale,
                    ConfigServer_KeyList ** keys, 
                    ConfigServer_ValueList ** values,
                    CORBA_Environment * ev)
{
  GSList* pairs;
  guint n;
  GSList* tmp;
  guint i;
  GConfContext* gcc;
  GConfError* error = NULL;
  GConfLocaleList* locale_list;
  
  gcc = lookup_context(ctx, &error);

  if (gconf_set_exception(&error, ev))
    return;

  locale_list = locale_cache_lookup(locale);
  
  if (gcc != NULL)
    pairs = context_all_entries(gcc, dir, locale_list->list, &error);
  else
    pairs = NULL;

  gconf_locale_list_unref(locale_list);
  
  n = g_slist_length(pairs);

  *keys= ConfigServer_KeyList__alloc();
  (*keys)->_buffer = CORBA_sequence_CORBA_string_allocbuf(n);
  (*keys)->_length = n;
  (*keys)->_maximum = n;

  *values= ConfigServer_ValueList__alloc();
  (*values)->_buffer = CORBA_sequence_ConfigValue_allocbuf(n);
  (*values)->_length = n;
  (*values)->_maximum = n;

  tmp = pairs;
  i = 0;

  while (tmp != NULL)
    {
      GConfEntry* p = tmp->data;

      g_assert(p != NULL);
      g_assert(p->key != NULL);
      g_assert(p->value != NULL);

      (*keys)->_buffer[i] = CORBA_string_dup(p->key);
      fill_corba_value_from_gconf_value(p->value, &((*values)->_buffer[i]));

      gconf_entry_destroy(p);

      ++i;
      tmp = g_slist_next(tmp);
    }
  
  g_assert(i == n);

  g_slist_free(pairs);

  gconf_set_exception(&error, ev);
}

static void 
gconfd_all_dirs (PortableServer_Servant servant,
                 ConfigServer_Context ctx,
                 CORBA_char * dir, 
                 ConfigServer_KeyList ** keys, CORBA_Environment * ev)
{
  GSList* subdirs;
  guint n;
  GSList* tmp;
  guint i;
  GConfContext* gcc;
  GConfError* error = NULL;
  
  gcc = lookup_context(ctx, &error);

  if (gconf_set_exception(&error, ev))
    return;
  
  if (gcc != NULL)
    subdirs = context_all_dirs(gcc, dir, &error);
  else
    subdirs = NULL;
  
  n = g_slist_length(subdirs);

  *keys= ConfigServer_KeyList__alloc();
  (*keys)->_buffer = CORBA_sequence_CORBA_string_allocbuf(n);
  (*keys)->_length = n;
  (*keys)->_maximum = n;

  tmp = subdirs;
  i = 0;

  while (tmp != NULL)
    {
      gchar* subdir = tmp->data;

      (*keys)->_buffer[i] = CORBA_string_dup(subdir);

      g_free(subdir);

      ++i;
      tmp = g_slist_next(tmp);
    }
  
  g_assert(i == n);

  g_slist_free(subdirs);

  gconf_set_exception(&error, ev);
}

static void 
gconfd_set_schema (PortableServer_Servant servant,
                   ConfigServer_Context ctx,
                   CORBA_char * key,
                   CORBA_char* schema_key, CORBA_Environment * ev)
{
  GConfContext* gcc;
  GConfError* error = NULL;
  
  gcc = lookup_context(ctx, &error);

  if (gconf_set_exception(&error, ev))
    return;
  
  context_set_schema(gcc, key, schema_key, &error);

  gconf_set_exception(&error, ev);
}

static void 
gconfd_sync(PortableServer_Servant servant,
            ConfigServer_Context ctx,
            CORBA_Environment *ev)
{
  GConfContext* gcc;
  GConfError* error = NULL;
  
  gcc = lookup_context(ctx, &error);

  if (gconf_set_exception(&error, ev))
    return;
  
  context_sync(gcc, &error);

  gconf_set_exception(&error, ev);
}

static CORBA_long
gconfd_ping(PortableServer_Servant servant, CORBA_Environment *ev)
{
  return getpid();
}

static void
gconfd_shutdown(PortableServer_Servant servant, CORBA_Environment *ev)
{
  gconf_log(GCL_INFO, _("Shutdown request received"));

  gconf_main_quit();
}

/*
 * Main code
 */

/* This needs to be called before we register with OAF
 */
static void
gconf_server_load_sources(void)
{
  gchar** addresses;
  GList* tmp;
  gboolean have_writeable = FALSE;
  gchar* conffile;
  GConfSources* sources = NULL;
  GConfError* error = NULL;
  
  conffile = g_strconcat(GCONF_SYSCONFDIR, "/gconf/path", NULL);

  addresses = gconf_load_source_path(conffile, NULL);

  g_free(conffile);

#ifdef GCONF_ENABLE_DEBUG
  /* -- Debug only */
  
  if (addresses == NULL)
    {
      gconf_log(GCL_DEBUG, _("gconfd compiled with debugging; trying to load gconf.path from the source directory"));
      conffile = g_strconcat(GCONF_SRCDIR, "/gconf/gconf.path", NULL);
      addresses = gconf_load_source_path(conffile, NULL);
      g_free(conffile);
    }

  /* -- End of Debug Only */
#endif

  if (addresses == NULL)
    {      
      /* Try using the default address xml:$(HOME)/.gconf */
      addresses = g_new0(gchar*, 2);

      addresses[0] = g_strconcat("xml:", g_get_home_dir(), "/.gconf", NULL);

      addresses[1] = NULL;
      
      gconf_log(GCL_INFO, _("No configuration files found, trying to use the default config source `%s'"), addresses[0]);
    }
  
  if (addresses == NULL)
    {
      /* We want to stay alive but do nothing, because otherwise every
         request would result in another failed gconfd being spawned.  
      */
      gchar* empty_addr[] = { NULL };
      gconf_log(GCL_ERR, _("No configuration sources in the source path, configuration won't be saved; edit "GCONF_SYSCONFDIR"/gconf/path"));
      /* don't request error since there aren't any addresses */
      sources = gconf_sources_new_from_addresses(empty_addr, NULL);

      /* Install the sources as the default context */
      set_default_context(context_new(sources));
    }
  else
    {
      sources = gconf_sources_new_from_addresses(addresses, &error);

      if (error != NULL)
        {
          gconf_log(GCL_ERR, _("Error loading some config sources: %s"),
                    error->str);

          gconf_error_destroy(error);
          error = NULL;
        }
      
      g_free(addresses);

      g_assert(sources != NULL);

      if (sources->sources == NULL)
        gconf_log(GCL_ERR, _("No config source addresses successfully resolved, can't load or store config data"));
    
      tmp = sources->sources;

      while (tmp != NULL)
        {
          if (((GConfSource*)tmp->data)->flags & GCONF_SOURCE_ALL_WRITEABLE)
            {
              have_writeable = TRUE;
              break;
            }

          tmp = g_list_next(tmp);
        }

      /* In this case, some sources may still return TRUE from their writeable() function */
      if (!have_writeable)
        gconf_log(GCL_WARNING, _("No writeable config sources successfully resolved, may not be able to save some configuration changes"));

        
      /* Install the sources as the default context */
      set_default_context(context_new(sources));
    }
}

static void
signal_handler (int signo)
{
  static gint in_fatal = 0;

  /* avoid loops */
  if (in_fatal > 1)
    return;
  
  ++in_fatal;
  
  gconf_log(GCL_ERR, _("Received signal %d\nshutting down."), signo);
  
  fast_cleanup();

  switch(signo) {
  case SIGSEGV:
    abort();
    break;
    
  default:
    exit (1);
  }
}

int 
main(int argc, char** argv)
{
  struct sigaction act;
  sigset_t empty_mask;
  PortableServer_ObjectId objid = {0, sizeof("ConfigServer"), "ConfigServer"};
  PortableServer_POA poa;
  
  CORBA_Environment ev;
  CORBA_ORB orb;
  gchar* logname;
  const gchar* username;
  guint len;
  GConfError* err = NULL;
  
  chdir ("/");

  umask(0);

  gconf_set_daemon_mode(TRUE);
  
  /* Logs */
  username = g_get_user_name();
  len = strlen(username) + strlen("gconfd") + 5;
  logname = g_malloc(len);
  g_snprintf(logname, len, "gconfd (%s)", username);

  openlog (logname, LOG_NDELAY, LOG_USER);
  /* openlog() does not copy logname - what total brokenness.
     So we free it at the end of main() */
  
  gconf_log(GCL_INFO, _("starting, pid %u user `%s'"), 
            (guint)getpid(), g_get_user_name());
  
  /* Session setup */
  sigemptyset (&empty_mask);
  act.sa_handler = signal_handler;
  act.sa_mask    = empty_mask;
  act.sa_flags   = 0;
  sigaction (SIGTERM,  &act, 0);
  sigaction (SIGINT,  &act, 0);
  sigaction (SIGHUP,  &act, 0);
  sigaction (SIGSEGV, &act, 0);
  sigaction (SIGABRT, &act, 0);

  act.sa_handler = SIG_IGN;
  sigaction (SIGINT, &act, 0);

  CORBA_exception_init(&ev);

  if (!oaf_init(argc, argv))
    {
      gconf_log(GCL_ERR, _("Failed to init Object Activation Framework: please mail bug report to OAF maintainers"));
      exit(1);
    }

  init_contexts();

  orb = oaf_orb_get();

  POA_ConfigServer__init(&poa_server_servant, &ev);
  
  poa = (PortableServer_POA)CORBA_ORB_resolve_initial_references(orb, "RootPOA", &ev);
  PortableServer_POAManager_activate(PortableServer_POA__get_the_POAManager(poa, &ev), &ev);
  PortableServer_POA_activate_object_with_id(poa,
                                             &objid, &poa_server_servant, &ev);
  
  server = PortableServer_POA_servant_to_reference(poa,
                                                   &poa_server_servant,
                                                   &ev);
  if (CORBA_Object_is_nil(server, &ev)) 
    {
      gconf_log(GCL_ERR, _("Failed to get object reference for ConfigServer"));
      return 1;
    }

  /* Needs to be done right before registration */
  gconf_server_load_sources();
  
  oaf_active_server_register(IID, server);

  gconf_main();

  fast_cleanup();

  shutdown_contexts();

  locale_cache_drop();
  
  gconf_log(GCL_INFO, _("Exiting normally"));
  
  g_free(logname);
  
  return 0;
}

/*
 * Main loop
 */

static GSList* main_loops = NULL;
static guint timeout_id = 0;

static gboolean
one_hour_timeout(gpointer data)
{
  /* shrink unused context objects */
  sleep_old_contexts();

  /* expire old locale cache entries */
  locale_cache_expire();
  
  return TRUE;
}

static void
gconf_main(void)
{
  GMainLoop* loop;

  loop = g_main_new(TRUE);

  if (main_loops == NULL)
    {
      g_assert(timeout_id == 0);
      timeout_id = g_timeout_add(1000*60*60, /* 1 sec * 60 s/min * 60 min */
                                 one_hour_timeout,
                                 NULL);

    }
  
  main_loops = g_slist_prepend(main_loops, loop);

  g_main_run(loop);

  main_loops = g_slist_remove(main_loops, loop);

  if (main_loops == NULL)
    {
      g_assert(timeout_id != 0);
      g_source_remove(timeout_id);
      timeout_id = 0;
    }
  
  g_main_destroy(loop);
}

static void 
gconf_main_quit(void)
{
  g_return_if_fail(main_loops != NULL);

  g_main_quit(main_loops->data);
}

/*
 * Listeners
 */

typedef struct _Listener Listener;

struct _Listener {
  ConfigListener obj;
};

static Listener* listener_new(ConfigListener obj);
static void      listener_destroy(Listener* l);

/*
 * Contexts
 */

static void
context_really_sync(GConfContext* ctx)
{
  GConfError* error = NULL;
  
  if (!gconf_sources_sync_all(ctx->sources, &error))
    {
      g_return_if_fail(error != NULL);

      gconf_log(GCL_ERR, _("Failed to sync one or more sources: %s"), 
                error->str);
      gconf_error_destroy(error);
    }
  else
    {
      gconf_log(GCL_DEBUG, "Sync completed without errors");
    }
}

static GConfContext*
context_new(GConfSources* sources)
{
  GConfContext* ctx;

  ctx = g_new0(GConfContext, 1);

  ctx->context = ConfigServer_invalid_context;
  
  ctx->listeners = gconf_listeners_new();

  ctx->sources = sources;

  ctx->last_access = time(NULL);

  ctx->sync_idle = 0;
  ctx->sync_timeout = 0;
  
  return ctx;
}

static void
context_destroy(GConfContext* ctx)
{
  if (ctx->listeners != NULL)
    {
      gboolean need_sync = FALSE;
      
      g_assert(ctx->sources != NULL);
      g_assert(ctx->saved_address == NULL);

      if (ctx->sync_idle != 0)
        {
          g_source_remove(ctx->sync_idle);
          ctx->sync_idle = 0;
          need_sync = TRUE;
        }

      if (ctx->sync_timeout != 0)
        {
          g_source_remove(ctx->sync_timeout);
          ctx->sync_timeout = 0;
          need_sync = TRUE;
        }

      if (need_sync)
        context_really_sync(ctx);
      
      gconf_listeners_destroy(ctx->listeners);
      gconf_sources_destroy(ctx->sources);
    }
  else
    {
      g_assert(ctx->saved_address != NULL);
      g_assert(ctx->sync_idle == 0);
      g_assert(ctx->sync_timeout == 0);
      
      g_free(ctx->saved_address);
    }
      
  g_free(ctx);
}

static void
context_hibernate(GConfContext* ctx)
{
  g_return_if_fail(ctx->listeners != NULL);
  g_return_if_fail(gconf_listeners_count(ctx->listeners) == 0);
  g_return_if_fail(ctx->sync_idle == 0);
  g_return_if_fail(ctx->sync_timeout == 0);
      
  gconf_listeners_destroy(ctx->listeners);
  ctx->listeners = NULL;

  ctx->saved_address = g_strdup(((GConfSource*)ctx->sources->sources->data)->address);
  
  gconf_sources_destroy(ctx->sources);
  ctx->sources = NULL;  
}

static void
context_awaken(GConfContext* ctx, GConfError** err)
{
  gchar* addresses[2];
  
  g_return_if_fail(ctx->listeners == NULL);
  g_return_if_fail(ctx->sources == NULL);
  g_return_if_fail(ctx->saved_address != NULL);

  addresses[0] = ctx->saved_address;
  addresses[1] = NULL;

  ctx->sources = gconf_sources_new_from_addresses(addresses, err);
  
  ctx->listeners = gconf_listeners_new();

  g_free(ctx->saved_address);

  ctx->saved_address = NULL;
}


static gint
context_sync_idle(GConfContext* ctx)
{
  ctx->sync_idle = 0;

  /* could have been added before reaching the
     idle */
  if (ctx->sync_timeout != 0)
    {
      g_source_remove(ctx->sync_timeout);
      ctx->sync_timeout = 0;
    }
  
  context_really_sync(ctx);
  
  /* Remove the idle function by returning FALSE */
  return FALSE; 
}

static gint
context_sync_timeout(GConfContext* ctx)
{
  ctx->sync_timeout = 0;
  
  /* Install the sync idle */
  if (ctx->sync_idle == 0)
    ctx->sync_idle = g_idle_add((GSourceFunc)context_sync_idle, ctx);

  gconf_log(GCL_DEBUG, "Sync queued one minute after changes occurred");
  
  /* Remove the timeout function by returning FALSE */
  return FALSE;
}

static void
context_sync_nowish(GConfContext* ctx)
{
  /* Go ahead and sync as soon as the event loop quiets down */

  /* remove the scheduled sync */
  if (ctx->sync_timeout != 0)
    {
      g_source_remove(ctx->sync_timeout);
      ctx->sync_timeout = 0;
    }

  /* Schedule immediate post-quietdown sync */
  if (ctx->sync_idle == 0)
    ctx->sync_idle = g_idle_add((GSourceFunc)context_sync_idle, ctx);
}

static void
context_schedule_sync(GConfContext* ctx)
{
  /* Plan to sync within a minute or so */
  if (ctx->sync_idle != 0)
    return;
  else if (ctx->sync_timeout != 0)
    return;
  else
    {
      /* 1 minute timeout */
      ctx->sync_timeout = g_timeout_add(60000, (GSourceFunc)context_sync_timeout, ctx);
    }
}


static CORBA_unsigned_long
context_add_listener(GConfContext* ctx,
                     ConfigListener who,
                     const gchar* where)
{
  Listener* l;
  guint cnxn;
  
  g_assert(ctx->listeners != NULL);
  
  ctx->last_access = time(NULL);
  
  l = listener_new(who);

  cnxn = gconf_listeners_add(ctx->listeners, where, l,
                             (GFreeFunc)listener_destroy);

  gconf_log(GCL_DEBUG, "Added listener %u", cnxn);

  return cnxn;  
}

static void
context_remove_listener(GConfContext* ctx,
                        CORBA_unsigned_long cnxn)
{
  g_assert(ctx->listeners != NULL);
  
  ctx->last_access = time(NULL);
  gconf_log(GCL_DEBUG, "Removing listener %u", (guint)cnxn);
  /* calls destroy notify */
  gconf_listeners_remove(ctx->listeners, cnxn);
}

typedef struct _ListenerNotifyClosure ListenerNotifyClosure;

struct _ListenerNotifyClosure {
  ConfigServer_Context context;
  ConfigValue* value;
  GSList* dead;
  CORBA_Environment ev;
};

static void
notify_listeners_cb(GConfListeners* listeners,
                    const gchar* all_above_key,
                    guint cnxn_id,
                    gpointer listener_data,
                    gpointer user_data)
{
  Listener* l = listener_data;
  ListenerNotifyClosure* closure = user_data;

  g_return_if_fail(closure->context != ConfigServer_invalid_context);
  
  ConfigListener_notify(l->obj, closure->context, cnxn_id, 
                        (gchar*)all_above_key, closure->value,
                        &closure->ev);
  
  if(closure->ev._major != CORBA_NO_EXCEPTION) 
    {
      gconf_log(GCL_WARNING, "Failed to notify listener %u, removing: %s", 
                cnxn_id, CORBA_exception_id(&closure->ev));
      CORBA_exception_free(&closure->ev);
      
      /* Dead listeners need to be forgotten */
      closure->dead = g_slist_prepend(closure->dead, GUINT_TO_POINTER(cnxn_id));
    }
  else
    {
      gconf_log(GCL_DEBUG, "Notified listener %u of change to key `%s'",
                cnxn_id, all_above_key);
    }
}

static void
context_notify_listeners(GConfContext* ctx,
                         const gchar* key, ConfigValue* value)
{
  ListenerNotifyClosure closure;
  GSList* tmp;

  g_return_if_fail(ctx != NULL);
  g_return_if_fail(ctx->context != ConfigServer_invalid_context);
  
  closure.value = value;
  closure.dead = NULL;
  closure.context = ctx->context;
  
  CORBA_exception_init(&closure.ev);
  
  gconf_listeners_notify(ctx->listeners, key, notify_listeners_cb, &closure);

  tmp = closure.dead;

  while (tmp != NULL)
    {
      guint dead = GPOINTER_TO_UINT(tmp->data);

      gconf_listeners_remove(ctx->listeners, dead);

      tmp = g_slist_next(tmp);
    }
}


static GConfValue*
context_query_value(GConfContext* ctx,
                    const gchar* key,
                    const gchar** locales,
                    gboolean use_schema_default,
                    GConfError** err)
{
  GConfValue* val;

  g_return_val_if_fail(err == NULL || *err == NULL, NULL);
  g_assert(ctx->listeners != NULL);
  
  ctx->last_access = time(NULL);
  
  val = gconf_sources_query_value(ctx->sources, key, locales,
                                  use_schema_default, err);

  if (err && *err != NULL)
    {
      gconf_log(GCL_ERR, _("Error getting value for `%s': %s"),
                key, (*err)->str);
    }
  
  return val;
}

static void
context_set(GConfContext* ctx,
            const gchar* key,
            GConfValue* val,
            ConfigValue* value,
            GConfError** err)
{
  g_assert(ctx->listeners != NULL);
  g_return_if_fail(err == NULL || *err == NULL);
  
  ctx->last_access = time(NULL);

  gconf_sources_set_value(ctx->sources, key, val, err);

  if (err && *err != NULL)
    {
      gconf_log(GCL_ERR, _("Error setting value for `%s': %s"),
                 key, (*err)->str);
    }
  else
    {
      context_schedule_sync(ctx);
      context_notify_listeners(ctx, key, value);
    }
}

static void
context_unset(GConfContext* ctx,
              const gchar* key,
              const gchar* locale,
              GConfError** err)
{
  ConfigValue* val;
  g_return_if_fail(err == NULL || *err == NULL);
  
  g_assert(ctx->listeners != NULL);
  
  ctx->last_access = time(NULL);
  
  gconf_log(GCL_DEBUG, "Received request to unset key `%s'", key);

  gconf_sources_unset_value(ctx->sources, key, locale, err);

  if (err && *err != NULL)
    {
      gconf_log(GCL_ERR, _("Error unsetting `%s': %s"),
                 key, (*err)->str);
    }
  else
    {
      val = invalid_corba_value();

      context_schedule_sync(ctx);
      context_notify_listeners(ctx, key, val);
      
      CORBA_free(val);
    }
}

static gboolean
context_dir_exists(GConfContext* ctx,
                   const gchar* dir,
                   GConfError** err)
{
  gboolean ret;
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  
  g_assert(ctx->listeners != NULL);
  
  ctx->last_access = time(NULL);
  
  gconf_log(GCL_DEBUG, "Received dir_exists request for `%s'", dir);
  
  ret = gconf_sources_dir_exists(ctx->sources, dir, err);
  
  if (err && *err != NULL)
    {
      gconf_log(GCL_ERR, _("Error checking existence of `%s': %s"),
                 dir, (*err)->str);
      ret = FALSE;
    }

  return ret;
}

static void
context_remove_dir(GConfContext* ctx,
                   const gchar* dir,
                   GConfError** err)
{
  g_return_if_fail(err == NULL || *err == NULL);
  g_assert(ctx->listeners != NULL);
  
  ctx->last_access = time(NULL);
  
  gconf_log(GCL_DEBUG, "Received request to remove dir `%s'", dir);
  
  gconf_sources_remove_dir(ctx->sources, dir, err);

  if (err && *err != NULL)
    {
      gconf_log(GCL_ERR, _("Error removing dir `%s': %s"),
                 dir, (*err)->str);
    }
  else
    {
      context_schedule_sync(ctx);
    }
}

static GSList*
context_all_entries(GConfContext* ctx, const gchar* dir,
                    const gchar** locales,
                    GConfError** err)
{
  GSList* entries;
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);
  
  g_assert(ctx->listeners != NULL);
  
  ctx->last_access = time(NULL);
  
  entries = gconf_sources_all_entries(ctx->sources, dir, locales, err);

  if (err && *err != NULL)
    {
      gconf_log(GCL_ERR, _("Failed to get all entries in `%s': %s"),
                 dir, (*err)->str);
    }

  return entries;
}

static GSList*
context_all_dirs(GConfContext* ctx, const gchar* dir, GConfError** err)
{
  GSList* subdirs;

  g_return_val_if_fail(err == NULL || *err == NULL, NULL);
  
  g_assert(ctx->listeners != NULL);
  
  ctx->last_access = time(NULL);
    
  gconf_log(GCL_DEBUG, "Received request to list all subdirs in `%s'", dir);

  subdirs = gconf_sources_all_dirs(ctx->sources, dir, err);

  if (err && *err != NULL)
    {
      gconf_log(GCL_ERR, _("Error listing dirs in `%s': %s"),
                 dir, (*err)->str);
    }
  return subdirs;
}

static void
context_set_schema(GConfContext* ctx, const gchar* key,
                   const gchar* schema_key,
                   GConfError** err)
{
  g_return_if_fail(err == NULL || *err == NULL);
  g_assert(ctx->listeners != NULL);
  
  ctx->last_access = time(NULL);
  
  gconf_sources_set_schema(ctx->sources, key, schema_key, err);

  if (err && *err != NULL)
    {
      gconf_log(GCL_ERR, _("Error setting schema for `%s': %s"),
                key, (*err)->str);
    }
  else
    {
      context_schedule_sync(ctx);
    }
}

static void
context_sync(GConfContext* ctx, GConfError** err)
{
  g_assert(ctx->listeners != NULL);
  
  ctx->last_access = time(NULL);
  
  gconf_log(GCL_DEBUG, "Received suggestion to sync all config data");

  context_sync_nowish(ctx);
}

/*
 * Context storage
 */

static GPtrArray* context_list = NULL;
static GHashTable* contexts_by_address = NULL;

static void
init_contexts(void)
{
  g_assert(context_list == NULL);
  g_assert(contexts_by_address == NULL);
  
  contexts_by_address = g_hash_table_new(g_str_hash, g_str_equal);

  context_list = g_ptr_array_new();

  g_ptr_array_add(context_list, NULL); /* Invalid context at index 0 */
  g_ptr_array_add(context_list, NULL); /* default at 1 */

  /* Default context isn't in the address hash since it has
     multiple addresses in a stack
  */
}

static void
set_default_context(GConfContext* ctx)
{
  g_assert(context_list != NULL);
  g_assert(contexts_by_address != NULL);

  g_assert(context_list->pdata[ConfigServer_default_context] == NULL);

  g_return_if_fail(ctx != NULL);
  g_return_if_fail(ctx->context == ConfigServer_invalid_context);
  
  context_list->pdata[ConfigServer_default_context] = ctx;

  ctx->context = ConfigServer_default_context;
  
  /* Default context isn't in the address hash since it has
     multiple addresses in a stack
  */
}

static void
sleep_old_contexts(void)
{
  guint i;
  GTime now;
  
  g_assert(context_list != NULL);
  
  now = time(NULL);
  
  i = 2; /* don't include the default context or invalid context */
  while (i < context_list->len)
    {
      GConfContext* c = context_list->pdata[i];

      if (c->listeners &&                             /* not already hibernating */
          gconf_listeners_count(c->listeners) == 0 && /* Can hibernate */
          (now - c->last_access) > (60*45))   /* 45 minutes without access */
        {
          context_hibernate(c);
        }
      
      ++i;
    }
}

static void
shutdown_contexts(void)
{
  guint i;
  
  g_assert(context_list != NULL);
  g_assert(contexts_by_address != NULL);
  
  i = context_list->len - 1;

  while (i > 0)
    {
      if (context_list->pdata[i] != NULL)
        {
          context_destroy(context_list->pdata[i]);

          context_list->pdata[i] = NULL;
        }
      
      --i;
    }
  
  g_ptr_array_free(context_list, TRUE);
  context_list = NULL;

  g_hash_table_destroy(contexts_by_address);

  contexts_by_address = NULL;  
}

static ConfigServer_Context
register_context(GConfContext* ctx)
{
  ConfigServer_Context next_id;
  
  g_assert(context_list != NULL);
  g_assert(contexts_by_address != NULL);
  g_return_val_if_fail(ctx->sources != NULL, ConfigServer_invalid_context);
  g_return_val_if_fail(ctx->sources->sources != NULL, ConfigServer_invalid_context);
  
  next_id = context_list->len;

  g_ptr_array_add(context_list, ctx);
  
  safe_g_hash_table_insert(contexts_by_address,
                           ((GConfSource*)ctx->sources->sources->data)->address,
                           GUINT_TO_POINTER(next_id));

  ctx->context = next_id;
  
  return next_id;
}

static void
unregister_context(ConfigServer_Context ctx)
{
  GConfContext* context;
  
  g_assert(context_list != NULL);
  g_assert(contexts_by_address != NULL);

  if (ctx == ConfigServer_invalid_context)
    {
      gconf_log(GCL_ERR, _("Attempt to unregister invalid context ID"));

      return;
    }
  
  if (ctx >= context_list->len)
    {
      gconf_log(GCL_ERR, _("Bad context ID %lu, request ignored"), (gulong)ctx);
      return;
    }

  context = context_list->pdata[ctx];

  if (context == NULL)
    {
      gconf_log(GCL_ERR, _("Already-unregistered context ID %lu, request ignored"),
             (gulong)ctx);

      return;
    }
  
  context_list->pdata[ctx] = NULL;

  g_hash_table_remove(contexts_by_address,
                      ((GConfSource*)(context->sources->sources->data))->address);
  
  context_destroy(context);
}

static GConfContext*
lookup_context(ConfigServer_Context ctx, GConfError** err)
{
  GConfContext* gcc;
  
  g_assert(context_list != NULL);
  g_assert(contexts_by_address != NULL);

  if (ctx >= context_list->len || ctx == ConfigServer_invalid_context)
    {
      gconf_set_error(err, GCONF_FAILED,
                      _("Attempt to use invalid context ID %lu"),
                      (gulong)ctx);
      return NULL;
    }
  
  gcc = context_list->pdata[ctx];

  if (gcc == NULL)
    {
      gconf_set_error(err, GCONF_FAILED,
                      _("Attempt to use already-unregistered context ID %lu"),
                      (gulong)ctx);
      return NULL;
    }

  if (gcc->listeners == NULL)
    {
      context_awaken(gcc, err);       /* Wake up hibernating contexts */
      if (gcc->listeners == NULL)
        {
          /* Failed, error should now be set. */
          g_return_val_if_fail(err == NULL || *err != NULL, NULL);
          return NULL;
        }
    }
  
  return gcc;
}

static ConfigServer_Context
lookup_context_id_from_address(const gchar* address)
{
  gpointer result;
  
  g_assert(context_list != NULL);
  g_assert(contexts_by_address != NULL);

  result = g_hash_table_lookup(contexts_by_address,
                               address);

  if (result != NULL)
    {
      return GPOINTER_TO_UINT(result);
    }
  else
    {
      return ConfigServer_invalid_context;
    }
}

/*
 * The listener object
 */

static Listener* 
listener_new(ConfigListener obj)
{
  Listener* l;
  CORBA_Environment ev;

  CORBA_exception_init(&ev);

  l = g_new0(Listener, 1);

  l->obj = CORBA_Object_duplicate(obj, &ev);

  return l;
}

static void      
listener_destroy(Listener* l)

{  
  CORBA_Environment ev;

  CORBA_exception_init(&ev);
  CORBA_Object_release(l->obj, &ev);
  g_free(l);
}

/*
 * Cleanup
 */

/* fast_cleanup() does the important parts, and is theoretically
   re-entrant. I don't think it is anymore with OAF, so we should fix
   that.
*/
static void 
fast_cleanup(void)
{
  /* The goal of this function is to remove the 
     stale server registration. 
  */
  if (server != CORBA_OBJECT_NIL)
    oaf_active_server_unregister("", server);
}


/* Exceptions */

static gboolean
gconf_set_exception(GConfError** error, CORBA_Environment* ev)
{
  GConfErrNo en;

  if (error == NULL)
    return FALSE;

  if (*error == NULL)
    return FALSE;
  
  en = (*error)->num;

  if (en == GCONF_SUCCESS)
    return FALSE;
  else
    {
      ConfigException* ce;

      ce = ConfigException__alloc();
      g_assert(error != NULL);
      g_assert(*error != NULL);
      g_assert((*error)->str != NULL);
      ce->message = CORBA_string_dup((gchar*)(*error)->str); /* cast const */
      
      switch (en)
        {
        case GCONF_FAILED:
          ce->err_no = ConfigFailed;
          break;
        case GCONF_NO_PERMISSION:
          ce->err_no = ConfigNoPermission;
          break;
        case GCONF_BAD_ADDRESS:
          ce->err_no = ConfigBadAddress;
          break;
        case GCONF_BAD_KEY:
          ce->err_no = ConfigBadKey;
          break;
        case GCONF_PARSE_ERROR:
          ce->err_no = ConfigParseError;
          break;
        case GCONF_CORRUPT:
          ce->err_no = ConfigCorrupt;
          break;
        case GCONF_TYPE_MISMATCH:
          ce->err_no = ConfigTypeMismatch;
          break;
        case GCONF_IS_DIR:
          ce->err_no = ConfigIsDir;
          break;
        case GCONF_IS_KEY:
          ce->err_no = ConfigIsKey;
          break;
        case GCONF_NO_SERVER:
        case GCONF_SUCCESS:
        default:
          g_assert_not_reached();
        }

      CORBA_exception_set(ev, CORBA_USER_EXCEPTION,
                          ex_ConfigException, ce);

      gconf_log(GCL_ERR, _("Returning exception: %s"), (*error)->str);
      
      gconf_error_destroy(*error);
      *error = NULL;
      
      return TRUE;
    }
}

/*
 * Locale hash
 */

static GConfLocaleCache* locale_cache = NULL;

static GConfLocaleList*
locale_cache_lookup(const gchar* locale)
{
  GConfLocaleList* locale_list;
  
  if (locale_cache == NULL)
    locale_cache = gconf_locale_cache_new();

  locale_list = gconf_locale_cache_get_list(locale_cache, locale);

  g_assert(locale_list != NULL);
  g_assert(locale_list->list != NULL);
  
  return locale_list;
}

static void
locale_cache_expire(void)
{
  if (locale_cache != NULL)
    gconf_locale_cache_expire(locale_cache, 60 * 30); /* 60 sec * 30 min */
}

static void
locale_cache_drop(void)
{
  if (locale_cache != NULL)
    {
      gconf_locale_cache_destroy(locale_cache);
      locale_cache = NULL;
    }
}
