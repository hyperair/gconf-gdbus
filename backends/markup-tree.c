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
#include "markup-tree.h"

static MarkupDir* markup_dir_new        (MarkupTree *tree,
                                         MarkupDir  *parent,
                                         const char *name);
static void       markup_dir_free       (MarkupDir  *dir);
static gboolean   markup_dir_needs_sync (MarkupDir  *dir);
static gboolean   markup_dir_sync       (MarkupDir  *dir,
                                         GError    **err);
static char*      markup_dir_build_path (MarkupDir  *dir,
                                         gboolean    with_data_file);

static MarkupEntry* markup_entry_new  (MarkupDir   *dir,
                                       const char  *name);
static void         markup_entry_free (MarkupEntry *entry);


static GSList* parse_entries (const char *filename,
                              GError    **err);

struct _MarkupTree
{
  char *dirname;
  guint dir_mode;
  guint file_mode;

  MarkupDir *root;
}

MarkupTree*
markup_tree_new (const char *root_dir,
                 guint       dir_mode,
                 guint       file_mode)
{
  MarkupTree *tree;

  tree = g_new0 (MarkupTree, 1);

  tree->dirname = g_strdup (root_dir);
  tree->dir_mode = dir_mode;
  tree->file_mode = file_mode;

  root = markup_dir_new (tree, NULL, "/");
  
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

MarkupDir*
markup_tree_lookup_dir (MarkupTree *tree,
                        const char *full_key)
{


}

MarkupDir*
markup_tree_ensure_dir (MarkupTree *tree,
                        const char *full_key)
{


}

MarkupEntry*
markup_dir_lookup_entry (MarkupDir   *dir,
                         const char  *relative_key,
                         GError     **err)
{


}

MarkupEntry*
markup_dir_ensure_entry (MarkupDir   *dir,
                         const char  *relative_key,
                         GError     **err)
{



}

GSList*
markup_dir_list_entries (MarkupDir   *dir,
                         GError     **err)
{


}

GSList*
markup_dir_list_subdirs (MarkupDir   *dir,
                         GError     **err)
{



}

static gboolean
markup_dir_needs_sync (MarkupDir  *dir)
{
  
}

static gboolean
markup_dir_sync (MarkupDir  *dir,
                 GError    **err)
{
  

}

static char*
markup_dir_build_path (MarkupDir  *dir,
                       gboolean    with_data_file)
{
  GString *name;
  GSList *components;
  GSList *tmp;
  MarkupDir *iter;
  int i;

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

struct _MarkupEntry
{
  MarkupDir  *dir;
  char       *name;
  GConfValue *value;
  GSList     *localized_values;
  char       *schema_name;
  char       *mod_user;
  GTime       mod_time;
};

static MarkupEntry*
markup_entry_new (MarkupDir  *dir,
                  const char *name)
{
  MarkupEntry *entry;

  entry = g_new0 (MarkupEntry, 1);

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
  
  g_free (entry);
}

static const char*
get_value_locale (const GConfValue *value)
{
  if (value->type == GCONF_VALUE_SCHEMA)
    {
      GConfSchema *schema;

      schema = gconf_value_get_schema (value);
      g_assert (schema);
      return schema_get_locale (schema);
    }
  else
    {
      return NULL;
    }
}

void
markup_entry_set_value (MarkupEntry       *entry,
                        const GConfValue  *value,
                        GError           **err)
{
  const char *locale;
  
  g_assert (entry->dir->entries_loaded);

  locale = get_value_locale (value);

  if (locale == NULL || strcmp (locale, "C") == 0)
    {
      if (entry->value == value)
        return;
      
      if (entry->value)
        gconf_value_free (entry->value);
      
      entry->value = gconf_value_copy (value);
    }
  else
    {
      GSList *tmp;

      tmp = entry->localized_values;
      while (tmp != NULL)
        {
          const GConfValue *v;
          const char *l;

          v = tmp->data;
          l = get_value_locale (v);

          g_assert (l != NULL);

          if (strcmp (l, locale) == 0)
            {
              gconf_value_free (tmp->data);
              tmp->data = gconf_value_copy (value);
              break;
            }

          tmp = tmp->next;
        }

      if (tmp == NULL)
        {
          /* Didn't find a value for locale, make a new entry in the list */
          entry->localized_values =
            g_slist_prepend (entry->localized_values,
                             gconf_value_copy (value));
        }
    }  

  entry->dir->entries_need_save = TRUE;
}


/*
 * Parser
 */


typedef enum
{
  STATE_START,
  STATE_GCONF,
  STATE_EMPTY_ENTRY,
  STATE_STRING_ENTRY,
  STATE_SCHEMA_ENTRY,
  STATE_LIST_ENTRY,
  STATE_PAIR_ENTRY,
  STATE_STRINGVALUE,  
  STATE_DEFAULT,
  STATE_SHORTDESC,
  STATE_LONGDESC,
  STATE_LOCAL_SCHEMA
} ParseState;

typedef struct
{
  GSList *states;

  GConfValueType current_entry_type;
  MarkupEntry   *current_entry;
  
} ParseInfo;

static void set_error (GError             **err,
                       GMarkupParseContext *context,
                       int                  error_domain,
                       int                  error_code,
                       const char          *format,
                       ...) G_GNUC_PRINTF (5, 6);

static void add_context_to_error (GError             **err,
                                  GMarkupParseContext *context);

static void          parse_info_init        (ParseInfo    *info);
static void          parse_info_free        (ParseInfo    *info);

static void       push_state (ParseInfo  *info,
                              ParseState  state);
static void       pop_state  (ParseInfo  *info);
static ParseState peek_state (ParseInfo  *info);

static void parse_foo_element     (GMarkupParseContext  *context,
                                   const gchar          *element_name,
                                   const gchar         **attribute_names,
                                   const gchar         **attribute_values,
                                   ParseInfo            *info,
                                   GError              **error);


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
           int                  error_domain,
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

  g_set_error (err, error_domain, error_code,
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
}

static void
parse_info_free (ParseInfo *info)
{  
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
                             G_MARKUP_ERROR,
                             G_MARKUP_ERROR_PARSE,
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
                     G_MARKUP_ERROR,
                     G_MARKUP_ERROR_PARSE,
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
                 G_MARKUP_ERROR,
                 G_MARKUP_ERROR_PARSE,
                 _("Attribute \"%s\" is invalid on <%s> element in this context"),
                 attribute_names[0], element_name);
      return FALSE;
    }

  return TRUE;
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
  const char *type;
  const char *stype;
  const char *list_type;
  const char *car_type;
  const char *cdr_type;
  const char *owner;
  const char *value;
  
  g_return_if_fail (peek_state (info) == STATE_GCONF);

  g_assert (ELEMENT_IS ("entry"));

  name = NULL;
  value = NULL;
  muser = NULL;
  mtime = NULL;
  type = NULL;
  stype = NULL;
  list_type = NULL;
  car_type = NULL;
  cdr_type = NULL;
  owner = NULL;
  
  if (!locate_attributes (context, element_name, attribute_names, attribute_values,
                          error,
                          "name", &name,
                          "value", &value,
                          "muser", &muser,
                          "mtime", &mtime,
                          "type", &type,
                          "stype", &stype,
                          "list_type", &list_type,
                          "car_type", &car_type,
                          "cdr_type", &cdr_type,
                          "owner", &owner,
                          NULL))
    return;
  
  if (name == NULL)
    {
      set_error (error, context, G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                 _("No \"%s\" attribute on element <%s>"),
                 "name", element_name);
      return;
    }

  if (type == NULL)
    {
      set_error (error, context, G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                 _("No \"%s\" attribute on element <%s>"),
                 "type", element_name);
      return;
    }  
  
  if (strcmp (type, "string") == 0)
    {
      push_state (info, STATE_STRING_ENTRY);

    }
  else if (strcmp (type, "list") == 0)
    {
      push_state (info, STATE_LIST_ENTRY);

    }
  else if (strcmp (type, "schema") == 0)
    {
      push_state (info, STATE_SCHEMA_ENTRY);
    }
  else if (strcmp (type, "pair") == 0)
    {
      push_state (info, STATE_PAIR_ENTRY);

    }
  else if (strcmp (type, "int") == 0 ||
           strcmp (type, "bool") == 0 ||
           strcmp (type, "float") == 0)
    {
      push_state (info, STATE_EMPTY_ENTRY);
      
    }
  else
    {
      set_error (error, context, G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                 _("Unknown value \"%s\" for \"%s\" attribute on element <%s>"),
                 type, "type", element_name);
      return;
    }
}

