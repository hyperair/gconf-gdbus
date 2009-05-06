/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Matthias Clasen <mclasen@redhat.com>
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
 *
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <pwd.h>

#include <glib.h>
#include <glib-object.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <polkit-dbus/polkit-dbus.h>
#include <polkit/polkit.h>

#define GCONF_ENABLE_INTERNALS
#include <gconf/gconf-client.h>
#include <gconf/gconf-engine.h>

#include "gconf-defaults.h"
#include "gconf-defaults-glue.h"

static gboolean
do_exit (gpointer user_data)
{
	g_debug ("Exiting due to inactivity");
	exit (1);
	return FALSE;
}

static guint timer_id = 0;

static void
stop_killtimer (void)
{
	if (timer_id > 0) {
		g_source_remove (timer_id);
		timer_id = 0;
	}
}

static void
start_killtimer (void)
{
	g_debug ("Setting killtimer to 30 seconds...");
	timer_id = g_timeout_add_seconds (30, do_exit, NULL);
}

struct GConfDefaultsPrivate
{
	DBusGConnection *system_bus_connection;
	DBusGProxy      *system_bus_proxy;
	PolKitContext   *pol_ctx;
};

static void gconf_defaults_finalize (GObject *object);

G_DEFINE_TYPE (GConfDefaults, gconf_defaults, G_TYPE_OBJECT)

#define GCONF_DEFAULTS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GCONF_TYPE_DEFAULTS, GConfDefaultsPrivate))

GQuark
gconf_defaults_error_quark (void)
{
	static GQuark ret = 0;

	if (ret == 0) {
		ret = g_quark_from_static_string ("gconf_defaults_error");
	}

	return ret;
}


#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
gconf_defaults_error_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] = {
			ENUM_ENTRY (GCONF_DEFAULTS_ERROR_GENERAL, "GeneralError"),
			ENUM_ENTRY (GCONF_DEFAULTS_ERROR_NOT_PRIVILEGED, "NotPrivileged"),
			{ 0, 0, 0 }
		};

		g_assert (GCONF_DEFAULTS_NUM_ERRORS == G_N_ELEMENTS (values) - 1);

		etype = g_enum_register_static ("GConfDefaultsError", values);
	}

	return etype;
}


static GObject *
gconf_defaults_constructor (GType                  type,
                            guint                  n_construct_properties,
                            GObjectConstructParam *construct_properties)
{
	GConfDefaults      *mechanism;
	GConfDefaultsClass *klass;

	klass = GCONF_DEFAULTS_CLASS (g_type_class_peek (GCONF_TYPE_DEFAULTS));

	mechanism = GCONF_DEFAULTS (G_OBJECT_CLASS (gconf_defaults_parent_class)->constructor (
						type,
						n_construct_properties,
						construct_properties));

	return G_OBJECT (mechanism);
}

enum {
	SYSTEM_SET,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
gconf_defaults_class_init (GConfDefaultsClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);

	object_class->constructor = gconf_defaults_constructor;
	object_class->finalize = gconf_defaults_finalize;

	signals[SYSTEM_SET] = g_signal_new ("system-set",
					    G_OBJECT_CLASS_TYPE (object_class),
					    G_SIGNAL_RUN_FIRST,
					    G_STRUCT_OFFSET (GConfDefaultsClass, system_set),
					    NULL, NULL,
					    g_cclosure_marshal_VOID__BOXED,
					    G_TYPE_NONE, 1, G_TYPE_STRV);

	g_type_class_add_private (klass, sizeof (GConfDefaultsPrivate));

	dbus_g_object_type_install_info (GCONF_TYPE_DEFAULTS, &dbus_glib_gconf_defaults_object_info);

	dbus_g_error_domain_register (GCONF_DEFAULTS_ERROR, NULL, GCONF_DEFAULTS_TYPE_ERROR);
}

