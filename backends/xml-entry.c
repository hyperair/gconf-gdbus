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

#include "xml-entry.h"
#include <gconf/gconf-internals.h>
#include <stdlib.h>

/* Quick hack so I can mark strings */

#ifdef _ 
#warning "_ already defined"
#else
#define _(x) x
#endif

#ifdef N_ 
#warning "N_ already defined"
#else
#define N_(x) x
#endif

static void
entry_sync_if_needed(Entry* e, GConfValue* val);
static GConfValue*
node_extract_value(xmlNodePtr node, const gchar** locales, GConfError** err);
static xmlNodePtr
find_schema_subnode_by_locale(xmlNodePtr node, const gchar* locale);
static void
node_unset_by_locale(xmlNodePtr node, const gchar* locale);
static void
node_unset_value(xmlNodePtr node);

struct _Entry {
  gchar* name; /* a relative key */
  gchar* schema_name;
  GConfValue* cached_value;
  xmlNodePtr node;
  gchar* mod_user;
  GTime mod_time;
  guint dirty : 1;
};

Entry*
entry_new (const gchar* relative_name)
{
  Entry* e;

  g_return_val_if_fail(relative_name != NULL, NULL);
  
  e = g_new0(Entry, 1);

  e->name = g_strdup(relative_name);

  e->dirty = TRUE;
  
  return e;
}

void
entry_destroy (Entry* e)
{
  if (e->name)
    g_free(e->name);

  if (e->cached_value)
    gconf_value_destroy(e->cached_value);

  if (e->mod_user)
    g_free(e->mod_user);

  if (e->node != NULL)
    {
      xmlUnlinkNode(e->node);
      xmlFreeNode(e->node);
      e->node = NULL;
    }
  
  g_free(e);
}

const gchar*
entry_get_name(Entry* e)
{
  return e->name;
}

void
entry_set_node (Entry*e, xmlNodePtr node)
{
  e->node = node;

  e->dirty = TRUE;
}

xmlNodePtr
entry_get_node (Entry*e)
{
  return e->node;
}

GConfValue*
entry_get_value(Entry* e, const gchar** locales, GConfError** err)
{
  const gchar* sl;
  
  g_return_val_if_fail(e != NULL, NULL);
  
  if (e->cached_value == NULL)
    return NULL;

  /* only schemas have locales for now anyway */
  if (e->cached_value->type != GCONF_VALUE_SCHEMA)
    return e->cached_value;

  g_assert(e->cached_value->type == GCONF_VALUE_SCHEMA);

  sl = gconf_schema_locale(gconf_value_schema(e->cached_value));

  /* optimize most common cases first */
  if (sl == NULL && (locales == NULL ||
                     *locales == NULL))
    return e->cached_value;
  else if (sl && locales && *locales &&
           strcmp(sl, *locales) == 0)
    return e->cached_value;
  else
    {
      /* We want a locale other than the currently-loaded one */
      GConfValue* newval;
      GConfError* error = NULL;

      entry_sync_if_needed(e, NULL);
      
      newval = node_extract_value(e->node, locales, &error);
      if (newval != NULL)
        {
          /* We found a schema with an acceptable locale */
          gconf_value_destroy(e->cached_value);
          e->cached_value = newval;
          g_return_val_if_fail(error == NULL, e->cached_value);
        }
      else if (error != NULL)
        {
          /* There was an error */
          gconf_log(GCL_WARNING, _("Ignoring XML node `%s': %s"),
                    e->name, error->str);
          gconf_error_destroy(error);
        }
      /* else fall back to the currently-loaded schema */
    }

  return e->cached_value;
}

void
entry_set_value(Entry* e, GConfValue* value)
{
  g_return_if_fail(e != NULL);

  entry_sync_if_needed(e, value);
  
  if (e->cached_value)
    gconf_value_destroy(e->cached_value);
      
  e->cached_value = gconf_value_copy(value);

  e->dirty = TRUE;
}

