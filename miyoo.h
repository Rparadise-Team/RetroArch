#pragma once

#if defined(MIYOOMINI)

#include "configuration.h"

void miyoo_event_fullscreen_impl(settings_t *settings);

#if defined(MIYOO_CUSTOM_MENU)
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
void miyoo_menu_netplay_menu_open(void);
void miyoo_menu_netplay_menu_close(void);
bool miyoo_menu_netplay_menu_is_open(void);
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
#endif

#endif
