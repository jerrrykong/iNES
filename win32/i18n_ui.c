/*
 * i18n_ui.c —— win32 前端的 i18n 接入层实现
 *
 * 与 mac/iNESi18n.m 同构: 只做「探测系统语言 / 读写 config.ini / 转成 TCHAR」三件事,
 * 并设置菜单文本、对话框文本与自适应布局。所有译文都在 lang/*.ini 里, 这里没有一句。
 *
 * 注意 win32 目标编译为 UNICODE: comm/i18n 返回的 ines_cstr_t 已是 UTF-16, 可直接交给
 * SetWindowText / MessageBox; 只有「把 UTF-8 的语言名、语言 ID 转成 harmless TCHAR」才
 * 需要本文件的两个转换函数。
 */

#include "stdafx.h"
#include "i18n_ui.h"

#include "../comm/log.h"
#include "../comm/i18n.h"

#include <stdlib.h>


/* 语言菜单(id / 显示名)与语言 ID 都是 ASCII + UTF-8, 在 UNICODE 日志里要用 %S 打印 */
#ifdef UNICODE
#define  APP_FMT_S   ISTR("%S")
#else
#define  APP_FMT_S   ISTR("%s")
#endif


/* Windows SDK 的 LOCALE_SNAME 需要 Vista 以上; 这里显式定义以免 targetver 过低时缺符号 */
#ifndef LOCALE_SNAME
#define LOCALE_SNAME   0x0000005c
#endif

#define APP_FIT_MAX       48      /* 单个对话框参与自适应的控件上限 */
#define APP_BTN_MIN_W     78      /* 按钮最小宽度(px), 与 mac 端的 >= 72pt 相当 */
#define APP_CTRL_MARGIN   7       /* 对话框左右的默认设计边距 */


/* ------------------------------------------------------------------ */
/* 字符集转换: TCHAR <-> UTF-8 的 char*                                 */
/* ------------------------------------------------------------------ */

/** TCHAR 串 -> UTF-8(char*)。dst_len 为 dst 字节数 */
static void i18n_ui_to_utf8(ines_cstr_t src, char* dst, size_t dst_len)
{
	if((dst == NULL) || (dst_len == 0))
		return;

	dst[0] = '\0';

	if(src == NULL)
		return;

#ifdef UNICODE
	WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, (int)dst_len - 1, NULL, NULL);
#else
	ines_strncpy(dst, src, dst_len - 1);
#endif

	dst[dst_len - 1] = '\0';
}

/** UTF-8(char*) -> TCHAR 串。dst_len 为 dst 的元素个数(不是字节数) */
static void i18n_ui_from_utf8(const char* src, ines_str_t dst, size_t dst_len)
{
	if((dst == NULL) || (dst_len == 0))
		return;

	dst[0] = '\0';

	if(src == NULL)
		return;

#ifdef UNICODE
	MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, (int)dst_len - 1);
#else
	ines_strncpy(dst, src, dst_len - 1);
#endif

	dst[dst_len - 1] = '\0';
}


/* ------------------------------------------------------------------ */
/* 取文本                                                              */
/* ------------------------------------------------------------------ */

ines_cstr_t i18n_ui_text(const char* key)
{
	return ines_i18n_text(key);
}

ines_cstr_t i18n_ui_text_fmt(const char* key, ...)
{
	static ines_char_t  buf[4][1024];       /* 轮换缓冲: 允许 L10NF 内再套 L10NF */
	static int          rotate = 0;
	ines_str_t          out;
	va_list             args;

	rotate = (rotate + 1) % count_of(buf);
	out    = buf[rotate];

	out[0] = '\0';

	va_start(args, key);
	ines_vsnprintf(out, (ines_size_t)count_of(buf[0]), ines_i18n_text(key), args);
	va_end(args);

	out[count_of(buf[0]) - 1] = '\0';

	return out;
}


/* ------------------------------------------------------------------ */
/* 初始化 / 语言菜单                                                    */
/* ------------------------------------------------------------------ */

/* iNES.c 提供的 config.ini 读写(与 mac 端同一套 section / key 名) */
extern ines_cstr_t  GetConfigStr(ines_cstr_t sec, ines_cstr_t key, ines_cstr_t def);
extern void         SetConfigStr(ines_cstr_t sec, ines_cstr_t key, ines_cstr_t val);


