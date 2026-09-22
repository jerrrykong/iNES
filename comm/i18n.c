/*
 * i18n —— 两端共用的界面文本映射(纯 C, 不碰 UI 框架)
 *
 * 设计要点见 docs/i18n-plan.md, 摘要:
 *   - 英文是内置编译期表(comm/i18n_en.c), 是唯一真源; 语言文件只做覆盖。
 *   - 语言文件为 UTF-8 INI(无 BOM / LF), 放在 <数据目录>/lang(优先) 或前端追加的目录
 *     (mac 的 bundle Resources/lang)。
 *   - 系统语言标签由前端取得后传入(本层不碰系统 API), 匹配失败落在 en。
 *   - 返回 ines_cstr_t: win32 为 UTF-16, POSIX 为 UTF-8, 前端直接用。
 */

#include "i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifdef WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <strings.h>
#endif

#include "log.h"


/* win32 下 ines_char_t 为 wchar_t, 日志格式串里 char* 要用 %S */
#ifdef WIN32
#define I18N_FMT_S   ISTR("%S")
#define I18N_FMT_KEY ISTR("%S.%S")     /* key 是 char*, UNICODE 下要用 %S */
#else
#define I18N_FMT_S   ISTR("%s")
#define I18N_FMT_KEY ISTR("%s.%s")
#endif


/* 扫描到的语言文件 */
typedef struct _i18n_file_
{
	char         id[INES_I18N_ID_MAX];       /* ASCII, 来自 [meta] id */
	char         name[INES_I18N_NAME_MAX];   /* UTF-8, 来自 [meta] name */
	ines_char_t  path[INES_MAX_PATH];
} i18n_file_t;


static ines_char_t**  s_text       = NULL;   /* 当前语言文本(与内置表同序) */
static int            s_text_count = 0;
static int            s_inited     = 0;

static i18n_file_t    s_files[INES_I18N_LANG_MAX];
static int            s_file_count = 0;

static char           s_cur_id[INES_I18N_ID_MAX]  = "en";
static ines_char_t    s_dirs[INES_I18N_DIR_MAX][INES_MAX_PATH];
static int            s_dir_count = 0;

static ines_char_t    s_unknown_buf[INES_I18N_KEY_MAX];   /* 未知 key 的回退文本 */


/* ---------- 基础工具 ---------- */

#ifndef count_of
#define count_of(a)   ((int)(sizeof(a) / sizeof((a)[0])))
#endif

/** UTF-8 串转 ines_char_t 串(堆分配); 失败返回 NULL */
static ines_char_t* i18n_dup_utf8(const char* utf8)
{
	ines_char_t* out;

	if (utf8 == NULL)
	{
		return NULL;
	}

#ifdef WIN32
	{
		int need = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);

		if (need <= 0)
		{
			return NULL;
		}

		out = (ines_char_t*)malloc((size_t)need * sizeof(ines_char_t));
		if (out == NULL)
		{
			return NULL;
		}

		MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, need);
	}
#else
	out = (ines_char_t*)malloc(strlen(utf8) + 1);
	if (out == NULL)
	{
		return NULL;
	}

	memcpy(out, utf8, strlen(utf8) + 1);
#endif

	return out;
}

/** 以二进制读取整个文件(UTF-8 字节流) */
static char* i18n_read_all(ines_cstr_t path, ines_size_t* out_len)
{
	FILE*        fp;
	char*        buf = NULL;
	ines_size_t  cap = 4096;
	ines_size_t  len = 0;
	ines_size_t  got;

#ifdef WIN32
	fp = _wfopen(path, L"rb");
#else
	fp = fopen(path, "rb");
#endif
	if (fp == NULL)
	{
		return NULL;
	}

	buf = (char*)malloc(cap);
	if (buf == NULL)
	{
		fclose(fp);
		return NULL;
	}

	for (;;)
	{
		if (len + 1024 + 1 > cap)
		{
			char* tmp;

			cap  = cap * 2;
			tmp  = (char*)realloc(buf, cap);
			if (tmp == NULL)
			{
				free(buf);
				fclose(fp);
				return NULL;
			}
			buf = tmp;
		}

		got = (ines_size_t)fread(buf + len, 1, 1024, fp);
		len += got;
		if (got < 1024)
		{
			break;
		}
	}

	fclose(fp);
	buf[len] = '\0';
	if (out_len != NULL)
	{
		*out_len = len;
	}

	return buf;
}

