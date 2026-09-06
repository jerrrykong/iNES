
// ============================================================================
// Mapper 021 -- Konami VRC4a / VRC4c (含子型号走线变体)
//   PRG 4 x 8KB; CHR 8 x 1KB; 镜像; IRQ 计数器 (参考 FCEUX VRC2And4)
//   寄存器地址线: A1/A6 -> 偏移bit0, A2/A7 -> 偏移bit1 (reg_mask1/2)
// ============================================================================
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"
#include "vrc.h"


ines_bool_t mapper21_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, VRC24_data_t);

	{
		VRC24_data_t* p = mapper2VRC24data(p_mapper);
		p->is_vrc2   = 0;      // VRC4 芯片
		p->reg_mask1 = 0x42;
		p->reg_mask2 = 0x84;
	}

	p_mapper->custom_sram = 0; // 部分 VRC4 卡带带 8K WRAM(电池按头)
	p_mapper->fini      = vrc24_fini;
	p_mapper->reset     = vrc24_reset;
	p_mapper->writehigh = vrc24_writehigh;
	p_mapper->hsync     = vrc24_hsync;
	return ines_true;
}
