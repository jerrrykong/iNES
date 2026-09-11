// ============================================================================
// Mapper 024 -- Konami VRC6a
//   PRG 16K+8K; CHR 8 x 1KB; 镜像; IRQ; 扩展音 3 路(VRC6 引擎, 经 APU 扩展输入槽发声)
//   A0/A1 直接寻址寄存器(0xX000-0xX003); 无 WRAM
// ============================================================================
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"
#include "vrc.h"


ines_bool_t mapper24_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, VRC6_data_t);

	{
		VRC6_data_t* p = mapper2VRC6data(p_mapper);
		p->is_vrc6b  = 0;      // VRC6a: A0/A1 直接寻址
	}

	p_mapper->custom_sram = 1; // VRC6a 不带 WRAM
	p_mapper->fini      = vrc6_fini;
	p_mapper->reset     = vrc6_reset;
	p_mapper->writehigh = vrc6_writehigh;
	p_mapper->hsync     = vrc6_hsync;
	return ines_true;
}
