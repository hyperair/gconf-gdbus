/* GConf
 * Copyright (C) 2002 Red Hat Inc.
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

#include <glib.h>
#include <gconf/gconf-internals.h>
#include <gconf/gconf-schema.h>
#include "markup-tree.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>


typedef struct
{
  char       *locale;
  char       *short_desc;
  char       *long_desc;
  GConfValue *default_value;
} LocalSchemaInfo;

struct _MarkupEntry
{
  MarkupDir  *dir;
  char       *name;
  GConfValue *value;
  /* list of LocalSchemaInfo */
  GSList     *local_schemas;
  char       *schema_name;
  char       *mod_user;
  GTime       mod_time;
};

static LocalSchemaInfo* local_schema_info_new  (void);
static void             local_schema_info_free (LocalSchemaInfo *info);


static MarkupDir* markup_dir_new        (MarkupTree *tree,
                                         MarkupDir  *parent,
                                         const char *name);
static void       markup_dir_free       (MarkupDir  *dir);
static gboolean   markup_dir_needs_sync (MarkupDir  *dir);
static gboolean   markup_dir_sync       (MarkupDir  *dir,
                                         GError    **err);
static char*      markup_dir_build_path (MarkupDir  *dir,
                                         gboolean    with_data_file);

static MarkupEntry* markup_entry_new             (MarkupDir   *dir,
                                                  const char  *name);
static void         markup_entry_free            (MarkupEntry *entry);
static void         markup_entry_set_mod_user    (MarkupEntry *entry,
                                                  const char  *muser);
static void         markup_entry_set_mod_time    (MarkupEntry *entry,
                                                  GTime        mtime);
static void         markup_entry_set_schema_name (MarkupEntry *entry,
                                                  const char  *schema_name);

static GSList* parse_entries (const char *filename,
                              GError    **err);

struct _MarkupTree
{
  char *dirname;
  guint dir_mode;
  guint file_mode;

  MarkupDir *root;

  guint read_only : 1;
};

MarkupTree*
markup_tree_new (const char *root_dir,
                 guint       dir_mode,
                 guint       file_mode,
                 gboolean    read_only)
{
  MarkupTree *tree;

  tree = g_new0 (MarkupTree, 1);

  tree->dirname = g_strdup (root_dir);
  tree->dir_mode = dir_mode;
  tree->file_mode = file_mode;
  tree->read_only = read_only;

  tree->root = markup_dir_new (tree, NULL, "/");

  return tree;
}

void
markup_tree_free (MarkupTree *tree)
{
  g_free (tree->dirname);

  g_free (tree);
}

struct _MarkupDir
{
  MarkupTree *tree;
  MarkupDir *parent;
  char *name;

  GSList *entries;
  GSList *subdirs;

  /* Have read the existing XML file */
  guint entries_loaded : 1;
  /* Need to rewrite the XML file since we changed
   * it
   */
  guint entries_need_save : 1;

  /* Have read the existing directories */
  guint subdirs_loaded : 1;

  /* Some subdirs added/removed */
  guint subdirs_added_or_removed : 1;
};

static MarkupDir*
markup_dir_new (MarkupTree *tree,
                MarkupDir  *parent,
                const char *name)
{
  MarkupDir *dir;

  dir = g_new0 (MarkupDir, 1);

  dir->name = g_strdup (name);
  dir->tree = tree;
  dir->parent = parent;

  return dir;
}

static void
markup_dir_free (MarkupDir *dir)
{
  g_free (dir->name);

  g_free (dir);
}

static MarkupDir*
markup_tree_get_dir_internal (MarkupTree *tree,
                              const char *full_key,
                              gboolean    create_if_not_found,
                              GError    **err)
{
  char **components;
  int i;
  MarkupDir *dir;
  
  g_return_val_if_fail (*full_key == '/', NULL);

  /* Split without leading '/' */
  components = g_strsplit (full_key + 1, "/", -1);

  dir = tree->root;

  if (components) /* if components == NULL the root dir was requested */
    {
      i = 0;
      while (components[i])
        {
          MarkupDir *subdir;
          GError *tmp_err;

          tmp_err = NULL;

          if (create_if_not_found)
            subdir = markup_dir_ensure_subdir (dir, components[i], &tmp_err);
          else
            subdir = markup_dir_lookup_subdir (dir, components[i], &tmp_err);

          if (tmp_err != NULL)
            {
              dir = NULL;
              g_propagate_error (err, tmp_err);
              goto out;
            }

          if (subdir)
            {
              /* Descend one level */
              dir = subdir;
            }
          else
            {
              dir = NULL;
              goto out;
            }
          
          ++i;
        }
    }

 out:
  g_strfreev (components);

  return dir;
}

MarkupDir*
markup_tree_lookup_dir (MarkupTree *tree,
                        const char *full_key,
                        GError    **err)
     
{
  return markup_tree_get_dir_internal (tree, full_key, FALSE, err);
}

MarkupDir*
markup_tree_ensure_dir (MarkupTree *tree,
                        const char *full_key,
                        GError    **err)
{
  return markup_tree_get_dir_internal (tree, full_key, TRUE, err);  
}

static gboolean
load_entries (MarkupDir *dir)
{
  /* Load the entries in this directory */
  char *markup_file;
  GSList *entries;
  GError *tmp_err;
  GSList *tmp;
  
  if (dir->entries_loaded)
    return TRUE;

  /* We mark it loaded even if the next stuff
   * fails, because we don't want to keep trying and
   * failing, plus we have invariants
   * that assume entries_loaded is TRUE once we've
   * called load_entries()
   */
  dir->entries_loaded = TRUE;
  
  markup_file = markup_dir_build_path (dir, TRUE);

  tmp_err = NULL;
  entries = parse_entries (markup_file, &tmp_err);

  if (tmp_err)
    {
      g_assert (entries == NULL);
      
      gconf_log (GCL_WARNING,
                 _("Failed to load file \"%s\": %s"),
                 markup_file, tmp_err->message);
      g_error_free (tmp_err);
      g_free (markup_file);
      return FALSE;
    }

  g_free (markup_file);

  g_assert (dir->entries == NULL);
  dir->entries = entries;

  /* Fill in entry->dir */
  tmp = dir->entries;
  while (tmp != NULL)
    {
      MarkupEntry *entry = tmp->data;

      entry->dir = dir;
      
      tmp = tmp->next;
    }
  
  return TRUE;
}

