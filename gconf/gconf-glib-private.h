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

#ifndef GCONF_GCONF_GLIB_PRIVATE_H
#define GCONF_GCONF_GLIB_PRIVATE_H

#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "gconf-glib-public.h"


typedef enum
{
  G_MARKUP_NODE_ELEMENT,
  G_MARKUP_NODE_TEXT,
  G_MARKUP_NODE_PASSTHROUGH
} GMarkupNodeType;

typedef enum
{
  G_MARKUP_PRESERVE_ALL_WHITESPACE = 1 << 0
  
} GMarkupParseFlags;

typedef enum
{
  G_MARKUP_NO_FORMATTING = 1 << 0

} GMarkupToStringFlags;

typedef union _GMarkupNode GMarkupNode;
typedef struct _GMarkupNodeText GMarkupNodeText;
typedef struct _GMarkupNodeElement GMarkupNodeElement;

struct _GMarkupNodeText
{
  GMarkupNodeType type;
  
  gchar *text;
};

struct _GMarkupNodeElement
{
  GMarkupNodeType type;

  gchar *name;
  
  GList *children;

  /* List members are an opaque datatype, so ignore this. */
  GList *attributes;
};

union _GMarkupNode
{
  GMarkupNodeType type;

  GMarkupNodeText text;
  GMarkupNodeElement element;  
};

typedef enum
{
  G_MARKUP_ERROR_BAD_UTF8,
  G_MARKUP_ERROR_EMPTY,
  G_MARKUP_ERROR_PARSE
} GMarkupErrorType;

#define G_MARKUP_ERROR g_markup_error_quark ()

GQuark g_markup_error_quark ();

GMarkupNodeText* g_markup_node_new_text (const gchar *text);
GMarkupNodeElement* g_markup_node_new_element (const gchar *name);

void         g_markup_node_free (GMarkupNode *node);

void g_markup_node_set_attribute (GMarkupNodeElement *node,
                                  const gchar *attribute_name,
                                  const gchar *attribute_value);

gchar* g_markup_node_get_attribute (GMarkupNodeElement *node,
                                    const gchar *attribute_name);

void g_markup_node_unset_attribute (GMarkupNodeElement *node,
                                    const gchar *attribute_name);

/* Get array of attribute names/values, otherwise you couldn't get
 * a list of them.
 */
void g_markup_node_get_attributes (GMarkupNodeElement *node,
                                   gchar ***names,
                                   gchar ***values,
                                   gint    *n_attributes);

GMarkupNode *g_markup_node_from_string (const gchar *text,
                                        gint length,
                                        GMarkupParseFlags flags,
                                        GError **error);

gchar *g_markup_node_to_string (GMarkupNode *node,
                                GMarkupToStringFlags flags);

GList *g_markup_nodes_from_string (const gchar *text,
                                   gint length,
                                   GMarkupParseFlags flags,
                                   GError **error);

gchar *g_markup_nodes_to_string (GList *nodes,
                                 GMarkupToStringFlags flags);


/***************************************************************/

#include <stddef.h>      /* For size_t */

typedef guint32 gunichar;
typedef guint16 gunichar2;

/* These are the possible character classifications.  */
typedef enum {
  G_UNICODE_CONTROL,
  G_UNICODE_FORMAT,
  G_UNICODE_UNASSIGNED,
  G_UNICODE_PRIVATE_USE,
  G_UNICODE_SURROGATE,
  G_UNICODE_LOWERCASE_LETTER,
  G_UNICODE_MODIFIER_LETTER,
  G_UNICODE_OTHER_LETTER,
  G_UNICODE_TITLECASE_LETTER,
  G_UNICODE_UPPERCASE_LETTER,
  G_UNICODE_COMBINING_MARK,
  G_UNICODE_ENCLOSING_MARK,
  G_UNICODE_NON_SPACING_MARK,
  G_UNICODE_DECIMAL_NUMBER,
  G_UNICODE_LETTER_NUMBER,
  G_UNICODE_OTHER_NUMBER,
  G_UNICODE_CONNECT_PUNCTUATION,
  G_UNICODE_DASH_PUNCTUATION,
  G_UNICODE_CLOSE_PUNCTUATION,
  G_UNICODE_FINAL_PUNCTUATION,
  G_UNICODE_INITIAL_PUNCTUATION,
  G_UNICODE_OTHER_PUNCTUATION,
  G_UNICODE_OPEN_PUNCTUATION,
  G_UNICODE_CURRENCY_SYMBOL,
  G_UNICODE_MODIFIER_SYMBOL,
  G_UNICODE_MATH_SYMBOL,
  G_UNICODE_OTHER_SYMBOL,
  G_UNICODE_LINE_SEPARATOR,
  G_UNICODE_PARAGRAPH_SEPARATOR,
  G_UNICODE_SPACE_SEPARATOR
} GUnicodeType;

/* Returns TRUE if current locale uses UTF-8 charset.  If CHARSET is
 * not null, sets *CHARSET to the name of the current locale's
 * charset.  This value is statically allocated.
 */
gboolean g_get_charset (char **charset);

/* These are all analogs of the <ctype.h> functions.
 */
