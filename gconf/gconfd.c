
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
      test_query(source, "/foo");
      test_query(source, "/bar");
      test_query(source, "/subdir/super");
      test_query(source, "/subdir/duper");
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

  return 0;
}
