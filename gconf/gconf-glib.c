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

#include "gconf-glib-public.h"
#include "gconf-glib-private.h"
#include <string.h>

#define G_N_ELEMENTS(arr)		(sizeof (arr) / sizeof ((arr)[0]))

static GList*
g_list_delete_link (GList *list,
		    GList *link)
{
  list = g_list_remove_link (list, link);
  g_list_free_1 (link);

  return list;
}

static void
g_string_append_len (GString *str,
                     const gchar *data,
                     gint len)
{
  gchar *tmp;
  
  if (len < 0)
    len = strlen (data);

  tmp = g_strndup (data, len);
  g_string_append (str, tmp);
  g_free (tmp);
}

static GError* 
g_error_new_valist(GQuark         domain,
                   gint           code,
                   const gchar   *format,
                   va_list        args)
{
  GError *error;
  
  error = g_new (GError, 1);
  
  error->domain = domain;
  error->code = code;
  error->message = g_strdup_vprintf (format, args);
  
  return error;
}

GError*
g_error_new (GQuark       domain,
             gint         code,
             const gchar *format,
             ...)
{
  GError* error;
  va_list args;

  g_return_val_if_fail (format != NULL, NULL);
  g_return_val_if_fail (domain != 0, NULL);

  va_start (args, format);
  error = g_error_new_valist (domain, code, format, args);
  va_end (args);

  return error;
}

GError*
g_error_new_literal (GQuark         domain,
                     gint           code,
                     const gchar   *message)
{
  GError* err;

  g_return_val_if_fail (message != NULL, NULL);
  g_return_val_if_fail (domain != 0, NULL);

  err = g_new (GError, 1);

  err->domain = domain;
  err->code = code;
  err->message = g_strdup (message);
  
  return err;
}

void
g_error_free (GError *error)
{
  g_return_if_fail (error != NULL);  

  g_free (error->message);

  g_free (error);
}

GError*
g_error_copy (const GError *error)
{
  GError *copy;
  
  g_return_val_if_fail (error != NULL, NULL);

  copy = g_new (GError, 1);

  *copy = *error;

  copy->message = g_strdup (error->message);

  return copy;
}

gboolean
g_error_matches (const GError *error,
                 GQuark        domain,
                 gint          code)
{
  return error &&
    error->domain == domain &&
    error->code == code;
}

void
g_set_error (GError     **err,
             GQuark       domain,
             gint         code,
             const gchar *format,
             ...)
{
  va_list args;

  if (err == NULL)
    return;

  if (*err != NULL)
    g_warning ("GError set over the top of a previous GError or uninitialized memory.\n"
               "This indicates a bug in someone's code. You must ensure an error is NULL before it's set.");
  
  va_start (args, format);
  *err = g_error_new_valist (domain, code, format, args);
  va_end (args);
}

#define ERROR_OVERWRITTEN_WARNING "GError set over the top of a previous GError or uninitialized memory.\n" \
               "This indicates a bug in someone's code. You must ensure an error is NULL before it's set."

/**
 * g_propagate_error:
 * @dest: error return location
 * @src: error to move into the return location
 * 
 * If @dest is NULL, free @src; otherwise,
 * moves @src into *@dest. *@dest must be NULL.
 **/
void    
g_propagate_error (GError       **dest,
		   GError        *src)
{
  g_return_if_fail (src != NULL);
  
  if (dest == NULL)
    {
      if (src)
        g_error_free (src);
      return;
    }
  else
    {
      if (*dest != NULL)
        g_warning (ERROR_OVERWRITTEN_WARNING);
      
      *dest = src;
    }
}

void
g_clear_error (GError **err)
{
  if (err && *err)
    {
      g_error_free (*err);
      *err = NULL;
    }
}



/**********************************************************/









/* gmarkup.c - Simple XML-like string parser/writer
 *
 *  Copyright 2000 Red Hat, Inc.
 *
 * GLib is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * GLib is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GLib; see the file COPYING.LIB.  If not,
 * write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *   Boston, MA 02111-1307, USA.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "gconf-internals.h"

typedef struct _GMarkupAttribute GMarkupAttribute;
typedef struct _GMarkupNodePassthrough GMarkupNodePassthrough;

struct _GMarkupAttribute
{
  gchar *name;
  gchar *value;
};


struct _GMarkupNodePassthrough
{
  GMarkupNodeType type;
  
  gchar *passthrough_text;
};

static GMarkupAttribute *attribute_new (const gchar *name, const gchar *value);
static void attribute_free (GMarkupAttribute *attr);
static void append_node (GString *str,
                         GMarkupNode *node,
                         int depth,
                         GMarkupToStringFlags flags);

static GMarkupNode* parse_element (const gchar *text,
                                   gint i,
                                   gint length,
                                   GMarkupParseFlags flags,
                                   gint *new_i,
                                   GError **error);

GQuark
g_markup_error_quark ()
{
  static GQuark error_quark = 0;

  if (error_quark == 0)
    error_quark = g_quark_from_static_string ("g-markup-error-quark");

  return error_quark;
}

static GMarkupNodePassthrough*
g_markup_node_new_passthrough (const gchar *text)
{
  GMarkupNodePassthrough *node;

  g_return_val_if_fail (text != NULL, NULL);
  
  node = g_new (GMarkupNodePassthrough, 1);

  node->type = G_MARKUP_NODE_PASSTHROUGH;
  node->passthrough_text = g_strdup (text);
  
  return node;
}

GMarkupNodeText*
g_markup_node_new_text (const gchar *text)
{
  GMarkupNodeText *node;

  g_return_val_if_fail (text != NULL, NULL);
  
  node = g_new (GMarkupNodeText, 1);

  node->type = G_MARKUP_NODE_TEXT;
  node->text = g_strdup (text);
  
  return node;
}

GMarkupNodeElement*
g_markup_node_new_element (const gchar *name)
{
  GMarkupNodeElement *node;

  g_return_val_if_fail (name != NULL, NULL);
  
  node = g_new (GMarkupNodeElement, 1);

  node->type = G_MARKUP_NODE_ELEMENT;
  node->name = g_strdup (name);

  node->children = NULL;
  node->attributes = NULL;

  return node;
}

static void
free_attribute_list (GList *list)
{
  GList *tmp_list;

  tmp_list = list;
  while (tmp_list)
    {
      GMarkupAttribute *attr = tmp_list->data;

      attribute_free (attr);
      
      tmp_list = g_list_next (tmp_list);
    }

  g_list_free (list);
}

static void
free_node_list (GList *list)
{
  GList *tmp_list;

  tmp_list = list;
  while (tmp_list)
    {
      GMarkupNode *node = tmp_list->data;

      g_markup_node_free (node);
      
      tmp_list = g_list_next (tmp_list);
    }

  g_list_free (list);
}

void
g_markup_node_free (GMarkupNode *node)
{
  g_return_if_fail (node != NULL);
  
  switch (node->type)
    {
    case G_MARKUP_NODE_TEXT:
      g_free (node->text.text);
      break;
      
    case G_MARKUP_NODE_ELEMENT:
      g_free (node->element.name);
      free_attribute_list (node->element.attributes);
      free_node_list (node->element.children);
      break;

    case G_MARKUP_NODE_PASSTHROUGH:
      g_free (((GMarkupNodePassthrough*)node)->passthrough_text);
      break;
      
    default:
      g_assert_not_reached ();
      break;
    }
  
  g_free (node);
}

void
g_markup_node_set_attribute (GMarkupNodeElement *node,
                             const gchar *attribute_name,
                             const gchar *attribute_value)
{
  GList *tmp_list;

  g_return_if_fail (node != NULL);
  g_return_if_fail (node->type == G_MARKUP_NODE_ELEMENT);
  g_return_if_fail (attribute_name != NULL);
  /* value is NULL to unset */
  
  tmp_list = node->children;
  while (tmp_list)
    {
      GMarkupAttribute *attr = tmp_list->data;

      if (strcmp (attr->name, attribute_name) == 0)
        {
          if (attribute_value)
            {
              g_free (attr->value);
              attr->value = g_strdup (attribute_value);
            }
          else
            {
              node->attributes = g_list_delete_link (node->attributes,
                                                     tmp_list);

              attribute_free (attr);
            }

          return;
        }
      
      tmp_list = g_list_next (tmp_list);
    }

  /* Not found, add it if we have a value */
  if (attribute_value)
    {
      GMarkupAttribute *attr;

      attr = attribute_new (attribute_name, attribute_value);
      
      node->attributes = g_list_prepend (node->attributes, attr);
    }
}

gchar*
g_markup_node_get_attribute (GMarkupNodeElement *node,
                             const gchar *attribute_name)
{
  GList *tmp_list;

  g_return_val_if_fail (node != NULL, NULL);
  g_return_val_if_fail (node->type == G_MARKUP_NODE_ELEMENT, NULL);
  g_return_val_if_fail (attribute_name != NULL, NULL);
  
  tmp_list = node->attributes;
  while (tmp_list)
    {
      GMarkupAttribute *attr = tmp_list->data;

      if (strcmp (attr->name, attribute_name) == 0)
        return g_strdup (attr->value);
      
      tmp_list = g_list_next (tmp_list);
    }

  return NULL;
}

void
g_markup_node_unset_attribute (GMarkupNodeElement *node,
                               const gchar *attribute_name)
{
  g_markup_node_set_attribute (node, attribute_name, NULL);
}

void
g_markup_node_get_attributes (GMarkupNodeElement *node,
                              gchar ***namesp,
                              gchar ***valuesp,
                              gint    *n_attributes)
{
  GList *tmp_list;
  gint len;
  gchar **names;
  gchar **values;
  gint i;
  
  g_return_if_fail (node != NULL);
  g_return_if_fail (node->type == G_MARKUP_NODE_ELEMENT);
  
  len = g_list_length (node->attributes);

  if (namesp)
    {
      names = g_new (gchar*, len + 1);
      names[len] = NULL;
    }
  else
    names = NULL;
  
  if (valuesp)
    {
      values = g_new (gchar*, len + 1);
      values[len] = NULL;
    }
  else
    values = NULL;
  
  i = 0;
  tmp_list = node->attributes;
  while (tmp_list)
    {
      GMarkupAttribute *attr = tmp_list->data;

      g_assert (i < len + 1);
      
      if (namesp)
        names[i] = g_strdup (attr->name);

      if (valuesp)
        values[i] = g_strdup (attr->value);
      
      tmp_list = g_list_next (tmp_list);
      ++i;
    }

  if (n_attributes)
    *n_attributes = len;

  if (namesp)
    *namesp = names;

  if (valuesp)
    *valuesp = values;
}


/* Parsing a string */

#if 1
#include <stdio.h>
#define T(desc, byte) printf("%8d %35s   (%s)\n", byte, desc, G_GNUC_FUNCTION)
#else
#define T(desc, byte)
#endif

static inline gint
next_char (const gchar *text, gint i)
{
  const gchar *p = &text[i];
  const gchar *n = g_utf8_next_char (p);
  return i + (n - p);
}

static gint
skip_spaces (const gchar *text,
             gint i,
             gint length)
{
  gunichar c;
  
  c = g_utf8_get_char (&text[i]);
  while (g_unichar_isspace (c))
    {
      i = next_char (text, i);
      if (i >= length)
        break;
      c = g_utf8_get_char (&text[i]);
    }

  return i;
}

static gchar*
text_before (const gchar *text,
             gint i)
{
  gint before = i - 30;

  if (before < 0)
    before = 0;

  return g_strndup (&text[before], 30);
}

static void
set_error (const gchar *text,
           gint i,
           gint length,
           GError **error,
           GMarkupErrorType code,
           const gchar   *format,
           ...)
{
  T("error", i);
  
  if (error)
    {
      gchar *s;
      gchar *surrounding;
      gchar *sub;
      gint lines;
      gint char_on_line;
      gint last_newline;
      gint j;
      gint point;
      gint start, end;
      
      va_list args;
      
      va_start (args, format);
      s = g_strdup_vprintf (format, args);
      va_end (args);

      /* count lines up to i */
      lines = 1;
      j = 0;
      last_newline = 0;
      while (j < i)
        {
          gunichar c = g_utf8_get_char (&text[j]);

          if (c == '\n' || c == '\r')
            {
              ++lines;
              last_newline = j;
            }
          
          j = next_char (text, j);
        }

      char_on_line = i - last_newline;
      
      start = i - 40;
      if (start < 0)
        start = 0;
      end = i + 40;
      if (end > length)
        end = length;

      surrounding = g_strndup (&text[start], end - start);
      /* only display stuff on the same line */
      point = i - start;
      sub = surrounding;
      j = 0;
      while (surrounding[j] != '\0')
        {
          if (surrounding[j] == '\n')
            {
              if (j < point)
                sub = &surrounding[j+1];

              surrounding[j] = '\0';
            }
          
          ++j;
        }
      
      *error = g_error_new (G_MARKUP_ERROR,
                            code,
                            _("Error on line %d char %d: %s\n(Some surrounding text was '%s')\n"),
                            lines, char_on_line, s, sub);

      g_free (surrounding);
      g_free (s);
    }
}           

static gboolean
is_name_start_char (gunichar c)
{
  if (g_unichar_isalpha (c) ||
      c == '_' ||
      c == ':')
    return TRUE;
  else
    return FALSE;
}

static gboolean
is_name_char (gunichar c)
{
  if (g_unichar_isalnum (c) ||
      c == '.' ||
      c == '-' ||
      c == '_' ||
      c == ':')
    return TRUE;
  else
    return FALSE;
}

static const gchar*
unthreadsafe_char_str (gunichar c)
{
  static gchar buf[7];

  memset (buf, '\0', 7);
  g_unichar_to_utf8 (c, buf);
  return buf;
}

static gint
find_name_end (const gchar *text,
               gint name_start,               
               gint length,
               GMarkupParseFlags flags,
               GError **error)
{
  gint i = name_start;

  T("name start", name_start);
  
  /* start of name assumed to be already validated */
  i = next_char (text, i);

  while (i < length)
    {
      gunichar c = g_utf8_get_char (&text[i]);

      if (!is_name_char (c))
        break;
      else
        i = next_char (text, i);
    }

  T("name end", i);
  
  return i;
}

static gunichar
parse_entity (const gchar *text,
              gint i,
              gint length,
              gint stop,
              gint *new_i,
              GError **error)     
{
  /* parse entity: &amp; &quot; &lt; &gt; &apos;
   * note all names shorter than 5 chars
   */
#define MAX_ENT_LEN 5
  gint ent_start = i + 1;
  gint semicolon = -1;
  gint ent_char = 0;
  gunichar ent_name[MAX_ENT_LEN];
  gboolean bad_entity;
  
  T("entity name start", ent_start);

  *new_i = i;
  
  i = ent_start;
            
  while (i < stop && ent_char < MAX_ENT_LEN)
    {
      gunichar c;
      
      c = g_utf8_get_char (&text[i]);
      ent_name[ent_char] = c;
                
      if (c == ';')
        {
          T("semicolon at end of entity", i);
          semicolon = i;
          break;
        }                
      else
        {
          ++ent_char;
          i = next_char (text, i);
        }
    }

  if (semicolon < 0)
    {
      set_error (text, i, length, 
                 error,
                 G_MARKUP_ERROR_PARSE,
                 _("Text ended in the middle of an entity, or entity name too long to be valid ('&' should begin an entity such as '&quot;')"));
              
      return (gunichar) -1;          
    }
  else
    *new_i = i;
  
  bad_entity = FALSE;
          
  /* switch on length of entity name */
  switch (ent_char)
    {
    case 2:
      if (ent_name[0] == 'l' && ent_name[1] == 't')
        return '<';
      else if (ent_name[0] == 'g' && ent_name[1] == 't')
        return '>';
      else
        bad_entity = TRUE;
      break;

    case 3:
      if (ent_name[0] == 'a' && ent_name[1] == 'm' &&
          ent_name[2] == 'p')
        return '&';
      else
        bad_entity = TRUE;
      break;

    case 4:
      if (ent_name[0] == 'q' && ent_name[1] == 'u' &&
          ent_name[2] == 'o' && ent_name[3] == 't')
        return '"';
      else if (ent_name[0] == 'a' && ent_name[1] == 'p' &&
               ent_name[2] == 'o' && ent_name[3] == 's')
        return '\'';
      else
        bad_entity = TRUE;
      break;

    default:
      bad_entity = TRUE;
      break;
    }
          
  if (bad_entity)
    {
      gchar *ent_str = g_strndup (&text[ent_start], i - ent_start);
            
      set_error (text, ent_start, length, 
                 error,
                 G_MARKUP_ERROR_PARSE,
                 _("Unknown entity '%s' ('&' must begin entities &amp; &quot; &lt; &gt; &apos;)"),
                 ent_str);
            
      g_free (ent_str);
            
      return (gunichar)-1;
    }

  T("semicolon after entity", i);
            
  /* i now points at the semicolon, and we'll skip past it */
#undef MAX_ENT_LEN

  return (gunichar) -1;
}

static gunichar
parse_char_ref (const gchar *text,
                gint i,
                gint length,
                gint stop,
                gint *new_i,
                GError **error)     
{
  /* parse char references such as: &#100; &#x0ff9; */
  gint ent_start = i + 1;
  gint semicolon = -1;
  gint ent_char = 0;
  gboolean is_hex = FALSE;
  gunichar c;
  
  T("char ref start", ent_start);

  *new_i = i;

  if (ent_start >= stop)
    {
      set_error (text, i, length, 
                 error,
                 G_MARKUP_ERROR_PARSE,
                 _("Markup text ended in the middle of a character reference, just after '&#'"));
      
      return (gunichar) -1;          
    }
  
  i = ent_start;

  c = g_utf8_get_char (&text[i]);
  if (c == 'x')
    {
      is_hex = TRUE;
      i = next_char (text, i);
      ent_start = i;
    }
  
  while (i < stop)
    {
      c = g_utf8_get_char (&text[i]);

      if (! (c == ';' ||
             (is_hex && g_unichar_isxdigit (c)) ||
             (!is_hex && g_unichar_isdigit (c))))
        {
          set_error (text, ent_start, length, 
                     error,
                     G_MARKUP_ERROR_PARSE,
                     _("Character reference contained non-digit '%s' ('&#' should begin a reference to a unicode character, such as '&#2342;')"),
                     unthreadsafe_char_str (c));
          
          return (gunichar) -1;          
        }
      
      if (c == ';')
        {
          T("semicolon at end of char ref", i);
          semicolon = i;
          break;
        }                
      else
        {
          ++ent_char;
          i = next_char (text, i);
        }
    }

  if (semicolon < 0)
    {
      set_error (text, i, length, 
                 error,
                 G_MARKUP_ERROR_PARSE,
                 _("Text ended in the middle of an character reference ('&#' should begin a character reference such as '&#2342;')"));
              
      return (gunichar) -1;
    }
  else
    *new_i = i;

  T("semicolon after char ref", i);
  
  {
    gulong l;
    gchar *end = NULL;
    errno = 0;
    if (is_hex)
      l = strtoul (&text[ent_start], &end, 16);
    else
      l = strtoul (&text[ent_start], &end, 10);

    if (errno != 0)
      {
        gchar *ent_str = g_strndup (&text[ent_start], i - ent_start);
        
        set_error (text, ent_start, length, 
                   error,
                   G_MARKUP_ERROR_PARSE,
                   _("Couldn't parse character reference '%s' ('&#' must begin a character reference such as '&#2343;')"),
                 ent_str);
        
        g_free (ent_str);
        
        return (gunichar)-1;
      }
    else
      {
        /* characters XML permits */
        if (l == 0x9 ||
            l == 0xA ||
            l == 0xD ||
            (l >= 0x20 && l <= 0xD7FF) ||
            (l >= 0xE000 && l <= 0xFFFD) ||
            (l >= 0x10000 && l <= 0x10FFFF))
          return l;
        else
          {
            set_error (text, ent_start, length, 
                       error,
                       G_MARKUP_ERROR_PARSE,
                       _("Character code %#lx is not allowed in XML documents or is not a valid Unicode character"),
                       l);
            
            return (gunichar)-1;
          }
      }
  }
}

static gchar*
unescape_text (const gchar *text,
               gint i,
               gint length,
               gint stop,
               gboolean *has_nonwhitespace,
               GError **error)
{
  GString *str;
  gchar *ret;

  T("unescaping text start", i);
  
  *has_nonwhitespace = FALSE;
  
  str = g_string_new ("");
  
  while (i < stop)
    {
      gunichar c = g_utf8_get_char (&text[i]);

      if (!*has_nonwhitespace &&
          !g_unichar_isspace (c))
        *has_nonwhitespace = TRUE;
      
      switch (c)
        {
        case '&':
          {
            GError *err;
            gint next_i;
            gunichar ent;
            
            if (i < stop)
              {
                /* See if it's a character reference */
                next_i = next_char (text, i);

                err = NULL;
                if (g_utf8_get_char (&text[next_i]) == '#')
                  {
                    i = next_i;
                    ent = parse_char_ref (text, i, length, stop, &next_i, &err);
                  }
                else
                  ent = parse_entity (text, i, length, stop, &next_i, &err);

                if (err)
                  {
                    if (error)
                      *error = err;
                    else
                      g_error_free (err);
                    
                    g_string_free (str, TRUE);
                    
                    return NULL;
                  }

                i = next_i;
                g_string_append (str, unthreadsafe_char_str (ent));
              }
            else
              {
                set_error (text, i, length, error,
                           G_MARKUP_ERROR_PARSE,
                           _("Document ended just after an '&', '&' should begin an entity or character reference."));
                
                g_string_free (str, TRUE);

                return NULL;
              }
          }
          break;

        case '<':
        case '>':
          set_error (text, i, length, 
                     error,
                     G_MARKUP_ERROR_PARSE,
                     _("'<' or '>' character not allowed here; only allowed around tags, for example <bold> or <paragraph>. Elsewhere, encode these characters as the entities &lt; and &gt;"));
          
          g_string_free (str, TRUE);
              
          return NULL;                    
          break;
          
        default:
          g_string_append (str, unthreadsafe_char_str (c));
          break;
        }
      
      i = next_char (text, i);
    }

  ret = str->str;
  g_string_free (str, FALSE);

  T("unescaping text stop", stop);
  
  return ret;
}

static GMarkupAttribute*
parse_attribute (const gchar *text,
                 gint i,
                 gint length,
                 GMarkupParseFlags flags,
                 gint *new_i,
                 GError **error)
{
  GMarkupAttribute *attr;
  gunichar c;
  gint name_start;
  gint name_end;
  gint value_start;
  gint value_end;
  gchar *value;
  GError *err;
  gboolean has_nonwhitespace;

  T("attribute name start", i);
  
  *new_i = i;
  
  name_start = i;

  c = g_utf8_get_char (&text[i]);

  if (!is_name_start_char (c))
    {
      set_error (text,
                 i, length,
                 error, 
                 G_MARKUP_ERROR_PARSE,
                 _("Character '%s' is not valid at the start of an attribute name"),
                 unthreadsafe_char_str (c));
      return NULL;
    }

  err = NULL;
  name_end = find_name_end (text, name_start, length, flags, &err);

  if (err)
    {
      if (error)
        *error = err;
      else
        g_error_free (err);
      
      return NULL;
    }

  T("attribute name end", name_end);
  
  i = name_end;
  
  if (name_end >= length)
    {
      set_error (text, i, length, 
                 error,
                 G_MARKUP_ERROR_PARSE,
                 _("Document ended just after attribute name"));
      return NULL;
    }
  
  c = g_utf8_get_char (&text[i]);

  if (c != '=')
    {
      set_error (text, i, length,
                 error,
                 G_MARKUP_ERROR_PARSE,
                 _("Attribute name must be immediately followed by an '=' character"));
      return NULL;
    }

  T("equals sign", i);
  
  i = next_char (text, i);
  
  c = g_utf8_get_char (&text[i]);

  if (c != '"')
    {
      set_error (text, i, length,
                 error,
                 G_MARKUP_ERROR_PARSE,
                 _("'=' character after attribute must be immediately followed by an '\"' character"));
      return NULL;
    }

  T("open quote", i);
  
  i = next_char (text, i);
  value_start = i;
  value_end = -1;
  while (i < length)
    {
      c = g_utf8_get_char (&text[i]);

      switch (c)
        {
        case '"':
          value_end = i;
          goto out;
          break;

        case '<':
        case '>':
          {
            set_error (text, i, length,
                       error,
                       G_MARKUP_ERROR_PARSE,
                       _("Character '%c' found inside an attribute value; perhaps your attribute value is missing the closing quotation mark '\"'"),
                       (char)c);
            return NULL;
          }
          break;
          
        default:
          break;
        }

      i = next_char (text, i);
    }

 out:
  
  if (value_end < 0)
    {
      set_error (text, value_start, length,
                 error,
                 G_MARKUP_ERROR_PARSE,
                 _("Ran out of text before a quote mark ('\"') was seen at the end of an attribute value"));

      return NULL;
    }

  g_assert (value_end >= value_start);
  g_assert (i == value_end);

  if (value_end >= length)
    {
      set_error (text, i, length,
                 error,
                 G_MARKUP_ERROR_PARSE,
                 _("Text ends immediately after an attribute value, before the element was closed"));

      return NULL;
    }

  T("close quote", value_end);
  
  err = NULL;
  value = unescape_text (text, value_start, length, value_end,
                         &has_nonwhitespace, &err);

  if (err)
    {
      if (error)
        *error = err;
      else
        g_error_free (err);
      
      return NULL;
    }

  attr = attribute_new (NULL, NULL);

  attr->name = g_strndup (&text[name_start], name_end - name_start);
  attr->value = value;
  
  g_assert (i < length);
  i = next_char (text, i);
  
  *new_i = i;

  T("char after quote", i);

#if 0
  printf ("attribute name: %s\n", attr->name);
  printf ("attribute valu: %s\n", attr->value);
#endif
  
  return attr;
}

