/* GConf
 *
 * Copyright (C) 1999, 2000 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <config.h>
#include <gnome.h>
#include <libgnorba/gnorba.h>
#include "gconf.h"
#include "gconf-orbit.h"

static gboolean self_change = FALSE;

static void 
notify_func(GConf* conf, guint cnxn_id, const gchar* key, GConfValue* value, gpointer user_data)
{
  int pid = getpid();
  g_print ("PID %d received notify on key `%s' connection %u\n", pid, key, cnxn_id);
  self_change = TRUE;
  gtk_entry_set_text(GTK_ENTRY(user_data), gconf_value_get_string(value));
  self_change = FALSE;
}

static void 
changed_cb(GtkWidget* entry, gpointer data)
{
  GConf* conf = data;
  gchar* txt;
  GConfValue* value;

  if (self_change)
    return;

  txt = gtk_entry_get_text(GTK_ENTRY(entry));
  
  value = gconf_value_new(GCONF_VALUE_STRING);

  gconf_value_set_string(value, txt);

  gconf_engine_set (conf, "/gnome/gconf-testclient/entry_contents", value);
}

int 
main(int argc, char* argv[])
{
  GtkWidget* app;
  GtkWidget* entry;
  CORBA_ORB orb;
  CORBA_Environment ev;
  GConf* conf;
  guint cnxn;
  GConfValue* val;

  CORBA_exception_init(&ev);

  /* Bleah. Server can't use Gnome authentication unless we have a
     gnome-gconfd, which we'll have to have eventually I guess, but
     anyway, this is irritating. 
  */
#if 0
  orb = gnome_CORBA_init(PACKAGE, VERSION, &argc, argv, 0, &ev);

  if (orb == CORBA_OBJECT_NIL)
    {
      g_warning("Couldn't get orb");
      exit(1);
    }


  gconf_set_orb(orb); 
#endif
  gnome_init(PACKAGE, VERSION, argc, argv);

  gconf_init_orb(&argc, argv);

  gconf_init();

  conf = gconf_new();

  app = gnome_app_new("gconf-test", "Testing GConf");

  entry = gtk_entry_new();
  
  val = gconf_engine_get (conf, "/gnome/gconf-testclient/entry_contents");

  if (val != NULL)
    {
      gtk_entry_set_text(GTK_ENTRY(entry), gconf_value_get_string(val));
      gconf_value_free(val);
      val = NULL;
    }

  gnome_app_set_contents(GNOME_APP(app), entry);

  gtk_signal_connect(GTK_OBJECT(entry), 
                     "changed", 
                     GTK_SIGNAL_FUNC(changed_cb),
                     conf);

  gtk_signal_connect(GTK_OBJECT(app),
                     "delete_event",
                     GTK_SIGNAL_FUNC(gtk_main_quit),
                     NULL);

  cnxn = gconf_engine_notify_add(conf, "/gnome/gconf-testclient/entry_contents", notify_func, entry);

  if (cnxn != 0)
    g_print ("Connection %u added\n", cnxn);
  else
    {
      fg_print (stderr, "Failed to add listener\n");
      return 1;
    }

  gtk_widget_show_all(app);

  gtk_main();

  gconf_destroy(conf);

  return 0;
}
