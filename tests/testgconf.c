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

#include <gconf.h>
#include <gconf-orbit.h>
#include <stdio.h>
#include <unistd.h>
#include <math.h>

static void
check(gboolean condition, const gchar* fmt, ...)
{
  va_list args;
  gchar* description;
  
  va_start (args, fmt);
  description = g_strdup_vprintf(fmt, args);
  va_end (args);
  
  if (condition)
    {
      printf(".");
      fflush(stdout);
    }
  else
    {
      fprintf(stderr, "\n*** FAILED: %s\n", description);
      exit(1);
    }
  
  g_free(description);
}

static const gchar*
keys[] = {
  "/testing/foo/tar",
  "/testing/foo/bar",
  "/testing/quad",
  "/testing/blah",
  "/testing/q/a/b/c/z/w/x/y/z",
  "/testing/foo/baz",
  "/testing/oops/bloo",
  "/testing/oops/snoo",
  "/testing/oops/kwoo",
  "/testing/foo/quaz",
  NULL
};

static const gchar*
some_strings[] = {
  "dkadfhg;ifb;klndfl;kghpaodigjhrekjt45u62%&@#&@#kl6$%76k@$%&L jk245L:Yj45&@$&KL #$^UY $5",
  "sdkjfkljg",
  "a",
  "&",
  "#$&&^(%^^#$&&*(%^&#!$%$%&^&(%&>>>>>>>>>>>>>!>>>.....<<<<<<<<<<<<<<,,,,,,,&&&&&&",
  "sjdflkjg;kljklj",
  "hello this is a string with spaces and \t\t\t\ttabs",
  "hello this\nstring\nhas\nnewlines\n   \t\t\t\t\t\ttabs and spaces  \n",
  "<?xml version=\"1.0\"?>
<gmr:Workbook xmlns:gmr=\"http://www.gnome.org/gnumeric/\">
  <gmr:Style HAlign=\"1\" VAlign=\"1\" Fit=\"0\" Orient=\"1\" Shade=\"0\" Format=\"#,##0_);[red](#,##0)\">
    <gmr:Font Unit=\"14\" NAME=\"FontDef1\">-adobe-helvetica-medium-r-normal--*-120-*-*-*-*-*-*</gmr:Font>
  </gmr:Style>
  <gmr:Geometry Width=\"610\" Height=\"418\"/>
  <gmr:Sheets>
    <gmr:Sheet>
      <gmr:Name>Sheet 0</gmr:Name>
      <gmr:MaxCol>6</gmr:MaxCol>
      <gmr:MaxRow>14</gmr:MaxRow>
      <gmr:Zoom>1.000000</gmr:Zoom>
      <gmr:Cols>
        <gmr:ColInfo No=\"0\" Unit=\"97\" MarginA=\"1\" MarginB=\"1\" HardSize=\"0\"/>
        <gmr:ColInfo No=\"1\" Unit=\"80\" MarginA=\"1\" MarginB=\"1\" HardSize=\"0\"/>
        <gmr:ColInfo No=\"2\" Unit=\"80\" MarginA=\"1\" MarginB=\"1\" HardSize=\"0\"/>
        <gmr:ColInfo No=\"3\" Unit=\"80\" MarginA=\"1\" MarginB=\"1\" HardSize=\"0\"/>
        <gmr:ColInfo No=\"6\" Unit=\"80\" MarginA=\"1\" MarginB=\"1\" HardSize=\"0\"/>
      </gmr:Cols>
      <gmr:Rows>
        <gmr:RowInfo No=\"0\" Unit=\"20\" MarginA=\"1\" MarginB=\"1\" HardSize=\"0\"/>
        <gmr:RowInfo No=\"1\" Unit=\"20\" MarginA=\"1\" MarginB=\"1\" HardSize=\"0\"/>
        <gmr:RowInfo No=\"2\" Unit=\"20\" MarginA=\"1\" MarginB=\"1\" HardSize=\"0\"/>
        <gmr:RowInfo No=\"3\" Unit=\"20\" MarginA=\"1\" MarginB=\"1\" HardSize=\"0\"/>
        <gmr:RowInfo No=\"4\" Unit=\"20\" MarginA=\"1\" MarginB=\"1\" HardSize=\"0\"/>
        <gmr:RowInfo No=\"5\" Unit=\"20\" MarginA=\"1\" MarginB=\"1\" HardSize=\"0\"/>
        <gmr:RowInfo No=\"6\" Unit=\"20\" MarginA=\"1\" MarginB=\"1\" HardSize=\"0\"/>
        <gmr:RowInfo No=\"7\" Unit=\"20\" MarginA=\"1\" MarginB=\"1\" HardSize=\"0\"/>
        <gmr:RowInfo No=\"8\" Unit=\"20\" MarginA=\"1\" MarginB=\"1\" HardSize=\"0\"/>
        <gmr:RowInfo No=\"9\" Unit=\"20\" MarginA=\"1\" MarginB=\"1\" HardSize=\"0\"/>
        <gmr:RowInfo No=\"10\" Unit=\"20\" MarginA=\"1\" MarginB=\"1\" HardSize=\"0\"/>
        <gmr:RowInfo No=\"11\" Unit=\"20\" MarginA=\"1\" MarginB=\"1\" HardSize=\"0\"/>
        <gmr:RowInfo No=\"12\" Unit=\"20\" MarginA=\"1\" MarginB=\"1\" HardSize=\"0\"/>
        <gmr:RowInfo No=\"13\" Unit=\"20\" MarginA=\"1\" MarginB=\"1\" HardSize=\"0\"/>
        <gmr:RowInfo No=\"14\" Unit=\"20\" MarginA=\"1\" MarginB=\"1\" HardSize=\"0\"/>
      </gmr:Rows>
      <gmr:Objects>
        <gmr:Ellipse Pattern=\"0\" Width=\"1\" Color=\"black\">
          <gmr:Points>(258.000000 320.000000)(356.000000 356.000000)</gmr:Points>
        </gmr:Ellipse>
        <gmr:Arrow Width=\"1\" Color=\"black\">
          <gmr:Points>(500.000000 131.000000)(332.000000 320.000000)</gmr:Points>
        </gmr:Arrow>
      </gmr:Objects>
      <gmr:Cells>
        <gmr:Cell Col=\"3\" Row=\"1\">
          <gmr:Style HAlign=\"1\" VAlign=\"1\" Fit=\"0\" Orient=\"1\" Shade=\"0\" Format=\"#,##0_);[red](#,##0)\">
            <gmr:Font Unit=\"14\" NAME=\"FontDef2\">-adobe-helvetica-medium-r-normal--*-120-*-*-*-*-*-*</gmr:Font>
          </gmr:Style>
          <gmr:Content>500</gmr:Content>",

  NULL
};