static GList*
parse_child_list (const gchar *text,
                  gint i,
                  gint length,
                  GMarkupParseFlags flags,
                  gint *new_i,
                  GError **error)
{
  GList *list = NULL;
  GError *err;
  gint text_start;
  gboolean has_nonwhitespace = FALSE;
  gboolean tmp;
  gint j;

  T("start of child list", i);
  
  *new_i = i;

  text_start = i;
  
  while (i < length)
    {
      gunichar c = g_utf8_get_char (&text[i]);

      if (c == '<')
        {
          GMarkupNode *node;
          
          if (text_start != i)
            {
              gchar *str;

              T("start of text node", text_start);
              T("end of text node", i);
              
              err = NULL;
              str = unescape_text (text, text_start,
                                   length, i,
                                   &tmp,
                                   &err);

              if (err)
                {
                  if (error)
                    *error = err;
                  else
                    g_error_free (err);
                  
                  free_node_list (list);
              
                  return NULL;
                }

              if (tmp)
                has_nonwhitespace = tmp;
              
              /* FIXME gratuituous string copy */
              list = g_list_prepend (list,
                                     g_markup_node_new_text (str));
              g_free (str);              
            }

          if ((i+1) < length &&
              text[i+1] == '/')
            {
              /* This is a close tag,
               * so we're finished.
               * the parse_element that called
               * us will check that the close
               * tag matches
               */
              goto finished;
            }
          else
            {
              /* An open tag, so recurse */
              
              T("start of element", i);
          
              err = NULL;
              node = parse_element (text, i, length,
                                    flags, &j, &err);
              i = j;
          
              if (err)
                {
                  if (error)
                    *error = err;
                  else
                    g_error_free (err);
              
                  free_node_list (list);
              
                  return NULL;
                }

              list = g_list_prepend (list, node);

              text_start = i;
            }
        }
      else
        i = next_char (text, i);
    }

  if (text_start != i)
    {
      gchar *str;

      T("start of text node", text_start);
      T("end of text node", i);
      
      err = NULL;
      str = unescape_text (text, text_start,
                           length, i,
                           &tmp,
                           &err);

      if (err)
        {
          if (error)
            *error = err;
          else
            g_error_free (err);
                  
          free_node_list (list);
              
          return NULL;
        }

      if (tmp)
        has_nonwhitespace = tmp;
      
      /* FIXME gratuituous string copy */
      list = g_list_prepend (list,
                             g_markup_node_new_text (str));
      g_free (str);
    }

 finished:
  
  *new_i = i;

  /* If we have text nodes that contain non-whitespace, we don't filter
   * out the text nodes. If all text nodes are just whitespace, then
   * we nuke them all. If we filter, we reverse the list at the
   * same time. The PRESERVE_ALL_WHITESPACE flag turns off the filter
   * behavior.
   */
  if (!has_nonwhitespace &&
      (flags & G_MARKUP_PRESERVE_ALL_WHITESPACE) == 0)
    {
      GList *new_list = NULL;
      GList *tmp_list;

      tmp_list = list;
      while (tmp_list != NULL)
        {
          GMarkupNode *node = tmp_list->data;

          if (node->type == G_MARKUP_NODE_TEXT)
            g_markup_node_free (node);
          else
            new_list = g_list_prepend (new_list, node);

          tmp_list = g_list_next (tmp_list);
        }

      g_list_free (list);
      list = new_list;
    }
  else
    list = g_list_reverse (list); /* no filter, just reverse */
  
  return list;
}

static GList*
parse_attribute_list (const gchar *text,
                      gint i,
                      gint length,
                      GMarkupParseFlags flags,
                      gint *new_i,
                      GError **error)
{
  GList *list = NULL;
  GError *err;
  gint j;

  T("start of attr list", i);
  
  *new_i = i;

  while (i < length)
    {
      GMarkupAttribute *attr;
      gunichar c;
      
      i = skip_spaces (text, i, length);
      
      T("after attr list leading ws", i);
      
      c = g_utf8_get_char (&text[i]);
      if (c == '>' || c == '/')
        break;
      
      err = NULL;
      attr = parse_attribute (text, i, length,
                              flags, &j, &err);
      i = j;

      if (err)
        {
          if (error)
            *error = err;
          else
            g_error_free (err);
          
          free_attribute_list (list);

          return NULL;
        }

      list = g_list_prepend (list, attr);
      
      i = skip_spaces (text, i, length);

      T("after attr list trailing ws", i);
      
      c = g_utf8_get_char (&text[i]);
      if (c == '>' || c == '/')
        break;
    }
  
  *new_i = i;

  T("after attr list", i);
  
  return list;
}

static GMarkupNode*
parse_element (const gchar *text,
               gint i,
               gint length,
               GMarkupParseFlags flags,
               gint *new_i,
               GError **error)
{
  gunichar c;
  gint name_start;
  gint name_end;
  GError *err;
  GList *attr_list;
  GList *child_list;
  gint close_name_start;
  gint close_name_end;
  GMarkupNodeElement *node;
  gint j;

  T("start of element", i);
  
  *new_i = i;
  
  c = g_utf8_get_char (&text[i]);

  if (c != '<')
    {
      set_error (text,
                 i, length,
                 error, 
                 G_MARKUP_ERROR_PARSE,
                 _("Missing '<' at start of element"));
      return NULL;
    }

  i = next_char (text, i);

  if (i >= length)
    {
      set_error (text, i, length, 
                 error,
                 G_MARKUP_ERROR_PARSE,
                 _("Document ended just after '<' character"));
      return NULL;
    }
  
  name_start = i;
  
  c = g_utf8_get_char (&text[i]);

  /* Parse comments and processing instructions as passthru nodes */

  if (c == '?' || c == '!')
    {
      /* Scan for '>' */
      while (c != '>')
        {
          i = next_char(text, i);
          if (i >= length)
            break;
          
          c = g_utf8_get_char (&text[i]);
        }

      if (c != '>')
        {
          set_error (text,
                     i, length,
                     error,
                     G_MARKUP_ERROR_PARSE,
                     _("No closing '>' found for a <? or <!"));

          return NULL;
        }
      else
        {
          GMarkupNodePassthrough *pnode;
          
          i = next_char (text, i);

          *new_i = i;
          
          pnode = g_markup_node_new_passthrough ("");

          g_free (pnode->passthrough_text);
          pnode->passthrough_text = g_strndup (&text[name_start],
                                               i - name_start - 1);

          return (GMarkupNode*)pnode;
        }
    }
  
  /* Regular element, not a comment or PI */
  if (!is_name_start_char (c))
    {
      set_error (text,
                 i, length,
                 error, 
                 G_MARKUP_ERROR_PARSE,
                 _("Character '%s' is not valid at the start of an element name"),
                 unthreadsafe_char_str (c));
      return NULL;
    }

  err = NULL;
  name_end = find_name_end (text, name_start, length, flags, &err);
  if (err)
    {
      if (error)
        *error = err;
      else
        g_error_free (err);
      
      return NULL;
    }

  i = name_end;
  
  if (name_end >= length)
    {
      set_error (text, i, length, 
                 error,
                 G_MARKUP_ERROR_PARSE,
                 _("Document ended just after element name, no '>' seen"));
      return NULL;
    }

  T("end of elem name", name_end);
  
  i = skip_spaces (text, i, length);

  if (i >= length)
    {
      set_error (text, i, length, 
                 error,
                 G_MARKUP_ERROR_PARSE,
                 _("Document ended just after element name, no '>' seen"));
      return NULL;
    }

  err = NULL;
  attr_list = parse_attribute_list (text, i, length,
                                    flags, &j, &err);
  i = j;
  
  if (err)
    {
      if (error)
        *error = err;
      else
        g_error_free (err);
      
      return NULL;
    }
  
  c = g_utf8_get_char (&text[i]);
  if (!(c == '>' || c == '/'))
    {
      set_error (text, i, length, 
                 error,
                 G_MARKUP_ERROR_PARSE,
                 _("Elements should be closed with '>' or '/>', not with '%s'"),
                 unthreadsafe_char_str (c));

      free_attribute_list (attr_list);
      
      return NULL;
    }

  if (c == '/')
    {
      i = next_char (text, i);
      c = g_utf8_get_char (&text[i]);
      if (c != '>')
        {
          set_error (text, i, length, 
                     error,
                     G_MARKUP_ERROR_PARSE,
                     _("Element ended just after '/', no '>' seen; empty elements should end with '/>'"));
          
          free_attribute_list (attr_list);
          
          return NULL;
        }

      child_list = NULL;
    }
  else
    {
      i = next_char (text, i);
  
      T("start of child list", i);
      child_list = parse_child_list (text, i, length,
                                     flags, &j, &err);
      i = j;
  
      if (err)
        {
          if (error)
            *error = err;
          else
            g_error_free (err);

          free_attribute_list (attr_list);
      
          return NULL;
        }

      T("end of child list", i);
  
      /* Should now be at our close tag, absorb it. */
      c = g_utf8_get_char (&text[i]);
      if (c != '<')
        {
          set_error (text, name_start, length, 
                     error,
                     G_MARKUP_ERROR_PARSE,
                     _("Close tag not found at end of element"));

          free_attribute_list (attr_list);
          free_node_list (child_list);
      
          return NULL;
        }
  
      i = next_char (text, i);
      if (i >= length)
        {

          set_error (text, i, length,
                     error,
                     G_MARKUP_ERROR_PARSE,
                     _("Close tag ends just after '<' character"));

          free_attribute_list (attr_list);
          free_node_list (child_list);
      
          return NULL;
        }

      c = g_utf8_get_char (&text[i]);
      if (c != '/')
        {
          set_error (text, i, length,
                     error,
                     G_MARKUP_ERROR_PARSE,
                     _("Close tag should begin with '</', '/' character is missing"));

          free_attribute_list (attr_list);
          free_node_list (child_list);
      
          return NULL;
        }

      i = next_char (text, i);
      if (i >= length)
        {
          set_error (text, i, length,
                     error,
                     G_MARKUP_ERROR_PARSE,
                     _("Close tag ends just after '/' character"));

          free_attribute_list (attr_list);
          free_node_list (child_list);
      
          return NULL;
        }
  
      /* Do a bytewise strcmp against the name of the opening tag */
      close_name_start = i;

      T("start of close name", close_name_start);
  
      err = NULL;
      close_name_end = find_name_end (text, close_name_start, length, flags, &err);
      if (err)
        {
          if (error)
            *error = err;
          else
            g_error_free (err);

          free_attribute_list (attr_list);
          free_node_list (child_list);
      
          return NULL;
        }
  
      i = close_name_end;
  
      if (close_name_end >= length)
        {
          set_error (text, i, length, 
                     error,
                     G_MARKUP_ERROR_PARSE,
                     _("Document ended just after element name in close tag, no '>' seen"));

          free_attribute_list (attr_list);
          free_node_list (child_list);

          return NULL;
        }

      T("end of close name", close_name_end);
  
      c = g_utf8_get_char (&text[i]);
      if (c != '>')
        {
          set_error (text, i, length, 
                     error,
                     G_MARKUP_ERROR_PARSE,
                     _("Document ended just after close tag name, no '>' seen"));

          free_attribute_list (attr_list);
          free_node_list (child_list);
      
          return NULL;
        }

      {
        gchar *open_name = g_strndup (&text[name_start],
                                      name_end - name_start);
        gchar *close_name = g_strndup (&text[close_name_start],
                                       close_name_end - close_name_start);

        if (strcmp (open_name, close_name) != 0)
          {
            set_error (text, i, length,
                       error,
                       G_MARKUP_ERROR_PARSE,
                       _("Close tag '%s' does not match opening tag '%s'"),
                       close_name, open_name);

            free_attribute_list (attr_list);
            free_node_list (child_list);

            g_free (open_name);
            g_free (close_name);

            return NULL;
          }
        
        g_free (open_name);
        g_free (close_name);
      }
    } /* If this is a <tag></tag> not a <tag/> */
  
  /* We finally have everything; skip past the final > and
   * assemble the node.
   */
  i = next_char (text, i);
  *new_i = i;

  {
    gchar *open_name = g_strndup (&text[name_start],
                                  name_end - name_start);
    
    node = g_markup_node_new_element (open_name);

    g_free (open_name);
  }
    
  node->children = child_list;
  node->attributes = attr_list;
  
  return (GMarkupNode*) node;
}

GMarkupNode*
g_markup_node_from_string (const gchar *text,
                           gint length,
                           GMarkupParseFlags flags,
                           GError **error)
{
  GList *list;
  GMarkupNode *node;
  GList *tmp_list;
  
  list = g_markup_nodes_from_string (text, length, flags, error);

  if (list == NULL)
    return NULL;

  /* return the first one. */
  node = list->data;

  tmp_list = list->next;
  while (tmp_list)
    {
      g_markup_node_free (tmp_list->data);

      tmp_list = g_list_next (tmp_list);
    }

  g_list_free (list);

  return node;
}

/* Writing a string */

static void
append_escaped_text (GString *str,
                     const gchar *text)
{
  const gchar *p;

  p = text;

  while (*p)
    {
      const gchar *next;
      next = g_utf8_next_char (p);
      
      switch (*p)
        {
        case '&':
          g_string_append (str, "&amp;");
          break;

        case '<':
          g_string_append (str, "&lt;");
          break;

        case '>':
          g_string_append (str, "&gt;");
          break;

        case '\'':
          g_string_append (str, "&apos;");
          break;

        case '"':
          g_string_append (str, "&quot;");
          break;
          
        default:
          g_string_append_len (str, p, next - p);
          break;
        }

      p = next;
    }
}

static void
append_attributes (GString *str,
                   GList *list)
{
  GList *tmp_list;
  
  tmp_list = list;
  while (tmp_list)
    {
      GMarkupAttribute *attr = tmp_list->data;

      g_string_append (str, attr->name);
      g_string_append (str, "=\"");
      /* FIXME not the same as for outside-attribute text */
      append_escaped_text (str, attr->value);
      g_string_append (str, "\" ");
      
      tmp_list = g_list_next (tmp_list);
    }

  if (list)
    {
      /* if we appended anything, remove the space at the end */
      g_string_truncate (str, str->len - 1);
    }
}

static void
append_node_list (GString *str,
                  GList *children,
                  int depth,
                  GMarkupToStringFlags flags)
{
  GList *tmp_list;

  tmp_list = children;

  while (tmp_list != NULL)
    {
      GMarkupNode *node = tmp_list->data;

      append_node (str, node, depth, flags);
      
      tmp_list = g_list_next (tmp_list);
    }
}

static void
indentation (GString *str,
             int depth,
             GMarkupToStringFlags flags)
{
  if ((flags & G_MARKUP_NO_FORMATTING) == 0)
    {
      /* indent */
      int i = 0;
      while (i < depth)
        {
          g_string_append_c (str, ' ');
          ++i;
        }
    }
}

static gboolean
nonwhitespace_nodes (GList *children)
{
  GList *tmp_list;

  tmp_list = children;

  while (tmp_list != NULL)
    {
      GMarkupNode *node = tmp_list->data;

      if (node->type == G_MARKUP_NODE_TEXT)
        {
          gchar *iter = node->text.text;
          while (*iter)
            {
              if (!g_unichar_isspace (g_utf8_get_char (iter)))
                return TRUE;

              iter = g_utf8_next_char (iter);
            }
        }

      tmp_list = g_list_next (tmp_list);
    }

  return FALSE; /* no non-whitespace found */
}

static void
append_node (GString *str,
             GMarkupNode *node,
             int depth,
             GMarkupToStringFlags flags)
{
  switch (node->type)
    {
    case G_MARKUP_NODE_TEXT:
      append_escaped_text (str, node->text.text);
      break;

    case G_MARKUP_NODE_PASSTHROUGH:
      g_string_append_c (str, '<');
      g_string_append (str, ((GMarkupNodePassthrough*)node)->passthrough_text);
      g_string_append_c (str, '>');
      break;
      
    case G_MARKUP_NODE_ELEMENT:
      {
        if (node->element.children != NULL)
          indentation (str, depth, flags);

        g_string_append_c (str, '<');
        g_string_append (str, node->element.name);
        if (node->element.attributes)
          {
            g_string_append_c (str, ' ');
            append_attributes (str, node->element.attributes);
          }

        if (node->element.children == NULL)
          g_string_append_c (str, '/');
        
        g_string_append_c (str, '>');

        if (node->element.children != NULL)
          {
            if ((flags & G_MARKUP_NO_FORMATTING) == 0 &&
                nonwhitespace_nodes (node->element.children))
              {
                /* If we have non-whitespace text immediately under this
                 * node, we can't do formatting for the child nodes,
                 * we have to dump them literally. So turn on
                 * G_MARKUP_NO_FORMATTING if it's off and we find nonwhitespace
                 * text nodes.
                 */
            
                append_node_list (str, node->element.children, depth + 1,
                                  flags & G_MARKUP_NO_FORMATTING);
              }
            else
              {
                /* If we don't find any non-whitespace text, leave
                 * G_MARKUP_NO_FORMATTING as-is, and put in a newline
                 * after the open-element if the flag is off
                 */

                if ((flags & G_MARKUP_NO_FORMATTING) == 0)
                  g_string_append_c (str, '\n');
            
                append_node_list (str, node->element.children, depth + 1,
                                  flags);
              }
        
            indentation (str, depth, flags);

            g_string_append (str, "</");
            g_string_append (str, node->element.name);
            g_string_append_c (str, '>');

            /* put a newline afterward if formatting is allowed within our
             * parent node
             */
            if ((flags & G_MARKUP_NO_FORMATTING) == 0)
              g_string_append_c (str, '\n');
          }
      }
      break;

    default:
      g_assert_not_reached ();
      break;
    }
}

gchar*
g_markup_node_to_string (GMarkupNode *node, GMarkupToStringFlags flags)
{
  GString *str;
  gchar *retval;
  
  g_return_val_if_fail (node != NULL, NULL);
  
  str = g_string_new ("");

  append_node (str, node, 0, flags);

  retval = str->str;
  g_string_free (str, FALSE);

  return retval;
}

GList*
g_markup_nodes_from_string (const gchar *text,
                            gint length,
                            GMarkupParseFlags flags,
                            GError **error)
{
  gint i;
  const gchar *invalid = NULL;
  GList *nodes = NULL;
  
  g_return_val_if_fail (text != NULL, NULL);

  if (length < 0)
    length = strlen (text);

  if (!g_utf8_validate (text, length, &invalid))
    {

      if (error)
        {
          gchar *before;
          
          before = text_before (text, invalid - text);

          *error = g_error_new (G_MARKUP_ERROR,
                                G_MARKUP_ERROR_BAD_UTF8,
                                _("Invalid UTF-8 character at byte %d in marked-up text. Some text before the bad character was '%s'"),
                                invalid - text,
                                before);
          
          g_free (before);
        }
      
      return NULL;
    }
  
  i = 0;
  while (i < length)
    {
      gunichar c = g_utf8_get_char (&text[i]);

      if (g_unichar_isspace (c))
        i = next_char (text, i);
      else
        break;
    }

  while (i < length)
    {
      gint next_i;
      GMarkupNode *node;

      node = parse_element (text, i, length, flags, &next_i, error);
      
      if (node == NULL)
        {
          free_node_list (nodes);
          return NULL;
        }
      else
        {
          nodes = g_list_append (nodes, node);
        }

      /* Eat whitespace again */
      i = next_i;

      while (i < length)
        {
          gunichar c = g_utf8_get_char (&text[i]);
          
          if (g_unichar_isspace (c))
            i = next_char (text, i);
          else
            break;
        }
    }

  if (nodes == NULL)
    {
      g_set_error (error,
                   G_MARKUP_ERROR,
                   G_MARKUP_ERROR_EMPTY,
                   _("The marked-up text contained nothing but whitespace."));
      
      return NULL;
    }
  else
    return nodes;
}

gchar*
g_markup_nodes_to_string (GList *nodes,
                          GMarkupToStringFlags flags)
{
  GString *str;
  gchar *retval;
  GList *tmp_list;
  
  g_return_val_if_fail (nodes != NULL, NULL);
  
  str = g_string_new ("");

  tmp_list = nodes;
  while (tmp_list != NULL)
    {
      append_node (str, tmp_list->data, 0, flags);

      tmp_list = g_list_next (tmp_list);
    }
  
  retval = str->str;
  g_string_free (str, FALSE);

  return retval;
}

static GMarkupAttribute*
attribute_new (const gchar *name, const gchar *value)
{
  GMarkupAttribute *attr;

  attr = g_new (GMarkupAttribute, 1);

  /* name/value are allowed to be NULL */
  attr->name = g_strdup (name);
  attr->value = g_strdup (value);

  return attr;
}

static void
attribute_free (GMarkupAttribute *attr)
{
  g_free (attr->name);
  g_free (attr->value);
  g_free (attr);
}










/********************************************************************/


/* This file is automatically generated.  DO NOT EDIT!
   Instead, edit gen-table.pl and re-run.  */

#ifndef CHARTABLES_H
#define CHARTABLES_H

#define G_UNICODE_DATA_VERSION "2.1.9"

#define G_UNICODE_LAST_CHAR 0xffff

static char page0[256] = {
  G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, 
  G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, 
  G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, 
  G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, 
  G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, 
  G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, 
  G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, 
  G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, 
  G_UNICODE_SPACE_SEPARATOR, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_CURRENCY_SYMBOL, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OPEN_PUNCTUATION, G_UNICODE_CLOSE_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_MATH_SYMBOL, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_DASH_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_OPEN_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_CLOSE_PUNCTUATION, G_UNICODE_MODIFIER_SYMBOL, 
  G_UNICODE_CONNECT_PUNCTUATION, G_UNICODE_MODIFIER_SYMBOL, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_OPEN_PUNCTUATION, G_UNICODE_MATH_SYMBOL, G_UNICODE_CLOSE_PUNCTUATION, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, 
  G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, 
  G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, 
  G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, 
  G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, 
  G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, 
  G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, 
  G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_CONTROL, 
  G_UNICODE_CONTROL, G_UNICODE_CONTROL, G_UNICODE_SPACE_SEPARATOR, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_CURRENCY_SYMBOL, 
  G_UNICODE_CURRENCY_SYMBOL, G_UNICODE_CURRENCY_SYMBOL, G_UNICODE_CURRENCY_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_INITIAL_PUNCTUATION, G_UNICODE_MATH_SYMBOL, G_UNICODE_DASH_PUNCTUATION, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_LOWERCASE_LETTER, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_FINAL_PUNCTUATION, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_MATH_SYMBOL, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER
};

static char page1[256] = {
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_TITLECASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_TITLECASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_TITLECASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_TITLECASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER
};

static char page2[256] = {
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_MODIFIER_LETTER, 
  G_UNICODE_MODIFIER_LETTER, G_UNICODE_MODIFIER_LETTER, G_UNICODE_MODIFIER_LETTER, 
  G_UNICODE_MODIFIER_LETTER, G_UNICODE_MODIFIER_LETTER, G_UNICODE_MODIFIER_LETTER, 
  G_UNICODE_MODIFIER_LETTER, G_UNICODE_MODIFIER_LETTER, G_UNICODE_MODIFIER_SYMBOL, 
  G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_LETTER, G_UNICODE_MODIFIER_LETTER, 
  G_UNICODE_MODIFIER_LETTER, G_UNICODE_MODIFIER_LETTER, G_UNICODE_MODIFIER_LETTER, 
  G_UNICODE_MODIFIER_LETTER, G_UNICODE_MODIFIER_LETTER, G_UNICODE_MODIFIER_SYMBOL, 
  G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, 
  G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, 
  G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, 
  G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, 
  G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_LETTER, G_UNICODE_MODIFIER_LETTER, 
  G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, 
  G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, 
  G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, 
  G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, 
  G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_UNASSIGNED, G_UNICODE_MODIFIER_LETTER, 
  G_UNICODE_MODIFIER_LETTER, G_UNICODE_MODIFIER_LETTER, G_UNICODE_MODIFIER_LETTER, 
  G_UNICODE_MODIFIER_LETTER, G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, 
  G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED
};

static char page3[256] = {
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_MODIFIER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED
};

static char page4[256] = {
  G_UNICODE_UNASSIGNED, G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED
};

static char page5[256] = {
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_MODIFIER_LETTER, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_UNASSIGNED, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED
};

static char page6[256] = {
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_MODIFIER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_ENCLOSING_MARK, G_UNICODE_ENCLOSING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_MODIFIER_LETTER, G_UNICODE_MODIFIER_LETTER, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_OTHER_SYMBOL, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED
};

static char page9[256] = {
  G_UNICODE_UNASSIGNED, G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_COMBINING_MARK, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_OTHER_LETTER, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_COMBINING_MARK, G_UNICODE_COMBINING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_COMBINING_MARK, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_COMBINING_MARK, G_UNICODE_COMBINING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_COMBINING_MARK, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_UNASSIGNED, G_UNICODE_COMBINING_MARK, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_COMBINING_MARK, G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_COMBINING_MARK, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_COMBINING_MARK, G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_CURRENCY_SYMBOL, 
  G_UNICODE_CURRENCY_SYMBOL, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_SYMBOL, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED
};

static char page10[256] = {
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_COMBINING_MARK, G_UNICODE_COMBINING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_COMBINING_MARK, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_OTHER_LETTER, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_COMBINING_MARK, G_UNICODE_COMBINING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_UNASSIGNED, G_UNICODE_COMBINING_MARK, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED
};

static char page11[256] = {
  G_UNICODE_UNASSIGNED, G_UNICODE_NON_SPACING_MARK, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_COMBINING_MARK, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_OTHER_LETTER, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_COMBINING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_COMBINING_MARK, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_COMBINING_MARK, G_UNICODE_COMBINING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_COMBINING_MARK, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_OTHER_SYMBOL, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_NON_SPACING_MARK, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_COMBINING_MARK, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_COMBINING_MARK, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_COMBINING_MARK, G_UNICODE_COMBINING_MARK, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_UNASSIGNED, G_UNICODE_COMBINING_MARK, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_COMBINING_MARK, G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED
};

static char page12[256] = {
  G_UNICODE_UNASSIGNED, G_UNICODE_COMBINING_MARK, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_COMBINING_MARK, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_COMBINING_MARK, G_UNICODE_COMBINING_MARK, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_UNASSIGNED, G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_COMBINING_MARK, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_COMBINING_MARK, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_COMBINING_MARK, G_UNICODE_COMBINING_MARK, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_UNASSIGNED, G_UNICODE_NON_SPACING_MARK, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_COMBINING_MARK, G_UNICODE_UNASSIGNED, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_COMBINING_MARK, G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_COMBINING_MARK, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED
};

static char page13[256] = {
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_COMBINING_MARK, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_COMBINING_MARK, G_UNICODE_COMBINING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_COMBINING_MARK, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_COMBINING_MARK, G_UNICODE_UNASSIGNED, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_COMBINING_MARK, G_UNICODE_COMBINING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_COMBINING_MARK, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED
};

