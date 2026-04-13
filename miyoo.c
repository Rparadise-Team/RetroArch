#if defined(MIYOOMINI)

#include "miyoo.h"
#include "configuration.h"
#include "command.h"
#include "file/config_file.h"
#include "file/file_path.h"
#include "file_path_special.h"
#include "gfx/gfx_widgets.h"
#include "gfx/video_driver.h"
#include "menu/menu_driver.h"
#include "msg_hash.h"
#include "paths.h"
#include "runloop.h"
#include "streams/file_stream.h"
#include "string/stdstring.h"
#include "verbosity.h"
#ifdef HAVE_CHEEVOS
#include "cheevos/cheevos.h"
#endif
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

/**
 * @brief Displays an on-screen notification of the current scaling option.
 * 
 * The 4 states are:
 * 1. Integer scaling: OFF (Original)
 * 2. Integer scaling: OFF (4:3)
 * 3. Integer scaling: ON (Original)
 * 4. Integer scaling: ON (4:3)
 * 
 * @param settings 
 */
static void show_miyoo_fullscreen_notification(settings_t *settings)
{
    char msg[PATH_MAX_LENGTH];
    struct retro_message_ext msg_obj = {0};

    msg[0] = '\0';

    snprintf(msg, sizeof(msg), "Integer scaling: %s (%s)",
             settings->bools.video_scale_integer ? "ON" : "OFF",
             settings->bools.video_dingux_ipu_keep_aspect ? "Original" : "4:3");

    msg_obj.msg = msg;
    msg_obj.duration = 1000;
    msg_obj.priority = 3;
    msg_obj.level = RETRO_LOG_INFO;
    msg_obj.target = RETRO_MESSAGE_TARGET_ALL;
    msg_obj.type = RETRO_MESSAGE_TYPE_STATUS;
    msg_obj.progress = -1;

    runloop_environment_cb(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &msg_obj);
}

/**
 * @brief Get the override path for a specific type
 * 
 * @param override_path Output parameter for override path
 * @param type Override type
 */
static void get_override_path(char *override_path, enum override_type type)
{
    char config_directory[PATH_MAX_LENGTH];
    char content_dir_name[PATH_MAX_LENGTH];
    rarch_system_info_t *system = &runloop_state_get_ptr()->system;
    const char *game_name = NULL;
    const char *core_name = system ? system->info.library_name : NULL;
    const char *rarch_path_basename = path_get(RARCH_PATH_BASENAME);
    bool has_content = !string_is_empty(rarch_path_basename);

    /* > Cannot save an override if we have no core
    * > Cannot save a per-game or per-content-directory
    *   override if we have no content */
    if (string_is_empty(core_name) || (!has_content && type != OVERRIDE_CORE))
        return;

    /* Get base config directory */
    fill_pathname_application_special(config_directory,
                                      sizeof(config_directory),
                                      APPLICATION_SPECIAL_DIRECTORY_CONFIG);

    switch (type) {
    case OVERRIDE_CORE:
        fill_pathname_join_special_ext(override_path,
                                       config_directory, core_name,
                                       core_name,
                                       FILE_PATH_CONFIG_EXTENSION,
                                       PATH_MAX_LENGTH);
        break;
    case OVERRIDE_GAME:
        game_name = path_basename_nocompression(rarch_path_basename);
        fill_pathname_join_special_ext(override_path,
                                       config_directory, core_name,
                                       game_name,
                                       FILE_PATH_CONFIG_EXTENSION,
                                       PATH_MAX_LENGTH);
        break;
    case OVERRIDE_CONTENT_DIR:
        fill_pathname_parent_dir_name(content_dir_name,
                                      rarch_path_basename, sizeof(content_dir_name));
        fill_pathname_join_special_ext(override_path,
                                       config_directory, core_name,
                                       content_dir_name,
                                       FILE_PATH_CONFIG_EXTENSION,
                                       PATH_MAX_LENGTH);
        break;
    case OVERRIDE_NONE:
    default:
        break;
    }
}

/**
 * @brief Checks if a config override of [type] exists and contains either the "keep aspect" or "integer scaling" option
 * 
 * @param override_path Override path
 * @return true Config override contains scaling options - use it
 * @return false Config override does not contain scaling options
 */
static bool check_config_has_scaling(const char *override_path)
{
    bool ret = false;
    config_file_t *conf = NULL;

    if ((conf = config_file_new_from_path_to_string(override_path))) {
        ret = !!config_get_entry(conf, "video_dingux_ipu_keep_aspect") || !!config_get_entry(conf, "video_scale_integer");
        config_file_free(conf);
    }

    return ret;
}

/**
 * @brief Saves the "keep aspect" and "integer scaling" options as a config override.
 * 
 * @param settings 
 * @return true Config override was saved successfully
 * @return false Config override was not saved
 */
