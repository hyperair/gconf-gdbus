/* -*- tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* GConf
 * Copyright (C) 1999, 2000 Red Hat Inc.
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

/* Throughout, this program is letting GConfClient use its default
   error handlers rather than checking for errors or attaching custom
   handlers to the "unreturned_error" signal. Thus the last arg to
   GConfClient functions is NULL.
*/

/* A word about Apply/Revert/OK/Cancel. These should work as follows:
    - "Apply" installs the currently selected settings
    - "OK" installs the currently selected settings, and closes the dialog
    - "Cancel" reverts, and closes the dialog
    - "Revert" reverts to the settings that were in effect when the dialog
       was opened; it also resets the prefs dialog contents to those settings.
*/

#include <gconf-client.h> /* Once GConf is installed, this should be
                             <gconf/gconf-client.h> */
#include <gtk/gtk.h>

static GtkWidget* create_main_window(GConfClient* client);
static GtkWidget* create_prefs_dialog(GtkWidget* parent, GConfClient* client);

int
main(int argc, char** argv)
{
  GError* error = NULL;
  GConfClient* client = NULL;
  GtkWidget* main_window;
  
  gtk_init(&argc, &argv);

  /* If you pass NULL for error, then gconf_init() will simply print
     to stderr and exit if an error occurs. */
  if (!gconf_init(argc, argv, &error))
    {
      g_assert(error != NULL);
      g_warning("GConf init failed:\n  %s", error->message);
      /* These next two lines would be important if we weren't going to
         exit immediately */
      g_error_free(error);
      error = NULL;
      return 1;
    }

  g_assert(error == NULL);

  client = gconf_client_get_default();

  gconf_client_add_dir(client, "/apps/basic-gconf-app", GCONF_CLIENT_PRELOAD_NONE, NULL);

  main_window = create_main_window(client);
  
  gtk_widget_show_all(main_window);
  
  gtk_main();

  /* This ensures we cleanly detach from the GConf server (assuming
   * we hold the last reference)
   */
  gtk_object_unref(GTK_OBJECT(client));
  
  return 0;
}

static gint
delete_event_callback(GtkWidget* window, GdkEventAny* event, gpointer data)
{
  gtk_main_quit();

  return TRUE;
}

static void
configurable_widget_destroy_callback(GtkWidget* widget, gpointer data)
{
  guint notify_id;
  GConfClient* client;

  client = gtk_object_get_data(GTK_OBJECT(widget), "client");
  notify_id = GPOINTER_TO_UINT(gtk_object_get_data(GTK_OBJECT(widget), "notify_id"));

  if (notify_id != 0)
    gconf_client_notify_remove(client, notify_id);
}

static void
configurable_widget_config_notify(GConfClient* client,
                                  guint cnxn_id,
                                  GConfEntry *entry,
                                  gpointer user_data)
{
  GtkWidget* label = user_data;

  g_return_if_fail(label != NULL);
  g_return_if_fail(GTK_IS_LABEL(label));

  /* Note that value can be NULL (unset) or it can have
     the wrong type! */
  
  if (entry->value == NULL)
    {
      gtk_label_set_text(GTK_LABEL(label), "");
    }
  else if (entry->value->type == GCONF_VALUE_STRING)
    {
      gtk_label_set_text(GTK_LABEL(label),
                         gconf_value_get_string(entry->value));
    }
  else
    {
      /* A real app would probably fall back to a reasonable default in this case.  */
      gtk_label_set_text(GTK_LABEL(label), "!type error!");
    }
}