static gboolean
load_subdirs (MarkupDir *dir)
{  
  DIR* dp;
  struct dirent* dent;
  struct stat statbuf;
  GSList* retval = NULL;
  gchar* fullpath;
  gchar* fullpath_end;
  guint len;
  guint subdir_len;
  char *markup_dir;
  
  if (dir->subdirs_loaded)
    return TRUE;

  /* We mark it loaded even if the next stuff
   * fails, because we don't want to keep trying and
   * failing, plus we have invariants
   * that assume subdirs_loaded is TRUE once we've
   * called load_subdirs()
   */
  dir->subdirs_loaded = TRUE;

  g_assert (dir->subdirs == NULL);

  markup_dir = markup_dir_build_path (dir, FALSE);
  
  dp = opendir (markup_dir);
  
  if (dp == NULL)
    {
      gconf_log (GCL_WARNING,
                 _("Could not open directory \"%s\": %s\n"),
                 /* strerror, in locale encoding */
                 markup_dir, strerror (errno));
      g_free (markup_dir);
      return FALSE;
    }

  len = strlen (markup_dir);
  
  subdir_len = PATH_MAX - len;
  
  fullpath = g_new0 (char, subdir_len + len + 2); /* ensure null termination */
  strcpy (fullpath, markup_dir);
  
  fullpath_end = fullpath + len;
  if (*(fullpath_end - 1) != '/')
    {
      *fullpath_end = '/';
      ++fullpath_end;
    }

  while ((dent = readdir (dp)) != NULL)
    {
      /* ignore ., .., and all dot-files */
      if (dent->d_name[0] == '.')
        continue;
      
      len = strlen (dent->d_name);
      
      if (len < subdir_len)
        {
          strcpy (fullpath_end, dent->d_name);
          strncpy (fullpath_end+len, "/%gconf.xml", subdir_len - len);
        }
      else
        continue; /* Shouldn't ever happen since PATH_MAX is available */
      
      if (stat (fullpath, &statbuf) < 0)
        {
          /* This is some kind of cruft, not an XML directory */
          continue;
        }

      retval = g_slist_prepend (retval,
                                markup_dir_new (dir->tree, dir, dent->d_name));
    }

  /* if this fails, we really can't do a thing about it
   * and it's not a meaningful error
   */
  closedir (dp);

  dir->subdirs = retval;

  g_free (fullpath);
  g_free (markup_dir);

  return TRUE;
}

MarkupEntry*
markup_dir_lookup_entry (MarkupDir   *dir,
                         const char  *relative_key,
                         GError     **err)
{
  GSList *tmp;

  load_entries (dir);
  
  tmp = dir->entries;
  while (tmp != NULL)
    {
      MarkupEntry *entry = tmp->data;

      if (strcmp (relative_key, entry->name) == 0)
        return entry;
      
      tmp = tmp->next;
    }

  return NULL;
}

MarkupEntry*
markup_dir_ensure_entry (MarkupDir   *dir,
                         const char  *relative_key,
                         GError     **err)
{
  MarkupEntry *entry;
  GError *tmp_err;
  
  tmp_err = NULL;
  entry = markup_dir_lookup_entry (dir, relative_key, &tmp_err);
  if (tmp_err != NULL)
    {
      g_propagate_error (err, tmp_err);
      return NULL;
    }
  
  if (entry != NULL)
    return entry;

  /* Create a new entry */
  entry = markup_entry_new (dir, relative_key);
  dir->entries = g_slist_prepend (dir->entries, entry);

  /* Need to save this */
  dir->entries_need_save = TRUE;

  return entry;
}

MarkupDir*
markup_dir_lookup_subdir (MarkupDir   *dir,
                          const char  *relative_key,
                          GError     **err)
{
  GSList *tmp;
  
  load_subdirs (dir);

  tmp = dir->subdirs;
  while (tmp != NULL)
    {
      MarkupDir *subdir = tmp->data;

      if (strcmp (subdir->name, relative_key) == 0)
        return subdir;

      tmp = tmp->next;
    }

  return NULL;
}

MarkupDir*
markup_dir_ensure_subdir (MarkupDir   *dir,
                          const char  *relative_key,
                          GError     **err)
{
  MarkupDir *subdir;
  GError *tmp_err;
  
  tmp_err = NULL;
  subdir = markup_dir_lookup_subdir (dir, relative_key, &tmp_err);
  if (tmp_err != NULL)
    {
      g_propagate_error (err, tmp_err);
      return NULL;
    }

  if (subdir == NULL)
    {
      subdir = markup_dir_new (dir->tree, dir, relative_key);

      dir->subdirs = g_slist_prepend (dir->subdirs,
                                      subdir);
    }

  return subdir;
}

GSList*
markup_dir_list_entries (MarkupDir   *dir,
                         GError     **err)
{
  load_entries (dir);

  return dir->entries;
}

GSList*
markup_dir_list_subdirs (MarkupDir   *dir,
                         GError     **err)
{
  load_subdirs (dir);

  return dir->subdirs;
}

static gboolean
markup_dir_needs_sync (MarkupDir *dir)
{
  /* Never write to read-only tree
   * (it shouldn't get marked dirty, but this
   * is here as a safeguard)
   */
  if (dir->tree->read_only)
    return FALSE;

  return dir->entries_need_save || dir->subdirs_added_or_removed;
}

static gboolean
markup_dir_sync (MarkupDir  *dir,
                 GError    **err)
{
  /* - Clean up local_schema */
  /* - never delete root dir, always delete other empty dirs */

}

static char*
markup_dir_build_path (MarkupDir  *dir,
                       gboolean    with_data_file)
{
  GString *name;
  GSList *components;
  GSList *tmp;
  MarkupDir *iter;

  components = NULL;
  iter = dir;
  while (iter != NULL)
    {
      components = g_slist_prepend (components, iter->name);
      iter = iter->parent;
    }

  name = g_string_new (dir->tree->dirname);
  tmp = components;
  while (tmp != NULL)
    {
      const char *comp = tmp->data;

      if (*comp != '/')
        g_string_append_c (name, '/');

      g_string_append (name, comp);

      tmp = tmp->next;
    }

  g_slist_free (components);

  if (with_data_file)
    g_string_append (name, "/%gconf.xml");

  return g_string_free (name, FALSE);
}

/*
 * MarkupEntry
 */

static MarkupEntry*
markup_entry_new (MarkupDir  *dir,
                  const char *name)
{
  MarkupEntry *entry;

  entry = g_new0 (MarkupEntry, 1);

  /* "dir" may be NULL during %gconf.xml parsing */

  entry->dir = dir;
  entry->name = g_strdup (name);

  return entry;
}

static void
markup_entry_free (MarkupEntry *entry)
{
  g_free (entry->name);
  if (entry->value)
    gconf_value_free (entry->value);
  g_free (entry->schema_name);
  g_free (entry->mod_user);

  g_slist_foreach (entry->local_schemas,
                   (GFunc) local_schema_info_free,
                   NULL);

  g_slist_free (entry->local_schemas);

  g_free (entry);
}

