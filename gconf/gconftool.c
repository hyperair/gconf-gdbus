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
#include "gconf-internals.h"
#include <stdio.h>
#include <unistd.h>
#include <popt.h>
#include <gnome-xml/tree.h>
#include <gnome-xml/parser.h>
#include <stdlib.h>
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
static char* schema_file = NULL;
static char* config_source = NULL;
static int use_local_source = FALSE;
static int makefile_install_mode = FALSE;
static int break_key_mode = FALSE;
static int break_dir_mode = FALSE;

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
    '\0',
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
    "install-schema-file",
    '\0',
    POPT_ARG_STRING,
    &schema_file,
    0,
    N_("Specify a schema file to be installed"),
    N_("FILENAME")
  },
  {
    "config-source",
    '\0',
    POPT_ARG_STRING,
    &config_source,
    0,
    N_("Specify a configuration source to use rather than the default path"),
    N_("SOURCE")
  },
  {
    "direct",
    '\0',
    POPT_ARG_NONE,
    &use_local_source,
    0,
    N_("Access the config database directly, bypassing server. Requires that gconfd is not running."),
    NULL
  },
  {
    "makefile-install-rule",
    '\0',
    POPT_ARG_NONE,
    &makefile_install_mode,
    0,
    N_("Properly installs schema files on the command line into the database. GCONF_CONFIG_SOURCE environment variable should be set to a non-default config source or set to the empty string to use the default."),
    NULL
  },
  {
    "break-key",
    '\0',
    POPT_ARG_NONE,
    &break_key_mode,
    0,
    N_("Torture-test an application by setting and unsetting a bunch of values of different types for keys on the command line."),
    NULL
  },
  {
    "break-directory",
    '\0',
    POPT_ARG_NONE,
    &break_dir_mode,
    0,
    N_("Torture-test an application by setting and unsetting a bunch of keys inside the directories on the command line."),
    NULL
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

static int do_break_key(GConfEngine* conf, const gchar** args);
static int do_break_directory(GConfEngine* conf, const gchar** args);
static int do_makefile_install(GConfEngine* conf, const gchar** args);
static int do_recursive_list(GConfEngine* conf, const gchar** args);
static int do_all_pairs(GConfEngine* conf, const gchar** args);
static void list_pairs_in_dir(GConfEngine* conf, const gchar* dir, guint depth);
static gboolean do_dir_exists(GConfEngine* conf, const gchar* dir);
static void do_spawn_daemon(GConfEngine* conf);
static int do_get(GConfEngine* conf, const gchar** args);
static int do_set(GConfEngine* conf, const gchar** args);
static int do_set_schema(GConfEngine* conf, const gchar** args);
static int do_all_entries(GConfEngine* conf, const gchar** args);
static int do_unset(GConfEngine* conf, const gchar** args);
static int do_all_subdirs(GConfEngine* conf, const gchar** args);
static int do_load_schema_file(GConfEngine* conf, const gchar* file);

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
                      spawn_gconfd || dir_exists || schema_file || makefile_install_mode ||
                      break_key_mode || break_dir_mode))
    {
      fprintf(stderr, _("Ping option must be used by itself.\n"));
      return 1;
    }

  if (dir_exists && (shutdown_gconfd || set_mode || get_mode || unset_mode ||
                     all_subdirs_mode || all_entries_mode || recursive_list || 
                     spawn_gconfd || schema_file || makefile_install_mode ||
                     break_key_mode || break_dir_mode))
    {
      fprintf(stderr, _("--dir-exists option must be used by itself.\n"));
      return 1;
    }

  if (schema_file && (shutdown_gconfd || set_mode || get_mode || unset_mode ||
                      all_subdirs_mode || all_entries_mode || recursive_list || 
                      spawn_gconfd || dir_exists || makefile_install_mode ||
                      break_key_mode || break_dir_mode))
    {
      fprintf(stderr, _("--install-schema-file must be used by itself.\n"));
      return 1;
    }


  if (makefile_install_mode && (shutdown_gconfd || set_mode || get_mode || unset_mode ||
                                all_subdirs_mode || all_entries_mode || recursive_list || 
                                spawn_gconfd || dir_exists || schema_file ||
                                break_key_mode || break_dir_mode))
    {
      fprintf(stderr, _("--makefile-install-rule must be used by itself.\n"));
      return 1;
    }


  if (break_key_mode && (shutdown_gconfd || set_mode || get_mode || unset_mode ||
                                all_subdirs_mode || all_entries_mode || recursive_list || 
                                spawn_gconfd || dir_exists || schema_file ||
                                makefile_install_mode || break_dir_mode))
    {
      fprintf(stderr, _("--break-key must be used by itself.\n"));
      return 1;
    }

  
  if (break_dir_mode && (shutdown_gconfd || set_mode || get_mode || unset_mode ||
                                all_subdirs_mode || all_entries_mode || recursive_list || 
                                spawn_gconfd || dir_exists || schema_file ||
                                break_key_mode || makefile_install_mode))
    {
      fprintf(stderr, _("--break-directory must be used by itself.\n"));
      return 1;
    }

  
  if (use_local_source && config_source == NULL)
    {
      fprintf(stderr, _("You must specify a config source with --config-source when using --direct\n"));
      return 1;
    }
  
  if (!gconf_init(argc, argv, &err))
    {
      fprintf(stderr, _("Failed to init GConf: %s\n"), err->str);
      gconf_error_destroy(err);
      err = NULL;
      return 1;
    }

  /* Do this first, since we want to do only this if the user selected
     it. */
  if (ping_gconfd)
    {
      if (gconf_ping_daemon())
        return 0;
      else 
        return 2;
    }
  
  if (makefile_install_mode)
    {
      g_assert (config_source == NULL);

      /* Try the environment variable */
      config_source = g_getenv("GCONF_CONFIG_SOURCE");

      if (config_source == NULL)
        {
          fprintf(stderr, _("Must set the GCONF_CONFIG_SOURCE environment variable\n"));
          return 1;
        }

      if (*config_source == '\0')
        {
          /* Properly set, but set to nothing (use default source) */
          config_source = NULL;
        }
      
      if (gconf_ping_daemon())
        {
          /* Eventually you should be able to run gconfd as long as
             you're installing to a different database from the one
             it's using, but I don't trust the locking right now. */
          fprintf(stderr, _("Shouldn't run gconfd while installing new schema files.\nUse gconftool --shutdown to shut down the daemon, most safely while no applications are running\n(though things theoretically work if apps are running).\n"));
          return 1;
        }

      /* Race condition! gconfd could start up. But we do have locking
         as a second line of defense. */
      use_local_source = TRUE;
    }
  
  if (config_source == NULL)
    conf = gconf_engine_new();
  else
    {
      if (use_local_source)
        conf = gconf_engine_new_local(config_source, &err);
      else
        conf = gconf_engine_new_from_address(config_source, &err);
    }
  
  if (conf == NULL)
    {
      g_assert(err != NULL);
      fprintf(stderr, _("Failed to access configuration source(s): %s\n"), err->str);
      gconf_error_destroy(err);
      err = NULL;
      return 1;
    }
  else
    {
      g_assert(err == NULL);
    }
  
  g_assert(conf != NULL);
  
  if (dir_exists != NULL) 
    {
      gboolean success;

      success = do_dir_exists(conf, dir_exists);

      gconf_engine_unref(conf);
      
      if (success)
        return 0; /* TRUE */
      else
        return 2; /* FALSE */
    }

  if (schema_file != NULL)
    {
      gint retval;

      retval = do_load_schema_file(conf, schema_file);

      gconf_engine_unref(conf);

      return retval;
    }
  
  if (spawn_gconfd)
    {
      do_spawn_daemon(conf);
      /* don't exit, it's OK to have this along with other options
         (however, it's probably pointless) */
    }

  if (makefile_install_mode)
    {
      const gchar** args = poptGetArgs(ctx);
      gint retval = do_makefile_install(conf, args);

      gconf_engine_unref(conf);

      return retval;
    }

  if (break_key_mode)
    {
      const gchar** args = poptGetArgs(ctx);
      gint retval = do_break_key(conf, args);

      gconf_engine_unref(conf);

      return retval;
    }
  
  if (break_dir_mode)
    {
      const gchar** args = poptGetArgs(ctx);
      gint retval = do_break_directory(conf, args);

      gconf_engine_unref(conf);

      return retval;
    }
  
  if (get_mode)
    {
      const gchar** args = poptGetArgs(ctx);
      if (do_get(conf, args)  == 1)
        {
          gconf_engine_unref(conf);
          return 1;
        }
    }

  if (set_mode)
    {
      const gchar** args = poptGetArgs(ctx);
      if (do_set(conf, args) == 1)
        {
          gconf_engine_unref(conf);
          return 1;
        }
    }

  if (set_schema_mode)
    {
      const gchar** args = poptGetArgs(ctx);
      if (do_set_schema(conf, args) == 1)
        {
          gconf_engine_unref(conf);
          return 1;
        }
    }

  if (all_entries_mode)
    {
      const gchar** args = poptGetArgs(ctx);

      if (do_all_entries(conf, args) == 1)
        {
          gconf_engine_unref(conf);
          return 1;
        }
    }

  if (unset_mode)
    {
      const gchar** args = poptGetArgs(ctx);

      if (do_unset(conf, args) == 1)
        {
          gconf_engine_unref(conf);
          return 1;
        }
    }

  if (all_subdirs_mode)
    {
      const gchar** args = poptGetArgs(ctx);

      if (do_all_subdirs(conf, args) == 1)
        {
          gconf_engine_unref(conf);
          return 1;
        }
    }

  if (recursive_list)
    {
      const gchar** args = poptGetArgs(ctx);
      
      if (do_recursive_list(conf, args) == 1)
        {
          gconf_engine_unref(conf);
          return 1;
        }
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

static int
do_recursive_list(GConfEngine* conf, const gchar** args)
{
  if (args == NULL)
    {
      fprintf(stderr, _("Must specify one or more dirs to recursively list.\n"));
      return 1;
    }

  while (*args)
    {
      GSList* subdirs;

      subdirs = gconf_all_dirs(conf, *args, NULL);

      list_pairs_in_dir(conf, *args, 0);
          
      recurse_subdir_list(conf, subdirs, *args, 1);
 
      ++args;
    }

  return 0;
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

          if (pair->value)
                  s = gconf_value_to_string(pair->value);
          else
                  s = g_strdup(_("(no value set)"));
          
          printf(" %s%s = %s\n", whitespace, pair->key, s);

          g_free(s);
                  
          gconf_entry_destroy(pair);

          tmp = g_slist_next(tmp);
        }

      g_slist_free(pairs);
    }

  g_free(whitespace);
}

