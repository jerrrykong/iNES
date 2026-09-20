/*
 * i18n 语言文件校验工具
 *
 * 编译(不进入主构建目标):
 *   clang -DINES_POSIX -o /tmp/i18n_check tools/i18n_check.c
 * 用法:
 *   /tmp/i18n_check lang/zh-CN.ini lang/ja.ini lang/fr.ini ...
 *
 * 检查项(任一失败则退出码非 0):
 *   1) UTF-8 无 BOM、换行 LF;
 *   2) [meta] 段有 id / name, 且 id 合法(不是 en);
 *   3) key 集与内置英文表一致(缺失会回退英文, 多余会被忽略 —— 都算问题);
 *   4) 占位符序列与英文一致(数量与顺序);
 *   5) 语法: 每行要么是注释/空行/段名, 要么是 key = value。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../comm/i18n.h"
#include "../comm/i18n_en.c"


#define CHECK_BUF   8192


static int g_errors = 0;


static char* trim(char* s)
{
	char* end;

	while (*s == ' ' || *s == '\t' || *s == '\r')
		s++;

	end = s + strlen(s) - 1;
	while (end >= s && (*end == ' ' || *end == '\t' || *end == '\r'))
	{
		*end = '\0';
		end--;
	}

	return s;
}

/** 提取 printf 占位符序列(%s %d %u %ld %lu %02X 等), 跳过 %% */
static void collect_specifiers(const char* text, char* out, size_t out_size)
{
	const char* p = text;
	size_t      n = 0;

	out[0] = '\0';

	while (p != NULL && *p != '\0' && n + 8 < out_size)
	{
		if (*p != '%')
		{
			p++;
			continue;
		}

		if (*(p + 1) == '%')
		{
			p += 2;
			continue;
		}

		/* 复制一个完整的转换说明: % [-+ #0 宽度 .精度 长度] 转换字符 */
		{
			const char* q = p + 1;

			while (*q != '\0' && strchr("-+ #0123456789.", *q) != NULL)
				q++;

			while (*q != '\0' && strchr("hlLqjzt", *q) != NULL)
				q++;

			if (*q == '\0')
				break;

			out[n++] = *q;
			out[n]   = '\0';

			p = q + 1;
		}
	}
}

static int find_key(const char* key)
{
	int i;

	for (i = 0; i < g_ines_i18n_en_count; i++)
	{
		if (strcmp(g_ines_i18n_en[i].key, key) == 0)
			return i;
	}

	return -1;
}

