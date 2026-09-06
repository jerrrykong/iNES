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
#ifdef linux
void ines_log(ines_log_level_t level, ines_log_module_t module, ines_cstr_t strFmt, ...) __attribute__((format (printf, 3,4 ))) ;
void ines_log_r(ines_log_level_t level, ines_log_module_t module, ines_cstr_t strFmt, ...)  __attribute__((format (printf, 3,4 )));
#else
void ines_log(ines_log_level_t level, ines_log_module_t module, ines_cstr_t strFmt, ...);
void ines_log_r(ines_log_level_t level, ines_log_module_t module, ines_cstr_t strFmt, ...);
#endif
ines_bool_t ines_check_level(ines_log_level_t level);
ines_cstr_t  ines_get_last_error_log(ines_int_t index, ines_int_t*  plevel);

ines_log_level_t  ines_get_log_level();
void ines_set_log_level(ines_log_level_t level);

void ines_set_log_stamp_func(ines_int64_t (*f) (void));

#ifdef __cplusplus
};
#endif



#endif


