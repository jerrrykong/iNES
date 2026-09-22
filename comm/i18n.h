#ifndef __INES_I18N_H__
#define __INES_I18N_H__


#include "idef.h"


#ifdef __cplusplus
extern "C"
{
#endif


// 语言 ID / 名称缓冲大小
#define INES_I18N_ID_MAX      16      /* en / zh-CN / ja / fr */
#define INES_I18N_NAME_MAX    64      /* English / 简体中文 / 日本語(UTF-8) */
#define INES_I18N_KEY_MAX     64      /* "dialog.openrom.parsing_zero_format" */
#define INES_I18N_LANG_MAX    16      /* 可用语言数上限(内置 en + 外部文件) */
#define INES_I18N_DIR_MAX     8       /* 额外搜索目录数上限(前端追加) */

// 内置英文表条目(英文真源, 见 comm/i18n_en.c)
typedef struct _ines_i18n_entry_
{
	const char*  key;      /* ASCII 点号命名: "menu.file.open" */
	const char*  text;     /* UTF-8 英文原文 */
} ines_i18n_entry_t;

// 语言清单条目(name 以本语言自身书写, 直接显示在"语言"菜单)
typedef struct _ines_i18n_lang_
{
	ines_char_t  id[INES_I18N_ID_MAX];       /* en / zh-CN / ja / fr */
	ines_char_t  name[INES_I18N_NAME_MAX];   /* English / 简体中文 / 日本語 */
	int          builtin;                    /* 1 = 内置(不依赖文件) */
} ines_i18n_lang_t;


extern const ines_i18n_entry_t  g_ines_i18n_en[];        /* 内置英文表(按 key 升序) */
extern const int                g_ines_i18n_en_count;    /* 条目数 */


/**
 * 追加一个语言文件搜索目录(UTF-8 路径)。
 * 必须在 ines_i18n_init() 之前调用, 可多次调用; 先加入的目录优先级更高
 * (内置搜索目录 <数据目录>/lang 始终最先, 即优先级最高)。
 */
void ines_i18n_add_lang_dir(const char* dir);

/**
 * 初始化: 建立条目表(以英文填充)、扫描语言目录、按 preferred 选定语言并加载。
 * preferred 为前端探测到的系统语言标签(如 "zh-Hans-CN"), 可为 NULL(则使用 en)。
 * 返回 0 成功。可重复调用(等于重新扫描 + 重新加载)。
 */
int  ines_i18n_init(const char* preferred);
void ines_i18n_fini(void);

/** 枚举可用语言(含内置 en), 返回数量; langs 可为 NULL(仅取数量)。 */
int  ines_i18n_enum(ines_i18n_lang_t* langs, int max_count);

/**
 * 重新扫描语言目录(运行时动态加载): 新增/删除/改写 ini 后调用即可, 无需重启。
 *  - 重新登记所有语言文件(<数据目录>/lang 优先, 其次前端追加的目录);
 *  - 当前语言的文件仍在则按新内容重新载入文本, 已被删除则回落英文;
 *  - 返回可用语言数(含内置 en); 未初始化返回 -1。
 * 注意: 已取出的 ines_i18n_text() 指针在本调用后失效(与切换语言一样不要缓存)。
 */
int  ines_i18n_rescan(void);

/**
 * 把系统语言标签归一到可用语言 ID: "zh-Hans-CN" -> "zh-CN" -> "zh" -> "en"。
 * 返回内部静态缓冲(ASCII), 永不返回 NULL。
 */
const char* ines_i18n_match(const char* lang_tag);

/**
 * 切换语言: 成功返回 0; 未知 ID 返回 -1(不改变当前语言)。
 * 切到 "en" 时恢复内置英文(不依赖文件)。
 */
int  ines_i18n_set_language(const char* id);
const char* ines_i18n_language(void);          /* 当前语言 ID(ASCII) */

/**
 * 取文本。key 为 ASCII("menu.file.open"); 缺失时回退英文, 再缺返回 key 本身。
 * 返回进程内静态串(切换语言后失效), 不要跨语言切换缓存该指针。
 */
ines_cstr_t ines_i18n_text(const char* key);

/**
 * 带格式: ines_i18n_text_fmt(buf, len, "msg.state_not_found_format", 3);
 * 内部即 ines_snprintf(buf, len, ines_i18n_text(key), ...)(占位符一律 printf 风格)。
 */
ines_str_t  ines_i18n_text_fmt(ines_str_t buf, ines_size_t len, const char* key, ...);


#ifdef __cplusplus
};
#endif


#endif
