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
#include "gconf-orbit.h"
#include <stdio.h>
#include <unistd.h>
#include <popt.h>

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

int set_mode = FALSE;
int get_mode = FALSE;
char* value_type = NULL;

struct poptOption options[] = {
  {
    "set",
    's',
    POPT_ARG_NONE,
    &set_mode,
    0,
    N_("Set a key to a value and sync."),
    NULL
  },
  { 
    "get",
    'g',
    POPT_ARG_NONE,
    &get_mode,
    0,
    N_("Print the value of a key to standard out."),
    NULL
  },
  { 
    "type",
    't',
    POPT_ARG_STRING,
    &value_type,
    0,
    N_("Specify the type of the value being set."),
    N_("[int|bool|float|string]")
  },  
  {
    NULL,
    '\0',
    0,
    NULL,
    0,
    NULL,
    NULL
  }
};

int 
main (int argc, char** argv)
{
  GConf* conf;
  poptContext ctx;
  gint nextopt;

  ctx = poptGetContext("gconftool", argc, argv, options, 0);

  poptReadDefaultConfig(ctx, TRUE);

  while((nextopt = poptGetNextOpt(ctx)) > 0)
    /*nothing*/;

  if(nextopt != -1) 
    {
      fprintf(stderr, _("Error on option %s: %s.\nRun '%s --help' to see a full list of available command line options.\n"),
              poptBadOption(ctx, 0),
              poptStrerror(nextopt),
              argv[0]);
      return 1;
    }

  if (get_mode && set_mode)
    {
      fprintf(stderr, _("Can't get and set simultaneously\n"));
      return 1;
    }

  if ((value_type != NULL) && !set_mode)
    {
      fprintf(stderr, _("Value type is only relevant when setting a value"));
      return 1;
    }

  if (set_mode && (value_type == NULL))
    {
      fprintf(stderr, _("Must specify a type when setting a value"));
      return 1;
    }

  if (g_conf_init_orb(&argc, argv) == CORBA_OBJECT_NIL)
    {
      fprintf(stderr, _("Failed to init orb"));
      return 1;
    }

  if (!g_conf_init())
    {
      fprintf(stderr, _("Failed to init GConf"));
      return 1;
    }

  conf = g_conf_new();

  g_assert(conf != NULL);

  if (get_mode)
    {
      gchar** args = poptGetArgs(ctx);

      if (args == NULL)
        {
          fprintf(stderr, _("Must specify a key to get"));
          return 1;
        }
      
      while (*args)
        {
          GConfValue* value;
          gchar* s;

          value = g_conf_get(conf, *args);
         
          if (value != NULL)
            {
              s = g_conf_value_to_string(value);

              fputs(s, stdout); /* in case the string contains printf formats */
              fputs("\n", stdout);

              g_free(s);
              g_conf_value_destroy(value);
            }
          else
            {
              fprintf(stderr, "No value set for `%s'\n", *args); 
            }
 
          ++args;
        }
    }

  if (set_mode)
    {
      gchar** args = poptGetArgs(ctx);

      if (args == NULL)
        {
          fprintf(stderr, _("Must specify alternating keys/values as arguments"));
          return 1;
        }

      while (*args)
        {
          gchar* key;
          gchar* value;
          GConfValueType type = G_CONF_VALUE_INVALID;
          GConfValue* gval;

          key = *args;
          ++args;
          value = *args;

          if (!g_conf_valid_key(key))
            {
              fprintf(stderr, _("Invalid key: `%s'"), key);
              return 1;
            }

          if (value == NULL)
            {
              fprintf(stderr, _("No value to set for key: `%s'"), key);
              return 1;
            }

          switch (*value_type)
            {
            case 'i':
            case 'I':
              type = G_CONF_VALUE_INT;
              break;
            case 'f':
            case 'F':
              type = G_CONF_VALUE_FLOAT;
              break;
            case 'b':
            case 'B':
              type = G_CONF_VALUE_BOOL;
              break;
            case 's':
            case 'S':
              type = G_CONF_VALUE_STRING;
              break;
            default:
              fprintf(stderr, _("Don't understand type `%s'"), value_type);
              return 1;
              break;
            }
          
          gval = g_conf_value_new_from_string(type, value);

          if (gval == NULL)
            {
              fprintf(stderr, _("Didn't understand value `%s'"), value);
              return 1;
            }

          g_conf_set(conf, key, gval);

          g_conf_value_destroy(gval);

          ++args;
        }
    }

  poptFreeContext(ctx);
  
  g_conf_destroy(conf);

  return 0;
}