gboolean
entry_unset_value     (Entry        *e,
                       const gchar  *locale)
{
  g_return_val_if_fail(e != NULL, FALSE);

  if (e->cached_value != NULL)
    {
      if (locale && e->cached_value->type == GCONF_VALUE_SCHEMA)
        {
          GConfError* error = NULL;
          
          /* Remove the localized node from the XML tree */
          g_assert(e->node != NULL);
          node_unset_by_locale(e->node, locale);

          /* e->cached_value is always non-NULL if some value is
             available; in the schema case there may be leftover
             values */
          gconf_value_destroy(e->cached_value);
          e->cached_value = node_extract_value(e->node, NULL, &error);

          if (error != NULL)
            {
              gconf_log(GCL_WARNING, _("%s"), error->str);
              gconf_error_destroy(error);
              error = NULL;
            }
        }
      else if (e->cached_value->type == GCONF_VALUE_SCHEMA)
        {
          /* if locale == NULL nuke all the locales */
          if (e->cached_value)
            gconf_value_destroy(e->cached_value);
          e->cached_value = NULL;
        }
      else
        {
          gconf_value_destroy(e->cached_value);
          e->cached_value = NULL;
        }

      e->dirty = TRUE;
      
      return TRUE;
    }
  else
    return FALSE;
}

GConfMetaInfo*
entry_get_metainfo(Entry* e)
{
  GConfMetaInfo* gcmi;
  
  g_return_val_if_fail(e != NULL, NULL);

  gcmi = gconf_meta_info_new();

  if (e->schema_name)
    gconf_meta_info_set_schema(gcmi, e->schema_name);

  if (e->mod_time != 0)
    gconf_meta_info_set_mod_time(gcmi, e->mod_time);

  if (e->mod_user)
    gconf_meta_info_set_mod_user(gcmi, e->mod_user);

  return gcmi;
}

const gchar*
entry_get_schema_name (Entry        *e)
{
  return e->schema_name;
}

void
entry_set_schema_name (Entry        *e,
                       const gchar  *name)
{
  if (e->schema_name)
    g_free(e->schema_name);

  e->schema_name = name ? g_strdup(name) : NULL;
  
  e->dirty = TRUE;
}

void
entry_set_mod_time   (Entry        *e,
                      GTime         mod_time)
{
  g_return_if_fail(e != NULL);

  e->mod_time = mod_time;
  e->dirty = TRUE;
}

void
entry_set_mod_user (Entry *e,
                    const gchar* user)
{
  g_return_if_fail(e != NULL);
  
  if (e->mod_user)
    g_free(e->mod_user);
  e->mod_user = g_strdup(user);

  e->dirty = TRUE;
}

/*
 * XML-related cruft
 */

static void
entry_sync_if_needed(Entry* e, GConfValue* val)
{
  if (!e->dirty)
    return;
  
  if (e->cached_value &&
      e->cached_value->type == GCONF_VALUE_SCHEMA)
    {
      entry_sync_to_node(e);
    }
}

void
entry_fill_from_node(Entry* e)
{
  gchar* tmp;
  GConfError* error = NULL;

  g_return_if_fail(e->node != NULL);
  
  tmp = my_xmlGetProp(e->node, "schema");
  
  if (tmp != NULL)
    {
      /* Filter any crap schemas that appear, some speed cost */
      gchar* why_bad = NULL;
      if (gconf_valid_key(tmp, &why_bad))
        {
          g_assert(why_bad == NULL);
          e->schema_name = g_strdup(tmp);
        }
      else
        {
          e->schema_name = NULL;
          gconf_log(GCL_WARNING, _("Ignoring schema name `%s', invalid: %s"),
                    tmp, why_bad);
          g_free(why_bad);
        }
          
      free(tmp);
    }
      
  tmp = my_xmlGetProp(e->node, "mtime");

  if (tmp != NULL)
    {
      e->mod_time = gconf_string_to_gulong(tmp);
      free(tmp);
    }
  else
    e->mod_time = 0;

  tmp = my_xmlGetProp(e->node, "muser");

  if (tmp != NULL)
    {
      e->mod_user = g_strdup(tmp);
      free(tmp);
    }
  else
    e->mod_user = NULL;

  entry_sync_if_needed(e, NULL);
  
  if (e->cached_value != NULL)
    gconf_value_destroy(e->cached_value);
  
  e->cached_value = node_extract_value(e->node, NULL, /* FIXME current locale as a guess */
                                       &error);

  if (e->cached_value)
    {
      g_return_if_fail(error == NULL);
      return;
    }
  else if (error != NULL)
    {
      gconf_log(GCL_WARNING, _("Ignoring XML node `%s': %s"),
                e->name, error->str);
      gconf_error_destroy(error);
    }
}