static bool write_core_override_aspect_scale(settings_t *settings)
{
    char override_path[PATH_MAX_LENGTH] = {0};
    config_file_t *conf = NULL;

    get_override_path(override_path, OVERRIDE_GAME);

    if (!path_is_directory(override_path) || !check_config_has_scaling(override_path)) {
        get_override_path(override_path, OVERRIDE_CONTENT_DIR);

        if (!path_is_directory(override_path)) {
            get_override_path(override_path, OVERRIDE_CORE);
        }
    }

    if (string_is_empty(override_path))
        return false;

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

    return ret;
}

#if defined(MIYOO_CUSTOM_MENU)
#define MIYOO_STATE_SLOT_COUNT 10

static bool miyoo_menu_active            = false;
static bool miyoo_native_quickmenu_open  = false;
static int miyoo_state_menu_mode         = 0;
static bool miyoo_cpu_menu_open          = false;
static bool miyoo_netplay_menu_open      = false;
static bool miyoo_netplay_saved_cheevos_valid  = false;
static bool miyoo_netplay_saved_cheevos_enable = false;
static bool miyoo_netplay_cheevos_suspended    = false;
static char miyoo_netplay_cfg_path[PATH_MAX_LENGTH] = {0};
static bool miyoo_achievements_menu_open = false;
static size_t miyoo_achievements_parent  = 0;
int miyoo_gfx_apply_cpuclock(int clock);

static bool miyoo_cpu_clock_build_core_path(char *out_path, size_t len, const char *file_name);
static bool miyoo_cpu_clock_write(long clock_hz);
static bool miyoo_cpu_clock_apply_target(long clock_hz);
static bool miyoo_cpu_clock_read(long *clock_hz);
static bool miyoo_cpu_clock_matches_target(long target_hz);
static bool miyoo_cpu_clock_sync_initialized = false;

void miyoo_menu_context_begin(void)
{
    bool flush_stack = true;
    long clock_hz    = 0;

    miyoo_menu_active           = true;
    miyoo_native_quickmenu_open = false;
    menu_driver_ctl(RARCH_MENU_CTL_SET_PENDING_QUICK_MENU, &flush_stack);

    if (miyoo_cpu_clock_read(&clock_hz))
       miyoo_cpu_clock_apply_target(clock_hz);
}

void miyoo_menu_context_end(void)
{
    long clock_hz = 0;

    miyoo_menu_active           = false;
    miyoo_native_quickmenu_open = false;
    miyoo_state_menu_mode       = 0;
    miyoo_cpu_menu_open         = false;
    miyoo_netplay_menu_open     = false;
    miyoo_achievements_menu_open = false;
    miyoo_achievements_parent    = 0;

    if (miyoo_cpu_clock_read(&clock_hz))
       miyoo_cpu_clock_apply_target(clock_hz);
}

bool miyoo_menu_context_active(void)
{
    return miyoo_menu_active;
}

bool miyoo_menu_context_is_native_quickmenu(void)
{
    return miyoo_menu_active && miyoo_native_quickmenu_open;
}

void miyoo_menu_open_native_quickmenu(void)
{
    bool flush_stack         = true;
    struct menu_state *menu_st = menu_state_get_ptr();

    if (!miyoo_menu_active || miyoo_native_quickmenu_open)
        return;

    menu_driver_ctl(RARCH_MENU_CTL_SET_PENDING_QUICK_MENU, &flush_stack);
    miyoo_native_quickmenu_open = true;

    /* Ensure menu loop is running immediately so quick menu
     * is rendered/interactable without requiring extra input. */
    if (!menu_st || !(menu_st->flags & MENU_ST_FLAG_ALIVE))
        command_event(CMD_EVENT_MENU_TOGGLE, NULL);
}

void miyoo_menu_resume_from_native_quickmenu(void)
{
    if (!miyoo_menu_active)
        return;

    miyoo_native_quickmenu_open = false;
}

static bool miyoo_cpu_clock_build_core_path(char *out_path, size_t len, const char *file_name);
static bool miyoo_cpu_clock_write(long clock_hz);
static bool miyoo_cpu_clock_apply_target(long clock_hz);
static bool miyoo_cpu_clock_read(long *clock_hz);

static long miyoo_cpu_clock_cached_hz       = 0;
static bool miyoo_cpu_clock_runtime_override = false;

static bool miyoo_cpu_clock_read_from_file(const char *path, long *clock_hz)
{
    RFILE *fp    = NULL;
    char buf[64] = {0};

    if (string_is_empty(path) || !clock_hz)
        return false;

    fp = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_NONE);
    if (!fp)
        return false;

    if (filestream_gets(fp, buf, sizeof(buf)))
        *clock_hz = strtol(buf, NULL, 10);
    filestream_close(fp);

    return (*clock_hz > 0);
}

static bool miyoo_cpu_clock_read_from_system(long *clock_hz)
{
    const char *clock_paths[] = {
        "/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq",
        "/sys/devices/system/cpu/cpufreq/policy0/scaling_setspeed",
        "/sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq"
    };
    size_t i;

    if (!clock_hz)
        return false;

    for (i = 0; i < (sizeof(clock_paths) / sizeof(clock_paths[0])); i++)
    {
        if (miyoo_cpu_clock_read_from_file(clock_paths[i], clock_hz))
            return true;
    }

    return (*clock_hz > 0);
}