static char page14[256] = {
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_NON_SPACING_MARK, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_CURRENCY_SYMBOL, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_MODIFIER_LETTER, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_OTHER_SYMBOL, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_MODIFIER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED
};

static char page15[256] = {
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_SYMBOL, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_NON_SPACING_MARK, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_OPEN_PUNCTUATION, 
  G_UNICODE_CLOSE_PUNCTUATION, G_UNICODE_OPEN_PUNCTUATION, 
  G_UNICODE_CLOSE_PUNCTUATION, G_UNICODE_COMBINING_MARK, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, G_UNICODE_COMBINING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED
};

static char page16[256] = {
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED
};

static char page17[256] = {
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED
};

static char page30[256] = {
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED
};

static char page31[256] = {
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_LOWERCASE_LETTER, G_UNICODE_MODIFIER_SYMBOL, 
  G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, 
  G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, 
  G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_UNASSIGNED
};

static char page32[256] = {
  G_UNICODE_SPACE_SEPARATOR, G_UNICODE_SPACE_SEPARATOR, G_UNICODE_SPACE_SEPARATOR, 
  G_UNICODE_SPACE_SEPARATOR, G_UNICODE_SPACE_SEPARATOR, G_UNICODE_SPACE_SEPARATOR, 
  G_UNICODE_SPACE_SEPARATOR, G_UNICODE_SPACE_SEPARATOR, G_UNICODE_SPACE_SEPARATOR, 
  G_UNICODE_SPACE_SEPARATOR, G_UNICODE_SPACE_SEPARATOR, G_UNICODE_SPACE_SEPARATOR, 
  G_UNICODE_FORMAT, G_UNICODE_FORMAT, G_UNICODE_FORMAT, G_UNICODE_FORMAT, 
  G_UNICODE_DASH_PUNCTUATION, G_UNICODE_DASH_PUNCTUATION, 
  G_UNICODE_DASH_PUNCTUATION, G_UNICODE_DASH_PUNCTUATION, 
  G_UNICODE_DASH_PUNCTUATION, G_UNICODE_DASH_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_INITIAL_PUNCTUATION, G_UNICODE_FINAL_PUNCTUATION, 
  G_UNICODE_OPEN_PUNCTUATION, G_UNICODE_INITIAL_PUNCTUATION, 
  G_UNICODE_INITIAL_PUNCTUATION, G_UNICODE_FINAL_PUNCTUATION, 
  G_UNICODE_OPEN_PUNCTUATION, G_UNICODE_INITIAL_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_LINE_SEPARATOR, G_UNICODE_PARAGRAPH_SEPARATOR, G_UNICODE_FORMAT, 
  G_UNICODE_FORMAT, G_UNICODE_FORMAT, G_UNICODE_FORMAT, G_UNICODE_FORMAT, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_INITIAL_PUNCTUATION, 
  G_UNICODE_FINAL_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_CONNECT_PUNCTUATION, 
  G_UNICODE_CONNECT_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_OPEN_PUNCTUATION, G_UNICODE_CLOSE_PUNCTUATION, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_FORMAT, G_UNICODE_FORMAT, G_UNICODE_FORMAT, 
  G_UNICODE_FORMAT, G_UNICODE_FORMAT, G_UNICODE_FORMAT, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_OPEN_PUNCTUATION, G_UNICODE_CLOSE_PUNCTUATION, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_OPEN_PUNCTUATION, 
  G_UNICODE_CLOSE_PUNCTUATION, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_CURRENCY_SYMBOL, G_UNICODE_CURRENCY_SYMBOL, G_UNICODE_CURRENCY_SYMBOL, 
  G_UNICODE_CURRENCY_SYMBOL, G_UNICODE_CURRENCY_SYMBOL, G_UNICODE_CURRENCY_SYMBOL, 
  G_UNICODE_CURRENCY_SYMBOL, G_UNICODE_CURRENCY_SYMBOL, G_UNICODE_CURRENCY_SYMBOL, 
  G_UNICODE_CURRENCY_SYMBOL, G_UNICODE_CURRENCY_SYMBOL, G_UNICODE_CURRENCY_SYMBOL, 
  G_UNICODE_CURRENCY_SYMBOL, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_ENCLOSING_MARK, G_UNICODE_ENCLOSING_MARK, 
  G_UNICODE_ENCLOSING_MARK, G_UNICODE_ENCLOSING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED
};

static char page33[256] = {
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_UPPERCASE_LETTER, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_UPPERCASE_LETTER, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_OTHER_SYMBOL, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_OTHER_SYMBOL, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_LETTER_NUMBER, 
  G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, 
  G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, 
  G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, 
  G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, 
  G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, 
  G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, 
  G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, 
  G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, 
  G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, 
  G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, 
  G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, 
  G_UNICODE_LETTER_NUMBER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED
};

static char page34[256] = {
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED
};

static char page35[256] = {
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OPEN_PUNCTUATION, 
  G_UNICODE_CLOSE_PUNCTUATION, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED
};

static char page36[256] = {
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED
};

static char page37[256] = {
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED
};

static char page38[256] = {
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED
};

static char page39[256] = {
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_SYMBOL, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED
};

static char page48[256] = {
  G_UNICODE_SPACE_SEPARATOR, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_MODIFIER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_LETTER_NUMBER, 
  G_UNICODE_OPEN_PUNCTUATION, G_UNICODE_CLOSE_PUNCTUATION, 
  G_UNICODE_OPEN_PUNCTUATION, G_UNICODE_CLOSE_PUNCTUATION, 
  G_UNICODE_OPEN_PUNCTUATION, G_UNICODE_CLOSE_PUNCTUATION, 
  G_UNICODE_OPEN_PUNCTUATION, G_UNICODE_CLOSE_PUNCTUATION, 
  G_UNICODE_OPEN_PUNCTUATION, G_UNICODE_CLOSE_PUNCTUATION, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OPEN_PUNCTUATION, G_UNICODE_CLOSE_PUNCTUATION, 
  G_UNICODE_OPEN_PUNCTUATION, G_UNICODE_CLOSE_PUNCTUATION, 
  G_UNICODE_OPEN_PUNCTUATION, G_UNICODE_CLOSE_PUNCTUATION, 
  G_UNICODE_OPEN_PUNCTUATION, G_UNICODE_CLOSE_PUNCTUATION, 
  G_UNICODE_DASH_PUNCTUATION, G_UNICODE_OPEN_PUNCTUATION, 
  G_UNICODE_CLOSE_PUNCTUATION, G_UNICODE_CLOSE_PUNCTUATION, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, 
  G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, 
  G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, G_UNICODE_LETTER_NUMBER, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_DASH_PUNCTUATION, G_UNICODE_MODIFIER_LETTER, G_UNICODE_MODIFIER_LETTER, 
  G_UNICODE_MODIFIER_LETTER, G_UNICODE_MODIFIER_LETTER, G_UNICODE_MODIFIER_LETTER, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_MODIFIER_LETTER, 
  G_UNICODE_MODIFIER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_CONNECT_PUNCTUATION, G_UNICODE_MODIFIER_LETTER, 
  G_UNICODE_MODIFIER_LETTER, G_UNICODE_MODIFIER_LETTER, G_UNICODE_UNASSIGNED
};

static char page49[256] = {
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED
};

static char page50[256] = {
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, G_UNICODE_OTHER_NUMBER, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_UNASSIGNED
};

static char page51[256] = {
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_OTHER_LETTER
};

static char page78[256] = {
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED
};

static char page159[256] = {
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER
};

static char page172[256] = {
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED
};

static char page215[256] = {
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, G_UNICODE_SURROGATE, 
  G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, 
  G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, 
  G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, 
  G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, 
  G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, 
  G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, 
  G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, 
  G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, 
  G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, 
  G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, 
  G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, 
  G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, 
  G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, 
  G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, 
  G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, 
  G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, 
  G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, 
  G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, 
  G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, 
  G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, 
  G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, 
  G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, 
  G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE
};

static char page216[256] = {
  G_UNICODE_SURROGATE, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED
};

static char page219[256] = {
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_SURROGATE, G_UNICODE_SURROGATE, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_SURROGATE
};

static char page220[256] = {
  G_UNICODE_SURROGATE, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED
};

static char page223[256] = {
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_SURROGATE
};

static char page224[256] = {
  G_UNICODE_PRIVATE_USE, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED
};

static char page248[256] = {
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_PRIVATE_USE
};

static char page250[256] = {
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED
};

static char page251[256] = {
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_NON_SPACING_MARK, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER
};

static char page253[256] = {
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OPEN_PUNCTUATION, 
  G_UNICODE_CLOSE_PUNCTUATION, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED
};

static char page254[256] = {
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_NON_SPACING_MARK, 
  G_UNICODE_NON_SPACING_MARK, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_DASH_PUNCTUATION, 
  G_UNICODE_DASH_PUNCTUATION, G_UNICODE_CONNECT_PUNCTUATION, 
  G_UNICODE_CONNECT_PUNCTUATION, G_UNICODE_OPEN_PUNCTUATION, 
  G_UNICODE_CLOSE_PUNCTUATION, G_UNICODE_OPEN_PUNCTUATION, 
  G_UNICODE_CLOSE_PUNCTUATION, G_UNICODE_OPEN_PUNCTUATION, 
  G_UNICODE_CLOSE_PUNCTUATION, G_UNICODE_OPEN_PUNCTUATION, 
  G_UNICODE_CLOSE_PUNCTUATION, G_UNICODE_OPEN_PUNCTUATION, 
  G_UNICODE_CLOSE_PUNCTUATION, G_UNICODE_OPEN_PUNCTUATION, 
  G_UNICODE_CLOSE_PUNCTUATION, G_UNICODE_OPEN_PUNCTUATION, 
  G_UNICODE_CLOSE_PUNCTUATION, G_UNICODE_OPEN_PUNCTUATION, 
  G_UNICODE_CLOSE_PUNCTUATION, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_CONNECT_PUNCTUATION, 
  G_UNICODE_CONNECT_PUNCTUATION, G_UNICODE_CONNECT_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_DASH_PUNCTUATION, 
  G_UNICODE_OPEN_PUNCTUATION, G_UNICODE_CLOSE_PUNCTUATION, 
  G_UNICODE_OPEN_PUNCTUATION, G_UNICODE_CLOSE_PUNCTUATION, 
  G_UNICODE_OPEN_PUNCTUATION, G_UNICODE_CLOSE_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_MATH_SYMBOL, G_UNICODE_DASH_PUNCTUATION, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_CURRENCY_SYMBOL, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_FORMAT
};

static char page255[256] = {
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_CURRENCY_SYMBOL, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OPEN_PUNCTUATION, 
  G_UNICODE_CLOSE_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_DASH_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, G_UNICODE_DECIMAL_NUMBER, 
  G_UNICODE_DECIMAL_NUMBER, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_UPPERCASE_LETTER, G_UNICODE_UPPERCASE_LETTER, 
  G_UNICODE_OPEN_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_CLOSE_PUNCTUATION, G_UNICODE_MODIFIER_SYMBOL, 
  G_UNICODE_CONNECT_PUNCTUATION, G_UNICODE_MODIFIER_SYMBOL, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_LOWERCASE_LETTER, G_UNICODE_LOWERCASE_LETTER, 
  G_UNICODE_OPEN_PUNCTUATION, G_UNICODE_MATH_SYMBOL, G_UNICODE_CLOSE_PUNCTUATION, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_PUNCTUATION, G_UNICODE_OPEN_PUNCTUATION, 
  G_UNICODE_CLOSE_PUNCTUATION, G_UNICODE_OTHER_PUNCTUATION, 
  G_UNICODE_CONNECT_PUNCTUATION, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_MODIFIER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_MODIFIER_LETTER, G_UNICODE_MODIFIER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, G_UNICODE_OTHER_LETTER, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_CURRENCY_SYMBOL, G_UNICODE_CURRENCY_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MODIFIER_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_CURRENCY_SYMBOL, 
  G_UNICODE_CURRENCY_SYMBOL, G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, G_UNICODE_MATH_SYMBOL, 
  G_UNICODE_MATH_SYMBOL, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED, 
  G_UNICODE_UNASSIGNED, G_UNICODE_OTHER_SYMBOL, G_UNICODE_OTHER_SYMBOL, 
  G_UNICODE_UNASSIGNED, G_UNICODE_UNASSIGNED
};

static char *type_table[256] = {
  page0,
  page1,
  page2,
  page3,
  page4,
  page5,
  page6,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  page9,
  page10,
  page11,
  page12,
  page13,
  page14,
  page15,
  page16,
  page17,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  page30,
  page31,
  page32,
  page33,
  page34,
  page35,
  page36,
  page37,
  page38,
  page39,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  page48,
  page49,
  page50,
  page51,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  page78,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  page159,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  (char *) G_UNICODE_OTHER_LETTER,
  page172,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  page215,
  page216,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  page219,
  page220,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  page223,
  page224,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  (char *) G_UNICODE_UNASSIGNED,
  page248,
  (char *) G_UNICODE_OTHER_LETTER,
  page250,
  page251,
  (char *) G_UNICODE_OTHER_LETTER,
  page253,
  page254,
  page255
};

static unsigned short attrpage0[256] = {
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 
  0x0006, 0x0007, 0x0008, 0x0009, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067, 
  0x0068, 0x0069, 0x006a, 0x006b, 0x006c, 0x006d, 0x006e, 0x006f, 0x0070, 
  0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077, 0x0078, 0x0079, 
  0x007a, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0041, 0x0042, 
  0x0043, 0x0044, 0x0045, 0x0046, 0x0047, 0x0048, 0x0049, 0x004a, 0x004b, 
  0x004c, 0x004d, 0x004e, 0x004f, 0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 
  0x0055, 0x0056, 0x0057, 0x0058, 0x0059, 0x005a, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x00e0, 0x00e1, 0x00e2, 0x00e3, 0x00e4, 0x00e5, 
  0x00e6, 0x00e7, 0x00e8, 0x00e9, 0x00ea, 0x00eb, 0x00ec, 0x00ed, 0x00ee, 
  0x00ef, 0x00f0, 0x00f1, 0x00f2, 0x00f3, 0x00f4, 0x00f5, 0x00f6, 0x0000, 
  0x00f8, 0x00f9, 0x00fa, 0x00fb, 0x00fc, 0x00fd, 0x00fe, 0x0000, 0x00c0, 
  0x00c1, 0x00c2, 0x00c3, 0x00c4, 0x00c5, 0x00c6, 0x00c7, 0x00c8, 0x00c9, 
  0x00ca, 0x00cb, 0x00cc, 0x00cd, 0x00ce, 0x00cf, 0x00d0, 0x00d1, 0x00d2, 
  0x00d3, 0x00d4, 0x00d5, 0x00d6, 0x0000, 0x00d8, 0x00d9, 0x00da, 0x00db, 
  0x00dc, 0x00dd, 0x00de, 0x0178
};

static unsigned short attrpage1[256] = {
  0x0101, 0x0100, 0x0103, 0x0102, 0x0105, 0x0104, 0x0107, 0x0106, 0x0109, 
  0x0108, 0x010b, 0x010a, 0x010d, 0x010c, 0x010f, 0x010e, 0x0111, 0x0110, 
  0x0113, 0x0112, 0x0115, 0x0114, 0x0117, 0x0116, 0x0119, 0x0118, 0x011b, 
  0x011a, 0x011d, 0x011c, 0x011f, 0x011e, 0x0121, 0x0120, 0x0123, 0x0122, 
  0x0125, 0x0124, 0x0127, 0x0126, 0x0129, 0x0128, 0x012b, 0x012a, 0x012d, 
  0x012c, 0x012f, 0x012e, 0x0069, 0x0049, 0x0133, 0x0132, 0x0135, 0x0134, 
  0x0137, 0x0136, 0x0000, 0x013a, 0x0139, 0x013c, 0x013b, 0x013e, 0x013d, 
  0x0140, 0x013f, 0x0142, 0x0141, 0x0144, 0x0143, 0x0146, 0x0145, 0x0148, 
  0x0147, 0x0000, 0x014b, 0x014a, 0x014d, 0x014c, 0x014f, 0x014e, 0x0151, 
  0x0150, 0x0153, 0x0152, 0x0155, 0x0154, 0x0157, 0x0156, 0x0159, 0x0158, 
  0x015b, 0x015a, 0x015d, 0x015c, 0x015f, 0x015e, 0x0161, 0x0160, 0x0163, 
  0x0162, 0x0165, 0x0164, 0x0167, 0x0166, 0x0169, 0x0168, 0x016b, 0x016a, 
  0x016d, 0x016c, 0x016f, 0x016e, 0x0171, 0x0170, 0x0173, 0x0172, 0x0175, 
  0x0174, 0x0177, 0x0176, 0x00ff, 0x017a, 0x0179, 0x017c, 0x017b, 0x017e, 
  0x017d, 0x0053, 0x0000, 0x0253, 0x0183, 0x0182, 0x0185, 0x0184, 0x0254, 
  0x0188, 0x0187, 0x0256, 0x0257, 0x018c, 0x018b, 0x0000, 0x01dd, 0x0259, 
  0x025b, 0x0192, 0x0191, 0x0260, 0x0263, 0x0000, 0x0269, 0x0268, 0x0199, 
  0x0198, 0x0000, 0x0000, 0x026f, 0x0272, 0x0000, 0x0275, 0x01a1, 0x01a0, 
  0x01a3, 0x01a2, 0x01a5, 0x01a4, 0x0280, 0x01a8, 0x01a7, 0x0283, 0x0000, 
  0x0000, 0x01ad, 0x01ac, 0x0288, 0x01b0, 0x01af, 0x028a, 0x028b, 0x01b4, 
  0x01b3, 0x01b6, 0x01b5, 0x0292, 0x01b9, 0x01b8, 0x0000, 0x0000, 0x01bd, 
  0x01bc, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x01c6, 0x0000, 
  0x01c4, 0x01c9, 0x0000, 0x01c7, 0x01cc, 0x0000, 0x01ca, 0x01ce, 0x01cd, 
  0x01d0, 0x01cf, 0x01d2, 0x01d1, 0x01d4, 0x01d3, 0x01d6, 0x01d5, 0x01d8, 
  0x01d7, 0x01da, 0x01d9, 0x01dc, 0x01db, 0x018e, 0x01df, 0x01de, 0x01e1, 
  0x01e0, 0x01e3, 0x01e2, 0x01e5, 0x01e4, 0x01e7, 0x01e6, 0x01e9, 0x01e8, 
  0x01eb, 0x01ea, 0x01ed, 0x01ec, 0x01ef, 0x01ee, 0x0000, 0x01f3, 0x0000, 
  0x01f1, 0x01f5, 0x01f4, 0x0000, 0x0000, 0x0000, 0x0000, 0x01fb, 0x01fa, 
  0x01fd, 0x01fc, 0x01ff, 0x01fe
};

static unsigned short attrpage2[256] = {
  0x0201, 0x0200, 0x0203, 0x0202, 0x0205, 0x0204, 0x0207, 0x0206, 0x0209, 
  0x0208, 0x020b, 0x020a, 0x020d, 0x020c, 0x020f, 0x020e, 0x0211, 0x0210, 
  0x0213, 0x0212, 0x0215, 0x0214, 0x0217, 0x0216, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0181, 0x0186, 0x0000, 0x0189, 0x018a, 0x0000, 0x018f, 
  0x0000, 0x0190, 0x0000, 0x0000, 0x0000, 0x0000, 0x0193, 0x0000, 0x0000, 
  0x0194, 0x0000, 0x0000, 0x0000, 0x0000, 0x0197, 0x0196, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x019c, 0x0000, 0x0000, 0x019d, 0x0000, 0x0000, 
  0x019f, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x01a6, 0x0000, 0x0000, 0x01a9, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x01ae, 0x0000, 0x01b1, 0x01b2, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x01b7, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000
};

static unsigned short attrpage3[256] = {
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x03ac, 
  0x0000, 0x03ad, 0x03ae, 0x03af, 0x0000, 0x03cc, 0x0000, 0x03cd, 0x03ce, 
  0x0000, 0x03b1, 0x03b2, 0x03b3, 0x03b4, 0x03b5, 0x03b6, 0x03b7, 0x03b8, 
  0x03b9, 0x03ba, 0x03bb, 0x03bc, 0x03bd, 0x03be, 0x03bf, 0x03c0, 0x03c1, 
  0x0000, 0x03c3, 0x03c4, 0x03c5, 0x03c6, 0x03c7, 0x03c8, 0x03c9, 0x03ca, 
  0x03cb, 0x0386, 0x0388, 0x0389, 0x038a, 0x0000, 0x0391, 0x0392, 0x0393, 
  0x0394, 0x0395, 0x0396, 0x0397, 0x0398, 0x0399, 0x039a, 0x039b, 0x039c, 
  0x039d, 0x039e, 0x039f, 0x03a0, 0x03a1, 0x03a3, 0x03a3, 0x03a4, 0x03a5, 
  0x03a6, 0x03a7, 0x03a8, 0x03a9, 0x03aa, 0x03ab, 0x038c, 0x038e, 0x038f, 
  0x0000, 0x0392, 0x0398, 0x0000, 0x0000, 0x0000, 0x03a6, 0x03a0, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x03e3, 0x03e2, 0x03e5, 0x03e4, 0x03e7, 0x03e6, 0x03e9, 0x03e8, 
  0x03eb, 0x03ea, 0x03ed, 0x03ec, 0x03ef, 0x03ee, 0x039a, 0x03a1, 0x03a3, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000
};

static unsigned short attrpage4[256] = {
  0x0000, 0x0451, 0x0452, 0x0453, 0x0454, 0x0455, 0x0456, 0x0457, 0x0458, 
  0x0459, 0x045a, 0x045b, 0x045c, 0x0000, 0x045e, 0x045f, 0x0430, 0x0431, 
  0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437, 0x0438, 0x0439, 0x043a, 
  0x043b, 0x043c, 0x043d, 0x043e, 0x043f, 0x0440, 0x0441, 0x0442, 0x0443, 
  0x0444, 0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044a, 0x044b, 0x044c, 
  0x044d, 0x044e, 0x044f, 0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 
  0x0416, 0x0417, 0x0418, 0x0419, 0x041a, 0x041b, 0x041c, 0x041d, 0x041e, 
  0x041f, 0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 
  0x0428, 0x0429, 0x042a, 0x042b, 0x042c, 0x042d, 0x042e, 0x042f, 0x0000, 
  0x0401, 0x0402, 0x0403, 0x0404, 0x0405, 0x0406, 0x0407, 0x0408, 0x0409, 
  0x040a, 0x040b, 0x040c, 0x0000, 0x040e, 0x040f, 0x0461, 0x0460, 0x0463, 
  0x0462, 0x0465, 0x0464, 0x0467, 0x0466, 0x0469, 0x0468, 0x046b, 0x046a, 
  0x046d, 0x046c, 0x046f, 0x046e, 0x0471, 0x0470, 0x0473, 0x0472, 0x0475, 
  0x0474, 0x0477, 0x0476, 0x0479, 0x0478, 0x047b, 0x047a, 0x047d, 0x047c, 
  0x047f, 0x047e, 0x0481, 0x0480, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0491, 0x0490, 0x0493, 0x0492, 0x0495, 0x0494, 0x0497, 0x0496, 0x0499, 
  0x0498, 0x049b, 0x049a, 0x049d, 0x049c, 0x049f, 0x049e, 0x04a1, 0x04a0, 
  0x04a3, 0x04a2, 0x04a5, 0x04a4, 0x04a7, 0x04a6, 0x04a9, 0x04a8, 0x04ab, 
  0x04aa, 0x04ad, 0x04ac, 0x04af, 0x04ae, 0x04b1, 0x04b0, 0x04b3, 0x04b2, 
  0x04b5, 0x04b4, 0x04b7, 0x04b6, 0x04b9, 0x04b8, 0x04bb, 0x04ba, 0x04bd, 
  0x04bc, 0x04bf, 0x04be, 0x0000, 0x04c2, 0x04c1, 0x04c4, 0x04c3, 0x0000, 
  0x0000, 0x04c8, 0x04c7, 0x0000, 0x0000, 0x04cc, 0x04cb, 0x0000, 0x0000, 
  0x0000, 0x04d1, 0x04d0, 0x04d3, 0x04d2, 0x04d5, 0x04d4, 0x04d7, 0x04d6, 
  0x04d9, 0x04d8, 0x04db, 0x04da, 0x04dd, 0x04dc, 0x04df, 0x04de, 0x04e1, 
  0x04e0, 0x04e3, 0x04e2, 0x04e5, 0x04e4, 0x04e7, 0x04e6, 0x04e9, 0x04e8, 
  0x04eb, 0x04ea, 0x0000, 0x0000, 0x04ef, 0x04ee, 0x04f1, 0x04f0, 0x04f3, 
  0x04f2, 0x04f5, 0x04f4, 0x0000, 0x0000, 0x04f9, 0x04f8, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000
};

static unsigned short attrpage5[256] = {
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0561, 0x0562, 0x0563, 0x0564, 0x0565, 
  0x0566, 0x0567, 0x0568, 0x0569, 0x056a, 0x056b, 0x056c, 0x056d, 0x056e, 
  0x056f, 0x0570, 0x0571, 0x0572, 0x0573, 0x0574, 0x0575, 0x0576, 0x0577, 
  0x0578, 0x0579, 0x057a, 0x057b, 0x057c, 0x057d, 0x057e, 0x057f, 0x0580, 
  0x0581, 0x0582, 0x0583, 0x0584, 0x0585, 0x0586, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0531, 0x0532, 
  0x0533, 0x0534, 0x0535, 0x0536, 0x0537, 0x0538, 0x0539, 0x053a, 0x053b, 
  0x053c, 0x053d, 0x053e, 0x053f, 0x0540, 0x0541, 0x0542, 0x0543, 0x0544, 
  0x0545, 0x0546, 0x0547, 0x0548, 0x0549, 0x054a, 0x054b, 0x054c, 0x054d, 
  0x054e, 0x054f, 0x0550, 0x0551, 0x0552, 0x0553, 0x0554, 0x0555, 0x0556, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000
};

static unsigned short attrpage6[256] = {
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0x0002, 
  0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0x0002, 
  0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000
};

static unsigned short attrpage9[256] = {
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 
  0x0006, 0x0007, 0x0008, 0x0009, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0x0002, 0x0003, 
  0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000
};

