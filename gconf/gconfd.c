
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

void add_listener(PortableServer_Servant servant, const CORBA_char * where, 
                  const ConfigListener who, CORBA_Environment *ev);
void remove_listener(PortableServer_Servant servant, const CORBA_char * where, 
                     const ConfigListener who, CORBA_Environment *ev);
CORBA_any* lookup(PortableServer_Servant servant, const CORBA_char * key, 
                  CORBA_Environment *ev);
CORBA_any* set(PortableServer_Servant servant, const CORBA_char * key, 
               const CORBA_any* value, CORBA_Environment *ev);

PortableServer_ServantBase__epv base_epv = {
  NULL,
  NULL,
  NULL
};

POA_ConfigServer__epv server_epv = { NULL, add_listener, remove_listener, lookup, set };
POA_ConfigServer__vepv poa_server_vepv = { &base_epv, &server_epv };
POA_ConfigServer poa_server_servant = { NULL, &poa_server_vepv };


void 
add_listener(PortableServer_Servant servant, const CORBA_char * where, 
             const ConfigListener who, CORBA_Environment *ev)
{


}

void 
remove_listener(PortableServer_Servant servant, const CORBA_char * where, 
                const ConfigListener who, CORBA_Environment *ev)
{


}

CORBA_any* 
lookup(PortableServer_Servant servant, const CORBA_char * key, 
       CORBA_Environment *ev)
{


}

CORBA_any* 
set(PortableServer_Servant servant, const CORBA_char * key, 
    const CORBA_any* value, CORBA_Environment *ev)
{


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

  IIOPAddConnectionHandler = orb_add_connection;
  IIOPRemoveConnectionHandler = orb_remove_connection;

  CORBA_exception_init(&ev);
  orb = CORBA_ORB_init(&argc, argv, "orbit-local-orb", &ev);
  
  POA_ConfigServer__init(&poa_server_servant, &ev);
  
  poa = (PortableServer_POA)CORBA_ORB_resolve_initial_references(orb, "RootPOA", &ev);
  PortableServer_POAManager_activate(PortableServer_POA__get_the_POAManager(poa, &ev), &ev);
  PortableServer_POA_activate_object_with_id(poa,
                                             &objid, &poa_server_servant, &ev);
  
  server = PortableServer_POA_servant_to_reference(poa,
                                                   &poa_server_servant,
                                                   &ev);
  if (server == NULL) {
    printf("Cannot get objref\n");
    return 1;
  }

  ior = CORBA_ORB_object_to_string(orb, server, &ev);

  CORBA_free(ior);

  CORBA_ORB_run(orb, &ev);

  g_conf_main();

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


