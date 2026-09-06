
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"



static void mapper13_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	
	if(p_host->prom_8k_num >= 4)
		ines_set_prom_bank_4(p_host, 0, 1, 2, 3);
	else
		ines_set_prom_bank_4(p_host, 0, 1, 0,1 );



}

static void mapper13_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t  val)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	ines_word_t    bank = (val & 0x3);
	ines_set_vram_bank_n(p_host, 4, bank * 4 + 0);
	ines_set_vram_bank_n(p_host, 5, bank * 4 + 1);
	ines_set_vram_bank_n(p_host, 6, bank * 4 + 2);
	ines_set_vram_bank_n(p_host, 7, bank * 4 + 3);
}

ines_bool_t  mapper13_create(ines_mapper_t* p_mapper)
{
	p_mapper->reset = mapper13_reset;
	p_mapper->writehigh = mapper13_writehigh;
	return ines_true;
}