/** 取系统 UI 语言的 BCP-47 标签(zh-CN / ja-JP / en-US ...), 失败时回退 "en" */
static void i18n_ui_system_language(char* dst, size_t dst_len)
{
	LCID         lcid;
	ines_char_t  tag[INES_I18N_ID_MAX * 2];

	dst[0] = '\0';
	tag[0] = '\0';

	lcid = MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT);

	if(GetLocaleInfo(lcid, LOCALE_SNAME, tag, (int)count_of(tag)) == 0)
		tag[0] = '\0';

	if(tag[0] == '\0')
		ines_strncpy(tag, ISTR("en"), count_of(tag) - 1);

	i18n_ui_to_utf8(tag, dst, dst_len);
}

void i18n_ui_init(void)
{
	char         tag[INES_I18N_ID_MAX * 2];
	char         saved[INES_I18N_ID_MAX];
	ines_cstr_t  cfg;

	i18n_ui_system_language(tag, count_of(tag));

	if(ines_i18n_init(tag) != 0)
	{
		INES_LOG(LOG_ERR, MOD_SYS, ISTR("i18n: init failed, falling back to English\n"));
		return;
	}

	/* 用户显式选过语言则优先于系统语言; 没选过就保持自动匹配, 不写回 config.ini */
	cfg = GetConfigStr(ISTR("ui"), ISTR("language"), ISTR(""));

	if((cfg != NULL) && (cfg[0] != '\0'))
	{
		i18n_ui_to_utf8(cfg, saved, count_of(saved));

		if(ines_i18n_set_language(saved) != 0)
		{
			INES_LOG(LOG_WAR, MOD_SYS,
					 ISTR("i18n: saved language ") APP_FMT_S ISTR(" unavailable, keep ") APP_FMT_S ISTR("\n"),
					 saved, ines_i18n_language());
		}
	}

	INES_LOG(LOG_INF, MOD_SYS,
			 ISTR("i18n: system language = ") APP_FMT_S ISTR(", using ") APP_FMT_S ISTR("\n"),
			 tag, ines_i18n_language());
}

void i18n_ui_fini(void)
{
	ines_i18n_fini();
}

int i18n_ui_language_index(void)
{
	ines_i18n_lang_t  langs[APP_LANG_ITEM_MAX];
	ines_char_t       cur[INES_I18N_ID_MAX];
	int               count = ines_i18n_enum(NULL, 0);
	int               i;

	if(count <= 0)
		return 0;

	if(count > APP_LANG_ITEM_MAX)
		count = APP_LANG_ITEM_MAX;

	if(ines_i18n_enum(langs, count) != count)
		return 0;

	/* ines_i18n_language() 是 ASCII, 而 langs[].id 是 TCHAR —— 先转再比 */
	i18n_ui_from_utf8(ines_i18n_language(), cur, count_of(cur));

	for(i = 0; i < count; i++)
	{
		if(ines_strcmp(langs[i].id, cur) == 0)
			return i;
	}

	return 0;
}

BOOL i18n_ui_select_language(int index)
{
	ines_i18n_lang_t  langs[APP_LANG_ITEM_MAX];
	char              id[INES_I18N_ID_MAX];
	int               count = ines_i18n_enum(NULL, 0);

	if((index < 0) || (count <= 0) || (index >= count) || (count > APP_LANG_ITEM_MAX))
		return FALSE;

	if(ines_i18n_enum(langs, count) != count)
		return FALSE;

	i18n_ui_to_utf8(langs[index].id, id, count_of(id));

	if(ines_i18n_set_language(id) != 0)
		return FALSE;

	SetConfigStr(ISTR("ui"), ISTR("language"), langs[index].id);

	INES_LOG(LOG_INF, MOD_SYS, ISTR("i18n: language switched to ") APP_FMT_S ISTR("\n"), id);

	return TRUE;
}