static unsigned short attrpage10[256] = {
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 
  0x0006, 0x0007, 0x0008, 0x0009, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0x0002, 0x0003, 
  0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000
};

static unsigned short attrpage11[256] = {
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 
  0x0006, 0x0007, 0x0008, 0x0009, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0x0002, 0x0003, 
  0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000
};

static unsigned short attrpage12[256] = {
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 
  0x0006, 0x0007, 0x0008, 0x0009, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0x0002, 0x0003, 
  0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000
};

static unsigned short attrpage13[256] = {
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 
  0x0006, 0x0007, 0x0008, 0x0009, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000
};

static unsigned short attrpage14[256] = {
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 
  0x0008, 0x0009, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000
};

static unsigned short attrpage15[256] = {
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0x0002, 0x0003, 
  0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000
};

static unsigned short attrpage16[256] = {
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x10d0, 0x10d1, 
  0x10d2, 0x10d3, 0x10d4, 0x10d5, 0x10d6, 0x10d7, 0x10d8, 0x10d9, 0x10da, 
  0x10db, 0x10dc, 0x10dd, 0x10de, 0x10df, 0x10e0, 0x10e1, 0x10e2, 0x10e3, 
  0x10e4, 0x10e5, 0x10e6, 0x10e7, 0x10e8, 0x10e9, 0x10ea, 0x10eb, 0x10ec, 
  0x10ed, 0x10ee, 0x10ef, 0x10f0, 0x10f1, 0x10f2, 0x10f3, 0x10f4, 0x10f5, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000
};

static unsigned short attrpage30[256] = {
  0x1e01, 0x1e00, 0x1e03, 0x1e02, 0x1e05, 0x1e04, 0x1e07, 0x1e06, 0x1e09, 
  0x1e08, 0x1e0b, 0x1e0a, 0x1e0d, 0x1e0c, 0x1e0f, 0x1e0e, 0x1e11, 0x1e10, 
  0x1e13, 0x1e12, 0x1e15, 0x1e14, 0x1e17, 0x1e16, 0x1e19, 0x1e18, 0x1e1b, 
  0x1e1a, 0x1e1d, 0x1e1c, 0x1e1f, 0x1e1e, 0x1e21, 0x1e20, 0x1e23, 0x1e22, 
  0x1e25, 0x1e24, 0x1e27, 0x1e26, 0x1e29, 0x1e28, 0x1e2b, 0x1e2a, 0x1e2d, 
  0x1e2c, 0x1e2f, 0x1e2e, 0x1e31, 0x1e30, 0x1e33, 0x1e32, 0x1e35, 0x1e34, 
  0x1e37, 0x1e36, 0x1e39, 0x1e38, 0x1e3b, 0x1e3a, 0x1e3d, 0x1e3c, 0x1e3f, 
  0x1e3e, 0x1e41, 0x1e40, 0x1e43, 0x1e42, 0x1e45, 0x1e44, 0x1e47, 0x1e46, 
  0x1e49, 0x1e48, 0x1e4b, 0x1e4a, 0x1e4d, 0x1e4c, 0x1e4f, 0x1e4e, 0x1e51, 
  0x1e50, 0x1e53, 0x1e52, 0x1e55, 0x1e54, 0x1e57, 0x1e56, 0x1e59, 0x1e58, 
  0x1e5b, 0x1e5a, 0x1e5d, 0x1e5c, 0x1e5f, 0x1e5e, 0x1e61, 0x1e60, 0x1e63, 
  0x1e62, 0x1e65, 0x1e64, 0x1e67, 0x1e66, 0x1e69, 0x1e68, 0x1e6b, 0x1e6a, 
  0x1e6d, 0x1e6c, 0x1e6f, 0x1e6e, 0x1e71, 0x1e70, 0x1e73, 0x1e72, 0x1e75, 
  0x1e74, 0x1e77, 0x1e76, 0x1e79, 0x1e78, 0x1e7b, 0x1e7a, 0x1e7d, 0x1e7c, 
  0x1e7f, 0x1e7e, 0x1e81, 0x1e80, 0x1e83, 0x1e82, 0x1e85, 0x1e84, 0x1e87, 
  0x1e86, 0x1e89, 0x1e88, 0x1e8b, 0x1e8a, 0x1e8d, 0x1e8c, 0x1e8f, 0x1e8e, 
  0x1e91, 0x1e90, 0x1e93, 0x1e92, 0x1e95, 0x1e94, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x1e60, 0x0000, 0x0000, 0x0000, 0x0000, 0x1ea1, 0x1ea0, 
  0x1ea3, 0x1ea2, 0x1ea5, 0x1ea4, 0x1ea7, 0x1ea6, 0x1ea9, 0x1ea8, 0x1eab, 
  0x1eaa, 0x1ead, 0x1eac, 0x1eaf, 0x1eae, 0x1eb1, 0x1eb0, 0x1eb3, 0x1eb2, 
  0x1eb5, 0x1eb4, 0x1eb7, 0x1eb6, 0x1eb9, 0x1eb8, 0x1ebb, 0x1eba, 0x1ebd, 
  0x1ebc, 0x1ebf, 0x1ebe, 0x1ec1, 0x1ec0, 0x1ec3, 0x1ec2, 0x1ec5, 0x1ec4, 
  0x1ec7, 0x1ec6, 0x1ec9, 0x1ec8, 0x1ecb, 0x1eca, 0x1ecd, 0x1ecc, 0x1ecf, 
  0x1ece, 0x1ed1, 0x1ed0, 0x1ed3, 0x1ed2, 0x1ed5, 0x1ed4, 0x1ed7, 0x1ed6, 
  0x1ed9, 0x1ed8, 0x1edb, 0x1eda, 0x1edd, 0x1edc, 0x1edf, 0x1ede, 0x1ee1, 
  0x1ee0, 0x1ee3, 0x1ee2, 0x1ee5, 0x1ee4, 0x1ee7, 0x1ee6, 0x1ee9, 0x1ee8, 
  0x1eeb, 0x1eea, 0x1eed, 0x1eec, 0x1eef, 0x1eee, 0x1ef1, 0x1ef0, 0x1ef3, 
  0x1ef2, 0x1ef5, 0x1ef4, 0x1ef7, 0x1ef6, 0x1ef9, 0x1ef8, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000
};

static unsigned short attrpage31[256] = {
  0x1f08, 0x1f09, 0x1f0a, 0x1f0b, 0x1f0c, 0x1f0d, 0x1f0e, 0x1f0f, 0x1f00, 
  0x1f01, 0x1f02, 0x1f03, 0x1f04, 0x1f05, 0x1f06, 0x1f07, 0x1f18, 0x1f19, 
  0x1f1a, 0x1f1b, 0x1f1c, 0x1f1d, 0x0000, 0x0000, 0x1f10, 0x1f11, 0x1f12, 
  0x1f13, 0x1f14, 0x1f15, 0x0000, 0x0000, 0x1f28, 0x1f29, 0x1f2a, 0x1f2b, 
  0x1f2c, 0x1f2d, 0x1f2e, 0x1f2f, 0x1f20, 0x1f21, 0x1f22, 0x1f23, 0x1f24, 
  0x1f25, 0x1f26, 0x1f27, 0x1f38, 0x1f39, 0x1f3a, 0x1f3b, 0x1f3c, 0x1f3d, 
  0x1f3e, 0x1f3f, 0x1f30, 0x1f31, 0x1f32, 0x1f33, 0x1f34, 0x1f35, 0x1f36, 
  0x1f37, 0x1f48, 0x1f49, 0x1f4a, 0x1f4b, 0x1f4c, 0x1f4d, 0x0000, 0x0000, 
  0x1f40, 0x1f41, 0x1f42, 0x1f43, 0x1f44, 0x1f45, 0x0000, 0x0000, 0x0000, 
  0x1f59, 0x0000, 0x1f5b, 0x0000, 0x1f5d, 0x0000, 0x1f5f, 0x0000, 0x1f51, 
  0x0000, 0x1f53, 0x0000, 0x1f55, 0x0000, 0x1f57, 0x1f68, 0x1f69, 0x1f6a, 
  0x1f6b, 0x1f6c, 0x1f6d, 0x1f6e, 0x1f6f, 0x1f60, 0x1f61, 0x1f62, 0x1f63, 
  0x1f64, 0x1f65, 0x1f66, 0x1f67, 0x1fba, 0x1fbb, 0x1fc8, 0x1fc9, 0x1fca, 
  0x1fcb, 0x1fda, 0x1fdb, 0x1ff8, 0x1ff9, 0x1fea, 0x1feb, 0x1ffa, 0x1ffb, 
  0x0000, 0x0000, 0x1f88, 0x1f89, 0x1f8a, 0x1f8b, 0x1f8c, 0x1f8d, 0x1f8e, 
  0x1f8f, 0x1f80, 0x1f81, 0x1f82, 0x1f83, 0x1f84, 0x1f85, 0x1f86, 0x1f87, 
  0x1f98, 0x1f99, 0x1f9a, 0x1f9b, 0x1f9c, 0x1f9d, 0x1f9e, 0x1f9f, 0x1f90, 
  0x1f91, 0x1f92, 0x1f93, 0x1f94, 0x1f95, 0x1f96, 0x1f97, 0x1fa8, 0x1fa9, 
  0x1faa, 0x1fab, 0x1fac, 0x1fad, 0x1fae, 0x1faf, 0x1fa0, 0x1fa1, 0x1fa2, 
  0x1fa3, 0x1fa4, 0x1fa5, 0x1fa6, 0x1fa7, 0x1fb8, 0x1fb9, 0x0000, 0x1fbc, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x1fb0, 0x1fb1, 0x1f70, 0x1f71, 0x1fb3, 
  0x0000, 0x0399, 0x0000, 0x0000, 0x0000, 0x0000, 0x1fcc, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x1f72, 0x1f73, 0x1f74, 0x1f75, 0x1fc3, 0x0000, 0x0000, 
  0x0000, 0x1fd8, 0x1fd9, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x1fd0, 0x1fd1, 0x1f76, 0x1f77, 0x0000, 0x0000, 0x0000, 0x0000, 0x1fe8, 
  0x1fe9, 0x0000, 0x0000, 0x0000, 0x1fec, 0x0000, 0x0000, 0x1fe0, 0x1fe1, 
  0x1f7a, 0x1f7b, 0x1fe5, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x1ffc, 0x0000, 0x0000, 0x0000, 0x0000, 0x1f78, 0x1f79, 0x1f7c, 0x1f7d, 
  0x1ff3, 0x0000, 0x0000, 0x0000
};

static unsigned short attrpage255[256] = {
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 
  0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xff41, 0xff42, 0xff43, 
  0xff44, 0xff45, 0xff46, 0xff47, 0xff48, 0xff49, 0xff4a, 0xff4b, 0xff4c, 
  0xff4d, 0xff4e, 0xff4f, 0xff50, 0xff51, 0xff52, 0xff53, 0xff54, 0xff55, 
  0xff56, 0xff57, 0xff58, 0xff59, 0xff5a, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0xff21, 0xff22, 0xff23, 0xff24, 0xff25, 0xff26, 0xff27, 
  0xff28, 0xff29, 0xff2a, 0xff2b, 0xff2c, 0xff2d, 0xff2e, 0xff2f, 0xff30, 
  0xff31, 0xff32, 0xff33, 0xff34, 0xff35, 0xff36, 0xff37, 0xff38, 0xff39, 
  0xff3a, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000
};

static unsigned short *attr_table[256] = {
  attrpage0,
  attrpage1,
  attrpage2,
  attrpage3,
  attrpage4,
  attrpage5,
  attrpage6,
  0x0000,
  0x0000,
  attrpage9,
  attrpage10,
  attrpage11,
  attrpage12,
  attrpage13,
  attrpage14,
  attrpage15,
  attrpage16,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  attrpage30,
  attrpage31,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  attrpage255
};

static unsigned short title_table[][3] = {
  { 0x01c5, 0x01c4, 0x01c6 },
  { 0x01c8, 0x01c7, 0x01c9 },
  { 0x01cb, 0x01ca, 0x01cc },
  { 0x01f2, 0x01f1, 0x01f3 }
};

#endif /* CHARTABLES_H */

/* This file is automatically generated.  DO NOT EDIT! */

#ifndef DECOMP_H
#define DECOMP_H

#define UNICODE_LAST_CHAR 0xffff

static unsigned char cclass3[256] = {
  230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 
  230, 230, 230, 230, 230, 230, 232, 220, 220, 220, 220, 232, 216, 220, 220, 
  220, 220, 220, 202, 202, 220, 220, 220, 220, 202, 202, 220, 220, 220, 220, 
  220, 220, 220, 220, 220, 220, 220, 1, 1, 1, 1, 1, 220, 220, 220, 220, 230, 
  230, 230, 230, 230, 230, 230, 230, 240, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 234, 234, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static unsigned char cclass4[256] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 230, 230, 230, 230, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0
};

static unsigned char cclass5[256] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 220, 230, 230, 
  230, 230, 220, 230, 230, 230, 222, 220, 230, 230, 230, 230, 230, 230, 0, 
  220, 220, 220, 220, 220, 230, 230, 220, 230, 230, 222, 228, 230, 10, 11, 
  12, 13, 14, 15, 16, 17, 18, 19, 0, 20, 21, 22, 0, 23, 0, 24, 25, 0, 230, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0
};

static unsigned char cclass6[256] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  27, 28, 29, 30, 31, 32, 33, 34, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 35, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 230, 230, 230, 230, 230, 
  230, 230, 0, 0, 230, 230, 230, 230, 220, 230, 0, 0, 230, 230, 0, 220, 230, 
  230, 220, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static unsigned char cclass9[256] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 9, 0, 0, 0, 230, 220, 230, 230, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0
};

static unsigned char cclass10[256] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0
};

static unsigned char cclass11[256] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0
};

static unsigned char cclass12[256] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 84, 0, 0, 0, 0, 
  0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 84, 91, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0
};

static unsigned char cclass13[256] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0
};

static unsigned char cclass14[256] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 103, 103, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 107, 
  107, 107, 107, 0, 107, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 118, 118, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 122, 122, 122, 122, 0, 122, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static unsigned char cclass15[256] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  220, 220, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 220, 0, 220, 0, 216, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 129, 130, 0, 132, 0, 0, 
  0, 0, 0, 130, 130, 130, 130, 0, 0, 130, 0, 230, 230, 9, 0, 230, 230, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static unsigned char cclass32[256] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 230, 230, 1, 1, 230, 230, 230, 230, 1, 1, 1, 230, 
  230, 0, 0, 0, 0, 230, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static unsigned char cclass48[256] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 218, 228, 232, 222, 224, 
  224, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static unsigned char cclass251[256] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 26, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0
};

static unsigned char cclass254[256] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 230, 230, 230, 230, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0
};

static unsigned char *combining_class_table[256] = {
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  cclass3,
  cclass4,
  cclass5,
  cclass6,
  (unsigned char *) 0,
  (unsigned char *) 0,
  cclass9,
  cclass10,
  cclass11,
  cclass12,
  cclass13,
  cclass14,
  cclass15,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  cclass32,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  cclass48,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  (unsigned char *) 0,
  cclass251,
  (unsigned char *) 0,
  (unsigned char *) 0,
  cclass254,
  (unsigned char *) 0
};

typedef struct
{
  unsigned short ch;
  unsigned char *expansion;
} decomposition;

