
/* GConf
 * Copyright (C) 1999, 2000 Red Hat Inc.
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
#include "gconf-orbit.h"
#include <stdio.h>
#include <unistd.h>


static void testclient_main(void);
static void notify_func(GConf* conf, guint cnxn_id, const gchar* key, GConfValue* value, gpointer user_data);


int 
main (int argc, char** argv)
{
  GConf* conf;
  guint cnxn;
  GConfValue* val;

  if (gconf_init_orb(&argc, argv) == CORBA_OBJECT_NIL)
    {
      g_warning("Failed to init orb");
      return 1;
    }

  if (!gconf_init())
    {
      g_warning("Failed to init GConf");
      return 1;
    }

  conf = gconf_new();

  cnxn = gconf_engine_notify_add(conf, "/hello/world", notify_func, NULL);

  if (cnxn != 0)
    g_print ("Connection %u added\n", cnxn);
  else
    {
      g_printerr ("Failed to add listener\n");
      return 1;
    }

  val = gconf_value_new(GCONF_VALUE_INT);

  gconf_value_set_int(val, 100);
 
  gconf_engine_set (conf, "/hello/world/whoo", val);
 
  testclient_main();

  gconf_destroy(conf);

  return 0;
}

static void 
notify_func(GConf* conf, guint cnxn_id, const gchar* key, GConfValue* value, gpointer user_data)
{
  int pid = getpid();
  g_print ("PID %d received notify on key `%s' connection %u\n", pid, key, cnxn_id);
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