BOOL i18n_ui_build_language_menu(HMENU hMainMenu)
{
	HMENU              hTools;
	HMENU              hLang;
	ines_i18n_lang_t   langs[APP_LANG_ITEM_MAX];
	int                count;
	int                i;

	if(hMainMenu == NULL)
		return FALSE;

	/* 位置与 iNES.rc 的排列绑定: 主菜单 -> "工具" -> "语言" */
	hTools = GetSubMenu(hMainMenu, APP_MENU_TOOLS_POS);
	if(hTools == NULL)
		return FALSE;

	hLang = GetSubMenu(hTools, APP_TOOLS_LANG_POS);
	if(hLang == NULL)
		return FALSE;

	while(GetMenuItemCount(hLang) > 0)
		RemoveMenu(hLang, 0, MF_BYPOSITION);

	count = ines_i18n_enum(NULL, 0);
	if(count <= 0)
		return FALSE;

	if(count > APP_LANG_ITEM_MAX)
		count = APP_LANG_ITEM_MAX;

	if(ines_i18n_enum(langs, count) != count)
		return FALSE;

	for(i = 0; i < count; i++)
	{
		MENUITEMINFO  mii;

		ZeroMemory(&mii, sizeof(mii));
		mii.cbSize     = sizeof(mii);
		mii.fMask      = MIIM_ID | MIIM_FTYPE | MIIM_STATE | MIIM_STRING;
		mii.fType      = MFT_STRING | MFT_RADIOCHECK;
		mii.fState     = (i == i18n_ui_language_index()) ? MFS_CHECKED : MFS_UNCHECKED;
		mii.wID        = (UINT)(IDM_LANGUAGE_BASE + i);
		mii.dwTypeData = (ines_str_t)langs[i].name;   /* 各语言以本语言自身的名字显示 */
		mii.cch        = (UINT)_tcslen(langs[i].name);

		InsertMenuItem(hLang, (UINT)i, TRUE, &mii);
	}

	return TRUE;
}

static BOOL CALLBACK i18n_ui_notify_proc(HWND hWnd, LPARAM lParam)
{
	DWORD  pid = 0;

	(void)lParam;

	GetWindowThreadProcessId(hWnd, &pid);

	if(pid == GetCurrentProcessId())
		SendMessage(hWnd, WM_APP_LANGCHANGED, 0, 0);

	return TRUE;
}

void i18n_ui_notify_language_change(void)
{
	EnumWindows(i18n_ui_notify_proc, 0);
}


/* ------------------------------------------------------------------ */
/* 菜单文本                                                            */
/* ------------------------------------------------------------------ */

/*
 * 助记符(&)与快捷键(\tCtrl+O)不进语言文件(§4.2), 由本表按命令 ID 追加:
 *   arg >= 0 时 key 指向 "%d" 格式串(缩放倍数 / 音量百分比 / 存档槽号)。
 * 顺序无关, 但 pop 表(s_menu_pops)的 position 必须与 iNES.rc 的菜单排列一致。
 */