static decomposition decomp_table[] =
{
  { 0x00c0, "\x00\x41\x03\x00\0" },
  { 0x00c1, "\x00\x41\x03\x01\0" },
  { 0x00c2, "\x00\x41\x03\x02\0" },
  { 0x00c3, "\x00\x41\x03\x03\0" },
  { 0x00c4, "\x00\x41\x03\x08\0" },
  { 0x00c5, "\x00\x41\x03\x0a\0" },
  { 0x00c7, "\x00\x43\x03\x27\0" },
  { 0x00c8, "\x00\x45\x03\x00\0" },
  { 0x00c9, "\x00\x45\x03\x01\0" },
  { 0x00ca, "\x00\x45\x03\x02\0" },
  { 0x00cb, "\x00\x45\x03\x08\0" },
  { 0x00cc, "\x00\x49\x03\x00\0" },
  { 0x00cd, "\x00\x49\x03\x01\0" },
  { 0x00ce, "\x00\x49\x03\x02\0" },
  { 0x00cf, "\x00\x49\x03\x08\0" },
  { 0x00d1, "\x00\x4e\x03\x03\0" },
  { 0x00d2, "\x00\x4f\x03\x00\0" },
  { 0x00d3, "\x00\x4f\x03\x01\0" },
  { 0x00d4, "\x00\x4f\x03\x02\0" },
  { 0x00d5, "\x00\x4f\x03\x03\0" },
  { 0x00d6, "\x00\x4f\x03\x08\0" },
  { 0x00d9, "\x00\x55\x03\x00\0" },
  { 0x00da, "\x00\x55\x03\x01\0" },
  { 0x00db, "\x00\x55\x03\x02\0" },
  { 0x00dc, "\x00\x55\x03\x08\0" },
  { 0x00dd, "\x00\x59\x03\x01\0" },
  { 0x00e0, "\x00\x61\x03\x00\0" },
  { 0x00e1, "\x00\x61\x03\x01\0" },
  { 0x00e2, "\x00\x61\x03\x02\0" },
  { 0x00e3, "\x00\x61\x03\x03\0" },
  { 0x00e4, "\x00\x61\x03\x08\0" },
  { 0x00e5, "\x00\x61\x03\x0a\0" },
  { 0x00e7, "\x00\x63\x03\x27\0" },
  { 0x00e8, "\x00\x65\x03\x00\0" },
  { 0x00e9, "\x00\x65\x03\x01\0" },
  { 0x00ea, "\x00\x65\x03\x02\0" },
  { 0x00eb, "\x00\x65\x03\x08\0" },
  { 0x00ec, "\x00\x69\x03\x00\0" },
  { 0x00ed, "\x00\x69\x03\x01\0" },
  { 0x00ee, "\x00\x69\x03\x02\0" },
  { 0x00ef, "\x00\x69\x03\x08\0" },
  { 0x00f1, "\x00\x6e\x03\x03\0" },
  { 0x00f2, "\x00\x6f\x03\x00\0" },
  { 0x00f3, "\x00\x6f\x03\x01\0" },
  { 0x00f4, "\x00\x6f\x03\x02\0" },
  { 0x00f5, "\x00\x6f\x03\x03\0" },
  { 0x00f6, "\x00\x6f\x03\x08\0" },
  { 0x00f9, "\x00\x75\x03\x00\0" },
  { 0x00fa, "\x00\x75\x03\x01\0" },
  { 0x00fb, "\x00\x75\x03\x02\0" },
  { 0x00fc, "\x00\x75\x03\x08\0" },
  { 0x00fd, "\x00\x79\x03\x01\0" },
  { 0x00ff, "\x00\x79\x03\x08\0" },
  { 0x0100, "\x00\x41\x03\x04\0" },
  { 0x0101, "\x00\x61\x03\x04\0" },
  { 0x0102, "\x00\x41\x03\x06\0" },
  { 0x0103, "\x00\x61\x03\x06\0" },
  { 0x0104, "\x00\x41\x03\x28\0" },
  { 0x0105, "\x00\x61\x03\x28\0" },
  { 0x0106, "\x00\x43\x03\x01\0" },
  { 0x0107, "\x00\x63\x03\x01\0" },
  { 0x0108, "\x00\x43\x03\x02\0" },
  { 0x0109, "\x00\x63\x03\x02\0" },
  { 0x010a, "\x00\x43\x03\x07\0" },
  { 0x010b, "\x00\x63\x03\x07\0" },
  { 0x010c, "\x00\x43\x03\x0c\0" },
  { 0x010d, "\x00\x63\x03\x0c\0" },
  { 0x010e, "\x00\x44\x03\x0c\0" },
  { 0x010f, "\x00\x64\x03\x0c\0" },
  { 0x0112, "\x00\x45\x03\x04\0" },
  { 0x0113, "\x00\x65\x03\x04\0" },
  { 0x0114, "\x00\x45\x03\x06\0" },
  { 0x0115, "\x00\x65\x03\x06\0" },
  { 0x0116, "\x00\x45\x03\x07\0" },
  { 0x0117, "\x00\x65\x03\x07\0" },
  { 0x0118, "\x00\x45\x03\x28\0" },
  { 0x0119, "\x00\x65\x03\x28\0" },
  { 0x011a, "\x00\x45\x03\x0c\0" },
  { 0x011b, "\x00\x65\x03\x0c\0" },
  { 0x011c, "\x00\x47\x03\x02\0" },
  { 0x011d, "\x00\x67\x03\x02\0" },
  { 0x011e, "\x00\x47\x03\x06\0" },
  { 0x011f, "\x00\x67\x03\x06\0" },
  { 0x0120, "\x00\x47\x03\x07\0" },
  { 0x0121, "\x00\x67\x03\x07\0" },
  { 0x0122, "\x00\x47\x03\x27\0" },
  { 0x0123, "\x00\x67\x03\x27\0" },
  { 0x0124, "\x00\x48\x03\x02\0" },
  { 0x0125, "\x00\x68\x03\x02\0" },
  { 0x0128, "\x00\x49\x03\x03\0" },
  { 0x0129, "\x00\x69\x03\x03\0" },
  { 0x012a, "\x00\x49\x03\x04\0" },
  { 0x012b, "\x00\x69\x03\x04\0" },
  { 0x012c, "\x00\x49\x03\x06\0" },
  { 0x012d, "\x00\x69\x03\x06\0" },
  { 0x012e, "\x00\x49\x03\x28\0" },
  { 0x012f, "\x00\x69\x03\x28\0" },
  { 0x0130, "\x00\x49\x03\x07\0" },
  { 0x0134, "\x00\x4a\x03\x02\0" },
  { 0x0135, "\x00\x6a\x03\x02\0" },
  { 0x0136, "\x00\x4b\x03\x27\0" },
  { 0x0137, "\x00\x6b\x03\x27\0" },
  { 0x0139, "\x00\x4c\x03\x01\0" },
  { 0x013a, "\x00\x6c\x03\x01\0" },
  { 0x013b, "\x00\x4c\x03\x27\0" },
  { 0x013c, "\x00\x6c\x03\x27\0" },
  { 0x013d, "\x00\x4c\x03\x0c\0" },
  { 0x013e, "\x00\x6c\x03\x0c\0" },
  { 0x0143, "\x00\x4e\x03\x01\0" },
  { 0x0144, "\x00\x6e\x03\x01\0" },
  { 0x0145, "\x00\x4e\x03\x27\0" },
  { 0x0146, "\x00\x6e\x03\x27\0" },
  { 0x0147, "\x00\x4e\x03\x0c\0" },
  { 0x0148, "\x00\x6e\x03\x0c\0" },
  { 0x014c, "\x00\x4f\x03\x04\0" },
  { 0x014d, "\x00\x6f\x03\x04\0" },
  { 0x014e, "\x00\x4f\x03\x06\0" },
  { 0x014f, "\x00\x6f\x03\x06\0" },
  { 0x0150, "\x00\x4f\x03\x0b\0" },
  { 0x0151, "\x00\x6f\x03\x0b\0" },
  { 0x0154, "\x00\x52\x03\x01\0" },
  { 0x0155, "\x00\x72\x03\x01\0" },
  { 0x0156, "\x00\x52\x03\x27\0" },
  { 0x0157, "\x00\x72\x03\x27\0" },
  { 0x0158, "\x00\x52\x03\x0c\0" },
  { 0x0159, "\x00\x72\x03\x0c\0" },
  { 0x015a, "\x00\x53\x03\x01\0" },
  { 0x015b, "\x00\x73\x03\x01\0" },
  { 0x015c, "\x00\x53\x03\x02\0" },
  { 0x015d, "\x00\x73\x03\x02\0" },
  { 0x015e, "\x00\x53\x03\x27\0" },
  { 0x015f, "\x00\x73\x03\x27\0" },
  { 0x0160, "\x00\x53\x03\x0c\0" },
  { 0x0161, "\x00\x73\x03\x0c\0" },
  { 0x0162, "\x00\x54\x03\x27\0" },
  { 0x0163, "\x00\x74\x03\x27\0" },
  { 0x0164, "\x00\x54\x03\x0c\0" },
  { 0x0165, "\x00\x74\x03\x0c\0" },
  { 0x0168, "\x00\x55\x03\x03\0" },
  { 0x0169, "\x00\x75\x03\x03\0" },
  { 0x016a, "\x00\x55\x03\x04\0" },
  { 0x016b, "\x00\x75\x03\x04\0" },
  { 0x016c, "\x00\x55\x03\x06\0" },
  { 0x016d, "\x00\x75\x03\x06\0" },
  { 0x016e, "\x00\x55\x03\x0a\0" },
  { 0x016f, "\x00\x75\x03\x0a\0" },
  { 0x0170, "\x00\x55\x03\x0b\0" },
  { 0x0171, "\x00\x75\x03\x0b\0" },
  { 0x0172, "\x00\x55\x03\x28\0" },
  { 0x0173, "\x00\x75\x03\x28\0" },
  { 0x0174, "\x00\x57\x03\x02\0" },
  { 0x0175, "\x00\x77\x03\x02\0" },
  { 0x0176, "\x00\x59\x03\x02\0" },
  { 0x0177, "\x00\x79\x03\x02\0" },
  { 0x0178, "\x00\x59\x03\x08\0" },
  { 0x0179, "\x00\x5a\x03\x01\0" },
  { 0x017a, "\x00\x7a\x03\x01\0" },
  { 0x017b, "\x00\x5a\x03\x07\0" },
  { 0x017c, "\x00\x7a\x03\x07\0" },
  { 0x017d, "\x00\x5a\x03\x0c\0" },
  { 0x017e, "\x00\x7a\x03\x0c\0" },
  { 0x01a0, "\x00\x4f\x03\x1b\0" },
  { 0x01a1, "\x00\x6f\x03\x1b\0" },
  { 0x01af, "\x00\x55\x03\x1b\0" },
  { 0x01b0, "\x00\x75\x03\x1b\0" },
  { 0x01cd, "\x00\x41\x03\x0c\0" },
  { 0x01ce, "\x00\x61\x03\x0c\0" },
  { 0x01cf, "\x00\x49\x03\x0c\0" },
  { 0x01d0, "\x00\x69\x03\x0c\0" },
  { 0x01d1, "\x00\x4f\x03\x0c\0" },
  { 0x01d2, "\x00\x6f\x03\x0c\0" },
  { 0x01d3, "\x00\x55\x03\x0c\0" },
  { 0x01d4, "\x00\x75\x03\x0c\0" },
  { 0x01d5, "\x00\x55\x03\x08\x03\x04\0" },
  { 0x01d6, "\x00\x75\x03\x08\x03\x04\0" },
  { 0x01d7, "\x00\x55\x03\x08\x03\x01\0" },
  { 0x01d8, "\x00\x75\x03\x08\x03\x01\0" },
  { 0x01d9, "\x00\x55\x03\x08\x03\x0c\0" },
  { 0x01da, "\x00\x75\x03\x08\x03\x0c\0" },
  { 0x01db, "\x00\x55\x03\x08\x03\x00\0" },
  { 0x01dc, "\x00\x75\x03\x08\x03\x00\0" },
  { 0x01de, "\x00\x41\x03\x08\x03\x04\0" },
  { 0x01df, "\x00\x61\x03\x08\x03\x04\0" },
  { 0x01e0, "\x00\x41\x03\x07\x03\x04\0" },
  { 0x01e1, "\x00\x61\x03\x07\x03\x04\0" },
  { 0x01e2, "\x00\xc6\x03\x04\0" },
  { 0x01e3, "\x00\xe6\x03\x04\0" },
  { 0x01e6, "\x00\x47\x03\x0c\0" },
  { 0x01e7, "\x00\x67\x03\x0c\0" },
  { 0x01e8, "\x00\x4b\x03\x0c\0" },
  { 0x01e9, "\x00\x6b\x03\x0c\0" },
  { 0x01ea, "\x00\x4f\x03\x28\0" },
  { 0x01eb, "\x00\x6f\x03\x28\0" },
  { 0x01ec, "\x00\x4f\x03\x28\x03\x04\0" },
  { 0x01ed, "\x00\x6f\x03\x28\x03\x04\0" },
  { 0x01ee, "\x01\xb7\x03\x0c\0" },
  { 0x01ef, "\x02\x92\x03\x0c\0" },
  { 0x01f0, "\x00\x6a\x03\x0c\0" },
  { 0x01f4, "\x00\x47\x03\x01\0" },
  { 0x01f5, "\x00\x67\x03\x01\0" },
  { 0x01fa, "\x00\x41\x03\x0a\x03\x01\0" },
  { 0x01fb, "\x00\x61\x03\x0a\x03\x01\0" },
  { 0x01fc, "\x00\xc6\x03\x01\0" },
  { 0x01fd, "\x00\xe6\x03\x01\0" },
  { 0x01fe, "\x00\xd8\x03\x01\0" },
  { 0x01ff, "\x00\xf8\x03\x01\0" },
  { 0x0200, "\x00\x41\x03\x0f\0" },
  { 0x0201, "\x00\x61\x03\x0f\0" },
  { 0x0202, "\x00\x41\x03\x11\0" },
  { 0x0203, "\x00\x61\x03\x11\0" },
  { 0x0204, "\x00\x45\x03\x0f\0" },
  { 0x0205, "\x00\x65\x03\x0f\0" },
  { 0x0206, "\x00\x45\x03\x11\0" },
  { 0x0207, "\x00\x65\x03\x11\0" },
  { 0x0208, "\x00\x49\x03\x0f\0" },
  { 0x0209, "\x00\x69\x03\x0f\0" },
  { 0x020a, "\x00\x49\x03\x11\0" },
  { 0x020b, "\x00\x69\x03\x11\0" },
  { 0x020c, "\x00\x4f\x03\x0f\0" },
  { 0x020d, "\x00\x6f\x03\x0f\0" },
  { 0x020e, "\x00\x4f\x03\x11\0" },
  { 0x020f, "\x00\x6f\x03\x11\0" },
  { 0x0210, "\x00\x52\x03\x0f\0" },
  { 0x0211, "\x00\x72\x03\x0f\0" },
  { 0x0212, "\x00\x52\x03\x11\0" },
  { 0x0213, "\x00\x72\x03\x11\0" },
  { 0x0214, "\x00\x55\x03\x0f\0" },
  { 0x0215, "\x00\x75\x03\x0f\0" },
  { 0x0216, "\x00\x55\x03\x11\0" },
  { 0x0217, "\x00\x75\x03\x11\0" },
  { 0x0340, "\x03\x00\0" },
  { 0x0341, "\x03\x01\0" },
  { 0x0343, "\x03\x13\0" },
  { 0x0344, "\x03\x08\x03\x01\0" },
  { 0x0374, "\x02\xb9\0" },
  { 0x037e, "\x00\x3b\0" },
  { 0x0385, "\x00\xa8\x03\x01\0" },
  { 0x0386, "\x03\x91\x03\x01\0" },
  { 0x0387, "\x00\xb7\0" },
  { 0x0388, "\x03\x95\x03\x01\0" },
  { 0x0389, "\x03\x97\x03\x01\0" },
  { 0x038a, "\x03\x99\x03\x01\0" },
  { 0x038c, "\x03\x9f\x03\x01\0" },
  { 0x038e, "\x03\xa5\x03\x01\0" },
  { 0x038f, "\x03\xa9\x03\x01\0" },
  { 0x0390, "\x03\xb9\x03\x08\x03\x01\0" },
  { 0x03aa, "\x03\x99\x03\x08\0" },
  { 0x03ab, "\x03\xa5\x03\x08\0" },
  { 0x03ac, "\x03\xb1\x03\x01\0" },
  { 0x03ad, "\x03\xb5\x03\x01\0" },
  { 0x03ae, "\x03\xb7\x03\x01\0" },
  { 0x03af, "\x03\xb9\x03\x01\0" },
  { 0x03b0, "\x03\xc5\x03\x08\x03\x01\0" },
  { 0x03ca, "\x03\xb9\x03\x08\0" },
  { 0x03cb, "\x03\xc5\x03\x08\0" },
  { 0x03cc, "\x03\xbf\x03\x01\0" },
  { 0x03cd, "\x03\xc5\x03\x01\0" },
  { 0x03ce, "\x03\xc9\x03\x01\0" },
  { 0x03d3, "\x03\xd2\x03\x01\0" },
  { 0x03d4, "\x03\xd2\x03\x08\0" },
  { 0x0401, "\x04\x15\x03\x08\0" },
  { 0x0403, "\x04\x13\x03\x01\0" },
  { 0x0407, "\x04\x06\x03\x08\0" },
  { 0x040c, "\x04\x1a\x03\x01\0" },
  { 0x040e, "\x04\x23\x03\x06\0" },
  { 0x0419, "\x04\x18\x03\x06\0" },
  { 0x0439, "\x04\x38\x03\x06\0" },
  { 0x0451, "\x04\x35\x03\x08\0" },
  { 0x0453, "\x04\x33\x03\x01\0" },
  { 0x0457, "\x04\x56\x03\x08\0" },
  { 0x045c, "\x04\x3a\x03\x01\0" },
  { 0x045e, "\x04\x43\x03\x06\0" },
  { 0x0476, "\x04\x74\x03\x0f\0" },
  { 0x0477, "\x04\x75\x03\x0f\0" },
  { 0x04c1, "\x04\x16\x03\x06\0" },
  { 0x04c2, "\x04\x36\x03\x06\0" },
  { 0x04d0, "\x04\x10\x03\x06\0" },
  { 0x04d1, "\x04\x30\x03\x06\0" },
  { 0x04d2, "\x04\x10\x03\x08\0" },
  { 0x04d3, "\x04\x30\x03\x08\0" },
  { 0x04d6, "\x04\x15\x03\x06\0" },
  { 0x04d7, "\x04\x35\x03\x06\0" },
  { 0x04da, "\x04\xd8\x03\x08\0" },
  { 0x04db, "\x04\xd9\x03\x08\0" },
  { 0x04dc, "\x04\x16\x03\x08\0" },
  { 0x04dd, "\x04\x36\x03\x08\0" },
  { 0x04de, "\x04\x17\x03\x08\0" },
  { 0x04df, "\x04\x37\x03\x08\0" },
  { 0x04e2, "\x04\x18\x03\x04\0" },
  { 0x04e3, "\x04\x38\x03\x04\0" },
  { 0x04e4, "\x04\x18\x03\x08\0" },
  { 0x04e5, "\x04\x38\x03\x08\0" },
  { 0x04e6, "\x04\x1e\x03\x08\0" },
  { 0x04e7, "\x04\x3e\x03\x08\0" },
  { 0x04ea, "\x04\xe8\x03\x08\0" },
  { 0x04eb, "\x04\xe9\x03\x08\0" },
  { 0x04ee, "\x04\x23\x03\x04\0" },
  { 0x04ef, "\x04\x43\x03\x04\0" },
  { 0x04f0, "\x04\x23\x03\x08\0" },
  { 0x04f1, "\x04\x43\x03\x08\0" },
  { 0x04f2, "\x04\x23\x03\x0b\0" },
  { 0x04f3, "\x04\x43\x03\x0b\0" },
  { 0x04f4, "\x04\x27\x03\x08\0" },
  { 0x04f5, "\x04\x47\x03\x08\0" },
  { 0x04f8, "\x04\x2b\x03\x08\0" },
  { 0x04f9, "\x04\x4b\x03\x08\0" },
  { 0x0929, "\x09\x28\x09\x3c\0" },
  { 0x0931, "\x09\x30\x09\x3c\0" },
  { 0x0934, "\x09\x33\x09\x3c\0" },
  { 0x0958, "\x09\x15\x09\x3c\0" },
  { 0x0959, "\x09\x16\x09\x3c\0" },
  { 0x095a, "\x09\x17\x09\x3c\0" },
  { 0x095b, "\x09\x1c\x09\x3c\0" },
  { 0x095c, "\x09\x21\x09\x3c\0" },
  { 0x095d, "\x09\x22\x09\x3c\0" },
  { 0x095e, "\x09\x2b\x09\x3c\0" },
  { 0x095f, "\x09\x2f\x09\x3c\0" },
  { 0x09b0, "\x09\xac\x09\xbc\0" },
  { 0x09cb, "\x09\xc7\x09\xbe\0" },
  { 0x09cc, "\x09\xc7\x09\xd7\0" },
  { 0x09dc, "\x09\xa1\x09\xbc\0" },
  { 0x09dd, "\x09\xa2\x09\xbc\0" },
  { 0x09df, "\x09\xaf\x09\xbc\0" },
  { 0x0a59, "\x0a\x16\x0a\x3c\0" },
  { 0x0a5a, "\x0a\x17\x0a\x3c\0" },
  { 0x0a5b, "\x0a\x1c\x0a\x3c\0" },
  { 0x0a5c, "\x0a\x21\x0a\x3c\0" },
  { 0x0a5e, "\x0a\x2b\x0a\x3c\0" },
  { 0x0b48, "\x0b\x47\x0b\x56\0" },
  { 0x0b4b, "\x0b\x47\x0b\x3e\0" },
  { 0x0b4c, "\x0b\x47\x0b\x57\0" },
  { 0x0b5c, "\x0b\x21\x0b\x3c\0" },
  { 0x0b5d, "\x0b\x22\x0b\x3c\0" },
  { 0x0b5f, "\x0b\x2f\x0b\x3c\0" },
  { 0x0b94, "\x0b\x92\x0b\xd7\0" },
  { 0x0bca, "\x0b\xc6\x0b\xbe\0" },
  { 0x0bcb, "\x0b\xc7\x0b\xbe\0" },
  { 0x0bcc, "\x0b\xc6\x0b\xd7\0" },
  { 0x0c48, "\x0c\x46\x0c\x56\0" },
  { 0x0cc0, "\x0c\xbf\x0c\xd5\0" },
  { 0x0cc7, "\x0c\xc6\x0c\xd5\0" },
  { 0x0cc8, "\x0c\xc6\x0c\xd6\0" },
  { 0x0cca, "\x0c\xc6\x0c\xc2\0" },
  { 0x0ccb, "\x0c\xc6\x0c\xc2\x0c\xd5\0" },
  { 0x0d4a, "\x0d\x46\x0d\x3e\0" },
  { 0x0d4b, "\x0d\x47\x0d\x3e\0" },
  { 0x0d4c, "\x0d\x46\x0d\x57\0" },
  { 0x0e33, "\x0e\x4d\x0e\x32\0" },
  { 0x0eb3, "\x0e\xcd\x0e\xb2\0" },
  { 0x0f43, "\x0f\x42\x0f\xb7\0" },
  { 0x0f4d, "\x0f\x4c\x0f\xb7\0" },
  { 0x0f52, "\x0f\x51\x0f\xb7\0" },
  { 0x0f57, "\x0f\x56\x0f\xb7\0" },
  { 0x0f5c, "\x0f\x5b\x0f\xb7\0" },
  { 0x0f69, "\x0f\x40\x0f\xb5\0" },
  { 0x0f73, "\x0f\x71\x0f\x72\0" },
  { 0x0f75, "\x0f\x71\x0f\x74\0" },
  { 0x0f76, "\x0f\xb2\x0f\x80\0" },
  { 0x0f78, "\x0f\xb3\x0f\x80\0" },
  { 0x0f81, "\x0f\x71\x0f\x80\0" },
  { 0x0f93, "\x0f\x92\x0f\xb7\0" },
  { 0x0f9d, "\x0f\x9c\x0f\xb7\0" },
  { 0x0fa2, "\x0f\xa1\x0f\xb7\0" },
  { 0x0fa7, "\x0f\xa6\x0f\xb7\0" },
  { 0x0fac, "\x0f\xab\x0f\xb7\0" },
  { 0x0fb9, "\x0f\x90\x0f\xb5\0" },
  { 0x1e00, "\x00\x41\x03\x25\0" },
  { 0x1e01, "\x00\x61\x03\x25\0" },
  { 0x1e02, "\x00\x42\x03\x07\0" },
  { 0x1e03, "\x00\x62\x03\x07\0" },
  { 0x1e04, "\x00\x42\x03\x23\0" },
  { 0x1e05, "\x00\x62\x03\x23\0" },
  { 0x1e06, "\x00\x42\x03\x31\0" },
  { 0x1e07, "\x00\x62\x03\x31\0" },
  { 0x1e08, "\x00\x43\x03\x27\x03\x01\0" },
  { 0x1e09, "\x00\x63\x03\x27\x03\x01\0" },
  { 0x1e0a, "\x00\x44\x03\x07\0" },
  { 0x1e0b, "\x00\x64\x03\x07\0" },
  { 0x1e0c, "\x00\x44\x03\x23\0" },
  { 0x1e0d, "\x00\x64\x03\x23\0" },
  { 0x1e0e, "\x00\x44\x03\x31\0" },
  { 0x1e0f, "\x00\x64\x03\x31\0" },
  { 0x1e10, "\x00\x44\x03\x27\0" },
  { 0x1e11, "\x00\x64\x03\x27\0" },
  { 0x1e12, "\x00\x44\x03\x2d\0" },
  { 0x1e13, "\x00\x64\x03\x2d\0" },
  { 0x1e14, "\x00\x45\x03\x04\x03\x00\0" },
  { 0x1e15, "\x00\x65\x03\x04\x03\x00\0" },
  { 0x1e16, "\x00\x45\x03\x04\x03\x01\0" },
  { 0x1e17, "\x00\x65\x03\x04\x03\x01\0" },
  { 0x1e18, "\x00\x45\x03\x2d\0" },
  { 0x1e19, "\x00\x65\x03\x2d\0" },
  { 0x1e1a, "\x00\x45\x03\x30\0" },
  { 0x1e1b, "\x00\x65\x03\x30\0" },
  { 0x1e1c, "\x00\x45\x03\x27\x03\x06\0" },
  { 0x1e1d, "\x00\x65\x03\x27\x03\x06\0" },
  { 0x1e1e, "\x00\x46\x03\x07\0" },
  { 0x1e1f, "\x00\x66\x03\x07\0" },
  { 0x1e20, "\x00\x47\x03\x04\0" },
  { 0x1e21, "\x00\x67\x03\x04\0" },
  { 0x1e22, "\x00\x48\x03\x07\0" },
  { 0x1e23, "\x00\x68\x03\x07\0" },
  { 0x1e24, "\x00\x48\x03\x23\0" },
  { 0x1e25, "\x00\x68\x03\x23\0" },
  { 0x1e26, "\x00\x48\x03\x08\0" },
  { 0x1e27, "\x00\x68\x03\x08\0" },
  { 0x1e28, "\x00\x48\x03\x27\0" },
  { 0x1e29, "\x00\x68\x03\x27\0" },
  { 0x1e2a, "\x00\x48\x03\x2e\0" },
  { 0x1e2b, "\x00\x68\x03\x2e\0" },
  { 0x1e2c, "\x00\x49\x03\x30\0" },
  { 0x1e2d, "\x00\x69\x03\x30\0" },
  { 0x1e2e, "\x00\x49\x03\x08\x03\x01\0" },
  { 0x1e2f, "\x00\x69\x03\x08\x03\x01\0" },
  { 0x1e30, "\x00\x4b\x03\x01\0" },
  { 0x1e31, "\x00\x6b\x03\x01\0" },
  { 0x1e32, "\x00\x4b\x03\x23\0" },
  { 0x1e33, "\x00\x6b\x03\x23\0" },
  { 0x1e34, "\x00\x4b\x03\x31\0" },
  { 0x1e35, "\x00\x6b\x03\x31\0" },
  { 0x1e36, "\x00\x4c\x03\x23\0" },
  { 0x1e37, "\x00\x6c\x03\x23\0" },
  { 0x1e38, "\x00\x4c\x03\x23\x03\x04\0" },
  { 0x1e39, "\x00\x6c\x03\x23\x03\x04\0" },
  { 0x1e3a, "\x00\x4c\x03\x31\0" },
  { 0x1e3b, "\x00\x6c\x03\x31\0" },
  { 0x1e3c, "\x00\x4c\x03\x2d\0" },
  { 0x1e3d, "\x00\x6c\x03\x2d\0" },
  { 0x1e3e, "\x00\x4d\x03\x01\0" },
  { 0x1e3f, "\x00\x6d\x03\x01\0" },
  { 0x1e40, "\x00\x4d\x03\x07\0" },
  { 0x1e41, "\x00\x6d\x03\x07\0" },
  { 0x1e42, "\x00\x4d\x03\x23\0" },
  { 0x1e43, "\x00\x6d\x03\x23\0" },
  { 0x1e44, "\x00\x4e\x03\x07\0" },
  { 0x1e45, "\x00\x6e\x03\x07\0" },
  { 0x1e46, "\x00\x4e\x03\x23\0" },
  { 0x1e47, "\x00\x6e\x03\x23\0" },
  { 0x1e48, "\x00\x4e\x03\x31\0" },
  { 0x1e49, "\x00\x6e\x03\x31\0" },
  { 0x1e4a, "\x00\x4e\x03\x2d\0" },
  { 0x1e4b, "\x00\x6e\x03\x2d\0" },
  { 0x1e4c, "\x00\x4f\x03\x03\x03\x01\0" },
  { 0x1e4d, "\x00\x6f\x03\x03\x03\x01\0" },
  { 0x1e4e, "\x00\x4f\x03\x03\x03\x08\0" },
  { 0x1e4f, "\x00\x6f\x03\x03\x03\x08\0" },
  { 0x1e50, "\x00\x4f\x03\x04\x03\x00\0" },
  { 0x1e51, "\x00\x6f\x03\x04\x03\x00\0" },
  { 0x1e52, "\x00\x4f\x03\x04\x03\x01\0" },
  { 0x1e53, "\x00\x6f\x03\x04\x03\x01\0" },
  { 0x1e54, "\x00\x50\x03\x01\0" },
  { 0x1e55, "\x00\x70\x03\x01\0" },
  { 0x1e56, "\x00\x50\x03\x07\0" },
  { 0x1e57, "\x00\x70\x03\x07\0" },
  { 0x1e58, "\x00\x52\x03\x07\0" },
  { 0x1e59, "\x00\x72\x03\x07\0" },
  { 0x1e5a, "\x00\x52\x03\x23\0" },
  { 0x1e5b, "\x00\x72\x03\x23\0" },
  { 0x1e5c, "\x00\x52\x03\x23\x03\x04\0" },
  { 0x1e5d, "\x00\x72\x03\x23\x03\x04\0" },
  { 0x1e5e, "\x00\x52\x03\x31\0" },
  { 0x1e5f, "\x00\x72\x03\x31\0" },
  { 0x1e60, "\x00\x53\x03\x07\0" },
  { 0x1e61, "\x00\x73\x03\x07\0" },
  { 0x1e62, "\x00\x53\x03\x23\0" },
  { 0x1e63, "\x00\x73\x03\x23\0" },
  { 0x1e64, "\x00\x53\x03\x01\x03\x07\0" },
  { 0x1e65, "\x00\x73\x03\x01\x03\x07\0" },
  { 0x1e66, "\x00\x53\x03\x0c\x03\x07\0" },
  { 0x1e67, "\x00\x73\x03\x0c\x03\x07\0" },
  { 0x1e68, "\x00\x53\x03\x23\x03\x07\0" },
  { 0x1e69, "\x00\x73\x03\x23\x03\x07\0" },
  { 0x1e6a, "\x00\x54\x03\x07\0" },
  { 0x1e6b, "\x00\x74\x03\x07\0" },
  { 0x1e6c, "\x00\x54\x03\x23\0" },
  { 0x1e6d, "\x00\x74\x03\x23\0" },
  { 0x1e6e, "\x00\x54\x03\x31\0" },
  { 0x1e6f, "\x00\x74\x03\x31\0" },
  { 0x1e70, "\x00\x54\x03\x2d\0" },
  { 0x1e71, "\x00\x74\x03\x2d\0" },
  { 0x1e72, "\x00\x55\x03\x24\0" },
  { 0x1e73, "\x00\x75\x03\x24\0" },
  { 0x1e74, "\x00\x55\x03\x30\0" },
  { 0x1e75, "\x00\x75\x03\x30\0" },
  { 0x1e76, "\x00\x55\x03\x2d\0" },
  { 0x1e77, "\x00\x75\x03\x2d\0" },
  { 0x1e78, "\x00\x55\x03\x03\x03\x01\0" },
  { 0x1e79, "\x00\x75\x03\x03\x03\x01\0" },
  { 0x1e7a, "\x00\x55\x03\x04\x03\x08\0" },
  { 0x1e7b, "\x00\x75\x03\x04\x03\x08\0" },
  { 0x1e7c, "\x00\x56\x03\x03\0" },
  { 0x1e7d, "\x00\x76\x03\x03\0" },
  { 0x1e7e, "\x00\x56\x03\x23\0" },
  { 0x1e7f, "\x00\x76\x03\x23\0" },
  { 0x1e80, "\x00\x57\x03\x00\0" },
  { 0x1e81, "\x00\x77\x03\x00\0" },
  { 0x1e82, "\x00\x57\x03\x01\0" },
  { 0x1e83, "\x00\x77\x03\x01\0" },
  { 0x1e84, "\x00\x57\x03\x08\0" },
  { 0x1e85, "\x00\x77\x03\x08\0" },
  { 0x1e86, "\x00\x57\x03\x07\0" },
  { 0x1e87, "\x00\x77\x03\x07\0" },
  { 0x1e88, "\x00\x57\x03\x23\0" },
  { 0x1e89, "\x00\x77\x03\x23\0" },
  { 0x1e8a, "\x00\x58\x03\x07\0" },
  { 0x1e8b, "\x00\x78\x03\x07\0" },
  { 0x1e8c, "\x00\x58\x03\x08\0" },
  { 0x1e8d, "\x00\x78\x03\x08\0" },
  { 0x1e8e, "\x00\x59\x03\x07\0" },
  { 0x1e8f, "\x00\x79\x03\x07\0" },
  { 0x1e90, "\x00\x5a\x03\x02\0" },
  { 0x1e91, "\x00\x7a\x03\x02\0" },
  { 0x1e92, "\x00\x5a\x03\x23\0" },
  { 0x1e93, "\x00\x7a\x03\x23\0" },
  { 0x1e94, "\x00\x5a\x03\x31\0" },
  { 0x1e95, "\x00\x7a\x03\x31\0" },
  { 0x1e96, "\x00\x68\x03\x31\0" },
  { 0x1e97, "\x00\x74\x03\x08\0" },
  { 0x1e98, "\x00\x77\x03\x0a\0" },
  { 0x1e99, "\x00\x79\x03\x0a\0" },
  { 0x1e9b, "\x01\x7f\x03\x07\0" },
  { 0x1ea0, "\x00\x41\x03\x23\0" },
  { 0x1ea1, "\x00\x61\x03\x23\0" },
  { 0x1ea2, "\x00\x41\x03\x09\0" },
  { 0x1ea3, "\x00\x61\x03\x09\0" },
  { 0x1ea4, "\x00\x41\x03\x02\x03\x01\0" },
  { 0x1ea5, "\x00\x61\x03\x02\x03\x01\0" },
  { 0x1ea6, "\x00\x41\x03\x02\x03\x00\0" },
  { 0x1ea7, "\x00\x61\x03\x02\x03\x00\0" },
  { 0x1ea8, "\x00\x41\x03\x02\x03\x09\0" },
  { 0x1ea9, "\x00\x61\x03\x02\x03\x09\0" },
  { 0x1eaa, "\x00\x41\x03\x02\x03\x03\0" },
  { 0x1eab, "\x00\x61\x03\x02\x03\x03\0" },
  { 0x1eac, "\x00\x41\x03\x23\x03\x02\0" },
  { 0x1ead, "\x00\x61\x03\x23\x03\x02\0" },
  { 0x1eae, "\x00\x41\x03\x06\x03\x01\0" },
  { 0x1eaf, "\x00\x61\x03\x06\x03\x01\0" },
  { 0x1eb0, "\x00\x41\x03\x06\x03\x00\0" },
  { 0x1eb1, "\x00\x61\x03\x06\x03\x00\0" },
  { 0x1eb2, "\x00\x41\x03\x06\x03\x09\0" },
  { 0x1eb3, "\x00\x61\x03\x06\x03\x09\0" },
  { 0x1eb4, "\x00\x41\x03\x06\x03\x03\0" },
  { 0x1eb5, "\x00\x61\x03\x06\x03\x03\0" },
  { 0x1eb6, "\x00\x41\x03\x23\x03\x06\0" },
  { 0x1eb7, "\x00\x61\x03\x23\x03\x06\0" },
  { 0x1eb8, "\x00\x45\x03\x23\0" },
  { 0x1eb9, "\x00\x65\x03\x23\0" },
  { 0x1eba, "\x00\x45\x03\x09\0" },
  { 0x1ebb, "\x00\x65\x03\x09\0" },
  { 0x1ebc, "\x00\x45\x03\x03\0" },
  { 0x1ebd, "\x00\x65\x03\x03\0" },
  { 0x1ebe, "\x00\x45\x03\x02\x03\x01\0" },
  { 0x1ebf, "\x00\x65\x03\x02\x03\x01\0" },
  { 0x1ec0, "\x00\x45\x03\x02\x03\x00\0" },
  { 0x1ec1, "\x00\x65\x03\x02\x03\x00\0" },
  { 0x1ec2, "\x00\x45\x03\x02\x03\x09\0" },
  { 0x1ec3, "\x00\x65\x03\x02\x03\x09\0" },
  { 0x1ec4, "\x00\x45\x03\x02\x03\x03\0" },
  { 0x1ec5, "\x00\x65\x03\x02\x03\x03\0" },
  { 0x1ec6, "\x00\x45\x03\x23\x03\x02\0" },
  { 0x1ec7, "\x00\x65\x03\x23\x03\x02\0" },
  { 0x1ec8, "\x00\x49\x03\x09\0" },
  { 0x1ec9, "\x00\x69\x03\x09\0" },
  { 0x1eca, "\x00\x49\x03\x23\0" },
  { 0x1ecb, "\x00\x69\x03\x23\0" },
  { 0x1ecc, "\x00\x4f\x03\x23\0" },
  { 0x1ecd, "\x00\x6f\x03\x23\0" },
  { 0x1ece, "\x00\x4f\x03\x09\0" },
  { 0x1ecf, "\x00\x6f\x03\x09\0" },
  { 0x1ed0, "\x00\x4f\x03\x02\x03\x01\0" },
  { 0x1ed1, "\x00\x6f\x03\x02\x03\x01\0" },
  { 0x1ed2, "\x00\x4f\x03\x02\x03\x00\0" },
  { 0x1ed3, "\x00\x6f\x03\x02\x03\x00\0" },
  { 0x1ed4, "\x00\x4f\x03\x02\x03\x09\0" },
  { 0x1ed5, "\x00\x6f\x03\x02\x03\x09\0" },
  { 0x1ed6, "\x00\x4f\x03\x02\x03\x03\0" },
  { 0x1ed7, "\x00\x6f\x03\x02\x03\x03\0" },
  { 0x1ed8, "\x00\x4f\x03\x23\x03\x02\0" },
  { 0x1ed9, "\x00\x6f\x03\x23\x03\x02\0" },
  { 0x1eda, "\x00\x4f\x03\x1b\x03\x01\0" },
  { 0x1edb, "\x00\x6f\x03\x1b\x03\x01\0" },
  { 0x1edc, "\x00\x4f\x03\x1b\x03\x00\0" },
  { 0x1edd, "\x00\x6f\x03\x1b\x03\x00\0" },
  { 0x1ede, "\x00\x4f\x03\x1b\x03\x09\0" },
  { 0x1edf, "\x00\x6f\x03\x1b\x03\x09\0" },
  { 0x1ee0, "\x00\x4f\x03\x1b\x03\x03\0" },
  { 0x1ee1, "\x00\x6f\x03\x1b\x03\x03\0" },
  { 0x1ee2, "\x00\x4f\x03\x1b\x03\x23\0" },
  { 0x1ee3, "\x00\x6f\x03\x1b\x03\x23\0" },
  { 0x1ee4, "\x00\x55\x03\x23\0" },
  { 0x1ee5, "\x00\x75\x03\x23\0" },
  { 0x1ee6, "\x00\x55\x03\x09\0" },
  { 0x1ee7, "\x00\x75\x03\x09\0" },
  { 0x1ee8, "\x00\x55\x03\x1b\x03\x01\0" },
  { 0x1ee9, "\x00\x75\x03\x1b\x03\x01\0" },
  { 0x1eea, "\x00\x55\x03\x1b\x03\x00\0" },
  { 0x1eeb, "\x00\x75\x03\x1b\x03\x00\0" },
  { 0x1eec, "\x00\x55\x03\x1b\x03\x09\0" },
  { 0x1eed, "\x00\x75\x03\x1b\x03\x09\0" },
  { 0x1eee, "\x00\x55\x03\x1b\x03\x03\0" },
  { 0x1eef, "\x00\x75\x03\x1b\x03\x03\0" },
  { 0x1ef0, "\x00\x55\x03\x1b\x03\x23\0" },
  { 0x1ef1, "\x00\x75\x03\x1b\x03\x23\0" },
  { 0x1ef2, "\x00\x59\x03\x00\0" },
  { 0x1ef3, "\x00\x79\x03\x00\0" },
  { 0x1ef4, "\x00\x59\x03\x23\0" },
  { 0x1ef5, "\x00\x79\x03\x23\0" },
  { 0x1ef6, "\x00\x59\x03\x09\0" },
  { 0x1ef7, "\x00\x79\x03\x09\0" },
  { 0x1ef8, "\x00\x59\x03\x03\0" },
  { 0x1ef9, "\x00\x79\x03\x03\0" },
  { 0x1f00, "\x03\xb1\x03\x13\0" },
  { 0x1f01, "\x03\xb1\x03\x14\0" },
  { 0x1f02, "\x03\xb1\x03\x13\x03\x00\0" },
  { 0x1f03, "\x03\xb1\x03\x14\x03\x00\0" },
  { 0x1f04, "\x03\xb1\x03\x13\x03\x01\0" },
  { 0x1f05, "\x03\xb1\x03\x14\x03\x01\0" },
  { 0x1f06, "\x03\xb1\x03\x13\x03\x42\0" },
  { 0x1f07, "\x03\xb1\x03\x14\x03\x42\0" },
  { 0x1f08, "\x03\x91\x03\x13\0" },
  { 0x1f09, "\x03\x91\x03\x14\0" },
  { 0x1f0a, "\x03\x91\x03\x13\x03\x00\0" },
  { 0x1f0b, "\x03\x91\x03\x14\x03\x00\0" },
  { 0x1f0c, "\x03\x91\x03\x13\x03\x01\0" },
  { 0x1f0d, "\x03\x91\x03\x14\x03\x01\0" },
  { 0x1f0e, "\x03\x91\x03\x13\x03\x42\0" },
  { 0x1f0f, "\x03\x91\x03\x14\x03\x42\0" },
  { 0x1f10, "\x03\xb5\x03\x13\0" },
  { 0x1f11, "\x03\xb5\x03\x14\0" },
  { 0x1f12, "\x03\xb5\x03\x13\x03\x00\0" },
  { 0x1f13, "\x03\xb5\x03\x14\x03\x00\0" },
  { 0x1f14, "\x03\xb5\x03\x13\x03\x01\0" },
  { 0x1f15, "\x03\xb5\x03\x14\x03\x01\0" },
  { 0x1f18, "\x03\x95\x03\x13\0" },
  { 0x1f19, "\x03\x95\x03\x14\0" },
  { 0x1f1a, "\x03\x95\x03\x13\x03\x00\0" },
  { 0x1f1b, "\x03\x95\x03\x14\x03\x00\0" },
  { 0x1f1c, "\x03\x95\x03\x13\x03\x01\0" },
  { 0x1f1d, "\x03\x95\x03\x14\x03\x01\0" },
  { 0x1f20, "\x03\xb7\x03\x13\0" },
  { 0x1f21, "\x03\xb7\x03\x14\0" },
  { 0x1f22, "\x03\xb7\x03\x13\x03\x00\0" },
  { 0x1f23, "\x03\xb7\x03\x14\x03\x00\0" },
  { 0x1f24, "\x03\xb7\x03\x13\x03\x01\0" },
  { 0x1f25, "\x03\xb7\x03\x14\x03\x01\0" },
  { 0x1f26, "\x03\xb7\x03\x13\x03\x42\0" },
  { 0x1f27, "\x03\xb7\x03\x14\x03\x42\0" },
  { 0x1f28, "\x03\x97\x03\x13\0" },
  { 0x1f29, "\x03\x97\x03\x14\0" },
  { 0x1f2a, "\x03\x97\x03\x13\x03\x00\0" },
  { 0x1f2b, "\x03\x97\x03\x14\x03\x00\0" },
  { 0x1f2c, "\x03\x97\x03\x13\x03\x01\0" },
  { 0x1f2d, "\x03\x97\x03\x14\x03\x01\0" },
  { 0x1f2e, "\x03\x97\x03\x13\x03\x42\0" },
  { 0x1f2f, "\x03\x97\x03\x14\x03\x42\0" },
  { 0x1f30, "\x03\xb9\x03\x13\0" },
  { 0x1f31, "\x03\xb9\x03\x14\0" },
  { 0x1f32, "\x03\xb9\x03\x13\x03\x00\0" },
  { 0x1f33, "\x03\xb9\x03\x14\x03\x00\0" },
  { 0x1f34, "\x03\xb9\x03\x13\x03\x01\0" },
  { 0x1f35, "\x03\xb9\x03\x14\x03\x01\0" },
  { 0x1f36, "\x03\xb9\x03\x13\x03\x42\0" },
  { 0x1f37, "\x03\xb9\x03\x14\x03\x42\0" },
  { 0x1f38, "\x03\x99\x03\x13\0" },
  { 0x1f39, "\x03\x99\x03\x14\0" },
  { 0x1f3a, "\x03\x99\x03\x13\x03\x00\0" },
  { 0x1f3b, "\x03\x99\x03\x14\x03\x00\0" },
  { 0x1f3c, "\x03\x99\x03\x13\x03\x01\0" },
  { 0x1f3d, "\x03\x99\x03\x14\x03\x01\0" },
  { 0x1f3e, "\x03\x99\x03\x13\x03\x42\0" },
  { 0x1f3f, "\x03\x99\x03\x14\x03\x42\0" },
  { 0x1f40, "\x03\xbf\x03\x13\0" },
  { 0x1f41, "\x03\xbf\x03\x14\0" },
  { 0x1f42, "\x03\xbf\x03\x13\x03\x00\0" },
  { 0x1f43, "\x03\xbf\x03\x14\x03\x00\0" },
  { 0x1f44, "\x03\xbf\x03\x13\x03\x01\0" },
  { 0x1f45, "\x03\xbf\x03\x14\x03\x01\0" },
  { 0x1f48, "\x03\x9f\x03\x13\0" },
  { 0x1f49, "\x03\x9f\x03\x14\0" },
  { 0x1f4a, "\x03\x9f\x03\x13\x03\x00\0" },
  { 0x1f4b, "\x03\x9f\x03\x14\x03\x00\0" },
  { 0x1f4c, "\x03\x9f\x03\x13\x03\x01\0" },
  { 0x1f4d, "\x03\x9f\x03\x14\x03\x01\0" },
  { 0x1f50, "\x03\xc5\x03\x13\0" },
  { 0x1f51, "\x03\xc5\x03\x14\0" },
  { 0x1f52, "\x03\xc5\x03\x13\x03\x00\0" },
  { 0x1f53, "\x03\xc5\x03\x14\x03\x00\0" },
  { 0x1f54, "\x03\xc5\x03\x13\x03\x01\0" },
  { 0x1f55, "\x03\xc5\x03\x14\x03\x01\0" },
  { 0x1f56, "\x03\xc5\x03\x13\x03\x42\0" },
  { 0x1f57, "\x03\xc5\x03\x14\x03\x42\0" },
  { 0x1f59, "\x03\xa5\x03\x14\0" },
  { 0x1f5b, "\x03\xa5\x03\x14\x03\x00\0" },
  { 0x1f5d, "\x03\xa5\x03\x14\x03\x01\0" },
  { 0x1f5f, "\x03\xa5\x03\x14\x03\x42\0" },
  { 0x1f60, "\x03\xc9\x03\x13\0" },
  { 0x1f61, "\x03\xc9\x03\x14\0" },
  { 0x1f62, "\x03\xc9\x03\x13\x03\x00\0" },
  { 0x1f63, "\x03\xc9\x03\x14\x03\x00\0" },
  { 0x1f64, "\x03\xc9\x03\x13\x03\x01\0" },
  { 0x1f65, "\x03\xc9\x03\x14\x03\x01\0" },
  { 0x1f66, "\x03\xc9\x03\x13\x03\x42\0" },
  { 0x1f67, "\x03\xc9\x03\x14\x03\x42\0" },
  { 0x1f68, "\x03\xa9\x03\x13\0" },
  { 0x1f69, "\x03\xa9\x03\x14\0" },
  { 0x1f6a, "\x03\xa9\x03\x13\x03\x00\0" },
  { 0x1f6b, "\x03\xa9\x03\x14\x03\x00\0" },
  { 0x1f6c, "\x03\xa9\x03\x13\x03\x01\0" },
  { 0x1f6d, "\x03\xa9\x03\x14\x03\x01\0" },
  { 0x1f6e, "\x03\xa9\x03\x13\x03\x42\0" },
  { 0x1f6f, "\x03\xa9\x03\x14\x03\x42\0" },
  { 0x1f70, "\x03\xb1\x03\x00\0" },
  { 0x1f71, "\x03\xb1\x03\x01\0" },
  { 0x1f72, "\x03\xb5\x03\x00\0" },
  { 0x1f73, "\x03\xb5\x03\x01\0" },
  { 0x1f74, "\x03\xb7\x03\x00\0" },
  { 0x1f75, "\x03\xb7\x03\x01\0" },
  { 0x1f76, "\x03\xb9\x03\x00\0" },
  { 0x1f77, "\x03\xb9\x03\x01\0" },
  { 0x1f78, "\x03\xbf\x03\x00\0" },
  { 0x1f79, "\x03\xbf\x03\x01\0" },
  { 0x1f7a, "\x03\xc5\x03\x00\0" },
  { 0x1f7b, "\x03\xc5\x03\x01\0" },
  { 0x1f7c, "\x03\xc9\x03\x00\0" },
  { 0x1f7d, "\x03\xc9\x03\x01\0" },
  { 0x1f80, "\x03\xb1\x03\x13\x03\x45\0" },
  { 0x1f81, "\x03\xb1\x03\x14\x03\x45\0" },
  { 0x1f82, "\x03\xb1\x03\x13\x03\x00\x03\x45\0" },
  { 0x1f83, "\x03\xb1\x03\x14\x03\x00\x03\x45\0" },
  { 0x1f84, "\x03\xb1\x03\x13\x03\x01\x03\x45\0" },
  { 0x1f85, "\x03\xb1\x03\x14\x03\x01\x03\x45\0" },
  { 0x1f86, "\x03\xb1\x03\x13\x03\x42\x03\x45\0" },
  { 0x1f87, "\x03\xb1\x03\x14\x03\x42\x03\x45\0" },
  { 0x1f88, "\x03\x91\x03\x13\x03\x45\0" },
  { 0x1f89, "\x03\x91\x03\x14\x03\x45\0" },
  { 0x1f8a, "\x03\x91\x03\x13\x03\x00\x03\x45\0" },
  { 0x1f8b, "\x03\x91\x03\x14\x03\x00\x03\x45\0" },
  { 0x1f8c, "\x03\x91\x03\x13\x03\x01\x03\x45\0" },
  { 0x1f8d, "\x03\x91\x03\x14\x03\x01\x03\x45\0" },
  { 0x1f8e, "\x03\x91\x03\x13\x03\x42\x03\x45\0" },
  { 0x1f8f, "\x03\x91\x03\x14\x03\x42\x03\x45\0" },
  { 0x1f90, "\x03\xb7\x03\x13\x03\x45\0" },
  { 0x1f91, "\x03\xb7\x03\x14\x03\x45\0" },
  { 0x1f92, "\x03\xb7\x03\x13\x03\x00\x03\x45\0" },
  { 0x1f93, "\x03\xb7\x03\x14\x03\x00\x03\x45\0" },
  { 0x1f94, "\x03\xb7\x03\x13\x03\x01\x03\x45\0" },
  { 0x1f95, "\x03\xb7\x03\x14\x03\x01\x03\x45\0" },
  { 0x1f96, "\x03\xb7\x03\x13\x03\x42\x03\x45\0" },
  { 0x1f97, "\x03\xb7\x03\x14\x03\x42\x03\x45\0" },
  { 0x1f98, "\x03\x97\x03\x13\x03\x45\0" },
  { 0x1f99, "\x03\x97\x03\x14\x03\x45\0" },
  { 0x1f9a, "\x03\x97\x03\x13\x03\x00\x03\x45\0" },
  { 0x1f9b, "\x03\x97\x03\x14\x03\x00\x03\x45\0" },
  { 0x1f9c, "\x03\x97\x03\x13\x03\x01\x03\x45\0" },
  { 0x1f9d, "\x03\x97\x03\x14\x03\x01\x03\x45\0" },
  { 0x1f9e, "\x03\x97\x03\x13\x03\x42\x03\x45\0" },
  { 0x1f9f, "\x03\x97\x03\x14\x03\x42\x03\x45\0" },
  { 0x1fa0, "\x03\xc9\x03\x13\x03\x45\0" },
  { 0x1fa1, "\x03\xc9\x03\x14\x03\x45\0" },
  { 0x1fa2, "\x03\xc9\x03\x13\x03\x00\x03\x45\0" },
  { 0x1fa3, "\x03\xc9\x03\x14\x03\x00\x03\x45\0" },
  { 0x1fa4, "\x03\xc9\x03\x13\x03\x01\x03\x45\0" },
  { 0x1fa5, "\x03\xc9\x03\x14\x03\x01\x03\x45\0" },
  { 0x1fa6, "\x03\xc9\x03\x13\x03\x42\x03\x45\0" },
  { 0x1fa7, "\x03\xc9\x03\x14\x03\x42\x03\x45\0" },
  { 0x1fa8, "\x03\xa9\x03\x13\x03\x45\0" },
  { 0x1fa9, "\x03\xa9\x03\x14\x03\x45\0" },
  { 0x1faa, "\x03\xa9\x03\x13\x03\x00\x03\x45\0" },
  { 0x1fab, "\x03\xa9\x03\x14\x03\x00\x03\x45\0" },
  { 0x1fac, "\x03\xa9\x03\x13\x03\x01\x03\x45\0" },
  { 0x1fad, "\x03\xa9\x03\x14\x03\x01\x03\x45\0" },
  { 0x1fae, "\x03\xa9\x03\x13\x03\x42\x03\x45\0" },
  { 0x1faf, "\x03\xa9\x03\x14\x03\x42\x03\x45\0" },
  { 0x1fb0, "\x03\xb1\x03\x06\0" },
  { 0x1fb1, "\x03\xb1\x03\x04\0" },
  { 0x1fb2, "\x03\xb1\x03\x00\x03\x45\0" },
  { 0x1fb3, "\x03\xb1\x03\x45\0" },
  { 0x1fb4, "\x03\xb1\x03\x01\x03\x45\0" },
  { 0x1fb6, "\x03\xb1\x03\x42\0" },
  { 0x1fb7, "\x03\xb1\x03\x42\x03\x45\0" },
  { 0x1fb8, "\x03\x91\x03\x06\0" },
  { 0x1fb9, "\x03\x91\x03\x04\0" },
  { 0x1fba, "\x03\x91\x03\x00\0" },
  { 0x1fbb, "\x03\x91\x03\x01\0" },
  { 0x1fbc, "\x03\x91\x03\x45\0" },
  { 0x1fbe, "\x03\xb9\0" },
  { 0x1fc1, "\x00\xa8\x03\x42\0" },
  { 0x1fc2, "\x03\xb7\x03\x00\x03\x45\0" },
  { 0x1fc3, "\x03\xb7\x03\x45\0" },
  { 0x1fc4, "\x03\xb7\x03\x01\x03\x45\0" },
  { 0x1fc6, "\x03\xb7\x03\x42\0" },
  { 0x1fc7, "\x03\xb7\x03\x42\x03\x45\0" },
  { 0x1fc8, "\x03\x95\x03\x00\0" },
  { 0x1fc9, "\x03\x95\x03\x01\0" },
  { 0x1fca, "\x03\x97\x03\x00\0" },
  { 0x1fcb, "\x03\x97\x03\x01\0" },
  { 0x1fcc, "\x03\x97\x03\x45\0" },
  { 0x1fcd, "\x1f\xbf\x03\x00\0" },
  { 0x1fce, "\x1f\xbf\x03\x01\0" },
  { 0x1fcf, "\x1f\xbf\x03\x42\0" },
  { 0x1fd0, "\x03\xb9\x03\x06\0" },
  { 0x1fd1, "\x03\xb9\x03\x04\0" },
  { 0x1fd2, "\x03\xb9\x03\x08\x03\x00\0" },
  { 0x1fd3, "\x03\xb9\x03\x08\x03\x01\0" },
  { 0x1fd6, "\x03\xb9\x03\x42\0" },
  { 0x1fd7, "\x03\xb9\x03\x08\x03\x42\0" },
  { 0x1fd8, "\x03\x99\x03\x06\0" },
  { 0x1fd9, "\x03\x99\x03\x04\0" },
  { 0x1fda, "\x03\x99\x03\x00\0" },
  { 0x1fdb, "\x03\x99\x03\x01\0" },
  { 0x1fdd, "\x1f\xfe\x03\x00\0" },
  { 0x1fde, "\x1f\xfe\x03\x01\0" },
  { 0x1fdf, "\x1f\xfe\x03\x42\0" },
  { 0x1fe0, "\x03\xc5\x03\x06\0" },
  { 0x1fe1, "\x03\xc5\x03\x04\0" },
  { 0x1fe2, "\x03\xc5\x03\x08\x03\x00\0" },
  { 0x1fe3, "\x03\xc5\x03\x08\x03\x01\0" },
  { 0x1fe4, "\x03\xc1\x03\x13\0" },
  { 0x1fe5, "\x03\xc1\x03\x14\0" },
  { 0x1fe6, "\x03\xc5\x03\x42\0" },
  { 0x1fe7, "\x03\xc5\x03\x08\x03\x42\0" },
  { 0x1fe8, "\x03\xa5\x03\x06\0" },
  { 0x1fe9, "\x03\xa5\x03\x04\0" },
  { 0x1fea, "\x03\xa5\x03\x00\0" },
  { 0x1feb, "\x03\xa5\x03\x01\0" },
  { 0x1fec, "\x03\xa1\x03\x14\0" },
  { 0x1fed, "\x00\xa8\x03\x00\0" },
  { 0x1fee, "\x00\xa8\x03\x01\0" },
  { 0x1fef, "\x00\x60\0" },
  { 0x1ff2, "\x03\xc9\x03\x00\x03\x45\0" },
  { 0x1ff3, "\x03\xc9\x03\x45\0" },
  { 0x1ff4, "\x03\xc9\x03\x01\x03\x45\0" },
  { 0x1ff6, "\x03\xc9\x03\x42\0" },
  { 0x1ff7, "\x03\xc9\x03\x42\x03\x45\0" },
  { 0x1ff8, "\x03\x9f\x03\x00\0" },
  { 0x1ff9, "\x03\x9f\x03\x01\0" },
  { 0x1ffa, "\x03\xa9\x03\x00\0" },
  { 0x1ffb, "\x03\xa9\x03\x01\0" },
  { 0x1ffc, "\x03\xa9\x03\x45\0" },
  { 0x1ffd, "\x00\xb4\0" },
  { 0x2000, "\x20\x02\0" },
  { 0x2001, "\x20\x03\0" },
  { 0x2126, "\x03\xa9\0" },
  { 0x212a, "\x00\x4b\0" },
  { 0x212b, "\x00\x41\x03\x0a\0" },
  { 0x2204, "\x22\x03\x03\x38\0" },
  { 0x2209, "\x22\x08\x03\x38\0" },
  { 0x220c, "\x22\x0b\x03\x38\0" },
  { 0x2224, "\x22\x23\x03\x38\0" },
  { 0x2226, "\x22\x25\x03\x38\0" },
  { 0x2241, "\x00\x7e\x03\x38\0" },
  { 0x2244, "\x22\x43\x03\x38\0" },
  { 0x2247, "\x22\x45\x03\x38\0" },
  { 0x2249, "\x22\x48\x03\x38\0" },
  { 0x2260, "\x00\x3d\x03\x38\0" },
  { 0x2262, "\x22\x61\x03\x38\0" },
  { 0x226d, "\x22\x4d\x03\x38\0" },
  { 0x226e, "\x00\x3c\x03\x38\0" },
  { 0x226f, "\x00\x3e\x03\x38\0" },
  { 0x2270, "\x22\x64\x03\x38\0" },
  { 0x2271, "\x22\x65\x03\x38\0" },
  { 0x2274, "\x22\x72\x03\x38\0" },
  { 0x2275, "\x22\x73\x03\x38\0" },
  { 0x2278, "\x22\x76\x03\x38\0" },
  { 0x2279, "\x22\x77\x03\x38\0" },
  { 0x2280, "\x22\x7a\x03\x38\0" },
  { 0x2281, "\x22\x7b\x03\x38\0" },
  { 0x2284, "\x22\x82\x03\x38\0" },
  { 0x2285, "\x22\x83\x03\x38\0" },
  { 0x2288, "\x22\x86\x03\x38\0" },
  { 0x2289, "\x22\x87\x03\x38\0" },
  { 0x22ac, "\x22\xa2\x03\x38\0" },
  { 0x22ad, "\x22\xa8\x03\x38\0" },
  { 0x22ae, "\x22\xa9\x03\x38\0" },
  { 0x22af, "\x22\xab\x03\x38\0" },
  { 0x22e0, "\x22\x7c\x03\x38\0" },
  { 0x22e1, "\x22\x7d\x03\x38\0" },
  { 0x22e2, "\x22\x91\x03\x38\0" },
  { 0x22e3, "\x22\x92\x03\x38\0" },
  { 0x22ea, "\x22\xb2\x03\x38\0" },
  { 0x22eb, "\x22\xb3\x03\x38\0" },
  { 0x22ec, "\x22\xb4\x03\x38\0" },
  { 0x22ed, "\x22\xb5\x03\x38\0" },
  { 0x2329, "\x30\x08\0" },
  { 0x232a, "\x30\x09\0" },
  { 0x304c, "\x30\x4b\x30\x99\0" },
  { 0x304e, "\x30\x4d\x30\x99\0" },
  { 0x3050, "\x30\x4f\x30\x99\0" },
  { 0x3052, "\x30\x51\x30\x99\0" },
  { 0x3054, "\x30\x53\x30\x99\0" },
  { 0x3056, "\x30\x55\x30\x99\0" },
  { 0x3058, "\x30\x57\x30\x99\0" },
  { 0x305a, "\x30\x59\x30\x99\0" },
  { 0x305c, "\x30\x5b\x30\x99\0" },
  { 0x305e, "\x30\x5d\x30\x99\0" },
  { 0x3060, "\x30\x5f\x30\x99\0" },
  { 0x3062, "\x30\x61\x30\x99\0" },
  { 0x3065, "\x30\x64\x30\x99\0" },
  { 0x3067, "\x30\x66\x30\x99\0" },
  { 0x3069, "\x30\x68\x30\x99\0" },
  { 0x3070, "\x30\x6f\x30\x99\0" },
  { 0x3071, "\x30\x6f\x30\x9a\0" },
  { 0x3073, "\x30\x72\x30\x99\0" },
  { 0x3074, "\x30\x72\x30\x9a\0" },
  { 0x3076, "\x30\x75\x30\x99\0" },
  { 0x3077, "\x30\x75\x30\x9a\0" },
  { 0x3079, "\x30\x78\x30\x99\0" },
  { 0x307a, "\x30\x78\x30\x9a\0" },
  { 0x307c, "\x30\x7b\x30\x99\0" },
  { 0x307d, "\x30\x7b\x30\x9a\0" },
  { 0x3094, "\x30\x46\x30\x99\0" },
  { 0x309e, "\x30\x9d\x30\x99\0" },
  { 0x30ac, "\x30\xab\x30\x99\0" },
  { 0x30ae, "\x30\xad\x30\x99\0" },
  { 0x30b0, "\x30\xaf\x30\x99\0" },
  { 0x30b2, "\x30\xb1\x30\x99\0" },
  { 0x30b4, "\x30\xb3\x30\x99\0" },
  { 0x30b6, "\x30\xb5\x30\x99\0" },
  { 0x30b8, "\x30\xb7\x30\x99\0" },
  { 0x30ba, "\x30\xb9\x30\x99\0" },
  { 0x30bc, "\x30\xbb\x30\x99\0" },
  { 0x30be, "\x30\xbd\x30\x99\0" },
  { 0x30c0, "\x30\xbf\x30\x99\0" },
  { 0x30c2, "\x30\xc1\x30\x99\0" },
  { 0x30c5, "\x30\xc4\x30\x99\0" },
  { 0x30c7, "\x30\xc6\x30\x99\0" },
  { 0x30c9, "\x30\xc8\x30\x99\0" },
  { 0x30d0, "\x30\xcf\x30\x99\0" },
  { 0x30d1, "\x30\xcf\x30\x9a\0" },
  { 0x30d3, "\x30\xd2\x30\x99\0" },
  { 0x30d4, "\x30\xd2\x30\x9a\0" },
  { 0x30d6, "\x30\xd5\x30\x99\0" },
  { 0x30d7, "\x30\xd5\x30\x9a\0" },
  { 0x30d9, "\x30\xd8\x30\x99\0" },
  { 0x30da, "\x30\xd8\x30\x9a\0" },
  { 0x30dc, "\x30\xdb\x30\x99\0" },
  { 0x30dd, "\x30\xdb\x30\x9a\0" },
  { 0x30f4, "\x30\xa6\x30\x99\0" },
  { 0x30f7, "\x30\xef\x30\x99\0" },
  { 0x30f8, "\x30\xf0\x30\x99\0" },
  { 0x30f9, "\x30\xf1\x30\x99\0" },
  { 0x30fa, "\x30\xf2\x30\x99\0" },
  { 0x30fe, "\x30\xfd\x30\x99\0" },
  { 0xf900, "\x8c\x48\0" },
  { 0xf901, "\x66\xf4\0" },
  { 0xf902, "\x8e\xca\0" },
  { 0xf903, "\x8c\xc8\0" },
  { 0xf904, "\x6e\xd1\0" },
  { 0xf905, "\x4e\x32\0" },
  { 0xf906, "\x53\xe5\0" },
  { 0xf907, "\x9f\x9c\0" },
  { 0xf908, "\x9f\x9c\0" },
  { 0xf909, "\x59\x51\0" },
  { 0xf90a, "\x91\xd1\0" },
  { 0xf90b, "\x55\x87\0" },
  { 0xf90c, "\x59\x48\0" },
  { 0xf90d, "\x61\xf6\0" },
  { 0xf90e, "\x76\x69\0" },
  { 0xf90f, "\x7f\x85\0" },
  { 0xf910, "\x86\x3f\0" },
  { 0xf911, "\x87\xba\0" },
  { 0xf912, "\x88\xf8\0" },
  { 0xf913, "\x90\x8f\0" },
  { 0xf914, "\x6a\x02\0" },
  { 0xf915, "\x6d\x1b\0" },
  { 0xf916, "\x70\xd9\0" },
  { 0xf917, "\x73\xde\0" },
  { 0xf918, "\x84\x3d\0" },
  { 0xf919, "\x91\x6a\0" },
  { 0xf91a, "\x99\xf1\0" },
  { 0xf91b, "\x4e\x82\0" },
  { 0xf91c, "\x53\x75\0" },
  { 0xf91d, "\x6b\x04\0" },
  { 0xf91e, "\x72\x1b\0" },
  { 0xf91f, "\x86\x2d\0" },
  { 0xf920, "\x9e\x1e\0" },
  { 0xf921, "\x5d\x50\0" },
  { 0xf922, "\x6f\xeb\0" },
  { 0xf923, "\x85\xcd\0" },
  { 0xf924, "\x89\x64\0" },
  { 0xf925, "\x62\xc9\0" },
  { 0xf926, "\x81\xd8\0" },
  { 0xf927, "\x88\x1f\0" },
  { 0xf928, "\x5e\xca\0" },
  { 0xf929, "\x67\x17\0" },
  { 0xf92a, "\x6d\x6a\0" },
  { 0xf92b, "\x72\xfc\0" },
  { 0xf92c, "\x90\xce\0" },
  { 0xf92d, "\x4f\x86\0" },
  { 0xf92e, "\x51\xb7\0" },
  { 0xf92f, "\x52\xde\0" },
  { 0xf930, "\x64\xc4\0" },
  { 0xf931, "\x6a\xd3\0" },
  { 0xf932, "\x72\x10\0" },
  { 0xf933, "\x76\xe7\0" },
  { 0xf934, "\x80\x01\0" },
  { 0xf935, "\x86\x06\0" },
  { 0xf936, "\x86\x5c\0" },
  { 0xf937, "\x8d\xef\0" },
  { 0xf938, "\x97\x32\0" },
  { 0xf939, "\x9b\x6f\0" },
  { 0xf93a, "\x9d\xfa\0" },
  { 0xf93b, "\x78\x8c\0" },
  { 0xf93c, "\x79\x7f\0" },
  { 0xf93d, "\x7d\xa0\0" },
  { 0xf93e, "\x83\xc9\0" },
  { 0xf93f, "\x93\x04\0" },
  { 0xf940, "\x9e\x7f\0" },
  { 0xf941, "\x8a\xd6\0" },
  { 0xf942, "\x58\xdf\0" },
  { 0xf943, "\x5f\x04\0" },
  { 0xf944, "\x7c\x60\0" },
  { 0xf945, "\x80\x7e\0" },
  { 0xf946, "\x72\x62\0" },
  { 0xf947, "\x78\xca\0" },
  { 0xf948, "\x8c\xc2\0" },
  { 0xf949, "\x96\xf7\0" },
  { 0xf94a, "\x58\xd8\0" },
  { 0xf94b, "\x5c\x62\0" },
  { 0xf94c, "\x6a\x13\0" },
  { 0xf94d, "\x6d\xda\0" },
  { 0xf94e, "\x6f\x0f\0" },
  { 0xf94f, "\x7d\x2f\0" },
  { 0xf950, "\x7e\x37\0" },
  { 0xf951, "\x96\xfb\0" },
  { 0xf952, "\x52\xd2\0" },
  { 0xf953, "\x80\x8b\0" },
  { 0xf954, "\x51\xdc\0" },
  { 0xf955, "\x51\xcc\0" },
  { 0xf956, "\x7a\x1c\0" },
  { 0xf957, "\x7d\xbe\0" },
  { 0xf958, "\x83\xf1\0" },
  { 0xf959, "\x96\x75\0" },
  { 0xf95a, "\x8b\x80\0" },
  { 0xf95b, "\x62\xcf\0" },
  { 0xf95c, "\x6a\x02\0" },
  { 0xf95d, "\x8a\xfe\0" },
  { 0xf95e, "\x4e\x39\0" },
  { 0xf95f, "\x5b\xe7\0" },
  { 0xf960, "\x60\x12\0" },
  { 0xf961, "\x73\x87\0" },
  { 0xf962, "\x75\x70\0" },
  { 0xf963, "\x53\x17\0" },
  { 0xf964, "\x78\xfb\0" },
  { 0xf965, "\x4f\xbf\0" },
  { 0xf966, "\x5f\xa9\0" },
  { 0xf967, "\x4e\x0d\0" },
  { 0xf968, "\x6c\xcc\0" },
  { 0xf969, "\x65\x78\0" },
  { 0xf96a, "\x7d\x22\0" },
  { 0xf96b, "\x53\xc3\0" },
  { 0xf96c, "\x58\x5e\0" },
  { 0xf96d, "\x77\x01\0" },
  { 0xf96e, "\x84\x49\0" },
  { 0xf96f, "\x8a\xaa\0" },
  { 0xf970, "\x6b\xba\0" },
  { 0xf971, "\x8f\xb0\0" },
  { 0xf972, "\x6c\x88\0" },
  { 0xf973, "\x62\xfe\0" },
  { 0xf974, "\x82\xe5\0" },
  { 0xf975, "\x63\xa0\0" },
  { 0xf976, "\x75\x65\0" },
  { 0xf977, "\x4e\xae\0" },
  { 0xf978, "\x51\x69\0" },
  { 0xf979, "\x51\xc9\0" },
  { 0xf97a, "\x68\x81\0" },
  { 0xf97b, "\x7c\xe7\0" },
  { 0xf97c, "\x82\x6f\0" },
  { 0xf97d, "\x8a\xd2\0" },
  { 0xf97e, "\x91\xcf\0" },
  { 0xf97f, "\x52\xf5\0" },
  { 0xf980, "\x54\x42\0" },
  { 0xf981, "\x59\x73\0" },
  { 0xf982, "\x5e\xec\0" },
  { 0xf983, "\x65\xc5\0" },
  { 0xf984, "\x6f\xfe\0" },
  { 0xf985, "\x79\x2a\0" },
  { 0xf986, "\x95\xad\0" },
  { 0xf987, "\x9a\x6a\0" },
  { 0xf988, "\x9e\x97\0" },
  { 0xf989, "\x9e\xce\0" },
  { 0xf98a, "\x52\x9b\0" },
  { 0xf98b, "\x66\xc6\0" },
  { 0xf98c, "\x6b\x77\0" },
  { 0xf98d, "\x8f\x62\0" },
  { 0xf98e, "\x5e\x74\0" },
  { 0xf98f, "\x61\x90\0" },
  { 0xf990, "\x62\x00\0" },
  { 0xf991, "\x64\x9a\0" },
  { 0xf992, "\x6f\x23\0" },
  { 0xf993, "\x71\x49\0" },
  { 0xf994, "\x74\x89\0" },
  { 0xf995, "\x79\xca\0" },
  { 0xf996, "\x7d\xf4\0" },
  { 0xf997, "\x80\x6f\0" },
  { 0xf998, "\x8f\x26\0" },
  { 0xf999, "\x84\xee\0" },
  { 0xf99a, "\x90\x23\0" },
  { 0xf99b, "\x93\x4a\0" },
  { 0xf99c, "\x52\x17\0" },
  { 0xf99d, "\x52\xa3\0" },
  { 0xf99e, "\x54\xbd\0" },
  { 0xf99f, "\x70\xc8\0" },
  { 0xf9a0, "\x88\xc2\0" },
  { 0xf9a1, "\x8a\xaa\0" },
  { 0xf9a2, "\x5e\xc9\0" },
  { 0xf9a3, "\x5f\xf5\0" },
  { 0xf9a4, "\x63\x7b\0" },
  { 0xf9a5, "\x6b\xae\0" },
  { 0xf9a6, "\x7c\x3e\0" },
  { 0xf9a7, "\x73\x75\0" },
  { 0xf9a8, "\x4e\xe4\0" },
  { 0xf9a9, "\x56\xf9\0" },
  { 0xf9aa, "\x5b\xe7\0" },
  { 0xf9ab, "\x5d\xba\0" },
  { 0xf9ac, "\x60\x1c\0" },
  { 0xf9ad, "\x73\xb2\0" },
  { 0xf9ae, "\x74\x69\0" },
  { 0xf9af, "\x7f\x9a\0" },
  { 0xf9b0, "\x80\x46\0" },
  { 0xf9b1, "\x92\x34\0" },
  { 0xf9b2, "\x96\xf6\0" },
  { 0xf9b3, "\x97\x48\0" },
  { 0xf9b4, "\x98\x18\0" },
  { 0xf9b5, "\x4f\x8b\0" },
  { 0xf9b6, "\x79\xae\0" },
  { 0xf9b7, "\x91\xb4\0" },
  { 0xf9b8, "\x96\xb8\0" },
  { 0xf9b9, "\x60\xe1\0" },
  { 0xf9ba, "\x4e\x86\0" },
  { 0xf9bb, "\x50\xda\0" },
  { 0xf9bc, "\x5b\xee\0" },
  { 0xf9bd, "\x5c\x3f\0" },
  { 0xf9be, "\x65\x99\0" },
  { 0xf9bf, "\x6a\x02\0" },
  { 0xf9c0, "\x71\xce\0" },
  { 0xf9c1, "\x76\x42\0" },
  { 0xf9c2, "\x84\xfc\0" },
  { 0xf9c3, "\x90\x7c\0" },
  { 0xf9c4, "\x9f\x8d\0" },
  { 0xf9c5, "\x66\x88\0" },
  { 0xf9c6, "\x96\x2e\0" },
  { 0xf9c7, "\x52\x89\0" },
  { 0xf9c8, "\x67\x7b\0" },
  { 0xf9c9, "\x67\xf3\0" },
  { 0xf9ca, "\x6d\x41\0" },
  { 0xf9cb, "\x6e\x9c\0" },
  { 0xf9cc, "\x74\x09\0" },
  { 0xf9cd, "\x75\x59\0" },
  { 0xf9ce, "\x78\x6b\0" },
  { 0xf9cf, "\x7d\x10\0" },
  { 0xf9d0, "\x98\x5e\0" },
  { 0xf9d1, "\x51\x6d\0" },
  { 0xf9d2, "\x62\x2e\0" },
  { 0xf9d3, "\x96\x78\0" },
  { 0xf9d4, "\x50\x2b\0" },
  { 0xf9d5, "\x5d\x19\0" },
  { 0xf9d6, "\x6d\xea\0" },
  { 0xf9d7, "\x8f\x2a\0" },
  { 0xf9d8, "\x5f\x8b\0" },
  { 0xf9d9, "\x61\x44\0" },
  { 0xf9da, "\x68\x17\0" },
  { 0xf9db, "\x73\x87\0" },
  { 0xf9dc, "\x96\x86\0" },
  { 0xf9dd, "\x52\x29\0" },
  { 0xf9de, "\x54\x0f\0" },
  { 0xf9df, "\x5c\x65\0" },
  { 0xf9e0, "\x66\x13\0" },
  { 0xf9e1, "\x67\x4e\0" },
  { 0xf9e2, "\x68\xa8\0" },
  { 0xf9e3, "\x6c\xe5\0" },
  { 0xf9e4, "\x74\x06\0" },
  { 0xf9e5, "\x75\xe2\0" },
  { 0xf9e6, "\x7f\x79\0" },
  { 0xf9e7, "\x88\xcf\0" },
  { 0xf9e8, "\x88\xe1\0" },
  { 0xf9e9, "\x91\xcc\0" },
  { 0xf9ea, "\x96\xe2\0" },
  { 0xf9eb, "\x53\x3f\0" },
  { 0xf9ec, "\x6e\xba\0" },
  { 0xf9ed, "\x54\x1d\0" },
  { 0xf9ee, "\x71\xd0\0" },
  { 0xf9ef, "\x74\x98\0" },
  { 0xf9f0, "\x85\xfa\0" },
  { 0xf9f1, "\x96\xa3\0" },
  { 0xf9f2, "\x9c\x57\0" },
  { 0xf9f3, "\x9e\x9f\0" },
  { 0xf9f4, "\x67\x97\0" },
  { 0xf9f5, "\x6d\xcb\0" },
  { 0xf9f6, "\x81\xe8\0" },
  { 0xf9f7, "\x7a\xcb\0" },
  { 0xf9f8, "\x7b\x20\0" },
  { 0xf9f9, "\x7c\x92\0" },
  { 0xf9fa, "\x72\xc0\0" },
  { 0xf9fb, "\x70\x99\0" },
  { 0xf9fc, "\x8b\x58\0" },
  { 0xf9fd, "\x4e\xc0\0" },
  { 0xf9fe, "\x83\x36\0" },
  { 0xf9ff, "\x52\x3a\0" },
  { 0xfa00, "\x52\x07\0" },
  { 0xfa01, "\x5e\xa6\0" },
  { 0xfa02, "\x62\xd3\0" },
  { 0xfa03, "\x7c\xd6\0" },
  { 0xfa04, "\x5b\x85\0" },
  { 0xfa05, "\x6d\x1e\0" },
  { 0xfa06, "\x66\xb4\0" },
  { 0xfa07, "\x8f\x3b\0" },
  { 0xfa08, "\x88\x4c\0" },
  { 0xfa09, "\x96\x4d\0" },
  { 0xfa0a, "\x89\x8b\0" },
  { 0xfa0b, "\x5e\xd3\0" },
  { 0xfa0c, "\x51\x40\0" },
  { 0xfa0d, "\x55\xc0\0" },
  { 0xfa10, "\x58\x5a\0" },
  { 0xfa12, "\x66\x74\0" },
  { 0xfa15, "\x51\xde\0" },
  { 0xfa16, "\x73\x2a\0" },
  { 0xfa17, "\x76\xca\0" },
  { 0xfa18, "\x79\x3c\0" },
  { 0xfa19, "\x79\x5e\0" },
  { 0xfa1a, "\x79\x65\0" },
  { 0xfa1b, "\x79\x8f\0" },
  { 0xfa1c, "\x97\x56\0" },
  { 0xfa1d, "\x7c\xbe\0" },
  { 0xfa1e, "\x7f\xbd\0" },
  { 0xfa20, "\x86\x12\0" },
  { 0xfa22, "\x8a\xf8\0" },
  { 0xfa25, "\x90\x38\0" },
  { 0xfa26, "\x90\xfd\0" },
  { 0xfa2a, "\x98\xef\0" },
  { 0xfa2b, "\x98\xfc\0" },
  { 0xfa2c, "\x99\x28\0" },
  { 0xfa2d, "\x9d\xb4\0" },
  { 0xfb1f, "\x05\xf2\x05\xb7\0" },
  { 0xfb2a, "\x05\xe9\x05\xc1\0" },
  { 0xfb2b, "\x05\xe9\x05\xc2\0" },
  { 0xfb2c, "\x05\xe9\x05\xbc\x05\xc1\0" },
  { 0xfb2d, "\x05\xe9\x05\xbc\x05\xc2\0" },
  { 0xfb2e, "\x05\xd0\x05\xb7\0" },
  { 0xfb2f, "\x05\xd0\x05\xb8\0" },
  { 0xfb30, "\x05\xd0\x05\xbc\0" },
  { 0xfb31, "\x05\xd1\x05\xbc\0" },
  { 0xfb32, "\x05\xd2\x05\xbc\0" },
  { 0xfb33, "\x05\xd3\x05\xbc\0" },
  { 0xfb34, "\x05\xd4\x05\xbc\0" },
  { 0xfb35, "\x05\xd5\x05\xbc\0" },
  { 0xfb36, "\x05\xd6\x05\xbc\0" },
  { 0xfb38, "\x05\xd8\x05\xbc\0" },
  { 0xfb39, "\x05\xd9\x05\xbc\0" },
  { 0xfb3a, "\x05\xda\x05\xbc\0" },
  { 0xfb3b, "\x05\xdb\x05\xbc\0" },
  { 0xfb3c, "\x05\xdc\x05\xbc\0" },
  { 0xfb3e, "\x05\xde\x05\xbc\0" },
  { 0xfb40, "\x05\xe0\x05\xbc\0" },
  { 0xfb41, "\x05\xe1\x05\xbc\0" },
  { 0xfb43, "\x05\xe3\x05\xbc\0" },
  { 0xfb44, "\x05\xe4\x05\xbc\0" },
  { 0xfb46, "\x05\xe6\x05\xbc\0" },
  { 0xfb47, "\x05\xe7\x05\xbc\0" },
  { 0xfb48, "\x05\xe8\x05\xbc\0" },
  { 0xfb49, "\x05\xe9\x05\xbc\0" },
  { 0xfb4a, "\x05\xea\x05\xbc\0" },
  { 0xfb4b, "\x05\xd5\x05\xb9\0" },
  { 0xfb4c, "\x05\xd1\x05\xbf\0" },
  { 0xfb4d, "\x05\xdb\x05\xbf\0" },
  { 0xfb4e, "\x05\xe4\x05\xbf\0" }
};