void
markup_entry_set_value (MarkupEntry       *entry,
                        const GConfValue  *value,
                        GError           **err)
{
  /* We have to have loaded entries, because
   * someone called ensure_entry to get this
   * entry.
   */
  g_return_if_fail (entry->dir != NULL);
  g_return_if_fail (entry->dir->entries_loaded);

  if (value->type != GCONF_VALUE_SCHEMA)
    {
      if (entry->value == value)
        return;

      if (entry->value)
        gconf_value_free (entry->value);

      entry->value = gconf_value_copy (value);

      /* Dump these if they exist, we aren't a schema anymore */
      if (entry->local_schemas)
        {
          g_slist_foreach (entry->local_schemas,
                           (GFunc) local_schema_info_free,
                           NULL);
          g_slist_free (entry->local_schemas);
          entry->local_schemas = NULL;
        }
    }
  else
    {
      /* For schema entries, we put the localized info
       * in a LocalSchemaInfo, and the other info
       * in the schema in the GConfValue
       */
      GSList *tmp;
      LocalSchemaInfo *local_schema;
      GConfSchema *schema;
      const char *locale;
      GConfSchema *current_schema;
      GConfValue *def_value;

      schema = gconf_value_get_schema (value);
      g_assert (schema);

      locale = gconf_schema_get_locale (schema);
      if (locale == NULL)
        locale = "C";

      local_schema = NULL;
      tmp = entry->local_schemas;
      while (tmp != NULL)
        {
          LocalSchemaInfo *lsi;

          lsi = tmp->data;

          if (strcmp (lsi->locale, locale) == 0)
            {
              local_schema = lsi;
              break;
            }

          tmp = tmp->next;
        }

      if (local_schema == NULL)
        {
          /* Didn't find a value for locale, make a new entry in the list */
          local_schema = local_schema_info_new ();
          local_schema->locale = g_strdup (locale);
          entry->local_schemas =
            g_slist_prepend (entry->local_schemas, local_schema);
        }

      if (local_schema->short_desc)
        g_free (local_schema->short_desc);
      if (local_schema->long_desc)
        g_free (local_schema->long_desc);
      if (local_schema->default_value)
        gconf_value_free (local_schema->default_value);

      local_schema->short_desc = g_strdup (gconf_schema_get_short_desc (schema));
      local_schema->long_desc = g_strdup (gconf_schema_get_long_desc (schema));
      def_value = gconf_schema_get_default_value (schema);
      if (def_value)
        local_schema->default_value = gconf_value_copy (def_value);
      else
        local_schema->default_value = NULL;

      /* When saving, we will check that the type of default_value is
       * consistent with the type in the entry->value schema, so that
       * we don't save something we can't load. We'll drop any
       * LocalSchemaInfo with the wrong type default value at that
       * time, more efficient than dropping it now.
       */
      if (entry->value->type != GCONF_VALUE_SCHEMA)
        {
          gconf_value_free (entry->value);
          entry->value = NULL;
        }

      if (entry->value == NULL)
        {
          entry->value = gconf_value_new (GCONF_VALUE_SCHEMA);
          current_schema = gconf_schema_new ();
          gconf_value_set_schema_nocopy (entry->value, current_schema);
        }
      else
        {
          current_schema = gconf_value_get_schema (entry->value);
        }

      /* Don't save localized info in the main schema */
      gconf_schema_set_locale (current_schema, NULL);
      gconf_schema_set_short_desc (current_schema, NULL);
      gconf_schema_set_long_desc (current_schema, NULL);

      /* But everything else goes in the main schema */
      gconf_schema_set_list_type (current_schema,
                                  gconf_schema_get_list_type (schema));
      gconf_schema_set_car_type (current_schema,
                                 gconf_schema_get_car_type (schema));
      gconf_schema_set_cdr_type (current_schema,
                                 gconf_schema_get_cdr_type (schema));
      gconf_schema_set_type (current_schema,
                             gconf_schema_get_type (schema));
      gconf_schema_set_owner (current_schema,
                              gconf_schema_get_owner (schema));
    }

  entry->dir->entries_need_save = TRUE;
}

GConfValue*
markup_entry_get_value (MarkupEntry *entry,
                        const char **locales,
                        GError     **err)
{
  /* We have to have loaded entries, because
   * someone called ensure_entry to get this
   * entry.
   */
  g_return_val_if_fail (entry->dir != NULL, NULL);
  g_return_val_if_fail (entry->dir->entries_loaded, NULL);

  if (entry->value == NULL)
    {
      return NULL;
    }
  else if (entry->value->type != GCONF_VALUE_SCHEMA)
    {
      return gconf_value_copy (entry->value);
    }
  else
    {
      GConfValue *retval;
      GConfSchema *schema;
      int n_locales;
      static const char *fallback_locales[2] = {
        "C", NULL
      };
      LocalSchemaInfo **local_schemas;
      LocalSchemaInfo *best;
      LocalSchemaInfo *c_local_schema;
      GSList *tmp;
      int i;

      retval = gconf_value_copy (entry->value);
      schema = gconf_value_get_schema (retval);
      g_return_val_if_fail (schema != NULL, NULL);

      /* Find the best local schema */

      if (locales == NULL && locales[0] == NULL)
        locales = fallback_locales;

      n_locales = 0;
      while (locales[n_locales])
        ++n_locales;

      local_schemas = g_new0 (LocalSchemaInfo*, n_locales);
      c_local_schema = NULL;

      tmp = entry->local_schemas;
      while (tmp != NULL)
        {
          LocalSchemaInfo *lsi = tmp->data;

          if (strcmp (lsi->locale, "C") == 0)
            c_local_schema = lsi;

          i = 0;
          while (locales[i])
            {
              if (strcmp (locales[i], lsi->locale) == 0)
                {
                  local_schemas[i] = lsi;
                  break;
                }
              ++i;
            }

          /* Quit as soon as we have the best possible locale */
          if (local_schemas[0] != NULL)
            break;

          tmp = tmp->next;
        }

      i = 0;
      best = local_schemas[i];
      while (best == NULL && i < n_locales)
        {
          best = local_schemas[i];
          ++i;
        }

      g_free (local_schemas);

      /* If we found localized info, add it to the return value,
       * fall back to C locale if we can
       */

      if (best->default_value)
        gconf_schema_set_default_value (schema, best->default_value);
      else if (c_local_schema && c_local_schema->default_value)
        gconf_schema_set_default_value (schema, c_local_schema->default_value);

      if (best->short_desc)
        gconf_schema_set_short_desc (schema, best->short_desc);
      else if (c_local_schema && c_local_schema->short_desc)
        gconf_schema_set_short_desc (schema, c_local_schema->short_desc);

      if (best->long_desc)
        gconf_schema_set_long_desc (schema, best->long_desc);
      else if (c_local_schema && c_local_schema->long_desc)
        gconf_schema_set_long_desc (schema, c_local_schema->long_desc);

      return retval;
    }
}

static void
markup_entry_set_mod_user (MarkupEntry *entry,
                           const char  *muser)
{
  if (muser == entry->mod_user)
    return;

  g_free (entry->mod_user);
  entry->mod_user = g_strdup (muser);
}

static void
markup_entry_set_mod_time (MarkupEntry *entry,
                           GTime        mtime)
{
  entry->mod_time = mtime;
}

