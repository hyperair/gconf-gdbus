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