static const APP_MENU_ITEM  s_menu_items[] =
{
	/* ---- 文件 ---- */
	{ IDM_OPEN,             "menu.file.open",             -1,  'O', ISTR("\tCtrl+O") },
	{ IDM_CLOSE,            "menu.file.close",            -1,  'U', ISTR("\tCtrl+U") },
	{ IDM_NET_PLAY,         "menu.file.net_play",         -1,  'N', NULL },
	{ IDM_LAN_MATCH,        "menu.file.lan_match",        -1,  'L', NULL },
	{ IDM_RECENT_FILES,     "menu.file.recent",           -1,  'R', NULL },
	{ IDM_EXIT,             "menu.file.exit",             -1,  'X', ISTR("\tAlt+Q") },

	/* ---- 控制 ---- */
	{ IDM_HARDRESET,        "menu.control.hard_reset",    -1,  'R', ISTR("\tCtrl+F1") },
	{ IDM_SOFTRESET,        "menu.control.soft_reset",    -1,  'S', ISTR("\tF1") },
	{ IDM_PAUSE,            "menu.control.pause",         -1,  'P', ISTR("\tP") },
	{ IDM_FRAME_STEP,       "menu.control.frame_step",    -1,  'T', ISTR("\tSpace") },
	{ IDM_FULL_SCREEN,      "menu.control.full_screen",   -1,  'F', ISTR("\tF12") },
	{ IDM_SCALE_ORIG,       "menu.control.aspect_original", -1, 'O', NULL },
	{ IDM_MUTE,             "menu.control.mute",          -1,  'M', ISTR("\tF9") },
	{ IDM_SNAPSHOT,         "menu.control.snapshot",      -1,  'S', ISTR("\tCtrl+F10") },

	/* 缩放 x1~x4(倍数不入语言文件, 只翻格式串里的 "x") */
	{ IDM_ZOOM_X1,          "menu.zoom.x_format",          1,  0,   ISTR("\tF5") },
	{ IDM_ZOOM_X2,          "menu.zoom.x_format",          2,  0,   ISTR("\tF6") },
	{ IDM_ZOOM_X3,          "menu.zoom.x_format",          3,  0,   ISTR("\tF7") },
	{ IDM_ZOOM_X4,          "menu.zoom.x_format",          4,  0,   ISTR("\tF8") },

	/* 音量 100%~0% */
	{ IDM_VOLUMN_100,       "menu.volume.percent_format", 100, 0,   NULL },
	{ IDM_VOLUMN_80,        "menu.volume.percent_format", 80,  0,   NULL },
	{ IDM_VOLUMN_60,        "menu.volume.percent_format", 60,  0,   NULL },
	{ IDM_VOLUMN_40,        "menu.volume.percent_format", 40,  0,   NULL },
	{ IDM_VOLUMN_20,        "menu.volume.percent_format", 20,  0,   NULL },
	{ IDM_VOLUMN_0,         "menu.volume.percent_format",  0,  0,   NULL },

	/* 即时存档 / 载入存档 0~9 */
	/*
	 * 存档 / 读档 10 个槽位: 这里只放不带序号的组名, 序号与存档时间由 UpdateMenuSaveState() /
	 * UpdateMenuLoadState() 追加(menu.state.empty_format 的参数就是"组名 + 序号")。
	 * 语言文件里的 menu.state.save_format / load_format 留给 mac 端的初始菜单项, win32 不直接用。
	 */
	{ IDM_SAVE_STATE_0,     "menu.control.save_state",     -1, 0,   ISTR("\tCtrl+0") },
	{ IDM_SAVE_STATE_1,     "menu.control.save_state",     -1, 0,   ISTR("\tCtrl+1") },
	{ IDM_SAVE_STATE_2,     "menu.control.save_state",     -1, 0,   ISTR("\tCtrl+2") },
	{ IDM_SAVE_STATE_3,     "menu.control.save_state",     -1, 0,   ISTR("\tCtrl+3") },
	{ IDM_SAVE_STATE_4,     "menu.control.save_state",     -1, 0,   ISTR("\tCtrl+4") },
	{ IDM_SAVE_STATE_5,     "menu.control.save_state",     -1, 0,   ISTR("\tCtrl+5") },
	{ IDM_SAVE_STATE_6,     "menu.control.save_state",     -1, 0,   ISTR("\tCtrl+6") },
	{ IDM_SAVE_STATE_7,     "menu.control.save_state",     -1, 0,   ISTR("\tCtrl+7") },
	{ IDM_SAVE_STATE_8,     "menu.control.save_state",     -1, 0,   ISTR("\tCtrl+8") },
	{ IDM_SAVE_STATE_9,     "menu.control.save_state",     -1, 0,   ISTR("\tCtrl+9") },
	{ IDM_LOAD_STATE_0,     "menu.control.load_state",     -1, 0,   ISTR("\tCtrl+Alt+0") },
	{ IDM_LOAD_STATE_1,     "menu.control.load_state",     -1, 0,   ISTR("\tCtrl+Alt+1") },
	{ IDM_LOAD_STATE_2,     "menu.control.load_state",     -1, 0,   ISTR("\tCtrl+Alt+2") },
	{ IDM_LOAD_STATE_3,     "menu.control.load_state",     -1, 0,   ISTR("\tCtrl+Alt+3") },
	{ IDM_LOAD_STATE_4,     "menu.control.load_state",     -1, 0,   ISTR("\tCtrl+Alt+4") },
	{ IDM_LOAD_STATE_5,     "menu.control.load_state",     -1, 0,   ISTR("\tCtrl+Alt+5") },
	{ IDM_LOAD_STATE_6,     "menu.control.load_state",     -1, 0,   ISTR("\tCtrl+Alt+6") },
	{ IDM_LOAD_STATE_7,     "menu.control.load_state",     -1, 0,   ISTR("\tCtrl+Alt+7") },
	{ IDM_LOAD_STATE_8,     "menu.control.load_state",     -1, 0,   ISTR("\tCtrl+Alt+8") },
	{ IDM_LOAD_STATE_9,     "menu.control.load_state",     -1, 0,   ISTR("\tCtrl+Alt+9") },

	/* ---- 工具 ---- */
	{ IDM_OPTIONS,          "menu.tools.options",         -1,  'O', NULL },
	{ IDM_VIEW_PT,          "view.pattern_table",         -1,  'I', NULL },
	{ IDM_VIEW_NT,          "view.name_table",            -1,  'N', NULL },
	{ IDM_VIEW_PAL,         "view.palette",               -1,  'P', NULL },
	{ IDM_VIEW_MEMORY,      "view.memory",                -1,  'M', NULL },
	{ IDM_VIEW_VMEMORY,     "view.vmemory",               -1,  'V', NULL },
	{ IDM_VIEW_SPMEMORY,    "view.spmemory",              -1,  'S', NULL },
	{ IDM_VIEW_REG,         "view.register",              -1,  'R', NULL },
	{ IDM_CPU_TRACE,        "menu.tools.cpu_trace",       -1,  'C', NULL },

	/* ---- 帮助 ---- */
	{ IDM_ABOUT,            "menu.help.about",            -1,  'A', ISTR("\tAlt+?") },
};


