/*
 * Copyright (C) 2005 Red Hat Inc.
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

#include "config.h"

#include <errno.h>
#include <libintl.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <ldap.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <glib.h>

#include <gconf/gconf.h>
#include <gconf/gconf-backend.h>
#include <gconf/gconf-internals.h>

typedef struct
{
  GConfSource source;

  char *conf_file;

  char *ldap_host;
  int   ldap_port;
  char *base_dn;
  char *filter_str;

  xmlDocPtr  xml_doc;
  xmlNodePtr template_account;

  LDAP *connection;

  GConfValue *accounts_value;

  guint conf_file_parsed : 1;
} EvoSource;

static void           x_shutdown      (GError           **err);
static GConfSource   *resolve_address (const char        *address,
                                       GError           **err);
static void           lock            (GConfSource       *source,
                                       GError           **err);
static void           unlock          (GConfSource       *source,
                                       GError           **err);
static gboolean       readable        (GConfSource       *source,
                                       const char        *key,
                                       GError           **err);
static gboolean       writable        (GConfSource       *source,
                                       const char        *key,
                                       GError           **err);
static GConfValue    *query_value     (GConfSource       *source,
                                       const char        *key,
                                       const char       **locales,
                                       char             **schema_name,
                                       GError           **err);
static GConfMetaInfo *query_metainfo  (GConfSource       *source,
                                       const char        *key,
                                       GError           **err);
static void           set_value       (GConfSource       *source,
                                       const char        *key,
                                       const GConfValue  *value,
                                       GError           **err);
static GSList        *all_entries     (GConfSource       *source,
                                       const char        *dir,
                                       const char       **locales,
                                       GError           **err);
static GSList        *all_subdirs     (GConfSource       *source,
                                       const char        *dir,
                                       GError           **err);
static void           unset_value     (GConfSource       *source,
                                       const char        *key,
                                       const char        *locale,
                                       GError           **err);
static gboolean       dir_exists      (GConfSource       *source,
                                       const char        *dir,
                                       GError           **err);
static void           remove_dir      (GConfSource       *source,
                                       const char        *dir,
                                       GError           **err);
static void           set_schema      (GConfSource       *source,
                                       const char        *key,
                                       const char        *schema_key,
                                       GError           **err);
static gboolean       sync_all        (GConfSource       *source,
                                       GError           **err);
static void           destroy_source  (GConfSource       *source);
static void           clear_cache     (GConfSource       *source);
static void           blow_away_locks (const char        *address);

static GConfBackendVTable evoldap_vtable = {
  sizeof (GConfBackendVTable),
  x_shutdown,
  resolve_address,
  lock,
  unlock,
  readable,
  writable,
  query_value,
  query_metainfo,
  set_value,
  all_entries,
  all_subdirs,
  unset_value,
  dir_exists,
  remove_dir,
  set_schema,
  sync_all,
  destroy_source,
  clear_cache,
  blow_away_locks,
  NULL, /* set_notify_func */
  NULL, /* add_listener    */
  NULL  /* remove_listener */
};

static void
x_shutdown (GError **err)
{
}

static GConfSource *
resolve_address (const char  *address,
		 GError     **err)
{
  EvoSource *esource;
  char      *conf_file;

  if ((conf_file = gconf_address_resource (address)) == NULL)
    {
      g_set_error (err, GCONF_ERROR,
		   GCONF_ERROR_BAD_ADDRESS,
		   _("Failed to get configurion file path from '%s'"),
		   address);
      return NULL;
    }

  esource = g_new0 (EvoSource, 1);

  esource->conf_file    = conf_file;
  esource->source.flags = GCONF_SOURCE_ALL_READABLE | GCONF_SOURCE_NEVER_WRITEABLE;

  gconf_log (GCL_DEBUG,
	     _("Created Evolution/LDAP source using configuration file '%s'"),
	     esource->conf_file);

  return (GConfSource *) esource;
}

static void
lock (GConfSource  *source,
      GError      **err)
{
}