#endif /* DECOMP_H */

/* guniprop.c - Unicode character properties.
 *
 * Copyright (C) 1999 Tom Tromey
 * Copyright (C) 2000 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "glib.h"

#include <config.h>

#include <stddef.h>

#define asize(x)  ((sizeof (x)) / sizeof (x[0]))

#define ATTTABLE(Page, Char) \
  ((attr_table[Page] == 0) ? 0 : (attr_table[Page][Char]))

/* We cheat a bit and cast type values to (char *).  We detect these
   using the &0xff trick.  */
#define TTYPE(Page, Char) \
  (((((int) type_table[Page]) & 0xff) == ((int) type_table[Page])) \
   ? ((int) (type_table[Page])) \
   : (type_table[Page][Char]))

#define TYPE(Char) (((Char) > (G_UNICODE_LAST_CHAR)) ? G_UNICODE_UNASSIGNED : TTYPE ((Char) >> 8, (Char) & 0xff))

#define ISDIGIT(Type) ((Type) == G_UNICODE_DECIMAL_NUMBER \
		       || (Type) == G_UNICODE_LETTER_NUMBER \
		       || (Type) == G_UNICODE_OTHER_NUMBER)

#define ISALPHA(Type) ((Type) == G_UNICODE_LOWERCASE_LETTER \
		       || (Type) == G_UNICODE_UPPERCASE_LETTER \
		       || (Type) == G_UNICODE_TITLECASE_LETTER \
		       || (Type) == G_UNICODE_MODIFIER_LETTER \
		       || (Type) == G_UNICODE_OTHER_LETTER)

