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

/* A very simple program that monitors a single key for changes. */

#include <gconf-client.h> /* Once GConf is installed, this should be
                             <gconf/gconf-client.h> */
#include <gtk/gtk.h>

void
key_changed_callback(GConfClient* client,
                     guint cnxn_id,
                     GConfEntry *entry,
                     gpointer user_data)
{
  GtkWidget* label;
  
  label = GTK_WIDGET(user_data);

  if (entry->value == NULL)
    {
      gtk_label_set(GTK_LABEL(label), "<unset>");
    }
  else
    {
      if (entry->value->type == GCONF_VALUE_STRING)
        {
          gtk_label_set(GTK_LABEL(label),
                        gconf_value_get_string(entry->value));
        }
      else
        {
          gtk_label_set(GTK_LABEL(label), "<wrong type>");
        }
    }
}

int
main(int argc, char** argv)
{
  GtkWidget* window;
  GtkWidget* label;
  GConfClient* client;
  gchar* str;
  
  gtk_init(&argc, &argv);
  gconf_init(argc, argv, NULL);

  client = gconf_client_get_default();
  
  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  str = gconf_client_get_string(client, "/extra/test/directory/key",
                                NULL);
  
  label = gtk_label_new(str ? str : "<unset>");

  if (str)
    g_free(str);
  
  gtk_container_add(GTK_CONTAINER(window), label);

  gconf_client_add_dir(client,
                       "/extra/test/directory",
                       GCONF_CLIENT_PRELOAD_NONE,
                       NULL);

  gconf_client_notify_add(client, "/extra/test/directory/key",
                          key_changed_callback,
                          label,
                          NULL, NULL);
  
  gtk_widget_show_all(window);

  gtk_main();

  return 0;
}