/** 子菜单标题表: 资源里的 POPUP 没有 ID, 只能按位置路径定位(务必与 iNES.rc 的排列一致) */
typedef struct _app_menu_popup_
{
	int         path[4];      /* 各级位置, -1 结尾 */
	const char* key;
	int         mnemonic;
} APP_MENU_POPUP;

#define  APP_PATH1(a)                   { (a), -1, -1, -1 }
#define  APP_PATH2(a, b)                { (a), (b), -1, -1 }

static const APP_MENU_POPUP  s_menu_pops[] =
{
	{ APP_PATH1(0),        "menu.file",                'F' },   /* 主菜单第 0 项            */
	{ APP_PATH1(1),        "menu.control",             'C' },   /* 主菜单第 1 项            */
	{ APP_PATH2(1, 7),     "menu.control.zoom",        'X' },
	{ APP_PATH2(1, 8),     "menu.control.aspect",      'C' },
	{ APP_PATH2(1, 11),    "menu.control.volume",      'V' },
	{ APP_PATH2(1, 13),    "menu.control.save_state",  'V' },
	{ APP_PATH2(1, 14),    "menu.control.load_state",  'L' },
	{ APP_PATH1(2),        "menu.tools",               'T' },
	{ APP_PATH2(2, 1),     "menu.language",            'L' },   /* "工具"里紧跟"选项"之后    */
	{ APP_PATH2(2, 11),    "menu.tools.log",           'L' },
	{ APP_PATH1(3),        "menu.help",                'H' },
};

static const APP_MENU_ITEM* i18n_ui_find_item(UINT id)
{
	int  i;

	for(i = 0; i < (int)count_of(s_menu_items); i++)
	{
		if(s_menu_items[i].id == id)
			return &s_menu_items[i];
	}

	return NULL;
}

void i18n_ui_add_mnemonic(ines_str_t buf, ines_size_t len, int ch)
{
	ines_size_t  cur;
	ines_size_t  i;
	int          up = (int)_totupper((ines_char_t)ch);

	if((buf == NULL) || (len == 0))
		return;

	cur = (ines_size_t)_tcslen(buf);

	for(i = 0; i < cur; i++)
	{
		ines_char_t  c = buf[i];

		if(c < 0 || c > 0x7f)      /* 非 ASCII: 不能往多字节序列里插 & */
			continue;

		if((int)_totupper(c) != up)
			continue;

		break;
	}

	if(i < cur)      /* 找到了: 在该字符前插入 & */
	{
		ines_size_t  n;

		if(cur + 2 >= len)
			return;

		for(n = cur; n > i; n--)
			buf[n] = buf[n - 1];

		buf[i]     = (ines_char_t)'&';
		buf[cur + 1] = '\0';
		return;
	}

	/* 找不到(译文里没有这个字母): 追加 " (&X)", 至少不让助记符丢掉 */
	if(cur + 5 < len)
	{
		ines_char_t  tail[8];

		ines_snprintf(tail, count_of(tail), ISTR(" (&%c)"), (ines_char_t)up);
		_tcsncat(buf, tail, len - cur - 1);
		buf[len - 1] = '\0';
	}
}

ines_cstr_t i18n_ui_menu_accel(UINT id)
{
	const APP_MENU_ITEM*  p = i18n_ui_find_item(id);

	return (p != NULL) ? p->accel : NULL;
}

ines_cstr_t i18n_ui_menu_text(UINT id, ines_str_t buf, ines_size_t len)
{
	const APP_MENU_ITEM*  p = i18n_ui_find_item(id);
	ines_cstr_t           accel;

	if((buf == NULL) || (len == 0))
		return NULL;

	/* 表里没有的项保持资源里的原文: 4:3 / 16:9 / "1 - TRACE" 这类数值与术语 */
	if(p == NULL)
		return NULL;

	if(p->arg >= 0)
	{
		/* key 是 "%d" 格式串(缩放倍数 / 音量百分比 / 存档槽号) */
		ines_snprintf(buf, len, ines_i18n_text(p->key), p->arg);
		buf[len - 1] = '\0';
	}
	else
		ines_strncpy(buf, ines_i18n_text(p->key), len - 1);

	buf[len - 1] = '\0';

	if(p->mnemonic != 0)
		i18n_ui_add_mnemonic(buf, len, p->mnemonic);

	accel = i18n_ui_menu_accel(id);
	if(accel != NULL)
	{
		_tcsncat(buf, accel, len - _tcslen(buf) - 1);
		buf[len - 1] = '\0';
	}

	return buf;
}

