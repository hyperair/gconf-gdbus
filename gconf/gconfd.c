
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
#include "gconf-orbit.h"
#include <orb/orbit.h>

#include "GConf.h"

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>


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
#if 0
#define safe_g_hash_table_insert g_hash_table_insert
#else
static void
safe_g_hash_table_insert(GHashTable* ht, gpointer key, gpointer value)
{
  gpointer oldkey = NULL, oldval = NULL;

  if (g_hash_table_lookup_extended(ht, key, &oldkey, &oldval))
    {
      g_warning("Hash key `%s' is already in the table!",
                (gchar*)key);
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

/* return TRUE if the exception was set */
static gboolean g_conf_set_exception(CORBA_Environment* ev);

static void g_conf_main(void);
static void g_conf_main_quit(void);

/* fast_cleanup() nukes the info file,
   and is theoretically re-entrant.
*/
static void fast_cleanup(void);

typedef struct _LTable LTable;
typedef struct _GConfContext GConfContext;

struct _GConfContext {
  struct _LTable* ltable;
  GConfSources* sources;
  gchar* saved_address; /* if sources and ltable are NULL, then this is a
                           "dormant" context removed from the cache
                           and has to be re-instated.
                        */
};

static GConfContext* context_new(GConfSources* sources);
static void          context_destroy(GConfContext* ctx);
static CORBA_unsigned_long context_add_listener(GConfContext* ctx,
                                                ConfigListener who,
                                                const gchar* where);
static void          context_remove_listener(GConfContext* ctx,
                                             CORBA_unsigned_long cnxn);

static GConfValue*   context_lookup_value(GConfContext* ctx,
                                          const gchar* key);

static void          context_set(GConfContext* ctx, const gchar* key,
                                 GConfValue* value, ConfigValue* cvalue);
static void          context_unset(GConfContext* ctx, const gchar* key);
static gboolean      context_dir_exists(GConfContext* ctx, const gchar* dir);
static void          context_remove_dir(GConfContext* ctx, const gchar* dir);
static GSList*       context_all_entries(GConfContext* ctx, const gchar* dir);
static GSList*       context_all_dirs(GConfContext* ctx, const gchar* dir);
static void          context_set_schema(GConfContext* ctx, const gchar* key,
                                        const gchar* schema_key);
static void          context_sync(GConfContext* ctx);
static void          context_hibernate(GConfContext* ctx);
static void          context_awaken(GConfContext* ctx);

static void                 init_contexts();
static void                 shutdown_contexts(void);
static void                 set_default_context(GConfContext* ctx);
static ConfigServer_Context register_context(GConfContext* ctx);
static void                 unregister_context(ConfigServer_Context ctx);
static GConfContext*        lookup_context(ConfigServer_Context ctx);
static ConfigServer_Context lookup_context_id_from_address(const gchar* address);

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
static void
gconfd_set(PortableServer_Servant servant, ConfigServer_Context ctx,
           CORBA_char * key, 
           ConfigValue* value, CORBA_Environment *ev);

static void 
gconfd_unset(PortableServer_Servant servant,
             ConfigServer_Context ctx,
             CORBA_char * key, 
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
  gconfd_set,
  gconfd_unset,
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

  return ConfigServer_invalid_context;
}

static CORBA_unsigned_long
gconfd_add_listener(PortableServer_Servant servant,
                    ConfigServer_Context ctx,
                    CORBA_char * where, 
                    const ConfigListener who, CORBA_Environment *ev)
{
  GConfContext* gcc;

  gcc = lookup_context(ctx);

  if (gcc != NULL)
    {
      return context_add_listener(gcc, who, where);
    }
  else
    {
      return 0;
    }
}

static void 
gconfd_remove_listener(PortableServer_Servant servant,
                       ConfigServer_Context ctx,
                       CORBA_unsigned_long cnxn, CORBA_Environment *ev)
{
  GConfContext* gcc;
  
  gcc = lookup_context(ctx);

  if (gcc != NULL)
    context_remove_listener(gcc, cnxn);
}

static ConfigValue*
gconfd_lookup(PortableServer_Servant servant,
              ConfigServer_Context ctx,
              CORBA_char * key, 
              CORBA_Environment *ev)
{
  GConfValue* val;
  GConfContext* gcc;
  
  gcc = lookup_context(ctx);
  
  g_conf_clear_error();
  
  val = context_lookup_value(gcc, key);

  if (val != NULL)
    {
      ConfigValue* cval = corba_value_from_g_conf_value(val);

      g_conf_value_destroy(val);

      return cval;
    }
  else
    {
      g_conf_set_exception(ev);

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
  
  if (value->_d == InvalidVal)
    {
      syslog(LOG_ERR, "Received invalid value in set request");
      return;
    }

  val = g_conf_value_from_corba_value(value);

  str = g_conf_value_to_string(val);

  syslog(LOG_DEBUG, "Received request to set key `%s' to `%s'", key, str);

  g_free(str);

  gcc = lookup_context(ctx);
  
  context_set(gcc, key, val, value);

  g_conf_set_exception(ev);
}

static void 
gconfd_unset(PortableServer_Servant servant,
             ConfigServer_Context ctx,
             CORBA_char * key, 
             CORBA_Environment *ev)
{
  GConfContext* gcc;

  gcc = lookup_context(ctx);

  context_unset(gcc, key);

  g_conf_set_exception(ev);
}

static CORBA_boolean
gconfd_dir_exists(PortableServer_Servant servant,
                  ConfigServer_Context ctx,
                  CORBA_char *dir,
                  CORBA_Environment *ev)
{
  GConfContext *gcc;
  CORBA_boolean retval;
  
  gcc = lookup_context(ctx);
  
  retval = context_dir_exists(gcc, dir) ? CORBA_TRUE : CORBA_FALSE;

  g_conf_set_exception(ev);

  return retval;
}


static void 
gconfd_remove_dir(PortableServer_Servant servant,
                  ConfigServer_Context ctx,
                  CORBA_char * dir, 
                  CORBA_Environment *ev)
{  
  GConfContext* gcc;

  gcc = lookup_context(ctx);

  context_remove_dir(gcc, dir);

  g_conf_set_exception(ev);
}

static void 
gconfd_all_entries (PortableServer_Servant servant,
                    ConfigServer_Context ctx,
                    CORBA_char * dir, 
                    ConfigServer_KeyList ** keys, 
                    ConfigServer_ValueList ** values,
                    CORBA_Environment * ev)
{
  GSList* pairs;
  guint n;
  GSList* tmp;
  guint i;
  GConfContext* gcc;

  gcc = lookup_context(ctx);

  if (gcc != NULL)
    pairs = context_all_entries(gcc, dir);
  else
    pairs = NULL;
  
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
      fill_corba_value_from_g_conf_value(p->value, &((*values)->_buffer[i]));

      g_conf_entry_destroy(p);

      ++i;
      tmp = g_slist_next(tmp);
    }
  
  g_assert(i == n);

  g_slist_free(pairs);

  g_conf_set_exception(ev);
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

  gcc = lookup_context(ctx);

  if (gcc != NULL)
    subdirs = context_all_dirs(gcc, dir);
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

  g_conf_set_exception(ev);
}

static void 
gconfd_set_schema (PortableServer_Servant servant,
                   ConfigServer_Context ctx,
                   CORBA_char * key,
                   CORBA_char* schema_key, CORBA_Environment * ev)
{
  GConfContext* gcc;

  gcc = lookup_context(ctx);

  context_set_schema(gcc, key, schema_key);

  g_conf_set_exception(ev);
}

static void 
gconfd_sync(PortableServer_Servant servant,
            ConfigServer_Context ctx,
            CORBA_Environment *ev)
{
  GConfContext* gcc;

  gcc = lookup_context(ctx);

  context_sync(gcc);

  g_conf_set_exception(ev);
}

static CORBA_long
gconfd_ping(PortableServer_Servant servant, CORBA_Environment *ev)
{
  return getpid();
}

static void
gconfd_shutdown(PortableServer_Servant servant, CORBA_Environment *ev)
{
  syslog(LOG_INFO, _("Shutdown request received; exiting."));

  g_conf_main_quit();
}

/*
 * Main code
 */

/* This is called after we get the info file lock
   but before we write the info file, to avoid 
   client requests prior to source loading. 
*/
static void
g_conf_server_load_sources(void)
{
  gchar** addresses;
  GList* tmp;
  gboolean have_writeable = FALSE;
  gchar* conffile;
  GConfSources* sources = NULL;
  
  conffile = g_strconcat(GCONF_SYSCONFDIR, "/gconf/path", NULL);

  addresses = g_conf_load_source_path(conffile);

  g_free(conffile);

  /* -- Debug only */

  if (addresses == NULL)
    {
      conffile = g_strconcat(GCONF_SRCDIR, "/gconf/gconf.path", NULL);
      addresses = g_conf_load_source_path(conffile);
      g_free(conffile);
    }

  /* -- End of Debug Only */
  
  if (addresses == NULL)
    {
      /* We want to stay alive but do nothing, because otherwise every
         request would result in another failed gconfd being spawned.  
      */
      gchar* empty_addr[] = { NULL };
      syslog(LOG_ERR, _("No configuration sources in the source path, configuration won't be saved; edit /etc/gconf/path"));
      sources = g_conf_sources_new(empty_addr);
      return;
    }
  
  sources = g_conf_sources_new(addresses);

  g_free(addresses);

  g_assert(sources != NULL);

  if (sources->sources == NULL)
    syslog(LOG_ERR, _("No config source addresses successfully resolved, can't load or store config data"));
    
  tmp = sources->sources;

  while (tmp != NULL)
    {
      if (((GConfSource*)tmp->data)->flags & G_CONF_SOURCE_WRITEABLE)
        {
          have_writeable = TRUE;
          break;
        }

      tmp = g_list_next(tmp);
    }

  if (!have_writeable)
    syslog(LOG_WARNING, _("No writeable config sources successfully resolved, won't be able to save configuration changes"));

  /* Install the sources as the default context */
  set_default_context(context_new(sources));
}

/* From Stevens */
int
lock_reg(int fd, int cmd, int type, off_t offset, int whence, off_t len)
{
  struct flock lock;
  lock.l_type = type;
  lock.l_start = offset;
  lock.l_whence = whence;
  lock.l_len = len;

  return fcntl(fd, cmd, &lock);
}

#define write_lock(fd, offset, whence, len) \
  lock_reg(fd, F_SETLK, F_WRLCK, offset, whence, len)

gboolean
g_conf_server_write_info_file(const gchar* ior)
{
  /* This writing-IOR-to-file crap is a temporary hack. */
  gchar* fn;
  int fd;

  fn = g_conf_server_info_file();

  if (!g_conf_file_exists(fn))
    {
      gchar* dir = g_conf_server_info_dir();

      if (!g_conf_file_test(dir, G_CONF_FILE_ISDIR))
        {
          if (mkdir(dir, S_IRUSR | S_IWUSR | S_IXUSR) < 0)
            {
              g_free(dir);
              return FALSE;
            }
          else
            {
              g_free(dir);
            }
        }
    }

  /* Can't O_TRUNC until we have the silly lock */
  fd = open(fn, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);

  g_free(fn);

  if (fd < 0)
    {
      syslog(LOG_ERR, _("Failed to open info file: %s"), strerror(errno));
      return FALSE;
    }


  if (write_lock(fd, 0, SEEK_SET, 0) < 0)
    {
      if (errno == EACCES || errno == EAGAIN)
        {
          syslog(LOG_ERR, _("Lock on info file is held by another process."));
          return FALSE;
        }
      else
        {
          syslog(LOG_ERR, _("Couldn't get lock on the info file: %s"),
                 strerror(errno));
          return FALSE;
        }
    }

  if (ftruncate(fd, 0) < 0)
    {
      syslog(LOG_ERR, _("Couldn't truncate info file: %s"),
             strerror(errno));
      return FALSE;
    }

  /* IMPORTANT this must be done here, after the lock, 
     but before writing the file contents, so no one
     tries to contact gconfd before we have sources 
  */
  g_conf_server_load_sources();

  /* This block writes the file contents */
  {
    gint len = strlen(ior);
    gint written = 0;
    gboolean done = FALSE;

    while (!done)
      {
        written = write(fd, ior, len);
         
        if (written == len)
          done = TRUE;
        else if (written < 0)
          {
            if (errno != EINTR)
              {
                syslog(LOG_ERR, _("Failed to write info file: %s"), strerror(errno));
                return FALSE;
              }
            else
              continue;
          }
        else
          {
            g_assert(written < len);
            ior += written;
            len -= written;
            g_assert(len == strlen(ior));
          }
      }
  }
      
  /* Make the FD close-on-exec */
  {
    int val;

    val = fcntl(fd, F_GETFD, 0);

    if (val < 0)
      {
        syslog(LOG_ERR, _("fcntl() F_GETFD failed for info file: %s"), strerror(errno));
        return FALSE;
      }

    val |= FD_CLOEXEC;

    if (fcntl(fd, F_SETFD, val) < 0)
      {
        syslog(LOG_ERR, _("fcntl() F_SETFD failed for info file: %s"), strerror(errno));
        return FALSE;
      }
  }

  /* Don't close the file until we exit and implicitly kill the lock. */

  return TRUE;
}

static void
signal_handler (int signo)
{
  static gint in_fatal = 0;

  /* avoid loops */
  if (in_fatal > 1)
    return;
  
  ++in_fatal;
  
  syslog (LOG_ERR, _("Received signal %d\nshutting down."), signo);
  
  fast_cleanup();

  switch(signo) {
  case SIGSEGV:
    abort();
    
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
  char *ior;
  CORBA_ORB orb;
  gchar* logname;
  const gchar* username;
  guint len;
  
  int launcher_fd = -1; /* FD passed from the client that spawned us */

  /* Following all Stevens' rules for daemons */

  switch (fork())
    {
    case -1:
      fprintf(stderr, _("Failed to fork gconfd"));
      exit(1);
      break;
    case 0:
      break;
    default:
      exit(0);
      break;
    }
  
  setsid();

  chdir ("/");

  umask(0);

  /* Logs */
  username = g_get_user_name();
  len = strlen(username) + strlen("gconfd") + 5;
  logname = g_malloc(len);
  g_snprintf(logname, len, "gconfd (%s)", username);

  openlog (logname, LOG_NDELAY, LOG_USER);
  /* openlog() does not copy logname - what total brokenness.
     So we free it at the end of main() */
  
  syslog (LOG_INFO, _("starting, pid %u user `%s'"), 
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

  if (!g_conf_init_orb(&argc, argv)) /* must do before our own arg parsing */
    {
      syslog(LOG_ERR, _("Failed to init ORB: %s"), g_conf_error());
      exit(1);
    }

  if (argc > 2)
    {
      syslog(LOG_ERR, _("Invalid command line arguments"));
      exit(1);
    }
  else if (argc == 2)
    {
      /* Verify that it's a positive integer */
      gchar* s = argv[1];
      while (*s)
        {
          if (!isdigit(*s))
            {
              syslog(LOG_ERR, _("Command line arg should be a file descriptor, not `%s'"),
                     argv[1]);
              exit(1);
            }
          ++s;
        }

      launcher_fd = atoi(argv[1]);

      syslog(LOG_DEBUG, _("Will notify launcher client on fd %d"), launcher_fd);
    }

  init_contexts();

  orb = g_conf_get_orb();

  if (orb == CORBA_OBJECT_NIL)
    {
      syslog(LOG_ERR, _("Failed to get ORB reference"));
      exit(1);
    }

  POA_ConfigServer__init(&poa_server_servant, &ev);
  
  poa = (PortableServer_POA)CORBA_ORB_resolve_initial_references(orb, "RootPOA", &ev);
  PortableServer_POAManager_activate(PortableServer_POA__get_the_POAManager(poa, &ev), &ev);
  PortableServer_POA_activate_object_with_id(poa,
                                             &objid, &poa_server_servant, &ev);
  
  server = PortableServer_POA_servant_to_reference(poa,
                                                   &poa_server_servant,
                                                   &ev);
  if (server == CORBA_OBJECT_NIL) 
    {
      syslog(LOG_ERR, _("Failed to get object reference for ConfigServer"));
      return 1;
    }

  ior = CORBA_ORB_object_to_string(orb, server, &ev);

  /* Write IOR to a file (temporary hack, name server will be used eventually */
  if (!g_conf_server_write_info_file(ior))
    {
      syslog(LOG_ERR, _("Failed to write info file - maybe another gconfd is running. Exiting."));
      return 1;
    }

  CORBA_free(ior);

  /* If we got a fd on the command line, write the magic byte 'g' 
     to it to notify our spawning client that we're ready.
  */

  if (launcher_fd >= 0)
    {
      if (write(launcher_fd, "g", 1) != 1)
        syslog(LOG_ERR, _("Failed to notify spawning parent of server liveness: %s"),
               strerror(errno));

      if (close(launcher_fd) < 0)
        syslog(LOG_ERR, _("Failed to close pipe to spawning parent: %d"), 
               strerror(errno));
    }

  g_conf_main();

  fast_cleanup();

  shutdown_contexts();

  g_free(logname);
  
  return 0;
}

/*
 * Main loop
 */

static GSList* main_loops = NULL;

static void
g_conf_main(void)
{
  GMainLoop* loop;

  loop = g_main_new(TRUE);

  main_loops = g_slist_prepend(main_loops, loop);

  g_main_run(loop);

  main_loops = g_slist_remove(main_loops, loop);

  g_main_destroy(loop);
}

static void 
g_conf_main_quit(void)
{
  g_return_if_fail(main_loops != NULL);

  g_main_quit(main_loops->data);
}

/*
 * Declarations for listener handling
 */


typedef struct _Listener Listener;

struct _Listener {
  ConfigListener obj; /* The CORBA object reference */
  guint cnxn;
};

static Listener* listener_new(const ConfigListener obj);
static void      listener_destroy(Listener* l);

/* Data structure to store the listeners */

struct _LTable {
  GNode* tree; /* Represents the config "filesystem" namespace. 
                *  Kept sorted. 
                */
  GPtrArray* listeners; /* Listeners are also kept in a flat array here, indexed by connection number */
  guint active_listeners; /* count of "alive" listeners */  
};

typedef struct _LTableEntry LTableEntry;

struct _LTableEntry {
  gchar* name; /* The name of this "directory" */
  GList* listeners; /* Each listener listening *exactly* here. You probably 
                        want to notify all listeners *below* this node as well. 
                     */
};

static LTable* ltable_new(void);
static void    ltable_insert(LTable* ltable, const gchar* where, Listener* listener);
static void    ltable_remove(LTable* ltable, guint cnxn);
static void    ltable_destroy(LTable* ltable);
static void    ltable_notify_listeners(LTable* ltable, const gchar* key, ConfigValue* value);
static void    ltable_spew(LTable* ltable);

static LTableEntry* ltable_entry_new(const gchar* name);
static void         ltable_entry_destroy(LTableEntry* entry);

/*
 * Contexts
 */

static GConfContext*
context_new(GConfSources* sources)
{
  GConfContext* ctx;

  ctx = g_new0(GConfContext, 1);

  ctx->ltable = ltable_new();

  ctx->sources = sources;

  return ctx;
}

static void
context_destroy(GConfContext* ctx)
{
  if (ctx->ltable != NULL)
    {
      g_assert(ctx->sources != NULL);
      g_assert(ctx->saved_address == NULL);
      
      ltable_destroy(ctx->ltable);
      g_conf_sources_destroy(ctx->sources);
    }
  else
    {
      g_assert(ctx->saved_address != NULL);
      
      g_free(ctx->saved_address);
    }
      
  g_free(ctx);
}

static void
context_hibernate(GConfContext* ctx)
{
  g_return_if_fail(ctx->ltable != NULL);
  g_return_if_fail(ctx->ltable->active_listeners == 0);
  
  ltable_destroy(ctx->ltable);
  ctx->ltable = NULL;

  ctx->saved_address = g_strdup(((GConfSource*)ctx->sources->sources->data)->address);
  
  g_conf_sources_destroy(ctx->sources);
  ctx->sources = NULL;  
}

static void
context_awaken(GConfContext* ctx)
{
  gchar* addresses[2];

  g_return_if_fail(ctx->ltable == NULL);
  g_return_if_fail(ctx->sources == NULL);
  g_return_if_fail(ctx->saved_address != NULL);

  addresses[0] = ctx->saved_address;
  addresses[1] = NULL;

  ctx->sources = g_conf_sources_new(addresses);

  if (ctx->sources == NULL)
    {
      g_conf_set_error(G_CONF_BAD_ADDRESS,
                       _("Couldn't re-resolve hibernating configuration source `%s'"), ctx->saved_address);
      return;
    }

  ctx->ltable = ltable_new();

  g_free(ctx->saved_address);

  ctx->saved_address = NULL;
}

static CORBA_unsigned_long
context_add_listener(GConfContext* ctx,
                     ConfigListener who,
                     const gchar* where)
{
  Listener* l;

  l = listener_new(who);

  ltable_insert(ctx->ltable, where, l);

  syslog(LOG_DEBUG, "Added listener %u", (guint)l->cnxn);

  return l->cnxn;  
}

static void
context_remove_listener(GConfContext* ctx,
                        CORBA_unsigned_long cnxn)
{  
  syslog(LOG_DEBUG, "Removing listener %u", (guint)cnxn);
  ltable_remove(ctx->ltable, cnxn);
}

static GConfValue*
context_lookup_value(GConfContext* ctx,
                     const gchar* key)
{
  GConfValue* val;
  
  val = g_conf_sources_query_value(ctx->sources, key);

  return val;
}

static void
context_set(GConfContext* ctx,
            const gchar* key,
            GConfValue* val,
            ConfigValue* value)
{
  g_conf_clear_error();
  g_conf_sources_set_value(ctx->sources, key, val);

  if (g_conf_errno() != G_CONF_SUCCESS)
    {
      syslog(LOG_ERR, _("Error setting value for `%s': %s"),
             key, g_conf_error());
    }
  else
    ltable_notify_listeners(ctx->ltable, key, value);
}

static void
context_unset(GConfContext* ctx,
              const gchar* key)
{
  ConfigValue* val;
  
  syslog(LOG_DEBUG, "Received request to unset key `%s'", key);

  g_conf_clear_error();

  g_conf_sources_unset_value(ctx->sources, key);

  if (g_conf_errno() != G_CONF_SUCCESS)
    {
      syslog(LOG_ERR, _("Error unsetting `%s': %s"),
             key, g_conf_error());
    }
  else
    {
      val = invalid_corba_value();
      
      ltable_notify_listeners(ctx->ltable, key, val);
      
      CORBA_free(val);
    }
}

static gboolean
context_dir_exists(GConfContext* ctx,
                   const gchar* dir)
{
  gboolean ret;
  
  syslog(LOG_DEBUG, "Received dir_exists request for `%s'");
  
  g_conf_clear_error();
  
  ret = g_conf_sources_dir_exists(ctx->sources, dir);
  
  if (g_conf_errno() != G_CONF_SUCCESS)
    {
      syslog(LOG_ERR, _("Error checking existence of `%s': %s"),
             dir, g_conf_error());
      ret = FALSE;
    }

  return ret;
}

static void
context_remove_dir(GConfContext* ctx,
                   const gchar* dir)
{
  syslog(LOG_DEBUG, "Received request to remove dir `%s'", dir);

  g_conf_clear_error();
  
  g_conf_sources_remove_dir(ctx->sources, dir);

  if (g_conf_errno() != G_CONF_SUCCESS)
    {
      syslog(LOG_ERR, _("Error removing dir `%s': %s"),
             dir, g_conf_error());
    }
}

static GSList*
context_all_entries(GConfContext* ctx, const gchar* dir)
{
  GSList* entries;
  
  g_conf_clear_error();
  
  entries = g_conf_sources_all_entries(ctx->sources, dir);

  if (g_conf_errno() != G_CONF_SUCCESS)
    {
      syslog(LOG_ERR, _("Failed to get all entries in `%s': %s"),
             dir, g_conf_error());
    }

  return entries;
}

static GSList*
context_all_dirs(GConfContext* ctx, const gchar* dir)
{
  GSList* subdirs;  
  syslog(LOG_DEBUG, "Received request to list all subdirs in `%s'", dir);

  g_conf_clear_error();

  subdirs = g_conf_sources_all_dirs(ctx->sources, dir);

  if (g_conf_errno() != G_CONF_SUCCESS)
    {
      syslog(LOG_ERR, _("Error listing dirs in `%s': %s"),
             dir, g_conf_error());
    }
  return subdirs;
}

static void
context_set_schema(GConfContext* ctx, const gchar* key,
                   const gchar* schema_key)
{
  
  g_conf_clear_error();

  g_conf_sources_set_schema(ctx->sources, key, schema_key);

  if (g_conf_errno() != G_CONF_SUCCESS)
    {
      syslog(LOG_ERR, _("Error setting schema for `%s': %s"),
             key, g_conf_error());
    }
}

static void
context_sync(GConfContext* ctx)
{
  syslog(LOG_DEBUG, "Received request to sync all config data");
  
  if (!g_conf_sources_sync_all(ctx->sources))
    {
      syslog(LOG_ERR, _("Failed to sync one or more sources: %s"), 
             g_conf_error());
      return;
    }
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

  context_list->pdata[ConfigServer_default_context] = ctx;

  /* Default context isn't in the address hash since it has
     multiple addresses in a stack
  */
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
      syslog(LOG_ERR, _("Attempt to unregister invalid context ID"));

      return;
    }
  
  if (ctx >= context_list->len)
    {
      syslog(LOG_ERR, _("Bad context ID %lu, request ignored"), (gulong)ctx);
      return;
    }

  context = context_list->pdata[ctx];

  if (context == NULL)
    {
      syslog(LOG_ERR, _("Already-unregistered context ID %lu, request ignored"),
             (gulong)ctx);

      return;
    }
  
  context_list->pdata[ctx] = NULL;

  g_hash_table_remove(contexts_by_address,
                      ((GConfSource*)(context->sources->sources->data))->address);
  
  context_destroy(context);
}

static GConfContext*
lookup_context(ConfigServer_Context ctx)
{
  GConfContext* gcc;
  
  g_assert(context_list != NULL);
  g_assert(contexts_by_address != NULL);

  if (ctx >= context_list->len || ctx == ConfigServer_invalid_context)
    {
      syslog(LOG_ERR, _("Attempt to use invalid context ID %lu"),
             (gulong)ctx);
      return NULL;
    }
  
  gcc = context_list->pdata[ctx];

  if (gcc == NULL)
    {
      syslog(LOG_ERR, _("Attempt to use already-unregistered context ID %lu"),
             (gulong)ctx);
      return NULL;
    }

  if (gcc->ltable == NULL)
    {
      context_awaken(gcc);
      if (gcc->ltable == NULL)
        {
          /* Failed, error is now set. */
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
 * The listener table
 */

/* 0 is an error value */
static guint next_cnxn = 1;

static Listener* 
listener_new(ConfigListener obj)
{
  Listener* l;
  CORBA_Environment ev;

  CORBA_exception_init(&ev);

  l = g_new0(Listener, 1);

  l->obj = CORBA_Object_duplicate(obj, &ev);
  l->cnxn = next_cnxn;
  ++next_cnxn;

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

static LTable* 
ltable_new(void)
{
  LTable* lt;
  LTableEntry* lte;

  lt = g_new0(LTable, 1);

  lt->listeners = g_ptr_array_new();

  /* Set initial size and init error value (0) to NULL */
  g_ptr_array_set_size(lt->listeners, 5);
  g_ptr_array_index(lt->listeners, 0) = NULL;

  lte = ltable_entry_new("/");

  lt->tree = g_node_new(lte);

  lt->active_listeners = 0;
  
  return lt;
}

static void    
ltable_insert(LTable* lt, const gchar* where, Listener* l)
{
  gchar** dirnames;
  guint i;
  GNode* cur;
  GNode* found = NULL;
  LTableEntry* lte;
  const gchar* noroot_where = where + 1;

  /* Add to the tree */
  dirnames = g_strsplit(noroot_where, "/", -1);
  
  cur = lt->tree;
  i = 0;
  while (dirnames[i])
    {
      LTableEntry* ne;
      GNode* across;

      /* Find this dirname on this level, or add it. */
      g_assert (cur != NULL);        

      found = NULL;

      across = cur->children;

      while (across != NULL)
        {
          LTableEntry* lte = across->data;
          int cmp;

          cmp = strcmp(lte->name, dirnames[i]);

          if (cmp == 0)
            {
              found = across;
              break;
            }
          else if (cmp > 0)
            {
              /* Past it */
              break;
            }
          else 
            {
              across = g_node_next_sibling(across);
            }
        }

      if (found == NULL)
        {
          ne = ltable_entry_new(dirnames[i]);
              
          if (across != NULL) /* Across is at the one past */
            found = g_node_insert_data_before(cur, across, ne);
          else                /* Never went past, append - could speed this up by saving last visited */
            found = g_node_append_data(cur, ne);
        }

      g_assert(found != NULL);

      cur = found;

      ++i;
    }

  /* cur is still the root node ("/") if where was "/" since nothing
     was returned from g_strsplit */
  lte = cur->data;

  lte->listeners = g_list_prepend(lte->listeners, l);

  g_strfreev(dirnames);

  /* Add tree node to the flat table */
  g_ptr_array_set_size(lt->listeners, next_cnxn);
  g_ptr_array_index(lt->listeners, l->cnxn) = found;

  lt->active_listeners += 1;
  
  ltable_spew(lt);
}

static void    
ltable_remove(LTable* lt, guint cnxn)
{
  LTableEntry* lte;
  GList* tmp;
  GNode* node;

  if (cnxn >= lt->listeners->len)
    {
      syslog(LOG_WARNING, _("Attempt to remove nonexistent connection"));
      return;
    }

  /* Remove from the flat table */
  node = g_ptr_array_index(lt->listeners, cnxn);
  g_ptr_array_index(lt->listeners, cnxn) = NULL;

  if (node == NULL)
    {
      syslog(LOG_WARNING, _("Attempt to remove nonexistent connection"));
      return;
    }

  lte = node->data;
  
  tmp = lte->listeners;

  g_return_if_fail(tmp != NULL);

  while (tmp != NULL)
    {
      Listener* l = tmp->data;

      if (l->cnxn == cnxn)
        {
          if (tmp->prev)
            {
              tmp->prev->next = tmp->next;
            }
          else
            {
              /* tmp was the first (and maybe last) node */
              lte->listeners = tmp->next;
            }
          if (tmp->next)
            {
              tmp->next->prev = tmp->prev;
            }
          g_list_free_1(tmp);

          listener_destroy(l);

          break;
        }

      tmp = g_list_next(tmp);
    }
  
  g_return_if_fail(tmp != NULL);

  /* Remove from the tree if this node is now pointless */
  if (lte->listeners == NULL && node->children == NULL)
    {
      ltable_entry_destroy(lte);
      g_node_destroy(node);
    }

  lt->active_listeners -= 1;
}

static void    
ltable_destroy(LTable* ltable)
{
  guint i;

  i = ltable->listeners->len - 1;

  while (i > 0) /* 0 position in array is invalid */
    {
      if (g_ptr_array_index(ltable->listeners, i) != NULL)
        {
          listener_destroy(g_ptr_array_index(ltable->listeners, i));
          g_ptr_array_index(ltable->listeners, i) = NULL;
        }
      
      --i;
    }
  
  g_ptr_array_free(ltable->listeners, TRUE);

  g_node_destroy(ltable->tree);

  g_free(ltable);
}

static void
notify_listener_list(GList* list, const gchar* key, ConfigValue* value, GList** dead)
{
  CORBA_Environment ev;
  GList* tmp;

  CORBA_exception_init(&ev);

  syslog(LOG_DEBUG, "%u listeners to notify", g_list_length(list));

  tmp = list;
  while (tmp != NULL)
    {
      Listener* l = tmp->data;

      ConfigListener_notify(l->obj, l->cnxn, 
                            (gchar*)key, value, &ev);
      
      if(ev._major != CORBA_NO_EXCEPTION) 
        {
          syslog(LOG_WARNING, "Failed to notify listener %u: %s", 
                 (guint)l->cnxn, CORBA_exception_id(&ev));
          CORBA_exception_free(&ev);

          /* Dead listeners need to be forgotten */
          *dead = g_list_prepend(*dead, GUINT_TO_POINTER(l->cnxn));
        }
      else
        {
          syslog(LOG_DEBUG, "Notified listener %u of change to key `%s'",
                 (guint)l->cnxn, key);
        }


      tmp = g_list_next(tmp);
    }
}

static void    
ltable_notify_listeners(LTable* lt, const gchar* key, ConfigValue* value)
{
  gchar** dirs;
  guint i;
  const gchar* noroot_key = key + 1;
  GNode* cur;
  GList* dead = NULL;

  /* Notify "/" listeners */
  notify_listener_list(((LTableEntry*)lt->tree->data)->listeners, 
                       (gchar*)key, value, &dead);

  dirs = g_strsplit(noroot_key, "/", -1);

  cur = lt->tree;
  i = 0;
  while (dirs[i] && cur)
    {
      GNode* child = cur->children;

      syslog(LOG_DEBUG, "scanning for %s", dirs[i]);

      while (child != NULL)
        {
          LTableEntry* lte = child->data;

          syslog(LOG_DEBUG, "Looking at %s", lte->name);

          if (strcmp(lte->name, dirs[i]) == 0)
            {
              syslog(LOG_DEBUG, "Notifying listeners attached to `%s'",
                     lte->name);
              notify_listener_list(lte->listeners, key, value, &dead);
              break;
            }

          child = g_node_next_sibling(child);
        }

      if (child != NULL) /* we found a child, scan below it */
        cur = child;
      else               /* end of the line */
        cur = NULL;

      ++i;
    }
  
  g_strfreev(dirs);

  /* Clear the dead listeners */
  {
    GList* tmp;

    tmp = dead;
    
    while (tmp != NULL)
      {
        guint cnxn = GPOINTER_TO_UINT(tmp->data);

        syslog(LOG_DEBUG, "Removing dead listener %u", cnxn);

        ltable_remove(lt, cnxn);

        tmp = g_list_next(tmp);
      }

    g_list_free(dead);
  }
}

static LTableEntry* 
ltable_entry_new(const gchar* name)
{
  LTableEntry* lte;

  lte = g_new0(LTableEntry, 1);

  lte->name = g_strdup(name);

  return lte;
}

static void         
ltable_entry_destroy(LTableEntry* lte)
{
  g_return_if_fail(lte->listeners == NULL); /* should destroy all listeners first. */
  g_free(lte->name);
  g_free(lte);
}

/* Debug */
gboolean
spew_func(GNode* node, gpointer data)
{
  LTableEntry* lte = node->data;  
  GList* tmp;

  syslog(LOG_DEBUG, " Spewing node `%s'", lte->name);

  tmp = lte->listeners;
  while (tmp != NULL)
    {
      Listener* l = tmp->data;

      syslog(LOG_DEBUG, "   listener %u is here", (guint)l->cnxn);

      tmp = g_list_next(tmp);
    }

  return FALSE;
}

static void    
ltable_spew(LTable* lt)
{
  g_node_traverse(lt->tree, G_LEVEL_ORDER, G_TRAVERSE_ALL, -1, spew_func, NULL);
}

/*
 * Cleanup
 */

/* fast_cleanup() does the important parts,
   and is theoretically re-entrant.
*/
static void 
fast_cleanup(void)
{
  /* The goal of this function is to remove the 
     stale server info file
  */
  gchar* fn;

  fn = g_conf_server_info_file();

  /* Nothing we can do if it fails */
  unlink(fn);

  g_free(fn);
}


/* Exceptions */

static gboolean
g_conf_set_exception(CORBA_Environment* ev)
{
  GConfErrNo en = g_conf_errno();

  if (en == G_CONF_SUCCESS)
    return FALSE;
  else
    {
      ConfigException* ce;

      ce = ConfigException__alloc();
      ce->message = CORBA_string_dup((gchar*)g_conf_error()); /* cast const */
      
      switch (en)
        {
        case G_CONF_FAILED:
          ce->err_no = ConfigFailed;
          break;
        case G_CONF_NO_PERMISSION:
          ce->err_no = ConfigNoPermission;
          break;
        case G_CONF_BAD_ADDRESS:
          ce->err_no = ConfigBadAddress;
          break;
        case G_CONF_BAD_KEY:
          ce->err_no = ConfigBadKey;
          break;
        case G_CONF_PARSE_ERROR:
          ce->err_no = ConfigParseError;
          break;
        case G_CONF_CORRUPT:
          ce->err_no = ConfigCorrupt;
          break;
        case G_CONF_TYPE_MISMATCH:
          ce->err_no = ConfigTypeMismatch;
          break;
        case G_CONF_IS_DIR:
          ce->err_no = ConfigIsDir;
          break;
        case G_CONF_IS_KEY:
          ce->err_no = ConfigIsKey;
          break;
        case G_CONF_NO_SERVER:
        case G_CONF_SUCCESS:
        default:
          g_assert_not_reached();
        }

      CORBA_exception_set(ev, CORBA_USER_EXCEPTION,
                          ex_ConfigException, ce);

      return TRUE;
    }
}
