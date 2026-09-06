
#include "log.h"
//#include "../win32/iNes.h"
#include <sys/stat.h>
#include <time.h>
#include <stdarg.h>
#ifdef WIN32
#include <windows.h>
#include <sys/timeb.h>
#elif defined(linux)
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

void ines_log_r(ines_log_level_t level, ines_log_module_t module, ines_cstr_t strFmt, ...)
{
	va_list vl;
	static ines_char_t  szLogFileName[4096] = {0};
	static ines_char_t  szNewFileName[4096] = {0};
	static struct _stat   st;

	static FILE* pfLog = NULL;
	static int lines = 0;

	ines_char_t   szPerfix[128];

	if(pfLog == NULL)
	{
		if(szLogFileName[0] == 0)
		{
#ifdef WIN32
			ines_char_t*  p;
			GetModuleFileName(NULL, szLogFileName, count_of(szLogFileName));
			p = szLogFileName + _tcslen(szLogFileName);
			while(p>szLogFileName && p[-1] != '\\') p--;
			ines_strcpy(p, ISTR("iNES.log"));
#elif defined linux 
	#ifdef __ANDROID__
			ines_strcpy(szLogFileName, ISTR("/sdcard/iNES.log"));
	#else
			ines_strcpy(szLogFileName, ISTR("/tmp/iNES.log"));
	#endif
#endif
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
		ines_log_perfix(szPerfix, level);
		va_start(vl, strFmt);
		_fputts(szPerfix, pfLog);
		_vftprintf(pfLog, strFmt, vl);
		va_end(vl);
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
#elif defined linux
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

		if(pfLog == NULL)
		{
			ines_char_t  szNewFileName[INES_MAX_PATH];

			struct _stat   st;
			if(0 == _tstat(ISTR("iNES.log"), &st))
			{
				if(st.st_size > 10*1024*1024)
				{
					time_t  t = time(NULL);
					ines_snprintf(szNewFileName, count_of(szNewFileName), ISTR("iNES.log.%d"), (unsigned int)t);
					_trename(ISTR("iNES.log"), szNewFileName);
				}
			}
			pfLog = _tfopen(ISTR("iNES.log"), ISTR("a+"));
		}

		lines++;
		if(lines >= 1000)
		{
			lines = 0;
			if(pfLog)
			{
				ines_char_t    szNewFileName[INES_MAX_PATH];
				struct _stat   st;
				if(0 == _tstat(ISTR("iNES.log"), &st))
				{
					if(st.st_size > 10*1024*1024)
					{
						time_t  t = time(NULL);
						fclose(pfLog);
						ines_snprintf(szNewFileName, count_of(szNewFileName), ISTR("iNES.log.%d"), (unsigned int)t);
						_trename(ISTR("iNES.log"), szNewFileName);
						pfLog = _tfopen(ISTR("iNES.log"), ISTR("a+"));
					}
				}
			}
		}

		if(pfLog)
		{
			_fputts(strBuffer, pfLog);
			fflush(pfLog);
		}
	}
}

