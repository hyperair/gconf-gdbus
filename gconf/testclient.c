
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

#include "gconf.h"
#include <stdio.h>
#include <unistd.h>


static void testclient_main(void);
static void notify_func(GConf* conf, const gchar* key, GConfValue* value, gpointer user_data);


int 
main (int argc, char** argv)
{
  GConf* conf;
  guint cnxn;

  conf = g_conf_global_conf();

  cnxn = g_conf_notify_add(conf, "/hello/world", notify_func, NULL);

  if (cnxn != 0)
    printf("Connection %u added\n", cnxn);
  else
    {
      fprintf(stderr, "Failed to add listener\n");
      return 1;
    }
  
  testclient_main();

  return 0;
}

static void 
notify_func(GConf* conf, const gchar* key, GConfValue* value, gpointer user_data)
{
  int pid = getpid();
  printf("PID %d received notify on key `%s'\n", pid, key);
}

/*
 * Main loop
 */

static GSList* main_loops = NULL;

static void
testclient_main(void)
{
  GMainLoop* loop;

  loop = g_main_new(TRUE);

  main_loops = g_slist_prepend(main_loops, loop);

  g_main_run(loop);

  main_loops = g_slist_remove(main_loops, loop);

  g_main_destroy(loop);
}


