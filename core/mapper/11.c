
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"



static void mapper11_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	
	ines_set_prom_bank_4(p_host, 0, 1, 2, 3);
	
	if(p_host->vrom_1k_num > 0)
	{
		ines_set_vrom_bank_8(p_host, 0, 1, 2, 3, 4, 5, 6, 7);
	}
}

static void mapper11_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t  val)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	ines_word_t   bank = (val&0x03)<<2;
	ines_word_t   vbank = (val&0xf0)>>1;
	ines_set_prom_bank_4(p_host, bank + 0, bank + 1, bank + 2, bank + 3);
	ines_set_vrom_bank_8(p_host, vbank + 0, vbank + 1, vbank + 2, vbank + 3
								, vbank + 4, vbank + 5, vbank + 6, vbank + 7);
	
}

ines_bool_t  mapper11_create(ines_mapper_t* p_mapper)
{
	p_mapper->reset = mapper11_reset;
	p_mapper->writehigh = mapper11_writehigh;
	return ines_true;
}