static int
do_all_pairs(GConfEngine* conf, const gchar** args)
{      
  while (*args)
    {
      list_pairs_in_dir(conf, *args, 0);
      ++args;
    }
  return 0;
}

static gboolean
do_dir_exists(GConfEngine* conf, const gchar* dir)
{
  GConfError* err = NULL;
  gboolean exists = FALSE;
  
  exists = gconf_dir_exists(conf, dir_exists, &err);
  
  if (err != NULL)
    {
      fprintf(stderr, "%s\n", err->str);
      gconf_error_destroy(err);
      err = NULL;
    }
  
  return exists;
}

static void
do_spawn_daemon(GConfEngine* conf)
{
  GConfError* err = NULL;

  if (!gconf_spawn_daemon(&err))
    {
      fprintf(stderr, _("Failed to spawn the config server (gconfd): %s\n"), 
              err->str);
      gconf_error_destroy(err);
      err = NULL;
    }
}

static int
do_get(GConfEngine* conf, const gchar** args)
{
  GConfError* err = NULL;

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
  return 0;
}

static int
do_set(GConfEngine* conf, const gchar** args)
{
  GConfError* err = NULL;
  
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

  gconf_suggest_sync(conf, &err);

  if (err != NULL)
    {
      fprintf(stderr, _("Error syncing: %s"),
              err->str);
      return 1;
    }

  return 0;
}

