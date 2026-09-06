
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"

struct _MMC2_data_;
typedef struct _MMC2_data_ MMC2_data_t;


struct _MMC2_data_
{
	ines_byte_t   reg[6];
	ines_byte_t   latch0;
	ines_byte_t   latch1;
};

#define mapper2MMC2data(mapper)   ((MMC2_data_t*)((mapper)->p_data))


static void MMC2_set_VROM_0(ines_host_t* p_host, MMC2_data_t* p)
{
	ines_word_t bank = (p->latch0 == 0xfd) ? p->reg[1] :  p->reg[2];
	bank <<= 2;
	ines_set_vrom_bank_n(p_host, 0, bank + 0);
	ines_set_vrom_bank_n(p_host, 1, bank + 1);
	ines_set_vrom_bank_n(p_host, 2, bank + 2);
	ines_set_vrom_bank_n(p_host, 3, bank + 3);
}

static void MMC2_set_VROM_1(ines_host_t* p_host, MMC2_data_t* p)
{
	ines_word_t bank = (p->latch1 == 0xfd) ? p->reg[3] :  p->reg[4];
	bank <<= 2;
	ines_set_vrom_bank_n(p_host, 4, bank + 0);
	ines_set_vrom_bank_n(p_host, 5, bank + 1);
	ines_set_vrom_bank_n(p_host, 6, bank + 2);
	ines_set_vrom_bank_n(p_host, 7, bank + 3);
}


static void mapper9_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	MMC2_data_t*  p  = mapper2MMC2data(p_mapper);

	p->reg[0] = 0;
	p->reg[1] = 0;
	p->reg[2] = 4;
	p->reg[3] = 0;
	p->reg[4] = 0;
	p->reg[5] = 0;


	p->latch0 = 0xfe;
	p->latch1 = 0xfe;
	
	ines_set_prom_bank_4(p_host, 0, p_host->prom_8k_num - 3, p_host->prom_8k_num - 2, p_host->prom_8k_num - 1);

	MMC2_set_VROM_0(p_host, p);
	MMC2_set_VROM_1(p_host, p);
}

static void mapper9_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t  val)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	MMC2_data_t*  p  = mapper2MMC2data(p_mapper);

	INES_LOG(LOG_DBG, MOD_MMC, ISTR("%s: $%04X = #$%02X.\n"), __TFUNCTION__, addr, val);	

	switch(addr & 0xf000)
	{
	case 0xa000:
		if(p->reg[0] != val)
		{
			p->reg[0] = val;
			ines_set_prom_bank_n(p_host, 4, val);
		}
		break;
	case 0xb000:
		if(p->reg[1] != val)
		{
			p->reg[1] = val;
			MMC2_set_VROM_0(p_host, p);
		}
		break;
	case 0xc000:
		if(p->reg[2] != val)
		{
			p->reg[2] = val;
			MMC2_set_VROM_0(p_host, p);
		}
		break;
	case 0xd000:
		if(p->reg[3] != val)
		{
			p->reg[3] = val;
			MMC2_set_VROM_1(p_host, p);
		}
		break;
	case 0xe000:
		if(p->reg[4] != val)
		{
			p->reg[4] = val;
			MMC2_set_VROM_1(p_host, p);
		}
		break;
	case 0xf000:
		p->reg[5] = val;
		if(0 == (val & 1))
		{
			ines_ppu_set_mirror_type(&p_host->ppu, MIRROR_VERT);
		}
		else
		{
			ines_ppu_set_mirror_type(&p_host->ppu, MIRROR_HORZ);
		}
		break;
	}
}


static void mapper9_PPU_latch_FDFE(ines_mapper_t* p_mapper, ines_word_t addr)
{
	if( (addr & 0x0fc0) == 0x0fc0 )
	{
		if( (addr & 0xfff0) == 0x0fd0 || (addr & 0xfff0) == 0x0fe0)
		{
			ines_host_t*  p_host = mapper2host(p_mapper);
			MMC2_data_t*  p  = mapper2MMC2data(p_mapper);
			if(p->latch0  != (addr & 0x0ff0) >> 4)
			{
				p->latch0 = (addr & 0x0ff0) >> 4;
				MMC2_set_VROM_0(p_host, p);
			}
		}
		else if( (addr & 0xfff0) == 0x1fd0 || (addr & 0xfff0) == 0x1fe0)
		{
			ines_host_t*  p_host = mapper2host(p_mapper);
			MMC2_data_t*  p  = mapper2MMC2data(p_mapper);
			if(p->latch1 != (addr & 0x0ff0) >> 4)
			{
				p->latch1 = (addr & 0x0ff0) >> 4;
				MMC2_set_VROM_1(p_host, p);
			}
		}
	}
}


static void mapper9_fini(ines_mapper_t* p_mapper)
{
	MMC2_data_t*  p  = mapper2MMC2data(p_mapper);
	ines_free(p);
	p_mapper->p_data = NULL;
}

ines_bool_t  mapper9_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, MMC2_data_t);
	p_mapper->reset = mapper9_reset;
	p_mapper->writehigh = mapper9_writehigh;
	p_mapper->PPU_latch_FDFE = mapper9_PPU_latch_FDFE;
	p_mapper->fini = mapper9_fini;
	return ines_true;
}



