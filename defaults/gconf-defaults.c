/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008, 2009 Matthias Clasen <mclasen@redhat.com>
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
#include <gio/gio.h>

#include <polkit/polkit.h>

#define GCONF_ENABLE_INTERNALS
#include <gconf/gconf-client.h>
#include <gconf/gconf-engine.h>

#include "gconf-defaults.h"

static const char introspection_xml[] =
  "<node name='/'>"
    "<interface name='org.gnome.GConf.Defaults'>"
#if 0
      "<!--"
        "includes: an array of GConf paths to copy from the"
        "callers GConf database to the system database"
        "excludes: an array of GConf paths to omit"

        "Copies values from the callers GConf database to the system-wide"
        "database. The subtree below each included path is copied recursively,"
        "skipping the excluded subtrees."
        "To decide which PolicyKit privilege to require for the copying of"
        "each path in includes, the mechanism looks for a privilege with an"
        "annotation with key org.gnome.gconf.defaults.set-system.prefix whose"
        "value is a prefix of the path. If no privilege is found this way, the"
        "org.gnome.gconf.defaults.set-system privilege is required."
        "-->"
#endif
      "<method name='SetSystem'>"
        "<arg name='includes' direction='in' type='as'/>"
        "<arg name='excludes' direction='in' type='as'/>"
      "</method>"
#if 0
      "<!--"
        "path: a path to a gconf key"
        "value: a value, as encoded by gconf_value_encode"

        "Sets the key at path to value in the system-wide database."
        "To decide which PolicyKit privilege to require for the copying of"
        "each path in includes, the mechanism looks for a privilege with an"
        "annotation with key org.gnome.gconf.defaults.set-system.prefix whose"
        "value is a prefix of the path. If no privilege is found this way, the"
        "org.gnome.gconf.defaults.set-system privilege is required."
        "-->"
#endif
      "<method name='SetSystemValue'>"
        "<arg name='path'  direction='in' type='s'/>"
        "<arg name='value' direction='in' type='s'/>"
      "</method>"
      "<method name='CanSetSystem'>"
        "<arg name='includes' direction='in' type='as'/>"
        "<arg name='result' direction='out' type='u'/>"
      "</method>"
      "<signal name='SystemSet'>"
        "<arg name='keys' type='as'/>"
      "</signal>"
#if 0
      "<!--"
        "includes: an array of GConf paths to copy from the"
        "callers GConf database to the mandatory database"
        "excludes: an array of GConf paths to omit"

        "Copies values from the callers GConf database to the system-wide"
        "mandatory database. The subtree below each included path is copied"
        "recursively, skipping the excluded subtrees."
        "To decide which PolicyKit privilege to require for the copying of"
        "each path in includes, the mechanism looks for a privilege with an"
        "annotation with key org.gnome.gconf.defaults.set-mandatory.prefix whose"
        "value is a prefix of the path. If no privilege is found this way, the"
        "org.gnome.gconf.defaults.set-mandatory privilege is required."
        "-->"
#endif
      "<method name='SetMandatory'>"
        "<arg name='includes' direction='in' type='as'/>"
        "<arg name='excludes' direction='in' type='as'/>"
      "</method>"
#if 0
      "<!--"
        "path: a path to a gconf key"
        "value: a value, as encoded by gconf_value_encode"

        "Sets the key at path to value in the system-wide mandatory database."
        "To decide which PolicyKit privilege to require for the copying of"
        "each path in includes, the mechanism looks for a privilege with an"
        "annotation with key org.gnome.gconf.defaults.set-mandatory.prefix whose"
        "value is a prefix of the path. If no privilege is found this way, the"
        "org.gnome.gconf.defaults.set-mandatory privilege is required."
        "-->"
#endif
      "<method name='SetMandatoryValue'>"
        "<arg name='path'  direction='in' type='s'/>"
        "<arg name='value' direction='in' type='s'/>"
      "</method>"
#if 0
      "<!--"
        "Unsets keys in the system-wide mandatory GConf database, making the"
        "keys writable again. The subtree below each included path is copied"
        "recursively, skipping the excluded subtrees."
        "To decide which PolicyKit privilege to require for the copying of"
        "each path in includes, the mechanism looks for a privilege with an"
        "annotation with key org.gnome.gconf.defaults.set-mandatory.prefix whose"
        "value is a prefix of the path. If no privilege is found this way, the"
        "org.gnome.gconf.defaults.set-mandatory privilege is required."
        "-->"