static bool miyoo_cpu_clock_matches_target(long target_hz)
{
    long current_hz = 0;
    long diff       = 0;

    if (target_hz <= 0)
        return false;

    if (!miyoo_cpu_clock_read_from_system(&current_hz) || current_hz <= 0)
        return false;

    diff = current_hz - target_hz;
    if (diff < 0)
       diff = -diff;

    return diff <= 50000;
}

static bool miyoo_cpu_clock_read(long *clock_hz)
{
    char path[PATH_MAX_LENGTH];
    char rom_clock_file[PATH_MAX_LENGTH];
    rarch_system_info_t *system            = &runloop_state_get_ptr()->system;
    const char *core_name                  = system ? system->info.library_name : NULL;
    const char *rarch_path_basename        = path_get(RARCH_PATH_BASENAME);
    const char *rom_name                   = path_basename_nocompression(rarch_path_basename);
    bool from_system                        = false;

    if (!clock_hz)
        return false;

    if (miyoo_cpu_clock_runtime_override && miyoo_cpu_clock_cached_hz > 0)
    {
       *clock_hz = miyoo_cpu_clock_cached_hz;
       return true;
    }

    *clock_hz = 0;

    if (!string_is_empty(core_name))
    {
       if (!string_is_empty(rom_name))
       {
          snprintf(rom_clock_file, sizeof(rom_clock_file), "%s-cpu", rom_name);
          if (miyoo_cpu_clock_build_core_path(path, sizeof(path), rom_clock_file)
                && miyoo_cpu_clock_read_from_file(path, clock_hz))
             goto found;
       }

       if (miyoo_cpu_clock_build_core_path(path, sizeof(path), "cpuclock")
             && miyoo_cpu_clock_read_from_file(path, clock_hz))
          goto found;
    }

    if (miyoo_cpu_clock_read_from_file("/mnt/SDCARD/.simplemenu/cpu.sav", clock_hz))
       goto found;

    if (!miyoo_cpu_clock_read_from_system(clock_hz))
    {
       if (miyoo_cpu_clock_cached_hz > 0)
       {
          *clock_hz = miyoo_cpu_clock_cached_hz;
          return true;
       }
       return false;
    }
    from_system = true;

found:
    miyoo_cpu_clock_cached_hz = *clock_hz;

    if (!from_system && !miyoo_cpu_clock_sync_initialized && *clock_hz > 0)
    {
       if (miyoo_gfx_apply_cpuclock((int)(*clock_hz)) != 0)
          miyoo_cpu_clock_write(*clock_hz);
       miyoo_cpu_clock_sync_initialized = true;
    }

    return (*clock_hz > 0);
}

static bool miyoo_cpu_clock_write(long clock_hz)
{
    const char *governor_paths[] = {
        "/sys/devices/system/cpu/cpufreq/policy0/scaling_governor",
        "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"
    };
    const char *clock_paths[] = {
        "/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq",
        "/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq",
        "/sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq",
        "/sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq",
        "/sys/devices/system/cpu/cpufreq/policy0/scaling_setspeed"
    };
    const char *userspace = "userspace";
    char buf[64] = {0};
    int n        = snprintf(buf, sizeof(buf), "%ld", clock_hz);
    size_t i;
    bool wrote_any = false;

    if (n <= 0)
        return false;

    for (i = 0; i < (sizeof(governor_paths) / sizeof(governor_paths[0])); i++)
    {
        RFILE *fp = filestream_open(governor_paths[i], RETRO_VFS_FILE_ACCESS_WRITE,
              RETRO_VFS_FILE_ACCESS_HINT_NONE);

        if (!fp)
            continue;

        filestream_write(fp, userspace, strlen(userspace));
        filestream_close(fp);
    }

    for (i = 0; i < (sizeof(clock_paths) / sizeof(clock_paths[0])); i++)
    {
        RFILE *fp = filestream_open(clock_paths[i], RETRO_VFS_FILE_ACCESS_WRITE,
              RETRO_VFS_FILE_ACCESS_HINT_NONE);

        if (!fp)
            continue;

        filestream_write(fp, buf, (size_t)n);
        filestream_close(fp);
        wrote_any = true;
    }

    if (wrote_any)
       miyoo_cpu_clock_cached_hz = clock_hz;

    return wrote_any;
}

static bool miyoo_cpu_clock_apply_target(long clock_hz)
{
    bool applied_runtime = false;
    bool applied_sysfs   = false;

    if (clock_hz <= 0)
        return false;

    if (miyoo_gfx_apply_cpuclock((int)clock_hz) == 0)
       applied_runtime = miyoo_cpu_clock_matches_target(clock_hz);

    applied_sysfs = miyoo_cpu_clock_write(clock_hz);

    if (!applied_runtime && applied_sysfs)
       applied_runtime = miyoo_cpu_clock_matches_target(clock_hz);

    return applied_runtime || applied_sysfs;
}