static void
markup_entry_set_schema_name (MarkupEntry *entry,
                              const char  *schema_name)
{
  if (schema_name == entry->schema_name)
    return;

  g_free (entry->schema_name);
  entry->schema_name = g_strdup (schema_name);
}

/*
 * Parser
 */


/* The GConf XML format is on a lot of crack. When I wrote it,
 * I didn't know what I was doing, and now we're stuck with it.
 * Apologies.
 */

typedef enum
{
  STATE_START,
  STATE_GCONF,
  STATE_ENTRY,
  STATE_STRINGVALUE,
  STATE_LONGDESC,

  STATE_LOCAL_SCHEMA,

  /* these all work just like <entry> in storing a value but have no
   * name/muser/mtime/owner and in the case of car/cdr/li can only
   * store primitive values.
   */

  STATE_DEFAULT,
  STATE_CAR,
  STATE_CDR,
  STATE_LI
} ParseState;

typedef struct
{
  GSList *states;
  MarkupEntry *current_entry;
  GSList *value_stack;
  GSList *value_freelist;

  /* Collected while parsing a schema entry */
  GSList *local_schemas;

  GSList *complete_entries;

} ParseInfo;

static void set_error (GError             **err,
                       GMarkupParseContext *context,
                       int                  error_code,
                       const char          *format,
                       ...) G_GNUC_PRINTF (4, 5);

static void add_context_to_error (GError             **err,
                                  GMarkupParseContext *context);

static void          parse_info_init        (ParseInfo    *info);
static void          parse_info_free        (ParseInfo    *info);

static void       push_state (ParseInfo  *info,
                              ParseState  state);
static void       pop_state  (ParseInfo  *info);
static ParseState peek_state (ParseInfo  *info);

static void        value_stack_push (ParseInfo  *info,
                                     GConfValue *value,
                                     gboolean    add_to_freelist);
static GConfValue* value_stack_peek (ParseInfo  *info);
static void        value_stack_pop  (ParseInfo  *info);


static void start_element_handler (GMarkupParseContext  *context,
                                   const gchar          *element_name,
                                   const gchar         **attribute_names,
                                   const gchar         **attribute_values,
                                   gpointer              user_data,
                                   GError              **error);
static void end_element_handler   (GMarkupParseContext  *context,
                                   const gchar          *element_name,
                                   gpointer              user_data,
                                   GError              **error);
static void text_handler          (GMarkupParseContext  *context,
                                   const gchar          *text,
                                   gsize                 text_len,
                                   gpointer              user_data,
                                   GError              **error);

static GMarkupParser gconf_parser = {
  start_element_handler,
  end_element_handler,
  text_handler,
  NULL,
  NULL
};

static void
set_error (GError             **err,
           GMarkupParseContext *context,
           int                  error_code,
           const char          *format,
           ...)
{
  int line, ch;
  va_list args;
  char *str;

  g_markup_parse_context_get_position (context, &line, &ch);

  va_start (args, format);
  str = g_strdup_vprintf (format, args);
  va_end (args);

  g_set_error (err, GCONF_ERROR, error_code,
               _("Line %d character %d: %s"),
               line, ch, str);

  g_free (str);
}

static void
add_context_to_error (GError             **err,
                      GMarkupParseContext *context)
{
  int line, ch;
  char *str;

  if (err == NULL || *err == NULL)
    return;

  g_markup_parse_context_get_position (context, &line, &ch);

  str = g_strdup_printf (_("Line %d character %d: %s"),
                         line, ch, (*err)->message);
  g_free ((*err)->message);
  (*err)->message = str;
}

static void
parse_info_init (ParseInfo *info)
{
  info->states = g_slist_prepend (NULL, GINT_TO_POINTER (STATE_START));
  info->current_entry = NULL;
  info->value_stack = NULL;
  info->value_freelist = NULL;
  info->local_schemas = NULL;
  info->complete_entries = NULL;
}

static void
parse_info_free (ParseInfo *info)
{
  if (info->current_entry)
    markup_entry_free (info->current_entry);

  g_slist_foreach (info->local_schemas,
                   (GFunc) local_schema_info_free,
                   NULL);
  g_slist_free (info->local_schemas);

  g_slist_foreach (info->complete_entries,
                   (GFunc) markup_entry_free,
                   NULL);
  g_slist_free (info->complete_entries);

  /* only free values on the freelist, not those on the stack,
   * but all values in the freelist are also in the stack.
   */
  g_slist_foreach (info->value_freelist, (GFunc) gconf_value_free, NULL);
  g_slist_free (info->value_freelist);
  g_slist_free (info->value_stack);

  g_slist_free (info->states);
}

static void
push_state (ParseInfo  *info,
            ParseState  state)
{
  info->states = g_slist_prepend (info->states, GINT_TO_POINTER (state));
}

static void
pop_state (ParseInfo *info)
{
  g_return_if_fail (info->states != NULL);

  info->states = g_slist_remove (info->states, info->states->data);
}

static ParseState
peek_state (ParseInfo *info)
{
  g_return_val_if_fail (info->states != NULL, STATE_START);

  return GPOINTER_TO_INT (info->states->data);
}


/* add_to_freelist means that if the parse is aborted
 * while the value is on the stack, free that value
 */
static void
value_stack_push (ParseInfo  *info,
                  GConfValue *value,
                  gboolean    add_to_freelist)
{
  info->value_stack = g_slist_prepend (info->value_stack, value);
  if (add_to_freelist)
    info->value_freelist = g_slist_prepend (info->value_freelist, value);
}

static GConfValue*
value_stack_peek (ParseInfo *info)
{
  return info->value_stack ? info->value_stack->data : NULL;
}

static void
value_stack_pop (ParseInfo *info)
{
  info->value_freelist = g_slist_remove (info->value_freelist,
                                         info->value_stack->data);
  info->value_stack = g_slist_remove (info->value_stack,
                                      info->value_stack->data);
}

#define ELEMENT_IS(name) (strcmp (element_name, (name)) == 0)

typedef struct
{
  const char  *name;
  const char **retloc;
} LocateAttr;

