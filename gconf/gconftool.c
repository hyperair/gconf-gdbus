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
#include "gconf-internals.h"
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

static int set_mode = FALSE;
static int get_mode = FALSE;
static int unset_mode = FALSE;
static int all_pairs_mode = FALSE;
static int all_subdirs_mode = FALSE;
static int recursive_list = FALSE;
static char* value_type = NULL;
static int shutdown_gconfd = FALSE;
static int ping_gconfd = FALSE;
static int spawn_gconfd = FALSE;

struct poptOption options[] = {
  { 
    NULL, 
    '\0', 
    POPT_ARG_INCLUDE_TABLE, 
    poptHelpOptions,
    0, 
    _("Help options"), 
    NULL 
  },
  {
    "set",
    's',
    POPT_ARG_NONE,
    &set_mode,
    0,
    N_("Set a key to a value and sync. Use with --type."),
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

    "unset",
    'u',
    POPT_ARG_NONE,
    &unset_mode,
    0, 
    N_("Unset the keys on the command line"),
    NULL
  },
  { 
    "all-pairs",
    'a',
    POPT_ARG_NONE,
    &all_pairs_mode,
    0,
    N_("Print all key/value pairs in a directory."),
    NULL
  },
  {
    "all-dirs",
    '\0',
    POPT_ARG_NONE,
    &all_subdirs_mode,
    0,
    N_("Print all subdirectories in a directory."),
    NULL
  },
  {
    "recursive-list",
    'R',
    POPT_ARG_NONE,
    &recursive_list,
    0,
    N_("Print all subdirectories and entries under a dir, recursively."),
    NULL
  },
  
  { 
    "shutdown",
    '\0',
    POPT_ARG_NONE,
    &shutdown_gconfd,
    0,
    N_("Shut down gconfd. DON'T USE THIS OPTION WITHOUT GOOD REASON."),
    NULL
  },
  { 
    "ping",
    'p',
    POPT_ARG_NONE,
    &ping_gconfd,
    0,
    N_("Return 0 if gconfd is running, 2 if not."),
    NULL
  },
  { 
    "spawn",
    's',
    POPT_ARG_NONE,
    &spawn_gconfd,
    0,
    N_("Launch the config server (gconfd). (Normally happens automatically when needed.)"),
    NULL
  },
  { 
    "type",
    't',
    POPT_ARG_STRING,
    &value_type,
    0,
    N_("Specify the type of the value being set. Unique abbreviations OK."),
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

static void do_recursive_list(GConf* conf, gchar** args);
static void do_all_pairs(GConf* conf, gchar** args);
static void list_pairs_in_dir(GConf* conf, gchar* dir, guint depth);

/* FIXME um, break this function up... */
/* FIXME do a single sync on exit */
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

  /* Um, this is a mess. Not using popt right? */

  if ((get_mode && set_mode) ||
      (get_mode && unset_mode))
    {
      fprintf(stderr, _("Can't get and set/unset simultaneously\n"));
      return 1;
    }

  if ((set_mode && get_mode) ||
      (set_mode && unset_mode))
    {
      fprintf(stderr, _("Can't set and get/unset simultaneously\n"));
      return 1;
    }

  if ((all_pairs_mode && get_mode) ||
      (all_pairs_mode && set_mode) ||
      (all_pairs_mode && unset_mode))
    {
      fprintf(stderr, _("Can't use --all-pairs with --get or --set\n"));
      return 1;
    }

  if ((all_subdirs_mode && get_mode) ||
      (all_subdirs_mode && set_mode) ||
      (all_subdirs_mode && unset_mode))
    {
      fprintf(stderr, _("Can't use --all-dirs with --get or --set\n"));
      return 1;
    }

  if ((recursive_list && get_mode) ||
      (recursive_list && set_mode) ||
      (recursive_list && unset_mode) ||
      (recursive_list && all_pairs_mode) ||
      (recursive_list && all_subdirs_mode))
    {
      fprintf(stderr, _("--recursive-list should not be used with --get, --set, --unset, --all-pairs, or --all-dirs\n"));
      return 1;
    }


  if ((value_type != NULL) && !set_mode)
    {
      fprintf(stderr, _("Value type is only relevant when setting a value\n"));
      return 1;
    }

  if (set_mode && (value_type == NULL))
    {
      fprintf(stderr, _("Must specify a type when setting a value\n"));
      return 1;
    }

  if (ping_gconfd && (shutdown_gconfd || set_mode || get_mode || unset_mode ||
                      all_subdirs_mode || all_pairs_mode || recursive_list || 
                      spawn_gconfd))
    {
      fprintf(stderr, _("Ping option must be used by itself.\n"));
      return 1;
    }

  if (g_conf_init_orb(&argc, argv) == CORBA_OBJECT_NIL)
    {
      fprintf(stderr, _("Failed to init orb\n"));
      return 1;
    }

  if (!g_conf_init())
    {
      fprintf(stderr, _("Failed to init GConf\n"));
      return 1;
    }

  conf = g_conf_new();

  g_assert(conf != NULL);

  /* Do this first, since we only want to do this if the user selected
     it. */
  if (ping_gconfd)
    {
      if (g_conf_ping_daemon())
        return 0;
      else 
        return 2;
    }

  if (spawn_gconfd)
    {
      if (!g_conf_spawn_daemon())
        fprintf(stderr, _("Failed to spawn the config server (gconfd)\n"));
      /* don't exit, it's OK to have this along with other options
         (however, it's probably pointless) */
    }

  if (get_mode)
    {
      gchar** args = poptGetArgs(ctx);

      if (args == NULL)
        {
          fprintf(stderr, _("Must specify a key or keys to get\n"));
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
          fprintf(stderr, _("Must specify alternating keys/values as arguments\n"));
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
              fprintf(stderr, _("Invalid key: `%s'\n"), key);
              return 1;
            }

          if (value == NULL)
            {
              fprintf(stderr, _("No value to set for key: `%s'\n"), key);
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
              fprintf(stderr, _("Don't understand type `%s'\n"), value_type);
              return 1;
              break;
            }
          
          gval = g_conf_value_new_from_string(type, value);

          if (gval == NULL)
            {
              fprintf(stderr, _("Didn't understand value `%s'\n"), value);
              return 1;
            }

          g_conf_set(conf, key, gval);

          g_conf_value_destroy(gval);

          ++args;
        }

      g_conf_sync(conf);
    }