bool miyoo_menu_cpu_clock_get_runtime_override(long *clock_hz)
{
    if (!clock_hz)
        return false;
    if (!miyoo_cpu_clock_runtime_override || miyoo_cpu_clock_cached_hz <= 0)
        return false;

    *clock_hz = miyoo_cpu_clock_cached_hz;
    return true;
}

static bool miyoo_cpu_clock_build_core_path(char *out_path, size_t len, const char *file_name)
{
    char config_directory[PATH_MAX_LENGTH];
    char core_directory[PATH_MAX_LENGTH];
    rarch_system_info_t *system = &runloop_state_get_ptr()->system;
    const char *core_name       = system ? system->info.library_name : NULL;

    if (!out_path || string_is_empty(file_name) || string_is_empty(core_name))
        return false;

    fill_pathname_application_special(config_directory,
                                      sizeof(config_directory),
                                      APPLICATION_SPECIAL_DIRECTORY_CONFIG);

    fill_pathname_join_special(core_directory, config_directory, core_name, sizeof(core_directory));
    if (!path_is_directory(core_directory))
       path_mkdir(core_directory);

    fill_pathname_join_special_ext(out_path, config_directory, core_name,
                                   file_name, ".txt", len);
    return true;
}

static int miyoo_cpu_clock_save_to_file(const char *file_name)
{
    long clock_hz = 0;
    char path[PATH_MAX_LENGTH];
    char data[64] = {0};
    RFILE *fp     = NULL;
    int n         = 0;

    if (!miyoo_cpu_clock_read(&clock_hz))
        return -1;
    if (!miyoo_cpu_clock_build_core_path(path, sizeof(path), file_name))
        return -1;

    n = snprintf(data, sizeof(data), "%ld\n", clock_hz);
    if (n <= 0)
        return -1;

    fp = filestream_open(path, RETRO_VFS_FILE_ACCESS_WRITE, RETRO_VFS_FILE_ACCESS_HINT_NONE);
    if (!fp)
        return -1;
    filestream_write(fp, data, (size_t)n);
    filestream_close(fp);
    return 0;
}

static void miyoo_menu_notify(const char *msg)
{
    if (string_is_empty(msg))
        return;
    runloop_msg_queue_push(msg, strlen(msg), 1, 90, true, NULL,
          MESSAGE_QUEUE_ICON_DEFAULT, MESSAGE_QUEUE_CATEGORY_INFO);
}

static bool miyoo_shell_escape_single_quotes(const char *in, char *out, size_t out_size)
{
    size_t in_pos = 0;
    size_t out_pos = 0;

    if (string_is_empty(in) || !out || out_size < 3)
       return false;

    out[out_pos++] = '\'';

    while (in[in_pos] != '\0')
    {
       if (out_pos + 6 >= out_size)
          return false;

       if (in[in_pos] == '\'')
       {
          out[out_pos++] = '\'';
          out[out_pos++] = '\\';
          out[out_pos++] = '\'';
          out[out_pos++] = '\'';
       }
       else
          out[out_pos++] = in[in_pos];

       in_pos++;
    }

    out[out_pos++] = '\'';
    out[out_pos] = '\0';
    return true;
}

static void miyoo_netplay_update_cheevos_cfg_file(const char *cfg_path, bool enable, bool append_if_missing)
{
    char escaped_cfg_path[(PATH_MAX_LENGTH * 2) + 8];
    char cmd[(PATH_MAX_LENGTH * 3) + 256];
    const char *value = enable ? "true" : "false";

    if (!miyoo_shell_escape_single_quotes(cfg_path, escaped_cfg_path, sizeof(escaped_cfg_path)))
       return;

    if (append_if_missing)
       snprintf(cmd, sizeof(cmd),
             "sed -i 's/^[[:space:]]*cheevos_enable[[:space:]]*=.*/cheevos_enable = \"%s\"/' %s; "
             "grep -q '^[[:space:]]*cheevos_enable[[:space:]]*=' %s || echo 'cheevos_enable = \"%s\"' >> %s",
             value, escaped_cfg_path, escaped_cfg_path, value, escaped_cfg_path);
    else
       snprintf(cmd, sizeof(cmd),
             "sed -i 's/^[[:space:]]*cheevos_enable[[:space:]]*=.*/cheevos_enable = \"%s\"/' %s",
             value, escaped_cfg_path);

    system(cmd);
}

static void miyoo_netplay_update_cheevos_cfg(bool enable)
{
    const char *cfg_path = path_get(RARCH_PATH_CONFIG);
    char core_cfg_path[PATH_MAX_LENGTH] = {0};
    rarch_system_info_t *system         = &runloop_state_get_ptr()->system;
    const char *core_name               = system ? system->info.library_name : NULL;
    char config_directory[PATH_MAX_LENGTH];

    if (string_is_empty(cfg_path))
       cfg_path = miyoo_netplay_cfg_path;
    else
       strlcpy(miyoo_netplay_cfg_path, cfg_path, sizeof(miyoo_netplay_cfg_path));

    if (!string_is_empty(cfg_path))
       miyoo_netplay_update_cheevos_cfg_file(cfg_path, enable, true);

    if (string_is_empty(core_name))
       return;

    fill_pathname_application_special(config_directory, sizeof(config_directory),
          APPLICATION_SPECIAL_DIRECTORY_CONFIG);
    fill_pathname_join_special(core_cfg_path, config_directory, core_name, sizeof(core_cfg_path));
    fill_pathname_slash(core_cfg_path, sizeof(core_cfg_path));
    strlcat(core_cfg_path, core_name, sizeof(core_cfg_path));
    strlcat(core_cfg_path, ".cfg", sizeof(core_cfg_path));

    if (path_is_valid(core_cfg_path))
       miyoo_netplay_update_cheevos_cfg_file(core_cfg_path, enable, false);
}