static void
unlock (GConfSource  *source,
        GError      **err)
{
}


static gboolean
readable (GConfSource  *source,
	  const char   *key,
	  GError      **err)
{
  return TRUE;
}

static gboolean
writable (GConfSource  *source,
	  const char   *key,
	  GError      **err)
{
  return FALSE;
}

/*
 * Taken from evolution/e-util/e-uid.c
 */
static char *
get_account_uid (void)
{
  static char *hostname;
  static int   serial;

  if (!hostname)
    {
      static char buffer [512];

      if ((gethostname (buffer, sizeof (buffer) - 1) == 0) &&
	  (buffer [0] != 0))
	hostname = buffer;
      else
	hostname = "localhost";
    }

  return g_strdup_printf ("%lu.%lu.%d@%s",
			  (unsigned long) time (NULL),
			  (unsigned long) getpid (),
			  serial++,
			  hostname);
}

static char *
get_variable (const char  *varname,
	      LDAP        *connection,
	      LDAPMessage *entry)
{
  BerElement *berptr;
  const char *attr;
  char       *retval;

  if (strcmp (varname, "USER") == 0)
    return g_strdup (g_get_user_name ());

  if (strcmp (varname, "ACCOUNT_UID") == 0)
    return get_account_uid ();

  if (connection == NULL || entry == NULL)
    return g_strdup ("");

  if (strncmp (varname, "LDAP_ATTR_", 10) != 0)
    return g_strdup ("");

  varname += 10;

  retval = NULL;

  berptr = NULL;
  attr = ldap_first_attribute (connection, entry, &berptr);
  while (attr != NULL && retval == NULL)
    {
      char **values;

      if (strcmp (attr, varname) == 0)
	{
	  values = ldap_get_values (connection, entry, attr);
	  if (values != NULL)
	    retval = g_strdup (values[0]);
	  ldap_value_free (values);
	}

      attr = ldap_next_attribute (connection, entry, berptr);
    }

  ber_free (berptr, 0);

  return retval ? retval : g_strdup ("");
}

/*
 * Copied from gconf/gconf-internals.c
 */
static char *
subst_variables (const char  *src,
		 LDAP        *connection,
		 LDAPMessage *entry)
{
  const char *iter;
  char       *retval;
  guint       retval_len;
  guint       pos;
  
  g_return_val_if_fail (src != NULL, NULL);

  retval_len = strlen (src) + 1;
  pos = 0;
  
  retval = g_malloc0 (retval_len + 3); /* add 3 just to avoid off-by-one
					  segvs - yeah I know it bugs
					  you, but C sucks */
  
  iter = src;
  while (*iter)
    {
      gboolean performed_subst = FALSE;
      
      if (pos >= retval_len)
        {
          retval_len *= 2;
          retval = g_realloc (retval, retval_len+3); /* add 3 for luck */
        }
      
      if (*iter == '$' && *(iter + 1) == '(')
        {
          const char *varstart = iter + 2;
          const char *varend   = strchr (varstart, ')');

          if (varend != NULL)
            {
              char       *varname;
              const char *varval;
              guint       varval_len;

              performed_subst = TRUE;

              varname = g_strndup (varstart, varend - varstart);
              
              varval = get_variable (varname, connection, entry);
              g_free (varname);

              varval_len = strlen (varval);

              if ((retval_len - pos) < varval_len)
                {
                  retval_len = pos + varval_len;
                  retval = g_realloc (retval, retval_len+3);
                }
              
              strcpy (&retval[pos], varval);
              pos += varval_len;

              iter = varend + 1;
            }
        }

      if (!performed_subst)
        {
          retval[pos] = *iter;
          ++pos;
          ++iter;
        }
    }

  retval[pos] = '\0';

  return retval;
}

