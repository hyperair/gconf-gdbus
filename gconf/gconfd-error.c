
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

#include "gconfd-error.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <unistd.h>
#include "GConf.h"

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

/*
 * Error handling
 */

static gchar* last_details = NULL;
static GConfErrNo last_errno = GCONF_SUCCESS;

void         
gconf_clear_error(void)
{
  if (last_details)
    {
      g_free(last_details);
      last_details = NULL;
    }
  last_errno = GCONF_SUCCESS;
}

void
gconf_set_error(GConfErrNo en, const gchar* fmt, ...)
{
  gchar* details;
  va_list args;

  if (last_details != NULL)
    g_free(last_details);
    
  va_start (args, fmt);
  details = g_strdup_vprintf(fmt, args);
  va_end (args);

  last_details = g_strconcat(gconf_strerror(en), ":\n ", details, NULL);

  last_errno = en;

  g_free(details);
}

const gchar* 
gconf_error          (void)
{
  return last_details ? last_details : _("No error");
}

GConfErrNo   
gconf_errno          (void)
{
  return last_errno;
}
