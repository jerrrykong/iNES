
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"

// no vrom

static void mapper2_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	ines_word_t   prom_8k_num = p_host->prom_8k_num;

	ines_set_prom_bank_4(p_host, 0,1,prom_8k_num - 2, prom_8k_num - 1);
}


static void mapper2_writehigh(ines_mapper_t* p_mapper, ines_word_t  addr, ines_byte_t val)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	ines_word_t   prom_8k_num = p_host->prom_8k_num;
	ines_word_t   b4;
	b4 = (val & (prom_8k_num-1)) << 1;

	ines_set_prom_bank_4(p_host, b4, b4+1,prom_8k_num - 2, prom_8k_num - 1);

}


ines_bool_t  mapper2_create(ines_mapper_t* p_mapper)
{
	p_mapper->reset = mapper2_reset;
	p_mapper->writehigh = mapper2_writehigh;
	return ines_true;
}



