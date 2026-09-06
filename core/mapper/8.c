
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"



static void mapper8_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	
	
	ines_set_prom_bank_4(p_host, 0, 1, 2, 3);
	ines_set_vrom_bank_8(p_host, 0, 1, 2, 3, 4, 5, 6, 7);
}

static void mapper8_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t  val)
{
	ines_host_t*  p_host = mapper2host(p_mapper);

	ines_word_t  prg_bank = (ines_word_t)(val & 0xf8) >> 2;
	ines_word_t  chr_bank = (ines_word_t)(val & 0x7) << 3;

	ines_set_prom_bank_n(p_host, 4, prg_bank + 0);
	ines_set_prom_bank_n(p_host, 5, prg_bank + 1);
	ines_set_vrom_bank_8(p_host, chr_bank + 0, chr_bank + 1, chr_bank + 2, chr_bank + 3, 
								chr_bank + 4, chr_bank + 5, chr_bank + 6, chr_bank + 7);

}

ines_bool_t  mapper8_create(ines_mapper_t* p_mapper)
{
	p_mapper->reset = mapper8_reset;
	p_mapper->writehigh = mapper8_writehigh;
	return ines_true;
}



