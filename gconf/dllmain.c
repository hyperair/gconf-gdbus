/* GConf - dllmain.c
 * Copyright (C) 2005 Novell, Inc
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

#include <windows.h>
#include <string.h>
#include <mbstring.h>
#include <glib.h>

static const char *runtime_prefix;
const char *gconf_win32_locale_dir;
const char *gconf_win32_confdir;
const char *gconf_win32_etcdir;
const char *gconf_win32_serverdir;
const char *gconf_win32_backend_dir;

char *
gconf_win32_replace_prefix (const char *configure_time_path)
{
  if (strncmp (configure_time_path, PREFIX "/", strlen (PREFIX) + 1) == 0)
    {
      return g_strconcat (runtime_prefix,
			  configure_time_path + strlen (PREFIX),
			  NULL);
    }
  else
    return g_strdup (configure_time_path);
}

/* DllMain function needed to fetch the DLL name and deduce the
 * installation directory from that, and then form the pathnames for
 * various directories relative to the installation directory.
 */
BOOL WINAPI
DllMain (HINSTANCE hinstDLL,
	 DWORD     fdwReason,
	 LPVOID    lpvReserved)
{
  wchar_t wcbfr[1000];
  char cpbfr[1000];
  char *dll_name = NULL;
  
  switch (fdwReason) {
  case DLL_PROCESS_ATTACH:
	  if (GLIB_CHECK_VERSION (2, 6, 0)) {
		  /* GLib 2.6 uses UTF-8 file names */
		  if (GetVersion () < 0x80000000) {
			  /* NT-based Windows has wide char API */
			  if (GetModuleFileNameW ((HMODULE) hinstDLL,
						  wcbfr, G_N_ELEMENTS (wcbfr)))
			      dll_name = g_utf16_to_utf8 (wcbfr, -1,
							  NULL, NULL, NULL);
		  } else {
			  /* Win9x, yecch */
			  if (GetModuleFileNameA ((HMODULE) hinstDLL,
						  cpbfr, G_N_ELEMENTS (cpbfr)))
				  dll_name = g_locale_to_utf8 (cpbfr, -1,
							       NULL, NULL, NULL);
		  }
	  } else {
		  /* Earlier GLibs use system codepage file names */
		  if (GetModuleFileNameA ((HMODULE) hinstDLL,
					  cpbfr, G_N_ELEMENTS (cpbfr)))
			  dll_name = g_strdup (cpbfr);
	  }

	  if (dll_name) {
		  gchar *p = strrchr (dll_name, '\\');
		  
		  if (p != NULL)
			  *p = '\0';

		  p = strrchr (dll_name, '\\');
		  if (p && (g_ascii_strcasecmp (p + 1, "bin") == 0 ||
			    g_ascii_strcasecmp (p + 1, "lib") == 0))
			  *p = '\0';
		  
		  runtime_prefix = dll_name;

		  /* Replace backslashes with forward slashes to avoid
		   * problems for instance in makefiles that use
		   * gconftool-2 --get-default-source.
		   */
		  if (GLIB_CHECK_VERSION (2, 6, 0)) {
			  while ((p = strrchr (runtime_prefix, '\\')) != NULL)
				  *p = '/';
		  } else {
			  while ((p = _mbsrchr (runtime_prefix, '\\')) != NULL)
				  *p = '/';
		  }
	  } else {
		  runtime_prefix = g_strdup ("");
	  }

	  gconf_win32_locale_dir = gconf_win32_replace_prefix (GCONF_LOCALE_DIR);
	  gconf_win32_confdir = gconf_win32_replace_prefix (GCONF_CONFDIR);
	  gconf_win32_etcdir = gconf_win32_replace_prefix (GCONF_ETCDIR);
	  gconf_win32_serverdir = gconf_win32_replace_prefix (GCONF_SERVERDIR);
	  gconf_win32_backend_dir = gconf_win32_replace_prefix (GCONF_BACKEND_DIR);
	  break;
  }

  return TRUE;
}