static void
gconf_defaults_init (GConfDefaults *mechanism)
{
	mechanism->priv = GCONF_DEFAULTS_GET_PRIVATE (mechanism);
}

static void
gconf_defaults_finalize (GObject *object)
{
	GConfDefaults *mechanism;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GCONF_IS_DEFAULTS (object));

	mechanism = GCONF_DEFAULTS (object);

	g_return_if_fail (mechanism->priv != NULL);

	g_object_unref (mechanism->priv->system_bus_proxy);

	G_OBJECT_CLASS (gconf_defaults_parent_class)->finalize (object);
}

static gboolean
pk_io_watch_have_data (GIOChannel   *channel,
                       GIOCondition  condition,
                       gpointer      user_data)
{
	int fd;
	PolKitContext *pk_context = user_data;
	fd = g_io_channel_unix_get_fd (channel);
	polkit_context_io_func (pk_context, fd);
	return TRUE;
}

static int
pk_io_add_watch (PolKitContext *pk_context, int fd)
{
	guint id = 0;
	GIOChannel *channel;
	channel = g_io_channel_unix_new (fd);
	if (channel == NULL)
		goto out;
	id = g_io_add_watch (channel, G_IO_IN, pk_io_watch_have_data, pk_context);
	if (id == 0) {
		g_io_channel_unref (channel);
		goto out;
	}
	g_io_channel_unref (channel);
out:
	return id;
}

static void
pk_io_remove_watch (PolKitContext *pk_context, int watch_id)
{
	g_source_remove (watch_id);
}

static gboolean
register_mechanism (GConfDefaults *mechanism)
{
	GError *error = NULL;

	mechanism->priv->pol_ctx = polkit_context_new ();
	polkit_context_set_io_watch_functions (mechanism->priv->pol_ctx, pk_io_add_watch, pk_io_remove_watch);
	if (!polkit_context_init (mechanism->priv->pol_ctx, NULL)) {
		g_critical ("cannot initialize libpolkit");
		goto error;
	}

	error = NULL;
	mechanism->priv->system_bus_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (mechanism->priv->system_bus_connection == NULL) {
		if (error != NULL) {
			g_critical ("error getting system bus: %s", error->message);
			g_error_free (error);
		}
		goto error;
	}

	dbus_g_connection_register_g_object (mechanism->priv->system_bus_connection, "/",
					     G_OBJECT (mechanism));

	mechanism->priv->system_bus_proxy = dbus_g_proxy_new_for_name (mechanism->priv->system_bus_connection,
								       DBUS_SERVICE_DBUS,
								       DBUS_PATH_DBUS,
								       DBUS_INTERFACE_DBUS);

	start_killtimer ();

	return TRUE;

error:
	return FALSE;
}


GConfDefaults *
gconf_defaults_new (void)
{
	GObject *object;
	gboolean res;

	object = g_object_new (GCONF_TYPE_DEFAULTS, NULL);

	res = register_mechanism (GCONF_DEFAULTS (object));
	if (! res) {
		g_object_unref (object);
		return NULL;
	}

	return GCONF_DEFAULTS (object);
}

static const char *
polkit_action_for_gconf_path (GConfDefaults *mechanism,
			      const char    *annotation_key,
			      const char    *path)
{
	PolKitPolicyCache *cache;
	PolKitPolicyFileEntry *entry;
	char *prefix, *p;
	const char *action;

	cache = polkit_context_get_policy_cache (mechanism->priv->pol_ctx);
	prefix = g_strdup (path);

	while (1) {
		entry = polkit_policy_cache_get_entry_by_annotation (cache,
								     annotation_key,
								     prefix);
		if (entry) {
			action = polkit_policy_file_entry_get_id (entry);
			break;
		}

		p = strrchr (prefix, '/');

		if (p == NULL || p == prefix) {
			action = NULL;
			break;
		}

		*p = 0;
	}

	g_free (prefix);

	return action;
}

