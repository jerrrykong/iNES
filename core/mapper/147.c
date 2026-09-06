
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"



static void mapper147_reset(ines_mapper_t* p_mapper)
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

static void mapper147_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t  val)
{
	ines_host_t*  p_host = mapper2host(p_mapper);	(void)p_host;
}

ines_bool_t  mapper147_create(ines_mapper_t* p_mapper)
{
	p_mapper->reset = mapper147_reset;
	p_mapper->writehigh = mapper147_writehigh;
	return ines_false;
}



