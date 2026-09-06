
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"



static void mapper182_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	
	if(p_host->prom_8k_num >= 4)
		ines_set_prom_bank_4(p_host, 0, 1, 2, 3);
	else
		ines_set_prom_bank_4(p_host, 0, 1, 0,1 );
	
	if(p_host->vrom_1k_num > 0)
	{
		ines_set_vrom_bank_8(p_host, 0, 1, 2, 3, 4, 5, 6, 7);
	}
}

static void mapper182_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t  val)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	(void)p_host;
}

ines_bool_t  mapper182_create(ines_mapper_t* p_mapper)
{
	p_mapper->reset = mapper182_reset;
	p_mapper->writehigh = mapper182_writehigh;
	return ines_false;
}