static void
node_set_value(xmlNodePtr node, GConfValue* value);

static void
free_childs(xmlNodePtr node)
{
  g_return_if_fail(node != NULL);
  
  if (node->childs)
    xmlFreeNodeList(node->childs);
  node->childs = NULL;
  node->last = NULL;
}

void
entry_sync_to_node (Entry* e)
{
  g_return_if_fail(e != NULL);
  g_return_if_fail(e->node != NULL);
  
  if (!e->dirty)
    return;

  /* Unset all properties, so we don't have old cruft. */
  if (e->node->properties)
    xmlFreePropList(e->node->properties);
  e->node->properties = NULL;
  
  my_xmlSetProp(e->node, "name", e->name);

  if (e->mod_time != 0)
    {
      gchar* str = g_strdup_printf("%u", (guint)e->mod_time);
      my_xmlSetProp(e->node, "mtime", str);
      g_free(str);
    }
  else
    my_xmlSetProp(e->node, "mtime", NULL); /* Unset */

  /* OK if schema_name is NULL, then we unset */
  my_xmlSetProp(e->node, "schema", e->schema_name);

  /* OK if mod_user is NULL, since it unsets */
  my_xmlSetProp(e->node, "muser", e->mod_user);

  if (e->cached_value)
    node_set_value(e->node, e->cached_value);
  else
    node_unset_value(e->node);
  
  e->dirty = FALSE;
}

static void
node_set_schema_value(xmlNodePtr node,
                      GConfValue* value)
{
  GConfSchema* sc;
  const gchar* locale;
  xmlNodePtr found = NULL;

  sc = gconf_value_schema(value);
  
  /* unset this in case the node was previously a different type */
  my_xmlSetProp(node, "value", NULL);

  /* set the cross-locale attributes */
  my_xmlSetProp(node, "stype", gconf_value_type_to_string(sc->type));
  my_xmlSetProp(node, "owner", sc->owner);

  locale = gconf_schema_locale(sc);

  gconf_log(GCL_DEBUG, "Setting XML node to schema with locale `%s'",
            locale);
  
  /* Find the node for this locale */

  found = find_schema_subnode_by_locale(node, locale);
  
  if (found == NULL)
    found = xmlNewChild(node, NULL, "local_schema", NULL);
  
  /* OK if these are set to NULL, since that unsets the property */
  my_xmlSetProp(found, "locale", sc->locale);
  my_xmlSetProp(found, "short_desc", sc->short_desc);

  free_childs(found);
  
  if (sc->default_value != NULL)
    {
      xmlNodePtr default_value_node;
      default_value_node = xmlNewChild(found, NULL, "default", NULL);
      node_set_value(default_value_node, sc->default_value);
    }
  
  if (sc->long_desc)
    {
      xmlNodePtr ld_node;
      
      ld_node = xmlNewChild(found, NULL, "longdesc", sc->long_desc);
    }
}

