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
              
              check (strcmp(gotten, *valp) == 0, "string set/get pair: `%s' set, `%s' got",
                     *valp, gotten);
              
              g_free(gotten);
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

          check (strcmp(gotten, *valp) == 0, "string set/get pair: `%s' set, `%s' got",
                 *valp, gotten);

          g_free(gotten);
          
          ++keyp;
        }

      ++valp;
    }


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

  check_string_storage(conf);
  
  g_conf_engine_unref(conf);

  printf("\n\n");
  
  return 0;
}
