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

#include <config.h>
#include <gnome.h>
#include "app.h"
#include <gconf.h>

static gint session_die(GnomeClient* client, gpointer client_data);

static gint save_session(GnomeClient *client, gint phase, 
                         GnomeSaveStyle save_style,
                         gint is_shutdown, GnomeInteractStyle interact_style,
                         gint is_fast, gpointer client_data);

static char* geometry = NULL;

struct poptOption options[] = {
  { 
    "geometry",
    '\0',
    POPT_ARG_STRING,
    &geometry,
    0,
    N_("Specify the geometry of the main window"),
    N_("GEOMETRY")
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

int 
main(int argc, char** argv)
{
  GtkWidget* app;  
  poptContext pctx;
  char** args;
  GnomeClient* client;
  GConfError* err = NULL;
  
  bindtextdomain(PACKAGE, GNOMELOCALEDIR);  
  textdomain(PACKAGE);

  if (!gconf_init(argc, argv, &err))
    {
      fprintf(stderr, _("Failed to init GConf: %s\n"), err->str);
      gconf_error_destroy(err);
      return 1;
    }

  gnome_init_with_popt_table(PACKAGE, VERSION, argc, argv, 
                             options, 0, &pctx);  

  poptFreeContext(pctx);

  client = gnome_master_client ();

  gtk_signal_connect (GTK_OBJECT (client), "save_yourself",
                      GTK_SIGNAL_FUNC (save_session), argv[0]);

  gtk_signal_connect (GTK_OBJECT (client), "die",
                      GTK_SIGNAL_FUNC (session_die), NULL);
  

  app = gce_app_new(geometry);

  gtk_widget_show(app);

  gnome_warning_dialog_parented("This application hasn't been written yet",
                                GTK_WINDOW(app));
  
  gtk_main();

  g_assert(gtk_main_level() == 0);

  return 0;
}

static gint
save_session (GnomeClient *client, gint phase, GnomeSaveStyle save_style,
              gint is_shutdown, GnomeInteractStyle interact_style,
              gint is_fast, gpointer client_data)
{
  gchar** argv;
  guint argc;

  /* allocate 0-filled, so it will be NULL-terminated */
  argv = g_malloc0(sizeof(gchar*)*4);
  argc = 1;

  argv[0] = client_data;
  
  gnome_client_set_clone_command (client, argc, argv);
  gnome_client_set_restart_command (client, argc, argv);

  return TRUE;
}

static gint 
session_die(GnomeClient* client, gpointer client_data)
{
  gtk_main_quit ();
  return TRUE;
}
