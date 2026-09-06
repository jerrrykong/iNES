// stdafx.h : 标准系统包含文件的包含文件，
// 或是经常使用但不常更改的
// 特定于项目的包含文件
//

#pragma once
#define _CRT_NON_CONFORMING_SWPRINTFS
#include "targetver.h"

#define WIN32_LEAN_AND_MEAN             // 从 Windows 头中排除极少使用的资料
// Windows 头文件:
#include <windows.h>
#include <CommDlg.h>
#include <mmsystem.h>
#include <Shellapi.h>
#include <CommCtrl.h>

// C 运行时头文件
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include <locale.h>

#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <assert.h>

#ifdef WIN32
#include <winsock2.h>
#include <sys/types.h>
#include <sys/timeb.h>
#elif defined( linux)
#include <sys/time.h>
#endif


#define MAX_LOADSTRING 128


/// lua support
#ifdef __cplusplus
extern "C" {
#endif
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#ifdef __cplusplus
}
#endif

// TODO: 在此处引用程序需要的其他头文件