static void
node_set_value(xmlNodePtr node, GConfValue* value)
{
  const gchar* type;
  gchar* value_str;

  g_return_if_fail(node != NULL);
  g_return_if_fail(value != NULL);
  g_return_if_fail(value->type != GCONF_VALUE_INVALID);
  
  type = gconf_value_type_to_string(value->type);

  g_assert(type != NULL);
  
  my_xmlSetProp(node, "type", type);

  switch (value->type)
    {
    case GCONF_VALUE_INT:
    case GCONF_VALUE_FLOAT:
    case GCONF_VALUE_BOOL:
    case GCONF_VALUE_STRING:
      free_childs(node);
      
      value_str = gconf_value_to_string(value);
  
      my_xmlSetProp(node, "value", value_str);

      g_free(value_str);
      break;
    case GCONF_VALUE_SCHEMA:
      {
        node_set_schema_value(node, value);
      }
      break;
    case GCONF_VALUE_LIST:
      {
        GSList* list;

        free_childs(node);

        my_xmlSetProp(node, "ltype",
                      gconf_value_type_to_string(gconf_value_list_type(value)));
        
        /* Add a new child for each node */
        list = gconf_value_list(value);

        while (list != NULL)
          {
            xmlNodePtr child;
            /* this is O(1) because libxml saves the list tail */
            child = xmlNewChild(node, NULL, "li", NULL);

            g_return_if_fail(list->data != NULL);
            
            node_set_value(child, (GConfValue*)list->data);
            
            list = g_slist_next(list);
          }
      }
      break;
      
    case GCONF_VALUE_PAIR:
      {
        xmlNodePtr car, cdr;

        free_childs(node);

        car = xmlNewChild(node, NULL, "car", NULL);
        cdr = xmlNewChild(node, NULL, "cdr", NULL);

        g_return_if_fail(gconf_value_car(value) != NULL);
        g_return_if_fail(gconf_value_cdr(value) != NULL);
        
        node_set_value(car, gconf_value_car(value));
        node_set_value(cdr, gconf_value_cdr(value));
      }
      break;
      
    default:
      g_assert_not_reached();
      break;
    }
}

static xmlNodePtr
find_schema_subnode_by_locale(xmlNodePtr node, const gchar* locale)
{
  xmlNodePtr iter;
  xmlNodePtr found = NULL;
    
  iter = node->childs;
      
  while (iter != NULL)
    {
      if (iter->type == XML_ELEMENT_NODE &&
          strcmp(iter->name, "local_schema") == 0)
        {
          char* this_locale = my_xmlGetProp(iter, "locale");
          
          if (locale && this_locale &&
              strcmp(locale, this_locale) == 0)
            {
              found = iter;
              free(this_locale);
              break;
            }
          else if (this_locale == NULL &&
                   locale == NULL)
            {
              found = iter;
              break;
            }
          else if (this_locale != NULL)
            free(this_locale);
        }
      iter = iter->next;
    }

  return found;
}

static void
node_unset_value(xmlNodePtr node)
{
  free_childs(node);
  my_xmlSetProp(node, "value", NULL);
  my_xmlSetProp(node, "type", NULL);
  my_xmlSetProp(node, "stype", NULL);
  my_xmlSetProp(node, "ltype", NULL);
}

static void
node_unset_by_locale(xmlNodePtr node, const gchar* locale)
{
  xmlNodePtr found;

  g_return_if_fail(node != NULL);
  g_return_if_fail(locale != NULL);

  if (locale != NULL)
    {
      found = find_schema_subnode_by_locale(node, locale);
      
      if (found != NULL)
        {
          xmlUnlinkNode(found);
          xmlFreeNode(found);
        }
    }
  else
    {
      node_unset_value(node);
    }
}

