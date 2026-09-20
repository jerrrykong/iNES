/*
 * i18n 模板导出工具 —— 从内置英文表(comm/i18n_en.c)生成 lang/en.ini
 *
 * 编译(不进入主构建目标):
 *   clang -DINES_POSIX -o /tmp/gen_i18n_template tools/gen_i18n_template.c
 * 用法:
 *   /tmp/gen_i18n_template [输出文件]        # 缺省 stdout
 *
 * 说明: 英文真源是 comm/i18n_en.c, 改英文后必须重跑本工具并覆盖 lang/en.ini。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../comm/i18n.h"
#include "../comm/i18n_en.c"


/** 把值按 INI 规则转义后输出(值里含 \\ " 换行 制表 时需要) */
static void put_value(FILE* out, const char* text)
{
	const char* p = text;

	fputc('"', out);
	while (p != NULL && *p != '\0')
	{
		switch (*p)
		{
		case '\\': fputs("\\\\", out); break;
		case '"':  fputs("\\\"", out); break;
		case '\n': fputs("\\n", out);  break;
		case '\t': fputs("\\t", out);  break;
		case '\r': fputs("\\r", out);  break;
		default:   fputc(*p, out);     break;
		}
		p++;
	}
	fputc('"', out);
}

int main(int argc, char** argv)
{
	FILE*        out = stdout;
	int          i;
	char         section[64];
	char         last_section[64];

	if (argc > 1)
	{
		out = fopen(argv[1], "wb");
		if (out == NULL)
		{
			fprintf(stderr, "cannot write %s\n", argv[1]);
			return 1;
		}
	}

	fputs("; ==========================================================\n", out);
	fputs("; iNES 界面语言文件 —— 英文模板(由 comm/i18n_en.c 自动生成, 请勿手工编辑)\n", out);
	fputs(";\n", out);
	fputs("; 新增一种语言的步骤:\n", out);
	fputs(";   1) 复制本文件为 <id>.ini(如 ja.ini);\n", out);
	fputs(";   2) 在文件头补上 [meta] 段:  id = \"ja\"   name = \"日本語\";\n", out);
	fputs(";   3) 逐条翻译等号右侧的值, 左侧 key 保持不动;\n", out);
	fputs(";   4) 占位符(%s %d %u %ld %lu %02X …)的数量与顺序必须与英文一致(%% 除外);\n", out);
	fputs(";   5) 存为 UTF-8 无 BOM、换行 LF, 放到 <数据目录>/lang/ 或程序目录/lang/ 下。\n", out);
	fputs(";   6) 跑 tools/i18n_check 校验后提交。\n", out);
	fputs(";\n", out);
	fputs("; 说明: 缺的条目会自动回退英文; 多余的 key 会被忽略并写日志。\n", out);
	fputs("; ==========================================================\n", out);

	last_section[0] = '\0';

	for (i = 0; i < g_ines_i18n_en_count; i++)
	{
		const char* key  = g_ines_i18n_en[i].key;
		const char* text = g_ines_i18n_en[i].text;
		const char* dot  = strchr(key, '.');
		size_t      n;

		if (dot == NULL)
		{
			fprintf(stderr, "bad key (no dot): %s\n", key);
			return 2;
		}

		n = (size_t)(dot - key);
		if (n >= sizeof(section))
			n = sizeof(section) - 1;
		memcpy(section, key, n);
		section[n] = '\0';

		if (strcmp(section, last_section) != 0)
		{
			fprintf(out, "\n[%s]\n", section);
			strncpy(last_section, section, sizeof(last_section) - 1);
			last_section[sizeof(last_section) - 1] = '\0';
		}

		fputs(dot + 1, out);
		fputs(" = ", out);
		put_value(out, text);
		fputc('\n', out);
	}

	if (out != stdout)
		fclose(out);

	fprintf(stderr, "gen_i18n_template: %d string(s) exported.\n", g_ines_i18n_en_count);

	return 0;
}