#endif
      "<method name='UnsetMandatory'>"
        "<arg name='includes' direction='in' type='as'/>"
        "<arg name='excludes' direction='in' type='as'/>"
      "</method>"
      "<method name='CanSetMandatory'>"
        "<arg name='includes' direction='in' type='as'/>"
        "<arg name='result' direction='out' type='u'/>"
      "</method>"
    "</interface>"
  "</node>";

static gboolean
do_exit (gpointer user_data)
{
        g_debug ("Exiting due to inactivity");
        exit (1);
        return FALSE;
}

static guint timer_id = 0;
gboolean disable_killtimer = FALSE;

static void
stop_killtimer (void)
{
	if (disable_killtimer)
		return;

        if (timer_id > 0) {
                g_source_remove (timer_id);
		timer_id = 0;
        }
}

static void
start_killtimer (void)
{
	if (disable_killtimer)
		return;

	if (timer_id == 0) {
        	g_debug ("Setting killtimer to 30 seconds...");
        	timer_id = g_timeout_add_seconds (30, do_exit, NULL);
	}
}

static gint operations = 0;

static void
start_operation (void)
{
	if (operations == 0)
		stop_killtimer ();
	operations++;
}

static void
stop_operation (void)
{
	if (operations == 1)
		start_killtimer ();
	operations --;
}

struct GConfDefaultsPrivate
{
        GDBusConnection  *system_bus_connection;
        PolkitAuthority *auth;
        guint registration_id;
};

static void gconf_defaults_finalize (GObject *object);

G_DEFINE_TYPE (GConfDefaults, gconf_defaults, G_TYPE_OBJECT)

#define GCONF_DEFAULTS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GCONF_TYPE_DEFAULTS, GConfDefaultsPrivate))

GQuark
gconf_defaults_error_quark (void)
{
        static volatile gsize quark_volatile = 0;

        if (quark_volatile == 0) {
                static const GDBusErrorEntry error_entries[] =
                {
                        { GCONF_DEFAULTS_ERROR_GENERAL,        "org.gnome.GConf.Defaults.Error.GeneralError"  },
                        { GCONF_DEFAULTS_ERROR_NOT_PRIVILEGED, "org.gnome.GConf.Defaults.Error.NotPrivileged" }
                };
           
                g_dbus_error_register_error_domain ("gconf-defaults-error-quark",
                                                    &quark_volatile,
                                                    error_entries,
                                                    G_N_ELEMENTS (error_entries));
        }

        return (GQuark) quark_volatile;
}