parse_entry_child_element (GMarkupParseContext  *context,
                           const gchar          *element_name,
                           const gchar         **attribute_names,
                           const gchar         **attribute_values,
                           ParseInfo            *info,
                           GError              **error)
{
  g_return_if_fail (peek_state (info) == STATE_ENTRY);

  if (ELEMENT_IS ("stringvalue"))
    {
      if (!check_no_attributes (context, element_name,
                                attribute_names, attribute_values,
                                error))
        return;

      push_state (info, STATE_MERGE_DIR);
    }
  else if (ELEMENT_IS ("local_schema"))
    {
      if (!check_no_attributes (context, element_name,
                                attribute_names, attribute_values,
                                error))
        return;
      
      push_state (info, STATE_DESKTOP_DIR);
    }
  else
    {
      set_error (error, context,
                 G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                 _("Element <%s> is not allowed below <%s>"),
                 element_name, "entry");
    }
}

parse_schema_child_element (GMarkupParseContext  *context,
                            const gchar          *element_name,
                            const gchar         **attribute_names,
                            const gchar         **attribute_values,
                            ParseInfo            *info,
                            GError              **error)
{
  g_return_if_fail (peek_state (info) == STATE_ENTRY);

  if (ELEMENT_IS ("default"))
    {
      if (!check_no_attributes (context, element_name,
                                attribute_names, attribute_values,
                                error))
        return;
      
      push_state (info, STATE_DESKTOP_DIR);
    }
  else if (ELEMENT_IS ("shortdesc"))
    {
      parse_folder_element (context, element_name,
                            attribute_names, attribute_values,
                            info, error);
    }
  else if (ELEMENT_IS ("longdesc"))
    {

    }
  else
    {
      set_error (error, context,
                 G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                 _("Element <%s> is not allowed below <%s>"),
                 element_name, "local_schema");
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

  switch (peek_state (info))
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
        set_error (error, context, G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
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
        set_error (error, context, G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                   _("Element <%s> is not allowed inside a <%s> element"),
                   element_name, "gconf");
      break;
      
    case STATE_MERGE_DIR:
      set_error (error, context, G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                 _("Element <%s> is not allowed inside a <%s> element"),
                 element_name, "MergeDir");
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

    case STATE_ONLY_UNALLOCATED:
      pop_state (info);
      break;
    }
}

#define NO_TEXT(element_name) set_error (error, context, G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE, _("No text is allowed inside element <%s>"), element_name)

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
  Vfolder *folder;
  
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
    case STATE_VFOLDER_INFO:
      NO_TEXT ("VFolderInfo");
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
#if 0
  else if (info.root_folder)
    {
      retval = info.root_folder;
      info.root_folder = NULL;
    }
#endif
  else
    {
      g_set_error (err, G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                   _("File %s did not contain a root <gconf> element"),
                   filename);
    }

  parse_info_free (&info);
  
  return retval;
}