gboolean
g_unichar_isalnum (gunichar c)
{
  int t = TYPE (c);
  return ISDIGIT (t) || ISALPHA (t);
}

gboolean
g_unichar_isalpha (gunichar c)
{
  int t = TYPE (c);
  return ISALPHA (t);
}

gboolean
g_unichar_iscntrl (gunichar c)
{
  return TYPE (c) == G_UNICODE_CONTROL;
}

gboolean
g_unichar_isdigit (gunichar c)
{
  return TYPE (c) == G_UNICODE_DECIMAL_NUMBER;
}

gboolean
g_unichar_isgraph (gunichar c)
{
  int t = TYPE (c);
  return (t != G_UNICODE_CONTROL
	  && t != G_UNICODE_FORMAT
	  && t != G_UNICODE_UNASSIGNED
	  && t != G_UNICODE_PRIVATE_USE
	  && t != G_UNICODE_SURROGATE
	  && t != G_UNICODE_SPACE_SEPARATOR);
}

gboolean
g_unichar_islower (gunichar c)
{
  return TYPE (c) == G_UNICODE_LOWERCASE_LETTER;
}

gboolean
g_unichar_isprint (gunichar c)
{
  int t = TYPE (c);
  return (t != G_UNICODE_CONTROL
	  && t != G_UNICODE_FORMAT
	  && t != G_UNICODE_UNASSIGNED
	  && t != G_UNICODE_PRIVATE_USE
	  && t != G_UNICODE_SURROGATE);
}

gboolean
g_unichar_ispunct (gunichar c)
{
  int t = TYPE (c);
  return (t == G_UNICODE_CONNECT_PUNCTUATION || t == G_UNICODE_DASH_PUNCTUATION
	  || t == G_UNICODE_CLOSE_PUNCTUATION || t == G_UNICODE_FINAL_PUNCTUATION
	  || t == G_UNICODE_INITIAL_PUNCTUATION || t == G_UNICODE_OTHER_PUNCTUATION
	  || t == G_UNICODE_OPEN_PUNCTUATION);
}

gboolean
g_unichar_isspace (gunichar c)
{
  /* special-case these since Unicode thinks they are not spaces */
  if (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
      c == '\f' || c == '\v') /* "the mythical vertical tab" */
    return TRUE;
  else
    {
      int t = TYPE (c);
      return (t == G_UNICODE_SPACE_SEPARATOR || t == G_UNICODE_LINE_SEPARATOR
              || t == G_UNICODE_PARAGRAPH_SEPARATOR);
    }
}

/**
 * g_unichar_isupper:
 * @c: a unicode character
 * 
 * Determines if a character is uppercase.
 * 
 * Return value: 
 **/
gboolean
g_unichar_isupper (gunichar c)
{
  return TYPE (c) == G_UNICODE_UPPERCASE_LETTER;
}

/**
 * g_unichar_istitle:
 * @c: a unicode character
 * 
 * Determines if a character is titlecase. Some characters in
 * Unicode which are composites, such as the DZ digraph
 * have three case variants instead of just two. The titlecase
 * form is used at the beginning of a word where only the
 * first letter is capitalized. The titlecase form of the DZ
 * digraph is U+01F2 LATIN CAPITAL LETTTER D WITH SMALL LETTER Z
 * 
 * Return value: %TRUE if the character is titlecase.
 **/
gboolean
g_unichar_istitle (gunichar c)
{
  unsigned int i;
  for (i = 0; i < asize (title_table); ++i)
    if (title_table[i][0] == c)
      return 1;
  return 0;
}

