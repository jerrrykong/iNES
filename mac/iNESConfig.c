// config.ini 读写(替代 win32 的 GetPrivateProfileString/WritePrivateProfileString)。
//
// 实现要点:
//   * 采用"整文件载入 -> 内存修改 -> 整文件回写"的方式。配置文件只有几十行,
//     这样比按行原地改写简单得多, 且不会破坏用户手工添加的未知配置项。
//   * section/key 比较忽略大小写, 与 Windows 的 PrivateProfile API 一致。
//   * 保留原有键值顺序, 便于用户对比两个平台的配置。
//   * 文件格式(UTF-8/无 BOM, LF 换行)与 win32 平台写出的 config.ini 兼容。

#include "iNESConfig.h"
#include "../comm/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>


#define CONFIG_MAX_ENTRIES   512
#define CONFIG_SEC_LEN       64
#define CONFIG_KEY_LEN       128
#define CONFIG_VAL_LEN       1024
#define CONFIG_RET_BUFFERS   4


typedef struct _config_entry_
{
	char   section[CONFIG_SEC_LEN];
	char   key[CONFIG_KEY_LEN];
	char   value[CONFIG_VAL_LEN];
} config_entry_t;


static config_entry_t  s_entries[CONFIG_MAX_ENTRIES];
static ines_int_t      s_count  = 0;
static char            s_path[INES_MAX_PATH] = {0};
static ines_int_t      s_loaded = 0;

static char            s_ret[CONFIG_RET_BUFFERS][CONFIG_VAL_LEN];
static ines_int_t      s_ret_index = 0;


// 去掉首尾空白(含 CR/LF), 返回新的起始位置
static char* config_trim(char* szText)
{
	char*  pEnd;

	if(szText == NULL)
		return NULL;

	while(*szText == ' ' || *szText == '\t')
		szText++;

	pEnd = szText + strlen(szText);
	while(pEnd > szText && (pEnd[-1] == ' ' || pEnd[-1] == '\t'
						 || pEnd[-1] == '\r' || pEnd[-1] == '\n'))
		pEnd--;
	*pEnd = 0;

	return szText;
}


static void config_copy(char* pDst, ines_size_t szLen, ines_cstr_t szSrc)
{
	if(pDst == NULL || szLen == 0)
		return;

	if(szSrc == NULL)
	{
		pDst[0] = 0;
		return;
	}

	strncpy(pDst, szSrc, szLen - 1);
	pDst[szLen - 1] = 0;
}


static config_entry_t* config_find(ines_cstr_t szSection, ines_cstr_t szKey)
{
	ines_int_t  i;

	if(szSection == NULL || szKey == NULL)
		return NULL;

	for(i = 0; i < s_count; i++)
	{
		if(0 == strcasecmp(s_entries[i].section, szSection)
		&& 0 == strcasecmp(s_entries[i].key, szKey))
			return &s_entries[i];
	}

	return NULL;
}


// 把内存中的配置整体写回文件
static void config_save(void)
{
	FILE*       fp;
	ines_int_t  i;
	ines_cstr_t szLast = NULL;

	if(s_path[0] == 0)
		return;

	fp = fopen(s_path, "w");
	if(fp == NULL)
	{
		INES_LOG(LOG_ERR, MOD_SYS, ISTR("config: open \'%s\' for write Failed!(errno=%d)\n"),
				 s_path, errno);
		return;
	}

	for(i = 0; i < s_count; i++)
	{
		if(szLast == NULL || 0 != strcasecmp(szLast, s_entries[i].section))
		{
			if(i > 0)
				fputc('\n', fp);
			fprintf(fp, "[%s]\n", s_entries[i].section);
			szLast = s_entries[i].section;
		}
		fprintf(fp, "%s=%s\n", s_entries[i].key, s_entries[i].value);
	}

	fclose(fp);
}


