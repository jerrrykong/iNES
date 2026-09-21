#ifndef __WIN32_I18N_UI_H__
#define __WIN32_I18N_UI_H__

/*
 * i18n_ui —— win32 前端的 i18n 接入层
 *
 * 与 mac/iNESi18n.{h,m} 一一对应(同一份 key 表 `lang/*.ini`), 只负责三件事:
 *   1. 探测系统语言(GetUserDefaultUILanguage -> BCP-47 标签)并把它交给 comm/i18n;
 *   2. 读/写 config.ini 的 [ui] language(与 mac 完全相同的键名);
 *   3. 把译文接到菜单 / 对话框 / 调试窗口上, 并按实际字体度量做自适应。
 *
 * 硬性约定(与 docs/i18n-plan.md §4.2、§16 一致):
 *   - 语言文件里不写助记符 `&` 与快捷键 `\tCtrl+O`, 由本文件的 win32 平台表在运行时追加;
 *   - 数字 / 硬件术语(4:3、16:9、PRG、CHR、Mapper...)不入语言文件, 代码直接写字面量;
 *   - 本文件不放任何译文, 缺 key 时由 comm/i18n 回退内置英文。
 */

#include "stdafx.h"
#include "../comm/idef.h"
#include "../comm/i18n.h"
#include "Resource.h"


/** 语言切换后广播给所有本进程顶层窗口的消息(调试窗口 / 对话框据此刷新文本) */
#define WM_APP_LANGCHANGED      (WM_APP + 10)

/** "语言"子菜单命令 ID 的起点(第 i 项 = IDM_LANGUAGE_BASE + i, 最多 APP_LANG_ITEM_MAX 项) */
#define APP_LANG_ITEM_MAX       16

/** 主菜单里"工具"的位置, 以及"工具"菜单里"语言"子菜单的位置 —— 与 iNES.rc 的排列保持一致 */
#define APP_MENU_TOOLS_POS      2
#define APP_TOOLS_LANG_POS      1


/**
 * 取界面文本(key 为 ASCII 点号形式, 如 "menu.file.open")。
 * 返回的字符串由 comm/i18n 持有, 切换语言后立即失效, **不要缓存**。
 */
ines_cstr_t i18n_ui_text(const char* key);

/**
 * 带 printf 风格参数: i18n_ui_text_fmt("menu.state.save_format", 3)。
 * 返回内部轮换静态缓冲(可安全嵌套 4 层), 同样不要缓存。
 */
ines_cstr_t i18n_ui_text_fmt(const char* key, ...);

/** 便捷宏(与 mac 的 L10N / L10NF 同名同义) */
#define L10N(k)         i18n_ui_text((k))
#define L10NF(k, ...)   i18n_ui_text_fmt((k), __VA_ARGS__)


/**
 * 初始化: ines_i18n_init(系统语言) -> 应用 config.ini 的 [ui] language。
 * 必须在任何窗口 / 菜单创建之前调用(窗口类要读编辑器窗口标题)。
 */
void i18n_ui_init(void);
void i18n_ui_fini(void);

/** 当前语言索引(语言菜单勾选用) */
int  i18n_ui_language_index(void);

/**
 * 选中语言菜单的第 index 项: 切到该语言并写回 config.ini 的 [ui] language。
 * 成功返回非零; 失败(索引越界)返回 0 且不改变当前语言。
 * 调用方(iNES.c)随后负责刷新菜单 / 标题 / 各窗口文本。
 */
BOOL i18n_ui_select_language(int index);

/** 重建"语言"子菜单(按 ines_i18n_enum() 的结果, 各项以本语言自身的名字显示, 当前项打勾) */
BOOL i18n_ui_build_language_menu(HMENU hMainMenu);

/** 向本进程所有顶层窗口广播 WM_APP_LANGCHANGED(语言切换后即时刷新已打开的窗口) */
void i18n_ui_notify_language_change(void);


/** 菜单项文本表: id -> key(可带一个 %d 参数) + 助记符 + 快捷键文本 */
typedef struct _app_menu_item_
{
	UINT        id;
	const char* key;      /* i18n key; arg >= 0 时该值是 "%d" 格式串 */
	int         arg;      /* >= 0: 作为 %d 参数参与格式化; -1: 无参数 */
	int         mnemonic; /* 助记符字母('O'), 0 = 不设 */
	ines_cstr_t accel;    /* 追加在 '\t' 之后的快捷键文本, NULL = 不追加 */
} APP_MENU_ITEM;

/** 按 platform 表把菜单(含各级子菜单标题)的文本设为当前语言; 不支持的菜单返回 0 */
BOOL i18n_ui_apply_menu(HMENU hMenu);

/** 拼出某个菜单项的完整文本(译文 + & + \t快捷键); 找不到表项返回 NULL(保持资源原文) */
ines_cstr_t i18n_ui_menu_text(UINT id, ines_str_t buf, ines_size_t len);

/** 某个菜单项的快捷键文本("\tCtrl+O"), 没有则返回 NULL */
ines_cstr_t i18n_ui_menu_accel(UINT id);

/** 在 buf 的第一个 ch(忽略大小写)之前插入 '&'; 找不到则追加 " (&X)" */
void i18n_ui_add_mnemonic(ines_str_t buf, ines_size_t len, int ch);


/** 对话框控件自适应标志 */
#define APP_FIT_NONE          0x00   /* 只换文本, 尺寸随原文大小 */
#define APP_FIT_GROW_W        0x01   /* 对话框变宽时该控件跟着变宽(EDIT / LIST / 整行静态文本) */
#define APP_FIT_ANCHOR_RIGHT  0x02   /* 右边缘与对话框右间距保持不变(右侧按钮) */
#define APP_FIT_WRAP          0x04   /* 可自动换行: 按实际宽度算行数并加高(LTEXT 长句) */

/** 对话框控件表: 控件 ID -> key + 自适应标志 */
typedef struct _app_dlg_item_
{
	int         id;
	const char* key;
	unsigned    flags;
} APP_DLG_ITEM;

/**
 * 对话框接入: 逐个设文本(SetDlgItemText)并按译文宽度做"溢出即加宽 / 换行加高"。
 * 算法见 docs/i18n-plan.md §8.1: 先按实际字体测量(两次遍历), 再统一移动/加宽。
 */
void i18n_ui_apply_dialog(HWND hDlg, const APP_DLG_ITEM* items, int count);

/** 列表控件: 表头按译文自动加宽(不小于设计宽度) */
void i18n_ui_fit_columns(HWND hList);

/** 单个控件加宽到 need_w(按钮另有 ~78px 的最小宽度), flags 决定贴哪边 */
void i18n_ui_widen_control(HWND hDlg, int id, int need_w, unsigned flags);

#endif  /* __WIN32_I18N_UI_H__ */
