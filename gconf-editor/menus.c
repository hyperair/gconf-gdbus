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
#include "menus.h"
#include "app.h"

static void nothing_cb(GtkWidget* widget, gpointer data);
static void new_app_cb(GtkWidget* widget, gpointer data);
static void close_cb  (GtkWidget* widget, gpointer data);
static void exit_cb   (GtkWidget* widget, gpointer data);
static void about_cb  (GtkWidget* widget, gpointer data);

static GnomeUIInfo file_menu [] = {
  GNOMEUIINFO_MENU_NEW_ITEM(N_("_New Editor"),
                            N_("Edit a different configuration source"),
                            new_app_cb, NULL),

  GNOMEUIINFO_MENU_CLOSE_ITEM(close_cb, NULL),

  GNOMEUIINFO_MENU_EXIT_ITEM(exit_cb, NULL),

  GNOMEUIINFO_END
};

static GnomeUIInfo edit_menu [] = {
  GNOMEUIINFO_MENU_CUT_ITEM(nothing_cb, NULL), 
  GNOMEUIINFO_MENU_COPY_ITEM(nothing_cb, NULL),
  GNOMEUIINFO_MENU_PASTE_ITEM(nothing_cb, NULL),
  GNOMEUIINFO_MENU_CLEAR_ITEM(nothing_cb, NULL),
  GNOMEUIINFO_MENU_UNDO_ITEM(nothing_cb, NULL),
  GNOMEUIINFO_MENU_REDO_ITEM(nothing_cb, NULL), 
  GNOMEUIINFO_MENU_FIND_ITEM(nothing_cb, NULL), 
  GNOMEUIINFO_MENU_FIND_AGAIN_ITEM(nothing_cb, NULL),
  GNOMEUIINFO_MENU_REPLACE_ITEM(nothing_cb, NULL),
  GNOMEUIINFO_END
};

static GnomeUIInfo help_menu [] = {
  GNOMEUIINFO_HELP ("gconf-editor"),
  
  GNOMEUIINFO_MENU_ABOUT_ITEM(about_cb, NULL),
  
  GNOMEUIINFO_END
};

static GnomeUIInfo menu [] = {
  GNOMEUIINFO_MENU_FILE_TREE(file_menu),
  GNOMEUIINFO_MENU_EDIT_TREE(edit_menu),
  GNOMEUIINFO_MENU_HELP_TREE(help_menu),
  GNOMEUIINFO_END
};

static GnomeUIInfo toolbar [] = {
  GNOMEUIINFO_ITEM_STOCK (N_("Cut"), N_("Cut some text to the clipboard"), nothing_cb, GNOME_STOCK_PIXMAP_CUT),

  GNOMEUIINFO_END
};


void 
gce_install_menus_and_toolbar(GtkWidget* app)
{
  gnome_app_create_toolbar_with_data(GNOME_APP(app), toolbar, app);
  gnome_app_create_menus_with_data(GNOME_APP(app), menu, app);
  gnome_app_install_menu_hints(GNOME_APP(app), menu);  
}


static void 
nothing_cb(GtkWidget* widget, gpointer data)
{
  GtkWidget* dialog;
  GtkWidget* app;
  
  app = (GtkWidget*) data;

  dialog = gnome_ok_dialog(_("This feature isn't implemented yet! We're working on it."));

  gnome_dialog_set_parent(GNOME_DIALOG(dialog), GTK_WINDOW(app));
}

static void 
new_app_cb(GtkWidget* widget, gpointer data)
{
  GtkWidget* app;

  app = gce_app_new(NULL);

  gtk_widget_show(app);
}

static void 
close_cb(GtkWidget* widget, gpointer data)
{
  GtkWidget* app;

  app = (GtkWidget*) data;

  gce_app_close(app);
}

static void 
exit_cb(GtkWidget* widget, gpointer data)
{
  gtk_main_quit();
}

static void 
about_cb(GtkWidget* widget, gpointer data)
{
  static GtkWidget* dialog = NULL;
  GtkWidget* app;

  app = (GtkWidget*) data;

  if (dialog != NULL) 
    {
      g_assert(GTK_WIDGET_REALIZED(dialog));
      gdk_window_show(dialog->window);
      gdk_window_raise(dialog->window);
    }
  else
    {        
      const gchar *authors[] = {
        "Havoc Pennington <hp@pobox.com>",
        NULL
      };

      gchar* logo = NULL; /* gnome_pixmap_file("gnome-hello-logo.png"); */

      dialog = gnome_about_new (_("Configuration Editor"), VERSION,
                                "(C) 1999 Red Hat, Inc.",
                                authors,
                                _("Used to edit values in the configuration database."),
                                logo);

      g_free(logo);

      gtk_signal_connect(GTK_OBJECT(dialog),
                         "destroy",
                         GTK_SIGNAL_FUNC(gtk_widget_destroyed),
                         &dialog);

      gnome_dialog_set_parent(GNOME_DIALOG(dialog), GTK_WINDOW(app));

      gtk_widget_show(dialog);
    }
}