static int i18n_is_blank(int c)
{
	return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

/** 去掉首尾空白, 返回新起点(原地截断) */
static char* i18n_trim(char* s)
{
	char* end;

	while (s != NULL && *s != '\0' && i18n_is_blank((unsigned char)*s))
	{
		s++;
	}

	end = (s == NULL) ? NULL : (s + strlen(s) - 1);
	while (end != NULL && end >= s && i18n_is_blank((unsigned char)*end))
	{
		*end = '\0';
		end--;
	}

	return s;
}

/** 就地解析转义: \\n \\t \\\\ \\" ; 返回长度 */
static void i18n_unescape(char* s)
{
	char* w = s;
	char* r = s;

	if (s == NULL)
	{
		return;
	}

	while (*r != '\0')
	{
		if (*r == '\\' && *(r + 1) != '\0')
		{
			switch (*(r + 1))
			{
			case 'n':  *w = '\n'; break;
			case 't':  *w = '\t'; break;
			case 'r':  *w = '\r'; break;
			case '\\': *w = '\\'; break;
			case '"':  *w = '"';  break;
			default:   *w = *(r + 1); break;
			}

			r += 2;
			w++;
			continue;
		}

		*w = *r;
		w++;
		r++;
	}

	*w = '\0';
}

/** 语言 ID 合法性: 只允许 [A-Za-z0-9_-] */
static int i18n_id_valid(const char* id)
{
	const char* p;

	if (id == NULL || id[0] == '\0' || (int)strlen(id) >= INES_I18N_ID_MAX)
	{
		return 0;
	}

	for (p = id; *p != '\0'; p++)
	{
		int c = (unsigned char)*p;

		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '-' || c == '_')
		{
			continue;
		}

		return 0;
	}

	return 1;
}

static int i18n_id_equal(const char* a, const char* b)
{
#ifdef WIN32
	return (_stricmp(a, b) == 0);
#else
	return (strcasecmp(a, b) == 0);
#endif
}

/** 在内置表里定位 key(线性查找, 条目数 ~180, 仅 UI 路径调用) */
static int i18n_find_key(const char* key)
{
	int i;

	if (key == NULL)
	{
		return -1;
	}

	for (i = 0; i < g_ines_i18n_en_count; i++)
	{
		if (strcmp(g_ines_i18n_en[i].key, key) == 0)
		{
			return i;
		}
	}

	return -1;
}


/* ---------- 内置表 -> 当前文本 ---------- */

static void i18n_fill_english(void)
{
	int i;

	for (i = 0; i < s_text_count; i++)
	{
		if (s_text[i] != NULL)
		{
			free(s_text[i]);
			s_text[i] = NULL;
		}

		s_text[i] = i18n_dup_utf8(g_ines_i18n_en[i].text);
	}
}

static void i18n_release_text(void)
{
	int i;

	for (i = 0; i < s_text_count; i++)
	{
		if (s_text[i] != NULL)
		{
			free(s_text[i]);
			s_text[i] = NULL;
		}
	}
}


/* ---------- 语言文件解析 ---------- */

