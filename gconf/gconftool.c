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
static int all_entries_mode = FALSE;
static int all_subdirs_mode = FALSE;
static char* dir_exists = NULL;
static int recursive_list = FALSE;
static int set_schema_mode = FALSE;
static char* value_type = NULL;
static int shutdown_gconfd = FALSE;
static int ping_gconfd = FALSE;
static int spawn_gconfd = FALSE;
static char* short_desc = NULL;
static char* long_desc = NULL;
static char* owner = NULL;

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
    N_("Print the value of a key to standard output."),
    NULL
  },
  {
    "set-schema",
    '\0',
    POPT_ARG_NONE,
    &set_schema_mode,
    0,
    N_("Set a schema and sync. Use with --short-desc, --long-desc, --owner, and --type."),
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
    "all-entries",
    'a',
    POPT_ARG_NONE,
    &all_entries_mode,
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
    "dir-exists",
    '\0',
    POPT_ARG_STRING,
    &dir_exists,
    0,
    N_("Return 0 if the directory exists, 2 if it does not."),
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
    N_("Specify the type of the value being set, or the type of the value a schema describes. Unique abbreviations OK."),
    N_("int|bool|float|string")
  },  
  { 
    "short-desc",
    '\0',
    POPT_ARG_STRING,
    &short_desc,
    0,
    N_("Specify a short half-line description to go in a schema."),
    N_("DESCRIPTION")
  },
  { 
    "long-desc",
    '\0',
    POPT_ARG_STRING,
    &long_desc,
    0,
    N_("Specify a several-line description to go in a schema."),
    N_("DESCRIPTION")
  },  
  {
    "owner",
    '\0',
    POPT_ARG_STRING,
    &owner,
    0,
    N_("Specify the owner of a schema"),
    N_("OWNER")
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

static void do_recursive_list(GConfEngine* conf, const gchar** args);
static void do_all_pairs(GConfEngine* conf, const gchar** args);
static void list_pairs_in_dir(GConfEngine* conf, const gchar* dir, guint depth);

/* FIXME um, break this function up... */
/* FIXME do a single sync on exit */
int 
main (int argc, char** argv)
{
  GConfEngine* conf;
  poptContext ctx;
  gint nextopt;
  GConfError* err = NULL;
  
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

  if ((all_entries_mode && get_mode) ||
      (all_entries_mode && set_mode) ||
      (all_entries_mode && unset_mode))
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
      (recursive_list && all_entries_mode) ||
      (recursive_list && all_subdirs_mode))
    {
      fprintf(stderr, _("--recursive-list should not be used with --get, --set, --unset, --all-pairs, or --all-dirs\n"));
      return 1;
    }

  if ((set_schema_mode && get_mode) ||
      (set_schema_mode && set_mode) ||
      (set_schema_mode && unset_mode) ||
      (set_schema_mode && all_entries_mode) ||
      (set_schema_mode && all_subdirs_mode))
    {
      fprintf(stderr, _("--set_schema should not be used with --get, --set, --unset, --all-pairs, --all-dirs\n"));
      return 1;
    }  

  if ((value_type != NULL) && !(set_mode || set_schema_mode))
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
                      all_subdirs_mode || all_entries_mode || recursive_list || 
                      spawn_gconfd || dir_exists))
    {
      fprintf(stderr, _("Ping option must be used by itself.\n"));
      return 1;
    }

  if (dir_exists && (shutdown_gconfd || set_mode || get_mode || unset_mode ||
                     all_subdirs_mode || all_entries_mode || recursive_list || 
                     spawn_gconfd))
    {
      fprintf(stderr, _("dir-exists option must be used by itself.\n"));
      return 1;
    }

  if (gconf_init_orb(&argc, argv, &err) == CORBA_OBJECT_NIL)
    {
      fprintf(stderr, _("Failed to init orb: %s\n"), err->str);
      gconf_error_destroy(err);
      err = NULL;
      return 1;
    }

  if (!gconf_init(&err))
    {
      fprintf(stderr, _("Failed to init GConf: %s\n"), err->str);
      gconf_error_destroy(err);
      err = NULL;
      return 1;
    }

  conf = gconf_engine_new();

  g_assert(conf != NULL);

  /* Do this first, since we only want to do this if the user selected
     it. */
  if (ping_gconfd)
    {
      if (gconf_ping_daemon())
        return 0;
      else 
        return 2;
    }

  if (dir_exists != NULL) 
    {
      gboolean exists = gconf_dir_exists(conf, dir_exists, &err);

      if (err != NULL)
        {
          fprintf(stderr, "%s\n", err->str);
          gconf_error_destroy(err);
          err = NULL;
        }

      if (exists)
        return 0;
      else
        return 2;
    }

  if (spawn_gconfd)
    {
      if (!gconf_spawn_daemon(&err))
        {
          fprintf(stderr, _("Failed to spawn the config server (gconfd): %s\n"), 
                  err->str);
          gconf_error_destroy(err);
          err = NULL;
        }
      /* don't exit, it's OK to have this along with other options
         (however, it's probably pointless) */
    }

  if (get_mode)
    {
      const gchar** args = poptGetArgs(ctx);

      if (args == NULL)
        {
          fprintf(stderr, _("Must specify a key or keys to get\n"));
          return 1;
        }
      
      while (*args)
        {
          GConfValue* value;
          gchar* s;

          err = NULL;

          value = gconf_get(conf, *args, &err);
         
          if (value != NULL)
            {
              if (value->type != GCONF_VALUE_SCHEMA)
                {
                  s = gconf_value_to_string(value);

                  printf("%s\n", s);

                  g_free(s);
                }
              else
                {
                  GConfSchema* sc = gconf_value_schema(value);
                  GConfValueType stype = gconf_schema_type(sc);
                  const gchar* long_desc = gconf_schema_long_desc(sc);
                  const gchar* short_desc = gconf_schema_short_desc(sc);
                  const gchar* owner = gconf_schema_owner(sc);

                  printf(_("Type: %s\n"), gconf_value_type_to_string(stype));
                  printf(_("Owner: %s\n"), owner ? owner : _("Unset"));
                  printf(_("Short Desc: %s\n"), short_desc ? short_desc : _("Unset"));
                  printf(_("Long Desc: %s\n"), long_desc ? long_desc : _("Unset"));
                }

              gconf_value_destroy(value);
            }
          else
            {
              if (err == NULL)
                {
                  fprintf(stderr, _("No value set for `%s'\n"), *args);
                }
              else
                {
                  fprintf(stderr, _("Failed to get value for `%s': %s\n"),
                          *args, err->str);
                  gconf_error_destroy(err);
                  err = NULL;
                }
            }
 
          ++args;
        }
    }

  if (set_mode)
    {
      const gchar** args = poptGetArgs(ctx);

      if (args == NULL)
        {
          fprintf(stderr, _("Must specify alternating keys/values as arguments\n"));
          return 1;
        }

      while (*args)
        {
          const gchar* key;
          const gchar* value;
          GConfValueType type = GCONF_VALUE_INVALID;
          GConfValue* gval;

          key = *args;
          ++args;
          value = *args;

          if (value == NULL)
            {
              fprintf(stderr, _("No value to set for key: `%s'\n"), key);
              return 1;
            }

          switch (*value_type)
            {
            case 'i':
            case 'I':
              type = GCONF_VALUE_INT;
              break;
            case 'f':
            case 'F':
              type = GCONF_VALUE_FLOAT;
              break;
            case 'b':
            case 'B':
              type = GCONF_VALUE_BOOL;
              break;
            case 's':
            case 'S':
              type = GCONF_VALUE_STRING;
              break;
            default:
              fprintf(stderr, _("Don't understand type `%s'\n"), value_type);
              return 1;
              break;
            }
          
          err = NULL;

          gval = gconf_value_new_from_string(type, value, &err);

          if (gval == NULL)
            {
              fprintf(stderr, _("Error: %s\n"),
                      err->str);
              gconf_error_destroy(err);
              err = NULL;
              return 1;
            }

          err = NULL;
          
          gconf_set(conf, key, gval, &err);

          if (err != NULL)
            {
              fprintf(stderr, _("Error setting value: %s"),
                      err->str);
              gconf_error_destroy(err);
              err = NULL;
              return 1;
            }

          gconf_value_destroy(gval);

          ++args;
        }

      err = NULL;

      gconf_sync(conf, &err);

      if (err != NULL)
        {
          fprintf(stderr, _("Error syncing: %s"),
                  err->str);
          return 1;
        }
    }

  if (set_schema_mode)
    {
      const gchar** args = poptGetArgs(ctx);
      GConfSchema* sc;
      GConfValue* val;
      const gchar* key;
      
      if ((args == NULL) || (args[1] != NULL))
        {
          fprintf(stderr, _("Must specify key (schema name) as the only argument\n"));
          return 1;
        }
      
      key = *args;

      val = gconf_value_new(GCONF_VALUE_SCHEMA);

      sc = gconf_schema_new();

      gconf_value_set_schema_nocopy(val, sc);

      if (short_desc)
        gconf_schema_set_short_desc(sc, short_desc);

      if (long_desc)
        gconf_schema_set_long_desc(sc, long_desc);

      if (owner)
        gconf_schema_set_owner(sc, owner);

      if (value_type)
        {
          GConfValueType type = GCONF_VALUE_INVALID;

          switch (*value_type)
            {
            case 'i':
            case 'I':
              type = GCONF_VALUE_INT;
              break;
            case 'f':
            case 'F':
              type = GCONF_VALUE_FLOAT;
              break;
            case 'b':
            case 'B':
              type = GCONF_VALUE_BOOL;
              break;
            case 's':
            case 'S':
              switch (value_type[1])
                {
                case 't':
                case 'T':
                  type = GCONF_VALUE_STRING;
                  break;
                case 'c':
                case 'C':
                  type = GCONF_VALUE_SCHEMA;
                  break;
                default:
                  fprintf(stderr, _("Don't understand type `%s'\n"), value_type);
                }
              break;
            default:
              fprintf(stderr, _("Don't understand type `%s'\n"), value_type);
              break;
            }

          if (type != GCONF_VALUE_INVALID)
            gconf_schema_set_type(sc, type);
        }

      err = NULL;
      
      gconf_set(conf, key, val, &err);
      
      if (err != NULL)
        {
          fprintf(stderr, _("Error setting value: %s"),
                  err->str);
          gconf_error_destroy(err);
          err = NULL;
          return 1;
        }
      
      gconf_value_destroy(val);

      err = NULL;
      gconf_sync(conf, &err);
      
      if (err != NULL)
        {
          fprintf(stderr, _("Error syncing: %s"),
                  err->str);
          gconf_error_destroy(err);
          err = NULL;
          return 1;
        }
    }

  if (all_entries_mode)
    {
      const gchar** args = poptGetArgs(ctx);

      if (args == NULL)
        {
          fprintf(stderr, _("Must specify one or more dirs to get key/value pairs from.\n"));
          return 1;
        }

      do_all_pairs(conf, args);
    }

  if (unset_mode)
    {
      const gchar** args = poptGetArgs(ctx);

      if (args == NULL)
        {
          fprintf(stderr, _("Must specify one or more keys to unset.\n"));
          return 1;
        }

      while (*args)
        {
          err = NULL;
          gconf_unset(conf, *args, &err);

          if (err != NULL)
            {
              fprintf(stderr, _("Error unsetting `%s': %s\n"),
                      *args, err->str);
              gconf_error_destroy(err);
              err = NULL;
            }

          ++args;
        }

      err = NULL;
      gconf_sync(conf, NULL); /* ignore errors */
    }

  if (all_subdirs_mode)
    {
      const gchar** args = poptGetArgs(ctx);

      if (args == NULL)
        {
          fprintf(stderr, _("Must specify one or more dirs to get subdirs from.\n"));
          return 1;
        }
      
      while (*args)
        {
          GSList* subdirs;
          GSList* tmp;

          err = NULL;

          subdirs = gconf_all_dirs(conf, *args, &err);
          
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
          else
            {
              if (err != NULL)
                {
                  fprintf(stderr, _("Error listing dirs: %s\n"),
                          err->str);
                  gconf_error_destroy(err);
                  err = NULL;
                }
            }
 
          ++args;
        }
    }

  if (recursive_list)
    {
      const gchar** args = poptGetArgs(ctx);

      if (args == NULL)
        {
          fprintf(stderr, _("Must specify one or more dirs to recursively list.\n"));
          return 1;
        }

      do_recursive_list(conf, args);
    }

  poptFreeContext(ctx);

  gconf_engine_unref(conf);

  if (shutdown_gconfd)
    {
      err = NULL;
      gconf_shutdown_daemon(&err);
    }
      
  if (err != NULL)
    {
      fprintf(stderr, _("Shutdown error: %s\n"),
              err->str);
      gconf_error_destroy(err);
      err = NULL;
    }

  return 0;
}

