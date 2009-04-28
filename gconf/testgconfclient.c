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
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <config.h>
#include "gconf-client.h"

#include <gtk/gtk.h>

static void create_controls(GConfClient* client);

int
main(int argc, char** argv)
{
  GError* error = NULL;
  GConfClient* client = NULL;

  gtk_init(&argc, &argv);
  
  if (!gconf_init(argc, argv, &error))
    {
      g_assert(error != NULL);
      g_warning("GConf init failed:\n  %s", error->message);
      return 1;
    }

  g_assert(error == NULL);

  client = gconf_client_get_default();

  gconf_client_add_dir(client, "/apps/gnome/testgconfclient", GCONF_CLIENT_PRELOAD_NONE, NULL);

  create_controls(client);
  
  gtk_main();

  g_object_unref(G_OBJECT(client));
  
  return 0;
}

static void
entry_notify_func(GConfClient* client, guint cnxn_id,
                  GConfEntry *gconf_entry,
                  gpointer user_data)
{
  GtkWidget* entry = user_data;
  
  g_return_if_fail(GTK_IS_ENTRY(entry));

  g_signal_handler_block_matched(entry, G_SIGNAL_MATCH_DATA, 0, 0,
                                 NULL, NULL, client);
  gtk_entry_set_text(GTK_ENTRY(entry),
                     gconf_value_get_string(gconf_entry->value));
  g_signal_handler_unblock_matched(entry, G_SIGNAL_MATCH_DATA, 0, 0,
                                   NULL, NULL, client);
}

static void
entry_destroyed_callback(GtkWidget* entry, gpointer data)
{
  GConfClient* client = data;

  guint notify_id = GPOINTER_TO_UINT(g_object_get_data(entry, "notify_id"));

  gconf_client_notify_remove(client, notify_id);
}

static void
entry_changed_callback(GtkWidget* entry, gpointer data)
{
  GConfClient* client = data;
  const gchar* key;
  gchar* text;

  text = gtk_editable_get_chars(GTK_EDITABLE(entry), 0, -1);
  key = g_object_get_data(G_OBJECT(entry), "key");
  
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

  g_object_set_data_full(G_OBJECT(entry), "key",
                         g_strdup(key), g_free);


  val = gconf_client_get_string(client, key, NULL);

  gtk_entry_set_text(GTK_ENTRY(entry), val ? val : "");

  g_free(val);
  
  notify_id = gconf_client_notify_add(client, key, entry_notify_func, entry, NULL, NULL);

  g_object_set_data(G_OBJECT(entry), "notify_id",
                    GUINT_TO_POINTER(notify_id));
  
  g_signal_connect(G_OBJECT(entry), "changed",
                   G_CALLBACK(entry_changed_callback),
                   client);

  g_signal_connect(G_OBJECT(entry), "destroy",
                   G_CALLBACK(entry_destroyed_callback),
                   client);
  
  hbox = gtk_hbox_new(FALSE, 10);

  label = gtk_label_new(key);

  gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
  gtk_box_pack_end(GTK_BOX(hbox), entry, FALSE, FALSE, 0);

  /* Set sensitive according to whether the key is writable or not. */
  gtk_widget_set_sensitive (entry,
                            gconf_client_key_is_writable (client,
                                                          key, NULL));
  
  return hbox;
}

static void
destroy_callback(GtkWidget* win, gpointer data)
{
  GConfClient* client = data;

  g_object_unref(G_OBJECT(client));
}

static void
quit_callback(GtkWidget* button, gpointer data)
{
  GtkWidget* win = data;

  gtk_widget_destroy(win);

  gtk_main_quit();
}

static void
addsub_callback(GtkWidget* button, gpointer data)
{
  GtkWidget* win = data;
  GConfClient* client = g_object_get_data(G_OBJECT(win), "client");
  GtkWidget* label = g_object_get_data(G_OBJECT(win), "label");
  int subdir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(win), "subdir"));
  int maindir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(win), "maindir"));
  char *s;

  subdir++;

  g_object_set_data(G_OBJECT(win), "subdir", GINT_TO_POINTER(subdir));

  gconf_client_add_dir(client, "/apps/gnome/testgconfclient/subdir", GCONF_CLIENT_PRELOAD_NONE, NULL);

  s = g_strdup_printf("Maindir added %d times\nSubdir added %d times", maindir, subdir);
  gtk_label_set_text(GTK_LABEL(label), s);
  g_free(s);
}

static void
removesub_callback(GtkWidget* button, gpointer data)
{
  GtkWidget* win = data;
  GConfClient* client = g_object_get_data(G_OBJECT(win), "client");
  GtkWidget* label = g_object_get_data(G_OBJECT(win), "label");
  int subdir = GPOINTER_TO_INT(g_object_get_data(GTK_OBJECT(win), "subdir"));
  int maindir = GPOINTER_TO_INT(g_object_get_data(GTK_OBJECT(win), "maindir"));
  char *s;

  subdir--;

  g_object_set_data(G_OBJECT(win), "subdir", GINT_TO_POINTER(subdir));

  gconf_client_remove_dir(client, "/apps/gnome/testgconfclient/subdir", NULL);

  s = g_strdup_printf("Maindir added %d times\nSubdir added %d times", maindir, subdir);
  gtk_label_set_text(GTK_LABEL(label), s);
  g_free(s);
}

