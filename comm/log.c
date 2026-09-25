
#include "log.h"
//#include "../win32/iNes.h"
#include <sys/stat.h>
#include <time.h>
#include <stdarg.h>
#ifdef WIN32
#include <windows.h>
#include <sys/timeb.h>
#elif defined(INES_POSIX)
#endif

static  ines_int64_t  (*g_get_stamp_func) (void) =  NULL;


static ines_log_level_t  g_log_level = LOG_INF;


#define MAX_LOG_LASTERROR   16
#define LOG_LASTERROR_SECONDS   30

static time_t g_lastTime[MAX_LOG_LASTERROR];
static int    g_lastLevel[MAX_LOG_LASTERROR];
static ines_char_t  g_lastError[MAX_LOG_LASTERROR][1024];
static int    g_lastNum = 0;
static int    g_lastHead = 0;

static void add_last_error_log(ines_int_t level, ines_cstr_t strFmt, va_list vl)
{
	int i;
	int len;
	i = (g_lastHead + g_lastNum) % MAX_LOG_LASTERROR;

	len = ines_vsnprintf(g_lastError[i], sizeof(g_lastError[i]), strFmt, vl);
	while(len>0 && _istspace((unsigned char)g_lastError[i][len-1]) )
	{
		len--;
		g_lastError[i][len] = 0;
	}

	g_lastTime[i] = time(NULL);
	g_lastLevel[i] = level;

	if(g_lastNum < MAX_LOG_LASTERROR)
		g_lastNum++;
	else
		g_lastHead = (g_lastHead + 1) % MAX_LOG_LASTERROR;
}

ines_cstr_t  ines_get_last_error_log(ines_int_t index, ines_int_t*  plevel)
{
	int i;

	while(g_lastNum > 0)
	{
		if(g_lastTime[g_lastHead] +  LOG_LASTERROR_SECONDS < time(NULL))
		{
			g_lastHead = (g_lastHead + 1) % MAX_LOG_LASTERROR;
			g_lastNum--;
		}
		else
		{
			break;
		}
	}


	if(0 > index || index >= g_lastNum)
		return NULL;

	i = (index + g_lastHead) % MAX_LOG_LASTERROR;
	if(plevel) *plevel = g_lastLevel[i];
	return g_lastError[i];
}

ines_bool_t ines_check_level(ines_log_level_t level)
{
	return level >= g_log_level;

}
ines_log_level_t  ines_get_log_level()
{
	return g_log_level;
}
void ines_set_log_level(ines_log_level_t level)
{
	g_log_level = level;
}

void ines_set_log_stamp_func(ines_int64_t (*f) (void))
{
	g_get_stamp_func = f;	
}




static const ines_char_t hexchs[0x10] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

static void ines_log_perfix(ines_char_t* szBuf, ines_log_level_t  level)
{
	ines_int64_t  cycles;
	int            len = 0;
	switch(level)
	{
	case LOG_TRA: szBuf[len++] = 'T'; break;
	case LOG_DBG: szBuf[len++] = 'D'; break;
	case LOG_INF: szBuf[len++] = 'I'; break;
	case LOG_WAR: szBuf[len++] = 'W'; break;
	case LOG_ERR: szBuf[len++] = 'E'; break;
	case LOG_FAU: szBuf[len++] = 'F'; break;
	case LOG_NTY: szBuf[len++] = 'N'; break;
	default:      szBuf[len++] = '?'; break;
	}
	szBuf[len++] = ':';

	if(g_get_stamp_func != NULL)
	{
		cycles = (*g_get_stamp_func)();
		//szBuf[len++] = hexchs[(cycles>>60)&0x0f];
		//szBuf[len++] = hexchs[(cycles>>56)&0x0f];
		//szBuf[len++] = hexchs[(cycles>>52)&0x0f];
		//szBuf[len++] = hexchs[(cycles>>48)&0x0f];
		//szBuf[len++] = hexchs[(cycles>>44)&0x0f];
		//szBuf[len++] = hexchs[(cycles>>40)&0x0f];
		//szBuf[len++] = hexchs[(cycles>>36)&0x0f];
		//szBuf[len++] = hexchs[(cycles>>32)&0x0f];
		szBuf[len++] = hexchs[(cycles>>28)&0x0f];
		szBuf[len++] = hexchs[(cycles>>24)&0x0f];
		szBuf[len++] = hexchs[(cycles>>20)&0x0f];
		szBuf[len++] = hexchs[(cycles>>16)&0x0f];
		szBuf[len++] = hexchs[(cycles>>12)&0x0f];
		szBuf[len++] = hexchs[(cycles>>8)&0x0f];
		szBuf[len++] = hexchs[(cycles>>4)&0x0f];
		szBuf[len++] = hexchs[(cycles)&0x0f];
		szBuf[len++] = ':';
	}

	szBuf[len++] = 0;

}

// 计算日志文件的默认路径(返回 szBuf):
//   Windows  : 可执行文件同目录(保持原有行为)
//   macOS    : 用户数据目录 ~/Library/Application Support/iNES
//   Android  : /sdcard
//   其它POSIX: /tmp
// 非 Windows 平台不用相对路径, 因为从桌面/Finder 启动时进程工作目录是 "/", 不可写。
static ines_cstr_t ines_default_log_path(ines_char_t* szBuf, ines_size_t szLen)
{
	if(szBuf == NULL || szLen < 2)
		return ISTR("");

#ifdef WIN32
	{
		ines_char_t*  p;
		GetModuleFileName(NULL, szBuf, (DWORD)szLen);
		szBuf[szLen - 1] = 0;
		p = szBuf + _tcslen(szBuf);
		while(p > szBuf && p[-1] != '\\' && p[-1] != '/')
			p--;
		if((ines_size_t)(p - szBuf) + 8 >= szLen)
			return szBuf;  // 空间不足: 保留目录部分, 调用方会写入失败但不越界
		ines_strcpy(p, ISTR("iNES.log"));
	}
#elif defined(__ANDROID__)
	ines_strncpy(szBuf, ISTR("/sdcard/iNES.log"), szLen);
	szBuf[szLen - 1] = 0;
#else
	{
		ines_char_t  szDir[INES_MAX_PATH];
		ines_get_data_dir(szDir, count_of(szDir));
		if(szDir[0] != 0)
			ines_snprintf(szBuf, szLen, ISTR("%s/iNES.log"), szDir);
		else
		{
			ines_strncpy(szBuf, ISTR("/tmp/iNES.log"), szLen);
			szBuf[szLen - 1] = 0;
		}
	}
#endif

	return szBuf;
}


/* ---------------------------------------------------------------------------
 * 日志文件写入: 一律按 UTF-8 落盘
 *
 * 两个目标的字符集不同(见 CMakeLists.txt 注释), 同一份 log.c 会被编译两次:
 *   - inescore : 多字节(MBCS), ines_char_t == char。源码以 /utf-8 编译, 字面量
 *                在二进制里本就是 UTF-8 字节, 直接写即可, 不能按 CP_ACP 再做转换(会把中文转坏)。
 *   - iNES     : Unicode(_UNICODE), ines_char_t == wchar_t, 字面量是 UTF-16。
 *                此时 _fputts()/_vftprintf() 会把 UTF-16 码元按字节写进文件
 *                (连 ASCII 都是两字节一个字符, 文本编辑器打开就是乱码),
 *                因此必须显式 WideCharToMultiByte(CP_UTF8) 后按字节写。
 * 这样两个目标产出的 iNES.log 编码才一致(UTF-8), 与源码/文档的编码约定也一致。
 * --------------------------------------------------------------------------- */

#define LOG_LINE_CHARS   4096   /* 单条日志的字符数上限(超出截断), 与 INES_MAX_PATH 同规格 */
#define LOG_UTF8_CHUNK   1024   /* UTF-16 -> UTF-8 的输出缓冲(字节), 长日志分段转换 */


#if defined(WIN32) && defined(_UNICODE)

/**
 * 把一段 UTF-16 文本转成 UTF-8 写入日志文件。
 * @param pfLog 已打开的日志文件
 * @param str   UTF-16 文本(可为 NULL)
 * @note 长文本分段转换: 一个 UTF-16 码元最多展开成 3 个 UTF-8 字节, 按最坏情况取分段长度;
 *       分段边界若落在高位代理上则回退 1 个码元, 避免把代理对拆开。
 */
static void log_write_utf16(FILE* pfLog, ines_cstr_t str)
{
	char          szUtf8[LOG_UTF8_CHUNK];
	ines_size_t   pos;
	ines_size_t   total;

	if(pfLog == NULL || str == NULL)
		return;

	total = (ines_size_t)_tcslen(str);
	pos = 0;

	while(pos < total)
	{
		ines_size_t  chunk = (ines_size_t)(sizeof(szUtf8) - 1) / 3;
		ines_int_t   n;

		if(chunk > total - pos)
			chunk = total - pos;

		/* 低位代理必须紧跟在高位代理之后, 别在此处断开 */
		if(chunk > 1 && pos + chunk < total && str[pos + chunk - 1] >= 0xD800 && str[pos + chunk - 1] <= 0xDBFF)
			chunk--;

		n = (ines_int_t)WideCharToMultiByte(CP_UTF8, 0, str + pos, (int)chunk, szUtf8, (int)(sizeof(szUtf8) - 1), NULL, NULL);
		if(n <= 0)
			break;   /* 转换失败(含不可映射字符)就停在这里, 已写入的部分保留 */

		fwrite(szUtf8, 1, (ines_size_t)n, pfLog);
		pos += chunk;
	}
}

