#ifndef __INES_I18N_MAC_H__
#define __INES_I18N_MAC_H__


#import <Cocoa/Cocoa.h>


/** 语言切换完成通知(object 为新语言 ID, NSString*) */
extern NSString* const INESLanguageDidChangeNotification;


/**
 * 初始化: 追加 bundle 内的 lang 目录 -> ines_i18n_init(系统语言) -> 应用 config.ini 的 [ui] language。
 * 必须在配置文件(iNES_config_set_file)之后、任何界面创建之前调用。
 */
void iNES_i18n_init(void);

/** 当前语言 ID(ASCII, 如 "en" / "zh-CN") */
NSString* iNES_i18n_language_id(void);

/**
 * 取界面文本。key 为 ASCII 点号命名("menu.file.open")。
 * 文本结尾的 ASCII "..." 会被替换成 "…"(U+2026), 以符合 macOS 习惯。
 */
NSString* iNES_i18n_text(const char* key);

/**
 * 带 printf 风格参数(字符串一律用 %s 传 C 串): iNES_i18n_text_format("msg.state_not_found_format", 3)
 * 注意: 格式串来自语言文件(运行时才确定), 因此不加 NS_FORMAT_FUNCTION 以免编译器误报。
 */
NSString* iNES_i18n_text_format(const char* key, ...);

/** 语言清单: @[ @[id, name], ... ], 第一项恒为内置英文 */
NSArray<NSArray<NSString*>*>* iNES_i18n_languages(void);

/**
 * 重新扫描语言目录(动态加载): 运行时往 <数据目录>/lang 丢进(或改/删) *.ini 后,
 * 无需重启, 下次打开"语言"菜单即可看到。返回可用语言数(含内置英文)。
 */
int iNES_i18n_rescan(void);

/**
 * 用户可放自定义语言文件的目录(<数据目录>/lang), 优先级高于 bundle 内的同名语言。
 * 目录不存在时会被创建; 失败返回 nil。
 */
NSString* iNES_i18n_user_lang_dir(void);

/** 切换语言(写回 config.ini 并广播 INESLanguageDidChangeNotification); 失败返回 NO */
BOOL iNES_i18n_set_language(NSString* langID);


/** 便捷宏: L10N("menu.file.open") / L10NF("msg.state_not_found_format", 3) */
#define L10N(k)         iNES_i18n_text(k)
#define L10NF(k, ...)   iNES_i18n_text_format(k, __VA_ARGS__)


#endif
