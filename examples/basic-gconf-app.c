/* -*- tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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

/* This program demonstrates how to use GConf in a GTK or GNOME
   application.  The key thing is that the main window and the prefs
   dialog have NO KNOWLEDGE of one another as far as configuration
   values are concerned; they don't even have to be in the same
   process. That is, the GConfClient acts as the data "model" for
   configuration information; the main application is a "view" of the
   model; and the prefs dialog is a "controller."
*/

/* Throughout, this program is letting GConfClient use its default error handlers
   rather than checking for errors or attaching custom handlers to the
   "unreturned_error" signal. Thus the last arg to GConfClient functions is NULL.
*/

#include <gconf-client.h>
#include <gtk/gtk.h>

static GtkWidget* create_main_window(GConfClient* client);
static GtkWidget* create_prefs_dialog(GtkWidget* parent, GConfClient* client);

int
main(int argc, char** argv)
{
  GConfError* error = NULL;
  GConfClient* client = NULL;
  GtkWidget* main_window;
  GtkWidget* prefs_dialog;
  
  gtk_init(&argc, &argv);
  
  if (!gconf_init(argc, argv, &error))
    {
      g_assert(error != NULL);
      g_warning("GConf init failed:\n  %s", error->str);
      /* These next two lines would be important if we weren't going to
         exit immediately */
      gconf_error_destroy(error);
      error = NULL;
      return 1;
    }

  g_assert(error == NULL);

  client = gconf_client_new();

  gconf_client_add_dir(client, "/apps/gnome/basic-gconf-app", GCONF_CLIENT_PRELOAD_NONE, NULL);

  /* The main() function takes over the floating object; the code that
     "owns" the object should do this, as with any Gtk object.
     Read about refcounting and destruction at developer.gnome.org/doc/GGAD/ */
  gtk_object_ref(GTK_OBJECT(client));
  gtk_object_sink(GTK_OBJECT(client));

  main_window = create_main_window(client);
  prefs_dialog = create_prefs_dialog(main_window, client);

  gtk_widget_show_all(main_window);
  gtk_widget_show_all(prefs_dialog);
  
  gtk_main();

  /* Shut down the client cleanly. Note the destroy rather than unref,
     so the shutdown occurs even if there are outstanding references.
     If your program isn't exiting you probably want to just plain unref()
     Read about refcounting and destruction at developer.gnome.org/doc/GGAD/ */
  gtk_object_destroy(GTK_OBJECT(client));
  /* Now avoid leaking memory (not that this matters since the program
     is exiting... */
  gtk_object_unref(GTK_OBJECT(client));
  
  return 0;
}

static gint
delete_event_callback(GtkWidget* window, GdkEventAny* event, gpointer data)
{
  gtk_main_quit();

  return TRUE;
}

static GtkWidget*
create_configurable_widget(GConfClient* client, const gchar* config_key)
{
  GtkWidget* frame;
  GtkWidget* label;
  GConfValue* initial;

  frame = gtk_frame_new(config_key);

  label = gtk_label_new("");

  gtk_container_add(GTK_CONTAINER(frame), label);
  
  initial = gconf_client_get(client, config_key, NULL);

  if (initial != NULL)
    {
      gchar* str = gconf_value_to_string(initial);
      gtk_label_set_text(GTK_LABEL(label), str);
      g_free(str);
    }

  return frame;
}

static GtkWidget*
create_main_window(GConfClient* client)
{
  GtkWidget* w;
  GtkWidget* vbox;
  GtkWidget* config;
  
  w = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  vbox = gtk_vbox_new(FALSE, 10);

  gtk_container_add(GTK_CONTAINER(w), vbox);
  
  config = create_configurable_widget(client, "/apps/gnome/basic-gconf-app/foo");
  gtk_box_pack_start(GTK_BOX(vbox), config, FALSE, FALSE, 0);

  config = create_configurable_widget(client, "/apps/gnome/basic-gconf-app/bar");
  gtk_box_pack_start(GTK_BOX(vbox), config, FALSE, FALSE, 0);
  
  config = create_configurable_widget(client, "/apps/gnome/basic-gconf-app/baz");
  gtk_box_pack_start(GTK_BOX(vbox), config, FALSE, FALSE, 0);

  config = create_configurable_widget(client, "/apps/gnome/basic-gconf-app/blah");
  gtk_box_pack_start(GTK_BOX(vbox), config, FALSE, FALSE, 0);

  gtk_signal_connect(GTK_OBJECT(w), "delete_event",
                     GTK_SIGNAL_FUNC(delete_event_callback),
                     NULL);
  
  return w;
}

static GConfChangeSet*
prefs_dialog_get_change_set(GtkWidget* dialog)
{
  return gtk_object_get_data(GTK_OBJECT(dialog), "changeset");
}