static int
do_set_schema(GConfEngine* conf, const gchar** args)
{
  GConfSchema* sc;
  GConfValue* val;
  const gchar* key;
  GConfError* err = NULL;
  
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
  gconf_suggest_sync(conf, &err);
      
  if (err != NULL)
    {
      fprintf(stderr, _("Error syncing: %s"),
              err->str);
      gconf_error_destroy(err);
      err = NULL;
      return 1;
    }

  return 0;
}

static int
do_all_entries(GConfEngine* conf, const gchar** args)
{
  if (args == NULL)
    {
      fprintf(stderr, _("Must specify one or more dirs to get key/value pairs from.\n"));
      return 1;
    }
  
  return do_all_pairs(conf, args);
}

static int
do_unset(GConfEngine* conf, const gchar** args)
{
  GConfError* err = NULL;
  
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
  gconf_suggest_sync(conf, NULL); /* ignore errors */

  return 0;
}


static int
do_all_subdirs(GConfEngine* conf, const gchar** args)
{
  GConfError* err = NULL;
  
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

  return 0;
}

/*
 * Schema stuff
 */

typedef struct _SchemaInfo SchemaInfo;

struct _SchemaInfo {
  gchar* key;
  gchar* owner;
  GSList* apply_to;
  GConfValueType type;
  GConfValue* global_default;
  GHashTable* hash;
  GConfEngine* conf;
};

