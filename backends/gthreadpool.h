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

#ifndef GCONF_GTHREADPOOL_H
#define GCONF_GTHREADPOOL_H

#include <glib.h>
/*
  #ifdef __cplusplus
  extern "C" {
  #endif
*/

typedef struct _GThreadPool GThreadPool;

struct _GThreadPool {
  gpointer dummy;
}

/* this can return NULL on failure and sets errno */
GThreadPool* g_thread_pool_new          (guint max_threads);
/* block until all tasks are finished, then return. */
void         g_thread_pool_finish_all   (GThreadPool* pool);
void         g_thread_pool_destroy      (GThreadPool* pool);

typedef gpointer (*GWorkerFunc)             (gpointer data);
typedef void     (*GWorkFinishedNotifyFunc) (gpointer result);

/* Your work func should obviously be thread safe :-) */
/* The return value of the worker func invocation is passed to the
 * GWorkFinishedNotifyFunc to tell you there are results.
 * GWorkFinishedNotifyFunc is a called from a glib main loop source,
 * so you need to be using the glib main loop to use GThreadPool
 */
   
void         g_thread_pool_do_work  (GThreadPool* pool,
                                     GWorkerFunc work_func,
                                     GWorkFinishedNotifyFunc notify_func,
                                     gpointer data);

/*
  #ifdef __cplusplus
  }
  #endif
*/

#endif