static gboolean
check_polkit_for_action (GConfDefaults         *mechanism,
                         DBusGMethodInvocation *context,
                         const char            *action)
{
	const char *sender;
	GError *error;
	DBusError dbus_error;
	PolKitCaller *pk_caller;
	PolKitAction *pk_action;
	PolKitResult pk_result;

	error = NULL;

	/* Check that caller is privileged */
	sender = dbus_g_method_get_sender (context);
	dbus_error_init (&dbus_error);
	pk_caller = polkit_caller_new_from_dbus_name (
	dbus_g_connection_get_connection (mechanism->priv->system_bus_connection),
					  sender,
					  &dbus_error);
	if (pk_caller == NULL) {
		error = g_error_new (GCONF_DEFAULTS_ERROR,
				     GCONF_DEFAULTS_ERROR_GENERAL,
				     "Error getting information about caller: %s: %s",
				     dbus_error.name, dbus_error.message);
		dbus_error_free (&dbus_error);
		dbus_g_method_return_error (context, error);
		g_error_free (error);
		return FALSE;
	}

	pk_action = polkit_action_new ();
	polkit_action_set_action_id (pk_action, action);
	pk_result = polkit_context_is_caller_authorized (mechanism->priv->pol_ctx, pk_action, pk_caller, TRUE, NULL);
	polkit_caller_unref (pk_caller);

	if (pk_result != POLKIT_RESULT_YES) {
		dbus_error_init (&dbus_error);
		polkit_dbus_error_generate (pk_action, pk_result, &dbus_error);
		dbus_set_g_error (&error, &dbus_error);
		dbus_g_method_return_error (context, error);
		dbus_error_free (&dbus_error);
		g_error_free (error);
		polkit_action_unref (pk_action);
		return FALSE;
	}

	polkit_action_unref (pk_action);
	return TRUE;
}

static char *
gconf_address_for_caller (GConfDefaults          *mechanism,
			  DBusGMethodInvocation  *context,
			  GError                **gerror)
{
	char *sender;
	DBusConnection *conn;
	uid_t uid;
	struct passwd *pwd;
	char *result;
	DBusError error;

	conn = dbus_g_connection_get_connection (mechanism->priv->system_bus_connection);
	sender = dbus_g_method_get_sender (context);

	dbus_error_init (&error);
	uid = dbus_bus_get_unix_user (conn, sender, &error);
	g_free (sender);
	if (uid == (unsigned)-1) {
		dbus_set_g_error (gerror, &error);
		dbus_error_free (&error);
		return NULL;
	}

	pwd = getpwuid (uid);
	if (pwd == NULL) {
		g_set_error (gerror,
			     0, 0,
			     "Failed to get passwd information for uid %d", uid);
		return NULL;
	}

	result = g_strconcat ("xml:merged:", pwd->pw_dir, "/.gconf", NULL);
	return result;
}

static gboolean
path_is_excluded (const char  *path,
		  const char **excludes)
{
	int i;

	for (i = 0; excludes && excludes[i]; i++) {
		if (g_str_has_prefix (path, excludes[i]))
			return TRUE;
	}

	return FALSE;
}

static void
copy_tree (GConfClient     *src,
	   const char      *path,
	   GConfChangeSet  *changes,
	   const char     **excludes)
{
	GSList *list, *l;
	GConfEntry *entry;

	if (path_is_excluded (path, excludes))
		return;

	list = gconf_client_all_entries (src, path, NULL);
	for (l = list; l; l = l->next) {
		entry = l->data;
		if (!path_is_excluded (entry->key, excludes))
			gconf_change_set_set (changes, entry->key, entry->value);
	}
	g_slist_foreach (list, (GFunc)gconf_entry_free, NULL);
	g_slist_free (list);

	list = gconf_client_all_dirs (src, path, NULL);
	for (l = list; l; l = l->next)
		copy_tree (src, (const char *)l->data, changes, excludes);
	g_slist_foreach (list, (GFunc)g_free, NULL);
	g_slist_free (list);
}