static void
parse_server_info (xmlNodePtr   node,
		   char       **host,
		   char       **base_dn,
		   int         *port)
{
  const char *node_name = (const char *) node->name;

  g_assert (strcmp (node_name, "server") == 0);

  node = node->children;
  while (node != NULL)
    {
      node_name = (const char *) node->name;

      if (strcmp (node_name, "host") == 0)
	{
	  xmlChar *host_value;

	  host_value = xmlNodeGetContent (node);

	  if (*host != NULL)
	    g_free (*host);
	  *host = g_strdup ((char *) host_value);

	  xmlFree (host_value);
	}
      else if (strcmp (node_name, "port") == 0)
	{
	  xmlChar *port_value;

	  if ((port_value = xmlNodeGetContent (node)) != NULL)
	    {
	      char *end;
	      long  l;

	      end = NULL;
	      l = strtol ((char *) port_value, &end, 10);
	      if (end != NULL && end != (char *)port_value && *end == '\0')
		*port = (int) l;

	      xmlFree (port_value);
	    }
	}
      else if (strcmp (node_name, "base_dn") == 0)
	{
	  xmlChar *base_dn_value;

	  base_dn_value = xmlNodeGetContent (node);

	  if (*base_dn != NULL)
	    g_free (*base_dn);
	  *base_dn = g_strdup ((char *) base_dn_value);

	  if (base_dn_value != NULL)
	    xmlFree (base_dn_value);
	}

      node = node->next;
    }
}

static gboolean
parse_conf_file (EvoSource  *esource,
		 GError    **err)
{
  xmlDocPtr   doc;
  xmlNodePtr  node;
  xmlNodePtr  template;
  xmlChar    *filter_str;
  char       *contents;
  gsize       length;

  if (esource->conf_file_parsed)
    return TRUE;

  length = 0;
  contents = NULL;
  if (!g_file_get_contents (esource->conf_file, &contents, &length, err))
    return FALSE;

  doc = xmlParseMemory (contents, length);
  g_free (contents);
  if (doc == NULL)
    {
      g_set_error (err, GCONF_ERROR,
		   GCONF_ERROR_PARSE_ERROR,
		   _("Unable to parse xml file '%s'"),
		   esource->conf_file);
      return FALSE;
    }

  if (doc->children == NULL)
    {
      g_set_error (err, GCONF_ERROR,
		   GCONF_ERROR_PARSE_ERROR,
		   _("Config file '%s' is empty"),
		   esource->conf_file);
      xmlFreeDoc (doc);
      return FALSE;
    }
  
  node = doc->children;
  if (strcmp ((char *) node->name, "evoldap") != 0)
    {
      g_set_error (err, GCONF_ERROR,
		   GCONF_ERROR_PARSE_ERROR,
		   _("Root node of '%s' must be <evoldap>, not <%s>"),
		   esource->conf_file,
		   node->name);
      xmlFreeDoc (doc);
      return FALSE;
    }

  esource->xml_doc = doc;
  esource->conf_file_parsed = TRUE;

  g_assert (esource->ldap_host == NULL);
  g_assert (esource->base_dn == NULL);

  esource->ldap_port = 389; /* standard LDAP port number */

  template = NULL;
  node = node->children;
  while (node != NULL)
    {
      const char *node_name = (const char *) node->name;

      if (strcmp (node_name, "server") == 0)
	{
	  parse_server_info (node,
			     &esource->ldap_host,
			     &esource->base_dn,
			     &esource->ldap_port);
	}
      else if (strcmp (node_name, "template") == 0)
	{
	  template = node;
	}

      node = node->next;
    }

  if (template == NULL)
    {
      gconf_log (GCL_ERR, _("No <template> specified in '%s'"), esource->conf_file);
      return TRUE;
    }

  if ((filter_str = xmlGetProp (template, (xmlChar *) "filter")) == NULL)
    {
      gconf_log (GCL_ERR,
		 _("No \"filter\" attribute specified on <template> in '%s'"),
		 esource->conf_file);
      return TRUE;
    }

  esource->filter_str = subst_variables ((char *) filter_str, NULL, NULL);
  xmlFree (filter_str);

  node = template->children;
  while (node != NULL)
    {
      const char *node_name = (const char *) node->name;

      if (strcmp (node_name, "account") == 0)
	esource->template_account = node;

      node = node->next;
    }

  if (esource->template_account == NULL)
    gconf_log (GCL_ERR,
	       _("No template <account> specified in '%s'"),
	       esource->conf_file);

  return TRUE;
}

