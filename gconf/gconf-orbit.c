/* GConf
 * Copyright (C) 1999 Red Hat Inc.
 * This file is basically a libgnorba cut-and-paste,
 *  but it relies on a cookie in the user's home dir instead
 *  of the X property (one gconfd per user and home dir)
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

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <time.h>
#include "gconf-internals.h"

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

static CORBA_ORB g_conf_orbit_orb = CORBA_OBJECT_NIL;
static CORBA_Principal g_conf_request_cookie;

static char * g_conf_get_cookie_reliably (const char *setme);
static const char *g_conf_cookie_setup(const char *setme);

static gboolean
orb_handle_connection(GIOChannel *source, GIOCondition cond,
		      GIOPConnection *cnx)
{

  /* The best way to know about an fd exception is if select()/poll()
	 * tells you about it, so we just relay that information on to ORBit
	 * if possible
	 */
	
  if(cond & (G_IO_HUP|G_IO_NVAL|G_IO_ERR))
    giop_main_handle_connection_exception(cnx);
  else
    giop_main_handle_connection(cnx);

  return TRUE;
}

static void
orb_add_connection(GIOPConnection *cnx)
{
  int tag;
  GIOChannel *channel;

  channel = g_io_channel_unix_new(GIOP_CONNECTION_GET_FD(cnx));
  tag = g_io_add_watch_full   (channel, G_PRIORITY_DEFAULT,
			       G_IO_IN|G_IO_ERR|G_IO_HUP|G_IO_NVAL, 
			       (GIOFunc)orb_handle_connection,
			       cnx, NULL);
  g_io_channel_unref (channel);

  cnx->user_data = GUINT_TO_POINTER (tag);
}

static void
orb_remove_connection(GIOPConnection *cnx)
{
  g_source_remove(GPOINTER_TO_UINT (cnx->user_data));
  cnx->user_data = GINT_TO_POINTER (-1);
}

static ORBit_MessageValidationResult
g_conf_ORBit_request_validate(CORBA_unsigned_long request_id,
			     CORBA_Principal *principal,
			     CORBA_char *operation)
{
  if (principal->_length ==  g_conf_request_cookie._length
      && !(principal->_buffer[principal->_length - 1])
      && !strcmp(principal->_buffer, g_conf_request_cookie._buffer))
    return ORBIT_MESSAGE_ALLOW_ALL;
  else
    return ORBIT_MESSAGE_BAD;
}

/*
 * I bet these will require porting sooner or later
 */
static gboolean
get_exclusive_lock (int fd)
{
	/* flock (fd, LOCK_EX); */
	struct flock lbuf;

	lbuf.l_type = F_WRLCK;
	lbuf.l_whence = SEEK_SET;
	lbuf.l_start = lbuf.l_len = 0L; /* Lock the whole file.  */
	if (fcntl (fd, F_SETLKW, &lbuf) < 0)
          {
            if (errno == EINTR)
              if (fcntl(fd, F_SETLKW, &lbuf) == 0) /* try again */
                return TRUE;
            
            g_conf_set_error(G_CONF_FAILED, _("Could not get lock to set up authentication cookie: %s"), strerror(errno));
            return FALSE;
          }
        else
          return TRUE;
}

static void
release_lock (int fd)
{
	/* flock (fd, LOCK_UN); */
	struct flock lbuf;

	lbuf.l_type = F_UNLCK;
	lbuf.l_whence = SEEK_SET;
	lbuf.l_start = lbuf.l_len = 0L; /* Unlock the whole file.  */
	fcntl (fd, F_SETLKW, &lbuf);
}