static gint ints[] = { -1, -2, -3, 0, 1, 2, 3, 4000, 0xfffff, -0xfffff, G_MININT, G_MAXINT, 0, 0, 57, 83, 95 };
static const guint n_ints = sizeof(ints)/sizeof(ints[0]);

static gboolean bools[] = { TRUE, TRUE, FALSE, FALSE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE,
                       FALSE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, FALSE, FALSE, TRUE };

static const guint n_bools = sizeof(bools)/sizeof(bools[0]);

static gdouble floats[] = { 0.0, 1.0, 2.0, 3.0, 4.0, -10.0, -10.34645764573475637657367346743734878734109870187200000000000009, -100.39458694856908, 3.14159, 4.4532464e7, 9.35e-10, 4.5, 6.7, 8.3, -5.1, G_MINFLOAT, -G_MAXFLOAT, G_MAXFLOAT }; /* don't use max/min double, we don't guarantee that we can store huge numbers */

static const guint n_floats = sizeof(floats)/sizeof(floats[0]);

static void
check_unset(GConfEngine* conf)
{
  GConfError* err = NULL;
  const gchar** keyp = NULL;

  keyp = keys;

  while (*keyp)
    {
      g_conf_unset(conf, *keyp, &err);

      if (err != NULL)
        {
          fprintf(stderr, "unset of `%s' failed: %s\n", *keyp, err->str);
          g_conf_error_destroy(err);
          err = NULL;
        }
      else
        {
          GConfValue* val;
          gchar* valstr;
          
          val = g_conf_get(conf, *keyp, &err);


          if (val)
            valstr = g_conf_value_to_string(val);
          else
            valstr = g_strdup("(none)");
          
          check(val == NULL, "unsetting a previously-set value `%s' the value `%s' existed", *keyp, valstr);

          g_free(valstr);
        }
      
      ++keyp;
    }
}