static void 
recurse_subdir_list(GConfEngine* conf, GSList* subdirs, const gchar* parent, guint depth)
{
  GSList* tmp;
  gchar* whitespace;

  whitespace = g_strnfill(depth, ' ');

  tmp = subdirs;
  
  while (tmp != NULL)
    {
      gchar* s = tmp->data;
      gchar* full = gconf_concat_key_and_dir(parent, s);
      
      printf("%s%s:\n", whitespace, s);
      
      list_pairs_in_dir(conf, full, depth);

      recurse_subdir_list(conf, gconf_all_dirs(conf, full, NULL), full, depth+1);

      g_free(s);
      g_free(full);
      
      tmp = g_slist_next(tmp);
    }
  
  g_slist_free(subdirs);
  g_free(whitespace);
}

static void
do_recursive_list(GConfEngine* conf, const gchar** args)
{
  while (*args)
    {
      GSList* subdirs;

      subdirs = gconf_all_dirs(conf, *args, NULL);

      list_pairs_in_dir(conf, *args, 0);
          
      recurse_subdir_list(conf, subdirs, *args, 1);
 
      ++args;
    }
}

static void 
list_pairs_in_dir(GConfEngine* conf, const gchar* dir, guint depth)
{
  GSList* pairs;
  GSList* tmp;
  gchar* whitespace;
  GConfError* err = NULL;
  
  whitespace = g_strnfill(depth, ' ');

  pairs = gconf_all_entries(conf, dir, &err);
          
  if (err != NULL)
    {
      fprintf(stderr, _("Failure listing pairs in `%s': %s"),
              dir, err->str);
      gconf_error_destroy(err);
      err = NULL;
    }

  if (pairs != NULL)
    {
      tmp = pairs;

      while (tmp != NULL)
        {
          GConfEntry* pair = tmp->data;
          gchar* s;

          s = gconf_value_to_string(pair->value);

          printf(" %s%s = %s\n", whitespace, pair->key, s);

          g_free(s);
                  
          gconf_entry_destroy(pair);

          tmp = g_slist_next(tmp);
        }

      g_slist_free(pairs);
    }

  g_free(whitespace);
}

static void 
do_all_pairs(GConfEngine* conf, const gchar** args)
{      
  while (*args)
    {
      list_pairs_in_dir(conf, *args, 0);
      ++args;
    }
}

