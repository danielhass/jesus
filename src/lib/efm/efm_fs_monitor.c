#define EFL_BETA_API_SUPPORT

#include <Eo.h>
#include <Ecore.h>
#include <Eio.h>

#include "efm_priv.h"

typedef struct
{
   Efm_Fs_File *origin;
   Eio_Monitor *mon; //< The eio monitor which is started in the directory
   Eio_File *file; //< The eio file as long as the ls is running

   Eina_Hash *file_icons; // < Hash table of all listed FM_Monitor_Files
   Efm_Filter *filter;

} Efm_Fs_Monitor_Data;

typedef struct {
  Eo *monitor;
} Efm_Monitor_Eio_Job;

#define MARK_POPULATED eo_key_data_set("__populated", ((void*)1));
#define UNMARK_POPULATED eo_key_data_del("__populated");
#define CHECK_POPULATED (eo_key_data_get("__populated") != NULL)

static inline Eina_Bool
_take_filter(Efm_Monitor *mon EINA_UNUSED, Efm_Fs_Monitor_Data *pd, Efm_File *file)
{
   Eina_Bool match;

   if (!pd->filter) return EINA_TRUE;

   eo_do(pd->filter, match = efm_filter_matches(file));

   return match;
}

static Eina_Bool
_file_del(void *data, Eo *obj, const Eo_Event_Description2 *desc EINA_UNUSED, void *event EINA_UNUSED)
{
   Efm_Fs_Monitor_Data *pd;

   pd = eo_data_scope_get(data, EFM_FS_MONITOR_CLASS);
   eina_hash_del_by_data(pd->file_icons, obj);
   return EO_CALLBACK_CONTINUE;
}

static void
_add(Efm_Monitor *mon, const char *file)
{
   const char *path, *filename;
   Efm_File *ef;
   Efm_Fs_Monitor_Data *pd;
   pd = eo_data_scope_get(mon, EFM_FS_MONITOR_CLASS);

   filename = ecore_file_file_get(file);

   eo_do(pd->origin, ef = efm_file_child_get(filename));

   if (!ef)
     {
       INF("Creation of %s failed, this is ... strange", file);
       return;
     }

   eo_do(ef,  path = efm_file_path_get();
              eo_event_callback_add(EFM_FILE_EVENT_INVALID, _file_del, mon));
   pd = eo_data_scope_get(mon, EFM_FS_MONITOR_CLASS);

   //prevent a file from beeing populated twice
   if (eina_hash_find(pd->file_icons, path)) return;

   eina_hash_add(pd->file_icons, path, ef);

   if (!_take_filter(mon, pd, ef))
     {
        eo_do(ef, UNMARK_POPULATED);
        return;
     }

   eo_do(mon, eo_event_callback_call(EFM_MONITOR_EVENT_FILE_ADD, ef));
   eo_do(ef, MARK_POPULATED);
}

void
_error(Efm_Monitor *efm)
{
   //tell everyone that this monitor is crashed now
   eo_do(efm, eo_event_callback_call(EFM_MONITOR_EVENT_ERROR, NULL));
   //delete the monitor
   eo_del(efm);
}

static void
_refresh_files(Efm_Monitor *mon, Efm_Fs_Monitor_Data *pd)
{
   Eina_Iterator *it;
   Efm_File *ef;

   it = eina_hash_iterator_data_new(pd->file_icons);

   EINA_ITERATOR_FOREACH(it, ef)
     {
        Eina_Bool populated;
        eo_do(ef, populated = CHECK_POPULATED ? EINA_TRUE : EINA_FALSE);
        // this is a hidden file
        if (_take_filter(mon, pd, ef) && !populated)
          {
             eo_do(ef, MARK_POPULATED);
             eo_do(mon, eo_event_callback_call(EFM_MONITOR_EVENT_FILE_ADD, ef));
          }
        else if (!_take_filter(mon, pd, ef) && populated)
          {
             eo_do(ef, UNMARK_POPULATED);
             eo_do(mon, eo_event_callback_call(EFM_MONITOR_EVENT_FILE_HIDE, ef));
          }
     }
}

static void
_fm_action(void *data EINA_UNUSED, Efm_Monitor *mon, const char *file, Fm_Action action)
{
   if (action == ADD)
     {
       _add(mon, file);
     }
   else if (action == SELFDEL)
     {
       //delete the monitor
       eo_del(mon);
     }
   else
     {
        _error(mon);
     }
}

static Eina_Bool
_filter_changed_cb(void *data, Eo *obj EINA_UNUSED, const Eo_Event_Description2 *desc EINA_UNUSED, void *event EINA_UNUSED)
{
   Efm_Fs_Monitor_Data *pd = eo_data_scope_get(data, EFM_FS_MONITOR_CLASS);

   _refresh_files(data, pd);
   return EO_CALLBACK_CONTINUE;
}

EOLIAN static void
_efm_fs_monitor_efm_monitor_filter_set(Eo *obj, Efm_Fs_Monitor_Data *pd, Efm_Filter *filter)
{

   if (filter != pd->filter)
     {
        if (pd->filter)
          {
             eo_do(pd->filter, eo_event_callback_del(EFM_FILTER_EVENT_FILTER_CHANGED, _filter_changed_cb, obj));
             eo_do(pd->filter, eo_wref_del(&pd->filter));
          }

        pd->filter = filter;
        if (pd->filter)
          {
             eo_do(pd->filter, eo_event_callback_add(EFM_FILTER_EVENT_FILTER_CHANGED, _filter_changed_cb, obj));
             eo_do(pd->filter, eo_wref_add(&pd->filter));
          }
        _refresh_files(obj, pd);
     }

}

