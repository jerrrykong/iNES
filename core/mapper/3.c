
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"

static void mapper3_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	ines_word_t   prom_8k_num = p_host->prom_8k_num;

	if(prom_8k_num > 2)
	{
		ines_set_prom_bank_4(p_host, 0,1,2,3);
	}
	else
	{
		ines_set_prom_bank_4(p_host, 0,1,0,1);
	}

	ines_set_vrom_bank_8(p_host, 0,1,2,3,4,5,6,7);
}

static void mapper3_writehigh(ines_mapper_t* p_mapper, ines_word_t  addr, ines_byte_t val)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	ines_word_t   vrom_1k_num = p_host->vrom_1k_num;
	ines_word_t   base;

	val &= ((vrom_1k_num>>1)-1);
	
	base = (ines_word_t)val << 3;

	ines_set_vrom_bank_8(p_host, base+0,base+1,base+2,base+3,base+4,base+5,base+6,base+7);
}


ines_bool_t  mapper3_create(ines_mapper_t* p_mapper)
{
	p_mapper->reset = mapper3_reset;
	p_mapper->writehigh = mapper3_writehigh;
	return ines_true;
}