/**
 * g_unichar_isxdigit:
 * @c: a unicode character.
 * 
 * Determines if a characters is a hexidecimal digit
 * 
 * Return value: %TRUE if the character is a hexidecimal digit.
 **/
gboolean
g_unichar_isxdigit (gunichar c)
{
  int t = TYPE (c);
  return ((c >= 'a' && c <= 'f')
	  || (c >= 'A' && c <= 'F')
	  || ISDIGIT (t));
}

/**
 * g_unichar_isdefined:
 * @c: a unicode character
 * 
 * Determines if a given character is assigned in the Unicode
 * standard
 *
 * Return value: %TRUE if the character has an assigned value.
 **/
gboolean
g_unichar_isdefined (gunichar c)
{
  int t = TYPE (c);
  return t != G_UNICODE_UNASSIGNED;
}

/**
 * g_unichar_iswide:
 * @c: a unicode character
 * 
 * Determines if a character is typically rendered in a double-width
 * cell.
 * 
 * Return value: %TRUE if the character is wide.
 **/
/* This function stolen from Markus Kuhn <Markus.Kuhn@cl.cam.ac.uk>.  */
gboolean
g_unichar_iswide (gunichar c)
{
  if (c < 0x1100)
    return 0;

  return ((c >= 0x1100 && c <= 0x115f)	   /* Hangul Jamo */
	  || (c >= 0x2e80 && c <= 0xa4cf && (c & ~0x0011) != 0x300a &&
	      c != 0x303f)		   /* CJK ... Yi */
	  || (c >= 0xac00 && c <= 0xd7a3)  /* Hangul Syllables */
	  || (c >= 0xf900 && c <= 0xfaff)  /* CJK Compatibility Ideographs */
	  || (c >= 0xfe30 && c <= 0xfe6f)  /* CJK Compatibility Forms */
	  || (c >= 0xff00 && c <= 0xff5f)  /* Fullwidth Forms */
	  || (c >= 0xffe0 && c <= 0xffe6));
}

/**
 * g_unichar_toupper:
 * @c: a unicode character
 * 
 * Convert a character to uppercase.
 * 
 * Return value: the result of converting @c to uppercase.
 *               If @c is not an lowercase or titlecase character,
 *               @c is returned unchanged.
 **/
gunichar
g_unichar_toupper (gunichar c)
{
  int t = TYPE (c);
  if (t == G_UNICODE_LOWERCASE_LETTER)
    return ATTTABLE (c >> 8, c & 0xff);
  else if (t == G_UNICODE_TITLECASE_LETTER)
    {
      unsigned int i;
      for (i = 0; i < asize (title_table); ++i)
	{
	  if (title_table[i][0] == c)
	    return title_table[i][1];
	}
    }
  return c;
}

/**
 * g_unichar_tolower:
 * @c: a unicode character.
 * 
 * Convert a character to lower case
 * 
e * Return value: the result of converting @c to lower case.
 *               If @c is not an upperlower or titlecase character,
 *               @c is returned unchanged.
 **/
gunichar
g_unichar_tolower (gunichar c)
{
  int t = TYPE (c);
  if (t == G_UNICODE_UPPERCASE_LETTER)
    return ATTTABLE (c >> 8, c & 0xff);
  else if (t == G_UNICODE_TITLECASE_LETTER)
    {
      unsigned int i;
      for (i = 0; i < asize (title_table); ++i)
	{
	  if (title_table[i][0] == c)
	    return title_table[i][2];
	}
    }
  return c;
}

/**
 * g_unichar_totitle:
 * @c: a unicode character
 * 
 * Convert a character to the titlecase
 * 
 * Return value: the result of converting @c to titlecase.
 *               If @c is not an uppercase or lowercase character,
 *               @c is returned unchanged.
 **/
gunichar
g_unichar_totitle (gunichar c)
{
  unsigned int i;
  for (i = 0; i < asize (title_table); ++i)
    {
      if (title_table[i][0] == c || title_table[i][1] == c
	  || title_table[i][2] == c)
	return title_table[i][0];
    }
  return (TYPE (c) == G_UNICODE_LOWERCASE_LETTER
	  ? ATTTABLE (c >> 8, c & 0xff)
	  : c);
}

/**
 * g_unichar_xdigit_value:
 * @c: a unicode character
 *
 * Determines the numeric value of a character as a decimal
 * degital.
 *
 * Return value: If @c is a decimal digit (according to
 * `g_unichar_isdigit'), its numeric value. Otherwise, -1.
 **/
int
g_unichar_digit_value (gunichar c)
{
  if (TYPE (c) == G_UNICODE_DECIMAL_NUMBER)
    return ATTTABLE (c >> 8, c & 0xff);
  return -1;
}

/**
 * g_unichar_xdigit_value:
 * @c: a unicode character
 *
 * Determines the numeric value of a character as a hexidecimal
 * degital.
 *
 * Return value: If @c is a hex digit (according to
 * `g_unichar_isxdigit'), its numeric value. Otherwise, -1.
 **/
int
g_unichar_xdigit_value (gunichar c)
{
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 1;
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 1;
  if (TYPE (c) == G_UNICODE_DECIMAL_NUMBER)
    return ATTTABLE (c >> 8, c & 0xff);
  return -1;
}

/**
 * g_unichar_type:
 * @c: a unicode character
 * 
 * Classifies a unicode character by type.
 * 
 * Return value: the typ of the character.
 **/
GUnicodeType
g_unichar_type (gunichar c)
{
  return TYPE (c);
}

/* decomp.c - Character decomposition.
 *
 *  Copyright (C) 1999, 2000 Tom Tromey
 *  Copyright 2000 Red Hat, Inc.
 *
 * The Gnome Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * The Gnome Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the Gnome Library; see the file COPYING.LIB.  If not,
 * write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *   Boston, MA 02111-1307, USA.
 */

#include "glib.h"

#include <config.h>

#include <stdlib.h>

/* We cheat a bit and cast type values to (char *).  We detect these
   using the &0xff trick.  */
#define CC(Page, Char) \
  (((((int) (combining_class_table[Page])) & 0xff) \
    == ((int) combining_class_table[Page])) \
   ? ((int) combining_class_table[Page]) \
   : (combining_class_table[Page][Char]))

#define COMBINING_CLASS(Char) \
     (((Char) > (UNICODE_LAST_CHAR)) ? 0 : CC((Char) >> 8, (Char) & 0xff))

/* Compute the canonical ordering of a string in-place.  */
void
g_unicode_canonical_ordering (gunichar *string,
			      size_t len)
{
  size_t i;
  int swap = 1;

  while (swap)
    {
      int last;
      swap = 0;
      last = COMBINING_CLASS (string[0]);
      for (i = 0; i < len - 1; ++i)
	{
	  int next = COMBINING_CLASS (string[i + 1]);
	  if (next != 0 && last > next)
	    {
	      size_t j;
	      /* Percolate item leftward through string.  */
	      for (j = i; j > 0; --j)
		{
		  gunichar t;
		  if (COMBINING_CLASS (string[j]) <= next)
		    break;
		  t = string[j + 1];
		  string[j + 1] = string[j];
		  string[j] = t;
		  swap = 1;
		}
	      /* We're re-entering the loop looking at the old
		 character again.  */
	      next = last;
	    }
	  last = next;
	}
    }
}

gunichar *
g_unicode_canonical_decomposition (gunichar ch,
				   size_t *result_len)
{
  gunichar *r = NULL;

  if (ch <= 0xffff)
    {
      int start = 0;
      int end = G_N_ELEMENTS (decomp_table);
      while (start != end)
	{
	  int half = (start + end) / 2;
	  if (ch == decomp_table[half].ch)
	    {
	      /* Found it.  */
	      int i, len;
	      /* We store as a double-nul terminated string.  */
	      for (len = 0; (decomp_table[half].expansion[len]
			     || decomp_table[half].expansion[len + 1]);
		   len += 2)
		;

	      /* We've counted twice as many bytes as there are
		 characters.  */
	      *result_len = len / 2;
	      r = malloc (len / 2 * sizeof (gunichar));

	      for (i = 0; i < len; i += 2)
		{
		  r[i / 2] = (decomp_table[half].expansion[i] << 8
			      | decomp_table[half].expansion[i + 1]);
		}
	      break;
	    }
	  else if (ch > decomp_table[half].ch)
	    start = half;
	  else
	    end = half;
	}
    }

  if (r == NULL)
    {
      /* Not in our table.  */
      r = malloc (sizeof (gunichar));
      *r = ch;
      *result_len = 1;
    }

  /* Supposedly following the Unicode 2.1.9 table means that the
     decompositions come out in canonical order.  I haven't tested
     this, but we rely on it here.  */
  return r;
}

/* gutf8.c - Operations on UTF-8 strings.
 *
 * Copyright (C) 1999 Tom Tromey
 * Copyright (C) 2000 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <config.h>

#include <stdlib.h>
#ifdef HAVE_CODESET
#include <langinfo.h>
#endif
#include <string.h>

#include <iconv.h>
#include <errno.h>

#include "glib.h"

#define UTF8_COMPUTE(Char, Mask, Len)					      \
  if (Char < 128)							      \
    {									      \
      Len = 1;								      \
      Mask = 0x7f;							      \
    }									      \
  else if ((Char & 0xe0) == 0xc0)					      \
    {									      \
      Len = 2;								      \
      Mask = 0x1f;							      \
    }									      \
  else if ((Char & 0xf0) == 0xe0)					      \
    {									      \
      Len = 3;								      \
      Mask = 0x0f;							      \
    }									      \
  else if ((Char & 0xf8) == 0xf0)					      \
    {									      \
      Len = 4;								      \
      Mask = 0x07;							      \
    }									      \
  else if ((Char & 0xfc) == 0xf8)					      \
    {									      \
      Len = 5;								      \
      Mask = 0x03;							      \
    }									      \
  else if ((Char & 0xfe) == 0xfc)					      \
    {									      \
      Len = 6;								      \
      Mask = 0x01;							      \
    }									      \
  else									      \
    Len = -1;

#define UTF8_GET(Result, Chars, Count, Mask, Len)			      \
  (Result) = (Chars)[0] & (Mask);					      \
  for ((Count) = 1; (Count) < (Len); ++(Count))				      \
    {									      \
      if (((Chars)[(Count)] & 0xc0) != 0x80)				      \
	{								      \
	  (Result) = -1;						      \
	  break;							      \
	}								      \
      (Result) <<= 6;							      \
      (Result) |= ((Chars)[(Count)] & 0x3f);				      \
    }
gchar g_utf8_skip[256] = {
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,6,6,0,0
};

/**
 * g_utf8_find_prev_char:
 * @str: pointer to the beginning of a UTF-8 string
 * @p: pointer to some position within @str
 * 
 * Given a position @p with a UTF-8 encoded string @str, find the start
 * of the previous UTF-8 character starting before @p. Returns %NULL if no
 * UTF-8 characters are present in @p before @str.
 *
 * @p does not have to be at the beginning of a UTF-8 chracter. No check
 * is made to see if the character found is actually valid other than
 * it starts with an appropriate byte.
 *
 * Return value: a pointer to the found character or %NULL.
 **/
gchar *
g_utf8_find_prev_char (const char *str,
		       const char *p)
{
  for (--p; p > str; --p)
    {
      if ((*p & 0xc0) != 0x80)
	return (gchar *)p;
    }
  return NULL;
}

/**
 * g_utf8_find_next_char:
 * @p: a pointer to a position within a UTF-8 encoded string
 * @end: a pointer to the end of the string, or %NULL to indicate
 *        that the string is NULL terminated, in which case
 *        the returned value will be 
 *
 * Find the start of the next utf-8 character in the string after @p
 *
 * @p does not have to be at the beginning of a UTF-8 chracter. No check
 * is made to see if the character found is actually valid other than
 * it starts with an appropriate byte.
 * 
 * Return value: a pointer to the found character or %NULL
 **/
gchar *
g_utf8_find_next_char (const gchar *p,
		       const gchar *end)
{
  if (*p)
    {
      if (end)
	for (++p; p < end && (*p & 0xc0) == 0x80; ++p)
	  ;
      else
	for (++p; (*p & 0xc0) == 0x80; ++p)
	  ;
    }
  return (p == end) ? NULL : (gchar *)p;
}

/**
 * g_utf8_prev_char:
 * @p: a pointer to a position within a UTF-8 encoded string
 *
 * Find the previous UTF-8 character in the string before @p
 *
 * @p does not have to be at the beginning of a UTF-8 character. No check
 * is made to see if the character found is actually valid other than
 * it starts with an appropriate byte. If @p might be the first
 * character of the string, you must use g_utf8_find_prev_char instead.
 * 
 * Return value: a pointer to the found character.
 **/
gchar *
g_utf8_prev_char (const gchar *p)
{
  while (TRUE)
    {
      p--;
      if ((*p & 0xc0) != 0x80)
	return (gchar *)p;
    }
}

/**
 * g_utf8_strlen:
 * @p: pointer to the start of a UTF-8 string.
 * @max: the maximum number of bytes to examine. If @max
 *       is less than 0, then the string is assumed to be
 *       nul-terminated.
 * 
 * Return value: the length of the string in characters
 */
gint
g_utf8_strlen (const gchar *p, gint max)
{
  int len = 0;
  const gchar *start = p;
  /* special case for the empty string */
  if (!*p) 
    return 0;
  /* Note that the test here and the test in the loop differ subtly.
     In the loop we want to see if we've passed the maximum limit --
     for instance if the buffer ends mid-character.  Here at the top
     of the loop we want to see if we've just reached the last byte.  */
  while (max < 0 || p - start < max)
    {
      p = g_utf8_next_char (p);
      ++len;
      if (! *p || (max > 0 && p - start > max))
	break;
    }
  return len;
}

/**
 * g_utf8_get_char:
 * @p: a pointer to unicode character encoded as UTF-8
 * 
 * Convert a sequence of bytes encoded as UTF-8 to a unicode character.
 * 
 * Return value: the resulting character or (gunichar)-1 if @p does
 *               not point to a valid UTF-8 encoded unicode character
 **/
gunichar
g_utf8_get_char (const gchar *p)
{
  int i, mask = 0, len;
  gunichar result;
  unsigned char c = (unsigned char) *p;

  UTF8_COMPUTE (c, mask, len);
  if (len == -1)
    return (gunichar)-1;
  UTF8_GET (result, p, i, mask, len);

  return result;
}

/**
 * g_utf8_offset_to_pointer:
 * @str: a UTF-8 encoded string
 * @offset: a character offset within the string.
 * 
 * Converts from an integer character offset to a pointer to a position
 * within the string.
 * 
 * Return value: the resulting pointer
 **/
gchar *
g_utf8_offset_to_pointer  (const gchar *str,
			   gint         offset)
{
  const gchar *s = str;
  while (offset--)
    s = g_utf8_next_char (s);
  
  return (gchar *)s;
}

/**
 * g_utf8_pointer_to_offset:
 * @str: a UTF-8 encoded string
 * @pos: a pointer to a position within @str
 * 
 * Converts from a pointer to position within a string to a integer
 * character offset
 * 
 * Return value: the resulting character offset
 **/
gint
g_utf8_pointer_to_offset (const gchar *str,
			  const gchar *pos)
{
  const gchar *s = str;
  gint offset = 0;
  
  while (s < pos)
    {
      s = g_utf8_next_char (s);
      offset++;
    }

  return offset;
}


gchar *
g_utf8_strncpy (gchar *dest, const gchar *src, size_t n)
{
  const gchar *s = src;
  while (n && *s)
    {
      s = g_utf8_next_char(s);
      n--;
    }
  strncpy(dest, src, s - src);
  dest[s - src] = 0;
  return dest;
}

static gboolean
g_utf8_get_charset_internal (char **a)
{
  char *charset = getenv("CHARSET");

  if (charset && a && ! *a)
    *a = charset;

  if (charset && strstr (charset, "UTF-8"))
      return TRUE;

#ifdef HAVE_CODESET
  charset = nl_langinfo(CODESET);
  if (charset)
    {
      if (a && ! *a)
	*a = charset;
      if (strcmp (charset, "UTF-8") == 0)
	return TRUE;
    }
#endif
  
#if 0 /* #ifdef _NL_CTYPE_CODESET_NAME */
  charset = nl_langinfo (_NL_CTYPE_CODESET_NAME);
  if (charset)
    {
      if (a && ! *a)
	*a = charset;
      if (strcmp (charset, "UTF-8") == 0)
	return TRUE;
    }
#endif

  if (a && ! *a) 
    *a = "US-ASCII";
  /* Assume this for compatibility at present.  */
  return FALSE;
}

static int utf8_locale_cache = -1;
static char *utf8_charset_cache = NULL;

gboolean
g_get_charset (char **charset) 
{
  if (utf8_locale_cache != -1)
    {
      if (charset)
	*charset = utf8_charset_cache;
      return utf8_locale_cache;
    }
  utf8_locale_cache = g_utf8_get_charset_internal (&utf8_charset_cache);
  if (charset) 
    *charset = utf8_charset_cache;
  return utf8_locale_cache;
}

/* unicode_strchr */

/**
 * g_unichar_to_utf8:
 * @ch: a ISO10646 character code
 * @out: output buffer, must have at least 6 bytes of space.
 *       If %NULL, the length will be computed and returned
 *       and nothing will be written to @out.
 * 
 * Convert a single character to utf8
 * 
 * Return value: number of bytes written
 **/
int
g_unichar_to_utf8 (gunichar c, gchar *outbuf)
{
  size_t len = 0;
  int first;
  int i;

  if (c < 0x80)
    {
      first = 0;
      len = 1;
    }
  else if (c < 0x800)
    {
      first = 0xc0;
      len = 2;
    }
  else if (c < 0x10000)
    {
      first = 0xe0;
      len = 3;
    }
   else if (c < 0x200000)
    {
      first = 0xf0;
      len = 4;
    }
  else if (c < 0x4000000)
    {
      first = 0xf8;
      len = 5;
    }
  else
    {
      first = 0xfc;
      len = 6;
    }

  if (outbuf)
    {
      for (i = len - 1; i > 0; --i)
	{
	  outbuf[i] = (c & 0x3f) | 0x80;
	  c >>= 6;
	}
      outbuf[0] = c | first;
    }

  return len;
}

/**
 * g_utf8_strchr:
 * @p: a nul-terminated utf-8 string
 * @c: a iso-10646 character/
 * 
 * Find the leftmost occurence of the given iso-10646 character
 * in a UTF-8 string.
 * 
 * Return value: NULL if the string does not contain the character, otherwise, a
 *               a pointer to the start of the leftmost of the character in the string.
 **/
gchar *
g_utf8_strchr (const char *p, gunichar c)
{
  gchar ch[10];

  gint len = g_unichar_to_utf8 (c, ch);
  ch[len] = '\0';
  
  return strstr(p, ch);
}

#if 0
/**
 * g_utf8_strrchr:
 * @p: a nul-terminated utf-8 string
 * @c: a iso-10646 character/
 * 
 * Find the rightmost occurence of the given iso-10646 character
 * in a UTF-8 string.
 * 
 * Return value: NULL if the string does not contain the character, otherwise, a
 *               a pointer to the start of the rightmost of the character in the string.
 **/

/* This is ifdefed out atm as there is no strrstr function in libc.
 */
gchar *
unicode_strrchr (const char *p, gunichar c)
{
  gchar ch[10];

  len = g_unichar_to_utf8 (c, ch);
  ch[len] = '\0';
  
  return strrstr(p, ch);
}
#endif


/**
 * g_utf8_to_ucs4:
 * @str: a UTF-8 encoded strnig
 * @len: the length of @
 * 
 * Convert a string from UTF-8 to a 32-bit fixed width
 * representation as UCS-4.
 * 
 * Return value: a pointer to a newly allocated UCS-4 string.
 *               This value must be freed with g_free()
 **/
gunichar *
g_utf8_to_ucs4 (const char *str, int len)
{
  gunichar *result;
  gint n_chars, i;
  const gchar *p;
  
  n_chars = g_utf8_strlen (str, len);
  result = g_new (gunichar, n_chars);
  
  p = str;
  for (i=0; i < n_chars; i++)
    {
      result[i] = g_utf8_get_char (p);
      p = g_utf8_next_char (p);
    }

  return result;
}

gboolean
g_utf8_validate (const gchar  *str,
                 gint          max_len,
                 const gchar **end)
{

  const gchar *p;
  gboolean retval = TRUE;
  
  if (end)
    *end = str;
  
  p = str;
  
  while ((max_len < 0 || (p - str) < max_len) && *p)
    {
      int i, mask = 0, len;
      gunichar result;
      unsigned char c = (unsigned char) *p;
      
      UTF8_COMPUTE (c, mask, len);

      if (len == -1)
        {
          retval = FALSE;
          break;
        }

      /* check that the expected number of bytes exists in str */
      if (max_len >= 0 &&
          ((max_len - (p - str)) < len))
        {
          retval = FALSE;
          break;
        }
        
      UTF8_GET (result, p, i, mask, len);

      if (result == (gunichar)-1)
        {
          retval = FALSE;
          break;
        }
      
      p += len;

      if (end)
        *end = p;
    }
  
  return retval;
}

/* iconv_open() etc. are not thread safe */
G_LOCK_DEFINE_STATIC (iconv_lock);

gchar*
g_convert (const gchar *str,
           gint         len,
           const gchar *to_codeset,
           const gchar *from_codeset,
           gint        *bytes_converted,
           gint        *bytes_written)
{
  gchar *dest;
  gchar *outp;
  const gchar *p;
  size_t inbytes_remaining;
  size_t outbytes_remaining;
  size_t err;
  iconv_t cd;
  size_t outbuf_size;
  
  g_return_val_if_fail (str != NULL, NULL);
  g_return_val_if_fail (to_codeset != NULL, NULL);
  g_return_val_if_fail (from_codeset != NULL, NULL);

  G_LOCK (iconv_lock);
  
  cd = iconv_open (to_codeset, from_codeset);

  if (cd == (iconv_t) -1)
    {
      /* Something went wrong.  */
      if (errno == EINVAL)
        ; /* don't warn; just return NULL with bytes_converted of 0 */
      else
        g_warning ("Failed to convert character set `%s' to `%s': %s",
                   from_codeset, to_codeset, strerror (errno));

      if (bytes_converted)
        *bytes_converted = 0;

      G_UNLOCK (iconv_lock);
      
      return NULL;
    }

  if (len < 0)
    len = strlen (str);

  p = str;
  inbytes_remaining = len;
  outbuf_size = len + 1; /* + 1 for nul in case len == 1 */
  outbytes_remaining = outbuf_size - 1; /* -1 for nul */
  outp = dest = g_malloc (outbuf_size);

 again:
  
  err = iconv (cd, (gchar**)&p,
               &inbytes_remaining, &outp, &outbytes_remaining);

  if (err == (size_t) -1)
    {
      if (errno == E2BIG)
        {
          size_t used = outp - dest;
          outbuf_size *= 2;
          dest = g_realloc (dest, outbuf_size);

          outp = dest + used;
          outbytes_remaining = outbuf_size - used - 1; /* -1 for nul */

          goto again;
        }
      
      /* On any other error, we just give up. */
    }

  *outp = '\0';
  
  if (iconv_close (cd) != 0)
    g_warning ("Failed to close iconv() conversion descriptor: %s",
               strerror (errno));

  if (bytes_converted)
    *bytes_converted = p - str;

  if (bytes_written)
    *bytes_written = outp - dest; /* doesn't include '\0' */
  
  G_UNLOCK (iconv_lock);
  
  if (p == str)
    {
      g_free (dest);
      return NULL;
    }
  else
    return dest;
}


/*************************************************************/


GQuark
g_file_error_quark ()
{
  static GQuark q = 0;
  if (q == 0)
    q = g_quark_from_static_string ("g-file-error-quark");

  return q;
}

static GFileError
errno_to_g_file_error (gint en)
{
  switch (en)
    {
    case EEXIST:
      return G_FILE_ERROR_EXIST;
      break;

    case EISDIR:
      return G_FILE_ERROR_ISDIR;
      break;

    case EACCES:
      return G_FILE_ERROR_ACCES;
      break;

    case ENAMETOOLONG:
      return G_FILE_ERROR_NAMETOOLONG;
      break;

    case ENOENT:
      return G_FILE_ERROR_NOENT;
      break;

    case ENOTDIR:
      return G_FILE_ERROR_NOTDIR;
      break;

    case ENXIO:
      return G_FILE_ERROR_NXIO;
      break;

    case ENODEV:
      return G_FILE_ERROR_NODEV;
      break;

    case EROFS:
      return G_FILE_ERROR_ROFS;
      break;

    case ETXTBSY:
      return G_FILE_ERROR_TXTBSY;
      break;

    case EFAULT:
      return G_FILE_ERROR_FAULT;
      break;

    case ELOOP:
      return G_FILE_ERROR_LOOP;
      break;

    case ENOSPC:
      return G_FILE_ERROR_NOSPC;
      break;

    case ENOMEM:
      return G_FILE_ERROR_NOMEM;
      break;

    case EMFILE:
      return G_FILE_ERROR_MFILE;
      break;

    case ENFILE:
      return G_FILE_ERROR_NFILE;
      break;

    default:
      return G_FILE_ERROR_FAILED;
      break;
    }

  return G_FILE_ERROR_FAILED;
}

gchar*
g_file_get_contents (const gchar *filename,
                     GError     **error)
{
  FILE *f;
  gchar buf[1024];
  size_t bytes;
  GString *str;
  gchar *retval;
  
  g_return_val_if_fail (filename != NULL, NULL);
  
  f = fopen (filename, "r");

  if (f == NULL)
    {
      g_set_error (error,
                   G_FILE_ERROR,
                   errno_to_g_file_error (errno),
                   _("Failed to open file '%s': %s"),
                   filename, strerror (errno));

      return NULL;
    }

  str = g_string_new ("");
  
  while (!feof (f))
    {
      bytes = fread (buf, 1, 1024, f);
      
      if (ferror (f))
        {
          g_set_error (error,
                       G_FILE_ERROR,
                       errno_to_g_file_error (errno),
                       _("Error reading file '%s': %s"),
                       filename, strerror (errno));

          g_string_free (str, TRUE);
          
          return NULL;
        }

      g_string_append_len (str, buf, bytes);
    }

  fclose (f);

  retval = str->str;
  g_string_free (str, FALSE);

  return retval;
}