/** 只解析 [meta] 段, 用于扫描目录时取 id / name */
static int i18n_parse_meta(const char* data, char* id, char* name)
{
	const char* line = data;
	int         in_meta = 0;
	int         has_id  = 0;
	int         has_name = 0;
	char        buf[512];
	const char* eol;
	int         n;
	char*       cur;
	char*       close;

	while (line != NULL && *line != '\0')
	{
		eol = strchr(line, '\n');
		n   = (eol == NULL) ? (int)strlen(line) : (int)(eol - line);

		if (n >= (int)sizeof(buf))
		{
			n = (int)sizeof(buf) - 1;
		}
		memcpy(buf, line, (size_t)n);
		buf[n] = '\0';

		cur = i18n_trim(buf);

		if (cur[0] == '\0' || cur[0] == ';' || cur[0] == '#')
		{
			line = (eol == NULL) ? NULL : (eol + 1);
			continue;
		}

		if (cur[0] == '[')
		{
			close = strchr(cur, ']');

			if (close != NULL)
			{
				*close = '\0';
				in_meta = (strcasecmp(cur + 1, "meta") == 0);
				if (!in_meta && has_id)
				{
					break;      /* meta 段已结束 */
				}
			}

			line = (eol == NULL) ? NULL : (eol + 1);
			continue;
		}

		if (in_meta)
		{
			char* eq = strchr(cur, '=');
			char* k;
			char* v;

			if (eq != NULL)
			{
				*eq = '\0';
				k = i18n_trim(cur);
				v = i18n_trim(eq + 1);

				if (strcasecmp(k, "id") == 0)
				{
					size_t  vlen = strlen(v);

					if (vlen >= 2 && v[0] == '"' && v[vlen - 1] == '"')
					{
						v[vlen - 1] = '\0';
						v++;
					}

					if (i18n_id_valid(v))
					{
						strncpy(id, v, INES_I18N_ID_MAX - 1);
						id[INES_I18N_ID_MAX - 1] = '\0';
						has_id = 1;
					}
				}
				else if (strcasecmp(k, "name") == 0)
				{
					size_t vlen = strlen(v);

					if (vlen >= 2 && v[0] == '"' && v[vlen - 1] == '"')
					{
						v[vlen - 1] = '\0';
						v++;
					}
					i18n_unescape(v);
					strncpy(name, v, INES_I18N_NAME_MAX - 1);
					name[INES_I18N_NAME_MAX - 1] = '\0';
					has_name = 1;
				}
			}
		}

		line = (eol == NULL) ? NULL : (eol + 1);
	}

	if (!has_id)
	{
		return 0;
	}

	if (!has_name || name[0] == '\0')
	{
		strncpy(name, id, INES_I18N_NAME_MAX - 1);
		name[INES_I18N_NAME_MAX - 1] = '\0';
	}

	return 1;
}