static gboolean
locate_attributes (GMarkupParseContext *context,
                   const char  *element_name,
                   const char **attribute_names,
                   const char **attribute_values,
                   GError     **error,
                   const char  *first_attribute_name,
                   const char **first_attribute_retloc,
                   ...)
{
  va_list args;
  const char *name;
  const char **retloc;
  int n_attrs;
#define MAX_ATTRS 24
  LocateAttr attrs[MAX_ATTRS];
  gboolean retval;
  int i;

  g_return_val_if_fail (first_attribute_name != NULL, FALSE);
  g_return_val_if_fail (first_attribute_retloc != NULL, FALSE);

  retval = TRUE;

  n_attrs = 1;
  attrs[0].name = first_attribute_name;
  attrs[0].retloc = first_attribute_retloc;
  *first_attribute_retloc = NULL;

  va_start (args, first_attribute_retloc);

  name = va_arg (args, const char*);
  retloc = va_arg (args, const char**);

  while (name != NULL)
    {
      g_return_val_if_fail (retloc != NULL, FALSE);

      g_assert (n_attrs < MAX_ATTRS);

      attrs[n_attrs].name = name;
      attrs[n_attrs].retloc = retloc;
      n_attrs += 1;
      *retloc = NULL;

      name = va_arg (args, const char*);
      retloc = va_arg (args, const char**);
    }

  va_end (args);

  if (!retval)
    return retval;

  i = 0;
  while (attribute_names[i])
    {
      int j;
      gboolean found;

      found = FALSE;
      j = 0;
      while (j < n_attrs)
        {
          if (strcmp (attrs[j].name, attribute_names[i]) == 0)
            {
              retloc = attrs[j].retloc;

              if (*retloc != NULL)
                {
                  set_error (error, context,
                             GCONF_ERROR_PARSE_ERROR,
                             _("Attribute \"%s\" repeated twice on the same <%s> element"),
                             attrs[j].name, element_name);
                  retval = FALSE;
                  goto out;
                }

              *retloc = attribute_values[i];
              found = TRUE;
            }

          ++j;
        }

      if (!found)
        {
          set_error (error, context,
                     GCONF_ERROR_PARSE_ERROR,
                     _("Attribute \"%s\" is invalid on <%s> element in this context"),
                     attribute_names[i], element_name);
          retval = FALSE;
          goto out;
        }

      ++i;
    }

 out:
  return retval;
}

static gboolean
check_no_attributes (GMarkupParseContext *context,
                     const char  *element_name,
                     const char **attribute_names,
                     const char **attribute_values,
                     GError     **error)
{
  if (attribute_names[0] != NULL)
    {
      set_error (error, context,
                 GCONF_ERROR_PARSE_ERROR,
                 _("Attribute \"%s\" is invalid on <%s> element in this context"),
                 attribute_names[0], element_name);
      return FALSE;
    }

  return TRUE;
}

static gboolean
int_from_string (GMarkupParseContext *context,
                 const char          *str,
                 int                 *val,
                 GError             **error)
{
  char* endptr = NULL;
  glong result;

  *val = 0;

  errno = 0;
  result = strtol (str, &endptr, 10);

  if (endptr == str)
    {
      set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                 _("Didn't understand `%s' (expected integer)"),
                 str);
      return FALSE;
    }
  else if (errno == ERANGE || result < G_MININT || result > G_MAXINT)
    {
      set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                 _("Integer `%s' is too large or small"),
                 str);
      return FALSE;
    }
  else
    {
      *val = result;
      return TRUE;
    }
}

static gboolean
bool_from_string (GMarkupParseContext *context,
                  const char          *str,
                  gboolean            *val,
                  GError             **error)
{
  if (strcmp (str, "true") == 0)
    {
      *val = TRUE;
      return TRUE;
    }
  else if (strcmp (str, "false") == 0)
    {
      *val = FALSE;
      return TRUE;
    }
  else
    {
      *val = FALSE;

      set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                 _("Didn't understand `%s' (expected true or false)"),
                 str);
      return FALSE;
    }
}


static gboolean
float_from_string (GMarkupParseContext *context,
                   const char          *str,
                   double              *val,
                   GError             **error)
{
  double num;

  if (gconf_string_to_double (str, &num))
    {
      *val = num;
      return TRUE;
    }
  else
    {
      *val = 0.0;
      set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                 _("Didn't understand `%s' (expected real number)"),
                 str);
      return FALSE;
    }
}