static int
fill_default_from_string(SchemaInfo* info, const gchar* default_value,
                         GConfValue** retloc)
{
  g_return_val_if_fail(info->key != NULL, 1);
  g_return_val_if_fail(default_value != NULL, 1);

  switch (info->type)
    {
    case GCONF_VALUE_INVALID:
      fprintf(stderr, _("WARNING: invalid or missing type for schema (%s)\n"),
              info->key);
      break;

    case GCONF_VALUE_LIST:
    case GCONF_VALUE_PAIR:
    case GCONF_VALUE_SCHEMA:
      fprintf(stderr, _("WARNING: For now gconftool only knows how to install default values for simple types (int, string, bool, float)\n"));
      break;

    case GCONF_VALUE_STRING:
    case GCONF_VALUE_INT:
    case GCONF_VALUE_BOOL:
    case GCONF_VALUE_FLOAT:
      {
        GConfError* error = NULL;
        *retloc = gconf_value_new_from_string(info->type,
                                              default_value,
                                              &error);
        if (*retloc == NULL)
          {
            g_assert(error != NULL);

            fprintf(stderr, _("WARNING: Failed to parse default value `%s' for schema (%s)\n"), default_value, info->key);

            gconf_error_destroy(error);
            error = NULL;
          }
        else
          {
            g_assert(error == NULL);
          }
      }
      break;
      
    default:
      fprintf(stderr, _("WARNING: gconftool internal error, unknown GConfValueType\n"));
      break;
    }

  return 0;
}

static int
extract_global_info(xmlNodePtr node,
                    SchemaInfo* info)
{
  xmlNodePtr iter;
  char* default_value = NULL;
      
  iter = node->childs;

  while (iter != NULL)
    {
      if (iter->type == XML_ELEMENT_NODE)
        {
          char* tmp;
      
          if (strcmp(iter->name, "key") == 0)
            {
              tmp = xmlNodeGetContent(iter);
              if (tmp)
                {
                  info->key = g_strdup(tmp);
                  free(tmp);
                }
            }
          else if (strcmp(iter->name, "owner") == 0)
            {
              tmp = xmlNodeGetContent(iter);
              if (tmp)
                {
                  info->owner = g_strdup(tmp);
                  free(tmp);
                }
            }
          else if (strcmp(iter->name, "type") == 0)
            {
              tmp = xmlNodeGetContent(iter);
              if (tmp)
                {
                  info->type = gconf_value_type_from_string(tmp);
                  free(tmp);
                  if (info->type == GCONF_VALUE_INVALID)
                    fprintf(stderr, _("WARNING: failed to parse type name `%s'\n"),
                            tmp);
                }
            }
          else if (strcmp(iter->name, "default") == 0)
            {
              default_value = xmlNodeGetContent(iter);
            }
          else if (strcmp(iter->name, "locale") == 0)
            {
              ; /* ignore, this is parsed later after we have the global info */
            }
          else if (strcmp(iter->name, "applyto") == 0)
            {
              /* Add the contents to the list of nodes to apply to */
              tmp = xmlNodeGetContent(iter);

              if (tmp)
                {
                  info->apply_to = g_slist_prepend(info->apply_to, g_strdup(tmp));
                  free(tmp);
                }
              else
                fprintf(stderr, _("WARNING: empty <applyto> node"));
            }
          else
            fprintf(stderr, _("WARNING: node <%s> not understood below <schema>\n"),
                    iter->name);

        }
      
      iter = iter->next;
    }

  if (info->key == NULL)
    {
      fprintf(stderr, _("WARNING: no key specified for schema\n"));
      if (default_value != NULL)
        free(default_value);
      return 1;
    }

  g_assert(info->key != NULL);
  
  /* Have to do this last, because the type may come after the default
     value
  */
  if (default_value != NULL)
    {
      fill_default_from_string(info, default_value, &info->global_default);
      
      free(default_value);
      default_value = NULL;
    }

  return 0;
}