static GtkWidget*
create_configurable_widget(GConfClient* client, const gchar* config_key)
{
  GtkWidget* frame;
  GtkWidget* label;
  guint notify_id;
  gchar* str;
  
  frame = gtk_frame_new(config_key);

  label = gtk_label_new("");

  gtk_container_add(GTK_CONTAINER(frame), label);
  
  str = gconf_client_get_string(client, config_key, NULL);

  if (str != NULL)
    {
      gtk_label_set_text(GTK_LABEL(label), str);
      g_free(str);
    }

  notify_id = gconf_client_notify_add(client,
                                      config_key,
                                      configurable_widget_config_notify,
                                      label,
                                      NULL, NULL);

  /* Note that notify_id will be 0 if there was an error,
     so we handle that in our destroy callback. */
  
  gtk_object_set_data(GTK_OBJECT(label), "notify_id", GUINT_TO_POINTER(notify_id));
  gtk_object_set_data(GTK_OBJECT(label), "client", client);

  gtk_signal_connect(GTK_OBJECT(label), "destroy",
                     configurable_widget_destroy_callback,
                     NULL);
  
  return frame;
}

static void
prefs_dialog_destroyed(GtkWidget* dialog, gpointer main_window)
{
  gtk_object_set_data(GTK_OBJECT(main_window), "prefs", NULL);
}

static void
prefs_clicked(GtkWidget* button, gpointer data)
{
  GtkWidget* prefs_dialog;
  GtkWidget* main_window = data;
  GConfClient* client;

  prefs_dialog = gtk_object_get_data(GTK_OBJECT(main_window), "prefs");

  if (prefs_dialog == NULL)
    {
      client = gtk_object_get_data(GTK_OBJECT(main_window), "client");
      
      prefs_dialog = create_prefs_dialog(main_window, client);

      gtk_object_set_data(GTK_OBJECT(main_window), "prefs", prefs_dialog);

      gtk_signal_connect(GTK_OBJECT(prefs_dialog), "destroy",
                         GTK_SIGNAL_FUNC(prefs_dialog_destroyed),
                         main_window);
      
      gtk_widget_show_all(prefs_dialog);
    }
  else if (GTK_WIDGET_REALIZED(prefs_dialog))
    {
      gdk_window_show(prefs_dialog->window);
      gdk_window_raise(prefs_dialog->window);
    }
}

static GtkWidget*
create_main_window(GConfClient* client)
{
  GtkWidget* w;
  GtkWidget* vbox;
  GtkWidget* config;
  GtkWidget* prefs;
  
  w = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  vbox = gtk_vbox_new(FALSE, 10);

  gtk_container_add(GTK_CONTAINER(w), vbox);
  
  config = create_configurable_widget(client, "/apps/basic-gconf-app/foo");
  gtk_box_pack_start(GTK_BOX(vbox), config, FALSE, FALSE, 0);

  config = create_configurable_widget(client, "/apps/basic-gconf-app/bar");
  gtk_box_pack_start(GTK_BOX(vbox), config, FALSE, FALSE, 0);
  
  config = create_configurable_widget(client, "/apps/basic-gconf-app/baz");
  gtk_box_pack_start(GTK_BOX(vbox), config, FALSE, FALSE, 0);

  config = create_configurable_widget(client, "/apps/basic-gconf-app/blah");
  gtk_box_pack_start(GTK_BOX(vbox), config, FALSE, FALSE, 0);

  gtk_signal_connect(GTK_OBJECT(w), "delete_event",
                     GTK_SIGNAL_FUNC(delete_event_callback),
                     NULL);

  gtk_object_set_data(GTK_OBJECT(w), "client", client);
  
  prefs = gtk_button_new_with_label("Prefs");
  gtk_box_pack_end(GTK_BOX(vbox), prefs, FALSE, FALSE, 0);
  gtk_signal_connect(GTK_OBJECT(prefs), "clicked",
                     GTK_SIGNAL_FUNC(prefs_clicked), w);
  
  return w;
}


/****************************************/
/*
 * Preferences dialog code. NOTE that the prefs dialog knows NOTHING
 * about the existence of the main window; it is purely a way to fool
 * with the GConf database.
 */


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
  GtkWidget* ok;
  GtkWidget* cancel;
  GConfChangeSet* cs;
  GConfChangeSet* revert_cs;
  
  apply = gtk_object_get_data(GTK_OBJECT(dialog), "apply");
  revert = gtk_object_get_data(GTK_OBJECT(dialog), "revert");
  ok = gtk_object_get_data(GTK_OBJECT(dialog), "ok");
  cancel = gtk_object_get_data(GTK_OBJECT(dialog), "cancel");

  g_assert(apply != NULL);
  g_assert(revert != NULL);
  
  cs = prefs_dialog_get_change_set(dialog);

  revert_cs = gtk_object_get_data(GTK_OBJECT(dialog), "revert_changeset");

  if (gconf_change_set_size(cs) > 0)
    {
      gtk_widget_set_sensitive(apply, TRUE);
    }
  else
    {
      gtk_widget_set_sensitive(apply, FALSE);
    }
      
  if (revert_cs != NULL)
    gtk_widget_set_sensitive(revert, TRUE);
  else
    gtk_widget_set_sensitive(revert, FALSE);
}

