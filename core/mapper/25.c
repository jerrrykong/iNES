// ============================================================================
// Mapper 025 -- Konami VRC2c / VRC4b / VRC4d / VRC4e
//   PRG 4 x 8KB; CHR 8 x 1KB; 镜像; IRQ 计数器 (参考 FCEUX VRC2And4)
//   寄存器地址线由 reg_mask1/2 统一对齐
// ============================================================================
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"
#include "vrc.h"


ines_bool_t mapper25_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, VRC24_data_t);

	{
		VRC24_data_t* p = mapper2VRC24data(p_mapper);
		p->is_vrc2   = 0;      // 走 VRC4 逻辑
		p->reg_mask1 = 0x0A;
		p->reg_mask2 = 0x05;
	}

	p_mapper->custom_sram = 0;
	p_mapper->fini      = vrc24_fini;
	p_mapper->reset     = vrc24_reset;
	p_mapper->writehigh = vrc24_writehigh;
	p_mapper->hsync     = vrc24_hsync;
	return ines_true;
}