static void
check_string_storage(GConfEngine* conf)
{
  GConfError* err = NULL;
  const gchar** keyp = NULL;
  const gchar** valp = NULL;

  /* Loop over keys, storing all strings at each key then immediately
     retrieving them */
  
  keyp = keys;

  while (*keyp)
    {
      valp = some_strings;
      while (*valp)
        {
          gchar* gotten;
          
          if (!g_conf_set_string(conf, *keyp, *valp, &err))
            {
              fprintf(stderr, "Failed to set key `%s' to `%s': %s\n",
                      *keyp, *valp, err->str);
              g_conf_error_destroy(err);
              err = NULL;
            }
          else
            {
              gotten = g_conf_get_string(conf, *keyp, NULL, &err);
              
              if (err != NULL)
                {
                  check(gotten == NULL, "string was returned though there was an error");
                  fprintf(stderr, "Failed to get key `%s': %s\n",
                          *keyp, err->str);
                  g_conf_error_destroy(err);
                  err = NULL;
                }
              else
                {
                  check (strcmp(gotten, *valp) == 0, "string set/get pair: `%s' set, `%s' got",
                         *valp, gotten);
              
                  g_free(gotten);
                }
            }
          
          ++valp;
        }

      ++keyp;
    }

  /* Now invert the loop and see if that causes problems */
  
  valp = some_strings;
  
  while (*valp)
    {
      keyp = keys;
      while (*keyp)
        {
          gchar* gotten;
          
          if (!g_conf_set_string(conf, *keyp, *valp, &err))
            {
              fprintf(stderr, "Failed to set key `%s' to `%s': %s\n",
                      *keyp, *valp, err->str);
              g_conf_error_destroy(err);
              err = NULL;
            }

          gotten = g_conf_get_string(conf, *keyp, NULL, &err);

          if (err != NULL)
            {
              check(gotten == NULL, "string was returned though there was an error");
              fprintf(stderr, "Failed to get key `%s': %s\n",
                      *keyp, err->str);
              g_conf_error_destroy(err);
              err = NULL;
            }
          else
            {
              check (strcmp(gotten, *valp) == 0, "string set/get pair: `%s' set, `%s' got",
                     *valp, gotten);
              
              g_free(gotten);
            }
              
          ++keyp;
        }

      ++valp;
    }


  check_unset(conf);
}

