
/* GConf
 * Copyright (C) 1999 Red Hat Inc.
 * Developed by Havoc Pennington, some code in here borrowed from 
 * gnome-name-server (Elliot Lee)
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
#warning "_ already defined in util.h"
#else
#define _(x) x
#endif

#ifdef N_ 
#warning "N_ already defined in util.h"
#else
#define N_(x) x
#endif

/*
 * Declarations
 */

static void g_conf_main(void);

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