static int
process_locale_info(xmlNodePtr node, SchemaInfo* info)
{
  char* name;
  GConfSchema* schema;
  xmlNodePtr iter;
  
  name = xmlGetProp(node, "name");

  if (name == NULL)
    {
      fprintf(stderr, _("WARNING: <locale> node has no `name=\"locale\"' attribute, ignoring\n"));
      return 1;
    }

  if (g_hash_table_lookup(info->hash, name) != NULL)
    {
      fprintf(stderr, _("WARNING: multiple <locale> nodes for locale `%s', ignoring all past first\n"),
              name);
      free(name);
      return 1;
    }
  
  schema = gconf_schema_new();

  gconf_schema_set_locale(schema, name);

  free(name);

  /* Fill in the global info */
  if (info->global_default != NULL)
    gconf_schema_set_default_value(schema, info->global_default);
      
  if (info->type != GCONF_VALUE_INVALID)
    gconf_schema_set_type(schema, info->type);

  if (info->owner != NULL)
    gconf_schema_set_owner(schema, info->owner);


  /* Locale-specific info */
  iter = node->childs;
  
  while (iter != NULL)
    {
      if (iter->type == XML_ELEMENT_NODE)
        {
          if (strcmp(iter->name, "default") == 0)
            {
              GConfValue* val = NULL;
              char* tmp;

              tmp = xmlNodeGetContent(iter);

              if (tmp != NULL)
                {
                  fill_default_from_string(info, tmp, &val);
                  if (val != NULL)
                    gconf_schema_set_default_value_nocopy(schema, val);

                  free(tmp);
                }
            }
          else if (strcmp(iter->name, "short") == 0)
            {
              char* tmp;

              tmp = xmlNodeGetContent(iter);

              if (tmp != NULL)
                {
                  gconf_schema_set_short_desc(schema, tmp);
                  free(tmp);
                }
            }
          else if (strcmp(iter->name, "long") == 0)
            {
              char* tmp;

              tmp = xmlNodeGetContent(iter);

              if (tmp != NULL)
                {
                  gconf_schema_set_long_desc(schema, tmp);
                  free(tmp);
                }
            }
          else
            {
              fprintf(stderr, _("WARNING: Invalid node <%s> in a <locale> node\n"),
                      iter->name);
            }
        }
      
      iter = iter->next;
    }

  g_hash_table_insert(info->hash,
                      (gchar*)gconf_schema_locale(schema), /* cheat to save copying this string */
                      schema);

  return 0;
}

static void
hash_foreach(gpointer key, gpointer value, gpointer user_data)
{
  SchemaInfo* info;
  GConfSchema* schema;
  GConfError* error = NULL;
  
  info = user_data;
  schema = value;
  
  if (!gconf_set_schema(info->conf, info->key, schema, &error))
    {
      g_assert(error != NULL);

      fprintf(stderr, _("WARNING: failed to install schema `%s' locale `%s': %s\n"),
              info->key, gconf_schema_locale(schema), error->str);
      gconf_error_destroy(error);
      error = NULL;
    }
  else
    {
      g_assert(error == NULL);
      printf(_("Installed schema `%s' for locale `%s'\n"),
             info->key, gconf_schema_locale(schema));
    }
      
  gconf_schema_destroy(schema);
}