static void
parse_value_element (GMarkupParseContext  *context,
                     const gchar          *element_name,
                     const gchar         **attribute_names,
                     const gchar         **attribute_values,
                     GConfValue          **retval,
                     GError              **error)
{
  const char *type;
  const char *stype;
  const char *car_type;
  const char *cdr_type;
  const char *value;
  /* check out the crack; "ltype" is for nodes storing a list,
   * and "list_type" is for nodes storing a schema
   */
  const char *ltype;
  const char *list_type;
  const char *owner;
  GConfValueType vtype;

#if 0
  g_assert (ELEMENT_IS ("entry") ||
            ELEMENT_IS ("default") ||
            ELEMENT_IS ("cdr") ||
            ELEMENT_IS ("car") ||
            ELEMENT_IS ("li"));
#endif

  *retval = NULL;

  value = NULL;
  type = NULL;
  stype = NULL;
  ltype = NULL;
  list_type = NULL;
  car_type = NULL;
  cdr_type = NULL;
  owner = NULL;

  if (!locate_attributes (context, element_name, attribute_names, attribute_values,
                          error,
                          "value", &value,
                          "type", &type,
                          "stype", &stype,
                          "ltype", &ltype,
                          "list_type", &list_type,
                          "car_type", &car_type,
                          "cdr_type", &cdr_type,
                          "owner", &owner,
                          NULL))
    return;

  if (type == NULL)
    {
      set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                 _("No \"%s\" attribute on element <%s>"),
                 "type", element_name);
      return;
    }
  
  vtype = gconf_value_type_from_string (type);
  if (vtype == GCONF_VALUE_INVALID)
    {
      set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                 _("Unknown value \"%s\" for \"%s\" attribute on element <%s>"),
                 type, "type", element_name);
      return;
    }

  switch (vtype)
    {
    case GCONF_VALUE_STRING:
      {
        *retval = gconf_value_new (GCONF_VALUE_STRING);
      }
      break;

    case GCONF_VALUE_LIST:
      {
        GConfValueType lvtype;

        if (ltype == NULL)
          {
            set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                       _("No \"%s\" attribute on element <%s>"),
                       "ltype", element_name);
            return;
          }

        lvtype = gconf_value_type_from_string (ltype);

        switch (lvtype)
          {
          case GCONF_VALUE_INVALID:
          case GCONF_VALUE_LIST:
          case GCONF_VALUE_PAIR:
          case GCONF_VALUE_SCHEMA:
            set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                       _("Invalid ltype \"%s\" on <%s>"),
                       ltype, element_name);
            return;
            break;
          default:
            break;
          }

        *retval = gconf_value_new (GCONF_VALUE_LIST);

        gconf_value_set_list_type (*retval,
                                   lvtype);
      }
      break;

    case GCONF_VALUE_SCHEMA:
      {
        GConfValueType schema_vtype;
        GConfSchema *schema;
        GConfValueType car_vtype;
        GConfValueType cdr_vtype;
        GConfValueType list_vtype;

        if (stype == NULL)
          {
            set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                       _("No \"%s\" attribute on element <%s>"),
                       "stype", element_name);
            return;
          }

        /* init for compiler warnings */
        car_vtype = GCONF_VALUE_INVALID;
        cdr_vtype = GCONF_VALUE_INVALID;
        list_vtype = GCONF_VALUE_INVALID;

        schema_vtype = gconf_value_type_from_string (stype);

        if (schema_vtype == GCONF_VALUE_PAIR)
          {
            if (car_type == NULL)
              {
                set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                           _("No \"%s\" attribute on element <%s>"),
                           "car_type", element_name);
                return;
              }

            if (cdr_type == NULL)
              {
                set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                           _("No \"%s\" attribute on element <%s>"),
                           "cdr_type", element_name);
                return;
              }

            car_vtype = gconf_value_type_from_string (car_type);
            cdr_vtype = gconf_value_type_from_string (cdr_type);

            switch (car_vtype)
              {
              case GCONF_VALUE_INVALID:
              case GCONF_VALUE_LIST:
              case GCONF_VALUE_PAIR:
              case GCONF_VALUE_SCHEMA:
                set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                           _("Invalid car_type \"%s\" on <%s>"),
                           car_type, element_name);
                return;
                break;
              default:
                break;
              }

            switch (cdr_vtype)
              {
              case GCONF_VALUE_INVALID:
              case GCONF_VALUE_LIST:
              case GCONF_VALUE_PAIR:
              case GCONF_VALUE_SCHEMA:
                set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                           _("Invalid cdr_type \"%s\" on <%s>"),
                           cdr_type, element_name);
                return;
                break;
              default:
                break;
              }
          }
        else if (schema_vtype == GCONF_VALUE_LIST)
          {
            if (list_type == NULL)
              {
                set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                           _("No \"%s\" attribute on element <%s>"),
                           "list_type", element_name);
                return;
              }

            list_vtype = gconf_value_type_from_string (list_type);

            switch (list_vtype)
              {
              case GCONF_VALUE_INVALID:
              case GCONF_VALUE_LIST:
              case GCONF_VALUE_PAIR:
              case GCONF_VALUE_SCHEMA:
                set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                           _("Invalid list_type \"%s\" on <%s>"),
                           list_type, element_name);
                return;
                break;
              default:
                break;
              }
          }

        *retval = gconf_value_new (GCONF_VALUE_SCHEMA);

        schema = gconf_schema_new ();
        gconf_schema_set_type (schema, schema_vtype);

        if (schema_vtype == GCONF_VALUE_PAIR)
          {
            gconf_schema_set_car_type (schema, car_vtype);
            gconf_schema_set_cdr_type (schema, cdr_vtype);
          }
        else if (schema_vtype == GCONF_VALUE_LIST)
          {
            gconf_schema_set_list_type (schema, list_vtype);
          }

        if (owner)
          gconf_schema_set_owner (schema, owner);

        gconf_value_set_schema_nocopy (*retval, schema);
      }
      break;

    case GCONF_VALUE_PAIR:
      {
        *retval = gconf_value_new (GCONF_VALUE_PAIR);
      }
      break;

    case GCONF_VALUE_INT:
    case GCONF_VALUE_BOOL:
    case GCONF_VALUE_FLOAT:
      {
        double fval;
        gboolean bval;
        int ival;

        if (value == NULL)
          {
            set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                       _("No \"%s\" attribute on element <%s>"),
                       "value", element_name);
            return;
          }

        switch (vtype)
          {
          case GCONF_VALUE_INT:
            if (!int_from_string (context, value, &ival, error))
              return;
            break;

          case GCONF_VALUE_BOOL:
            if (!bool_from_string (context, value, &bval, error))
              return;
            break;

          case GCONF_VALUE_FLOAT:
            if (!float_from_string (context, value, &fval, error))
              return;
            break;

          default:
            g_assert_not_reached ();
          }

        *retval = gconf_value_new (vtype);

        switch (vtype)
          {
          case GCONF_VALUE_INT:
            gconf_value_set_int (*retval, ival);
            break;

          case GCONF_VALUE_BOOL:
            gconf_value_set_bool (*retval, bval);
            break;

          case GCONF_VALUE_FLOAT:
            gconf_value_set_float (*retval, fval);
            break;

          default:
            g_assert_not_reached ();
          }
      }
      break;

    case GCONF_VALUE_INVALID:
      g_assert_not_reached ();
      break;
    }
}

static void
parse_entry_element (GMarkupParseContext  *context,
                     const gchar          *element_name,
                     const gchar         **attribute_names,
                     const gchar         **attribute_values,
                     ParseInfo            *info,
                     GError              **error)
{
  const char *name;
  const char *muser;
  const char *mtime;
  const char *schema;
  GConfValue *value;

  g_return_if_fail (peek_state (info) == STATE_GCONF);
  g_return_if_fail (ELEMENT_IS ("entry"));
  g_return_if_fail (info->current_entry == NULL);

  push_state (info, STATE_ENTRY);

  name = NULL;
  muser = NULL;
  mtime = NULL;
  schema = NULL;

  if (!locate_attributes (context, element_name, attribute_names, attribute_values,
                          error,
                          "name", &name,
                          "muser", &muser,
                          "mtime", &mtime,
                          "schema", &schema,
                          NULL))
    return;

  if (name == NULL)
    {
      set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                 _("No \"%s\" attribute on element <%s>"),
                 "name", element_name);
      return;
    }

  value = NULL;
  parse_value_element (context, element_name, attribute_names,
                       attribute_values, &value,
                       error);
  if (value == NULL)
    return;

  info->current_entry = markup_entry_new (NULL, name);
  info->current_entry->value = value;
  value_stack_push (info, value, FALSE); /* FALSE since current_entry owns it */

  if (muser)
    markup_entry_set_mod_user (info->current_entry,
                               muser);
  if (mtime)
    {
      GTime vmtime;

      vmtime = gconf_string_to_gulong (mtime);
      
      markup_entry_set_mod_time (info->current_entry,
                                 vmtime);
    }

  if (schema)
    markup_entry_set_schema_name (info->current_entry,
                                  schema);
}

static void
parse_local_schema_child_element (GMarkupParseContext  *context,
                                  const gchar          *element_name,
                                  const gchar         **attribute_names,
                                  const gchar         **attribute_values,
                                  ParseInfo            *info,
                                  GError              **error)
{
  LocalSchemaInfo *local_schema;

  g_return_if_fail (peek_state (info) == STATE_LOCAL_SCHEMA);

  local_schema = info->local_schemas->data;

  if (ELEMENT_IS ("default"))
    {
      GConfValue *value;
      
      push_state (info, STATE_DEFAULT);

      value = NULL;
      parse_value_element (context, element_name, attribute_names,
                           attribute_values, &value,
                           error);
      if (value == NULL)
        return;

      if (local_schema->default_value != NULL)
        {
          gconf_value_free (value);
          set_error (error, context,
                     GCONF_ERROR_PARSE_ERROR,
                     _("Two <default> elements below a <local_schema>"));
          return;
        }

      local_schema->default_value = value;
      value_stack_push (info, value, FALSE); /* local_schema owns it */
    }
  else if (ELEMENT_IS ("longdesc"))
    {
      push_state (info, STATE_LONGDESC);

      if (local_schema->long_desc != NULL)
        {
          set_error (error, context,
                     GCONF_ERROR_PARSE_ERROR,
                     _("Two <longdesc> elements below a <local_schema>"));
        }
    }
  else
    {
      set_error (error, context,
                 GCONF_ERROR_PARSE_ERROR,
                 _("Element <%s> is not allowed below <%s>"),
                 element_name, "local_schema");
    }
}