static void
prefs_dialog_apply(GtkWidget* dialog)
{
  GConfChangeSet* cs;
  GConfClient* client;
  GConfChangeSet* revert_cs;
  
  client = gtk_object_get_data(GTK_OBJECT(dialog), "client");

  /* Create the revert changeset on the first apply; this means
     the revert button should now be sensitive */
  revert_cs = gtk_object_get_data(GTK_OBJECT(dialog), "revert_changeset");

  if (revert_cs == NULL)
    {
      revert_cs = gconf_client_change_set_from_current(client,
                                                              NULL,
                                                              "/apps/basic-gconf-app/foo",
                                                              "/apps/basic-gconf-app/bar",
                                                              "/apps/basic-gconf-app/baz",
                                                              "/apps/basic-gconf-app/blah",
                                                              NULL);

      gtk_object_set_data(GTK_OBJECT(dialog), "revert_changeset", revert_cs);
    }
      
  cs = gtk_object_get_data(GTK_OBJECT(dialog), "changeset");
  
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
update_entry(GtkWidget* dialog, GConfChangeSet* cs, const gchar* config_key)
{
  GConfValue* value = NULL;
  GConfClient* client;

  client = gtk_object_get_data(GTK_OBJECT(dialog), "client");
  
  if (gconf_change_set_check_value(cs, config_key,
                                   &value))
    {
      GtkWidget* entry;

      entry = gtk_object_get_data(GTK_OBJECT(dialog), config_key);

      g_assert(entry != NULL);
      
      if (value == NULL)
        {
          /* We need to check for a default value,
             since the revert set will unset the user setting */
          GConfValue* def;

          def = gconf_client_get_default_from_schema(client,
                                                     config_key,
                                                     NULL);

          if (def)
            {
              if (def->type == GCONF_VALUE_STRING)
                {
                  gtk_entry_set_text(GTK_ENTRY(entry), gconf_value_get_string(def));
                }
              else
                g_warning("Wrong type for default value of %s", config_key);

              gconf_value_free(def);
            }
          else
            gtk_entry_set_text(GTK_ENTRY(entry), "");
        }
      else if (value->type == GCONF_VALUE_STRING)
        {
          gtk_entry_set_text(GTK_ENTRY(entry), gconf_value_get_string(value));
        }
      else
        {
          /* error, wrong type value in the config database */
          g_warning("GConfChangeSet had wrong value type %d for key %s",
                    value->type, config_key);
          gtk_entry_set_text(GTK_ENTRY(entry), "");          
        }
    }
}

static void
prefs_dialog_revert(GtkWidget* dialog)
{
  GConfChangeSet* cs;
  GConfChangeSet* revert_cs;
  GConfClient* client;

  revert_cs = gtk_object_get_data(GTK_OBJECT(dialog), "revert_changeset");

  if (revert_cs == NULL)
    return; /* happens on cancel, if no apply has been done */
  
  client = gtk_object_get_data(GTK_OBJECT(dialog), "client");
  
  cs = gtk_object_get_data(GTK_OBJECT(dialog), "changeset");

  /* When reverting, you want to discard any pending changes so
     "apply" won't do anything */
  gconf_change_set_clear(cs);

  /* FALSE so we don't remove committed stuff from the revert set */
  gconf_client_commit_change_set(client, revert_cs, FALSE, NULL);

  /* Set the prefs dialog contents back to the
     new values */
  update_entry(dialog, revert_cs, "/apps/basic-gconf-app/foo");
  update_entry(dialog, revert_cs, "/apps/basic-gconf-app/bar");
  update_entry(dialog, revert_cs, "/apps/basic-gconf-app/baz");
  update_entry(dialog, revert_cs, "/apps/basic-gconf-app/blah");
  
  /* Update sensitivity of the dialog buttons */
  prefs_dialog_update_sensitivity(dialog);
}

static void
config_entry_destroy_callback(GtkWidget* entry, gpointer data)
{
  gchar* key;

  key = gtk_object_get_data(GTK_OBJECT(entry), "key");

  g_free(key);
}

static void
config_entry_changed_callback(GtkWidget* entry, gpointer data)
{
  GtkWidget* prefs_dialog = data;
  GConfChangeSet* cs;
  gchar* text;
  const gchar* key;
  
  cs = prefs_dialog_get_change_set(prefs_dialog);

  text = gtk_editable_get_chars(GTK_EDITABLE(entry), 0, -1);

  key = gtk_object_get_data(GTK_OBJECT(entry), "key");

  /* Unset if the string is zero-length, otherwise set */
  if (*text != '\0')
    gconf_change_set_set_string(cs, key, text);
  else
    gconf_change_set_unset(cs, key);
  
  g_free(text);
  
  prefs_dialog_update_sensitivity(prefs_dialog);
}

static GtkWidget*
create_config_entry(GtkWidget* prefs_dialog, GConfClient* client, const gchar* config_key)
{
  GtkWidget* frame;
  GtkWidget* entry;
  GConfValue* initial = NULL;
  
  frame = gtk_frame_new(config_key);

  entry = gtk_entry_new();
  
  gtk_container_add(GTK_CONTAINER(frame), entry);
  
  initial = gconf_client_get(client, config_key, NULL);

  if (initial != NULL && initial->type == GCONF_VALUE_STRING)
    {
      const gchar* str = gconf_value_get_string(initial);
      gtk_entry_set_text(GTK_ENTRY(entry), str);
    }

  if (initial)
    gconf_value_free(initial);
  
  gtk_object_set_data(GTK_OBJECT(entry), "client", client);
  gtk_object_set_data(GTK_OBJECT(entry), "key", g_strdup(config_key));

  gtk_signal_connect(GTK_OBJECT(entry), "destroy",
                     config_entry_destroy_callback,
                     NULL);

  gtk_signal_connect(GTK_OBJECT(entry), "changed",
                     config_entry_changed_callback,
                     prefs_dialog);

  /* A dubious hack; set the entry as object data using its
     config key as the key so we can find it in the prefs dialog
     revert code */
  gtk_object_set_data(GTK_OBJECT(prefs_dialog),
                      config_key, entry);

  /* Set the entry insensitive if the key it edits isn't writable */
  gtk_widget_set_sensitive (entry,
                            gconf_client_key_is_writable (client,
                                                          config_key, NULL));
  
  return frame;
}

static void
apply_button_callback(GtkWidget* button, gpointer data)
{
  prefs_dialog_apply(data);
}

static void
revert_button_callback(GtkWidget* button, gpointer data)
{
  prefs_dialog_revert(data);
}

static void
ok_button_callback(GtkWidget* button, gpointer data)
{
  GtkWidget* dialog = data;
  
  prefs_dialog_apply(dialog);
  gtk_widget_destroy(dialog);
}

static void
cancel_button_callback(GtkWidget* button, gpointer data)
{
  GtkWidget* dialog = data;
  
  prefs_dialog_revert(dialog);
  gtk_widget_destroy(dialog);
}

static void
prefs_dialog_destroy_callback(GtkWidget* dialog, gpointer data)
{
  GConfChangeSet* cs;
  GConfChangeSet* revert_cs;
  GConfClient* client;

  client = gtk_object_get_data(GTK_OBJECT(dialog), "client");
  
  cs = gtk_object_get_data(GTK_OBJECT(dialog), "changeset");
  
  revert_cs = gtk_object_get_data(GTK_OBJECT(dialog), "revert_changeset");

  gconf_change_set_unref(cs);

  if (revert_cs)
    gconf_change_set_unref(revert_cs);

  gtk_object_unref(GTK_OBJECT(client));
}

static GtkWidget*
create_prefs_dialog(GtkWidget* parent, GConfClient* client)
{
  GConfChangeSet* cs;
  GtkWidget* dialog;
  GtkWidget* bbox;
  GtkWidget* apply;
  GtkWidget* revert;
  GtkWidget* ok;
  GtkWidget* cancel;
  GtkWidget* vbox_outer;
  GtkWidget* vbox_inner;
  GtkWidget* entry;
  
  dialog = gtk_window_new(GTK_WINDOW_DIALOG);

  apply = gtk_button_new_with_label("Apply");
  revert = gtk_button_new_with_label("Revert");

  ok = gtk_button_new_with_label("OK");
  cancel = gtk_button_new_with_label("Cancel");
  
  gtk_object_set_data(GTK_OBJECT(dialog), "apply", apply);
  gtk_object_set_data(GTK_OBJECT(dialog), "revert", revert);
  gtk_object_set_data(GTK_OBJECT(dialog), "ok", ok);
  gtk_object_set_data(GTK_OBJECT(dialog), "cancel", cancel);
  
  bbox = gtk_hbutton_box_new();

  vbox_outer = gtk_vbox_new(FALSE, 10);

  vbox_inner = gtk_vbox_new(FALSE, 10);
  
  gtk_container_add(GTK_CONTAINER(dialog), vbox_outer);

  gtk_box_pack_start(GTK_BOX(vbox_outer), vbox_inner, TRUE, TRUE, 0);
  gtk_box_pack_end(GTK_BOX(vbox_outer), bbox, FALSE, FALSE, 0);

  gtk_container_add(GTK_CONTAINER(bbox), apply);
  gtk_container_add(GTK_CONTAINER(bbox), revert);
  gtk_container_add(GTK_CONTAINER(bbox), ok);
  gtk_container_add(GTK_CONTAINER(bbox), cancel);

  cs = gconf_change_set_new();
  
  gtk_object_set_data(GTK_OBJECT(dialog), "changeset", cs);
  gtk_object_set_data(GTK_OBJECT(dialog), "client", client);

  /* Grab a reference */
  gtk_object_ref(GTK_OBJECT(client));
  
  gtk_signal_connect(GTK_OBJECT(dialog), "destroy",
                     GTK_SIGNAL_FUNC(prefs_dialog_destroy_callback),
                     NULL);

  prefs_dialog_update_sensitivity(dialog);

  gtk_signal_connect(GTK_OBJECT(apply), "clicked",
                     GTK_SIGNAL_FUNC(apply_button_callback),
                     dialog);

  gtk_signal_connect(GTK_OBJECT(revert), "clicked",
                     GTK_SIGNAL_FUNC(revert_button_callback),
                     dialog);

  gtk_signal_connect(GTK_OBJECT(ok), "clicked",
                     GTK_SIGNAL_FUNC(ok_button_callback),
                     dialog);

  gtk_signal_connect(GTK_OBJECT(cancel), "clicked",
                     GTK_SIGNAL_FUNC(cancel_button_callback),
                     dialog);
  
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));

  entry = create_config_entry(dialog, client, "/apps/basic-gconf-app/foo");
  gtk_box_pack_start(GTK_BOX(vbox_inner), entry, 
                     FALSE, FALSE, 0);

  entry = create_config_entry(dialog, client, "/apps/basic-gconf-app/bar");
  gtk_box_pack_start(GTK_BOX(vbox_inner), entry, 
                     FALSE, FALSE, 0);

  entry = create_config_entry(dialog, client, "/apps/basic-gconf-app/baz");
  gtk_box_pack_start(GTK_BOX(vbox_inner), entry, 
                     FALSE, FALSE, 0);

  entry = create_config_entry(dialog, client, "/apps/basic-gconf-app/blah");
  gtk_box_pack_start(GTK_BOX(vbox_inner), entry, 
                     FALSE, FALSE, 0);
  
  return dialog;
}
