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


#include "gthreadpool.h"
#include <pthread.h>

typedef struct _Task Task;

struct _Task {
  GWorkerFunc work_func;
  GWorkFinishedNotifyFunc notify_func;
  gpointer data;
  gpointer result;
}

typedef struct _GThreadPoolPrivate GThreadPoolPrivate;

typedef struct _WorkerThread WorkerThread;

struct _WorkerThread {
  Task* task;
  pthread_t thread;
  GThreadPoolPrivate* pool;
  gboolean destroyed;
};

struct _GThreadPoolPrivate {
  guint max_threads;
  guint thread_count;
  GSList* threads;

  pthread_mutex_t worker_mutex;
  pthread_cond_t worker_cond;
  pthread_cond_t empty_incoming_cond;
  guint incoming_task_count;
  GSList* incoming_tasks;
  guint busy_task_count;
  GSList* busy_tasks;
  guint finished_task_count;
  GSList* finished_tasks;

  guint notify_pipe_io_watcher_id;
  int notify_pipe[2];
  GIOChannel* notify_channel;
}

static void*
do_worker_thread(void* _wt)
{
  WorkerThread* wt = (WorkerThread*)_wt;

  while (TRUE)
    {
      /* Wait until there are incoming tasks or we should
         exit */
      pthread_mutex_lock(&(wt->pool->worker_mutex));

      while (wt->pool->incoming_task_count == 0 &&
             (!wt->destroyed))
        {
          pthread_cond_wait(&(wt->pool->worker_cond),
                            &(wt->pool->worker_mutex));
        }

      /* Exit if requested */
      if (wt->destroyed)
        {
          pthread_mutex_unlock(&(wt->pool->worker_mutex));
          pthread_exit(NULL);
          return NULL; /* not reached */
        }

      /* Otherwise, take the first incoming task */
      
      g_assert(wt->pool->incoming_task_count > 0);

      wt->pool->incoming_task_count -= 1;
      wt->task = wt->pool->incoming_tasks->data;
      wt->pool->incoming_tasks = g_slist_remove(wt->pool->incoming_tasks,
                                                wt->task);

      g_assert(wt->task != NULL);

      wt->pool->busy_task_count += 1;
      wt->pool->busy_tasks = g_slist_prepend(wt->pool->busy_tasks,
                                             wt->task);

      pthread_mutex_unlock(&(wt->pool->worker_mutex));

      /* Perform the task; we release the lock,
       * so other threads can get started or stopped, and also
       * so conceivably the task can add more tasks to the pool
       * (however that sounds like it could be a bad idea...)
       */
      
      wt->task->result = (*wt->task->work_func)(wt->task->data);

      /* Move the task onto the finished list */
      
      pthread_mutex_lock(&(wt->pool->worker_mutex));

      g_assert(wt->pool->busy_task_count > 0);
      
      wt->pool->busy_task_count -= 1;
      wt->pool->busy_tasks = g_slist_remove(wt->pool->busy_tasks,
                                            wt->task);

      wt->pool->finished_task_count += 1;
      wt->pool->finished_tasks = g_slist_prepend(wt->pool->finished_tasks,
                                                 wt->task);

      /* Forget about our task */
      wt->task = NULL;

      /* Notify the glib main loop (via an input handler on this pipe)
       * that there are finished tasks
       */

    try_again:
      if (write(wt->pool->notify_pipe[1], "g", 1) < 0)
        {
          if (errno == EINTR)
            goto try_again;
          else
            g_warning("Write failure with pipe notification: %s", strerror(errno)); /* shouldn't happen */
        }
      
      /* Notify main thread (g_thread_pool_destroy()) if
       * we emptied the queue
       */

      if (wt->pool->incoming_task_count == 0)
        pthread_cond_signal(&(wt->pool->empty_incoming_cond));
      
      pthread_mutex_unlock(&(wt->pool->worker_mutex));

      /* Continue forever */
    }
}

/* These should be called from the main thread */

static WorkerThread*
worker_thread_new(void)
{
  int rtn;
  WorkerThread* wt;

  wt = g_new0(WorkerThread, 1);

  wt->task = NULL;
  wt->destroyed = FALSE;
  
  rtn = pthread_create(&wt->thread, NULL, do_worker_thread, wt);

  if (rtn != 0)
    {
      fprintf(stderr, "Failed to create thread: %s\n", strerror(rtn));
      g_free(wt);
      return NULL;
    }

  return wt;
}

static void
worker_thread_destroy(WorkerThread* wt)
{
  pthread_join(wt->thread, NULL);
  g_free(wt);
}

/* note that this function doesn't do locking */
static void
g_thread_pool_notify(GThreadPool* pool)
{
  GThreadPoolPrivate* priv = (GThreadPoolPrivate*)pool;
  GSList* tmp;
  
  tmp = priv->finished_tasks;
  while (tmp != NULL)
    {
      Task* task = tmp->data;

      (*task->notify_func)(task->result);

      g_free(task);
      
      tmp = g_slist_next(tmp);
    }

  g_slist_free(priv->finished_tasks);
  priv->finished_task_count = 0;
  priv->finished_tasks = NULL;
}

