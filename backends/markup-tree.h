/* GConf
 * Copyright (C) 2002 Red Hat Inc.
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

#ifndef MARKUP_TREE_H
#define MARKUP_TREE_H

#include <glib.h>
#include <gconf/gconf-value.h>

typedef struct _MarkupTree  MarkupTree;
typedef struct _MarkupDir   MarkupDir;
typedef struct _MarkupEntry MarkupEntry;

/* Tree */

MarkupTree* markup_tree_new        (const char *root_dir,
                                    guint       dir_mode,
                                    guint       file_mode,
                                    gboolean    read_only);
void        markup_tree_free       (MarkupTree *tree);
MarkupDir*  markup_tree_lookup_dir (MarkupTree *tree,
                                    const char *full_key,
                                    GError    **err);
MarkupDir*  markup_tree_ensure_dir (MarkupTree *tree,
                                    const char *full_key,
                                    GError    **err);

/* Directories in the tree */

MarkupEntry* markup_dir_lookup_entry  (MarkupDir   *dir,
                                       const char  *relative_key,
                                       GError     **err);
MarkupEntry* markup_dir_ensure_entry  (MarkupDir   *dir,
                                       const char  *relative_key,
                                       GError     **err);
MarkupDir*   markup_dir_lookup_subdir (MarkupDir   *dir,
                                       const char  *relative_key,
                                       GError     **err);
MarkupDir*   markup_dir_ensure_subdir (MarkupDir   *dir,
                                       const char  *relative_key,
                                       GError     **err);
GSList*      markup_dir_list_entries (MarkupDir   *dir,
                                      GError     **err);
GSList*      markup_dir_list_subdirs (MarkupDir   *dir,
                                      GError     **err);

/* Value entries in the directory */
void        markup_entry_set_value (MarkupEntry       *entry,
                                    const GConfValue  *value,
                                    GError           **err);
GConfValue* markup_entry_get_value (MarkupEntry       *entry,
                                    const char       **locales,
                                    GError           **err);


#endif
