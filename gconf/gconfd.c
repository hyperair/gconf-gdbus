
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
 * Declarations
 */

static void g_conf_main(void);

typedef struct _Listener Listener;

struct _Listener {
  ConfigListener obj; /* The CORBA object reference */
  guint cnxn;
};

static Listener* listener_new(const ConfigListener obj);
static void      listener_destroy(Listener* l);

/* Data structure to store the listeners */
typedef struct _LTable LTable;

struct _LTable {
  GNode* tree; /* Represents the config "filesystem" namespace. 
                *  Kept sorted. 
                */
  GPtrArray* listeners; /* Listeners are also kept in a flat array here, indexed by connection number */
};

typedef struct _LTableEntry LTableEntry;

struct _LTableEntry {
  gchar* name; /* The name of this "directory" */
  GList* listeners; /* Each listener listening *exactly* here. You probably 
                        want to notify all listeners *below* this node as well. 
                     */
};

static LTable* ltable = NULL;

static LTable* ltable_new(void);
static void    ltable_insert(LTable* ltable, const gchar* where, Listener* listener);
static void    ltable_remove(LTable* ltable, guint cnxn);
static void    ltable_destroy(LTable* ltable);
static void    ltable_notify_listeners(LTable* ltable, const gchar* key, const CORBA_any* value);

static LTableEntry* ltable_entry_new(const gchar* name);
static void         ltable_entry_destroy(LTableEntry* entry);

/*
 * ORB event loop integration
 */

static gboolean
orb_handle_connection(GIOChannel *source, GIOCondition cond,
		      GIOPConnection *cnx)
{
  /* The best way to know about an fd exception is if select()/poll()
   * tells you about it, so we just relay that information on to ORBit
   * if possible
   */
	
  if(cond & (G_IO_HUP|G_IO_NVAL|G_IO_ERR))
    giop_main_handle_connection_exception(cnx);
  else
    giop_main_handle_connection(cnx);

  return TRUE;
}

static void
orb_add_connection(GIOPConnection *cnx)
{
  int tag;
  GIOChannel *channel;

  channel = g_io_channel_unix_new(GIOP_CONNECTION_GET_FD(cnx));
  tag = g_io_add_watch_full   (channel, G_PRIORITY_DEFAULT,
			       G_IO_IN|G_IO_ERR|G_IO_HUP|G_IO_NVAL, 
			       (GIOFunc)orb_handle_connection,
			       cnx, NULL);
  g_io_channel_unref (channel);

  cnx->user_data = GUINT_TO_POINTER (tag);
}

static void
orb_remove_connection(GIOPConnection *cnx)
{
  g_source_remove(GPOINTER_TO_UINT (cnx->user_data));
  cnx->user_data = GINT_TO_POINTER (-1);
}


/* 
 * CORBA goo
 */

ConfigServer server = CORBA_OBJECT_NIL;

CORBA_long 
gconfd_add_listener(PortableServer_Servant servant, const CORBA_char * where, 
                        const ConfigListener who, CORBA_Environment *ev);
void 
gconfd_remove_listener(PortableServer_Servant servant,
                     const CORBA_long cnxn,
                     CORBA_Environment *ev);
CORBA_any* 
gconfd_lookup(PortableServer_Servant servant, const CORBA_char * key, 
                  CORBA_Environment *ev);
void
gconfd_set(PortableServer_Servant servant, const CORBA_char * key, 
           const CORBA_any* value, CORBA_Environment *ev);
void 
gconfd_sync(PortableServer_Servant servant, CORBA_Environment *ev);

CORBA_long
gconfd_ping(PortableServer_Servant servant, CORBA_Environment *ev);


PortableServer_ServantBase__epv base_epv = {
  NULL,
  NULL,
  NULL
};

