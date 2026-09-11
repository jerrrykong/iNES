
// ============================================================================
// Mapper 022 -- Konami VRC2a
//   PRG 4 x 8KB(末两页固定); CHR 2KB 粒度 (is_vrc2); 无 IRQ; 无 WRAM
//   寄存器地址线: A1 -> 偏移bit0, A0 -> 偏移bit1 (与 VRC4 相反)
// ============================================================================
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"
#include "vrc.h"


ines_bool_t mapper22_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, VRC24_data_t);

	{
		VRC24_data_t* p = mapper2VRC24data(p_mapper);
		p->is_vrc2   = 1;      // VRC2a: CHR 2KB 粒度, 无 IRQ
		p->reg_mask1 = 0x02;
		p->reg_mask2 = 0x01;
	}

	p_mapper->custom_sram = 1; // VRC2a 无 WRAM
	p_mapper->fini      = vrc24_fini;
	p_mapper->reset     = vrc24_reset;
	p_mapper->writehigh = vrc24_writehigh;
	// 无 IRQ 硬件, 不挂 hsync
	return ines_true;
}
