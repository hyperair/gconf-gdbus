/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Matthias Clasen <mclasen@redhat.com>
 * Copyright © 2010 Christian Persch
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <config.h>

#include <stdlib.h>

#include <glib.h>
#include <gio/gio.h>

#include "gconf-defaults.h"

#define BUS_NAME "org.gnome.GConf.Defaults"

extern gboolean disable_killtimer;
static gboolean debug = FALSE;

static const GOptionEntry entries [] = {
	{ "debug", 0, 0, G_OPTION_ARG_NONE, &debug, "Emit debug output", NULL },
	{ "no-kill", 0, 0, G_OPTION_ARG_NONE, &disable_killtimer, "Don't exit when idle", NULL },
	{ NULL, }
};

static gint log_levels = (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING);

static void
log_default_handler (const gchar   *log_domain,
                     GLogLevelFlags log_level,
                     const gchar   *message,
                     gpointer       unused_data)
{
	if ((log_level & log_levels) != 0) {
		g_log_default_handler (log_domain, log_level, message, unused_data);
	}
}

static GMainLoop *loop = NULL;
static GConfDefaults *mechanism = NULL;

static void
bus_acquired_cb (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
        mechanism = gconf_defaults_new (connection);
        if (mechanism == NULL && g_main_loop_is_running (loop))
                g_main_loop_quit (loop);
}

static void
name_acquired_cb (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
        g_debug ("Name '%s' acquired\n", name);
}

static void
name_lost_cb (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
        g_debug ("Name '%s' lost \n", name);

        if (mechanism) {
                g_object_unref (mechanism);
                mechanism = NULL;
        }

        if (g_main_loop_is_running (loop))
                g_main_loop_quit (loop);
}

int
main (int argc, char **argv)
{
        GOptionContext *options;
        guint           owner_id;
        GError         *error = NULL;

        g_type_init ();

	options = g_option_context_new (NULL);
	g_option_context_add_main_entries (options, entries, NULL);
	if (!g_option_context_parse (options, &argc, &argv, &error)) {
		g_printerr ("Failed to parse options: %s\n", error->message);
		g_error_free (error);
                g_option_context_free (options);
                return 1;
	}
	g_option_context_free (options);

	g_log_set_default_handler (log_default_handler, NULL);
	if (debug) {
		log_levels = log_levels | G_LOG_LEVEL_DEBUG;
	}

        loop = g_main_loop_new (NULL, FALSE);

        owner_id = g_bus_own_name (G_BUS_TYPE_STARTER,
                                   BUS_NAME,
                                   G_BUS_NAME_OWNER_FLAGS_NONE,
                                   bus_acquired_cb,
                                   name_acquired_cb,
                                   name_lost_cb,
                                   NULL, NULL);

        g_main_loop_run (loop);

        g_bus_unown_name (owner_id);
        g_main_loop_unref (loop);

        return 0;
}