static int
process_key_list(GConfEngine* conf, const gchar* schema_name, GSList* keylist)
{
  GSList* tmp;
  GConfError* error = NULL;

  tmp = keylist;

  while (tmp != NULL)
    {
      if (!gconf_associate_schema(conf, tmp->data, schema_name,  &error))
        {
          g_assert(error != NULL);
          
          fprintf(stderr, _("WARNING: failed to associate schema `%s' with key `%s': %s\n"),
                  schema_name, (gchar*)tmp->data, error->str);
          gconf_error_destroy(error);
          error = NULL;
        }
      else
        {
          g_assert(error == NULL);
          printf(_("Attached schema `%s' to key `%s'\n"),
                 schema_name, (gchar*)tmp->data);
        }
          
      tmp = g_slist_next(tmp);
    }
  
  return 0;
}

static int
process_schema(GConfEngine* conf, xmlNodePtr node)
{
  xmlNodePtr iter;
  SchemaInfo info;

  info.key = NULL;
  info.type = GCONF_VALUE_INVALID;
  info.apply_to = NULL;
  info.owner = NULL;
  info.global_default = NULL;
  info.hash = g_hash_table_new(g_str_hash, g_str_equal);
  info.conf = conf;
  
  extract_global_info(node, &info);

  if (info.key == NULL || info.type == GCONF_VALUE_INVALID)
    {
      g_free(info.owner);

      if (info.global_default)
        gconf_value_destroy(info.global_default);

      return 1;
    }
  
  iter = node->childs;

  while (iter != NULL)
    {
      if (iter->type == XML_ELEMENT_NODE)
        {
          if (strcmp(iter->name, "key") == 0)
            ; /* nothing */
          else if (strcmp(iter->name, "owner") == 0)
            ;  /* nothing */
          else if (strcmp(iter->name, "type") == 0)
            ;  /* nothing */
          else if (strcmp(iter->name, "default") == 0)
            ;  /* nothing */
          else if (strcmp(iter->name, "applyto") == 0)
            ;  /* nothing */
          else if (strcmp(iter->name, "locale") == 0)
            {
              process_locale_info(iter, &info);
            }
          else
            fprintf(stderr, _("WARNING: node <%s> not understood below <schema>\n"),
                    iter->name);
        }
          
      iter = iter->next;
    }

  /* Attach schema to keys */

  process_key_list(conf, info.key, info.apply_to);

  if (g_hash_table_size(info.hash) == 0)
    {
      fprintf(stderr, _("You must have at least one <locale> entry in a <schema>\n"));
      return 1;
    }
      
  /* Now install each schema in the hash into the GConfEngine */
  g_hash_table_foreach(info.hash, hash_foreach, &info);

  g_hash_table_destroy(info.hash);

  g_free(info.key);
  g_free(info.owner);
  if (info.global_default)
    gconf_value_destroy(info.global_default);
  
  return 0;
}

static int
process_schema_list(GConfEngine* conf, xmlNodePtr node)
{
  xmlNodePtr iter;

  iter = node->childs;

  while (iter != NULL)
    {
      if (strcmp(iter->name, "schema") == 0)
        process_schema(conf, iter);
      else
        fprintf(stderr, _("WARNING: node <%s> not understood below <schemalist>\n"),
                iter->name);

      iter = iter->next;
    }

  return 0;
}