static void config_load(void)
{
	FILE*  fp;
	char   szLine[CONFIG_VAL_LEN + CONFIG_KEY_LEN + 8];
	char   szSection[CONFIG_SEC_LEN] = "";

	s_count  = 0;
	s_loaded = 1;

	if(s_path[0] == 0)
		return;

	fp = fopen(s_path, "r");
	if(fp == NULL)
	{
		// 首次运行, 没有配置文件是正常的
		INES_LOG(LOG_NTY, MOD_SYS, ISTR("config: \'%s\' not found, use default settings.\n"), s_path);
		return;
	}

	while(fgets(szLine, sizeof(szLine), fp) != NULL)
	{
		char*  szItem = config_trim(szLine);
		char*  szValue;
		ines_size_t  szSecLen;

		if(szItem == NULL || szItem[0] == 0 || szItem[0] == ';' || szItem[0] == '#')
			continue;

		// [section]
		if(szItem[0] == '[')
		{
			szSecLen = strlen(szItem);
			if(szSecLen < 3 || szItem[szSecLen - 1] != ']')
				continue;
			szItem[szSecLen - 1] = 0;
			config_copy(szSection, sizeof(szSection), config_trim(szItem + 1));
			continue;
		}

		// key=value
		szValue = strchr(szItem, '=');
		if(szValue == NULL)
			continue;
		*szValue = 0;
		szValue  = config_trim(szValue + 1);

		if(s_count >= CONFIG_MAX_ENTRIES)
		{
			INES_LOG(LOG_ERR, MOD_SYS, ISTR("config: too many entries(> %d), \'%s\' ignored.\n"),
					 CONFIG_MAX_ENTRIES, szItem);
			continue;
		}

		config_copy(s_entries[s_count].section, CONFIG_SEC_LEN, szSection);
		config_copy(s_entries[s_count].key,     CONFIG_KEY_LEN, config_trim(szItem));
		config_copy(s_entries[s_count].value,   CONFIG_VAL_LEN, szValue);
		s_count++;
	}

	fclose(fp);
}


void iNES_config_set_file(ines_cstr_t szPath)
{
	config_copy(s_path, sizeof(s_path), szPath);
	s_loaded = 0;
	config_load();
}


ines_cstr_t GetConfigStr(ines_cstr_t szSection, ines_cstr_t szKey, ines_cstr_t szDefault)
{
	config_entry_t*  pEntry;
	char*            szBuf;

	if(!s_loaded)
		config_load();

	szBuf = s_ret[s_ret_index];
	s_ret_index = (s_ret_index + 1) % CONFIG_RET_BUFFERS;
	szBuf[0] = 0;

	pEntry = config_find(szSection, szKey);
	if(pEntry != NULL)
	{
		config_copy(szBuf, CONFIG_VAL_LEN, pEntry->value);
		return szBuf;
	}

	config_copy(szBuf, CONFIG_VAL_LEN, szDefault);

	return szBuf;
}


ines_int_t GetConfigInt(ines_cstr_t szSection, ines_cstr_t szKey, ines_int_t iDefault)
{
	ines_cstr_t  szValue = GetConfigStr(szSection, szKey, NULL);

	if(szValue == NULL || szValue[0] == 0)
		return iDefault;

	return (ines_int_t)strtol(szValue, NULL, 10);
}


void SetConfigStr(ines_cstr_t szSection, ines_cstr_t szKey, ines_cstr_t szValue)
{
	config_entry_t*  pEntry;

	if(szSection == NULL || szKey == NULL)
		return;

	if(!s_loaded)
		config_load();

	pEntry = config_find(szSection, szKey);
	if(pEntry == NULL)
	{
		if(s_count >= CONFIG_MAX_ENTRIES)
		{
			INES_LOG(LOG_ERR, MOD_SYS, ISTR("config: too many entries(> %d), \'%s.%s\' ignored.\n"),
					 CONFIG_MAX_ENTRIES, szSection, szKey);
			return;
		}
		pEntry = &s_entries[s_count];
		config_copy(pEntry->section, CONFIG_SEC_LEN, szSection);
		config_copy(pEntry->key,     CONFIG_KEY_LEN, szKey);
		pEntry->value[0] = 0;
		s_count++;
	}

	config_copy(pEntry->value, CONFIG_VAL_LEN, szValue);

	config_save();
}


void SetConfigInt(ines_cstr_t szSection, ines_cstr_t szKey, ines_int_t iValue)
{
	ines_char_t  szValue[32];

	ines_snprintf(szValue, sizeof(szValue), ISTR("%d"), (int)iValue);
	SetConfigStr(szSection, szKey, szValue);
}