gboolean g_unichar_isalnum   (gunichar c);
gboolean g_unichar_isalpha   (gunichar c);
gboolean g_unichar_iscntrl   (gunichar c);
gboolean g_unichar_isdigit   (gunichar c);
gboolean g_unichar_isgraph   (gunichar c);
gboolean g_unichar_islower   (gunichar c);
gboolean g_unichar_isprint   (gunichar c);
gboolean g_unichar_ispunct   (gunichar c);
gboolean g_unichar_isspace   (gunichar c);
gboolean g_unichar_isupper   (gunichar c);
gboolean g_unichar_isxdigit  (gunichar c);
gboolean g_unichar_istitle   (gunichar c);
gboolean g_unichar_isdefined (gunichar c);
gboolean g_unichar_iswide    (gunichar c);

/* More <ctype.h> functions.  These convert between the three cases.
 * See the Unicode book to understand title case.  */
gunichar g_unichar_toupper (gunichar c);
gunichar g_unichar_tolower (gunichar c);
gunichar g_unichar_totitle (gunichar c);

/* If C is a digit (according to `g_unichar_isdigit'), then return its
   numeric value.  Otherwise return -1.  */
gint g_unichar_digit_value (gunichar c);

gint g_unichar_xdigit_value (gunichar c);

/* Return the Unicode character type of a given character.  */
GUnicodeType g_unichar_type (gunichar c);



/* Compute canonical ordering of a string in-place.  This rearranges
   decomposed characters in the string according to their combining
   classes.  See the Unicode manual for more information.  */
void g_unicode_canonical_ordering (gunichar *string,
				   size_t   len);

/* Compute canonical decomposition of a character.  Returns g_malloc()d
   string of Unicode characters.  RESULT_LEN is set to the resulting
   length of the string.  */
gunichar *g_unicode_canonical_decomposition (gunichar  ch,
					     size_t   *result_len);

/* Array of skip-bytes-per-initial character.
 * We prefix variable declarations so they can
 * properly get exported in windows dlls.
 */
#ifndef GLIB_VAR
#  ifdef G_OS_WIN32
#    ifdef GLIB_COMPILATION
#      define GLIB_VAR __declspec(dllexport)
#    else /* !GLIB_COMPILATION */
#      define GLIB_VAR extern __declspec(dllimport)
#    endif /* !GLIB_COMPILATION */
#  else /* !G_OS_WIN32 */
#    define GLIB_VAR extern
#  endif /* !G_OS_WIN32 */
#endif /* !GLIB_VAR */

GLIB_VAR char g_utf8_skip[256];

#define g_utf8_next_char(p) (char *)((p) + g_utf8_skip[*(guchar *)(p)])

gunichar g_utf8_get_char          (const gchar *p);
gchar *  g_utf8_offset_to_pointer  (const gchar *str,
				    gint         offset);
gint     g_utf8_pointer_to_offset (const gchar *str,
				   const gchar *pos);
gchar *  g_utf8_prev_char         (const gchar *p);
gchar *  g_utf8_find_next_char    (const gchar *p,
				   const gchar *bound);
gchar *  g_utf8_find_prev_char    (const gchar *str,
				   const gchar *p);

gint g_utf8_strlen (const gchar *p,
		    gint         max);

/* Copies n characters from src to dest */
gchar *g_utf8_strncpy (gchar       *dest,
		       const gchar *src,
		       size_t       n);

/* Find the UTF-8 character corresponding to ch, in string p. These
   functions are equivalants to strchr and strrchr */

gchar *g_utf8_strchr  (const gchar *p,
		       gunichar     ch);
gchar *g_utf8_strrchr (const gchar *p,
		       gunichar     ch);

gunichar2 *g_utf8_to_utf16 (const gchar     *str,
			    gint             len);
gunichar * g_utf8_to_ucs4  (const gchar     *str,
			    gint             len);
gunichar * g_utf16_to_ucs4 (const gunichar2 *str,
			    gint             len);
gchar *    g_utf16_to_utf8 (const gunichar2 *str,
			    gint             len);
gunichar * g_ucs4_to_utf16 (const gunichar  *str,
			    gint             len);
gchar *    g_ucs4_to_utf8  (const gunichar  *str,
			    gint             len);

/* Convert a single character into UTF-8. outbuf must have at
 * least 6 bytes of space. Returns the number of bytes in the
 * result.
 */
gint      g_unichar_to_utf8 (gunichar    c,
			     char       *outbuf);

/* Validate a UTF8 string, return TRUE if valid, put pointer to
 * first invalid char in **end
 */

gboolean g_utf8_validate (const gchar  *str,
                          gint          len,
                          const gchar **end);

gchar*   g_convert       (const gchar  *str,
                          gint          len,
                          const gchar  *to_codeset,
                          const gchar  *from_codeset,
                          gint         *bytes_converted,
                          gint         *bytes_written);


/*****************************************/

#define G_FILE_ERROR g_file_error_quark ()

typedef enum
{
  G_FILE_ERROR_EXIST,
  G_FILE_ERROR_ISDIR,
  G_FILE_ERROR_ACCES,
  G_FILE_ERROR_NAMETOOLONG,
  G_FILE_ERROR_NOENT,
  G_FILE_ERROR_NOTDIR,
  G_FILE_ERROR_NXIO,
  G_FILE_ERROR_NODEV,
  G_FILE_ERROR_ROFS,
  G_FILE_ERROR_TXTBSY,
  G_FILE_ERROR_FAULT,
  G_FILE_ERROR_LOOP,
  G_FILE_ERROR_NOSPC,
  G_FILE_ERROR_NOMEM,
  G_FILE_ERROR_MFILE,
  G_FILE_ERROR_NFILE,
  G_FILE_ERROR_FAILED
} GFileError;

GQuark g_file_error_quark ();

gchar*   g_file_get_contents (const gchar *filename,
                              GError     **error);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif



