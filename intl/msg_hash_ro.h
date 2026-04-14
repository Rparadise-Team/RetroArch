#if defined(_MSC_VER) && !defined(_XBOX) && (_MSC_VER >= 1500 && _MSC_VER < 1900)
#if (_MSC_VER >= 1700)
/* https://support.microsoft.com/en-us/kb/980263 */
#pragma execution_character_set("utf-8")
#endif
#pragma warning(disable:4566)
#endif

/* Top-Level Menu */


/* Main Menu */

#ifdef HAVE_LAKKA
#endif

/* Main Menu > Load Core */


/* Main Menu > Load Content */


/* Main Menu > Load Content > Playlists */


/* Main Menu > Online Updater */


/* Main Menu > Information */


/* Main Menu > Information > Core Information */


/* Main Menu > Information > System Information */


/* Main Menu > Information > Database Manager */


/* Main Menu > Information > Database Manager > Information */


/* Main Menu > Configuration File */


/* Main Menu > Help */


/* Main Menu > Help > Basic Menu Controls */


/* Settings */


/* Core option category placeholders for icons */

#ifdef HAVE_MIST
#endif

/* Settings > Drivers */


#ifdef HAVE_MICROPHONE
#endif

/* Settings > Video */

#if defined(DINGUX)
#if defined(RS90) || defined(MIYOO)
#endif
#endif

/* Settings > Video > CRT SwitchRes */


/* Settings > Video > Output */

#if defined (WIIU)
#endif
#if defined(DINGUX) && defined(DINGUX_BETA)
#endif

/* Settings > Video > Fullscreen Mode */


/* Settings > Video > Windowed Mode */


/* Settings > Video > Scaling */

#if defined(DINGUX)
#endif
#if defined(RARCH_MOBILE)
#endif

/* Settings > Video > HDR */


/* Settings > Video > Synchronization */


/* Settings > Audio */

#ifdef HAVE_MICROPHONE
#endif

/* Settings > Audio > Output */


#ifdef HAVE_MICROPHONE
/* Settings > Audio > Input */
#endif

/* Settings > Audio > Resampler */


/* Settings > Audio > Synchronization */


/* Settings > Audio > MIDI */


/* Settings > Audio > Mixer Settings > Mixer Stream */


/* Settings > Audio > Menu Sounds */


/* Settings > Input */

#if defined(HAVE_DINPUT) || defined(HAVE_WINRAWINPUT)
#endif
#ifdef ANDROID
#endif


/* Settings > Input > Haptic Feedback/Vibration */


/* Settings > Input > Menu Controls */


/* Settings > Input > Hotkeys */











/* Settings > Input > Port # Controls */


/* Settings > Latency */

#if !(defined(HAVE_DYNAMIC) || defined(HAVE_DYLIB))
#endif

/* Settings > Core */

#ifndef HAVE_DYNAMIC
#endif
#ifdef HAVE_MIST







#endif
/* Settings > Configuration */


/* Settings > Saving */


/* Settings > Logging */


/* Settings > File Browser */


/* Settings > Frame Throttle */


/* Settings > Frame Throttle > Rewind */


/* Settings > Frame Throttle > Frame Time Counter */


/* Settings > Recording */


/* Settings > On-Screen Display */


/* Settings > On-Screen Display > On-Screen Overlay */


#if defined(ANDROID)
#endif

/* Settings > On-Screen Display > On-Screen Overlay > Keyboard Overlay */


/* Settings > On-Screen Display > On-Screen Overlay > Overlay Lightgun */


/* Settings > On-Screen Display > On-Screen Overlay > Overlay Mouse */


/* Settings > On-Screen Display > Video Layout */


/* Settings > On-Screen Display > On-Screen Notifications */


/* Settings > User Interface */

#ifdef _3DS
#endif

/* Settings > User Interface > Menu Item Visibility */

#ifdef HAVE_LAKKA
#endif

/* Settings > User Interface > Menu Item Visibility > Quick Menu */


/* Settings > User Interface > Views > Settings */



/* Settings > User Interface > Appearance */


