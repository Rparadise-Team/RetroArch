#pragma once

#if defined(MIYOOMINI)

#include "configuration.h"

void miyoo_event_fullscreen_impl(settings_t *settings);

#if defined(MIYOO_CUSTOM_MENU)
/* Entries of the Miyoo menu root, in display order. Some are shown only
 * in some builds or settings, so their index comes from
 * miyoo_menu_root_index(), never from a constant. */
enum miyoo_menu_root_entry
{
   MIYOO_ROOT_RESUME = 0,
   MIYOO_ROOT_RESTART,
   MIYOO_ROOT_SAVE_STATE,
   MIYOO_ROOT_LOAD_STATE,
   MIYOO_ROOT_SYNC_NOW,
   MIYOO_ROOT_CPU_CLOCK,
   MIYOO_ROOT_SAVE_CPU_CORE,
   MIYOO_ROOT_SAVE_CPU_ROM,
   MIYOO_ROOT_ACHIEVEMENTS,
   MIYOO_ROOT_NETPLAY_HOST,
   MIYOO_ROOT_NETPLAY_CLIENT,
   MIYOO_ROOT_CORE_OPTIONS,
   MIYOO_ROOT_JOYPAD_SENSE,
   MIYOO_ROOT_RETROARCH_SETTINGS,
   MIYOO_ROOT_QUIT
};

bool miyoo_menu_root_entry_shown(enum miyoo_menu_root_entry entry);
size_t miyoo_menu_root_index(enum miyoo_menu_root_entry entry);
void miyoo_menu_context_begin(void);
void miyoo_menu_context_end(void);
bool miyoo_menu_context_active(void);
bool miyoo_menu_context_is_native_quickmenu(void);
void miyoo_menu_open_native_quickmenu(void);
void miyoo_menu_resume_from_native_quickmenu(void);
int miyoo_menu_action_resume(void);
int miyoo_menu_action_save_state(void);
int miyoo_menu_action_load_state(void);
int miyoo_menu_action_sync_now(void);
int miyoo_menu_action_cpu_adjust(int delta_mhz);
int miyoo_menu_action_set_cpu_clock(int mhz);
long miyoo_menu_cpu_clock_get_hz(void);
bool miyoo_menu_cpu_clock_get_runtime_override(long *clock_hz);
void miyoo_menu_cpu_menu_open(void);
void miyoo_menu_cpu_menu_close(void);
bool miyoo_menu_cpu_menu_is_open(void);
int miyoo_menu_cpu_menu_get_index(void);
int miyoo_menu_action_save_cpu_core(void);
int miyoo_menu_action_save_cpu_rom(void);
bool miyoo_menu_cpu_saved_clock_get_core(long *clock_hz);
bool miyoo_menu_cpu_saved_clock_get_rom(long *clock_hz);
int miyoo_menu_action_netplay_host(void);
int miyoo_menu_action_netplay_client(void);
bool miyoo_menu_netplay_cheevos_suspended(void);
void miyoo_menu_netplay_on_stopped(void);
void miyoo_menu_netplay_recover_cheevos(void);
void miyoo_menu_netplay_menu_open(void);
void miyoo_menu_netplay_menu_close(void);
bool miyoo_menu_netplay_menu_is_open(void);
void miyoo_menu_core_options_menu_open(void);
void miyoo_menu_core_options_menu_close(void);
bool miyoo_menu_core_options_menu_is_open(void);
void miyoo_menu_achievements_menu_open(size_t parent_index);
void miyoo_menu_achievements_menu_close(void);
bool miyoo_menu_achievements_menu_is_open(void);
size_t miyoo_menu_achievements_parent_index(void);
int miyoo_menu_action_open_quick_menu(void);
int miyoo_menu_action_quit_retroarch(void);
void miyoo_menu_state_menu_open_save(void);
void miyoo_menu_state_menu_open_load(void);
void miyoo_menu_state_menu_close(void);
int miyoo_menu_state_menu_get_mode(void);
int miyoo_menu_state_parent_index(int mode);
int miyoo_menu_action_state_slot(int slot);
void miyoo_menu_state_slot_label(int slot, char *out, size_t len);
void miyoo_menu_state_slot_metadata(int slot, char *out, size_t len);
void miyoo_menu_update_savestate_thumbnail(unsigned selection);
int miyoo_menu_joypad_sense_get(void);
int miyoo_menu_joypad_sense_set(int sense);
#endif

#endif