static void i18n_ui_set_item_text(HMENU hMenu, UINT pos, ines_cstr_t text)
{
	MENUITEMINFO  mii;

	if(text == NULL)
		return;

	ZeroMemory(&mii, sizeof(mii));
	mii.cbSize     = sizeof(mii);
	mii.fMask      = MIIM_FTYPE | MIIM_STRING;
	mii.fType      = MFT_STRING;
	mii.dwTypeData = (ines_str_t)text;      /* SetMenuItemInfo 的接口本身不是 const */
	mii.cch        = (UINT)_tcslen(text);

	SetMenuItemInfo(hMenu, pos, TRUE, &mii);
}

/** 按位置路径取子菜单标题(buf 里已是拼接好的文本, 找不到返回 NULL) */
static BOOL i18n_ui_popup_text(const int* path, int depth, int pos,
							   ines_str_t buf, ines_size_t len)
{
	int  i;
	int  d;

	for(i = 0; i < (int)count_of(s_menu_pops); i++)
	{
		const APP_MENU_POPUP*  p = &s_menu_pops[i];

		for(d = 0; d < (int)count_of(p->path); d++)
		{
			int  want;

			if(d < depth)            want = path[d];
			else if(d == depth)      want = pos;
			else                     want = -1;

			if(p->path[d] != want)
				break;
		}

		if(d != (int)count_of(p->path))
			continue;

		ines_strncpy(buf, ines_i18n_text(p->key), len - 1);
		buf[len - 1] = '\0';

		if(p->mnemonic != 0)
			i18n_ui_add_mnemonic(buf, len, p->mnemonic);

		return TRUE;
	}

	return FALSE;
}

static void i18n_ui_apply_sub_menu(HMENU hMenu, const int* path, int depth)
{
	UINT   n = GetMenuItemCount(hMenu);
	UINT   i;
	size_t k;
	int    child[4];

	if((n == (UINT)-1) || (depth >= (int)count_of(child)))
		return;

	for(k = 0; k < count_of(child); k++)
		child[k] = (k < (size_t)depth) ? path[k] : -1;

	for(i = 0; i < n; i++)
	{
		ines_char_t  buf[256];
		MENUITEMINFO mii;

		ZeroMemory(&mii, sizeof(mii));
		mii.cbSize = sizeof(mii);
		mii.fMask  = MIIM_ID | MIIM_SUBMENU | MIIM_FTYPE;

		if(!GetMenuItemInfo(hMenu, i, TRUE, &mii))
			continue;

		if(mii.hSubMenu != NULL)      /* 子菜单: 标题按位置路径设, 再递归进里面 */
		{
			if(i18n_ui_popup_text(path, depth, (int)i, buf, count_of(buf)))
				i18n_ui_set_item_text(hMenu, i, buf);

			if(depth < (int)count_of(child))
			{
				child[depth] = (int)i;
				i18n_ui_apply_sub_menu(mii.hSubMenu, child, depth + 1);
				child[depth] = -1;
			}

			continue;
		}

		if(mii.wID == 0)              /* 分隔线 */
			continue;

		if(i18n_ui_menu_text(mii.wID, buf, count_of(buf)) != NULL)
			i18n_ui_set_item_text(hMenu, i, buf);
	}
}

BOOL i18n_ui_apply_menu(HMENU hMenu)
{
	if(hMenu == NULL)
		return FALSE;

	i18n_ui_apply_sub_menu(hMenu, NULL, 0);

	return TRUE;
}


/* ------------------------------------------------------------------ */
/* 对话框自适应(§8.1: 先测量, 再定位)                                   */
/* ------------------------------------------------------------------ */

typedef struct _app_fit_ctrl_
{
	HWND      hwnd;
	unsigned  flags;
	RECT      rc;          /* 相对对话框客户区 */
	int       need_w;      /* 文本 + 装饰所需宽度 */
	int       need_h;      /* 换行后所需高度(0 表示不变) */
	int       min_w;       /* 该控件的最小宽度(按钮等) */
} APP_FIT_CTRL;

/** 按控件类别返回文本之外的装饰宽度 / 最小宽度 */
static void i18n_ui_ctrl_decor(HWND hCtrl, int* decor, int* min_w)
{
	ines_char_t  cls[64];

	*decor = 4;
	*min_w = 0;

	cls[0] = '\0';
	GetClassName(hCtrl, cls, (int)count_of(cls));

	if(_tcsicmp(cls, ISTR("Button")) == 0)
	{
		DWORD  style = (DWORD)GetWindowLong(hCtrl, GWL_STYLE);
		int    type  = (int)(style & BS_TYPEMASK);
		int    edge  = 2 * GetSystemMetrics(SM_CXEDGE) + 6;

		*min_w = APP_BTN_MIN_W;

		if((type == BS_CHECKBOX) || (type == BS_AUTOCHECKBOX) ||
		   (type == BS_RADIOBUTTON) || (type == BS_AUTORADIOBUTTON))
		{
			*decor = GetSystemMetrics(SM_CXMENUCHECK) + 6;
			*min_w = 0;
		}
		else if(type == BS_GROUPBOX)
		{
			*decor = 12;
			*min_w = 0;
		}
		else
		{
			*decor = edge;
		}
	}
	else if(_tcsicmp(cls, ISTR("Edit")) == 0)
	{
		*decor = 2 * GetSystemMetrics(SM_CXEDGE) + 4;
	}
	else if(_tcsicmp(cls, ISTR("Static")) == 0)
	{
		*decor = 2;
	}
}

static void i18n_ui_measure(HWND hDlg, HDC hDC, APP_FIT_CTRL* c)
{
	ines_char_t  text[512];
	SIZE         sz;
	int          decor;
	int          min_w;
	int          width;
	int          height;

	text[0] = '\0';
	GetWindowText(c->hwnd, text, (int)count_of(text));

	if(text[0] == '\0')            /* 空文本(动态标签)不参与测量 */
	{
		c->need_w = c->rc.right - c->rc.left;
		c->need_h = 0;
		return;
	}

	GetTextExtentPoint32(hDC, text, (int)_tcslen(text), &sz);

	i18n_ui_ctrl_decor(c->hwnd, &decor, &min_w);

	width        = c->rc.right - c->rc.left;
	height       = c->rc.bottom - c->rc.top;
	c->need_w    = sz.cx + decor;
	c->need_h    = 0;
	c->min_w     = min_w;

	if(c->need_w < min_w)
		c->need_w = min_w;

	if(c->flags & APP_FIT_WRAP)    /* 可换行: 按当前宽度算折行后的高度 */
	{
		RECT  calc;

		SetRect(&calc, 0, 0, width, 0);
		DrawText(hDC, text, -1, &calc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS);

		if(calc.bottom > height)
			c->need_h = calc.bottom;
	}
}

void i18n_ui_widen_control(HWND hDlg, int id, int need_w, unsigned flags)
{
	HWND   hCtrl;
	RECT   rc;
	int    left;

	if(hDlg == NULL)
		return;

	hCtrl = GetDlgItem(hDlg, id);
	if(hCtrl == NULL)
		return;

	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (LPPOINT)&rc, 2);

	left = rc.left;

	if(flags & APP_FIT_ANCHOR_RIGHT)      /* 右边距不变: 左边缘右移 */
		left = rc.right - need_w;

	SetWindowPos(hCtrl, NULL, left, rc.top, need_w, rc.bottom - rc.top,
				 SWP_NOZORDER | SWP_NOACTIVATE);
}

void i18n_ui_apply_dialog(HWND hDlg, const APP_DLG_ITEM* items, int count)
{
	APP_FIT_CTRL  ctrls[APP_FIT_MAX];
	HFONT         hFont;
	HDC           hDC;
	HGDIOBJ       hOld;
	RECT          rcClient;
	int           i;
	int           n = 0;
	int           overflow = 0;      /* 对话框需要加宽的量 */
	int           grow_h   = 0;      /* 因换行需要加高的量 */
	int           wrap_bottom = -1;  /* 换行控件的原始下沿: 它下面的控件都要下移 */

	if((hDlg == NULL) || (items == NULL) || (count <= 0))
		return;

	if(count > APP_FIT_MAX)
		count = APP_FIT_MAX;

	hDC = GetDC(hDlg);
	if(hDC == NULL)
		return;

	hFont = (HFONT)SendMessage(hDlg, WM_GETFONT, 0, 0);
	hOld  = (hFont != NULL) ? SelectObject(hDC, hFont) : NULL;

	GetClientRect(hDlg, &rcClient);

	/* ---- 第一趟: 设文本 + 测量 ---- */
	for(i = 0; i < count; i++)
	{
		HWND  hCtrl = GetDlgItem(hDlg, items[i].id);

		if(hCtrl == NULL)
			continue;

		SetWindowText(hCtrl, i18n_ui_text(items[i].key));

		ZeroMemory(&ctrls[n], sizeof(ctrls[n]));
		ctrls[n].hwnd = hCtrl;
		ctrls[n].flags = items[i].flags;

		GetWindowRect(hCtrl, &ctrls[n].rc);
		MapWindowPoints(NULL, hDlg, (LPPOINT)&ctrls[n].rc, 2);

		i18n_ui_measure(hDlg, hDC, &ctrls[n]);

		if((ctrls[n].flags & APP_FIT_WRAP) && (ctrls[n].need_h > 0))
		{
			int  bottom = ctrls[n].rc.bottom;

			if(bottom > wrap_bottom)
				wrap_bottom = bottom;
		}

		n++;
	}

	if(hOld != NULL)
		SelectObject(hDC, hOld);
	ReleaseDC(hDlg, hDC);

	GetClientRect(hDlg, &rcClient);

	/* ---- 第二趟: 算溢出量 ---- */
	for(i = 0; i < n; i++)
	{
		int  right;
		int  delta_h;

		if(!(ctrls[i].flags & APP_FIT_ANCHOR_RIGHT))
			right = ctrls[i].rc.left + ctrls[i].need_w;
		else
			right = ctrls[i].rc.right;

		if(right > rcClient.right - APP_CTRL_MARGIN)
		{
			int  more = right - (rcClient.right - APP_CTRL_MARGIN);

			if(more > overflow)
				overflow = more;
		}

		delta_h = (ctrls[i].need_h > 0) ? (ctrls[i].need_h - (ctrls[i].rc.bottom - ctrls[i].rc.top)) : 0;
		if(delta_h > grow_h)
			grow_h = delta_h;
	}

	/* 需要第 2 趟结果才知道要不要加宽对话框 —— 这里一次性把宽与高都算完再加 */
	if((overflow > 0) || (grow_h > 0))
	{
		RECT  rcWin;
		int   cx;
		int   cy;

		GetWindowRect(hDlg, &rcWin);
		cx = (rcWin.right - rcWin.left) + overflow;
		cy = (rcWin.bottom - rcWin.top) + grow_h;

		SetWindowPos(hDlg, NULL, 0, 0, cx, cy,
					 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	}

	/* ---- 第三趟: 重新定位 ---- */
	for(i = 0; i < n; i++)
	{
		int  x = ctrls[i].rc.left;
		int  y = ctrls[i].rc.top;
		int  w = ctrls[i].rc.right - ctrls[i].rc.left;
		int  h = ctrls[i].rc.bottom - ctrls[i].rc.top;

		if(ctrls[i].flags & APP_FIT_ANCHOR_RIGHT)
			x += overflow;                     /* 右边缘跟着对话框一起走 */
		else
		{
			if(ctrls[i].need_w > w)
				w = ctrls[i].need_w;

			if(ctrls[i].flags & APP_FIT_GROW_W)
				w += overflow;                 /* 编辑框 / 列表跟着加宽 */
		}

		if((ctrls[i].flags & APP_FIT_WRAP) && (ctrls[i].need_h > 0))
			h = ctrls[i].need_h;

		/* 换行控件以下的控件一起下移, 保持相对位置 */
		if((grow_h > 0) && (wrap_bottom >= 0) && (ctrls[i].rc.top > wrap_bottom))
			y += grow_h;

		SetWindowPos(ctrls[i].hwnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
	}

	InvalidateRect(hDlg, NULL, TRUE);
}

void i18n_ui_fit_columns(HWND hList)
{
	HWND  hHeader;
	int   count;
	int   i;

	if(hList == NULL)
		return;

	/* ListView_GetColumnCount 需要较新的 WINVER, 这里统一走 header 取列数 */
	hHeader = ListView_GetHeader(hList);
	if(hHeader == NULL)
		return;

	count = Header_GetItemCount(hHeader);
	if(count <= 0)
		return;

	for(i = 0; i < count; i++)
	{
		int  design = ListView_GetColumnWidth(hList, i);
		int  need;

		ListView_SetColumnWidth(hList, i, LVSCW_AUTOSIZE_USEHEADER);
		need = ListView_GetColumnWidth(hList, i) + 8;

		/* 不小于资源里的设计宽度(英文通常比译文短) */
		ListView_SetColumnWidth(hList, i, (need > design) ? need : design);
	}
}
