/* 
 * GConf BerkeleyDB back-end
 *
 * Copyright (C) 2000 Sun Microsystems Inc
 * Contributed to the GConf project.
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

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>

#include <gconf/gconf-backend.h>
#include <gconf/gconf-internals.h>
#include <gconf/gconf.h>

/* mode_to_to_mode: copied from xml-dir.c - needs to be a common utility */
guint
mode_t_to_mode(mode_t orig)
{
  guint mode = 0;

  if (orig & S_IRUSR)
    mode |= 0400;

  if (orig & S_IWUSR)
    mode |= 0200;

  if (orig & S_IXUSR)
    mode |= 0100;

  if (orig & S_IRGRP)
    mode |= 0040;

  if (orig & S_IWGRP)
    mode |= 0020;

  if (orig & S_IXGRP)
    mode |= 0010;

  if (orig & S_IROTH)
    mode |= 0004;

  if (orig & S_IWOTH)
    mode |= 0002;

  if (orig & S_IXOTH)
    mode |= 0001;
  
  return mode;
}

static void
free_list_element (gpointer data, gpointer user_data)
{
  g_free (data);
}

/* g_free()s each element's data, then free's the list */
void
_gconf_slist_free_all (GSList * list)
{
  if (!list)
    return;
  g_slist_foreach (list, free_list_element, NULL);
  g_slist_free (list);
}

/* NOTE: body of _gconf_get_root_dir() is taken directly from initial code in
   xml-backend.c:resolve_address() */

/* parses root directory of a file-based GConf database and checks
 * directory existence/writeability/locking
 */
char *
_gconf_get_root_dir (const char *address, guint * pflags,
		     const gchar * dbtype, GError ** err)
{
  gchar *root_dir;
  guint len;
  guint dir_mode = 0700;
  guint file_mode = 0600;
  gint flags = 0;
  GConfLock *lock = NULL;

  root_dir = gconf_address_resource (address);

  if (root_dir == NULL)
    {
      gconf_set_error (err, GCONF_ERROR_BAD_ADDRESS,
		       _
		       ("Couldn't find the %s root directory in the address `%s'"),
		       dbtype, address);
      return NULL;
    }

  /* Chop trailing '/' to canonicalize */
  len = strlen (root_dir);

  if (root_dir[len - 1] == '/')
    root_dir[len - 1] = '\0';

  if (mkdir (root_dir, dir_mode) < 0)
    {
      if (errno != EEXIST)
	{
	  gconf_set_error (err, GCONF_ERROR_FAILED,
			   _("Could not make directory `%s': %s"),
			   (gchar *) root_dir, strerror (errno));
	  g_free (root_dir);
	  return NULL;
	}
      else
	{
	  /* Already exists, base our dir_mode on it */
	  struct stat statbuf;
	  if (stat (root_dir, &statbuf) == 0)
	    {
	      dir_mode = mode_t_to_mode (statbuf.st_mode);
	      /* dir_mode without search bits */
	      file_mode = dir_mode & (~0111);
	    }
	}
    }

  {
    /* See if we're writeable */
    gboolean writeable = FALSE;
    int fd;
    gchar *testfile;

    testfile = g_strconcat (root_dir, "/.testing.writeability", NULL);

    fd = open (testfile, O_CREAT | O_WRONLY, S_IRWXU);

    if (fd >= 0)
      {
	writeable = TRUE;
	close (fd);
      }

    unlink (testfile);

    g_free (testfile);

    if (writeable)
      flags |= GCONF_SOURCE_ALL_WRITEABLE;

    /* We only do locking if it's writeable,
       which is sort of broken but close enough
     */
    if (writeable)
      {
	gchar *lockdir;

	lockdir = gconf_concat_dir_and_key (root_dir, "%gconf-backend.lock");

	lock = gconf_get_lock (lockdir, err);

	if (lock != NULL)
	  gconf_log (GCL_DEBUG, "Acquired %s lock directory `%s'", dbtype,
		     lockdir);

	g_free (lockdir);

	if (lock == NULL)
	  {
	    g_free (root_dir);
	    return NULL;
	  }
      }
  }

  {
    /* see if we're readable */
    gboolean readable = FALSE;
    DIR *d;

    d = opendir (root_dir);

    if (d != NULL)
      {
	readable = TRUE;
	closedir (d);
      }

    if (readable)
      flags |= GCONF_SOURCE_ALL_READABLE;
  }

  if (!(flags & GCONF_SOURCE_ALL_READABLE) &&
      !(flags & GCONF_SOURCE_ALL_WRITEABLE))
    {
      gconf_set_error (err, GCONF_ERROR_BAD_ADDRESS,
		       _
		       ("Can't read from or write to the %s root directory in the address `%s'"),
		       dbtype, address);
      g_free (root_dir);
      return NULL;
    }
  *pflags = flags;

  return root_dir;
}