int miyoo_menu_action_resume(void)
{
    return command_event(CMD_EVENT_MENU_TOGGLE, NULL) ? 0 : -1;
}

int miyoo_menu_action_save_state(void)
{
    miyoo_menu_state_menu_open_save();
    return 0;
}

int miyoo_menu_action_load_state(void)
{
    miyoo_menu_state_menu_open_load();
    return 0;
}

int miyoo_menu_action_sync_now(void)
{
#ifdef HAVE_CLOUDSYNC
    return command_event(CMD_EVENT_CLOUD_SYNC, NULL) ? 0 : -1;
#else
    return -1;
#endif
}

int miyoo_menu_action_cpu_adjust(int delta_mhz)
{
    long current = 0;
    long target  = 0;

    if (!miyoo_cpu_clock_read(&current))
        return -1;

    target = current + ((long)delta_mhz * 1000L);
    if (target < 200000)
        target = 200000;
    else if (target > 1600000)
        target = 1600000;

    if (miyoo_cpu_clock_apply_target(target))
    {
        char msg[64];
        miyoo_cpu_clock_cached_hz = target;
        miyoo_cpu_clock_runtime_override = true;
        snprintf(msg, sizeof(msg), "CPU: %ld MHz", target / 1000L);
        miyoo_menu_notify(msg);
        return 0;
    }
    return -1;
}

int miyoo_menu_action_set_cpu_clock(int mhz)
{
    long target = (long)mhz * 1000L;

    if (target < 200000)
        target = 200000;
    else if (target > 1600000)
        target = 1600000;

    if (!miyoo_cpu_clock_apply_target(target))
        return -1;
    miyoo_cpu_clock_cached_hz = target;
    miyoo_cpu_clock_runtime_override = true;

    {
        char msg[64];
        snprintf(msg, sizeof(msg), "CPU: %ld MHz", target / 1000L);
        miyoo_menu_notify(msg);
    }
    return 0;
}

void miyoo_menu_cpu_menu_open(void)
{
    miyoo_cpu_menu_open = true;
}

void miyoo_menu_cpu_menu_close(void)
{
    miyoo_cpu_menu_open = false;
}

bool miyoo_menu_cpu_menu_is_open(void)
{
    return miyoo_cpu_menu_open;
}

int miyoo_menu_cpu_menu_get_index(void)
{
    static const int clocks_mhz[] = {200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100, 1200, 1300, 1400, 1500, 1600};
    long hz = miyoo_menu_cpu_clock_get_hz();
    int mhz = (int)(hz / 1000L);
    int i;
    int best_idx = 0;
    int best_delta = 0x7fffffff;

    for (i = 0; i < (int)(sizeof(clocks_mhz) / sizeof(clocks_mhz[0])); i++)
    {
        int delta = clocks_mhz[i] - mhz;
        if (delta < 0)
            delta = -delta;
        if (delta < best_delta)
        {
            best_delta = delta;
            best_idx   = i;
        }
    }

    return best_idx;
}

long miyoo_menu_cpu_clock_get_hz(void)
{
    long current = 0;
    if (!miyoo_cpu_clock_read(&current))
        return 0;
    return current;
}

int miyoo_menu_action_save_cpu_core(void)
{
    int ret = miyoo_cpu_clock_save_to_file("cpuclock");
    if (ret == 0)
    {
        long clock_hz                  = miyoo_menu_cpu_clock_get_hz();
        long clock_mhz                 = (clock_hz > 0) ? (clock_hz / 1000L) : 0;
        rarch_system_info_t *system    = &runloop_state_get_ptr()->system;
        const char *saved_label        = msg_hash_to_str(MSG_SAVED_SUCCESSFULLY_TO);
        const char *core_name          = (system && !string_is_empty(system->info.library_name))
              ? system->info.library_name
              : msg_hash_to_str(MENU_ENUM_LABEL_VALUE_NOT_AVAILABLE);
        char msg[256];

        if (clock_mhz > 0)
            snprintf(msg, sizeof(msg), "%s %s (%ld MHz)", saved_label, core_name, clock_mhz);
        else
            snprintf(msg, sizeof(msg), "%s %s", saved_label, core_name);
        miyoo_menu_notify(msg);
    }
    else
    {
        rarch_system_info_t *system = &runloop_state_get_ptr()->system;
        const char *error_label      = msg_hash_to_str(MSG_ERROR);
        const char *core_name        = (system && !string_is_empty(system->info.library_name))
              ? system->info.library_name
              : msg_hash_to_str(MENU_ENUM_LABEL_VALUE_NOT_AVAILABLE);
        char msg[128];
        snprintf(msg, sizeof(msg), "%s: %s (N/D)", error_label, core_name);
        miyoo_menu_notify(msg);
    }
    return ret;
}