static void
copy_entry (GConfClient     *src,
	    const char      *path,
	    GConfChangeSet  *changes,
	    const char     **excludes)
{
	GConfValue *value;

	if (path_is_excluded (path, excludes))
		return;

	value = gconf_client_get (src, path, NULL);
	if (value) {
		gconf_change_set_set (changes, path, value);
		gconf_value_free (value);
	}
}


static void
do_copy (GConfDefaults          *mechanism,
	 gboolean                mandatory,
	 const char            **includes,
	 const char            **excludes,
	 DBusGMethodInvocation  *context,
	 GConfChangeSet        **changeset_out)
{
        char *address = NULL;
	GConfClient *source = NULL;
	GConfClient *dest = NULL;
	GConfChangeSet *changes = NULL;
	GConfEngine *engine;
	GError *error;
	GError *error2;
	const char *action;
	const char *annotation_key;
	const char *default_action;
	const char *dest_address;
	int i;

	if (changeset_out)
		*changeset_out = NULL;

        stop_killtimer ();

	/* check privileges for each include */
	if (mandatory) {
		annotation_key = "org.gnome.gconf.defaults.set-mandatory.prefix"; 
		default_action = "org.gnome.gconf.defaults.set-mandatory";
		dest_address = "xml:merged:" SYSGCONFDIR "/gconf.xml.mandatory";
	}
	else {
		annotation_key = "org.gnome.gconf.defaults.set-system.prefix";
		default_action = "org.gnome.gconf.defaults.set-system";
		dest_address = "xml:merged:" SYSGCONFDIR "/gconf.xml.system";
	}

	for (i = 0; includes[i]; i++) {
		action = polkit_action_for_gconf_path (mechanism, annotation_key, includes[i]);
		if (action == NULL)
			action = default_action;

		if (!check_polkit_for_action (mechanism, context, action))
			goto out;
	}

	error = NULL;
	engine = gconf_engine_get_local (dest_address, &error);
	if (error)
		goto cleanup;

	dest = gconf_client_get_for_engine (engine);
	gconf_engine_unref (engine);

	/* find the address to from the caller id */
	address = gconf_address_for_caller (mechanism, context, &error);
	if (error)
		goto cleanup;

	engine = gconf_engine_get_local (address, &error);
	if (error)
		goto cleanup;

	source = gconf_client_get_for_engine (engine);
	gconf_engine_unref (engine);

	changes = gconf_change_set_new ();

 	/* recursively copy each include, leaving out the excludes */
	for (i = 0; includes[i]; i++) {
		if (gconf_client_dir_exists (source, includes[i], NULL))
			copy_tree (source, includes[i], changes, excludes);
		else
			copy_entry (source, includes[i], changes, excludes);
	}

	gconf_client_commit_change_set (dest, changes, FALSE, &error);
	gconf_client_suggest_sync (dest, NULL);

	if (changeset_out) {
		*changeset_out = changes;
		changes = NULL;
	}

cleanup:
	g_free (address);
	if (changes)
		gconf_change_set_unref (changes);
	if (dest)
		g_object_unref (dest);
	if (source)
		g_object_unref (source);

	if (error) {
		g_print ("failed to set GConf values:  %s\n", error->message);
		error2 = g_error_new_literal (GCONF_DEFAULTS_ERROR,
					      GCONF_DEFAULTS_ERROR_GENERAL,
					      error->message);
		g_error_free (error);

		dbus_g_method_return_error (context, error2);
		g_error_free (error2);
	}
	else
		dbus_g_method_return (context);

out:
	start_killtimer ();
}

static void
append_key (GConfChangeSet *cs,
	    const gchar *key,
	    GConfValue *value,
	    gpointer user_data)
{
	GPtrArray *keys = (GPtrArray *) user_data;

	g_ptr_array_add (keys, (gpointer) key);
}