void
check_bool_storage(GConfEngine* conf)
{
  GConfError* err = NULL;
  const gchar** keyp = NULL;
  guint i; 
  
  /* Loop over keys, storing all bools at each then retrieving them */
  
  keyp = keys;

  while (*keyp)
    {
      i = 0;
      while (i < n_bools)
        {
          gboolean gotten;
          
          if (!g_conf_set_bool(conf, *keyp, bools[i], &err))
            {
              fprintf(stderr, "Failed to set key `%s' to `%d': %s\n",
                      *keyp, bools[i], err->str);
              g_conf_error_destroy(err);
              err = NULL;
            }
          else
            {
              gotten = g_conf_get_bool(conf, *keyp, FALSE, &err);

              if (err != NULL)
                {
                  check(gotten == FALSE, "TRUE was returned though there was an error");

                  fprintf(stderr, "Failed to get key `%s': %s\n",
                          *keyp, err->str);
                  g_conf_error_destroy(err);
                  err = NULL;
                }
              else
                {
                  check (bools[i] == gotten, "bool set/get pair: `%d' set, `%d' got",
                         bools[i], gotten);

                }
            }
          
          ++i;
        }
      
      ++keyp;
    }

  /* Now invert the loop and see if that causes problems */

  i = 0;
      
  while (i < n_bools)
    {
      keyp = keys;
      
      while (*keyp)
        {
          gboolean gotten;
          
          if (!g_conf_set_bool(conf, *keyp, bools[i], &err))
            {
              fprintf(stderr, "Failed to set key `%s' to `%d': %s\n",
                      *keyp, bools[i], err->str);
              g_conf_error_destroy(err);
              err = NULL;
            }
          else
            {
              gotten = g_conf_get_bool(conf, *keyp, FALSE, &err);
              
              if (err != NULL)
                {
                  check(gotten == FALSE, "TRUE was returned though there was an error");

                  fprintf(stderr, "Failed to get key `%s': %s\n",
                          *keyp, err->str);
                  g_conf_error_destroy(err);
                  err = NULL;
                }
              else
                {
                  check (bools[i] == gotten, "bool set/get pair: `%d' set, `%d' got",
                         bools[i], gotten);

                }
            }

          ++keyp;
        }

      ++i;
    }
  
  check_unset(conf);
}

void
check_float_storage(GConfEngine* conf)
{
  GConfError* err = NULL;
  const gchar** keyp = NULL;
  guint i; 
  const gdouble tolerance = 1e-5;
  
  /* Loop over keys, storing all values at each then retrieving them */
  
  keyp = keys;

  while (*keyp)
    {
      i = 0;
      while (i < n_floats)
        {
          gdouble gotten;
          
          if (!g_conf_set_float(conf, *keyp, floats[i], &err))
            {
              fprintf(stderr, "Failed to set key `%s' to `%g': %s\n",
                      *keyp, floats[i], err->str);
              g_conf_error_destroy(err);
              err = NULL;
            }
          else
            {
              gotten = g_conf_get_float(conf, *keyp, 0.0, &err);

              if (err != NULL)
                {
                  check(gotten == 0.0, "0.0 not returned though there was an error");

                  fprintf(stderr, "Failed to get key `%s': %s\n",
                          *keyp, err->str);
                  g_conf_error_destroy(err);
                  err = NULL;
                }
              else
                {
                  check (fabs(floats[i] - gotten) < tolerance,
                         "float set/get pair: `%g' set, `%g' got, `%g' epsilon",
                         floats[i], gotten, floats[i] - gotten);

                }
            }
          
          ++i;
        }
      
      ++keyp;
    }

  /* Now invert the loop and see if that causes problems */

  i = 0;
  while (i < n_floats)
    {

      keyp = keys;

      while (*keyp)
        {
          gdouble gotten;
          
          if (!g_conf_set_float(conf, *keyp, floats[i], &err))
            {
              fprintf(stderr, "Failed to set key `%s' to `%g': %s\n",
                      *keyp, floats[i], err->str);
              g_conf_error_destroy(err);
              err = NULL;
            }
          else
            {
              gotten = g_conf_get_float(conf, *keyp, 0.0, &err);

              if (err != NULL)
                {
                  check(gotten == 0.0, "0.0 not returned though there was an error");

                  fprintf(stderr, "Failed to get key `%s': %s\n",
                          *keyp, err->str);
                  g_conf_error_destroy(err);
                  err = NULL;
                }
              else
                {
                  check (fabs(floats[i] - gotten) < tolerance,
                         "float set/get pair: `%g' set, `%g' got, `%g' epsilon",
                         floats[i], gotten, floats[i] - gotten);

                }
            }
          
      
          ++keyp;
        }

      ++i;
    }
          
  check_unset(conf);
}