static void
prefs_dialog_update_sensitivity(GtkWidget* dialog)
{
  GtkWidget* apply;
  GtkWidget* revert;    
  GConfChangeSet* cs;
  GConfChangeSet* revert_cs;
  
  apply = gtk_object_get_data(GTK_OBJECT(dialog), "apply");
  revert = gtk_object_get_data(GTK_OBJECT(dialog), "revert");

  g_assert(apply != NULL);
  g_assert(revert != NULL);
  
  cs = prefs_dialog_get_change_set(dialog);

  revert_cs = gtk_object_get_data(GTK_OBJECT(dialog), "revert_changeset");

  if (gconf_change_set_size(cs) > 0)
    gtk_widget_set_sensitive(apply, TRUE);
  else
    gtk_widget_set_sensitive(apply, FALSE);

  if (revert_cs != NULL)
    gtk_widget_set_sensitive(revert, TRUE);
  else
    gtk_widget_set_sensitive(revert, FALSE);
}

static void
prefs_dialog_apply(GtkWidget* dialog, gpointer data)
{
  GConfChangeSet* cs;
  GConfChangeSet* revert_cs;
  GConfClient* client;

  client = gtk_object_get_data(GTK_OBJECT(dialog), "client");
  
  cs = gtk_object_get_data(GTK_OBJECT(dialog), "changeset");

  /* apply button shouldn't have been sensitive in this case */
  g_assert(cs != NULL);

  revert_cs = gtk_object_get_data(GTK_OBJECT(dialog), "revert_changeset");

  if (revert_cs != NULL)
    gconf_change_set_unref(revert_cs);

  revert_cs = gconf_client_create_reverse_change_set(client, cs, NULL);
  
  /* if revert_cs == NULL there was an error; that is OK in this
     code, the default error handler should display a dialog
  */
  
  gtk_object_set_data(GTK_OBJECT(dialog), "revert_changeset", revert_cs);

  /* again, relying on default error handler. The third argument here
     is whether to remove the successfully-committed items from the
     change set; here we remove the already-committed stuff, so if the
     user resolves the error, they can bang on "apply" some more and
     commit the remaining items. The return value indicates whether an
     error occurred. */
  gconf_client_commit_change_set(client, cs, TRUE, NULL);
  
  prefs_dialog_update_sensitivity(dialog);
}

static void
prefs_dialog_revert(GtkWidget* dialog, gpointer data)
{
  GConfChangeSet* cs;
  GConfChangeSet* revert_cs;
  GConfClient* client;

  client = gtk_object_get_data(GTK_OBJECT(dialog), "client");
  
  cs = gtk_object_get_data(GTK_OBJECT(dialog), "changeset");

  /* When reverting, you want to discard any pending changes so
     "apply" won't do anything */
  gconf_change_set_clear(cs);
  
  revert_cs = gtk_object_get_data(GTK_OBJECT(dialog), "revert_changeset");

  if (revert_cs != NULL)
    {
      gconf_client_commit_change_set(client, revert_cs, FALSE, NULL);
      
      gconf_change_set_unref(revert_cs);

      /* Set the revert changeset to NULL */  
      gtk_object_set_data(GTK_OBJECT(dialog), "revert_changeset", NULL);
    }
  
  prefs_dialog_update_sensitivity(dialog);
}

static GtkWidget*
create_prefs_dialog(GtkWidget* parent, GConfClient* client)
{
  GConfChangeSet* cs;
  GtkWidget* dialog;
  GtkWidget* bbox;
  GtkWidget* apply;
  GtkWidget* revert;
  GtkWidget* vbox_outer;
  GtkWidget* vbox_inner;
  
  dialog = gtk_dialog_new();

  apply = gtk_button_new_with_label("Apply");
  revert = gtk_button_new_with_label("Revert");

  gtk_object_set_data(GTK_OBJECT(dialog), "apply", apply);
  gtk_object_set_data(GTK_OBJECT(dialog), "revert", revert);
  
  bbox = gtk_hbutton_box_new();

  vbox_outer = gtk_vbox_new(FALSE, 10);

  vbox_inner = gtk_vbox_new(FALSE, 10);
  
  gtk_container_add(GTK_CONTAINER(dialog), vbox_outer);

  gtk_box_pack_start(GTK_BOX(vbox_outer), vbox_inner, TRUE, TRUE, 0);
  gtk_box_pack_end(GTK_BOX(vbox_outer), bbox, FALSE, FALSE, 0);

  gtk_container_add(GTK_CONTAINER(bbox), apply);
  gtk_container_add(GTK_CONTAINER(bbox), revert);
  
  cs = gconf_change_set_new();
  
  gtk_object_set_data(GTK_OBJECT(dialog), "changeset", cs);
  gtk_object_set_data(GTK_OBJECT(dialog), "client", client);
  
  gtk_signal_connect(GTK_OBJECT(dialog), "delete_event",
                     GTK_SIGNAL_FUNC(delete_event_callback),
                     NULL);

  prefs_dialog_update_sensitivity(dialog);

  gtk_signal_connect(GTK_OBJECT(apply), "clicked",
                     GTK_SIGNAL_FUNC(prefs_dialog_apply),
                     NULL);

  gtk_signal_connect(GTK_OBJECT(apply), "clicked",
                     GTK_SIGNAL_FUNC(prefs_dialog_revert),
                     NULL);

  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  
  return dialog;
}