/** 用语言文件覆盖当前文本(未收录的 key 忽略) */
static int i18n_apply_file(ines_cstr_t path)
{
	char*       data;
	ines_size_t len = 0;
	const char* line;
	char        section[64];
	char        buf[4096];
	int         applied = 0;
	int         skipped = 0;
	int         warned_crlf = 0;

	data = i18n_read_all(path, &len);
	if (data == NULL)
	{
		INES_LOG(LOG_WAR, MOD_SYS, ISTR("i18n: cannot read language file\n"));
		return 0;
	}

	if (len >= 3 && (unsigned char)data[0] == 0xEF &&
		(unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF)
	{
		INES_LOG(LOG_WAR, MOD_SYS, ISTR("i18n: language file has a UTF-8 BOM (should be no BOM)\n"));
		line = data + 3;
	}
	else
	{
		line = data;
	}

	section[0] = '\0';

	while (line != NULL && *line != '\0')
	{
		const char* eol = strchr(line, '\n');
		int         n   = (eol == NULL) ? (int)strlen(line) : (int)(eol - line);
		char*       cur;
		char*       eq;
		char*       k;
	char*       v;
		char        full[INES_I18N_KEY_MAX + 64];
		int         idx;

		if (n > 0 && line[n - 1] == '\r' && !warned_crlf)
		{
			INES_LOG(LOG_WAR, MOD_SYS, ISTR("i18n: language file uses CRLF line endings (should be LF)\n"));
			warned_crlf = 1;
		}

		if (n >= (int)sizeof(buf))
		{
			n = (int)sizeof(buf) - 1;
		}
		memcpy(buf, line, (size_t)n);
		buf[n] = '\0';
		cur = i18n_trim(buf);

		if (cur[0] != '\0' && cur[0] != ';' && cur[0] != '#')
		{
			if (cur[0] == '[')
			{
				char* close = strchr(cur, ']');

				if (close != NULL)
				{
					*close = '\0';
					strncpy(section, i18n_trim(cur + 1), sizeof(section) - 1);
					section[sizeof(section) - 1] = '\0';
				}
			}
			else if ((eq = strchr(cur, '=')) != NULL)
			{
				*eq = '\0';
				k = i18n_trim(cur);
				v = i18n_trim(eq + 1);

				/* 去掉成对引号并解析转义 */
				{
					size_t vlen = strlen(v);

					if (vlen >= 2 && v[0] == '"' && v[vlen - 1] == '"')
					{
						v[vlen - 1] = '\0';
						v++;
					}
				}
				i18n_unescape(v);

				if (strcasecmp(section, "meta") == 0)
				{
					line = (eol == NULL) ? NULL : (eol + 1);
					continue;
				}

				if (section[0] == '\0')
				{
					strncpy(full, k, sizeof(full) - 1);
				}
				else
				{
					ines_snprintf(full, sizeof(full), I18N_FMT_KEY, section, k);
				}
				full[sizeof(full) - 1] = '\0';

				idx = i18n_find_key(full);
				if (idx >= 0)
				{
					ines_char_t* dup = i18n_dup_utf8(v);

					if (dup != NULL)
					{
						if (s_text[idx] != NULL)
						{
							free(s_text[idx]);
						}
						s_text[idx] = dup;
						applied++;
					}
				}
				else
				{
					skipped++;
				}
			}
		}

		line = (eol == NULL) ? NULL : (eol + 1);
	}

	free(data);

	if (skipped > 0)
	{
		INES_LOG(LOG_WAR, MOD_SYS, ISTR("i18n: %d unknown key(s) ignored\n"), skipped);
	}
	INES_LOG(LOG_INF, MOD_SYS, ISTR("i18n: applied %d string(s)\n"), applied);

	return applied;
}


/* ---------- 目录扫描 ---------- */

static void i18n_register_file(ines_cstr_t path)
{
	char*       data;
	char        id[INES_I18N_ID_MAX];
	char        name[INES_I18N_NAME_MAX];
	int         i;

	if (s_file_count >= INES_I18N_LANG_MAX)
	{
		return;
	}

	data = i18n_read_all(path, NULL);
	if (data == NULL)
	{
		return;
	}

	id[0]   = '\0';
	name[0] = '\0';
	if (!i18n_parse_meta(data, id, name))
	{
		free(data);
		/* lang/en.ini 是模板(英文内置), 没有 [meta] 属正常 */
		INES_LOG(LOG_INF, MOD_SYS, ISTR("i18n: no [meta] id in ") ISTR("%s") ISTR(", skipped (template?)\n"), path);
		return;
	}
	free(data);

	if (i18n_id_equal(id, "en"))
	{
		INES_LOG(LOG_WAR, MOD_SYS, ISTR("i18n: external file must not declare id=en, skipped\n"));
		return;
	}

	for (i = 0; i < s_file_count; i++)
	{
		if (i18n_id_equal(s_files[i].id, id))
		{
			return;      /* 先扫描到的目录优先 */
		}
	}

	strncpy(s_files[s_file_count].id, id, INES_I18N_ID_MAX - 1);
	s_files[s_file_count].id[INES_I18N_ID_MAX - 1] = '\0';
	strncpy(s_files[s_file_count].name, name, INES_I18N_NAME_MAX - 1);
	s_files[s_file_count].name[INES_I18N_NAME_MAX - 1] = '\0';
	ines_strncpy(s_files[s_file_count].path, path, INES_MAX_PATH - 1);
	s_files[s_file_count].path[INES_MAX_PATH - 1] = '\0';
	s_file_count++;

	INES_LOG(LOG_INF, MOD_SYS, ISTR("i18n: language ") I18N_FMT_S ISTR(" (" ) I18N_FMT_S ISTR(") found\n"), id, name);
}

static void i18n_scan_dir(ines_cstr_t dir)
{
#ifdef WIN32
	{
		ines_char_t      pattern[INES_MAX_PATH];
		WIN32_FIND_DATA  fd;
		HANDLE           h;

		ines_snprintf(pattern, INES_MAX_PATH, ISTR("%s\\*.ini"), dir);
		h = FindFirstFile(pattern, &fd);
		if (h == INVALID_HANDLE_VALUE)
		{
			return;
		}

		do
		{
			ines_char_t path[INES_MAX_PATH];

			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				continue;
			}

			ines_snprintf(path, INES_MAX_PATH, ISTR("%s\\%s"), dir, fd.cFileName);
			i18n_register_file(path);
		}
		while (FindNextFile(h, &fd));

		FindClose(h);
	}
#else
	{
		DIR*           d;
		struct dirent* e;

		d = opendir(dir);
		if (d == NULL)
		{
			return;
		}

		while ((e = readdir(d)) != NULL)
		{
			ines_char_t path[INES_MAX_PATH];
			size_t      len = strlen(e->d_name);

			if (len < 5 || strcasecmp(e->d_name + len - 4, ".ini") != 0)
			{
				continue;
			}

			ines_snprintf(path, INES_MAX_PATH, "%s/%s", dir, e->d_name);
			i18n_register_file(path);
		}

		closedir(d);
	}
#endif
}