bool miyoo_menu_cpu_saved_clock_get_core(long *clock_hz)
{
    char path[PATH_MAX_LENGTH];

    if (!clock_hz)
        return false;
    if (!miyoo_cpu_clock_build_core_path(path, sizeof(path), "cpuclock"))
        return false;

    return miyoo_cpu_clock_read_from_file(path, clock_hz);
}

bool miyoo_menu_cpu_saved_clock_get_rom(long *clock_hz)
{
    char path[PATH_MAX_LENGTH];
    char rom_cpu_file[PATH_MAX_LENGTH];
    const char *rarch_path_basename = path_get(RARCH_PATH_BASENAME);
    const char *rom_name             = path_basename_nocompression(rarch_path_basename);

    if (!clock_hz || string_is_empty(rom_name))
        return false;

    snprintf(rom_cpu_file, sizeof(rom_cpu_file), "%s-cpu", rom_name);

    if (!miyoo_cpu_clock_build_core_path(path, sizeof(path), rom_cpu_file))
        return false;

    return miyoo_cpu_clock_read_from_file(path, clock_hz);
}

int miyoo_menu_action_save_cpu_rom(void)
{
    char rom_cpu_file[PATH_MAX_LENGTH];
    const char *rarch_path_basename = path_get(RARCH_PATH_BASENAME);
    const char *rom_name             = path_basename_nocompression(rarch_path_basename);

    if (string_is_empty(rom_name))
        return -1;

    snprintf(rom_cpu_file, sizeof(rom_cpu_file), "%s-cpu", rom_name);
    {
        int ret = miyoo_cpu_clock_save_to_file(rom_cpu_file);
        if (ret == 0)
        {
            long clock_hz  = miyoo_menu_cpu_clock_get_hz();
            long clock_mhz = (clock_hz > 0) ? (clock_hz / 1000L) : 0;
            const char *saved_label = msg_hash_to_str(MSG_SAVED_SUCCESSFULLY_TO);
            char msg[256];

            if (clock_mhz > 0)
                snprintf(msg, sizeof(msg), "%s %s (%ld MHz)", saved_label, rom_name, clock_mhz);
            else
                snprintf(msg, sizeof(msg), "%s %s", saved_label, rom_name);
            miyoo_menu_notify(msg);
        }
        else
        {
            const char *error_label = msg_hash_to_str(MSG_ERROR);
            char msg[128];
            snprintf(msg, sizeof(msg), "%s: %s (N/D)", error_label, rom_name);
            miyoo_menu_notify(msg);
        }
        return ret;
    }
}

int miyoo_menu_action_netplay_host(void)
{
#ifdef HAVE_NETWORKING
    settings_t *settings = config_get_ptr();

    if (!settings)
       return -1;

    if (!miyoo_netplay_saved_cheevos_valid)
    {
       miyoo_netplay_saved_cheevos_enable = settings->bools.cheevos_enable;
       miyoo_netplay_saved_cheevos_valid  = true;
       miyoo_netplay_cheevos_suspended    = false;
    }

    settings->bools.cheevos_enable = false;
    miyoo_netplay_update_cheevos_cfg(false);
    RARCH_LOG("[MIYOO][Netplay] Forcing cheevos_enable=false before netplay start.\n");
#ifdef HAVE_CHEEVOS
    rcheevos_unload();
    rcheevos_hardcore_enabled_changed();
#endif

    if (command_event(CMD_EVENT_NETPLAY_ENABLE_HOST, NULL))
    {
       miyoo_netplay_cheevos_suspended = true;
       return 0;
    }

    if (miyoo_netplay_saved_cheevos_valid)
    {
       settings->bools.cheevos_enable = miyoo_netplay_saved_cheevos_enable;
       miyoo_netplay_update_cheevos_cfg(miyoo_netplay_saved_cheevos_enable);
       RARCH_LOG("[MIYOO][Netplay] Netplay start failed, restoring cheevos_enable=%s.\n",
             miyoo_netplay_saved_cheevos_enable ? "true" : "false");
#ifdef HAVE_CHEEVOS
       rcheevos_hardcore_enabled_changed();
#endif
       miyoo_netplay_saved_cheevos_valid = false;
       miyoo_netplay_cheevos_suspended   = false;
    }
    return -1;
#else
    return -1;
#endif
}

