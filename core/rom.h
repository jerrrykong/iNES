#ifndef __ROM_H__
#define __ROM_H__

#include "../comm/idef.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* NES文件结构

+----+--------+------------------------------------------------------------+
|偏移| 字节数 |                     内容                                   |
+----+--------+------------------------------------------------------------+
|0－3|    4   | 字符串“NES^Z”用来识别.NES文件                              |
+----+--------+------------------------------------------------------------+
|  4 |    1   | 16kB ROM的数目                                             |
+----+--------+------------------------------------------------------------+
|  5 |    1   | 8kB VROM的数目                                             |
+----+--------+------------------------------------------------------------+
|  6 |    1   | D0：1＝垂直镜像，0＝水平镜像                               |
|    |        | D1：1＝有电池记忆，SRAM地址$6000-$7FFF                     |
|    |        | D2：1＝在$7000-$71FF有一个512字节的trainer（金手指）       |
|    |        | D3：1＝4屏幕VRAM布局                                       |
|    |        | D4－D7：ROM Mapper的低4位                                  |
+----+--------+------------------------------------------------------------+
|  7 |   1    | D0－D3：保留，必须是0（准备作为副Mapper号^_^）             |
|    |        | D4－D7：ROM Mapper的高4位                                  |
+----+--------+------------------------------------------------------------+
|8－F|   8    | 保留，必须是0                                              |
+----+--------+------------------------------------------------------------+
|16- | 16KxM  | ROM段升序排列，如果存在trainer，它的512字节摆在ROM段之前   |
|    |        | (第二行开始后就是游戏的数据)                               |
+----+--------+------------------------------------------------------------+
|-EOF|  8KxN  | VROM段, 升序排列                                           |
+----+--------+------------------------------------------------------------+

*/

// NES文件头部结构
#pragma pack(push, 1)
struct _ines_file_header_ {
	ines_byte_t   tag[4];
	ines_byte_t   PROM_block_num;
	ines_byte_t   VROM_block_num;
	ines_byte_t   flag1;
	ines_byte_t   flag2;
	ines_byte_t   reserved[8];  // 保留的字节，必需为0
};
#pragma pack(pop)

typedef struct _ines_file_header_   ines_file_header_t;
// NES文件头字节数
#define INES_FILE_HEADER_SIZE    sizeof(ines_file_header_t)
// PROM每块字节数
#define INES_PROM_BLOCK_SIZE     0x4000
// VROM每块字节数
#define INES_VROM_BLOCK_SIZE     0x2000

#define INES_TRAINER_BLOCK_SIZE   0x200

#define INES_FILE_TAG     "NES\x1a"



struct _ines_rom_ {
	ines_byte_t        mapper_num;
	ines_byte_t        mirror_type;
	ines_byte_t        has_sram;
	ines_byte_t        has_trainer;
	ines_byte_t        PROM_block_num;
	ines_byte_t        VROM_block_num;   /* 8KB 块数；文件实际 CHR 大于头声明时按文件修正 */
	ines_byte_t        trainer_data[INES_TRAINER_BLOCK_SIZE];
	ines_byte_t*       pPROMs;
	ines_byte_t*       pVROMs;
	ines_dword_t       crc32_p;
};


typedef struct _ines_rom_   ines_rom_t;



void  ines_rom_init(ines_rom_t*  p_rom);
void  ines_rom_free(ines_rom_t*  p_rom);

void  ines_rom_reset(ines_rom_t*  p_rom);

ines_bool_t ines_rom_load_from_file(ines_rom_t*  p_rom, ines_cstr_t strFileName);



#ifdef __cplusplus
};
#endif





#endif


