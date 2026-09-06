// ============================================================================
// Mapper 085 -- Konami VRC7
//   PRG 3 x 8K 可切换 + 末页固定; CHR 8 x 1KB; 镜像; IRQ
//   FM(YM2413): $9010 地址锁存 / $9030 数据写 (仅捕获寄存器, 待 APU 扩展)
//   WRAM 8K @$6000 (battery 依 iNES 头, 如 Lagrange Point)
//   寄存器间隔 0x10, 由 A4/A12 折叠寻址
// ============================================================================
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"
#include "vrc.h"


ines_bool_t mapper85_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, VRC7_data_t);

	p_mapper->custom_sram = 0; // VRC7 带 8K WRAM
	p_mapper->fini      = vrc7_fini;
	p_mapper->reset     = vrc7_reset;
	p_mapper->writehigh = vrc7_writehigh;
	p_mapper->hsync     = vrc7_hsync;
	return ines_true;
}