static char *
g_conf_get_cookie_reliably (const char *setme)
{
  char buf[64];
  char *random_string = NULL;
  char *name;
  int fd = -1;
  gchar* dir;

  dir = g_conf_server_info_dir();

  if (mkdir(dir, 0700) < 0)
    {
      if (errno != EEXIST)
        {
          g_conf_set_error(G_CONF_FAILED, _("Couldn't make directory `%s': %s"),
                           dir, strerror(errno));
          g_free(dir);
          return NULL;
        }
    }
  
  name = g_strconcat (dir, "/cookie", NULL);

  g_free(dir);

  if(setme) {

    /* Just write it into the file for reference purposes */
    fd = open (name, O_CREAT|O_WRONLY, S_IRUSR | S_IWUSR);

    if (fd < 0)
      {
        g_conf_set_error(G_CONF_FAILED, _("Could not open cookie file `%s': %s"),
                         name, strerror(errno));
        goto out;
      }
        
    if (!get_exclusive_lock(fd))
      goto out; /* error is set */
    
    if (write(fd, setme, strlen(setme)) < 0)
      {
        g_conf_set_error(G_CONF_FAILED, _("Could not write cookie to file `%s': %s"),
                         name, strerror(errno));
        release_lock(fd);
        goto out;
      }
    
    release_lock(fd);
    random_string = g_strdup(setme);

  } else {

    buf [sizeof(buf)-1] = '\0';

    /*
     * Create the file exclusively with permissions rw for the
     * user.  if this fails, it means the file already existed
     */
    fd = open (name, O_CREAT|O_EXCL|O_WRONLY, S_IRUSR | S_IWUSR);

    if(fd >= 0) {
      unsigned int i;

      get_exclusive_lock (fd);
      srandom (time (NULL));
      for (i = 0; i < sizeof (buf)-1; i++)
	buf [i] = (random () % (126-33)) + 33;

      if(write(fd, buf, sizeof(buf)-1) < (sizeof(buf)-1))
	goto out;

      release_lock(fd);
    } else if(fd < 0) {
      int i;
      fd = open(name, O_RDONLY);
      if (fd < 0)
        {
          g_conf_set_error(G_CONF_FAILED, _("Failed to open cookie file `%s': %s"),
                           name, strerror(errno));
          goto out;
        }
      i = read(fd, buf, sizeof(buf)-1);
      if(i < 0)
        {
          g_conf_set_error(G_CONF_FAILED, _("Failed to read cookie file `%s': %s"),
                           name, strerror(errno));
          goto out;
        }
      buf[i] = '\0';
    }

    random_string = g_strdup(buf);
  }

 out:
  if(fd >= 0) 
    close(fd);
  g_free(name);

  return random_string;
}

static const char *
g_conf_cookie_setup(const char *setme)
{
  g_conf_request_cookie._buffer = g_conf_get_cookie_reliably (setme);
		
  if (g_conf_request_cookie._buffer == NULL ||
      *g_conf_request_cookie._buffer == '\0')
    return NULL;
		
  g_conf_request_cookie._length = strlen(g_conf_request_cookie._buffer) + 1;

  ORBit_set_request_validation_handler(&g_conf_ORBit_request_validate);
  ORBit_set_default_principal(&g_conf_request_cookie);

  return g_conf_request_cookie._buffer;
}

CORBA_ORB
g_conf_init_orb(int* argc, char** argv)
{
  CORBA_ORB retval;
  CORBA_Environment ev;

  CORBA_exception_init(&ev);

  IIOPAddConnectionHandler = orb_add_connection;
  IIOPRemoveConnectionHandler = orb_remove_connection;

  g_conf_orbit_orb = retval = CORBA_ORB_init(argc, argv, "orbit-local-orb", &ev);
	
  if (ev._major != CORBA_NO_EXCEPTION)
    {
      g_conf_set_error(G_CONF_FAILED, _("Failure initializing ORB: %s"),
                       CORBA_exception_id(&ev));
      CORBA_exception_free(&ev);
      return CORBA_OBJECT_NIL;
    }

  if (g_conf_cookie_setup(NULL) == NULL)
    {
      return CORBA_OBJECT_NIL;
    }
      
  return retval;
}

CORBA_ORB
g_conf_get_orb(void)
{
  return g_conf_orbit_orb;
}

void 
g_conf_set_orb(CORBA_ORB orb)
{
  g_return_if_fail(g_conf_orbit_orb == CORBA_OBJECT_NIL);
  g_return_if_fail(orb != CORBA_OBJECT_NIL);

  g_conf_orbit_orb = orb;
}

