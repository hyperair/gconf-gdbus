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
#include "menus.h"
#include <gconf.h>

typedef struct _AppInfo AppInfo;

struct _AppInfo {
  GConfEngine* conf;
  GtkWidget* tree;
  GtkWidget* details;
  GtkWidget* key_label;  
};

static AppInfo* appinfo_new(void);
static void     appinfo_destroy(AppInfo* ai);

/* Keep a list of all open application windows */
static GSList* app_list = NULL;

static gint delete_event_cb(GtkWidget* w, GdkEventAny* e, gpointer data);

GtkWidget* 
gce_app_new(const gchar* geometry)
{
  GtkWidget* app;
  GtkWidget* status;
  GtkWidget* paned;
  AppInfo* ai;

  gchar* titles[3];

  titles[0] = _("Key");
  titles[1] = _("Value");
  titles[2] = NULL;

  app = gnome_app_new(PACKAGE, _("Configuration Editor"));

  ai = appinfo_new();

  gtk_object_set_data_full(GTK_OBJECT(app), "ai",
                           ai, (GtkDestroyNotify)appinfo_destroy);

  gtk_window_set_policy(GTK_WINDOW(app), FALSE, TRUE, FALSE);
  gtk_window_set_default_size(GTK_WINDOW(app), 350, 400);
  gtk_window_set_wmclass(GTK_WINDOW(app), "gconfedit", "GConfEdit");

  paned = gtk_hpaned_new();

  gnome_app_set_contents(GNOME_APP(app), paned);

  status = gnome_appbar_new(FALSE, TRUE, GNOME_PREFERENCES_NEVER);

  gnome_app_set_statusbar(GNOME_APP(app), status);

  gce_install_menus_and_toolbar(app);

  ai->tree = gtk_ctree_new_with_titles(2, 1, titles);

  gtk_paned_add1(GTK_PANED(paned), ai->tree);

  gtk_container_set_border_width(GTK_CONTAINER(ai->tree),
                                 GNOME_PAD);

  ai->details = gtk_frame_new(NULL);

  gtk_paned_add2(GTK_PANED(paned), ai->details);

  gtk_container_set_border_width(GTK_CONTAINER(ai->details),
                                 GNOME_PAD);

  gtk_signal_connect(GTK_OBJECT(app),
                     "delete_event",
                     GTK_SIGNAL_FUNC(delete_event_cb),
                     NULL);

  gtk_paned_set_position(GTK_PANED(paned), 200);

  if (geometry != NULL) 
    {
      gint x, y, w, h;
      if ( gnome_parse_geometry( geometry, 
                                 &x, &y, &w, &h ) ) 
        {
          if (x != -1)
            {
              gtk_widget_set_uposition(app, x, y);
            }

          if (w != -1) 
            {
              gtk_window_set_default_size(GTK_WINDOW(app), w, h);
            }
        }
      else 
        {
          gnome_warning_dialog_parented(_("Could not parse geometry string"), GTK_WINDOW(app));
        }
    }

  app_list = g_slist_prepend(app_list, app);

  /* Setup config stuff */

  ai->conf = g_conf_engine_new();

  /* Show widgets */

  gtk_widget_show_all(paned);
  gtk_widget_show(status);

  return app;
}

void       
gce_app_close(GtkWidget* app)
{
  g_return_if_fail(GNOME_IS_APP(app));

  app_list = g_slist_remove(app_list, app);

  gtk_widget_destroy(app);

  if (app_list == NULL)
    {
      /* No windows remaining */
      if (gtk_main_level() > 0)
        gtk_main_quit();
    }
}

static gint 
delete_event_cb(GtkWidget* window, GdkEventAny* e, gpointer data)
{
  gce_app_close(window);

  /* Prevent the window's destruction, since we destroyed it 
   * ourselves with hello_app_close()
   */
  return TRUE;
}


static AppInfo* 
appinfo_new(void)
{
  AppInfo* ai;

  ai = g_new0(AppInfo, 1);

  return ai;
}

static void     
appinfo_destroy(AppInfo* ai)
{

  g_free(ai);
}