int miyoo_menu_action_netplay_client(void)
{
#ifdef HAVE_NETWORKING
    settings_t *settings = config_get_ptr();

    if (!settings)
       return -1;

    if (!miyoo_netplay_saved_cheevos_valid)
    {
       miyoo_netplay_saved_cheevos_enable = settings->bools.cheevos_enable;
       miyoo_netplay_saved_cheevos_valid  = true;
       miyoo_netplay_cheevos_suspended    = false;
    }

    settings->bools.cheevos_enable = false;
    miyoo_netplay_update_cheevos_cfg(false);
    RARCH_LOG("[MIYOO][Netplay] Forcing cheevos_enable=false before netplay client flow.\n");
#ifdef HAVE_CHEEVOS
    rcheevos_unload();
    rcheevos_hardcore_enabled_changed();
#endif

    miyoo_netplay_cheevos_suspended = true;
    return 0;
#else
    return -1;
#endif
}

void miyoo_menu_netplay_menu_open(void)
{
    miyoo_netplay_menu_open = true;
}

void miyoo_menu_netplay_menu_close(void)
{
    miyoo_netplay_menu_open = false;
}

bool miyoo_menu_netplay_menu_is_open(void)
{
    return miyoo_netplay_menu_open;
}

bool miyoo_menu_netplay_cheevos_suspended(void)
{
    return miyoo_netplay_cheevos_suspended;
}

void miyoo_menu_netplay_on_stopped(void)
{
    settings_t *settings = config_get_ptr();

    if (    !settings
         || !miyoo_netplay_saved_cheevos_valid
         || !miyoo_netplay_cheevos_suspended)
       return;

    settings->bools.cheevos_enable = miyoo_netplay_saved_cheevos_enable;
    miyoo_netplay_update_cheevos_cfg(miyoo_netplay_saved_cheevos_enable);
    RARCH_LOG("[MIYOO][Netplay] Netplay stopped, restoring cheevos_enable=%s.\n",
          miyoo_netplay_saved_cheevos_enable ? "true" : "false");
#ifdef HAVE_CHEEVOS
    rcheevos_hardcore_enabled_changed();
#endif
    miyoo_netplay_saved_cheevos_valid = false;
    miyoo_netplay_cheevos_suspended   = false;
}

void miyoo_menu_achievements_menu_open(size_t parent_index)
{
    miyoo_achievements_menu_open = true;
    miyoo_achievements_parent    = parent_index;
}

void miyoo_menu_achievements_menu_close(void)
{
    miyoo_achievements_menu_open = false;
}

bool miyoo_menu_achievements_menu_is_open(void)
{
    return miyoo_achievements_menu_open;
}

size_t miyoo_menu_achievements_parent_index(void)
{
    return miyoo_achievements_parent;
}

int miyoo_menu_action_open_quick_menu(void)
{
    miyoo_menu_open_native_quickmenu();
    return 0;
}

int miyoo_menu_action_quit_retroarch(void)
{
    miyoo_menu_netplay_on_stopped();
    return command_event(CMD_EVENT_QUIT, NULL) ? 0 : -1;
}

void miyoo_menu_state_menu_open_save(void)
{
    miyoo_state_menu_mode = 1;
}

void miyoo_menu_state_menu_open_load(void)
{
    miyoo_state_menu_mode = 2;
}

void miyoo_menu_state_menu_close(void)
{
    miyoo_state_menu_mode = 0;
}

int miyoo_menu_state_menu_get_mode(void)
{
    return miyoo_state_menu_mode;
}

int miyoo_menu_state_parent_index(int mode)
{
    if (mode == 1)
        return 1;
    if (mode == 2)
        return 2;
    return 0;
}

int miyoo_menu_action_state_slot(int slot)
{
    settings_t *settings = config_get_ptr();
    int mode             = miyoo_state_menu_mode;
    bool old_thumbnail   = false;
    bool old_auto_index  = false;
    bool command_ok      = false;
    char shot_name[16]   = {0};
    char state_path[PATH_MAX_LENGTH];

    if (!settings || slot < 0 || slot >= MIYOO_STATE_SLOT_COUNT)
        return -1;

    configuration_set_int(settings, settings->ints.state_slot, slot);
    old_auto_index = settings->bools.savestate_auto_index;
    settings->bools.savestate_auto_index = false;

    if (mode == 1)
    {
        if (runloop_get_savestate_path(state_path, sizeof(state_path), slot))
        {
            if (path_is_valid(state_path) && !path_is_directory(state_path))
                filestream_delete(state_path);

            {
                size_t _len = strlen(state_path);
                strlcpy(state_path + _len, FILE_PATH_PNG_EXTENSION,
                      sizeof(state_path) - _len);

                if (path_is_valid(state_path) && !path_is_directory(state_path))
                    filestream_delete(state_path);
            }
        }

        old_thumbnail = settings->bools.savestate_thumbnail_enable;
        settings->bools.savestate_thumbnail_enable = true;
        command_ok = command_event(CMD_EVENT_SAVE_STATE, NULL);
        settings->bools.savestate_thumbnail_enable = old_thumbnail;
    }
    else if (mode == 2)
        command_ok = command_event(CMD_EVENT_LOAD_STATE, NULL);
    else
    {
        settings->bools.savestate_auto_index = old_auto_index;
        return -1;
    }

    settings->bools.savestate_auto_index = old_auto_index;
    configuration_set_int(settings, settings->ints.state_slot, slot);

    if (command_ok && dispwidget_get_ptr()->active && runloop_get_savestate_path(state_path, sizeof(state_path), slot))
    {
        size_t _len = strlen(state_path);
        strlcpy(state_path + _len, FILE_PATH_PNG_EXTENSION, sizeof(state_path) - _len);
        snprintf(shot_name, sizeof(shot_name), "%d", slot + 1);
        gfx_widget_state_slot_show(dispwidget_get_ptr(), shot_name, state_path);
    }

    miyoo_state_menu_mode = 0;
    return command_event(CMD_EVENT_MENU_TOGGLE, NULL) ? 0 : -1;
}