static int
do_load_schema_file(GConfEngine* conf, const gchar* file)
{
  xmlDocPtr doc;
  xmlNodePtr iter;
  GConfError * err = NULL;
  
  errno = 0;
  doc = xmlParseFile(file);

  if (doc == NULL)
    {
      if (errno != 0)
        fprintf(stderr, _("Failed to open `%s': %s\n"),
                file, strerror(errno));
      return 1;
    }

  if (doc->root == NULL)
    {
      fprintf(stderr, _("Document `%s' is empty?\n"),
              file);
      return 1;
    }

  iter = doc->root;
  while (iter != NULL) 
    {
      if (iter->type == XML_ELEMENT_NODE)
        { 
          if (strcmp(iter->name, "gconfschemafile") != 0)
            {
              fprintf(stderr, _("Document `%s' has the wrong type of root node (<%s>, should be <gconfschemafile>)\n"),
                      file, iter->name);
              return 1;
            }
          else
            break;
        }         

      iter = iter->next;
    }
  
  if (iter == NULL)
    {
      fprintf(stderr, _("Document `%s' has no top level <gconfschemafile> node\n"),
              file);
      return 1;
    }

  iter = iter->childs;

  while (iter != NULL)
    {
      if (iter->type == XML_ELEMENT_NODE)
        {
          if (strcmp(iter->name, "schemalist") == 0)
            process_schema_list(conf, iter);
          else
            fprintf(stderr, _("WARNING: node <%s> below <gconfschemafile> not understood\n"),
                    iter->name);
        }
          
      iter = iter->next;
    }

  gconf_suggest_sync(conf, &err);

  if (err != NULL)
    {
      fprintf(stderr, _("Error syncing config data: %s"),
              err->str);
      gconf_error_destroy(err);
      return 1;
    }
  
  return 0;
}

static int
do_makefile_install(GConfEngine* conf, const gchar** args)
{
  GConfError* err = NULL;
  
  if (args == NULL)
    {
      fprintf(stderr, _("Must specify some schema files to install\n"));
      return 1;
    }

  while (*args)
    {
      if (do_load_schema_file(conf, *args) != 0)
        return 1;

      ++args;
    }

  gconf_suggest_sync(conf, &err);

  if (err != NULL)
    {
      fprintf(stderr, _("Error syncing config data: %s"),
              err->str);
      gconf_error_destroy(err);
      return 1;
    }
  
  return 0;
}

typedef enum {
  BreakageSetBadValues,
  BreakageCleanup
} BreakagePhase;

static gboolean
check_err(GConfError** err)
{
  printf(".");
  
  if (*err != NULL)
    {
      fprintf(stderr, _("\n%s\n"),
              (*err)->str);
      gconf_error_destroy(*err);
      *err = NULL;
      return TRUE;
    }
  else
    return FALSE;
}