static void i18n_scan_all(void)
{
	ines_char_t  data_dir[INES_MAX_PATH];
	ines_char_t  lang_dir[INES_MAX_PATH];
	int          i;

	s_file_count = 0;

	data_dir[0] = '\0';
	ines_get_data_dir(data_dir, INES_MAX_PATH);
	if (data_dir[0] != '\0')
	{
		ines_snprintf(lang_dir, INES_MAX_PATH, ISTR("%s/lang"), data_dir);
		i18n_scan_dir(lang_dir);
	}

	for (i = 0; i < s_dir_count; i++)
	{
		i18n_scan_dir(s_dirs[i]);
	}
}


/* ---------- 对外接口 ---------- */

void ines_i18n_add_lang_dir(const char* dir)
{
	ines_char_t* w;

	if (dir == NULL || dir[0] == '\0' || s_dir_count >= INES_I18N_DIR_MAX)
	{
		return;
	}

	w = i18n_dup_utf8(dir);
	if (w == NULL)
	{
		return;
	}

	ines_strncpy(s_dirs[s_dir_count], w, INES_MAX_PATH - 1);
	s_dirs[s_dir_count][INES_MAX_PATH - 1] = '\0';
	s_dir_count++;
	free(w);
}

int ines_i18n_init(const char* preferred)
{
	const char* id;

	if (s_inited)
	{
		i18n_release_text();
		free(s_text);
		s_text = NULL;
		s_inited = 0;
	}

	s_text_count = g_ines_i18n_en_count;
	s_text = (ines_char_t**)malloc((size_t)s_text_count * sizeof(ines_char_t*));
	if (s_text == NULL)
	{
		INES_LOG(LOG_ERR, MOD_SYS, ISTR("i18n: out of memory\n"));
		return -1;
	}
	memset(s_text, 0, (size_t)s_text_count * sizeof(ines_char_t*));
	s_inited = 1;

	i18n_fill_english();
	i18n_scan_all();

	id = ines_i18n_match(preferred);
	if (id[0] != '\0')
	{
		ines_i18n_set_language(id);
	}

	INES_LOG(LOG_INF, MOD_SYS, ISTR("i18n: language = ") I18N_FMT_S ISTR("\n"), s_cur_id);

	return 0;
}

void ines_i18n_fini(void)
{
	if (!s_inited)
	{
		return;
	}

	i18n_release_text();
	free(s_text);
	s_text = NULL;
	s_text_count = 0;
	s_inited = 0;
	s_file_count = 0;
	s_dir_count = 0;
	strncpy(s_cur_id, "en", sizeof(s_cur_id) - 1);
}