void
check_int_storage(GConfEngine* conf)
{
  GConfError* err = NULL;
  const gchar** keyp = NULL;
  guint i; 
  
  /* Loop over keys, storing all values at each then retrieving them */
  
  keyp = keys;

  while (*keyp)
    {
      i = 0;
      while (i < n_ints)
        {
          gint gotten;
          
          if (!g_conf_set_int(conf, *keyp, ints[i], &err))
            {
              fprintf(stderr, "Failed to set key `%s' to `%d': %s\n",
                      *keyp, ints[i], err->str);
              g_conf_error_destroy(err);
              err = NULL;
            }
          else
            {
              gotten = g_conf_get_int(conf, *keyp, 0.0, &err);

              if (err != NULL)
                {
                  check(gotten == 0.0, "0.0 not returned though there was an error");

                  fprintf(stderr, "Failed to get key `%s': %s\n",
                          *keyp, err->str);
                  g_conf_error_destroy(err);
                  err = NULL;
                }
              else
                {
                  check (ints[i] == gotten,
                         "int set/get pair: `%d' set, `%d' got",
                         ints[i], gotten);

                }
            }
          
          ++i;
        }
      
      ++keyp;
    }

  /* Now invert the loop and see if that causes problems */

  i = 0;
  while (i < n_ints)
    {

      keyp = keys;

      while (*keyp)
        {
          gint gotten;
          
          if (!g_conf_set_int(conf, *keyp, ints[i], &err))
            {
              fprintf(stderr, "Failed to set key `%s' to `%d': %s\n",
                      *keyp, ints[i], err->str);
              g_conf_error_destroy(err);
              err = NULL;
            }
          else
            {
              gotten = g_conf_get_int(conf, *keyp, 0.0, &err);

              if (err != NULL)
                {
                  check(gotten == 0, "0 not returned though there was an error");

                  fprintf(stderr, "Failed to get key `%s': %s\n",
                          *keyp, err->str);
                  g_conf_error_destroy(err);
                  err = NULL;
                }
              else
                {
                  check (ints[i] == gotten,
                         "int set/get pair: `%d' set, `%d' got",
                         ints[i], gotten);

                }
            }
          
      
          ++keyp;
        }

      ++i;
    }
          
  check_unset(conf);
}

static void
compare_listvals(GConfValue* first, GConfValue* second)
{
  GSList* l1;
  GSList* l2;
  
  check(first->type == G_CONF_VALUE_LIST, "first list value isn't a list value");
  check(second->type == G_CONF_VALUE_LIST, "second list value isn't a list value");
  check(g_conf_value_list_type(first) == g_conf_value_list_type(second),
        "two lists don't have the same type");

  l1 = g_conf_value_list(first);
  l2 = g_conf_value_list(second);

  while (l1 != NULL)
    {
      GConfValue* val1;
      GConfValue* val2;
      
      check(l2 != NULL, "second list too short");
        
      val1 = l1->data;
      val2 = l2->data;

      check(val1->type == val2->type, "values in list have a different type");

      check(val1->type == g_conf_value_list_type(first), "first list has incorrectly typed value");
      check(val2->type == g_conf_value_list_type(second), "second list has incorrectly typed value");
      
      switch (val1->type)
        {
        case G_CONF_VALUE_INT:
          check(g_conf_value_int(val1) == g_conf_value_int(val2),
                "integer values %d and %d are not equal", g_conf_value_int(val1),
                g_conf_value_int(val2));
          break;
        case G_CONF_VALUE_BOOL:
          check(g_conf_value_bool(val1) == g_conf_value_bool(val2),
                "boolean values %d and %d are not equal", g_conf_value_bool(val1),
                g_conf_value_bool(val2));
          break;
        case G_CONF_VALUE_FLOAT:
          check(fabs(g_conf_value_float(val1) - g_conf_value_float(val2)) < 1e-7,
                "float values %g and %g are not equal", g_conf_value_float(val1),
                g_conf_value_float(val2));
          break;
        case G_CONF_VALUE_STRING:
          check(strcmp(g_conf_value_string(val1), g_conf_value_string(val2)) == 0, 
                "string values `%s' and `%s' are not equal", g_conf_value_string(val1),
                g_conf_value_string(val2));
          break;
        default:
          g_assert_not_reached();
          break;
        }
      
      l1 = g_slist_next(l1);
      l2 = g_slist_next(l2);
    }
}