static void
parse_local_schema_element (GMarkupParseContext  *context,
                            const gchar          *element_name,
                            const gchar         **attribute_names,
                            const gchar         **attribute_values,
                            ParseInfo            *info,
                            GError              **error)
{
  const char *locale;
  const char *short_desc;
  LocalSchemaInfo *local_schema;
  GConfValue *value;

  g_return_if_fail (ELEMENT_IS ("local_schema"));

  value = value_stack_peek (info);
  if (value == NULL || value->type != GCONF_VALUE_SCHEMA)
    {
      set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                 _("<%s> provided but current element does not have type %s"),
                 "local_schema", "schema");
      return;
    }

  push_state (info, STATE_LOCAL_SCHEMA);

  locale = NULL;
  short_desc = NULL;

  if (!locate_attributes (context, element_name, attribute_names, attribute_values,
                          error,
                          "locale", &locale,
                          "short_desc", &short_desc,
                          NULL))
    return;

  if (locale == NULL)
    {
      set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                 _("No \"%s\" attribute on element <%s>"),
                 "locale", element_name);
      return;
    }

  local_schema = local_schema_info_new ();
  local_schema->locale = g_strdup (locale);
  local_schema->short_desc = g_strdup (short_desc);

  info->local_schemas = g_slist_prepend (info->local_schemas,
                                         local_schema);
}

static void
parse_car_or_cdr_element (GMarkupParseContext  *context,
                          const gchar          *element_name,
                          const gchar         **attribute_names,
                          const gchar         **attribute_values,
                          ParseInfo            *info,
                          GError              **error)
{
  ParseState current_state;
  GConfValue *value;
  GConfValue *pair;

  current_state = ELEMENT_IS ("car") ? STATE_CAR : STATE_CDR;
  push_state (info, current_state);

  value = NULL;
  parse_value_element (context, element_name, attribute_names,
                       attribute_values, &value,
                       error);
  if (value == NULL)
    return;

  pair = value_stack_peek (info);

  if (pair->type == GCONF_VALUE_PAIR)
    {
      if (current_state == STATE_CAR)
        {
          if (gconf_value_get_car (pair) == NULL)
            {
              gconf_value_set_car_nocopy (pair, value);
              value_stack_push (info, value, FALSE); /* pair owns it */
            }
          else
            {
              gconf_value_free (value);
              set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                         _("Two <car> elements given for same pair"));
            }
        }
      else
        {
          if (gconf_value_get_cdr (pair) == NULL)
            {
              gconf_value_set_cdr_nocopy (pair, value);
              value_stack_push (info, value, FALSE); /* pair owns it */
            }
          else
            {
              gconf_value_free (value);
              set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                         _("Two <cdr> elements given for same pair"));
            }
        }
    }
  else
    {
      gconf_value_free (value);
      set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                 _("<%s> provided but current element does not have type %s"),
                 current_state == STATE_CAR ? "car" : "cdr", "pair");
    }
}

static void
parse_li_element (GMarkupParseContext  *context,
                  const gchar          *element_name,
                  const gchar         **attribute_names,
                  const gchar         **attribute_values,
                  ParseInfo            *info,
                  GError              **error)
{
  ParseState current_state;
  GConfValue *value;
  GConfValue *list;

  current_state = peek_state (info);

  value = NULL;
  parse_value_element (context, element_name, attribute_names,
                       attribute_values, &value,
                       error);
  if (value == NULL)
    return;

  list = value_stack_peek (info);

  if (list->type == GCONF_VALUE_LIST)
    {
      if (value->type == gconf_value_get_list_type (list))
        {
          GSList *slist;

          slist = gconf_value_steal_list (list);
          slist = g_slist_append (slist, value);
          gconf_value_set_list_nocopy (list, slist);
        }
      else
        {
          gconf_value_free (value);
          set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                     _("<li> has wrong type %s"),
                     gconf_value_type_to_string (value->type));
        }
    }
  else
    {
      gconf_value_free (value);
      set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                 _("<%s> provided but current element does not have type %s"),
                 "li", "list");
    }
}

static void
parse_value_child_element (GMarkupParseContext  *context,
                           const gchar          *element_name,
                           const gchar         **attribute_names,
                           const gchar         **attribute_values,
                           ParseInfo            *info,
                           GError              **error)
{
  ParseState current_state;

  current_state = peek_state (info);

  if (ELEMENT_IS ("stringvalue"))
    {
      GConfValue *value;

      value = value_stack_peek (info);

      if (value->type == GCONF_VALUE_STRING)
        {
          push_state (info, STATE_STRINGVALUE);
        }
      else
        {
          set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                     _("<%s> provided but current element does not have type %s"),
                     "stringvalue", "string");
        }
    }
  else if (ELEMENT_IS ("local_schema"))
    {
      switch (current_state)
        {
        case STATE_CAR:
        case STATE_CDR:
        case STATE_LI:
        case STATE_DEFAULT:
          set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                     _("Element <%s> is not allowed inside current element"),
                     element_name);
          break;
        case STATE_ENTRY:
          parse_local_schema_element (context, element_name,
                                      attribute_names, attribute_values,
                                      info, error);
          break;
        default:
          g_assert_not_reached ();
          break;
        }
    }
  else if (ELEMENT_IS ("car") ||
           ELEMENT_IS ("cdr"))
    {
      switch (current_state)
        {
        case STATE_CAR:
        case STATE_CDR:
        case STATE_LI:
          set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                     _("Element <%s> is not allowed inside current element"),
                     element_name);
          break;
        case STATE_DEFAULT:
        case STATE_ENTRY:
          parse_car_or_cdr_element (context, element_name,
                                    attribute_names, attribute_values,
                                    info, error);
          break;
        default:
          g_assert_not_reached ();
          break;
        }
    }
  else if (ELEMENT_IS ("li"))
    {
      switch (current_state)
        {
        case STATE_CAR:
        case STATE_CDR:
        case STATE_LI:
          set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                     _("Element <%s> is not allowed inside current element"),
                     element_name);
          break;
        case STATE_DEFAULT:
        case STATE_ENTRY:
          parse_li_element (context, element_name,
                            attribute_names, attribute_values,
                            info, error);
          break;
        default:
          g_assert_not_reached ();
          break;
        }
    }
  else
    {
      set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                 _("Element <%s> is not allowed inside current element"),
                 element_name);
    }
}