/* Settings > AI Service */


/* Settings > Accessibility */


/* Settings > Power Management */

/* Settings > Achievements */


/* Settings > Achievements > Appearance */


/* Settings > Achievements > Visibility */


/* Settings > Network */


/* Settings > Network > Updater */


/* Settings > Playlists */


/* Settings > Playlists > Playlist Management */


/* Settings > User */


/* Settings > User > Privacy */


/* Settings > User > Accounts */


/* Settings > User > Accounts > RetroAchievements */


/* Settings > User > Accounts > YouTube */


/* Settings > User > Accounts > Twitch */


/* Settings > User > Accounts > Facebook Gaming */


/* Settings > Directory */


#ifdef HAVE_MIST
/* Settings > Steam */



#endif

/* Music */

/* Music > Quick Menu */


/* Netplay */


/* Netplay > Host */


/* Import Content */


/* Import Content > Scan File */


/* Import Content > Manual Scan */


/* Explore tab */

/* Playlist > Playlist Item */


/* Playlist Item > Set Core Association */


/* Playlist Item > Information */


/* Quick Menu */


/* Quick Menu > Options */


/* Quick Menu > Options > Manage Core Options */


/* - Legacy (unused) */

/* Quick Menu > Controls */


/* Quick Menu > Controls > Manage Remap Files */


/* Quick Menu > Controls > Manage Remap Files > Load Remap File */


/* Quick Menu > Cheats */


/* Quick Menu > Cheats > Start or Continue Cheat Search */


/* Quick Menu > Cheats > Load Cheat File (Replace) */


/* Quick Menu > Cheats > Load Cheat File (Append) */


/* Quick Menu > Cheats > Cheat Details */


/* Quick Menu > Disc Control */


/* Quick Menu > Shaders */



/* Quick Menu > Shaders > Shader Parameters */


/* Quick Menu > Overrides */


/* Quick Menu > Achievements */


/* Quick Menu > Information */


/* Miscellaneous UI Items */


/* Settings Options */


/* RGUI: Settings > User Interface > Appearance */


/* RGUI: Settings Options */


/* XMB: Settings > User Interface > Appearance */


/* XMB: Settings Options */


/* Ozone: Settings > User Interface > Appearance */




/* MaterialUI: Settings > User Interface > Appearance */


/* MaterialUI: Settings Options */


/* Qt (Desktop Menu) */


/* Unsorted */


/* Unused (Only Exist in Translation Files) */


/* Unused (Needs Confirmation) */


/* Discord Status */


/* Notifications */



/* Lakka */


/* Environment Specific Settings */

#ifdef HAVE_LIBNX
#endif
#ifdef HAVE_LAKKA
#ifdef HAVE_LAKKA_SWITCH
#endif
#endif
#ifdef HAVE_LAKKA_SWITCH
#endif
#ifdef GEKKO
#endif
#ifdef UDEV_TOUCH_SUPPORT
#endif
#ifdef HAVE_ODROIDGO2
#else
#endif
#ifdef _3DS
#endif
#ifdef HAVE_QT
#endif
#ifdef HAVE_GAME_AI





#endif


/* Miyoo Custom Menu */

#if defined(MIYOO_CUSTOM_MENU)
MSG_HASH(
   MENU_ENUM_LABEL_VALUE_MIYOO_MENU,
   "Meniu Miyoo"
   )
MSG_HASH(
   MENU_ENUM_LABEL_VALUE_MIYOO_RESUME,
   "Continuă"
   )
MSG_HASH(
   MENU_ENUM_LABEL_VALUE_MIYOO_SAVE_STATE,
   "Salvare rapidă"
   )
MSG_HASH(
   MENU_ENUM_LABEL_VALUE_MIYOO_LOAD_STATE,
   "Încărcare rapidă"
   )
MSG_HASH(
   MENU_ENUM_LABEL_VALUE_MIYOO_SYNC_NOW,
   "Sincronizează acum"
   )
MSG_HASH(
   MENU_ENUM_LABEL_VALUE_MIYOO_CPU_CLOCK,
   "Setează viteza ceasului CPU Miyoo"
   )
