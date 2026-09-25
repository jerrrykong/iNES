#ifndef __LOG_H__
#define __LOG_H__

#include "idef.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef ines_int_t        ines_log_level_t;
typedef ines_cstr_t       ines_log_module_t;

#ifdef  WIN32

#define  __TFUNCTION__   _T(__FUNCTION__)
#define  __TFILE__       _T(__FILE__)
#define  __TLINE__       _T(__LINE__)
#else
#define  __TFUNCTION__   __FUNCTION__
#define  __TFILE__       __FILE__
#define  __TLINE__       __LINE__
#endif


#define LOG_TRA  1
#define LOG_DBG  2
#define LOG_INF  3
#define LOG_WAR  4
#define LOG_ERR  5
#define LOG_FAU  6
#define LOG_NTY  10
#define LOG_MAX  99

#define MOD_SYS      ISTR("<SYS>")
#define MOD_INES     ISTR("<INES>")
#define MOD_ROM      ISTR("<ROM>")
#define MOD_CPU      ISTR("<CPU>")
#define MOD_MMC      ISTR("<MMC>")
#define MOD_PPU      ISTR("<PPU>")
#define MOD_APU      ISTR("<APU>")
#define MOD_IN       ISTR("<INPUT>")
#define MOD_SCN      ISTR("<SCREEN>")
#define MOD_NET      ISTR("<NET>")


//#define INES_LOG(l,m,f,...)   do{  if(ines_check_level(l)) { ines_log((l),(m),ISTR("") f, __VA_ARGS__); } } while(0)
#define INES_LOG(l,m,...)   do{  if(ines_check_level(l)) { ines_log_r((l),(m),ISTR("")  __VA_ARGS__); } } while(0)

ines_cstr_t ines_get_ctime_u();
#ifdef INES_POSIX
void ines_log(ines_log_level_t level, ines_log_module_t module, ines_cstr_t strFmt, ...) __attribute__((format (printf, 3,4 ))) ;
void ines_log_r(ines_log_level_t level, ines_log_module_t module, ines_cstr_t strFmt, ...)  __attribute__((format (printf, 3,4 )));
#else
void ines_log(ines_log_level_t level, ines_log_module_t module, ines_cstr_t strFmt, ...);
void ines_log_r(ines_log_level_t level, ines_log_module_t module, ines_cstr_t strFmt, ...);
#endif
ines_bool_t ines_check_level(ines_log_level_t level);
ines_cstr_t  ines_get_last_error_log(ines_int_t index, ines_int_t*  plevel);

/**
 * 把 UTF-8 窄串(const char*) 转成 ines_char_t 串, 供 INES_LOG 以 %s 打印。
 *
 * UNICODE 目标(iNES.exe, ines_char_t == wchar_t)下窄串必须显式转码, 否则两条路都是错的:
 *   - 直接用 %s: 把 char* 当成 wchar_t* 解读, 乱码且可能越界;
 *   - 用 %S    : CRT 按当前 locale 逐字节转宽, UTF-8 的中日文同样变成乱码。
 * 转出来的串是 ines_char_t, 与"日志文件一律 UTF-8 落盘"的约定配套。
 *
 * @param pDst  输出缓冲(调用方提供, 通常是栈上的 ines_char_t 数组)
 * @param nDst  输出缓冲的**元素个数**(不是字节数), 必须 > 0
 * @param pUtf8 输入: UTF-8 窄串, 可为 NULL(结果为空串)
 * @return pDst(便于直接作为 INES_LOG 实参); pDst 为 NULL 或 nDst 为 0 时返回 NULL
 * @note 目标缓冲装不下时结果为**空串**(而不是截断): 宁可丢内容, 也不越界或写出半个多字节序列。
 *       调用方按参数实际长度给缓冲即可(语言 id / key / 名称这类短串都远小于 64)。
 */
ines_cstr_t ines_utf8_to_ines(ines_str_t pDst, ines_size_t nDst, const char* pUtf8);

ines_log_level_t  ines_get_log_level();
void ines_set_log_level(ines_log_level_t level);

void ines_set_log_stamp_func(ines_int64_t (*f) (void));

#ifdef __cplusplus
};
#endif



#endif