static void
start_element_handler (GMarkupParseContext *context,
                       const gchar         *element_name,
                       const gchar        **attribute_names,
                       const gchar        **attribute_values,
                       gpointer             user_data,
                       GError             **error)
{
  ParseInfo *info = user_data;
  ParseState current_state;

  current_state = peek_state (info);

  switch (current_state)
    {
    case STATE_START:
      if (ELEMENT_IS ("gconf"))
        {
          if (!check_no_attributes (context, element_name,
                                    attribute_names, attribute_values,
                                    error))
            return;

          push_state (info, STATE_GCONF);
        }
      else
        set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                   _("Outermost element in menu file must be <gconf> not <%s>"),
                   element_name);
      break;

    case STATE_GCONF:
      if (ELEMENT_IS ("entry"))
        {
          parse_entry_element (context, element_name,
                               attribute_names, attribute_values,
                               info, error);
        }
      else
        set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                   _("Element <%s> is not allowed inside a <%s> element"),
                   element_name, "gconf");
      break;

    case STATE_ENTRY:
    case STATE_DEFAULT:
    case STATE_CAR:
    case STATE_CDR:
    case STATE_LI:
      parse_value_child_element (context, element_name,
                                 attribute_names, attribute_values,
                                 info, error);
      break;

    case STATE_LOCAL_SCHEMA:
      parse_local_schema_child_element (context, element_name,
                                        attribute_names, attribute_values,
                                        info, error);
      break;
      
    case STATE_STRINGVALUE:
      set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                 _("Element <%s> is not allowed inside a <%s> element"),
                 element_name, "stringvalue");
      break;
    case STATE_LONGDESC:
      set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                 _("Element <%s> is not allowed inside a <%s> element"),
                 element_name, "longdesc");
      break;
    }
}

static void
end_element_handler (GMarkupParseContext *context,
                     const gchar         *element_name,
                     gpointer             user_data,
                     GError             **error)
{
  ParseInfo *info = user_data;

  switch (peek_state (info))
    {
    case STATE_START:
      break;
      
    case STATE_ENTRY:
      g_assert (info->current_entry);
      g_assert (info->current_entry->local_schemas == NULL);

      info->current_entry->local_schemas = info->local_schemas;
      info->local_schemas = NULL;

      info->complete_entries = g_slist_prepend (info->complete_entries,
                                                info->current_entry);
      info->current_entry = NULL;

      value_stack_pop (info);
      pop_state (info);
      break;

    case STATE_DEFAULT:
      {
        GConfValue *value;
        LocalSchemaInfo *local_schema;

        local_schema = info->local_schemas->data;

        /* Default should already be in a LocalSchemaInfo */
        value = value_stack_peek (info);

        g_assert (value == local_schema->default_value);

        value_stack_pop (info);

        pop_state (info);
      }
      break;

    case STATE_CAR:
    case STATE_CDR:
    case STATE_LI:
      value_stack_pop (info);
      pop_state (info);
      break;

    case STATE_GCONF:
    case STATE_LOCAL_SCHEMA:
    case STATE_LONGDESC:
    case STATE_STRINGVALUE:
      pop_state (info);
      break;
    }
}

#define NO_TEXT(element_name) set_error (error, context, GCONF_ERROR_PARSE_ERROR, _("No text is allowed inside element <%s>"), element_name)

static gboolean
all_whitespace (const char *text,
                int         text_len)
{
  const char *p;
  const char *end;

  p = text;
  end = text + text_len;

  while (p != end)
    {
      if (!g_ascii_isspace (*p))
        return FALSE;

      p = g_utf8_next_char (p);
    }

  return TRUE;
}

static void
text_handler (GMarkupParseContext *context,
              const gchar         *text,
              gsize                text_len,
              gpointer             user_data,
              GError             **error)
{
  ParseInfo *info = user_data;

  if (all_whitespace (text, text_len))
    return;

  /* FIXME http://bugzilla.gnome.org/show_bug.cgi?id=70448 would
   * allow a nice cleanup here.
   */

  switch (peek_state (info))
    {
    case STATE_START:
      g_assert_not_reached (); /* gmarkup shouldn't do this */
      break;
    case STATE_STRINGVALUE:
      {
        GConfValue *value;

        value = value_stack_peek (info);
        g_assert (value->type == GCONF_VALUE_STRING);

        if (gconf_value_get_string (value) != NULL)
          {
            set_error (error, context, GCONF_ERROR_PARSE_ERROR,
                       _("Element <%s> is not allowed inside current element"),
                       "stringvalue");
          }
        else
          {
            gconf_value_set_string_nocopy (value,
                                           g_strndup (text, text_len));
          }
      }
      break;
    case STATE_LONGDESC:
      {
        LocalSchemaInfo *local_schema;

        local_schema = info->local_schemas->data;

        local_schema->long_desc = g_strndup (text, text_len);
      }
      break;
    case STATE_GCONF:
      NO_TEXT ("gconf");
      break;
    case STATE_ENTRY:
      NO_TEXT ("entry");
      break;
    case STATE_LOCAL_SCHEMA:
      NO_TEXT ("local_schema");
      break;
    case STATE_DEFAULT:
      NO_TEXT ("default");
      break;
    case STATE_CAR:
      NO_TEXT ("car");
      break;
    case STATE_CDR:
      NO_TEXT ("cdr");
      break;
    case STATE_LI:
      NO_TEXT ("li");
      break;
    }
}

static GSList*
parse_entries (const char *filename,
               GError    **err)
{
  GMarkupParseContext *context;
  GError *error;
  ParseInfo info;
  char *text;
  int length;
  GSList *retval;

  text = NULL;
  length = 0;
  retval = NULL;

  if (!g_file_get_contents (filename,
                            &text,
                            &length,
                            err))
    return NULL;

  g_assert (text);

  parse_info_init (&info);

  context = g_markup_parse_context_new (&gconf_parser,
                                        0, &info, NULL);

  error = NULL;
  if (!g_markup_parse_context_parse (context,
                                     text,
                                     length,
                                     &error))
    goto out;

  error = NULL;
  if (!g_markup_parse_context_end_parse (context, &error))
    goto out;

  g_markup_parse_context_free (context);

  goto out;

 out:

  g_free (text);

  if (error)
    {
      g_propagate_error (err, error);
    }
  else if (info.complete_entries)
    {
      retval = info.complete_entries;
      info.complete_entries = NULL;
    }
  else
    {
      /* No entries in here, but that's not an error,
       * empty files are allowed.
       */
    }

  parse_info_free (&info);

  return retval;
}

/*
 * Local schema
 */

static LocalSchemaInfo*
local_schema_info_new (void)
{
  LocalSchemaInfo *info;

  info = g_new0 (LocalSchemaInfo, 1);

  return info;
}

static void
local_schema_info_free (LocalSchemaInfo *info)
{
  g_free (info->locale);
  g_free (info->short_desc);
  g_free (info->long_desc);
  if (info->default_value)
    gconf_value_free (info->default_value);
  g_free (info);
}
