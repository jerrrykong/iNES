// ============================================================================
// Mapper 026 -- Konami VRC6b
//   PRG 16K+8K; CHR 8 x 1KB; 镜像; IRQ; 扩展音 3 路(VRC6 引擎, 经 APU 扩展输入槽发声)
//   A0/A1 交换寻址 (VRC6b); 带 8K WRAM @$6000 (battery 依 iNES 头)
// ============================================================================
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"
#include "vrc.h"


ines_bool_t mapper26_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, VRC6_data_t);

	{
		VRC6_data_t* p = mapper2VRC6data(p_mapper);
		p->is_vrc6b  = 1;      // VRC6b: A0/A1 交换
	}

	p_mapper->custom_sram = 0; // VRC6b 带 8K WRAM
	p_mapper->fini      = vrc6_fini;
	p_mapper->reset     = vrc6_reset;
	p_mapper->writehigh = vrc6_writehigh;
	p_mapper->hsync     = vrc6_hsync;
	return ines_true;
}