MSG_HASH(
   MENU_ENUM_LABEL_VALUE_MIYOO_SAVE_CPU_CLOCK_CORE,
   "Salvează ceasul CPU (nucleu)"
   )
MSG_HASH(
   MENU_ENUM_LABEL_VALUE_MIYOO_SAVE_CPU_CLOCK_ROM,
   "Salvează ceasul CPU (ROM)"
   )
MSG_HASH(
   MENU_ENUM_LABEL_VALUE_MIYOO_ACHIEVEMENTS,
   "Realizări"
   )
MSG_HASH(
   MENU_ENUM_LABEL_VALUE_MIYOO_NETPLAY_HOST,
   "Pornește Netplay (server)"
   )
MSG_HASH(
   MENU_ENUM_LABEL_VALUE_MIYOO_NETPLAY_CLIENT,
   "Gazdă/LAN Netplay (client)"
   )
MSG_HASH(
   MENU_ENUM_LABEL_VALUE_MIYOO_RETROARCH_SETTINGS,
   "Setări"
   )
MSG_HASH(
   MENU_ENUM_LABEL_VALUE_MIYOO_QUIT_RETROARCH,
   "Închide RetroArch"
   )
MSG_HASH(
   MENU_ENUM_LABEL_VALUE_MIYOO_MENU_RETURN,
   "Meniu Miyoo"
   )

MSG_HASH(
   MENU_ENUM_SUBLABEL_MIYOO_RESUME,
   "închideți meniul, reveniți la joc"
   )
MSG_HASH(
   MENU_ENUM_SUBLABEL_MIYOO_SAVE_STATE,
   "Creați o captură de ecran salvată a romului."
   )
MSG_HASH(
   MENU_ENUM_SUBLABEL_MIYOO_LOAD_STATE,
   "Încărcați salvarea capturii de ecran a romului."
   )
MSG_HASH(
   MENU_ENUM_SUBLABEL_MIYOO_SYNC_NOW,
   "Sincronizați fișierele de salvare în cloud."
   )
MSG_HASH(
   MENU_ENUM_SUBLABEL_MIYOO_CPU_CLOCK,
   "Control în timp real al puterii procesorului pentru Miyoo, de la 200Mhz la 1600Mhz."
   )
MSG_HASH(
   MENU_ENUM_SUBLABEL_MIYOO_SAVE_CPU_CLOCK_CORE,
   "salvează puterea procesorului selectată pentru nucleul curent."
   )
MSG_HASH(
   MENU_ENUM_SUBLABEL_MIYOO_SAVE_CPU_CLOCK_ROM,
   "salvează puterea procesorului selectată pentru rom-ul curent, aceasta are prioritate față de viteza de bază dacă a fost definită anterior."
   )
MSG_HASH(
   MENU_ENUM_SUBLABEL_MIYOO_ACHIEVEMENTS,
   "Afișează realizările cu previzualizare a insignelor în acest meniu."
   )
MSG_HASH(
   MENU_ENUM_SUBLABEL_MIYOO_NETPLAY_HOST,
   "Rețeaua Boots funcționează ca player 1 pe nucleele acceptate."
   )
MSG_HASH(
   MENU_ENUM_SUBLABEL_MIYOO_NETPLAY_CLIENT,
   "Meniul de configurare Net-play și lista camerelor disponibile."
   )
MSG_HASH(
   MENU_ENUM_SUBLABEL_MIYOO_RETROARCH_SETTINGS,
   "Reveniți la meniul rapid retroarch generic și dezactivați Meniul personalizat Miyoo, pentru setări mai aprofundate."
   )
MSG_HASH(
   MENU_ENUM_SUBLABEL_MIYOO_QUIT_RETROARCH,
   "aproape retroarh."
   )
MSG_HASH(
   MENU_ENUM_SUBLABEL_MIYOO_MENU_RETURN,
   "returnați meniul personalizat Miyoo, dezactivați meniul rapid retroarch generic."
   )
#endif