int ines_i18n_enum(ines_i18n_lang_t* langs, int max_count)
{
	int  total = s_file_count + 1;      /* 内置 en + 外部文件 */
	int  i;
	int  n = 0;

	if (langs != NULL && max_count > 0)
	{
		if (n < max_count)
		{
			ines_char_t* w = i18n_dup_utf8("en");

			ines_strncpy(langs[n].id, (w != NULL) ? w : ISTR("en"), INES_I18N_ID_MAX - 1);
			langs[n].id[INES_I18N_ID_MAX - 1] = '\0';
			if (w != NULL)
			{
				free(w);
			}

			w = i18n_dup_utf8("English");
			ines_strncpy(langs[n].name, (w != NULL) ? w : ISTR("English"), INES_I18N_NAME_MAX - 1);
			langs[n].name[INES_I18N_NAME_MAX - 1] = '\0';
			if (w != NULL)
			{
				free(w);
			}

			langs[n].builtin = 1;
		}
		n++;

		for (i = 0; i < s_file_count && n < max_count; i++, n++)
		{
			ines_char_t* w = i18n_dup_utf8(s_files[i].id);

			ines_strncpy(langs[n].id, (w != NULL) ? w : ISTR(""), INES_I18N_ID_MAX - 1);
			langs[n].id[INES_I18N_ID_MAX - 1] = '\0';
			if (w != NULL)
			{
				free(w);
			}

			w = i18n_dup_utf8(s_files[i].name);
			ines_strncpy(langs[n].name, (w != NULL) ? w : ISTR(""), INES_I18N_NAME_MAX - 1);
			langs[n].name[INES_I18N_NAME_MAX - 1] = '\0';
			if (w != NULL)
			{
				free(w);
			}

			langs[n].builtin = 0;
		}
	}

	return total;
}

static const i18n_file_t* i18n_find_file(const char* id);   /* 定义在下方 */

int ines_i18n_rescan(void)
{
	char                cur[INES_I18N_ID_MAX];
	const i18n_file_t*  file;

	if (!s_inited)
	{
		return -1;
	}

	strncpy(cur, s_cur_id, sizeof(cur) - 1);
	cur[sizeof(cur) - 1] = '\0';

	i18n_scan_all();

	/* 全部回到英文, 再由当前语言的文件覆盖(文件里缺的 key 自动回退英文) */
	i18n_fill_english();

	if (!i18n_id_equal(cur, "en"))
	{
		file = i18n_find_file(cur);
		if (file != NULL)
		{
			i18n_apply_file(file->path);
		}
		else
		{
			/* 当前语言的文件被删掉了: 回落英文, 由前端决定是否重新选择 */
			INES_LOG(LOG_WAR, MOD_SYS, ISTR("i18n: language ") I18N_FMT_S ISTR(" gone after rescan, falling back to en\n"), cur);
			strncpy(s_cur_id, "en", sizeof(s_cur_id) - 1);
			s_cur_id[sizeof(s_cur_id) - 1] = '\0';
		}
	}

	INES_LOG(LOG_INF, MOD_SYS, ISTR("i18n: rescan done, ") ISTR("%d") ISTR(" language(s) available, current = ") I18N_FMT_S ISTR("\n"),
			 s_file_count + 1, s_cur_id);

	return s_file_count + 1;
}

/** 候选比较: 在已注册语言里找 id(大小写不敏感); 找不到返回 NULL */
static const i18n_file_t* i18n_find_file(const char* id)
{
	int i;

	for (i = 0; i < s_file_count; i++)
	{
		if (i18n_id_equal(s_files[i].id, id))
		{
			return &s_files[i];
		}
	}

	return NULL;
}