#endif


/**
 * 把一条"本地编码"文本按 UTF-8 写入日志文件。
 * @param pfLog 已打开的日志文件(可为 NULL, 此时静默返回)
 * @param str   win32 Unicode 下为 UTF-16, 其它情况本身就是 UTF-8 字节
 */
static void log_write_text(FILE* pfLog, ines_cstr_t str)
{
	if(pfLog == NULL || str == NULL)
		return;

#if defined(WIN32) && defined(_UNICODE)
	log_write_utf16(pfLog, str);
#else
	_fputts(str, pfLog);
#endif
}


/* ---------------------------------------------------------------------------
 * 日志参数侧: UTF-8 窄串 -> ines_char_t
 *
 * 与上面的落盘转换是同一个问题的两面: 落盘要保证 UTF-8, 参数也要先变成 ines_char_t
 * 才能用 %s 打印(详见 comm/log.h 里 ines_utf8_to_ines 的说明)。
 * --------------------------------------------------------------------------- */

ines_cstr_t ines_utf8_to_ines(ines_str_t pDst, ines_size_t nDst, const char* pUtf8)
{
	if((pDst == NULL) || (nDst == 0))
		return NULL;      /* 缓冲非法: 调用方保证不为 NULL, 这里只做兜底 */

	pDst[0] = 0;

	if(pUtf8 == NULL)
		return pDst;

#if defined(WIN32) && defined(_UNICODE)
	/* 源串以 '\0' 结尾(-1), 由 API 一并转换终止符; 返回 0 表示失败(含目标缓冲不足) */
	if(MultiByteToWideChar(CP_UTF8, 0, pUtf8, -1, pDst, (int)(nDst - 1)) <= 0)
		pDst[0] = 0;
#else
	ines_strncpy(pDst, pUtf8, nDst - 1);
#endif

	pDst[nDst - 1] = 0;   /* 转换/拷贝都可能刚好填满, 统一封尾 */

	return pDst;
}


void ines_log_r(ines_log_level_t level, ines_log_module_t module, ines_cstr_t strFmt, ...)
{
	va_list vl;
	static ines_char_t  szLogFileName[4096] = {0};
	static ines_char_t  szNewFileName[4096] = {0};
	static struct _stat   st;

	static FILE* pfLog = NULL;
	static int lines = 0;
	static ines_bool_t  path_failed = ines_false;  /* 所有候选路径都打不开时置位, 避免每次写日志都重试 */

	ines_char_t   szPerfix[128];

	if(pfLog == NULL && !path_failed)
	{
		if(szLogFileName[0] == 0)
		{
			ines_default_log_path(szLogFileName, count_of(szLogFileName));
		}
		if(0 == _tstat(szLogFileName, &st))
		{
			//if(st.st_size > 10*1024*1024)
			{
				ines_snprintf(szNewFileName, count_of(szNewFileName), ISTR("%s.%d"), szLogFileName, (unsigned int)time(NULL));
				_trename(szLogFileName, szNewFileName);
			}
		}
		pfLog = _tfopen(szLogFileName, ISTR("a+"));
		if(pfLog == NULL)
		{
			/* exe 所在目录不可写(如进程运行在系统目录): 回退到临时目录 */
			ines_size_t  n = (ines_size_t)GetTempPath((DWORD)count_of(szLogFileName), szLogFileName);
			if(n > 0 && n + 8 < count_of(szLogFileName))
			{
				ines_strcpy(szLogFileName + n, ISTR("iNES.log"));
				pfLog = _tfopen(szLogFileName, ISTR("a+"));
			}
			if(pfLog == NULL)
			{
				path_failed = ines_true;
			}
		}
	}

	lines++;
	if(lines >= 1000)
	{
		lines = 0;
		if(pfLog)
		{
			if( ftell(pfLog) >= 10*1024*1024)
			{
				fclose(pfLog);
				ines_snprintf(szNewFileName, count_of(szNewFileName), ISTR("%s.%d"), szLogFileName, (unsigned int)time(NULL));
				_trename(szLogFileName, szNewFileName);
				pfLog = _tfopen(szLogFileName, ISTR("a+"));
			}
		}
	}

	if(pfLog)
	{
		ines_char_t   szLine[LOG_LINE_CHARS];

		ines_log_perfix(szPerfix, level);
		va_start(vl, strFmt);
		ines_vsnprintf(szLine, count_of(szLine), strFmt, vl);
		va_end(vl);
		szLine[count_of(szLine) - 1] = 0;   /* _vsntprintf 在输出被截断时不一定补 '\0' */

		log_write_text(pfLog, szPerfix);
		log_write_text(pfLog, szLine);
		fflush(pfLog);
	}

	if(level >= LOG_WAR)
	{
		va_start(vl, strFmt);
		add_last_error_log(level, strFmt, vl);
		va_end(vl);
	}
}