POA_ConfigServer__epv server_epv = { NULL, 
                                     gconfd_add_listener, 
                                     gconfd_remove_listener, 
                                     gconfd_lookup, 
                                     gconfd_set, 
                                     gconfd_sync,
                                     gconfd_ping
};
POA_ConfigServer__vepv poa_server_vepv = { &base_epv, &server_epv };
POA_ConfigServer poa_server_servant = { NULL, &poa_server_vepv };


CORBA_long
gconfd_add_listener(PortableServer_Servant servant, const CORBA_char * where, 
                    const ConfigListener who, CORBA_Environment *ev)
{
  Listener* l;

  l = listener_new(who);

  ltable_insert(ltable, where, l);

  return l->cnxn;
}

void 
gconfd_remove_listener(PortableServer_Servant servant, const CORBA_long cnxn, CORBA_Environment *ev)
{
  ltable_remove(ltable, cnxn);
}

CORBA_any* 
gconfd_lookup(PortableServer_Servant servant, const CORBA_char * key, 
              CORBA_Environment *ev)
{
  
  return NULL; /* FIXME */
}

void
gconfd_set(PortableServer_Servant servant, const CORBA_char * key, 
           const CORBA_any* value, CORBA_Environment *ev)
{
  /* FIXME actually set. :-) */

  ltable_notify_listeners(ltable, key, value);
}

void 
gconfd_sync(PortableServer_Servant servant, CORBA_Environment *ev)
{

  
}

CORBA_long
gconfd_ping(PortableServer_Servant servant, CORBA_Environment *ev)
{
  return getpid();
}

#if 0

void
test_query(GConfSource* source, const gchar* key)
{
  GConfValue* value;

  value = g_conf_source_query_value(source, key);

  if (value != NULL)
    {
      gchar* str = g_conf_value_to_string(value);
      syslog(LOG_INFO, "Got value `%s' for key `%s'\n", str, key);
      g_free(str);
      g_conf_value_destroy(value);
    }
  else
    {
      syslog(LOG_INFO, "Didn't get value for `%s'\n", key);
    }
}

void 
test_set(GConfSource* source, const gchar* key, int val)
{
  GConfValue* value;

  value = g_conf_value_new(G_CONF_VALUE_INT);
  
  g_conf_value_set_int(value, val);

  g_conf_source_set_value(source, key, value);

  g_conf_value_destroy(value);

  syslog(LOG_INFO, "Set value of `%s' to %d\n", key, val);
}
#endif

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
      syslog(LOG_ERR, "Failed to open info file: %s", strerror(errno));
      return FALSE;
    }


  if (write_lock(fd, 0, SEEK_SET, 0) < 0)
    {
      if (errno == EACCES || errno == EAGAIN)
        {
          syslog(LOG_ERR, "Lock on info file is held by another process.");
          return FALSE;
        }
      else
        {
          syslog(LOG_ERR, "Couldn't get lock on the info file: %s",
                 strerror(errno));
          return FALSE;
        }
    }

  if (ftruncate(fd, 0) < 0)
    {
      syslog(LOG_ERR, "Couldn't truncate info file: %s",
             strerror(errno));
      return FALSE;
    }

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
                syslog(LOG_ERR, "Failed to write info file: %s", strerror(errno));
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
        syslog(LOG_ERR, "fcntl() F_GETFD failed for info file: %s", strerror(errno));
        return FALSE;
      }

    val |= FD_CLOEXEC;

    if (fcntl(fd, F_SETFD, val) < 0)
      {
        syslog(LOG_ERR, "fcntl() F_SETFD failed for info file: %s", strerror(errno));
        return FALSE;
      }
  }

  /* Don't close the file until we exit and implicitly kill the lock. */

  return TRUE;
}