  if (all_pairs_mode)
    {
      gchar** args = poptGetArgs(ctx);

      if (args == NULL)
        {
          fprintf(stderr, _("Must specify one or more dirs to get key/value pairs from.\n"));
          return 1;
        }

      do_all_pairs(conf, args);
    }

  if (unset_mode)
    {
      gchar** args = poptGetArgs(ctx);

      if (args == NULL)
        {
          fprintf(stderr, _("Must specify one or more keys to unset.\n"));
          return 1;
        }

      while (*args)
        {
          g_conf_unset(conf, *args);
          ++args;
        }

      g_conf_sync(conf);
    }

  if (all_subdirs_mode)
    {
      gchar** args = poptGetArgs(ctx);

      if (args == NULL)
        {
          fprintf(stderr, _("Must specify one or more dirs to get subdirs from.\n"));
          return 1;
        }
      
      while (*args)
        {
          GSList* subdirs;
          GSList* tmp;

          subdirs = g_conf_all_dirs(conf, *args);
          
          if (subdirs != NULL)
            {
              tmp = subdirs;

              while (tmp != NULL)
                {
                  gchar* s = tmp->data;

                  printf(" %s\n", s);

                  g_free(s);

                  tmp = g_slist_next(tmp);
                }

              g_slist_free(subdirs);
            }
 
          ++args;
        }
    }

  if (recursive_list)
    {
      gchar** args = poptGetArgs(ctx);

      if (args == NULL)
        {
          fprintf(stderr, _("Must specify one or more dirs to recursively list.\n"));
          return 1;
        }

      do_recursive_list(conf, args);
    }

  poptFreeContext(ctx);
  
  g_conf_destroy(conf);

  if (shutdown_gconfd)
    g_conf_shutdown_daemon();

  return 0;
}

static void 
recurse_subdir_list(GConf* conf, GSList* subdirs, guint depth)
{
  GSList* tmp;
  gchar* whitespace;

  whitespace = g_strnfill(depth, ' ');

  tmp = subdirs;
  
  while (tmp != NULL)
    {
      gchar* s = tmp->data;

      printf("%s%s:\n", whitespace, s);
      
      list_pairs_in_dir(conf, s, depth);

      recurse_subdir_list(conf, g_conf_all_dirs(conf, s), depth+1);

      g_free(s);
      
      tmp = g_slist_next(tmp);
    }
  
  g_slist_free(subdirs);
  g_free(whitespace);
}

static void
do_recursive_list(GConf* conf, gchar** args)
{
  while (*args)
    {
      GSList* subdirs;

      subdirs = g_conf_all_dirs(conf, *args);

      list_pairs_in_dir(conf, *args, 0);
          
      recurse_subdir_list(conf, subdirs, 1);
 
      ++args;
    }
}

static void 
list_pairs_in_dir(GConf* conf, gchar* dir, guint depth)
{
  GSList* pairs;
  GSList* tmp;
  gchar* whitespace;

  whitespace = g_strnfill(depth, ' ');

  pairs = g_conf_all_pairs(conf, dir);
          
  if (pairs != NULL)
    {
      tmp = pairs;

      while (tmp != NULL)
        {
          GConfPair* pair = tmp->data;
          gchar* s;

          s = g_conf_value_to_string(pair->value);

          printf(" %s%s = %s\n", whitespace, pair->key, s);

          g_free(s);
                  
          g_conf_pair_destroy(pair);

          tmp = g_slist_next(tmp);
        }

      g_slist_free(pairs);
    }

  g_free(whitespace);
}

static void 
do_all_pairs(GConf* conf, gchar** args)
{      
  while (*args)
    {
      list_pairs_in_dir(conf, *args, 0);
      ++args;
    }
}