static LDAP *
get_ldap_connection (EvoSource  *esource,
		     GError    **err)
{
  LDAP *connection;

  g_assert (esource->conf_file_parsed);

  if (esource->ldap_host == NULL || esource->base_dn == NULL)
    {
      g_set_error (err, GCONF_ERROR,
		   GCONF_ERROR_FAILED,
		   _("No LDAP server or base DN specified in '%s'"),
		   esource->conf_file);
      return NULL;
    }

  gconf_log (GCL_DEBUG,
	     _("Contacting LDAP server: host '%s', port '%d', base DN '%s'"),
	     esource->ldap_host, esource->ldap_port, esource->base_dn);

  if ((connection = ldap_init (esource->ldap_host, esource->ldap_port)) == NULL)
    {
      gconf_log (GCL_ERR,
		 _("Failed to contact LDAP server: %s"),
		 g_strerror (errno));
      return NULL;
    }

  esource->connection = connection;

  return esource->connection;
}

static char *
subst_variables_into_template_account (EvoSource   *esource,
				       LDAP        *connection,
				       LDAPMessage *entry)
{
  xmlDocPtr  new_doc;
  xmlChar   *template;
  char      *retval;

  new_doc = xmlNewDoc (NULL);
  xmlDocSetRootElement (new_doc,
			xmlCopyNode (esource->template_account, 1));

  xmlDocDumpMemory (new_doc, &template, NULL);
  xmlFreeDoc (new_doc);

  retval =  subst_variables ((char *) template, connection, entry);
  xmlFree (template);

  return retval;
}

static GConfValue *
lookup_value_from_ldap (EvoSource  *esource,
			GError    **err)
{
  LDAP        *connection;
  LDAPMessage *entries;
  LDAPMessage *entry;
  GConfValue  *retval;
  GSList      *account_values;
  int          ret;

  if (!parse_conf_file (esource, err))
    return FALSE;

  if (esource->filter_str == NULL || esource->template_account == NULL)
    return NULL;

  if ((connection = get_ldap_connection (esource, err)) == NULL)
    return NULL;

  gconf_log (GCL_DEBUG,
	     _("Searching for entries using filter: %s"),
	     esource->filter_str);

  entries = NULL;
  ret = ldap_search_s (connection,
		       esource->base_dn,
		       LDAP_SCOPE_ONELEVEL,
		       esource->filter_str,
		       NULL, 0,
		       &entries);
  if (ret != LDAP_SUCCESS)
    {
      gconf_log (GCL_ERR,
		 _("Error querying LDAP server: %s"),
		 ldap_err2string (ret));
      return NULL;
    }

  g_assert (entries != NULL);

  gconf_log (GCL_DEBUG,
	     _("Got %d entries using filter: %s"),
	     ldap_count_entries (connection, entries),
	     esource->filter_str);

  account_values = NULL;

  entry = ldap_first_entry (connection, entries);
  while (entry != NULL)
    {
      GConfValue *value;
      char       *account_str;

      account_str = subst_variables_into_template_account (esource, connection, entry);

      value = gconf_value_new (GCONF_VALUE_STRING);
      gconf_value_set_string_nocopy (value, account_str);

      account_values = g_slist_append (account_values, value);

      entry = ldap_next_entry (connection, entry);
    }

  retval = NULL;
  if (account_values != NULL)
    {
      retval = gconf_value_new (GCONF_VALUE_LIST);
      gconf_value_set_list_type (retval, GCONF_VALUE_STRING);
      gconf_value_set_list_nocopy (retval, account_values);
    }

  ldap_msgfree (entries);

  return retval;
}