static void
free_value_list(GSList* list)
{
  GSList* tmp = list;

  while (tmp != NULL)
    {
      g_conf_value_destroy(tmp->data);

      tmp = g_slist_next(tmp);
    }

  g_slist_free(list);
}

static GSList*
list_of_intvals(void)
{
  GSList* retval = NULL;
  guint i = 0;
  while (i < n_ints)
    {
      GConfValue* val;

      val = g_conf_value_new(G_CONF_VALUE_INT);

      g_conf_value_set_int(val, ints[i]);
      
      retval = g_slist_prepend(retval, val);
      
      ++i;
    }
  return retval;
}

static void
check_list_storage(GConfEngine* conf)
{
  GConfError* err = NULL;
  const gchar** keyp = NULL;
  GSList* list_of_lists = NULL;
  GSList* intlist;
  
  intlist = list_of_intvals();

  list_of_lists = g_slist_prepend(list_of_lists, intlist);
  
  /* Loop over keys, storing all values at each then retrieving them */
  
  keyp = keys;

  while (*keyp)
    {
      GSList* tmp = list_of_lists;

      while (tmp != NULL)
        {
          GConfValue* gotten = NULL;
          GConfValue* thislist = NULL;

          thislist = g_conf_value_new(G_CONF_VALUE_LIST);

          g_conf_value_set_list_type(thislist, G_CONF_VALUE_INT);

          g_conf_value_set_list(thislist, intlist); /* makes a copy */
          
          if (!g_conf_set(conf, *keyp, thislist, &err))
            {
              fprintf(stderr, "Failed to set key `%s' to list: %s\n",
                      *keyp, err->str);
              g_conf_error_destroy(err);
              err = NULL;
            }
          else
            {
              gotten = g_conf_get(conf, *keyp, &err);

              if (err != NULL)
                {
                  check(gotten == NULL, "NULL not returned though there was an error");

                  fprintf(stderr, "Failed to get key `%s': %s\n",
                          *keyp, err->str);
                  g_conf_error_destroy(err);
                  err = NULL;
                }
              else
                {
                  compare_listvals(gotten, thislist);
                  g_conf_value_destroy(gotten);
                }
            }

          g_conf_value_destroy(thislist);
          
          tmp = g_slist_next(tmp);
        }
      
      ++keyp;
    }

  free_value_list(intlist);

  g_slist_free(list_of_lists);
  
  check_unset(conf);
}

int 
main (int argc, char** argv)
{
  GConfEngine* conf;
  GConfError* err = NULL;

  if (g_conf_init_orb(&argc, argv, &err) == CORBA_OBJECT_NIL)
    {
      fprintf(stderr, "Failed to init orb: %s\n", err->str);
      g_conf_error_destroy(err);
      err = NULL;
      return 1;
    }

  if (!g_conf_init(&err))
    {
      fprintf(stderr, "Failed to init GConf: %s\n", err->str);
      g_conf_error_destroy(err);
      err = NULL;
      return 1;
    }
  
  conf = g_conf_engine_new();

  check(conf != NULL, "create the default conf engine");


  printf("\nChecking list storage:");
  
  check_list_storage(conf);
  
  printf("\nChecking integer storage:");
  
  check_int_storage(conf);

  printf("\nChecking float storage:");
  
  check_float_storage(conf);

  printf("\nChecking string storage:");
  
  check_string_storage(conf);

  printf("\nChecking bool storage:");
  
  check_bool_storage(conf);
  
  g_conf_engine_unref(conf);

  printf("\n\n");
  
  return 0;
}
