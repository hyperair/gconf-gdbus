
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


/*
 * This is the per-user configuration daemon.
 */

/* (well, right now it's a test program for the backend) */

#include "gconf-internals.h"

#include <stdio.h>

/* A bit pointless for now. :-) But we'll need it later. */

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

void
test_query(GConfSource* source, const gchar* key)
{
  GConfValue* value;

  value = g_conf_source_query_value(source, key);

  if (value != NULL)
    {
      gchar* str = g_conf_value_to_string(value);
      printf("Got value `%s' for key `%s'\n", str, key);
      g_free(str);
      g_conf_value_destroy(value);
    }
  else
    {
      printf("Didn't get value for `%s'\n", key);
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

  printf("Set value of `%s' to %d\n", key, val);
}

int 
main(int argc, char** argv)
{
  GConfSource* source;
  GConfSource* source2;

  source = g_conf_resolve_address("xml:/home/hp/.gconf");

  if (source != NULL)
    {
      printf("Resolved source.\n");

      test_query(source, "/foo");
      test_query(source, "/bar");
      test_set(source, "/foo", 40);
      test_query(source, "/foo");
      test_query(source, "/bar");
      test_query(source, "/subdir/super");
      test_query(source, "/subdir/duper");

      if (!g_conf_source_sync_all(source))
        {
          printf("Sync failed.\n");
        }
    }
  else
    printf("Failed.\n");

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

  /*  g_conf_main(); */

  return 0;
}