void miyoo_menu_update_savestate_thumbnail(unsigned selection)
{
    settings_t *settings        = config_get_ptr();
    dispgfx_widget_t *widget_st = dispwidget_get_ptr();
    struct menu_state *menu_st  = menu_state_get_ptr();
    char shot_name[16]          = {0};
    char state_path[PATH_MAX_LENGTH];
    int slot                    = settings ? settings->ints.state_slot : -1;

    (void)selection;

    if (!widget_st || !widget_st->active)
        return;

    if (slot < 0 || slot > 999)
    {
        gfx_widget_state_slot_show(widget_st, NULL, NULL);
        return;
    }

    if (!runloop_get_savestate_path(state_path, sizeof(state_path), slot))
    {
        gfx_widget_state_slot_show(widget_st, NULL, NULL);
        return;
    }

    {
        size_t _len = strlen(state_path);
        strlcpy(state_path + _len, FILE_PATH_PNG_EXTENSION, sizeof(state_path) - _len);
    }

    if (!path_is_valid(state_path))
    {
        gfx_widget_state_slot_show(widget_st, NULL, NULL);
        return;
    }

    snprintf(shot_name, sizeof(shot_name), "%d", slot + 1);
    gfx_widget_state_slot_show(widget_st, shot_name, state_path);

    if (!menu_st || !menu_st->driver_ctx)
        return;

    if (menu_st->driver_ctx->update_savestate_thumbnail_path)
        menu_st->driver_ctx->update_savestate_thumbnail_path(
              menu_st->userdata, selection);
    if (menu_st->driver_ctx->update_savestate_thumbnail_image)
        menu_st->driver_ctx->update_savestate_thumbnail_image(menu_st->userdata);
}

void miyoo_menu_state_slot_label(int slot, char *out, size_t len)
{
    char state_path[PATH_MAX_LENGTH];
    char thumb_path[PATH_MAX_LENGTH * 2];
    char date_buf[64] = {0};
    struct stat st_state;
    struct stat st_thumb;
    bool has_thumb = false;

    if (!out || len == 0)
        return;

    out[0] = '\0';
    if (slot < 0 || slot >= MIYOO_STATE_SLOT_COUNT
          || !runloop_get_savestate_path(state_path, sizeof(state_path), slot))
    {
        snprintf(out, len, "Slot %d - NO DATA", slot);
        return;
    }

    if (stat(state_path, &st_state) != 0)
    {
        snprintf(out, len, "Slot %d - NO DATA", slot);
        return;
    }

    strlcpy(thumb_path, state_path, sizeof(thumb_path));
    strlcpy(thumb_path + strlen(thumb_path), FILE_PATH_PNG_EXTENSION,
          sizeof(thumb_path) - strlen(thumb_path));
    has_thumb = (stat(thumb_path, &st_thumb) == 0);

    {
       struct tm tm_buf;
       localtime_r(&st_state.st_mtime, &tm_buf);
       strftime(date_buf, sizeof(date_buf), "%Y-%m-%d %H:%M", &tm_buf);
    }

    (void)has_thumb;
    snprintf(out, len, "Slot %d - %s", slot, date_buf);
}

void miyoo_menu_state_slot_metadata(int slot, char *out, size_t len)
{
    char full_label[64];

    if (!out || len == 0)
        return;

    miyoo_menu_state_slot_label(slot, full_label, sizeof(full_label));

    out[0] = '\0';
    if (string_starts_with_size(full_label, "Slot ", STRLEN_CONST("Slot ")))
    {
        char *sep = strstr(full_label, " - ");
        if (sep)
        {
            strlcpy(out, sep + 3, len);
            return;
        }
    }

    strlcpy(out, full_label, len);
}
#endif

/**
 * @brief Toggle scaling options.
 * 
 * The 4 states are:
 * 1. Integer scaling: OFF (Original)
 * 2. Integer scaling: OFF (4:3)
 * 3. Integer scaling: ON (Original)
 * 4. Integer scaling: ON (4:3)
 * 
 * @param settings 
 */
void miyoo_event_fullscreen_impl(settings_t *settings)
{
    settings->bools.video_dingux_ipu_keep_aspect = !settings->bools.video_dingux_ipu_keep_aspect;
    if (settings->bools.video_dingux_ipu_keep_aspect) {
        settings->bools.video_scale_integer = !settings->bools.video_scale_integer;
    }

    video_driver_apply_state_changes();

    show_miyoo_fullscreen_notification(settings);
    write_core_override_aspect_scale(settings);
}

#endif