EOLIAN static Efm_Filter *
_efm_fs_monitor_efm_monitor_filter_get(Eo *obj EINA_UNUSED, Efm_Fs_Monitor_Data *pd)
{
   return pd->filter;
}

static Eina_Bool
_eio_filter_cb(void *data EINA_UNUSED, Eio_File *handler EINA_UNUSED, const char *file EINA_UNUSED)
{
   // We take everything, so the configuration can be easily changed later
   return EINA_TRUE;
}

static void
_eio_main_cb(void *data, Eio_File *handler EINA_UNUSED, const char *file)
{
  Efm_Monitor_Eio_Job *job;

  job = data;

  if (!job->monitor)
    return;

   _add(job->monitor, file);
}

static void
_eio_done_cb(void *data, Eio_File *handler EINA_UNUSED)
{
   Efm_Monitor_Eio_Job *job;
   Efm_Fs_Monitor_Data *pd;
   Efm_Monitor *mon;
   const char *dir;
   job = data;

   if (!job->monitor)
     return;

   pd = eo_data_scope_get(job->monitor, EFM_FS_MONITOR_CLASS);
   //free the eio file
   pd->file = NULL;

   //free the job of the monitor
   mon = job->monitor;
   eo_do(job->monitor, eo_wref_del(&job->monitor));
   free(job);

   //call changed event
   eo_do(mon, eo_event_callback_call(EFM_MONITOR_EVENT_LISTING_DONE, NULL));

   //start monitoring
   eo_do_ret(pd->origin, dir, efm_file_path_get());
   pd->mon = eio_monitor_stringshared_add(dir);
   fm_monitor_add(mon, pd->mon, _fm_action);

}

static void
_eio_error_cb(void *data, Eio_File *handler EINA_UNUSED, int error EINA_UNUSED)
{
   Efm_Monitor_Eio_Job *job;

   job = data;

   if (job->monitor)
     return;

   _error(job->monitor);

   eo_do(job->monitor, eo_wref_del(&job->monitor));
   free(job);
}

static void
_del_cb(void *data)
{
   eo_unref(data);
}

EOLIAN static Eo_Base*
_efm_fs_monitor_eo_base_constructor(Eo *obj, Efm_Fs_Monitor_Data *pd)
{
   Eo_Base *construct;

   eo_do_super(obj, EFM_FS_MONITOR_CLASS, construct = eo_constructor());

   pd->file_icons = eina_hash_string_superfast_new(_del_cb);

   return construct;
}

EOLIAN static Eo_Base*
_efm_fs_monitor_eo_base_finalize(Eo *obj, Efm_Fs_Monitor_Data *pd)
{
   Eo_Base *base;
   Efm_Monitor_Eio_Job *job;
   const char *dir;

   eo_do_super(obj, EFM_FS_MONITOR_CLASS, base = eo_finalize());

   if (!pd->origin) {
     ERR("monitor does not have a set path");
     return NULL;
   }

   eo_do_ret(pd->origin, dir, efm_file_path_get());

   //prepare the listing
   job = calloc(1, sizeof(Efm_Monitor_Eio_Job));
   job->monitor = obj;
   eo_do(obj, eo_wref_add(&job->monitor));

   //start the listing
   pd->file = eio_file_ls(dir, _eio_filter_cb, _eio_main_cb,
                           _eio_done_cb, _eio_error_cb, job);
   return base;
}

static Eina_Bool
_inv_file_cb(void *data, Eo *obj EINA_UNUSED, const Eo_Event_Description2 *desc EINA_UNUSED, void *event_info EINA_UNUSED)
{
   eo_do(data, eo_event_callback_call(EFM_MONITOR_EVENT_ERROR, NULL));
   return EO_CALLBACK_CONTINUE;
}

EOLIAN static void
_efm_fs_monitor_install(Eo *obj, Efm_Fs_Monitor_Data *pd, Efm_Fs_File *file, Efm_Filter *filter)
{
   pd->origin = file;
   eo_ref(pd->origin);
   eo_do(pd->origin, eo_event_callback_add(EFM_FILE_EVENT_INVALID, _inv_file_cb, obj));
   eo_do(obj, efm_monitor_filter_set(filter));
}

EOLIAN static Efm_File*
_efm_fs_monitor_efm_monitor_file_get(Eo *obj EINA_UNUSED, Efm_Fs_Monitor_Data *pd)
{
   return pd->origin;
}

EOLIAN static void
_efm_fs_monitor_eo_base_destructor(Eo *obj, Efm_Fs_Monitor_Data *pd)
{

   if (pd->file)
     eio_file_cancel(pd->file);

   if (pd->filter)
     {
        eo_do(pd->filter, eo_event_callback_del(EFM_FILTER_EVENT_FILTER_CHANGED, _filter_changed_cb, obj));
        eo_do(pd->filter, eo_wref_del(&pd->filter));
     }

   eo_unref(pd->origin);

   fm_monitor_del(obj, pd->mon);
   eio_monitor_del(pd->mon);

   eina_hash_free(pd->file_icons);

   eo_do_super(obj, EFM_FS_MONITOR_CLASS, eo_destructor());
}
#include "efm_fs_monitor.eo.x"
