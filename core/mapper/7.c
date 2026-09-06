
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"



static void mapper7_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	
	
	ines_set_prom_bank_4(p_host, 0,1,2,3);
}

static void mapper7_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t  val)
{
	ines_host_t*  p_host = mapper2host(p_mapper);

	ines_word_t  prg_bank = (ines_word_t)(val & 0x07) << 2;

	ines_set_prom_bank_4(p_host, prg_bank + 0, prg_bank + 1, prg_bank + 2, prg_bank + 3);

	if(0 == (val & 0x10))
	{
		ines_ppu_set_mirror(&p_host->ppu, 0,0,0,0);
	}
	else
	{
		ines_ppu_set_mirror(&p_host->ppu, 1,1,1,1);
	}
}


ines_bool_t  mapper7_create(ines_mapper_t* p_mapper)
{
	p_mapper->reset = mapper7_reset;
	p_mapper->writehigh = mapper7_writehigh;
	return ines_true;
}