static void
schema_subnode_extract_data(xmlNodePtr node, GConfSchema* sc)
{
  gchar* sd_str;
  gchar* locale_str;
  GConfError* error = NULL;
  
  sd_str = my_xmlGetProp(node, "short_desc");
  locale_str = my_xmlGetProp(node, "locale");
  
  if (sd_str)
    {
      gconf_schema_set_short_desc(sc, sd_str);
      free(sd_str);
    }

  if (locale_str)
    {
      gconf_log(GCL_DEBUG, "found locale `%s'", locale_str);
      gconf_schema_set_locale(sc, locale_str);
      free(locale_str);
    }
  else
    gconf_log(GCL_WARNING, "found <local_schema> with no locale setting");
  
  if (node->childs != NULL)
    {
      GConfValue* default_value = NULL;
      gchar* ld_str = NULL;
      GSList* bad_nodes = NULL;
      xmlNodePtr iter = node->childs;

      while (iter != NULL)
        {
          if (iter->type == XML_ELEMENT_NODE)
            {
              if (default_value == NULL &&
                  strcmp(iter->name, "default") == 0)
                {
                  default_value = node_extract_value(iter, NULL, &error);

                  if (error != NULL)
                    {
                      g_assert(default_value == NULL);
                      
                      gconf_log(GCL_WARNING, _("Failed reading default value for schema: %s"), 
                                error->str);
                      gconf_error_destroy(error);
                      error = NULL;
                      
                      bad_nodes = g_slist_prepend(bad_nodes, iter);
                    }
                }
              else if (ld_str == NULL &&
                       strcmp(iter->name, "longdesc") == 0)
                {
                  ld_str = xmlNodeGetContent(iter);
                }
              else
                {
                  bad_nodes = g_slist_prepend(bad_nodes, iter);
                }
            }
          else
            bad_nodes = g_slist_prepend(bad_nodes, iter); /* what is this node? */

          iter = iter->next;
        }
      

      /* Remove the bad nodes from the parse tree */
      if (bad_nodes != NULL)
        {
          GSList* tmp = bad_nodes;
          
          while (tmp != NULL)
            {
              xmlUnlinkNode(tmp->data);
              xmlFreeNode(tmp->data);
              
              tmp = g_slist_next(tmp);
            }
          
          g_slist_free(bad_nodes);
        }

      if (default_value != NULL)
        gconf_schema_set_default_value_nocopy(sc, default_value);

      if (ld_str)
        {
          gconf_schema_set_long_desc(sc, ld_str);
          free(ld_str);
        }
    }
}

static GConfValue*
schema_node_extract_value(xmlNodePtr node, const gchar** locales)
{
  GConfValue* value = NULL;
  gchar* owner_str;
  gchar* stype_str;
  GConfSchema* sc;
  xmlNodePtr iter;
  guint i;
  xmlNodePtr* localized_nodes;
  xmlNodePtr best = NULL;
  
  /* owner, type are for all locales;
     default value, descriptions are per-locale
  */

  owner_str = my_xmlGetProp(node, "owner");
  stype_str = my_xmlGetProp(node, "stype");

  sc = gconf_schema_new();

  if (owner_str)
    {
      gconf_schema_set_owner(sc, owner_str);
      free(owner_str);
    }
  if (stype_str)
    {
      GConfValueType stype;
      stype = gconf_value_type_from_string(stype_str);
      gconf_schema_set_type(sc, stype);
      free(stype_str);
    }

  if (locales != NULL)
    {
      /* count the number of possible locales */
      i = 0;
      while (locales[i])
        ++i;
      
      localized_nodes = g_new0(xmlNodePtr, i+2);
      
      /* Find the node for each possible locale */
      iter = node->childs;
      
      while (iter != NULL)
        {
          if (iter->type == XML_ELEMENT_NODE &&
              strcmp(iter->name, "local_schema") == 0)
            {
              char* locale_name;
              
              locale_name = my_xmlGetProp(iter, "locale");
              
              if (locale_name != NULL)
                {
                  i = 0;
                  while (locales[i])
                    {
                      if (strcmp(locales[i], locale_name) == 0)
                        {
                          localized_nodes[i] = iter;
                          break;
                        }
                      ++i;
                    }
                  
                  /* Quit as soon as we have the best possible locale */
                  if (localized_nodes[0] != NULL)
                    break;
                  
                  free(locale_name);
                }
            }
          
          iter = iter->next;
        }

      /* See which is the best locale we managed to load, they are in
         order of preference. */
      
      i = 0;
      best = localized_nodes[i];
      while (best == NULL && localized_nodes[i])
        {
          best = localized_nodes[i];
          ++i;
        }
      
      g_free(localized_nodes);
    }

  /* If no locale matched, try picking the first node */
  if (best == NULL)
    best = node->childs;
  
  /* Extract info from the best locale node */
  if (best != NULL)
    schema_subnode_extract_data(best, sc);  
  
  /* Create a GConfValue with this schema and return it */
  value = gconf_value_new(GCONF_VALUE_SCHEMA);
      
  gconf_value_set_schema_nocopy(value, sc);

  return value;
}

