/* GConf
 * Copyright (C) 1999, 2000 Red Hat Inc.
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
#include "gconf-schema.h"
#include "gconf-glib-private.h"
#include "gconf.h"
#include "gconfd.h"
#include "gconf-database.h"
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

static void gconf_main(void);
static void gconf_main_quit(void);

static void logfile_save (void);
static void logfile_read (void);

/* fast_cleanup() nukes the info file,
   and is theoretically re-entrant.
*/
static void fast_cleanup(void);

static void                 init_databases (void);
static void                 shutdown_databases (void);
static void                 set_default_database (GConfDatabase* db);
static void                 register_database (GConfDatabase* db);
static void                 unregister_database (GConfDatabase* db);
static GConfDatabase*       lookup_database (const gchar *address);
static GConfDatabase*       obtain_database (const gchar *address,
                                             GError **err);
static void                 drop_old_databases (void);

/* 
 * CORBA goo
 */

static ConfigServer server = CORBA_OBJECT_NIL;
static PortableServer_POA the_poa;

static ConfigDatabase
gconfd_get_default_database(PortableServer_Servant servant,
                            CORBA_Environment* ev);

static ConfigDatabase
gconfd_get_database(PortableServer_Servant servant,
                    const CORBA_char* address,
                    CORBA_Environment* ev);

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
  gconfd_get_default_database,
  gconfd_get_database,
  gconfd_ping,
  gconfd_shutdown
};

static POA_ConfigServer__vepv poa_server_vepv = { &base_epv, &server_epv };
static POA_ConfigServer poa_server_servant = { NULL, &poa_server_vepv };

static ConfigDatabase
gconfd_get_default_database(PortableServer_Servant servant,
                            CORBA_Environment* ev)
{
  GConfDatabase *db;

  db = lookup_database (NULL);

  if (db)
    return CORBA_Object_duplicate (db->objref, ev);
  else
    return CORBA_OBJECT_NIL;
}

static ConfigDatabase
gconfd_get_database(PortableServer_Servant servant,
                    const CORBA_char* address,
                    CORBA_Environment* ev)
{
  GConfDatabase *db;
  GError* error = NULL;  

  db = obtain_database (address, &error);

  if (db != NULL)
    return CORBA_Object_duplicate (db->objref, ev);
  else if (gconf_set_exception(&error, ev))
    return CORBA_OBJECT_NIL;
  else
    return CORBA_OBJECT_NIL;
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
  GError* error = NULL;
  
  conffile = g_strconcat(GCONF_CONFDIR, "/path", NULL);

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
      /* Try using the default address xml:readwrite:$(HOME)/.gconf */
      addresses = g_new0(gchar*, 2);

      addresses[0] = g_strconcat("xml:readwrite:", g_get_home_dir(), "/.gconf", NULL);

      addresses[1] = NULL;
      
      gconf_log(GCL_INFO, _("No configuration files found, trying to use the default config source `%s'"), addresses[0]);
    }
  
  if (addresses == NULL)
    {
      /* We want to stay alive but do nothing, because otherwise every
         request would result in another failed gconfd being spawned.  
      */
      const gchar* empty_addr[] = { NULL };
      gconf_log(GCL_ERR, _("No configuration sources in the source path, configuration won't be saved; edit "GCONF_CONFDIR"/path"));
      /* don't request error since there aren't any addresses */
      sources = gconf_sources_new_from_addresses(empty_addr, NULL);

      /* Install the sources as the default database */
      set_default_database (gconf_database_new(sources));
    }
  else
    {
      sources = gconf_sources_new_from_addresses((const gchar**)addresses,
                                                 &error);

      if (error != NULL)
        {
          gconf_log(GCL_ERR, _("Error loading some config sources: %s"),
                    error->message);

          g_error_free(error);
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

        
      /* Install the sources as the default database */
      set_default_database (gconf_database_new(sources));
    }
}

static void
signal_handler (int signo)
{
  static gint in_fatal = 0;

  /* avoid loops */
  if (in_fatal > 0)
    return;
  
  ++in_fatal;
  
  switch(signo) {
    /* Fast cleanup only */
  case SIGSEGV:
  case SIGBUS:
  case SIGILL:
    fast_cleanup();
    gconf_log(GCL_ERR, _("Received signal %d, shutting down."), signo);
    abort();
    break;

    /* maybe it's more feasible to clean up more mess */
  case SIGFPE:
  case SIGPIPE:
  case SIGTERM:
    /* Slow cleanup cases */
    fast_cleanup ();
    /* remove lockfiles, etc. */
    shutdown_databases ();
    gconf_log(GCL_ERR, _("Received signal %d, shutting down."), signo);
    exit (1);
    break;
    
  case SIGHUP:
    gconf_log(GCL_INFO, _("Received signal %d, ignoring"), signo);
    --in_fatal;
    break;
    
  default:
    break;
  }
}

