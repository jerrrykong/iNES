#ifndef __IDEF_H__
#define __IDEF_H__


#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <memory.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <math.h>

// 平台归一化: Windows 用 WIN32 区分, 其余 POSIX 平台(Linux/Android/macOS)
// 统一用 INES_POSIX 区分, 避免源码里散落的非标准宏 "linux"。
// 说明: 编译器在 Linux/Android 目标上会预定义 linux/__linux__, 在 macOS 上会
//       预定义 __APPLE__, 这里统一收敛为 INES_POSIX; CMake 也会显式定义它。
#if defined(linux) || defined(__linux__) || defined(__ANDROID__) || defined(__APPLE__)
	#ifndef INES_POSIX
		#define INES_POSIX 1
	#endif
#endif

#ifdef WIN32
#pragma warning(disable:4996)
#include <tchar.h>
#elif defined(INES_POSIX)
#include <unistd.h>
// struct timeval: macOS 上不随 <time.h> 提供, 必须显式包含
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
// strcasecmp: macOS 上声明在 <strings.h>(Linux 由 <string.h> 提供)
#include <strings.h>
#endif

#include <assert.h>

// basic type


typedef  unsigned char  ines_byte_t;
typedef  signed char    ines_sbyte_t;
typedef  signed int     ines_bool_t;
typedef  unsigned short ines_word_t;
typedef  signed short   ines_sword_t;
typedef  unsigned int   ines_dword_t;


#ifdef WIN32
typedef  const TCHAR*   ines_cstr_t;
typedef  TCHAR*         ines_str_t;
typedef  TCHAR          ines_char_t;

#define PRI64    "I64"


#elif defined(INES_POSIX)
typedef  const char*   ines_cstr_t;
typedef  char*         ines_str_t;
typedef  char          ines_char_t;

#define PRI64    "ll"

#endif
typedef  int            ines_int_t;
typedef  size_t         ines_size_t;
#ifdef WIN32
typedef  __int64        ines_int64_t;
#elif defined(INES_POSIX)
typedef  long long        ines_int64_t;
#endif

#define ines_true   1
#define ines_false  0


// plain string


#ifdef  WIN32
#define ISTR(s)   _T(s)

#define  ines_printf     _tprintf
#define  ines_sprintf    _stprintf
#define  ines_snprintf   _sntprintf
#define  ines_vsnprintf  _vsntprintf
#define  ines_strstr     _tcsstr
#define  ines_strcpy     _tcscpy
#define  ines_strncpy     _tcsncpy
#define  ines_strcmp     _tcscmp
#define  ines_strcasecmp     _tcsicmp
#elif defined(INES_POSIX)
#define ISTR(s)   s

#define  ines_printf     printf
#define  ines_sprintf    sprintf
#define  ines_snprintf   snprintf
#define  ines_vsnprintf  vsnprintf
#define  ines_strstr     strstr
#define  ines_strcpy     strcpy
#define  ines_strncpy    strncpy
#define  ines_strcmp     strcmp
#define  ines_strcasecmp     strcasecmp


#define _tstat     stat
#define _stat      stat
#define _trename   rename
#define _tfopen    fopen
#define _fputts    fputs
#define _vftprintf vfprintf
#define _istspace  isspace
#define _tcslen    strlen
#define _tcserror  strerror

#endif

#define ines_alloc_st(st)    (st*)malloc(sizeof(st))
#define ines_alloc(sz)        malloc(sz)
#define ines_free(p)          free(p);


#define ines_valid_str(s)  ((s)!=NULL)
#define ines_iif(b,x,y)    ((b)?(x):(y))
#define ines_ifnull(s,def)  ines_iif(ines_valid_str(s),(s),(def))


#define count_of(a)  (sizeof(a)/sizeof((a)[0]))

#define INES_MAX_PATH     4096


#define MAKE_BYTES(b0,b1,b2,b3,b4,b5,b6,b7)   ((ines_byte_t)( ((b0)?1:0)|((b1)?2:0)|((b2)?4:0)|((b3)?8:0)|((b4)?16:0)|((b5)?32:0)|((b6)?64:0)|((b7)?128:0) ))

#ifdef __cplusplus
extern "C"
{
#endif


ines_char_t* get_file_title(ines_char_t* title, ines_cstr_t  file_path);

// 取"用户数据目录"(配置/日志/存档/截图/即时存档的存放根目录), 返回值即 szPath,
// 目录不存在时会被创建(创建失败时仍然返回路径, 由调用方在写入时报错):
//   Windows : 可执行文件所在目录
//   macOS   : $HOME/Library/Application Support/iNES
//   其它POSIX: $HOME/.local/share/iNES
// 之所以不在 Windows 上改变原行为, 是为了让 win32 前端与旧版本完全一致。
ines_cstr_t ines_get_data_dir(ines_str_t szPath, ines_size_t szLen);

#ifdef __cplusplus
}
#endif


#endif

