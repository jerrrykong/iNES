/*
 * iNESi18n —— macOS 前端的 i18n 桥接层
 *
 * 职责(仅此三项, 业务逻辑仍留在 comm/i18n):
 *   1. 探测系统语言(NSLocale)并把 bundle 内的 lang 目录告诉 comm 层;
 *   2. 读/写 config.ini 的 [ui] language;
 *   3. 把 ines_cstr_t 转成 NSString, 并把 ASCII "..." 归一成 "…"(HIG)。
 */

#import "iNESi18n.h"

#import "iNESConfig.h"

#include "../comm/i18n.h"
#include "../comm/log.h"

#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>


NSString* const INESLanguageDidChangeNotification = @"INESLanguageDidChangeNotification";


/** ines_cstr_t(POSIX 下即 UTF-8 char*) -> NSString, 并把结尾的 "..." 换成 "…" */
static NSString* iNES_i18n_nsstr(ines_cstr_t text)
{
	NSString* str;

	if (text == NULL)
	{
		return @"";
	}

	str = [NSString stringWithUTF8String:text];
	if (str == nil)
	{
		return @"";
	}

	if ([str hasSuffix:@"..."])
	{
		str = [[str substringToIndex:(str.length - 3)] stringByAppendingString:@"…"];
	}

	return str;
}

void iNES_i18n_init(void)
{
	NSString*     preferred;
	NSString*     bundleLang;
	NSString*     userLang   = nil;
	ines_cstr_t   saved;

	/* 先确保 <数据目录>/lang 存在(用户可往里放自定义语言文件), 再扫描 */
	userLang = iNES_i18n_user_lang_dir();
	if (userLang != nil)
	{
		INES_LOG(LOG_INF, MOD_SYS, ISTR("i18n: user language dir = ") ISTR("%s\n"), [userLang UTF8String]);
	}

	/* bundle 内的 Resources/lang(优先级低于 <数据目录>/lang) */
	bundleLang = [[[NSBundle mainBundle] resourcePath] stringByAppendingPathComponent:@"lang"];
	if (bundleLang != nil)
	{
		ines_i18n_add_lang_dir([bundleLang UTF8String]);
	}

	preferred = [[NSLocale preferredLanguages] firstObject];
	if (preferred == nil)
	{
		preferred = @"en";
	}

	if (ines_i18n_init([preferred UTF8String]) != 0)
	{
		INES_LOG(LOG_ERR, MOD_SYS, ISTR("i18n: init failed, falling back to English\n"));
		return;
	}

	/* 用户显式选择过的语言优先于系统语言 */
	saved = GetConfigStr(ISTR("ui"), ISTR("language"), ISTR(""));
	if (saved != NULL && saved[0] != '\0')
	{
		if (ines_i18n_set_language(saved) != 0)
		{
			INES_LOG(LOG_WAR, MOD_SYS, ISTR("i18n: saved language ") ISTR("%s") ISTR(" unavailable, keep ") ISTR("%s\n"),
					 saved, ines_i18n_language());
		}
	}

	INES_LOG(LOG_INF, MOD_SYS, ISTR("i18n: system language = ") ISTR("%s") ISTR(", using ") ISTR("%s\n"),
			 [preferred UTF8String], ines_i18n_language());
}

NSString* iNES_i18n_language_id(void)
{
	return [NSString stringWithUTF8String:ines_i18n_language()];
}

NSString* iNES_i18n_text(const char* key)
{
	return iNES_i18n_nsstr(ines_i18n_text(key));
}

NSString* iNES_i18n_text_format(const char* key, ...)
{
	ines_char_t  buf[1024];
	va_list      args;

	/*
	 * 必须用 C 层的 vsnprintf 而不是 [NSString initWithFormat:arguments:]:
	 * NSString 的 %s 按"系统编码"解释字节, 遇到 UTF-8 的中日文参数会得到空串或乱码
	 * (实测: 标题 `iNES - 90tank - 動作中` 的状态段变空)。语言文件里的占位符一律
	 * 是 printf 风格, 在 C 层按字节拼好再转成 NSString 才与 win32 完全一致。
	 */
	va_start(args, key);
	ines_vsnprintf(buf, sizeof(buf), ines_i18n_text(key), args);
	va_end(args);

	buf[sizeof(buf) - 1] = '\0';

	return iNES_i18n_nsstr(buf);
}

NSArray<NSArray<NSString*>*>* iNES_i18n_languages(void)
{
	NSMutableArray* out;
	int             count;
	int             i;
	ines_i18n_lang_t* langs;

	count = ines_i18n_enum(NULL, 0);
	if (count <= 0)
	{
		return @[ @[ @"en", @"English" ] ];
	}

	langs = (ines_i18n_lang_t*)calloc((size_t)count, sizeof(ines_i18n_lang_t));
	if (langs == NULL)
	{
		return @[ @[ @"en", @"English" ] ];
	}

	ines_i18n_enum(langs, count);

	out = [NSMutableArray arrayWithCapacity:(NSUInteger)count];
	for (i = 0; i < count; i++)
	{
		NSString* ident = [NSString stringWithUTF8String:langs[i].id];
		NSString* name  = [NSString stringWithUTF8String:langs[i].name];

		[out addObject:@[ (ident != nil) ? ident : @"", (name != nil) ? name : @"" ]];
	}

	free(langs);

	return out;
}

NSString* iNES_i18n_user_lang_dir(void)
{
	ines_char_t  data_dir[INES_MAX_PATH];
	ines_char_t  lang_dir[INES_MAX_PATH];

	data_dir[0] = '\0';
	ines_get_data_dir(data_dir, INES_MAX_PATH);
	if (data_dir[0] == '\0')
	{
		return nil;
	}

	ines_snprintf(lang_dir, INES_MAX_PATH, ISTR("%s/lang"), data_dir);

	/* 目录不存在就建出来: 用户往这里丢 *.ini 即可新增语言, 不必动 bundle */
	if (mkdir(lang_dir, 0755) != 0 && errno != EEXIST)
	{
		return nil;
	}

	return [NSString stringWithUTF8String:lang_dir];
}

int iNES_i18n_rescan(void)
{
	return ines_i18n_rescan();
}

BOOL iNES_i18n_set_language(NSString* langID)
{
	const char* ident;

	if (langID == nil || langID.length == 0)
	{
		return NO;
	}

	ident = [langID UTF8String];
	if (ines_i18n_set_language(ident) != 0)
	{
		return NO;
	}

	SetConfigStr(ISTR("ui"), ISTR("language"), (ines_cstr_t)ident);

	[[NSNotificationCenter defaultCenter] postNotificationName:INESLanguageDidChangeNotification
														object:langID];

	return YES;
}