static GConfValue *
query_value (GConfSource  *source,
	     const char   *key,
	     const char  **locales,
	     char        **schema_name,
	     GError      **err)
{
  EvoSource *esource = (EvoSource *) source;

  if (strcmp (key, "/apps/evolution/mail/accounts") != 0)
    return NULL;

  if (schema_name != NULL)
    *schema_name = NULL;

  if (esource->accounts_value == NULL)
    esource->accounts_value = lookup_value_from_ldap (esource, err);

  return esource->accounts_value ? gconf_value_copy (esource->accounts_value): NULL;
}

static GConfMetaInfo *
query_metainfo  (GConfSource  *source,
		 const char   *key,
		 GError      **err)
{
  return NULL;
}

static void
set_value (GConfSource       *source,
	   const char        *key,
	   const GConfValue  *value,
	   GError           **err)
{
}

static GSList *
all_entries (GConfSource  *source,
	     const char   *dir,
	     const char  **locales,
	     GError      **err)
{
  GConfValue *value;

  if (strcmp (dir, "/apps/evolution/mail") != 0)
    {
      return NULL;
    }

  value = query_value (source, "/apps/evolution/mail/accounts", NULL, NULL, NULL);

  return g_slist_append (NULL,
			 gconf_entry_new_nocopy ("/apps/evolution/mail/accounts", value));
}

static GSList *
all_subdirs (GConfSource  *source,
	     const char   *dir,
	     GError      **err)
{
  if (dir[0] != '/')
    {
      return NULL;
    }

  dir++;
  if (dir[0] == '\0')
    {
      return g_slist_append (NULL, g_strdup ("apps"));
    }

  if (strncmp (dir, "apps", 4) != 0)
    {
      return NULL;
    }

  dir += 4;
  if (dir[0] == '\0')
    {
      return g_slist_append (NULL, g_strdup ("evolution"));
    }

  if (strncmp (dir, "/evolution", 10) != 0)
    {
      return NULL;
    }

  dir += 10;
  if (dir[0] == '\0')
    {
      return g_slist_append (NULL, g_strdup ("mail"));
    }

  return NULL;
}

static void
unset_value (GConfSource  *source,
	     const char   *key,
	     const char   *locale,
	     GError      **err)
{
}

static gboolean
dir_exists (GConfSource  *source,
	    const char   *dir,
	    GError      **err)
{
  if (strcmp (dir, "/apps/evolution/mail/local") == 0)
    return TRUE;

  return FALSE;
}

static void
remove_dir (GConfSource  *source,
	    const char   *dir,
	    GError      **err)
{
}

static void
set_schema (GConfSource  *source,
	    const char   *key,
	    const char   *schema_key,
	    GError      **err)
{
}

static gboolean
sync_all (GConfSource *source,
	  GError      **err)
{
  return TRUE;
}

static void
destroy_source (GConfSource *source)
{
  EvoSource *esource = (EvoSource *) source;

  esource->connection = NULL;

  if (esource->accounts_value != NULL)
    gconf_value_free (esource->accounts_value);
  esource->accounts_value = NULL;

  if (esource->xml_doc != NULL)
    xmlFreeDoc (esource->xml_doc);
  esource->xml_doc = NULL;
  esource->template_account = NULL;

  if (esource->filter_str != NULL)
    g_free (esource->filter_str);
  esource->filter_str = NULL;

  if (esource->ldap_host != NULL)
    g_free (esource->ldap_host);
  esource->ldap_host = NULL;

  if (esource->base_dn != NULL)
    g_free (esource->base_dn);
  esource->base_dn = NULL;

  if (esource->conf_file != NULL)
    g_free (esource->conf_file);
  esource->conf_file = NULL;

  g_free (esource);
}

static void
clear_cache (GConfSource *source)
{
}

static void
blow_away_locks (const char *address)
{
}

GConfBackendVTable *gconf_backend_get_vtable (void);

G_MODULE_EXPORT GConfBackendVTable *
gconf_backend_get_vtable (void)
{
  return &evoldap_vtable;
}