const char* ines_i18n_match(const char* lang_tag)
{
	static char  result[INES_I18N_ID_MAX];
	char         tag[INES_I18N_ID_MAX * 2];
	char*        parts[6];
	int          nparts = 0;
	char*        p;

	strncpy(result, "en", sizeof(result) - 1);
	result[sizeof(result) - 1] = '\0';

	if (lang_tag == NULL || lang_tag[0] == '\0')
	{
		return result;
	}

	if ((int)strlen(lang_tag) >= (int)sizeof(tag))
	{
		return result;
	}
	strcpy(tag, lang_tag);

	/* 按 '-' 切段 */
	p = tag;
	parts[nparts++] = p;
	while (*p != '\0')
	{
		if (*p == '-')
		{
			*p = '\0';
			if (nparts < (int)(sizeof(parts) / sizeof(parts[0])))
			{
				parts[nparts++] = p + 1;
			}
		}
		p++;
	}

	/*
	 * 依次尝试: zh-Hans-CN -> zh-CN(去 script) -> zh-Hans -> zh -> en
	 * script 段特征: 4 个字母(如 Hans / Hant)
	 */
	{
		char cand[INES_I18N_ID_MAX];
		int  order[4][3] = { { 0, 1, 2 }, { 0, 2, -1 }, { 0, 1, -1 }, { 0, -1, -1 } };
		int  o;

		for (o = 0; o < (int)(sizeof(order) / sizeof(order[0])); o++)
		{
			int  first = 1;
			int  k;

			cand[0] = '\0';
			for (k = 0; k < 3; k++)
			{
				int idx = order[o][k];

				if (idx < 0 || idx >= nparts)
				{
					continue;
				}
				/* 第 2 轮(去 script)只接受 3 段以上的 tag */
				if (o == 1 && idx == 2 && nparts < 3)
				{
					continue;
				}

				if (!first)
				{
					strncat(cand, "-", sizeof(cand) - strlen(cand) - 1);
				}
				strncat(cand, parts[idx], sizeof(cand) - strlen(cand) - 1);
				first = 0;
			}

			if (cand[0] == '\0')
			{
				continue;
			}

			if (i18n_id_equal(cand, "en"))
			{
				strncpy(result, "en", sizeof(result) - 1);
				result[sizeof(result) - 1] = '\0';
				return result;
			}

			if (i18n_find_file(cand) != NULL)
			{
				strncpy(result, cand, sizeof(result) - 1);
				result[sizeof(result) - 1] = '\0';
				return result;
			}
		}
	}

	return result;
}

int ines_i18n_set_language(const char* id)
{
	const i18n_file_t* file;

	if (!s_inited)
	{
		return -1;
	}

	if (id == NULL || id[0] == '\0')
	{
		id = "en";
	}

	if (i18n_id_equal(id, "en"))
	{
		i18n_fill_english();
		strncpy(s_cur_id, "en", sizeof(s_cur_id) - 1);
		s_cur_id[sizeof(s_cur_id) - 1] = '\0';
		return 0;
	}

	file = i18n_find_file(id);
	if (file == NULL)
	{
		INES_LOG(LOG_WAR, MOD_SYS, ISTR("i18n: unknown language ") I18N_FMT_S ISTR("\n"), id);
		return -1;
	}

	/* 全部回到英文, 再由文件覆盖(文件里缺的 key 自动回退英文) */
	i18n_fill_english();
	i18n_apply_file(file->path);

	strncpy(s_cur_id, file->id, sizeof(s_cur_id) - 1);
	s_cur_id[sizeof(s_cur_id) - 1] = '\0';

	return 0;
}

const char* ines_i18n_language(void)
{
	return s_cur_id;
}

ines_cstr_t ines_i18n_text(const char* key)
{
	int idx;

	if (!s_inited)
	{
		return ISTR("");
	}

	idx = i18n_find_key(key);
	if (idx < 0 || s_text[idx] == NULL)
	{
		/* 未知 key: 直接用 key 本身, 便于开发期发现漏登记 */
		ines_char_t* w = i18n_dup_utf8(key);

		if (w != NULL)
		{
			ines_strncpy(s_unknown_buf, w, INES_I18N_KEY_MAX - 1);
			s_unknown_buf[INES_I18N_KEY_MAX - 1] = '\0';
			free(w);
		}
		else
		{
			s_unknown_buf[0] = '\0';
		}

		INES_LOG(LOG_WAR, MOD_SYS, ISTR("i18n: unknown key ") I18N_FMT_S ISTR("\n"), key);

		return s_unknown_buf;
	}

	return s_text[idx];
}

ines_str_t ines_i18n_text_fmt(ines_str_t buf, ines_size_t len, const char* key, ...)
{
	va_list args;

	if (buf == NULL || len <= 0)
	{
		return buf;
	}

	buf[0] = '\0';

	va_start(args, key);
	ines_vsnprintf(buf, len, ines_i18n_text(key), args);
	va_end(args);

	buf[len - 1] = '\0';

	return buf;
}
