/* -*- tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* GConf
 * Copyright (C) 1999, 2000, 2000 Red Hat Inc.
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

#include "gconf-client.h"

#include <gtk/gtk.h>

static void create_controls(GConfClient* client);

int
main(int argc, char** argv)
{
  GConfError* error = NULL;
  GConfClient* client = NULL;

  gtk_init(&argc, &argv);
  
  if (!gconf_init(argc, argv, &error))
    {
      g_assert(error != NULL);
      g_warning("GConf init failed:\n  %s", error->str);
      return 1;
    }

  g_assert(error == NULL);

  client = gconf_client_get_default();

  gconf_client_add_dir(client, "/apps/gnome/testgconfclient", GCONF_CLIENT_PRELOAD_NONE, NULL);

  create_controls(client);
  
  gtk_main();

  gtk_object_unref(GTK_OBJECT(client));
  
  return 0;
}

static void
entry_notify_func(GConfClient* client, guint cnxn_id, const gchar* key, GConfValue* value, gboolean is_default, gpointer user_data)
{
  GtkWidget* entry = user_data;
  
  g_return_if_fail(GTK_IS_ENTRY(entry));

  gtk_signal_handler_block_by_data(GTK_OBJECT(entry), client);
  gtk_entry_set_text(GTK_ENTRY(entry), gconf_value_string(value));
  gtk_signal_handler_unblock_by_data(GTK_OBJECT(entry), client);
}

static void
entry_destroyed_callback(GtkWidget* entry, gpointer data)
{
  GConfClient* client = data;

  guint notify_id = GPOINTER_TO_UINT(gtk_object_get_data(GTK_OBJECT(entry),
                                                         "notify_id"));

  gconf_client_notify_remove(client, notify_id);
}

static void
entry_changed_callback(GtkWidget* entry, gpointer data)
{
  GConfClient* client = data;
  const gchar* key;
  gchar* text;

  text = gtk_editable_get_chars(GTK_EDITABLE(entry), 0, -1);
  key = gtk_object_get_data(GTK_OBJECT(entry), "key");
  
  gconf_client_set_string(client, key, text, NULL);

  g_free(text);
}

static GtkWidget*
entry_attached_to(GConfClient* client, const gchar* key)
{
  GtkWidget* entry;
  GtkWidget* hbox;
  GtkWidget* label;
  gchar* val;
  guint notify_id;
  
  entry = gtk_entry_new();

  gtk_object_set_data_full(GTK_OBJECT(entry), "key",
                           g_strdup(key), g_free);


  val = gconf_client_get_string(client, key, NULL);

  gtk_entry_set_text(GTK_ENTRY(entry), val ? val : "");

  g_free(val);
  
  notify_id = gconf_client_notify_add(client, key, entry_notify_func, entry, NULL, NULL);

  gtk_object_set_data(GTK_OBJECT(entry), "notify_id",
                      GUINT_TO_POINTER(notify_id));
  
  gtk_signal_connect(GTK_OBJECT(entry), "changed",
                     GTK_SIGNAL_FUNC(entry_changed_callback),
                     client);

  gtk_signal_connect(GTK_OBJECT(entry), "destroy",
                     GTK_SIGNAL_FUNC(entry_destroyed_callback),
                     client);
  
  hbox = gtk_hbox_new(FALSE, 10);

  label = gtk_label_new(key);

  gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
  gtk_box_pack_end(GTK_BOX(hbox), entry, FALSE, FALSE, 0);

  return hbox;
}

static void
destroy_callback(GtkWidget* win, gpointer data)
{
  GConfClient* client = data;

  gtk_object_unref(GTK_OBJECT(client));
}

static void
quit_callback(GtkWidget* button, gpointer data)
{
  GtkWidget* win = data;

  gtk_widget_destroy(win);

  gtk_main_quit();
}
  

static void
create_controls(GConfClient* client)
{
  GtkWidget* win;
  GtkWidget* vbox;
  GtkWidget* quit_button;
  GtkWidget* entry;
  
  /* Reference held by the window */

  gtk_object_ref(GTK_OBJECT(client));

  win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  
  gtk_object_set_data(GTK_OBJECT(win), "client", client);

  gtk_signal_connect(GTK_OBJECT(win), "destroy",
                     GTK_SIGNAL_FUNC(destroy_callback), client);

  vbox = gtk_vbox_new(FALSE, 10);

  gtk_container_add(GTK_CONTAINER(win), vbox);

  quit_button = gtk_button_new_with_label("Quit");

  gtk_signal_connect(GTK_OBJECT(quit_button), "clicked",
                     GTK_SIGNAL_FUNC(quit_callback), win);
  
  gtk_box_pack_end(GTK_BOX(vbox), quit_button, FALSE, FALSE, 0);
  
  entry = entry_attached_to(client, "/apps/gnome/testgconfclient/blah");
  gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 0);

  entry = entry_attached_to(client, "/apps/gnome/testgconfclient/foo");
  gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 0);

  entry = entry_attached_to(client, "/apps/gnome/testgconfclient/bar");
  gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 0);
  
  gtk_widget_show_all(win);
}