static gboolean
key_breakage(GConfEngine* conf, const gchar* key, BreakagePhase phase)
{
  GConfError* error = NULL;
  
  if (phase == BreakageCleanup)
    {
      gconf_unset(conf, key, &error);
      if (error != NULL)
        {
          fprintf(stderr, _("Failed to unset breakage key %s: %s\n"),
                  key, error->str);
          gconf_error_destroy(error);
          return FALSE;
        }
    }
  else if (phase == BreakageSetBadValues)
    {
      gint an_int = 43;
      gboolean a_bool = TRUE;
      gdouble a_float = 43695.435;
      const gchar* a_string = "Hello";
      GConfValue* val;
      GSList* list = NULL;
      
      printf("  +");
      
      gconf_set_string(conf, key, "", &error);
      if (check_err(&error))
        return FALSE;
      
      gconf_set_string(conf, key, "blah blah blah 93475028934670 @%^%$&%$&^%", &error);
      if (check_err(&error))
        return FALSE;
      
      gconf_set_bool(conf, key, TRUE, &error);
      if (check_err(&error))
        return FALSE;

      gconf_set_bool(conf, key, FALSE, &error);
      if (check_err(&error))
        return FALSE;

      gconf_set_float(conf, key, 100.0, &error);
      if (check_err(&error))
        return FALSE;

      gconf_set_float(conf, key, -100.0, &error);
      if (check_err(&error))
        return FALSE;

      gconf_set_float(conf, key, 0.0, &error);
      if (check_err(&error))
        return FALSE;

      gconf_set_int(conf, key, 0, &error);
      if (check_err(&error))
        return FALSE;

      gconf_set_int(conf, key, 5384750, &error);
      if (check_err(&error))
        return FALSE;
      
      gconf_set_int(conf, key, -11, &error);
      if (check_err(&error))
        return FALSE;

      gconf_set_list(conf, key, GCONF_VALUE_BOOL, list, &error);
      if (check_err(&error))
        return FALSE;

      gconf_set_pair(conf, key, GCONF_VALUE_INT, GCONF_VALUE_BOOL,
                     &an_int, &a_bool, &error);
      if (check_err(&error))
        return FALSE;

      gconf_set_pair(conf, key, GCONF_VALUE_FLOAT, GCONF_VALUE_STRING,
                     &a_float, &a_string, &error);
      if (check_err(&error))
        return FALSE;

      /* empty pair */
      val = gconf_value_new(GCONF_VALUE_PAIR);
      gconf_set(conf, key, val, &error);
      gconf_value_destroy(val);
      if (check_err(&error))
        return FALSE;

      list = NULL;
      gconf_set_list(conf, key, GCONF_VALUE_STRING, list, &error);
      if (check_err(&error))
        return FALSE;
      gconf_set_list(conf, key, GCONF_VALUE_INT, list, &error);
      if (check_err(&error))
        return FALSE;
      gconf_set_list(conf, key, GCONF_VALUE_BOOL, list, &error);
      if (check_err(&error))
        return FALSE;

      list = g_slist_prepend(list, GINT_TO_POINTER(10));
      list = g_slist_prepend(list, GINT_TO_POINTER(14));
      list = g_slist_prepend(list, GINT_TO_POINTER(-93));
      list = g_slist_prepend(list, GINT_TO_POINTER(1000000));
      list = g_slist_prepend(list, GINT_TO_POINTER(32));
      gconf_set_list(conf, key, GCONF_VALUE_INT, list, &error);
      if (check_err(&error))
        return FALSE;

      g_slist_free(list);
      list = NULL;

      list = g_slist_prepend(list, "");
      list = g_slist_prepend(list, "blah");
      list = g_slist_prepend(list, "");
      list = g_slist_prepend(list, "\n\t\r\n     \n");
      list = g_slist_prepend(list, "woo fooo s^%*^%&@^$@%&@%$");
      gconf_set_list(conf, key, GCONF_VALUE_STRING, list, &error);
      if (check_err(&error))
        return FALSE;

      g_slist_free(list);
      list = NULL;
      
      printf("\n");
    }
  else
    g_assert_not_reached();
      
  return TRUE;
}

static int
do_break_key(GConfEngine* conf, const gchar** args)
{
  if (args == NULL)
    {
      fprintf(stderr, _("Must specify some keys to break\n"));
      return 1;
    }
  
  while (*args)
    {
      printf(_("Trying to break your application by setting bad values for key:\n  %s\n"), *args);
      
      if (!key_breakage(conf, *args, BreakageSetBadValues))
        return 1;
      if (!key_breakage(conf, *args, BreakageCleanup))
        return 1;

      ++args;
    }

  return 0;
}

static int
do_break_directory(GConfEngine* conf, const gchar** args)
{
  if (args == NULL)
    {
      fprintf(stderr, _("Must specify some directories to break\n"));
      return 1;
    }
  
  while (*args)
    {
      gchar* keys[10] = { NULL };
      gchar* full_keys[10] = { NULL };
      int i;

      i = 0;
      while (i < 10)
        {
          keys[i] = gconf_unique_key();
          full_keys[i] = gconf_concat_key_and_dir(*args, keys[i]);

          ++i;
        }

      printf(_("Trying to break your application by setting bad values for keys in directory:\n  %s\n"), *args);

      i = 0;
      while (i < 10)
        {
          if (!key_breakage(conf, full_keys[i], BreakageSetBadValues))
            return 1;

          ++i;
        }
      
      i = 0;
      while (i < 10)
        {
          if (!key_breakage(conf, full_keys[i], BreakageCleanup))
            return 1;
          
          ++i;
        }

      i = 0;
      while (i < 10)
        {
          g_free(keys[i]);
          g_free(full_keys[i]);

          ++i;
        }
      
      ++args;
    }

  return 0;
}