/* this actually works on any node,
   not just <entry>, such as the <car>
   and <cdr> nodes and the <li> nodes and the
   <default> node
*/
static GConfValue*
node_extract_value(xmlNodePtr node, const gchar** locales, GConfError** err)
{
  GConfValue* value = NULL;
  gchar* type_str;
  GConfValueType type = GCONF_VALUE_INVALID;
  const gchar* default_locales[] = { "C", NULL };
  
  if (locales == NULL)
    locales = default_locales;
  
  type_str = my_xmlGetProp(node, "type");

  if (type_str == NULL)
    {
      gconf_set_error(err, GCONF_ERROR_PARSE_ERROR,
                      _("No \"type\" attribute for <%s> node"),
                      (node->name ? (char*)node->name : "(nil)"));
      return NULL;
    }
      
  type = gconf_value_type_from_string(type_str);

  free(type_str);
  
  switch (type)
    {
    case GCONF_VALUE_INVALID:
      {
        gconf_set_error(err, GCONF_ERROR_PARSE_ERROR,
                        _("A node has unknown \"type\" attribute `%s', ignoring"), type_str);
        return NULL;
      }
      break;
    case GCONF_VALUE_INT:
    case GCONF_VALUE_STRING:
    case GCONF_VALUE_BOOL:
    case GCONF_VALUE_FLOAT:
      {
        gchar* value_str;
        
        value_str = my_xmlGetProp(node, "value");
        
        if (value_str == NULL)
          {
            gconf_set_error(err, GCONF_ERROR_PARSE_ERROR,
                            _("No \"value\" attribute for node"));
            return NULL;
          }
            
        value = gconf_value_new_from_string(type, value_str, err);

        free(value_str);

        g_return_val_if_fail( (value != NULL) ||
                              (err == NULL) ||
                              (*err != NULL),
                              NULL );
        
        return value;
      }
      break;
    case GCONF_VALUE_SCHEMA:
      return schema_node_extract_value(node, locales);
      break;
    case GCONF_VALUE_LIST:
      {
        xmlNodePtr iter;
        GSList* values = NULL;
        GConfValueType list_type = GCONF_VALUE_INVALID;

        {
          gchar* s;
          s = my_xmlGetProp(node, "ltype");
          if (s != NULL)
            {
              list_type = gconf_value_type_from_string(s);
              free(s);
            }
        }

        switch (list_type)
          {
          case GCONF_VALUE_INVALID:
          case GCONF_VALUE_LIST:
          case GCONF_VALUE_PAIR:
            gconf_set_error(err, GCONF_ERROR_PARSE_ERROR,
                            _("Invalid type (list, pair, or unknown) in a list node"));
              
            return NULL;
          default:
            break;
          }
        
        iter = node->childs;

        while (iter != NULL)
          {
            if (iter->type == XML_ELEMENT_NODE)
              {
                GConfValue* v = NULL;
                if (strcmp(iter->name, "li") == 0)
                  {
                    
                    v = node_extract_value(iter, locales, err);
                    if (v == NULL)
                      {
                        if (err && *err)
                          {
                            gconf_log(GCL_WARNING,
                                      _("Bad XML node: %s"),
                                      (*err)->str);
                            /* avoid pile-ups */
                            gconf_clear_error(err);
                          }
                      }
                    else if (v->type != list_type)
                      {
                        gconf_log(GCL_WARNING, _("List contains a badly-typed node (%s, should be %s)"),
                                  gconf_value_type_to_string(v->type),
                                  gconf_value_type_to_string(list_type));
                        gconf_value_destroy(v);
                        v = NULL;
                      }
                  }
                else
                  {
                    /* What the hell is this? */
                    gconf_log(GCL_WARNING,
                              _("Didn't understand XML node <%s> inside an XML list node"),
                              iter->name ? iter->name : (guchar*)"???");
                  }

                if (v != NULL)
                  values = g_slist_prepend(values, v);
              }
            iter = iter->next;
          }
        
        /* put them in order, set the value */
        values = g_slist_reverse(values);

        value = gconf_value_new(GCONF_VALUE_LIST);

        gconf_value_set_list_type(value, list_type);
        gconf_value_set_list_nocopy(value, values);

        return value;
      }
      break;
    case GCONF_VALUE_PAIR:
      {
        GConfValue* car = NULL;
        GConfValue* cdr = NULL;
        xmlNodePtr iter;
        
        iter = node->childs;

        while (iter != NULL)
          {
            if (iter->type == XML_ELEMENT_NODE)
              {
                if (car == NULL && strcmp(iter->name, "car") == 0)
                  {
                    car = node_extract_value(iter, locales, err);
                    if (car == NULL)
                      {
                        if (err && *err)
                          {
                            gconf_log(GCL_WARNING,
                                      _("Ignoring bad car from XML pair: %s"),
                                      (*err)->str);
                            /* prevent pile-ups */
                            gconf_clear_error(err);
                          }
                      }
                    else if (car->type == GCONF_VALUE_LIST ||
                             car->type == GCONF_VALUE_PAIR)
                      {
                        gconf_log(GCL_WARNING, _("parsing XML file: lists and pairs may not be placed inside a pair"));
                        gconf_value_destroy(car);
                        car = NULL;
                      }
                  }
                else if (cdr == NULL && strcmp(iter->name, "cdr") == 0)
                  {
                    cdr = node_extract_value(iter, locales, err);
                    if (cdr == NULL)
                      {
                        if (err && *err)
                          {
                            gconf_log(GCL_WARNING,
                                      _("Ignoring bad cdr from XML pair: %s"),
                                      (*err)->str);
                            /* avoid pile-ups */
                            gconf_clear_error(err);
                          }
                      }
                    else if (cdr->type == GCONF_VALUE_LIST ||
                             cdr->type == GCONF_VALUE_PAIR)
                      {
                        gconf_log(GCL_WARNING,
                                  _("parsing XML file: lists and pairs may not be placed inside a pair"));
                        gconf_value_destroy(cdr);
                        cdr = NULL;
                      }
                  }
                else
                  {
                    /* What the hell is this? */
                    gconf_log(GCL_WARNING,
                              _("Didn't understand XML node <%s> inside an XML pair node"),
                              iter->name ? (gchar*)iter->name : "???");                    
                  }
              }
            iter = iter->next;
          }

        /* Return the pair */
        value = gconf_value_new(GCONF_VALUE_PAIR);
        gconf_value_set_car_nocopy(value, car);
        gconf_value_set_cdr_nocopy(value, cdr);

        return value;
      }
      break;
    default:
      g_assert_not_reached();
      return NULL;
      break;
    }
}

/*
 * Enhanced libxml
 */

/* makes setting to NULL or empty string equal to unset */

void
my_xmlSetProp(xmlNodePtr node,
              const gchar* name,
              const gchar* str)
{
  xmlAttrPtr prop;

  prop = xmlSetProp(node, name, str);

  if (str == NULL || *str == '\0')
    {
      xmlAttrPtr iter;
      xmlAttrPtr prev;

      prev = NULL;
      iter = node->properties;
      while (iter != NULL)
        {
          if (iter == prop)
            break;
          prev = iter;
          iter = iter->next;
        }

      g_assert(iter == prop);
      
      if (prev)
        prev->next = iter->next;
      else
        node->properties = iter->next; /* we were the first node */

      xmlFreeProp(iter);
    }
}

/* Makes empty strings equal to "unset" */
char*
my_xmlGetProp(xmlNodePtr node,
              const gchar* name)
{
  char* prop;

  prop = xmlGetProp(node, name);

  if (prop && *prop == '\0')
    {
      free(prop);
      return NULL;
    }
  else
    return prop;
}