static void
addmain_callback(GtkWidget* button, gpointer data)
{
  GtkWidget* win = data;
  GConfClient* client = g_object_get_data(G_OBJECT(win), "client");
  GtkWidget* label = g_object_get_data(G_OBJECT(win), "label");
  int subdir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(win), "subdir"));
  int maindir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(win), "maindir"));
  char *s;

  maindir++;

  g_object_set_data(G_OBJECT(win), "maindir", GINT_TO_POINTER(maindir));

  gconf_client_add_dir(client, "/apps/gnome/testgconfclient", GCONF_CLIENT_PRELOAD_NONE, NULL);

  s = g_strdup_printf("Maindir added %d times\nSubdir added %d times", maindir, subdir);
  gtk_label_set_text(GTK_LABEL(label), s);
  g_free(s);
}
  
static void
removemain_callback(GtkWidget* button, gpointer data)
{
  GtkWidget* win = data;
  GConfClient* client = g_object_get_data(G_OBJECT(win), "client");
  GtkWidget* label = g_object_get_data(G_OBJECT(win), "label");
  int subdir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(win), "subdir"));
  int maindir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(win), "maindir"));
  char *s;

  maindir--;

  g_object_set_data(G_OBJECT(win), "maindir", GINT_TO_POINTER(maindir));

  gconf_client_remove_dir(client, "/apps/gnome/testgconfclient", NULL);

  s = g_strdup_printf("Maindir added %d times\nSubdir added %d times", maindir, subdir);
  gtk_label_set_text(GTK_LABEL(label), s);
  g_free(s);
}

static void
create_controls(GConfClient* client)
{
  GtkWidget* win;
  GtkWidget* label;
  GtkWidget* vbox;
  GtkWidget* button;
  GtkWidget* entry;
  
  /* Reference held by the window */

  g_object_ref(G_OBJECT(client));

  win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  
  g_object_set_data(G_OBJECT(win), "client", client);

  g_signal_connect(G_OBJECT(win), "destroy",
                   G_CALLBACK(destroy_callback), client);

  vbox = gtk_vbox_new(FALSE, 10);

  gtk_container_add(GTK_CONTAINER(win), vbox);

  label = gtk_label_new("Maindir added 1 times\nSubdir added 0 times");
  gtk_box_pack_end(GTK_BOX(vbox), label, FALSE, FALSE, 0);
  g_object_set_data(G_OBJECT(win), "label", label);
  g_object_set_data(G_OBJECT(win), "subdir", GINT_TO_POINTER(0));
  g_object_set_data(G_OBJECT(win), "maindir", GINT_TO_POINTER(1));

  button = gtk_button_new_with_label("Quit");
  g_signal_connect(G_OBJECT(button), "clicked",
                   G_CALLBACK(quit_callback), win);
  gtk_box_pack_end(GTK_BOX(vbox), button, FALSE, FALSE, 0);
  
  button = gtk_button_new_with_label("Remove subdir");
  g_signal_connect(G_OBJECT(button), "clicked",
                   G_CALLBACK(removesub_callback), win);
  gtk_box_pack_end(GTK_BOX(vbox), button, FALSE, FALSE, 0);

  button = gtk_button_new_with_label("Add subdir");
  g_signal_connect(G_OBJECT(button), "clicked",
                   G_CALLBACK(addsub_callback), win);
  gtk_box_pack_end(GTK_BOX(vbox), button, FALSE, FALSE, 0);

  button = gtk_button_new_with_label("Remove maindir");
  g_signal_connect(G_OBJECT(button), "clicked",
                   G_CALLBACK(removemain_callback), win);
  gtk_box_pack_end(GTK_BOX(vbox), button, FALSE, FALSE, 0);

  button = gtk_button_new_with_label("Add maindir");
  g_signal_connect(G_OBJECT(button), "clicked",
                   G_CALLBACK(addmain_callback), win);
  gtk_box_pack_end(GTK_BOX(vbox), button, FALSE, FALSE, 0);

  entry = entry_attached_to(client, "/apps/gnome/testgconfclient/blah");
  gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 0);

  entry = entry_attached_to(client, "/apps/gnome/testgconfclient/foo");
  gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 0);

  entry = entry_attached_to(client, "/apps/gnome/testgconfclient/bar");
  gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 0);

  entry = entry_attached_to(client, "/apps/gnome/testgconfclient/subdir/testsub1");
  gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 0);

  entry = entry_attached_to(client, "/apps/gnome/testgconfclient/subdir/testsub2");
  gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 0);
  
  gtk_widget_show_all(win);
}


