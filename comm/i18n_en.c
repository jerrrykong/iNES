/*
 * i18n 内置英文表 —— 英文真源(唯一)
 *
 * 规则:
 *   1. 本表是英文的唯一来源; 改英文必须改这里, 再用 tools/gen_i18n_template 重新导出 lang/en.ini。
 *   2. key 为 ASCII 点号命名(section.name), 按字典序排列(便于二分与 diff)。
 *   3. key 只增不改: 已有 key 的语义不能变, 否则必须启用新 key(旧语言文件缺失会自动回退英文)。
 *   4. 语言文件里不出现的术语(寄存器名、助记符、硬件缩写)由程序直接写字面量, 不属于本表。
 */

#include "i18n.h"


const ines_i18n_entry_t g_ines_i18n_en[] =
{
	/* ---- app ---- */
	{ "app.title",                          "iNES" },
	{ "app.title_net_format",               "%s - %s - %s (%s)" },   /* iNES - ROM - 联网对战 - 状态 */
	{ "app.title_off_format",               "%s - %s" },             /* iNES - 状态 */
	{ "app.title_rom_format",               "%s - %s - %s" },        /* iNES - ROM - 状态 */

	/* ---- debug: 调试视图的纯 UI 元素(术语不翻译) ---- */
	{ "debug.dma_confirm_message",          "This writes 256 bytes from $%02X00 into sprite memory at once, and costs 514 CPU cycles." },
	{ "debug.dma_confirm_title",            "Write to $4014 (OAMDMA)" },
	{ "debug.reg.copy_all",                 "Copy All" },
	{ "debug.reg.copy_value",               "Copy Value" },
	{ "debug.reg.note",                     "Note" },
	{ "debug.reg.tip_binary_only",          "Only 0 / 1 can be entered." },
	{ "debug.reg.tip_bit_readonly",         "This bit is read-only." },
	{ "debug.reg.tip_reg_readonly",         "This register is read-only." },

	/* ---- dialog.lan: 局域网快速对战 ---- */
	{ "dialog.lan.cache_frames_format",     "%d frames" },
	{ "dialog.lan.close",                   "Close" },
	{ "dialog.lan.col_cache",               "Cache" },
	{ "dialog.lan.col_nick",                "Nickname" },
	{ "dialog.lan.col_rom",                 "ROM" },
	{ "dialog.lan.col_ver",                 "Version" },
	{ "dialog.lan.connect_failed",          "Connection failed." },
	{ "dialog.lan.connecting",              "Connecting..." },
	{ "dialog.lan.err_addr_invalid",        "The room address is invalid." },
	{ "dialog.lan.err_generic",             "LAN discovery is not available." },
	{ "dialog.lan.err_open_failed",         "Could not start LAN discovery (UDP port 8892 unavailable)." },
	{ "dialog.lan.err_resume_failed",       "Could not resume publishing." },
	{ "dialog.lan.err_room_gone",           "The room is gone; please retry shortly." },
	{ "dialog.lan.err_rom_diff",            "The ROM is different; cannot join." },
	{ "dialog.lan.err_ver_diff",            "Protocol versions differ; upgrade to the same version before playing." },
	{ "dialog.lan.hint_format",             "Published to the LAN (cache %d frames, fixed after publishing; adjustable via [netplay] cache_num in config.ini). Joining another room makes this machine the second player (client)." },
	{ "dialog.lan.join",                    "Join" },
	{ "dialog.lan.joining_format",          "Joining %s's room..." },
	{ "dialog.lan.nickname",                "Nickname" },
	{ "dialog.lan.no_rooms",                "Published, waiting for other players (no room found: needs the same LAN and no firewall blocking)" },
	{ "dialog.lan.restore_format",          "%s (publishing resumed)" },
	{ "dialog.lan.rom_diff_suffix",         " (ROM differs)" },
	{ "dialog.lan.rom_prefix_format",       "ROM: %s" },
	{ "dialog.lan.rooms_format",            "Published, waiting for other players (%d room(s) found)" },
	{ "dialog.lan.title",                   "LAN Quick Match" },
	{ "dialog.lan.ver_old",                 " (upgrade needed)" },
	{ "dialog.lan.ver_upgrade_format",      "%u (upgrade needed)" },

	/* ---- dialog.netplay: 网络对战 ---- */
	{ "dialog.netplay.address",             "Address" },
	{ "dialog.netplay.cancel",              "Cancel" },
	{ "dialog.netplay.client",              "Client" },
	{ "dialog.netplay.connect_failed",      "Connection failed." },
	{ "dialog.netplay.connecting",          "Connecting to the server..." },
	{ "dialog.netplay.err_port",            "Invalid port: enter a value between 1 and 65535." },
	{ "dialog.netplay.err_port_short",      "Invalid port" },
	{ "dialog.netplay.port",                "Port" },
	{ "dialog.netplay.run_as",              "Run as" },
	{ "dialog.netplay.server",              "Server" },
	{ "dialog.netplay.start",               "Start" },
	{ "dialog.netplay.title",               "Net Play" },
	{ "dialog.netplay.waiting",             "Waiting for the client to connect..." },

	/* ---- dialog.openrom: 载入 NES 文件(文件管理器) ---- */
	{ "dialog.openrom.cancel",              "Cancel" },
	{ "dialog.openrom.col_battery",         "Battery" },
	{ "dialog.openrom.col_chr",             "CHR" },
	{ "dialog.openrom.col_mapper",          "Mapper" },
	{ "dialog.openrom.col_mirror",          "Mirror" },
	{ "dialog.openrom.col_name",            "File Name" },
	{ "dialog.openrom.col_prg",             "PRG" },
	{ "dialog.openrom.col_rom_size",        "ROM Size" },
	{ "dialog.openrom.col_trainer",         "Trainer" },
	{ "dialog.openrom.dir_invalid",         "The folder does not exist or is not accessible!" },
	{ "dialog.openrom.file_missing",        "The file no longer exists; please select another one!" },
	{ "dialog.openrom.folder",              "Folder:" },
	{ "dialog.openrom.load",                "Load" },
	{ "dialog.openrom.mirror_four",         "Four-screen" },
	{ "dialog.openrom.mirror_horizontal",   "Horizontal" },
	{ "dialog.openrom.mirror_unknown",      "Unknown" },
	{ "dialog.openrom.mirror_vertical",     "Vertical" },
	{ "dialog.openrom.no",                  "No" },
	{ "dialog.openrom.no_folder",           "No folder selected" },
	{ "dialog.openrom.no_nes_file",         "No NES files found" },
	{ "dialog.openrom.no_supported",        "No supported NES files found" },
	{ "dialog.openrom.parsing_format",      "Parsing %ld/%ld ..." },
	{ "dialog.openrom.parsing_zero_format", "Parsing 0/%lu ..." },
	{ "dialog.openrom.select_dir_title",    "Select the folder containing NES files" },
	{ "dialog.openrom.title",               "Load NES File" },
	{ "dialog.openrom.total_format",        "%lu supported NES file(s)" },
	{ "dialog.openrom.yes",                 "Yes" },

	/* ---- menu ---- */
	{ "menu.app.about",                     "About iNES" },
	{ "menu.app.hide",                      "Hide iNES" },
	{ "menu.app.quit",                      "Quit iNES" },
	{ "menu.control",                       "Control" },
	{ "menu.control.aspect",                "Aspect Ratio" },
	{ "menu.control.aspect_original",       "Original" },
	{ "menu.control.frame_step",            "Frame Step" },
	{ "menu.control.full_screen",           "Full Screen" },
	{ "menu.control.hard_reset",            "Hard Reset" },
	{ "menu.control.load_state",            "Load State" },
	{ "menu.control.mute",                  "Mute" },
	{ "menu.control.pause",                 "Pause" },
	{ "menu.control.save_state",            "Save State" },
	{ "menu.control.snapshot",              "Screenshot" },
	{ "menu.control.soft_reset",            "Soft Reset" },
	{ "menu.control.volume",                "Volume" },
	{ "menu.control.zoom",                  "Zoom" },
	{ "menu.file",                          "File" },
	{ "menu.file.close",                    "Unload ROM" },
	{ "menu.file.exit",                     "Exit" },
	{ "menu.file.lan_match",                "LAN Quick Match..." },
	{ "menu.file.net_play",                 "Net Play..." },
	{ "menu.file.open",                     "Load ROM..." },
	{ "menu.file.recent",                   "Recent Files" },
	{ "menu.help",                          "Help" },
	{ "menu.help.about",                    "About iNES" },
	{ "menu.language",                      "Language" },
	{ "menu.recent.none",                   "(None)" },
	{ "menu.state.empty_format",            "%s %d (Empty)" },
	{ "menu.state.load_format",             "Load %d" },
	{ "menu.state.save_format",             "Save %d" },
	{ "menu.tools",                         "Tools" },
	{ "menu.tools.cpu_trace",               "CPU TRACE" },
	{ "menu.tools.debug_view",              "Debug Views" },
	{ "menu.tools.log",                     "Log" },
	{ "menu.tools.options",                 "Options..." },
	{ "menu.tools.osd",                     "Show OSD" },
	{ "menu.volume.percent_format",         "Volume: %d%%" },
	{ "menu.zoom.x_format",                 "x %d" },

	/* ---- msg: 通用提示 / 对话框标题 / 状态提示 ---- */
	{ "msg.about",                          "iNES, version 1.0\nCopyright (C) 2015" },
	{ "msg.about_title",                    "About iNES" },
	{ "msg.cancel",                         "Cancel" },
	{ "msg.control_title",                  "Control" },
	{ "msg.load_rom_failed_format",         "Could not load the file:\n%s\n\nMake sure it is a valid NES ROM and the mapper is supported." },
	{ "msg.load_rom_failed_title",          "Failed to Load ROM" },
	{ "msg.load_rom_first",                 "Please load a ROM first." },
	{ "msg.netplay_already",                "Already in a net play game." },
	{ "msg.netplay_host_reset_only",        "Only the host can reset during net play." },
	{ "msg.netplay_quit_confirm",           "End Game" },
	{ "msg.netplay_quit_message",           "A net play game is running. End it now?" },
	{ "msg.netplay_quit_title",             "Net Play" },
	{ "msg.ok",                             "OK" },
	{ "msg.option_title",                   "Options" },
	{ "msg.option_unimplemented",           "This feature is not implemented in the current version." },
	{ "msg.overwrite",                      "Overwrite" },
	{ "msg.overwrite_state_format",         "Save state %d already exists. Overwrite it?" },
	{ "msg.overwrite_state_title",          "Overwrite Save State" },
	{ "msg.peer_left",                      "The peer has left the game; continuing in single-player mode." },
	{ "msg.snapshot_failed_title",          "Screenshot Failed" },
	{ "msg.snapshot_no_image",              "Could not create the image data." },
	{ "msg.snapshot_saved",                 "Screenshot saved to ~/Pictures/iNES" },
	{ "msg.snapshot_write_failed_format",   "Could not write the screenshot file:\n%s" },
	{ "msg.startup_failed_title",           "Startup Failed" },
	{ "msg.startup_no_thread",              "Could not create the simulation thread; see the log for details." },
	{ "msg.state_host_only",                "Only the host can load a save state during net play." },
	{ "msg.state_load_failed",              "Failed to load the save state." },
	{ "msg.state_not_found_format",         "Save state %d does not exist or does not match the current ROM." },
	{ "msg.state_sync_begin_failed",        "Could not start the save state sync." },
	{ "msg.state_sync_done",                "Save state synced; the game continues." },
	{ "msg.state_sync_failed_end",          "Save state sync failed; net play ended." },
	{ "msg.state_sync_reset",               "Failed to load the save state; the game was reset." },
	{ "msg.state_syncing",                  "Syncing the save state, please wait..." },
	{ "msg.state_syncing_start",            "Syncing save state..." },
	{ "msg.state_too_large",                "The save state is too large to sync." },

	/* ---- status: 主窗口标题里的运行状态 ---- */
	{ "status.frame_step",                  "Frame step" },
	{ "status.net_play_tag",                "Net Play" },
	{ "status.not_running",                 "Not running" },
	{ "status.paused",                      "Paused" },
	{ "status.running",                     "Running" },

	/* ---- view: 调试窗口标题 ---- */
	{ "view.default",                       "Debug Window" },
	{ "view.memory",                        "Memory Viewer" },
	{ "view.name_table",                    "Name Table Viewer" },
	{ "view.palette",                       "Palette Viewer" },
	{ "view.pattern_table",                 "Pattern Table Viewer" },
	{ "view.pattern_title_format",          "%s(%s%d)" },   /* 参数: 窗口标题 / BG|SP / 索引 */
	{ "view.register",                      "Register Viewer" },
	{ "view.spmemory",                      "Sprite Memory Viewer" },
	{ "view.vmemory",                       "Pattern Memory Viewer" },

	/* ---- video ---- */
	{ "video.drag_tip",                     "Drag a .nes file here, or use File > Load ROM... (⌘O)" }
};

const int g_ines_i18n_en_count = (int)(sizeof(g_ines_i18n_en) / sizeof(g_ines_i18n_en[0]));