PortableServer_POA
gconf_get_poa ()
{
  return the_poa;
}

int 
main(int argc, char** argv)
{
  struct sigaction act;
  sigset_t empty_mask;
  /*   PortableServer_ObjectId objid = {0, sizeof("ConfigServer"), "ConfigServer"}; */
  PortableServer_ObjectId* objid;
  CORBA_Environment ev;
  CORBA_ORB orb;
  gchar* logname;
  const gchar* username;
  guint len;
  gchar* ior;
  OAF_RegistrationResult result;
  
  chdir ("/");

  umask(0);

  gconf_set_daemon_mode(TRUE);
  
  /* Logs */
  username = g_get_user_name();
  len = strlen(username) + strlen("gconfd") + 15;
  logname = g_malloc(len);
  g_snprintf(logname, len, "gconfd (%s-%u)", username, (guint)getpid());

  openlog (logname, LOG_NDELAY, LOG_USER);
  /* openlog() does not copy logname - what total brokenness.
     So we free it at the end of main() */
  
  gconf_log (GCL_INFO, _("starting (version %s), pid %u user '%s'"), 
             VERSION, (guint)getpid(), g_get_user_name());

#ifdef GCONF_ENABLE_DEBUG
  gconf_log (GCL_DEBUG, _("GConf was built with debugging features enabled"));
#endif
  
  /* Session setup */
  sigemptyset (&empty_mask);
  act.sa_handler = signal_handler;
  act.sa_mask    = empty_mask;
  act.sa_flags   = 0;
  sigaction (SIGTERM,  &act, 0);
  sigaction (SIGILL,  &act, 0);
  sigaction (SIGBUS,  &act, 0);
  sigaction (SIGFPE,  &act, 0);
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

  init_databases ();

  orb = oaf_orb_get();

  POA_ConfigServer__init(&poa_server_servant, &ev);
  
  the_poa = (PortableServer_POA)CORBA_ORB_resolve_initial_references(orb, "RootPOA", &ev);
  PortableServer_POAManager_activate(PortableServer_POA__get_the_POAManager(the_poa, &ev), &ev);

  objid = PortableServer_POA_activate_object(the_poa, &poa_server_servant, &ev);
  
  server = PortableServer_POA_servant_to_reference(the_poa,
                                                   &poa_server_servant,
                                                   &ev);
  if (CORBA_Object_is_nil(server, &ev)) 
    {
      gconf_log(GCL_ERR, _("Failed to get object reference for ConfigServer"));
      return 1;
    }

  /* Needs to be done before loading sources */
  ior = CORBA_ORB_object_to_string(orb, server, &ev);
  gconf_set_daemon_ior(ior);
  CORBA_free(ior);
  
  /* Needs to be done right before registration,
     after setting up the POA etc. */
  gconf_server_load_sources();
  
  result = oaf_active_server_register(IID, server);

  if (result != OAF_REG_SUCCESS)
    {
      switch (result)
        {
        case OAF_REG_NOT_LISTED:
          gconf_log(GCL_ERR, _("OAF doesn't know about our IID; indicates broken installation; can't register; exiting\n"));
          break;
          
        case OAF_REG_ALREADY_ACTIVE:
          gconf_log(GCL_ERR, _("Another gconfd already registered with OAF; exiting\n"));
          break;

        case OAF_REG_ERROR:
        default:
          gconf_log(GCL_ERR, _("Unknown error registering gconfd with OAF; exiting\n"));
          break;
        }
      fast_cleanup();
      shutdown_databases();
      return 1;
    }

  /* Read saved log file, if any */
  logfile_read ();
  
  gconf_main();

  logfile_save ();
  
  fast_cleanup();

  shutdown_databases();

  gconfd_locale_cache_drop();
  
  gconf_log(GCL_INFO, _("Exiting"));
  
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
  gconf_log(GCL_DEBUG, "Performing periodic cleanup, expiring cache cruft");
  
  drop_old_databases ();

  /* expire old locale cache entries */
  gconfd_locale_cache_expire();
  
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
 * Database storage
 */

static GList* db_list = NULL;
static GHashTable* dbs_by_address = NULL;
static GConfDatabase *default_db = NULL;

static void
init_databases (void)
{
  g_assert(db_list == NULL);
  g_assert(dbs_by_address == NULL);
  
  dbs_by_address = g_hash_table_new(g_str_hash, g_str_equal);

  /* Default database isn't in the address hash since it has
     multiple addresses in a stack
  */
}

static void
set_default_database (GConfDatabase* db)
{
  default_db = db;
  
  /* Default database isn't in the address hash since it has
     multiple addresses in a stack
  */
}

static void
register_database (GConfDatabase *db)
{
  safe_g_hash_table_insert(dbs_by_address,
                           ((GConfSource*)db->sources->sources->data)->address,
                           db);
  
  db_list = g_list_prepend (db_list, db);
}

static void
unregister_database (GConfDatabase *db)
{
  g_hash_table_remove(dbs_by_address,
                      ((GConfSource*)(db->sources->sources->data))->address);

  db_list = g_list_remove (db_list, db);

  gconf_database_destroy (db);
}

static GConfDatabase*
lookup_database (const gchar *address)
{
  if (address == NULL)
    return default_db;
  else
    return g_hash_table_lookup (dbs_by_address, address);
}

static GConfDatabase*
obtain_database (const gchar *address,
                 GError **err)
{
  
  GConfSources* sources;
  const gchar* addresses[] = { address, NULL };
  GError* error = NULL;
  GConfDatabase *db;

  db = lookup_database (address);

  if (db)
    return db;
  
  sources = gconf_sources_new_from_addresses(addresses, &error);

  if (error != NULL)
    {
      if (err)
        *err = error;
      else
        g_error_free (error);

      return NULL;
    }
  
  if (sources == NULL)
    return NULL;

  db = gconf_database_new (sources);

  register_database (db);

  return db;
}

static void
drop_old_databases(void)
{
  GList *tmp_list;
  GList *dead = NULL;
  GTime now;
  
  now = time(NULL);
  
  tmp_list = db_list;
  while (tmp_list)
    {
      GConfDatabase* db = tmp_list->data;

      if (db->listeners &&                             /* not already hibernating */
          gconf_listeners_count(db->listeners) == 0 && /* Can hibernate */
          (now - db->last_access) > (60*45))   /* 45 minutes without access */
        {
          dead = g_list_prepend (dead, db);
        }
      
      tmp_list = g_list_next (tmp_list);
    }

  tmp_list = dead;
  while (tmp_list)
    {
      GConfDatabase* db = tmp_list->data;

      unregister_database (db);
            
      tmp_list = g_list_next (tmp_list);
    }

  g_list_free (dead);
}

static void
shutdown_databases (void)
{
  GList *tmp_list;  

  /* This may be called before we init fully,
     so check that everything != NULL */
  
  tmp_list = db_list;

  while (tmp_list)
    {
      GConfDatabase *db = tmp_list->data;

      gconf_database_destroy (db);
      
      tmp_list = g_list_next (tmp_list);
    }

  g_list_free (db_list);
  db_list = NULL;

  if (dbs_by_address)
    g_hash_table_destroy(dbs_by_address);

  dbs_by_address = NULL;

  if (default_db)
    gconf_database_destroy (default_db);
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
#if 0
  /* first and foremost, remove the stale server registration */
  if (server != CORBA_OBJECT_NIL)
    oaf_active_server_unregister("", server);
#endif
  /* OK we aren't going to unregister, because it can cause weird oafd
     spawning. The problem is that we have a race condition because
     we're going to destroy everything in shutdown_databases and if we
     get incoming connections we'll just segfault and crash
     spectacularly. Should probably add a we_are_shut_down flag or
     something. FIXME */
}


/* Exceptions */

gboolean
gconf_set_exception(GError** error,
                    CORBA_Environment* ev)
{
  GConfError en;

  if (error == NULL)
    return FALSE;

  if (*error == NULL)
    return FALSE;
  
  en = (*error)->code;

  /* success is not supposed to get set */
  g_return_val_if_fail(en != GCONF_ERROR_SUCCESS, FALSE);
  
  {
    ConfigException* ce;

    ce = ConfigException__alloc();
    g_assert(error != NULL);
    g_assert(*error != NULL);
    g_assert((*error)->message != NULL);
    ce->message = CORBA_string_dup((gchar*)(*error)->message); /* cast const */
      
    switch (en)
      {
      case GCONF_ERROR_FAILED:
        ce->err_no = ConfigFailed;
        break;
      case GCONF_ERROR_NO_PERMISSION:
        ce->err_no = ConfigNoPermission;
        break;
      case GCONF_ERROR_BAD_ADDRESS:
        ce->err_no = ConfigBadAddress;
        break;
      case GCONF_ERROR_BAD_KEY:
        ce->err_no = ConfigBadKey;
        break;
      case GCONF_ERROR_PARSE_ERROR:
        ce->err_no = ConfigParseError;
        break;
      case GCONF_ERROR_CORRUPT:
        ce->err_no = ConfigCorrupt;
        break;
      case GCONF_ERROR_TYPE_MISMATCH:
        ce->err_no = ConfigTypeMismatch;
        break;
      case GCONF_ERROR_IS_DIR:
        ce->err_no = ConfigIsDir;
        break;
      case GCONF_ERROR_IS_KEY:
        ce->err_no = ConfigIsKey;
        break;
      case GCONF_ERROR_NO_SERVER:
      case GCONF_ERROR_SUCCESS:
      default:
        g_assert_not_reached();
      }

    CORBA_exception_set(ev, CORBA_USER_EXCEPTION,
                        ex_ConfigException, ce);

    gconf_log(GCL_ERR, _("Returning exception: %s"), (*error)->message);
      
    g_error_free(*error);
    *error = NULL;
      
    return TRUE;
  }
}

/*
 * Logging
 */

static void
logfile_save (void)
{
  GMarkupNodeElement *root_node;
  gchar *str;
  int fd = -1;
  gchar *logfile;
  gchar *logdir;
  GList *tmp_list;

  root_node = g_markup_node_new_element ("gconfd_logfile");
  
  tmp_list = db_list;

  while (tmp_list)
    {
      GConfDatabase *db = tmp_list->data;
      GMarkupNode* node;
      
      node = gconf_database_to_node (db, FALSE);
      
      root_node->children = g_list_prepend (root_node->children,
                                            node);

      tmp_list = g_list_next (tmp_list);
    }

  /* Default database */
  root_node->children = g_list_prepend (root_node->children,
                                        gconf_database_to_node (default_db,
                                                                TRUE));
  
  str = g_markup_node_to_string ((GMarkupNode*)root_node, 0);

  logdir = gconf_concat_key_and_dir (g_get_home_dir (), ".gconfd");
  logfile = gconf_concat_key_and_dir (logdir, "saved_state");

  mkdir (logdir, 0700); /* ignore failure, we'll catch failures
                         * that matter on open() below
                         */
  
  fd = open (logfile, O_WRONLY | O_CREAT | O_TRUNC, 0700);

  if (fd < 0)
    {
      gconf_log (GCL_ERR, _("Failed to create ~/.gconfd/saved_state to record gconfd's state: %s"), strerror (errno));
      
      goto finished;
    }

  if (write (fd, str, strlen (str)) < 0)
    {
      gconf_log (GCL_ERR, _("Failed to write ~/.gconfd/saved_state to record gconfd's state: %s"), strerror (errno));
      
      goto finished;
    }
  
 finished:

  g_free (logfile);
  g_free (logdir);
  
  g_free (str);

  g_markup_node_free ((GMarkupNode*)root_node);
}

static void
restore_listener (GConfDatabase* db,
                  GMarkupNodeElement *node)
{
  gchar *ior;
  gchar *location;
  gchar *cnxn;
  ConfigListener cl;
  CORBA_Environment ev;
  guint new_cnxn;
  gchar *address;
  
  if (strcmp (node->name, "listener") != 0)
    {
      gconf_log (GCL_WARNING, _("Didn't understand element '%s' in saved state file"), node->name);

      return;
    }

  location = g_markup_node_get_attribute (node, "location");
  ior = g_markup_node_get_attribute (node, "ior");
  cnxn = g_markup_node_get_attribute (node, "connection");
  
  if (location == NULL)
    {
      gconf_log (GCL_WARNING, _("No location attribute on listener element in saved state file"));

      goto finished;
    }

  if (ior == NULL)
    {
      gconf_log (GCL_WARNING, _("No IOR attribute on listener element in saved state file"));

      goto finished;
    }
  
  if (cnxn == NULL)
    {
      gconf_log (GCL_WARNING, _("No connection ID attribute on listener element in saved state file"));

      goto finished;
    }

  CORBA_exception_init (&ev);
  
  cl = CORBA_ORB_string_to_object (oaf_orb_get (),
                                   ior,
                                   &ev);

  CORBA_exception_free (&ev);
  
  if (CORBA_Object_is_nil (cl, &ev))
    {
      CORBA_exception_free (&ev);

      gconf_log (GCL_DEBUG, "Client in saved state file no longer exists, not updating its listener connections");
      
      goto finished;
    }
  
  new_cnxn = gconf_database_add_listener(db, cl, location);

  if (db == default_db)
    address = "def"; /* cheesy hack */
  else
    address = ((GConfSource*)db->sources->sources->data)->address;
  
  ConfigListener_update_listener (cl,
                                  db->objref,
                                  address,
                                  gconf_string_to_gulong (cnxn),
                                  location,
                                  new_cnxn,
                                  &ev);

  if (ev._major != CORBA_NO_EXCEPTION)
    {
      gconf_log (GCL_DEBUG, "Failed to update client in saved state file, probably the client no longer exists");

      /* listener will get removed next time we try to notify */
      
      goto finished;
    }

  CORBA_Object_release (cl, &ev);

  CORBA_exception_free (&ev);
  
 finished:
  
  g_free (ior);
  g_free (cnxn);
  g_free (location);
}

static void
restore_database (GMarkupNodeElement *node)
{
  gchar *address;
  GList *tmp_list;
  GConfDatabase *db;
  
  if (!(strcmp (node->name, "database") == 0 ||
        strcmp (node->name, "default_database") == 0))
    {
      gconf_log (GCL_WARNING, _("Didn't understand element '%s' in saved state file"), node->name);

      return;
    }

  address = g_markup_node_get_attribute (node, "address");
  if (address == NULL)
    db = default_db;
  else
    db = obtain_database (address, NULL);
  
  if (db == NULL)
    return;
  
  tmp_list = node->children;
  while (tmp_list != NULL)
    {
      GMarkupNode *n = tmp_list->data;

      if (n->type != G_MARKUP_NODE_ELEMENT)
        {
          gconf_log (GCL_WARNING, _("Strange non-element node in saved state file"));
        }
      else
        {
          restore_listener (db, (GMarkupNodeElement*)n);
        }

      tmp_list = g_list_next (tmp_list);
    }
}

static void
logfile_read (void)
{
  GMarkupNode *root_node;
  GMarkupNodeElement *root_element;
  gchar *str;
  gchar *logfile;
  gchar *logdir;
  GError *error;
  GList *tmp_list;
  
  logdir = gconf_concat_key_and_dir (g_get_home_dir (), ".gconfd");
  logfile = gconf_concat_key_and_dir (logdir, "saved_state");

  error = NULL;
  str = g_file_get_contents (logfile, &error);
  if (str == NULL)
    {
      gconf_log (GCL_ERR, _("Unable to open saved state file '%s': %s"),
                 logfile, error->message);


      g_error_free (error);

      goto finished;
    }

  error = NULL;
  root_node = g_markup_node_from_string (str, -1, 0, &error);
  if (error != NULL)
    {
      gconf_log (GCL_ERR, _("Failed to restore saved state from file '%s': %s"),
                 logfile, error->message);
      g_error_free (error);

      goto finished;
    }

  if (root_node->type != G_MARKUP_NODE_ELEMENT)
    {
      gconf_log (GCL_ERR, _("Root node of saved state file should be an element"));
      
      goto finished;
    }

  root_element = (GMarkupNodeElement*) root_node;

  if (strcmp (root_element->name, "gconfd_logfile") != 0)
    {
      gconf_log (GCL_ERR,
                 _("Root node of saved state file should be 'gconfd_logfile', not '%s'"),
                 root_element->name);

      goto finished;
    }

  tmp_list = root_element->children;
  while (tmp_list != NULL)
    {
      GMarkupNode *node = tmp_list->data;      

      if (node->type != G_MARKUP_NODE_ELEMENT)
        {
          gconf_log (GCL_WARNING, _("Funny non-element node under <gconfd_logfile> in saved state file"));

        }
      else
        {
          restore_database ((GMarkupNodeElement*)node);
        }
      
      tmp_list = g_list_next (tmp_list);
    }
  
 finished:
  
  g_free (logfile);
  g_free (logdir);
}

static guint logfile_save_timeout_id = 0;

static gint
logfile_save_timeout (gpointer user_data)
{
  logfile_save ();
  logfile_save_timeout_id = 0;

  return FALSE;
}

void
gconf_logfile_queue_save (void)
{
  if (logfile_save_timeout_id == 0)
    {
      logfile_save_timeout_id =
        g_timeout_add (1000 /* 1000 * 60 * 5*/, /* 1 sec * sec/min * 5 min */
                       logfile_save_timeout, NULL);
    }
}
