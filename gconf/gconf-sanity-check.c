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

#include "gconf.h"
#include "gconf-internals.h"
#include <gtk/gtk.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <popt.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>

struct poptOption options[] = {
  { 
    NULL, 
    '\0', 
    POPT_ARG_INCLUDE_TABLE, 
    poptHelpOptions,
    0, 
    N_("Help options"), 
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

static gboolean ensure_gtk (void);
static void     show_fatal_error_dialog (const char *format,
                                         ...) G_GNUC_PRINTF (1, 2);
static gboolean check_file_locking (void);
static gboolean check_gconf (void);

int 
main (int argc, char** argv)
{
  poptContext ctx;
  gint nextopt;
  GError* err = NULL;
  
  ctx = poptGetContext ("gconf-sanity-check-2", argc, (const char **) argv, options, 0);

  poptReadDefaultConfig (ctx, TRUE);

  while ((nextopt = poptGetNextOpt(ctx)) > 0)
    /*nothing*/;

  if (nextopt != -1) 
    {
      g_printerr (_("Error on option %s: %s.\nRun '%s --help' to see a full list of available command line options.\n"),
                  poptBadOption(ctx, 0),
                  poptStrerror(nextopt),
                  argv[0]);
      return 1;
    }

  poptFreeContext (ctx);

  if (!check_file_locking ())
    return 1;

  if (!check_gconf ())
    return 1;
  
  return 0;
}

/* Your basic Stevens cut-and-paste */
static int
lock_reg (int fd, int cmd, int type, off_t offset, int whence, off_t len)
{
  struct flock lock;

  lock.l_type = type; /* F_RDLCK, F_WRLCK, F_UNLCK */
  lock.l_start = offset; /* byte offset relative to whence */
  lock.l_whence = whence; /* SEEK_SET, SEEK_CUR, SEEK_END */
  lock.l_len = len; /* #bytes, 0 for eof */

  return fcntl (fd, cmd, &lock);
}

#define lock_entire_file(fd) \
  lock_reg ((fd), F_SETLK, F_WRLCK, 0, SEEK_SET, 0)
#define unlock_entire_file(fd) \
  lock_reg ((fd), F_SETLK, F_UNLCK, 0, SEEK_SET, 0)

static gboolean
check_file_locking (void)
{
  char *testfile;
  int fd;
  gboolean retval;

  testfile = g_build_filename (g_get_home_dir (),
                               ".gconf-test-locking-file",
                               NULL);
  
  retval = FALSE;

  /* keep the open from failing due to non-writable old file or something */
  unlink (testfile);
  
  fd = open (testfile, O_WRONLY | O_CREAT, 0700);

  if (fd < 0)
    {      
      show_fatal_error_dialog (_("Please contact your system administrator to resolve the following problem:\n"
                                 "Could not open or create the file \"%s\"; this indicates "
                                 "that there may be a problem with your configuration, "
                                 "as many programs will need to create files in your "
                                 "home directory. The error was \"%s\" (errno = %d)."),
                               testfile, strerror (errno), errno);

      goto out;
    }

  if (lock_entire_file (fd) < 0)
    {      
      show_fatal_error_dialog (_("Please contact your system administrator to resolve the following problem:\n"
                                 "Could not lock the file \"%s\"; this indicates "
                                 "that there may be a problem with your operating system "
                                 "configuration. If you have an NFS-mounted home directory, "
                                 "either the client or the server may be set up incorrectly. "
                                 "See the rpc.statd and rpc.lockd documentation. "
                                 "The error was \"%s\" (errno = %d)."),
                               testfile, strerror (errno), errno); 
      goto out;
    }

  retval = TRUE;

 out:
  close (fd);
  if (unlink (testfile) < 0)
    g_printerr (_("Can't remove file %s: %s\n"), testfile, strerror (errno));
  g_free (testfile);
  
  return retval;
}

static gboolean
check_gconf (void)
{
  GConfEngine *conf;
  
  /* FIXME the main thing is to try to guess at stale locks caused
   * by kernel crashes, and detect hosed config files.
   */
  
  return TRUE;
}

static void
show_fatal_error_dialog (const char *format,
                         ...)
{
  GtkWidget *d;
  char *str;
  va_list args;

  va_start (args, format);
  str = g_strdup_vprintf (format, args);
  va_end (args);

  if (!ensure_gtk ())
    {
      g_printerr ("%s", str);
      return;
    }
  
  d = gtk_message_dialog_new (NULL, 0,
                              GTK_MESSAGE_ERROR,
                              GTK_BUTTONS_CLOSE,
                              "%s", str);

  g_free (str);
  
  gtk_dialog_run (GTK_DIALOG (d));

  gtk_widget_destroy (d);
}

/* this is because setting up gtk is kind of slow, no point doing it
 * if we don't need an error dialog.
 */
static gboolean
ensure_gtk (void)
{
  static gboolean done_init = FALSE;  
  static gboolean ok = FALSE;
  
  if (!done_init)
    {
      ok = gtk_init_check (0, NULL);
      done_init = TRUE;
    }
  
  return ok;
}