#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
gconf_defaults_error_get_type (void)
{
        static GType etype = 0;

        if (etype == 0)
        {
                static const GEnumValue values[] =
                        {
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

static void
method_call_cb (GDBusConnection       *connection,
                const gchar           *sender,
                const gchar           *object_path,
                const gchar           *interface_name,
                const gchar           *method_name,
                GVariant              *parameters,
                GDBusMethodInvocation *invocation,
                gpointer               user_data)
{
        GConfDefaults *mechanism = GCONF_DEFAULTS (user_data);

        if (g_strcmp0 (interface_name, "org.gnome.GConf.Defaults") != 0)
                return;

        if (g_strcmp0 (method_name, "SetSystem") == 0) {
                char **includes, **excludes;

                g_variant_get (parameters, "(^a&s^a&s)", &includes, &excludes);
                gconf_defaults_set_system (mechanism, (const char **) includes, (const char**) excludes, invocation);
                g_free (includes);
                g_free (excludes);
        } else if (g_strcmp0 (method_name, "SetSystemValue") == 0) {
                const char *path, *value;

                g_variant_get (parameters, "(&s&s)", &path, &value);
                gconf_defaults_set_system_value (mechanism, path, value, invocation);
        } else if (g_strcmp0 (method_name, "CanSetSystem") == 0) {
                char **includes;

                g_variant_get (parameters, "(^a&s)", &includes);
                gconf_defaults_can_set_system (mechanism, (const char **) includes, invocation);
                g_free (includes);
        } else if (g_strcmp0 (method_name, "SetMandatory") == 0) {
                char **includes, **excludes;

                g_variant_get (parameters, "(^a&s^a&s)", &includes, &excludes);
                gconf_defaults_set_mandatory (mechanism, (const char **) includes, (const char**) excludes, invocation);
                g_free (includes);
                g_free (excludes);
        } else if (g_strcmp0 (method_name, "UnsetMandatory") == 0) {
                char **includes, **excludes;

                g_variant_get (parameters, "(^a&s^a&s)", &includes, &excludes);
                gconf_defaults_unset_mandatory (mechanism, (const char **) includes, (const char**) excludes, invocation);
                g_free (includes);
                g_free (excludes);
        } else if (g_strcmp0 (method_name, "SetMandatoryValue") == 0) {
                const char *path, *value;

                g_variant_get (parameters, "(&s&s)", &path, &value);
                gconf_defaults_set_mandatory_value (mechanism, path, value, invocation);
        } else if (g_strcmp0 (method_name, "CanSetMandatory") == 0) {
                char **includes;

                g_variant_get (parameters, "(^a&s)", &includes);
                gconf_defaults_can_set_mandatory (mechanism, (const char **) includes, invocation);
                g_free (includes);
        }
}

static const GDBusInterfaceVTable interface_vtable = {
        method_call_cb,
        NULL,
        NULL
};

static void
gconf_defaults_class_init (GConfDefaultsClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->constructor = gconf_defaults_constructor;
        object_class->finalize = gconf_defaults_finalize;

        g_type_class_add_private (klass, sizeof (GConfDefaultsPrivate));
}

static void
gconf_defaults_init (GConfDefaults *mechanism)
{
        mechanism->priv = GCONF_DEFAULTS_GET_PRIVATE (mechanism);
}

static void
gconf_defaults_finalize (GObject *object)
{
        GConfDefaults *mechanism = GCONF_DEFAULTS (object);
        GConfDefaultsPrivate *priv = mechanism->priv;

        if (priv->registration_id != 0) {
                g_dbus_connection_unregister_object (priv->system_bus_connection, priv->registration_id);
        }

	g_object_unref (priv->auth);
        g_object_unref (priv->system_bus_connection);

        G_OBJECT_CLASS (gconf_defaults_parent_class)->finalize (object);
}

static gboolean
register_mechanism (GConfDefaults *mechanism,
                    GDBusConnection *connection)
{
        GConfDefaultsPrivate *priv = mechanism->priv;
        GDBusNodeInfo *introspection_data;
        GError *error = NULL;

        priv->auth = polkit_authority_get ();
        priv->system_bus_connection = g_object_ref (connection);

        introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
        g_assert (introspection_data != NULL);

        priv->registration_id = g_dbus_connection_register_object
                                    (connection,
                                     "/",
                                     introspection_data->interfaces[0],
                                     &interface_vtable,
                                     mechanism,
                                     (GDestroyNotify) g_object_unref,
                                     &error);
        if (priv->registration_id == 0) {
                g_warning ("Failed to register object: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        start_killtimer ();

        return TRUE;
}

GConfDefaults *
gconf_defaults_new (GDBusConnection *connection)
{
        GObject *object;
        gboolean res;

        object = g_object_new (GCONF_TYPE_DEFAULTS, NULL);

        res = register_mechanism (GCONF_DEFAULTS (object), connection);
        if (! res) {
                g_object_unref (object);
                return NULL;
        }

        return GCONF_DEFAULTS (object);
}

static const char *
polkit_action_for_gconf_path (GConfDefaults *mechanism,
			      GList         *action_descriptions,
			      const char    *annotation_key,
			      const char    *path)
{
	char *prefix, *p;
	const char *action;
        GList *l;
        PolkitActionDescription *action_description;
	const gchar *annotation;

	g_debug ("finding action for path '%s'", path);
	prefix = g_strdup (path);
	while (1) {
                for (l = action_descriptions; l; l = l->next) {
			action_description = l->data;

			annotation = polkit_action_description_get_annotation (action_description, annotation_key);
			if (g_strcmp0 (prefix, annotation) == 0) {
				action = polkit_action_description_get_action_id (action_description);
				g_debug ("action for prefix '%s': '%s'\n", prefix, action);
				goto found;
			}
		}

		p = strrchr (prefix, '/');

		if (p == NULL || p == prefix) {
			action = NULL;
			break;
		}

		*p = 0;
	}

 found:
	g_free (prefix);

	return action;
}

typedef void (*AuthObtainedCallback) (GConfDefaults          *mechanism,
                                      GDBusMethodInvocation  *context,
                                      gpointer                user_data);

typedef struct
{
	GConfDefaults                   *mechanism;
	GDBusMethodInvocation           *context;
	gchar                          **actions;
	gint				 id;
	gint				 flags;
	AuthObtainedCallback             auth_obtained_callback;
	GAsyncReadyCallback		 check_auth_callback;
	gpointer                         user_data;
	GDestroyNotify                   destroy;
	PolkitSubject			*subject;
	gboolean			 challenge;
} CheckAuthData;

static void
check_auth_data_free (CheckAuthData *data)
{
	g_object_unref (data->mechanism);
	g_strfreev (data->actions);
	if (data->destroy)
		data->destroy (data->user_data);
        g_object_unref (data->subject);
        g_object_unref (data->context);
	g_free (data);
}

static void check_next_action (CheckAuthData *data);

static void
check_authorization_callback (PolkitAuthority *authority,
                              GAsyncResult    *res,
                              gpointer         user_data)
{
	CheckAuthData *data = user_data;
	PolkitAuthorizationResult *result;
	GError *error;
	gboolean is_authorized;

	is_authorized = FALSE;

	error = NULL;
	result = polkit_authority_check_authorization_finish (authority,
							      res,
							      &error);
	if (error != NULL) {
		g_debug ("error checking action '%s'\n", error->message);
                g_dbus_method_invocation_return_error (data->context,
                                                       GCONF_DEFAULTS_ERROR,
                                                       GCONF_DEFAULTS_ERROR_NOT_PRIVILEGED,
                                                       "Not Authorized: %s", error->message);
		g_error_free (error);
	}
	else {
		if (polkit_authorization_result_get_is_authorized (result)) {
			g_debug ("result for '%s': authorized\n",
				 data->actions[data->id]);
			is_authorized = TRUE;
		}
		else if (polkit_authorization_result_get_is_challenge (result)) {
			g_debug ("result for '%s': challenge\n",
				 data->actions[data->id]);
                        g_dbus_method_invocation_return_error_literal (data->context,
                                                                       GCONF_DEFAULTS_ERROR,
                                                                       GCONF_DEFAULTS_ERROR_NOT_PRIVILEGED,
                                                                       "Authorization is required");
		}
		else {
			g_debug ("result for '%s': not authorized\n",
				 data->actions[data->id]);
                        g_dbus_method_invocation_return_error_literal (data->context,
                                                                       GCONF_DEFAULTS_ERROR,
                                                                       GCONF_DEFAULTS_ERROR_NOT_PRIVILEGED,
                                                                       "Not Authorized");
		}
	}

	if (is_authorized) {
		data->id++;
		if (data->actions[data->id] == NULL)
			data->auth_obtained_callback (data->mechanism,
					   	      data->context,
						      data->user_data);
		else {
			check_next_action (data);
			return; /* continue operation */
		}
	}

	check_auth_data_free (data);
	g_object_unref (result);
	stop_operation ();
}

static void
check_next_action (CheckAuthData *data)
{
	g_debug ("checking action '%s'\n", data->actions[data->id]);
        polkit_authority_check_authorization (data->mechanism->priv->auth,
                                              data->subject,
                                              data->actions[data->id],
					      NULL,
					      data->flags,
                                              NULL,
                                              data->check_auth_callback,
                                              data);
}

static void
check_polkit_for_actions (GConfDefaults                   *mechanism,
                          GDBusMethodInvocation           *context,
                          gchar                          **actions,
                          AuthObtainedCallback             auth_obtained_callback,
                          gpointer                         user_data,
			  GDestroyNotify                   destroy)
{
        CheckAuthData *data;

	data = g_new0 (CheckAuthData, 1);
	data->mechanism = g_object_ref (mechanism);
	data->context = g_object_ref (context);
	data->actions = actions;
        data->flags = POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION;
	data->id = 0;
	data->auth_obtained_callback = auth_obtained_callback;
	data->check_auth_callback = (GAsyncReadyCallback)check_authorization_callback;
	data->user_data = user_data;
	data->destroy = destroy;
	data->subject = polkit_system_bus_name_new (g_dbus_method_invocation_get_sender (context));
	data->challenge = FALSE;

	check_next_action (data);
}

static char *
gconf_address_for_caller (GConfDefaults          *mechanism,
			  GDBusMethodInvocation  *context)
{
        GDBusConnection *connection;
        GCredentials *credentials;
	uid_t uid;
	struct passwd *pwd;
	char *result;
        GError *error = NULL;

        connection = g_dbus_method_invocation_get_connection (context);
        credentials = g_dbus_connection_get_peer_credentials (connection);
        if (credentials == NULL) {
                g_dbus_method_invocation_return_error_literal (context,
                                                               GCONF_DEFAULTS_ERROR,
                                                               GCONF_DEFAULTS_ERROR_GENERAL,
                                                               "Failed to get credentials");
                return NULL;
        }

        uid = g_credentials_get_unix_user (credentials, &error);
        g_object_unref (credentials);
        if (error) {
                g_dbus_method_invocation_return_gerror (context, error);
                g_error_free (error);
                return NULL;
        }

	pwd = getpwuid (uid);
	if (pwd == NULL) {
                g_dbus_method_invocation_return_error (context,
                                                       GCONF_DEFAULTS_ERROR,
                                                       GCONF_DEFAULTS_ERROR_GENERAL,
                                                       "Failed to get passwd information for uid %d", (int) uid);
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

typedef void (*ChangeSetCallback) (GConfDefaults  *mechanism,
                                   GConfChangeSet *changes,
                                   gpointer        data);

typedef struct
{
	GConfDefaults                   *mechanism;
	GDBusMethodInvocation           *context;
	const char 			*dest_address;
	char 			       **actions;
	char            	       **includes;
	char            	       **excludes;
        GConfValue                      *value;
	ChangeSetCallback 		 changeset_callback;
	gpointer			 user_data;
	GDestroyNotify			 destroy;
} CopyData;

static void
copy_data_free (gpointer user_data)
{
	CopyData *data = user_data;

	g_object_unref (data->mechanism);
	g_strfreev (data->includes);
	g_strfreev (data->excludes);
	g_strfreev (data->actions);
	if (data->value)
		gconf_value_free (data->value);
	if (data->destroy)
		data->destroy (data->user_data);
        g_object_unref (data->context);
	g_free (data);
}

static void
do_copy_authorized (GConfDefaults          *mechanism,
                    GDBusMethodInvocation  *context,
		    gpointer                user_data)
{
        CopyData    *data = user_data;
	GConfClient *source = NULL;
	GConfClient *dest = NULL;
	GConfChangeSet *changes = NULL;
	GConfEngine *engine;
        char *address = NULL;
        gint i;
	GError *error;

	/* find the address to from the caller id */
        address = gconf_address_for_caller (data->mechanism, data->context);
        if (address == NULL)
                return;

	error = NULL;
	engine = gconf_engine_get_local (data->dest_address, &error);
	if (error)
		goto cleanup;

	dest = gconf_client_get_for_engine (engine);
	gconf_engine_unref (engine);

	engine = gconf_engine_get_local (address, &error);
	if (error)
		goto cleanup;

	source = gconf_client_get_for_engine (engine);
	gconf_engine_unref (engine);

	changes = gconf_change_set_new ();

	if (data->value) {
		g_assert (data->includes[1] == NULL);
                g_assert (data->excludes == NULL);

		gconf_change_set_set (changes, data->includes[0], data->value);
	}
	else {
	 	/* recursively copy each include, leaving out the excludes */
		for (i = 0; data->includes[i]; i++) {
			if (gconf_client_dir_exists (source, data->includes[i], NULL))
				copy_tree (source, data->includes[i], changes, (const char **)data->excludes);
			else
				copy_entry (source, data->includes[i], changes, (const char **)data->excludes);
		}
	}

	gconf_client_commit_change_set (dest, changes, FALSE, &error);
	gconf_client_suggest_sync (dest, NULL);

	if (data->changeset_callback) {
		data->changeset_callback (data->mechanism, changes, data->user_data);
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
                g_dbus_method_invocation_return_error_literal (data->context,
                                                               GCONF_DEFAULTS_ERROR,
                                                               GCONF_DEFAULTS_ERROR_GENERAL,
                                                               error->message);
		g_error_free (error);
	}
	else
        	g_dbus_method_invocation_return_value (data->context, NULL);
}

typedef void (*ActionsReadyCallback) (GConfDefaults          *mechanism,
				      GDBusMethodInvocation  *context,
				      gchar                 **actions,
                          	      AuthObtainedCallback    auth_obtained_callback,
				      gpointer                data,
				      GDestroyNotify          destroy);

typedef struct
{
	GConfDefaults 			*mechanism;
	GDBusMethodInvocation           *context;
	char                           **includes;
	const char			*default_action;
	const char			*annotation_key;
        ActionsReadyCallback		 actions_ready_callback;
	AuthObtainedCallback             auth_obtained_callback;
	gpointer			 data;
	GDestroyNotify			 destroy;
} ActionData;

static void
action_data_free (ActionData *data)
{
	g_object_unref (data->mechanism);
	g_strfreev (data->includes);
	if (data->destroy)
		data->destroy (data->data);
        g_object_unref (data->context);
	g_free (data);
}

static void
actions_ready_cb (GObject      *source,
		  GAsyncResult *res,
		  gpointer      user_data)
{
	ActionData *data = user_data;
	GList *action_descriptions;
	GError *error = NULL;
	int i;
	GHashTable *obtained;
	GHashTableIter iter;
	const gchar *action;
	gchar **actions;
	gpointer key, value;

	action_descriptions = polkit_authority_enumerate_actions_finish (data->mechanism->priv->auth, res, &error);

	if (error) {
                g_dbus_method_invocation_return_error (data->context,
                                                       GCONF_DEFAULTS_ERROR,
                                                       GCONF_DEFAULTS_ERROR_GENERAL,
                                                       "Failed to get action descriptions: %s", error->message);
		g_error_free (error);
		action_data_free (data);
		stop_operation ();
		return;
	}

	obtained = g_hash_table_new (g_str_hash, g_str_equal);

	for (i = 0; data->includes[i]; i++) {
		action = polkit_action_for_gconf_path (data->mechanism, action_descriptions, data->annotation_key, data->includes[i]);
		if (action == NULL) {
			g_debug ("using default action '%s' for path '%s'",
				 data->default_action, data->includes[i]);
			action = data->default_action;
		}

		g_hash_table_insert (obtained, (gpointer)action, (gpointer)action);
	}
	actions = g_new0 (char *, g_hash_table_size (obtained) + 1);
	g_hash_table_iter_init (&iter, obtained);
	i = 0;
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		actions[i] = g_strdup ((char *)key);
		i++;
	}
	g_hash_table_destroy (obtained);
	g_list_foreach (action_descriptions, (GFunc)g_object_unref, NULL);
	g_list_free (action_descriptions);

	data->actions_ready_callback (data->mechanism, data->context, actions, data->auth_obtained_callback, data->data, data->destroy);

	data->destroy = NULL;
	action_data_free (data);
}

static void
do_copy (GConfDefaults          *mechanism,
	 gboolean                mandatory,
	 const gchar           **includes,
	 const gchar           **excludes,
	 GConfValue             *value,
	 GDBusMethodInvocation  *context,
	 ChangeSetCallback       changeset_callback,
	 gpointer                user_data,
         GDestroyNotify          destroy)
{
	CopyData *cdata;
	ActionData *adata;

        start_operation ();

	cdata = g_new0 (CopyData, 1);
	cdata->mechanism = g_object_ref (mechanism);
	cdata->context = g_object_ref (context);
	cdata->includes = g_strdupv ((gchar **)includes);
	cdata->excludes = g_strdupv ((gchar **)excludes);
        cdata->value = value;
	cdata->actions = NULL;
	cdata->changeset_callback = changeset_callback;
	cdata->user_data = user_data;
	cdata->destroy = destroy;

	adata = g_new0 (ActionData, 1);
	adata->mechanism = g_object_ref (mechanism);
	adata->context = g_object_ref (context);
	adata->includes = g_strdupv ((gchar **)includes);
	adata->actions_ready_callback = check_polkit_for_actions;
	adata->auth_obtained_callback = do_copy_authorized;
	adata->data = cdata;
	adata->destroy = copy_data_free;

	/* check privileges for each include */
	if (mandatory) {
		adata->annotation_key = "org.gnome.gconf.defaults.set-mandatory.prefix";
		adata->default_action = "org.gnome.gconf.defaults.set-mandatory";
		cdata->dest_address = "xml:merged:" SYSGCONFDIR "/gconf.xml.mandatory";
	}
	else {
		adata->annotation_key = "org.gnome.gconf.defaults.set-system.prefix";
		adata->default_action = "org.gnome.gconf.defaults.set-system";
		cdata->dest_address = "xml:merged:" SYSGCONFDIR "/gconf.xml.system";
	}

        polkit_authority_enumerate_actions (mechanism->priv->auth,
				            NULL,
				            actions_ready_cb,
				            adata);
}

static void
append_key (GConfChangeSet *cs,
	    const gchar *key,
	    GConfValue *value,
	    gpointer user_data)
{
	GVariantBuilder *builder = (GVariantBuilder *) user_data;

        g_variant_builder_add (builder, "s", key);
}

static void
set_system_changes (GConfDefaults  *mechanism,
		    GConfChangeSet *changes,
                    gpointer        data)
{
        GConfDefaultsPrivate *priv = mechanism->priv;
	GVariantBuilder builder;

	g_variant_builder_init (&builder, G_VARIANT_TYPE ("as"));
	gconf_change_set_foreach (changes, append_key, &builder);

        g_dbus_connection_emit_signal (priv->system_bus_connection,
                                       NULL /* FIXME? */,
                                       "/",
                                       "org.gnome.GConf.Defaults",
                                       "SystemSet",
                                       g_variant_builder_end (&builder),
                                       NULL);
}

void
gconf_defaults_set_system (GConfDefaults          *mechanism,
			   const char            **includes,
			   const char            **excludes,
			   GDBusMethodInvocation  *context)
{
	do_copy (mechanism, FALSE, includes, excludes, NULL, context, set_system_changes, NULL, NULL);
}

void
gconf_defaults_set_mandatory (GConfDefaults          *mechanism,
                              const char            **includes,
                              const char            **excludes,
                              GDBusMethodInvocation  *context)
{
	do_copy (mechanism, TRUE, includes, excludes, NULL, context, NULL, NULL, NULL);
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
unset_in_db (GConfDefaults   *mechanism,
	     const gchar     *address,
             const gchar    **includes,
             const gchar    **excludes,
	     GError         **error)
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

typedef struct
{
	GConfDefaults          *mechanism;
        GDBusMethodInvocation  *context;
        char                  **includes;
        char                  **excludes;
} UnsetData;

static void
unset_data_free (gpointer user_data)
{
	UnsetData *data = user_data;

	g_object_unref (data->mechanism);
	g_strfreev (data->includes);
	g_strfreev (data->excludes);
        g_object_unref (data->context);
	g_free (data);
}

static void
do_unset_authorized (GConfDefaults          *mechanism,
                     GDBusMethodInvocation  *context,
		     gpointer 		     user_data)
{
        UnsetData *data = user_data;
	GError *error;

	error = NULL;
	unset_in_db (mechanism, "xml:merged:" SYSGCONFDIR "/gconf.xml.mandatory", 
		     (const gchar **)data->includes,
		     (const gchar **)data->excludes, &error);

	if (error) {
                g_dbus_method_invocation_return_error_literal (data->context,
                                                               GCONF_DEFAULTS_ERROR,
                                                               GCONF_DEFAULTS_ERROR_GENERAL,
                                                               error->message);
		g_error_free (error);
	}
	else
        	g_dbus_method_invocation_return_value (data->context, NULL);
}

void
gconf_defaults_unset_mandatory (GConfDefaults          *mechanism,
                                const char            **includes,
                                const char            **excludes,
                                GDBusMethodInvocation  *context)
{
	UnsetData *udata;
	ActionData *adata;

	start_operation ();

	udata = g_new0 (UnsetData, 1);
	udata->mechanism = g_object_ref (mechanism);
	udata->context = g_object_ref (context);
	udata->includes = g_strdupv ((gchar **)includes);
	udata->excludes = g_strdupv ((gchar **)excludes);

	adata = g_new0 (ActionData, 1);
	adata->mechanism = g_object_ref (mechanism);
	adata->context = g_object_ref (context);
	adata->includes = g_strdupv ((gchar **)includes);
	adata->auth_obtained_callback = do_unset_authorized;
	adata->data = udata;
	adata->destroy = unset_data_free;

	adata->annotation_key = "org.gnome.gconf.defaults.set-mandatory.prefix";
	adata->default_action = "org.gnome.gconf.defaults.set-mandatory";

	polkit_authority_enumerate_actions (mechanism->priv->auth,
					    NULL,
					    actions_ready_cb,
					    adata);
}

static void
check_authorization_only_callback (PolkitAuthority *authority,
                                   GAsyncResult    *res,
                                   gpointer         user_data)
{
	CheckAuthData *data = user_data;
	PolkitAuthorizationResult *result;
	GError *error;
	gboolean is_authorized;

	is_authorized = FALSE;

	error = NULL;
	result = polkit_authority_check_authorization_finish (authority,
							      res,
							      &error);
	if (error != NULL) {
		g_debug ("error checking action '%s'\n", error->message);
		g_dbus_method_invocation_return_error (data->context,
                                                       GCONF_DEFAULTS_ERROR,
                                                       GCONF_DEFAULTS_ERROR_NOT_PRIVILEGED,
                                                       "Not Authorized: %s", error->message);
		g_error_free (error);
		goto out;
	}
	else {
		if (polkit_authorization_result_get_is_authorized (result)) {
                        g_debug ("result for '%s': authorized\n",
                                 data->actions[data->id]);
			is_authorized = TRUE;
		}
		else if (polkit_authorization_result_get_is_challenge (result)) {
			g_debug ("result for '%s': challenge\n",
                                 data->actions[data->id]);
			is_authorized = TRUE;
			data->challenge = TRUE;
		}
		else {
			g_debug ("result for '%s': not authorized\n",
                                 data->actions[data->id]);
			is_authorized = FALSE;
		}
	}

	if (is_authorized) {
		data->id++;
		if (data->actions[data->id] == NULL) {
			gint result;

			result = data->challenge ? 1 : 2;
			g_debug ("return %d\n", result);
			g_dbus_method_invocation_return_value (data->context, g_variant_new ("(u)", (guint32) result));
		}
		else {
			check_next_action (data);
			return; /* continue operation */
		}
	}
	else {
		g_debug ("return 0\n");
		g_dbus_method_invocation_return_value (data->context, g_variant_new ("(u)", 0));
	}

out:
	check_auth_data_free (data);
	g_object_unref (result);
	stop_operation ();
}

static void
check_permissions_only (GConfDefaults                   *mechanism,
                        GDBusMethodInvocation           *context,
                        gchar                          **actions,
                        AuthObtainedCallback             auth_obtained_callback,
                        gpointer                         user_data,
 	 		GDestroyNotify                   destroy)
{
        CheckAuthData *data;

	data = g_new0 (CheckAuthData, 1);
	data->mechanism = g_object_ref (mechanism);
	data->context = g_object_ref (context);
	data->actions = actions;
	data->flags = 0;
	data->id = 0;
	data->check_auth_callback = (GAsyncReadyCallback)check_authorization_only_callback;
	data->auth_obtained_callback = NULL;
	data->user_data = NULL;
	data->destroy = NULL;
	data->subject = polkit_system_bus_name_new (g_dbus_method_invocation_get_sender (context));
	data->challenge = FALSE;

	check_next_action (data);
}

static void
do_check (GConfDefaults          *mechanism,
          gboolean                mandatory,
          const gchar           **includes,
          GDBusMethodInvocation  *context)
{
	ActionData *adata;

	start_operation ();

	adata = g_new0 (ActionData, 1);
	adata->mechanism = g_object_ref (mechanism);
	adata->context = g_object_ref (context);
	adata->includes = g_strdupv ((gchar **)includes);
	adata->actions_ready_callback = check_permissions_only;
	adata->auth_obtained_callback = NULL;
	adata->data = NULL;
	adata->destroy = NULL;

	if (mandatory) {
		adata->annotation_key = "org.gnome.gconf.defaults.set-mandatory.prefix";
		adata->default_action = "org.gnome.gconf.defaults.set-mandatory";
	}
	else {
		adata->annotation_key = "org.gnome.gconf.defaults.set-system.prefix";
		adata->default_action = "org.gnome.gconf.defaults.set-system";
	}

	polkit_authority_enumerate_actions (mechanism->priv->auth,
					    NULL,
					    actions_ready_cb,
					    adata);
}

void
gconf_defaults_can_set_system (GConfDefaults          *mechanism,
			       const char            **includes,
			       GDBusMethodInvocation  *context)
{
	do_check (mechanism, FALSE, includes, context);
}

void
gconf_defaults_can_set_mandatory (GConfDefaults          *mechanism,
			          const char            **includes,
			          GDBusMethodInvocation  *context)
{
	do_check (mechanism, TRUE, includes, context);
}

void
gconf_defaults_set_system_value (GConfDefaults         *mechanism,
                                 const char            *path,
                                 const char            *value,
                                 GDBusMethodInvocation *context)
{
	GConfValue *gvalue;
 	const char *includes[] = { NULL, NULL };

	gvalue = gconf_value_decode (value);
	if (gvalue) {
		includes[0] = path;
		do_copy (mechanism, FALSE, includes, NULL, gvalue, context, set_system_changes, NULL, NULL);
	} else {
                g_dbus_method_invocation_return_error_literal (context,
                                                               GCONF_DEFAULTS_ERROR,
                                                               GCONF_DEFAULTS_ERROR_GENERAL,
                                                               "Failed to parse value as GConf value");
        }
}

void
gconf_defaults_set_mandatory_value (GConfDefaults         *mechanism,
                                    const char            *path,
                                    const char            *value,
                                    GDBusMethodInvocation *context)
{
	GConfValue *gvalue;
 	const char *includes[] = { NULL, NULL };

	gvalue = gconf_value_decode (value);
	if (gvalue) {
		includes[0] = path;
		do_copy (mechanism, TRUE, includes, NULL, gvalue, context, NULL, NULL, NULL);
	} else {
                g_dbus_method_invocation_return_error_literal (context,
                                                               GCONF_DEFAULTS_ERROR,
                                                               GCONF_DEFAULTS_ERROR_GENERAL,
                                                               "Failed to parse value as GConf value");
        }
}
