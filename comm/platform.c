#include "idef.h"

#ifdef WIN32
#include <windows.h>
#endif




ines_char_t* get_file_title(ines_char_t* title, ines_cstr_t  file_path)
{
	ines_cstr_t  from, to;

	from = file_path;
	to = NULL;

	while(*file_path)
	{
		if(*file_path == '\\' || *file_path == '/')
		{
			from = file_path+1;
			to = NULL;
		}
		else if(*file_path == '.' && from != NULL && to == NULL)
		{
			to = file_path;
		}
		file_path++;
	}

	if(to == NULL)
		to = file_path;

	while(from < to)
	{
		*title++ = *from++;
	}
	*title = 0;
	
	return title;
}


#ifdef WIN32

ines_cstr_t ines_get_data_dir(ines_str_t szPath, ines_size_t szLen)
{
	ines_str_t  p;

	if(szPath == NULL || szLen < 2)
		return ISTR("");

	// 取可执行文件全路径, 再截掉文件名部分
	if(0 == GetModuleFileName(NULL, szPath, (DWORD)szLen))
	{
		szPath[0] = 0;
		return szPath;
	}
	szPath[szLen - 1] = 0;

	p = szPath + _tcslen(szPath);
	while(p > szPath && *(p - 1) != '\\' && *(p - 1) != '/')
		p--;
	*p = 0;

	return szPath;
}

#elif defined(INES_POSIX)

// 递归创建目录(等价 mkdir -p), 失败时忽略: 目录已存在也会返回非 0
static void ines_mkdir_p(const char* path)
{
	char   buf[INES_MAX_PATH];
	char*  p;
	size_t len;

	if(path == NULL)
		return;

	len = strlen(path);
	if(len == 0 || len >= sizeof(buf))
		return;
	memcpy(buf, path, len + 1);

	for(p = buf + 1; *p; p++)
	{
		if(*p != '/')
			continue;
		*p = 0;
		mkdir(buf, 0755);
		*p = '/';
	}
	mkdir(buf, 0755);
}

ines_cstr_t ines_get_data_dir(ines_str_t szPath, ines_size_t szLen)
{
	const char* home;
	ines_cstr_t sub;
	int         n;

	if(szPath == NULL || szLen < 2)
		return "";

	home = getenv("HOME");
	if(home == NULL || home[0] == 0)
		home = "/tmp";

#if defined(__APPLE__)
	sub = "/Library/Application Support/iNES";
#else
	sub = "/.local/share/iNES";
#endif

	n = snprintf(szPath, szLen, "%s%s", home, sub);
	if(n < 0 || (ines_size_t)n >= szLen)
	{
		szPath[0] = 0;
		return szPath;
	}

	ines_mkdir_p(szPath);

	return szPath;
}

#endif