gboolean
notify_callback (GIOChannel *source,
                 GIOCondition condition,
                 gpointer data)
{
  GThreadPoolPrivate* priv = data;
  guint count;
  gchar* buf;
  
  pthread_mutex_lock(&(wt->pool->worker_mutex));

  count = priv->finished_task_count;

  if (count == 0)
    {
      g_warning("Strange, we got a notify callback with no finished tasks");
      pthread_mutex_unlock(&(wt->pool->worker_mutex));
      return;
    }
  
  g_thread_pool_notify((GThreadPool*)priv);

  g_assert(priv->finished_task_count == 0 &&
           priv->finished_tasks == NULL);

  /* read notify bytes from the pipe */

  buf = g_malloc(count);
 again:
  if (read(priv->notify_pipe[0], buf, count) < 0)
    {
      if (errno == EINTR)
        goto again;
      else
        {
          /* shouldn't happen */
          g_warning("Read failed from notify pipe: %s", strerror(errno));
        }
    }
  /* discard the bytes */
  g_free(buf);
  
  pthread_mutex_unlock(&(wt->pool->worker_mutex));
}

GThreadPool*
g_thread_pool_new      (guint max_threads)
{
  GThreadPoolPrivate* priv;
  int ret;

  g_return_val_if_fail(max_threads > 0, NULL);
  
  priv = g_new0(GThreadPoolPrivate, 1);

  priv->max_threads = max_threads;

  if (pipe(priv->notify_pipe) < 0)
    {
      g_free(priv);
      return NULL;
    }
  
  ret = pthread_cond_init(&(priv->worker_cond));
  if (ret != 0)
    {
      fprintf(stderr, "Failed to init pthread_cond: %s",
              strerror(ret));
      exit(1);
    }

  ret = pthread_cond_init(&(priv->empty_incoming_cond));
  if (ret != 0)
    {
      fprintf(stderr, "Failed to init pthread_cond: %s",
              strerror(ret));
      exit(1);
    }
  
  ret = pthread_mutex_init(&(priv->worker_mutex));
  if (ret != 0)
    {
      fprintf(stderr, "Failed to init pthread_mutex: %s",
              strerror(ret));
      exit(1);
    }

  priv->notify_channel = g_io_channel_unix_new(priv->notify_pipe[0]);
  
  priv->notify_pipe_io_watcher_id = g_io_add_watch(priv->notify_channel,
                                                   G_IO_IN,
                                                   notify_callback,
                                                   priv);
  
  return (GThreadPool*)priv;
}

void
g_thread_pool_destroy  (GThreadPool* pool)
{
  GThreadPoolPrivate* priv = (GThreadPoolPrivate*)pool;
  GSList* tmp;
  
  pthread_mutex_lock(&(priv->worker_mutex));

  /* Remove the notify event source */
  g_source_remove(priv->work_notify_source_id);

  /* close the pipe */
  close(priv->notify_pipe[0]);
  close(priv->notify_pipe[1]);
  
  /* Clear the queue */
  while (priv->incoming_task_count != 0)
    {
      g_assert(priv->thread_count > 0);

      pthread_cond_wait(&(priv->empty_incoming_cond),
                        &(priv->worker_mutex));
    }

  g_assert(priv->incoming_task_count == 0);

  /* Now do notification if any tasks were just finished */

  g_thread_pool_notify(pool);
  
  /* Mark all worker threads destroyed */

  tmp = priv->threads;
  while (tmp != NULL)
    {
      WorkerThread* wt = tmp->data;

      g_assert(wt->task == NULL);
      
      wt->destroyed = TRUE;

      tmp = g_slist_next(tmp);
    }

  /* Wake up all threads so they notice they are destroyed and exit */
  pthread_cond_broadcast(&(priv->worker_cond));
  pthread_mutex_unlock(&(priv->worker_mutex));
  
  /* Now delete all the threads */
  tmp = priv->threads;
  while (tmp != NULL)
    {
      WorkerThread* wt = tmp->data;

      worker_thread_destroy(wt);
      
      tmp = g_slist_next(tmp);
    }

  g_slist_free(priv->threads);
  priv->threads = NULL;
  priv->thread_count = 0;

  g_free(priv);
}

void
g_thread_pool_finish_all   (GThreadPool* pool)
{
  GThreadPoolPrivate* priv = (GThreadPoolPrivate*)pool;
  
  pthread_mutex_lock(&(priv->worker_mutex));
  
  /* Clear the queue */
  while (priv->incoming_task_count != 0)
    {
      g_assert(priv->thread_count > 0);

      pthread_cond_wait(&(priv->empty_incoming_cond),
                        &(priv->worker_mutex));
    }

  g_assert(priv->incoming_task_count == 0);

  pthread_mutex_unlock(&(priv->worker_mutex));
}

void
g_thread_pool_do_work  (GThreadPool* pool,
                        GWorkerFunc work_func,
                        gpointer data)
{
  GThreadPoolPrivate* priv = (GThreadPoolPrivate*)pool;
  Task* task;

  task = g_new0(Task, 1);

  task->work_func = work_func;
  task->notify_func = notify_func;
  task->data = data;

  pthread_mutex_lock(&(priv->worker_mutex));

  priv->incoming_task_count += 1;
  priv->incoming_tasks = g_slist_prepend(priv->incoming_tasks,
                                         task);

  if ((priv->thread_count < priv->max_threads) && 
      ((priv->busy_task_count + priv->incoming_task_count) > priv->thread_count))
    {
      /* Add another worker thread */
      WorkerThread* wt = worker_thread_new();

      priv->threads = g_slist_prepend(priv->threads, wt);
      priv->thread_count += 1;
    }

  /* Wake up one of the waiting threads */
  pthread_cond_signal(&(priv->worker_cond));
  
  pthread_mutex_unlock(&(priv->worker_mutex));
}



