#if defined(MIYOOMINI)

#include "miyoo.h"
#include "gfx/video_driver.h"
#include "configuration.h"
#include "runloop.h"
#include "file_path_special.h"

static void show_miyoo_fullscreen_notification(settings_t* settings){
   char msg[80];
   struct retro_message_ext msg_obj = {0};

   msg[0] = '\0';

   snprintf(msg, sizeof(msg), "Aspect: %s. %s.",
      settings->bools.video_dingux_ipu_keep_aspect ? "Original" : "4:3",
      settings->bools.video_scale_integer ? "1x Scale" : "Screen Fill");

   msg_obj.msg      = msg;
   msg_obj.duration = 1000;
   msg_obj.priority = 3;
   msg_obj.level    = RETRO_LOG_INFO;
   msg_obj.target   = RETRO_MESSAGE_TARGET_ALL;
   msg_obj.type     = RETRO_MESSAGE_TYPE_STATUS;
   msg_obj.progress = -1;

   runloop_environment_cb(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &msg_obj);
}

static void write_core_override_aspect_scale(settings_t* settings){
   // Saves the Aspect and Scaling settings to the core override file
   // It will use an existing file if it exists, otherwise a new one will be created
   // This logic is modified from `configuration.c::config_save_overrides()`.

   char override_path[PATH_MAX_LENGTH] = {0};
   char config_directory[PATH_MAX_LENGTH] = {0};
   config_file_t* conf = NULL;

   /* Used to get running core */
   rarch_system_info_t* system = &runloop_state_get_ptr()->system;

   /* Get base config directory */
   fill_pathname_application_special(config_directory,
         sizeof(config_directory),
         APPLICATION_SPECIAL_DIRECTORY_CONFIG);

   /* Get override file path - Core Override */
   fill_pathname_join_special_ext(override_path,
                  config_directory, system->info.library_name,
                  system->info.library_name,
                  FILE_PATH_CONFIG_EXTENSION,
                  sizeof(override_path));

   // Use existing override file, if exists, or create new config file
   if (!(conf = config_file_new_from_path_to_string(override_path)))
         conf = config_file_new_alloc();

   // Set the two overrides - leave everything else as-is
   config_set_string(conf, "video_dingux_ipu_keep_aspect",
      (settings->bools.video_dingux_ipu_keep_aspect) ? "true" : "false");

   config_set_string(conf, "video_scale_integer",
      (settings->bools.video_scale_integer) ? "true" : "false");

   // Write override file
   bool ret = config_file_write(conf, override_path, false);
   config_file_free(conf);
}

void miyoo_event_fullscreen_impl(settings_t* settings){
   /* Toggle Aspect & Scaling */
   settings->bools.video_dingux_ipu_keep_aspect = !settings->bools.video_dingux_ipu_keep_aspect;
   if (!settings->bools.video_dingux_ipu_keep_aspect) {
      settings->bools.video_scale_integer = !settings->bools.video_scale_integer;
   }

   video_driver_apply_state_changes();

   show_miyoo_fullscreen_notification(settings);
   write_core_override_aspect_scale(settings);
}
#endif