static void
emit_system_set_signal (GConfDefaults  *mechanism,
                        GConfChangeSet *changes)
{
	GPtrArray *keys;

	if (!changes)
		return;

	keys = g_ptr_array_new ();
	gconf_change_set_foreach (changes, append_key, keys);
	g_ptr_array_add (keys, NULL);

	g_signal_emit (mechanism, signals[SYSTEM_SET], 0, keys->pdata);

	g_ptr_array_free (keys, TRUE);
}

void
gconf_defaults_set_system (GConfDefaults          *mechanism,
			   const char            **includes,
			   const char            **excludes,
			   DBusGMethodInvocation  *context)
{
	GConfChangeSet *changes = NULL;

	do_copy (mechanism, FALSE, includes, excludes, context, &changes);

	emit_system_set_signal (mechanism, changes);
	gconf_change_set_unref (changes);
}

void
gconf_defaults_set_mandatory (GConfDefaults          *mechanism,
                              const char            **includes,
                              const char            **excludes,
                              DBusGMethodInvocation  *context)
{
	do_copy (mechanism, FALSE, includes, excludes, context, NULL);
}

static void
do_set_value (GConfDefaults          *mechanism,
              gboolean                mandatory,
              const char             *path,
              const char             *value,
              DBusGMethodInvocation  *context,
              GConfChangeSet        **changeset_out)
{
	GConfClient *dest = NULL;
	GConfChangeSet *changes = NULL;
	GConfEngine *engine;
	GConfValue *gvalue;
	GError *error;
	GError *error2;
	const char *action;
	const char *annotation_key;
	const char *default_action;
	const char *dest_address;

	if (changeset_out)
		*changeset_out = NULL;

	stop_killtimer ();

	if (mandatory) {
		annotation_key = "org.gnome.gconf.defaults.set-mandatory.prefix";
		default_action = "org.gnome.gconf.defaults.set-mandatory";
		dest_address = "xml:merged:/etc/gconf/gconf.xml.mandatory";
	}
	else {
		annotation_key = "org.gnome.gconf.defaults.set-system.prefix";
		default_action = "org.gnome.gconf.defaults.set-system";
		dest_address = "xml:merged:/etc/gconf/gconf.xml.system";
	}

	action = polkit_action_for_gconf_path (mechanism, annotation_key, path);
	if (action == NULL)
		action = default_action;

	if (!check_polkit_for_action (mechanism, context, action))
		goto out;

	error = NULL;
	engine = gconf_engine_get_local (dest_address, &error);
	if (error)
		goto cleanup;

	dest = gconf_client_get_for_engine (engine);
	gconf_engine_unref (engine);

	changes = gconf_change_set_new ();

	gvalue = gconf_value_decode (value);
	if (!gvalue)
		goto cleanup;

	gconf_change_set_set (changes, path, gvalue);
	gconf_value_free (gvalue);

	gconf_client_commit_change_set (dest, changes, FALSE, &error);
	gconf_client_suggest_sync (dest, NULL);

	if (changeset_out) {
		*changeset_out = changes;
		changes = NULL;
	}

cleanup:
	if (changes)
		gconf_change_set_unref (changes);
	if (dest)
		g_object_unref (dest);

	if (error) {
		g_print ("failed to set GConf values:  %s\n", error->message);
		error2 = g_error_new_literal (GCONF_DEFAULTS_ERROR,
					      GCONF_DEFAULTS_ERROR_GENERAL,
					      error->message);
		g_error_free (error);

		dbus_g_method_return_error (context, error2);
		g_error_free (error2);
	}
	else
		dbus_g_method_return (context);

out:
	start_killtimer ();
}

void
gconf_defaults_set_system_value (GConfDefaults         *mechanism,
                                 const char            *path,
                                 const char            *value,
                                 DBusGMethodInvocation *context)
{
	GConfChangeSet *changes = NULL;

	do_set_value (mechanism, FALSE, path, value, context, &changes);

        emit_system_set_signal (mechanism, changes);
	gconf_change_set_unref (changes);
}

