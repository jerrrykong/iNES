
// ============================================================================
// Mapper 023 -- Konami VRC2b / VRC4f
//   PRG 4 x 8KB; CHR 8 x 1KB; 镜像; IRQ 计数器 (参考 FCEUX VRC2And4)
//   寄存器地址线: A0/A2? -> 统一由 reg_mask1/2 对齐
// ============================================================================
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"
#include "vrc.h"


ines_bool_t mapper23_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, VRC24_data_t);

	{
		VRC24_data_t* p = mapper2VRC24data(p_mapper);
		p->is_vrc2   = 0;      // 走 VRC4 逻辑(VRC4f)
		p->reg_mask1 = 0x15;
		p->reg_mask2 = 0x2A;
	}

	p_mapper->custom_sram = 0;
	p_mapper->fini      = vrc24_fini;
	p_mapper->reset     = vrc24_reset;
	p_mapper->writehigh = vrc24_writehigh;
	p_mapper->hsync     = vrc24_hsync;
	return ines_true;
}
