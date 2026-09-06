
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"


static void mapper0_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);

	if(p_host->rom.PROM_block_num > 1)
	{
		ines_set_prom_bank_4(p_host, 0,1,2,3);
	}
	else
	{
		ines_set_prom_bank_4(p_host, 0,1,0,1);
	}

	if(p_host->rom.VROM_block_num > 0)
	{
		ines_set_vrom_bank_8(p_host, 0,1,2,3,4,5,6,7);
	}
}



ines_bool_t  mapper0_create(ines_mapper_t* p_mapper)
{
	p_mapper->reset = mapper0_reset;
	return ines_true;
}