static void check_file(const char* path)
{
	FILE*        fp;
	char         line[CHECK_BUF];
	char         section[64];
	char         full[128];
	char         id[64];
	char         name[128];
	int          has_id   = 0;
	int          has_name = 0;
	int          lineno   = 0;
	int          crlf     = 0;
	int*         seen;
	unsigned char bom[3];
	int          i;
	int          missing = 0;
	int          extra   = 0;

	seen = (int*)calloc((size_t)g_ines_i18n_en_count, sizeof(int));
	if (seen == NULL)
	{
		fprintf(stderr, "i18n_check: out of memory\n");
		exit(2);
	}

	fp = fopen(path, "rb");
	if (fp == NULL)
	{
		printf("FAIL  %s: cannot open\n", path);
		g_errors++;
		free(seen);
		return;
	}

	if (fread(bom, 1, 3, fp) == 3 && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF)
	{
		printf("FAIL  %s: file has a UTF-8 BOM (must be no BOM)\n", path);
		g_errors++;
	}

	rewind(fp);

	section[0] = '\0';
	id[0]      = '\0';
	name[0]    = '\0';

	while (fgets(line, sizeof(line), fp) != NULL)
	{
		char*  cur;
		size_t len = strlen(line);

		lineno++;

		if (len > 0 && line[len - 1] == '\n')
		{
			line[len - 1] = '\0';
			len--;
		}
		if (len > 0 && line[len - 1] == '\r')
		{
			line[len - 1] = '\0';
			crlf = 1;
		}

		cur = trim(line);
		if (cur[0] == '\0' || cur[0] == ';' || cur[0] == '#')
			continue;

		if (cur[0] == '[')
		{
			char* close = strchr(cur, ']');

			if (close == NULL)
			{
				printf("FAIL  %s:%d: malformed section header\n", path, lineno);
				g_errors++;
				continue;
			}

			*close = '\0';
			strncpy(section, trim(cur + 1), sizeof(section) - 1);
			section[sizeof(section) - 1] = '\0';
			continue;
		}

		{
			char* eq = strchr(cur, '=');

			if (eq == NULL)
			{
				printf("FAIL  %s:%d: not a `key = value` line\n", path, lineno);
				g_errors++;
				continue;
			}

			*eq = '\0';
			{
				char* k = trim(cur);
				char* v = trim(eq + 1);
				size_t vlen;

				/* 去成对引号 */
				vlen = strlen(v);
				if (vlen >= 2 && v[0] == '"' && v[vlen - 1] == '"')
				{
					v[vlen - 1] = '\0';
					v++;
				}

				if (strcmp(section, "meta") == 0)
				{
					if (strcmp(k, "id") == 0)
					{
						strncpy(id, v, sizeof(id) - 1);
						id[sizeof(id) - 1] = '\0';
						has_id = 1;
					}
					else if (strcmp(k, "name") == 0)
					{
						strncpy(name, v, sizeof(name) - 1);
						name[sizeof(name) - 1] = '\0';
						has_name = 1;
					}
					else
					{
						printf("FAIL  %s:%d: unknown meta key `%s`\n", path, lineno, k);
						g_errors++;
					}
					continue;
				}

				if (section[0] == '\0')
				{
					printf("FAIL  %s:%d: key `%s` outside any section\n", path, lineno, k);
					g_errors++;
					continue;
				}

				snprintf(full, sizeof(full), "%s.%s", section, k);

				i = find_key(full);
				if (i < 0)
				{
					printf("FAIL  %s:%d: unknown key `%s`\n", path, lineno, full);
					g_errors++;
					extra++;
					continue;
				}

				if (seen[i] != 0)
				{
					printf("FAIL  %s:%d: duplicate key `%s`\n", path, lineno, full);
					g_errors++;
					continue;
				}
				seen[i] = 1;

				{
					char  want[64];
					char  got[64];

					collect_specifiers(g_ines_i18n_en[i].text, want, sizeof(want));
					collect_specifiers(v, got, sizeof(got));

					if (strcmp(want, got) != 0)
					{
						printf("FAIL  %s:%d: `%s` placeholder mismatch (en=`%s`, this=`%s`)\n",
							   path, lineno, full, want, got);
						g_errors++;
					}
				}
			}
		}
	}

	fclose(fp);

	if (crlf)
	{
		printf("FAIL  %s: CRLF line endings (must be LF)\n", path);
		g_errors++;
	}

	if (!has_id || id[0] == '\0')
	{
		printf("FAIL  %s: missing [meta] id\n", path);
		g_errors++;
	}
	else if (strcmp(id, "en") == 0)
	{
		printf("FAIL  %s: [meta] id must not be `en`\n", path);
		g_errors++;
	}

	if (!has_name || name[0] == '\0')
	{
		printf("FAIL  %s: missing [meta] name\n", path);
		g_errors++;
	}

	for (i = 0; i < g_ines_i18n_en_count; i++)
	{
		if (seen[i] == 0)
		{
			printf("WARN  %s: missing key `%s` (falls back to English)\n", path, g_ines_i18n_en[i].key);
			missing++;
		}
	}

	free(seen);

	if (g_errors == 0 && missing == 0 && extra == 0)
		printf("OK    %s (%s / %s)\n", path, id, name);
	else
		printf("----  %s: %d error(s), %d missing, %d extra\n", path, g_errors, missing, extra);
}

int main(int argc, char** argv)
{
	int i;

	if (argc < 2)
	{
		fprintf(stderr, "usage: i18n_check <lang file> ...\n");
		return 2;
	}

	for (i = 1; i < argc; i++)
		check_file(argv[i]);

	printf("i18n_check: %d error(s).\n", g_errors);

	return (g_errors == 0) ? 0 : 1;
}