void
gconf_defaults_set_mandatory_value (GConfDefaults         *mechanism,
                                    const char            *path,
                                    const char            *value,
                                    DBusGMethodInvocation *context)
{
	do_set_value (mechanism, TRUE, path, value, context, NULL);
}

static void
unset_tree (GConfClient     *dest,
            const char      *path,
	    GConfChangeSet  *changes,
            const char     **excludes)
{
	GSList *list, *l;
	GConfEntry *entry;

	if (path_is_excluded (path, excludes)) 
		return;

	list = gconf_client_all_entries (dest, path, NULL);
	for (l = list; l; l = l->next) {
		entry = l->data;
		if (!path_is_excluded (entry->key, excludes)) 
			gconf_change_set_unset (changes, entry->key);
	}
	g_slist_foreach (list, (GFunc)gconf_entry_free, NULL);
	g_slist_free (list);

	list = gconf_client_all_dirs (dest, path, NULL);
	for (l = list; l; l = l->next)
		unset_tree (dest, (const char *)l->data, changes, excludes);
	g_slist_foreach (list, (GFunc)g_free, NULL);
	g_slist_free (list);
}
            
static void
unset_entry (GConfClient     *dest,
             const char      *path,
	     GConfChangeSet  *changes,
             const char     **excludes)
{
	if (path_is_excluded (path, excludes)) 
		return;

	gconf_change_set_unset (changes, path);
}
            
static void
unset_in_db (GConfDefaults  *mechanism,
	     const char     *address,
             const char    **includes,
             const char    **excludes,
	     GError        **error)
{
	GConfEngine *engine;
	GConfClient *dest = NULL;
	GConfChangeSet *changes = NULL;
	int i;

	engine = gconf_engine_get_local (address, error);
	if (*error) 
		goto out;

	dest = gconf_client_get_for_engine (engine);
	gconf_engine_unref (engine);

	changes = gconf_change_set_new ();

 	/* recursively copy each include, leaving out the excludes */
	for (i = 0; includes[i]; i++) {
		if (gconf_client_dir_exists (dest, includes[i], NULL))
			unset_tree (dest, includes[i], changes, excludes);
		else
			unset_entry (dest, includes[i], changes, excludes);
	}

	gconf_client_commit_change_set (dest, changes, TRUE, error);
	gconf_client_suggest_sync (dest, NULL);

out:
	if (dest)
		g_object_unref (dest);
	if (changes)
		gconf_change_set_unref (changes);
}

void
gconf_defaults_unset_mandatory (GConfDefaults          *mechanism,
                                const char            **includes,
                                const char            **excludes,
                                DBusGMethodInvocation  *context)
{
	const char *annotation_key;
	const char *default_action;
	int i;
	const char *action;
	GError *error;
	GError *error2;

	stop_killtimer ();

	annotation_key = "org.gnome.gconf.defaults.set-mandatory.prefix"; 
	default_action = "org.gnome.gconf.defaults.set-mandatory";

	for (i = 0; includes[i]; i++) {
		action = polkit_action_for_gconf_path (mechanism, annotation_key, includes[i]);
		if (action == NULL) 
			action = default_action;

		if (!check_polkit_for_action (mechanism, context, action)) 
			goto out;
	}

	error = NULL;
	unset_in_db (mechanism, "xml:merged:" SYSGCONFDIR "/gconf.xml.mandatory", 
		     includes, excludes, &error);

	if (error) {
		error2 = g_error_new_literal (GCONF_DEFAULTS_ERROR,
					      GCONF_DEFAULTS_ERROR_GENERAL,
					      error->message);
		g_error_free (error);

		dbus_g_method_return_error (context, error2);
		g_error_free (error2);
	}
	else
        	dbus_g_method_return (context);
out:
	start_killtimer();
}