ines_cstr_t ines_get_ctime_u()
{
	static  ines_char_t szTime[256];
	struct timeval  tv;
	time_t          tsec;
	struct tm*      tm;
#ifdef WIN32
	struct _timeb timebuffer;
#endif

#ifdef WIN32
	_ftime( &timebuffer );
	tv.tv_sec  = (long)timebuffer.time;
	tv.tv_usec = timebuffer.millitm;
#define MTRP   ISTR("%03d")   // 支持毫秒级的 注意这里的tv_usec 是毫秒,不是微秒
#elif defined(INES_POSIX)
	gettimeofday(&tv, NULL);
#define MTRP   ISTR("%06ld")    // 支持微秒级
#endif
	tsec = tv.tv_sec;
	tm = localtime(&tsec);

	if(tm)
	{
		ines_snprintf(szTime, count_of(szTime), ISTR("%04d-%02d-%02d %02d:%02d:%02d.") MTRP ISTR(" "), 
			tm->tm_year+1970, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, (long)tv.tv_usec);
	}
	return  szTime;
}


void ines_log(ines_log_level_t level, ines_log_module_t module, ines_cstr_t strFmt, ...)
{
	va_list vl;
	ines_char_t     strBuffer[INES_MAX_PATH];
	ines_size_t     len;

	va_start(vl, strFmt);

	len = 0;
	
	switch(level)
	{
	case LOG_TRA: strBuffer[len++] = 'T'; break;
	case LOG_DBG: strBuffer[len++] = 'D'; break;
	case LOG_INF: strBuffer[len++] = 'I'; break;
	case LOG_WAR: strBuffer[len++] = 'W'; break;
	case LOG_ERR: strBuffer[len++] = 'E'; break;
	case LOG_FAU: strBuffer[len++] = 'F'; break;
	case LOG_NTY: strBuffer[len++] = 'N'; break;
	default:      strBuffer[len++] = '?'; break;
	}
	// add space
	strBuffer[len++] = ' ';

	// add date/time
	len += ines_snprintf(strBuffer+len, count_of(strBuffer)-len, ISTR("%s"), ines_get_ctime_u()); 

	if(module)
	{
		len += ines_snprintf(strBuffer+len, count_of(strBuffer)-len, ISTR("%s"), module);
	}

	// log text (留一个字符)
	len += ines_vsnprintf(strBuffer+len, count_of(strBuffer)-len-1, strFmt, vl);


	va_end(vl);

	// add new line;

	if(strBuffer[len-1] != '\n')
	{
		strBuffer[len++] = '\n';
		strBuffer[len] = '\0';
	}

#ifdef _DEBUG
#ifdef _WIN32
	if(IsDebuggerPresent())
	{
		OutputDebugString(strBuffer);
	}
#endif
#endif
	// write to file
	{
		static FILE* pfLog = NULL;
		static int   lines = 0;
		static ines_char_t  szLogFile[INES_MAX_PATH] = {0};
		ines_char_t  szNewFileName[INES_MAX_PATH];
		struct _stat   st;

		if(pfLog == NULL)
		{
#ifdef WIN32
			// Windows 保持原有行为: 相对当前工作目录
			ines_strncpy(szLogFile, ISTR("iNES.log"), count_of(szLogFile));
#else
			// POSIX: 从桌面/Finder 启动时工作目录为 "/" 不可写, 改用绝对路径
			ines_default_log_path(szLogFile, count_of(szLogFile));
#endif
			if(0 == _tstat(szLogFile, &st))
			{
				if(st.st_size > 10*1024*1024)
				{
					time_t  t = time(NULL);
					ines_snprintf(szNewFileName, count_of(szNewFileName), ISTR("%s.%d"), szLogFile, (unsigned int)t);
					_trename(szLogFile, szNewFileName);
				}
			}
			pfLog = _tfopen(szLogFile, ISTR("a+"));
		}

		lines++;
		if(lines >= 1000)
		{
			lines = 0;
			if(pfLog)
			{
				if(0 == _tstat(szLogFile, &st))
				{
					if(st.st_size > 10*1024*1024)
					{
						time_t  t = time(NULL);
						fclose(pfLog);
						ines_snprintf(szNewFileName, count_of(szNewFileName), ISTR("%s.%d"), szLogFile, (unsigned int)t);
						_trename(szLogFile, szNewFileName);
						pfLog = _tfopen(szLogFile, ISTR("a+"));
					}
				}
			}
		}

		if(pfLog)
		{
			log_write_text(pfLog, strBuffer);
			fflush(pfLog);
		}
	}
}