static void
signal_handler (int signo)
{
  syslog (LOG_ERR, "Received signal %d\nshutting down.", signo);
  
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

  GConfSource* source;
  GConfSource* source2;

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
  openlog ("gconfd", LOG_NDELAY, LOG_USER);
  syslog (LOG_INFO, "starting");
  
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

#if 0
  source = g_conf_resolve_address("xml:/home/hp/.gconf");

  if (source != NULL)
    {
      syslog(LOG_INFO, "Resolved source.\n");

      test_query(source, "/foo");
      test_query(source, "/bar");
      test_set(source, "/foo", 40);
      test_query(source, "/foo");
      test_query(source, "/bar");
      test_query(source, "/subdir/super");
      test_query(source, "/subdir/duper");
      test_set(source, "/hello/this/is/a/nested/subdir", 115);

      if (!g_conf_source_sync_all(source))
        {
          syslog(LOG_ERR, "Sync failed.\n");
        }
    }
  else
    syslog(LOG_ERR, "Failed to resolve source.\n");

  source2 = g_conf_resolve_address("xml:/home/hp/random");
  
  if (source2 != NULL)
    {
      printf("Resolved second source\n");

      test_query(source2, "/hmm");
      test_query(source2, "/hrm");
    }

  if (source)
    g_conf_source_destroy(source);
  if (source2)
    g_conf_source_destroy(source2);
#endif

  ltable = ltable_new();

  IIOPAddConnectionHandler = orb_add_connection;
  IIOPRemoveConnectionHandler = orb_remove_connection;

  CORBA_exception_init(&ev);
  orb = CORBA_ORB_init(&argc, argv, "orbit-local-orb", &ev);

  if (orb == CORBA_OBJECT_NIL)
    {
      syslog(LOG_ERR, "Failed to get ORB reference");
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
      syslog(LOG_ERR, "Failed to get object reference for ConfigServer");
      return 1;
    }

  ior = CORBA_ORB_object_to_string(orb, server, &ev);

  /* Write IOR to a file (temporary hack, name server will be used eventually */
  if (!g_conf_server_write_info_file(ior))
    {
      syslog(LOG_ERR, "Failed to write info file - maybe another gconfd is running. Exiting.");
      return 1;
    }

  CORBA_free(ior);

  CORBA_ORB_run(orb, &ev);

  g_conf_main();

  ltable_destroy(ltable);
  ltable = NULL;

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
}

static void    
ltable_destroy(LTable* ltable)
{
  g_warning("Not bothering to destroy ltable for now");
}

static void
notify_listener_list(GList* list, const gchar* key, const CORBA_any* value)
{
  CORBA_Environment ev;
  GList* tmp;

  CORBA_exception_init(&ev);

  tmp = list;
  while (tmp != NULL)
    {
      Listener* l = tmp->data;

      ConfigListener_notify(l->obj, l->cnxn, key, value, &ev);
      
      if(ev._major != CORBA_NO_EXCEPTION) 
        {
          g_warning("Failed to notify listener %u", l->cnxn);
          /* FIXME FIXME remove these dead listeners */
          /* FIXME Actually I think we won't get an error from oneway,
             so we need to periodically ping all the listeners or something.
          */

          CORBA_exception_free(&ev);
        }


      tmp = g_list_next(tmp);
    }
}

static void    
ltable_notify_listeners(LTable* lt, const gchar* key, const CORBA_any* value)
{
  gchar** dirs;
  guint i;
  const gchar* noroot_key = key + 1;
  GNode* cur;

  /* Notify "/" listeners */
  notify_listener_list(((LTableEntry*)lt->tree->data)->listeners, key, value);

  dirs = g_strsplit(noroot_key, "/", -1);

  cur = lt->tree;
  i = 0;
  while (dirs[i] && cur)
    {
      GNode* child = cur->children;

      while (child != NULL)
        {
          LTableEntry* lte = child->data;

          if (strcmp(lte->name, dirs[i]) == 0)
            {
              notify_listener_list(lte->listeners, key, value);
              break;
            }

          child = g_node_next_sibling(child);
        }


      if (child != NULL)
        {
          /* We found and notified a dir */
          cur = child->children;
        }
      else
        {
          /* End of the line, no more listeners along this path */
          cur = NULL;
        }
        
      ++i;
    }
  
  g_strfreev(dirs);
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
