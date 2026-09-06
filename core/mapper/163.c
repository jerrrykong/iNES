
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"

struct _MMC163_data_
{
	ines_byte_t    sec_ctrl;
	ines_byte_t    sec_tri;
	ines_byte_t    sec_reg;
	ines_byte_t    prg_reg;
	ines_byte_t    chr_reg;
};

typedef struct _MMC163_data_   MMC163_data_t;

#define mapper2MMC163data(mapper)   ((MMC163_data_t*)((mapper)->p_data))


static void mapper163_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	MMC163_data_t*  p = mapper2MMC163data(p_mapper);


	p->sec_reg = 0;
	p->sec_tri = 0;
	p->sec_ctrl = 1;
	p->prg_reg = 0x0f;
	p->chr_reg = 0;

	ines_set_prom_bank_4(p_host, p->prg_reg * 4, p->prg_reg * 4 + 1, p->prg_reg * 4 + 2, p->prg_reg * 4 + 3);

}


static ines_byte_t mapper163_readlow(ines_mapper_t* p_mapper, ines_word_t addr)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	MMC163_data_t*  p = mapper2MMC163data(p_mapper);
	(void)p_host;

	switch(addr & 0x7700)
	{
	case 0x5000:
	case 0x5200:
	case 0x5300:
	case 0x5400:
	case 0x5600:
	case 0x5700:
		return 0x04;
	case 0x5100:
		return p->sec_reg;
	case 0x5500:
		return p->sec_tri ? p->sec_reg : 0;
	}

	return addr>>8;  // open bus
}

static void mapper163_writelow(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t  val)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	MMC163_data_t*  p = mapper2MMC163data(p_mapper);
	INES_LOG(LOG_DBG, MOD_MMC, ISTR("%s: $%04X = #$%02X\n"), __TFUNCTION__, addr, val);

	if(addr == 0x5100)
	{
		if(val == 6)
		{
			// set 32K ROM bank to the 3th.
			ines_set_prom_bank_4(p_host, 12,13,14,15);
		}
	}
	else if(addr == 0x5101)
	{
		if(p->sec_ctrl && !val)
		{
			p->sec_tri ^= 0x01;
		}
		p->sec_ctrl = val;
	}
	else
	{
		switch(addr & 0x7300)
		{
		case 0x5000:
			p->chr_reg  =  val & 0x80;
			if(p->chr_reg == 0)
			{
				ines_set_vram_bank_n(p_host, 0, 0);
				ines_set_vram_bank_n(p_host, 1, 1);
				ines_set_vram_bank_n(p_host, 2, 2);
				ines_set_vram_bank_n(p_host, 3, 3);
				ines_set_vram_bank_n(p_host, 4, 4);
				ines_set_vram_bank_n(p_host, 5, 5);
				ines_set_vram_bank_n(p_host, 6, 6);
				ines_set_vram_bank_n(p_host, 7, 7);
			}
			p->prg_reg &= 0xf0;
			p->prg_reg |= (val & 0x0f);
			ines_set_prom_bank_4(p_host, p->prg_reg * 4, p->prg_reg * 4 + 1, p->prg_reg * 4 + 2, p->prg_reg * 4 + 3);
			break;
		case 0x5200:
			p->prg_reg &= 0x0f;
			p->prg_reg |= (val & 0x0f)<<4;
			ines_set_prom_bank_4(p_host, p->prg_reg * 4, p->prg_reg * 4 + 1, p->prg_reg * 4 + 2, p->prg_reg * 4 + 3);
			break;
		case 0x5300:
			p->sec_reg = val;
			break;
		}
	}
}


static void mapper163_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t  val)
{
//	ines_host_t*  p_host = mapper2host(p_mapper);
//	MMC163_data_t*  p = mapper2MMC163data(p_mapper);
}

static void mapper163_hsync(ines_mapper_t* p_mapper, ines_int_t  line)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	MMC163_data_t*  p = mapper2MMC163data(p_mapper);
	
	if(p->chr_reg && (p_host->ppu.reg_ctrl_2 & (PPU_ENABLE_BG|PPU_ENABLE_SPR)))
	{
		if(line == 128)
		{
			ines_set_vram_bank_n(p_host, 0, 4);
			ines_set_vram_bank_n(p_host, 1, 5);
			ines_set_vram_bank_n(p_host, 2, 6);
			ines_set_vram_bank_n(p_host, 3, 7);
			ines_set_vram_bank_n(p_host, 4, 4);
			ines_set_vram_bank_n(p_host, 5, 5);
			ines_set_vram_bank_n(p_host, 6, 6);
			ines_set_vram_bank_n(p_host, 7, 7);
		}
		else if(line == 240)
		{
			ines_set_vram_bank_n(p_host, 0, 0);
			ines_set_vram_bank_n(p_host, 1, 1);
			ines_set_vram_bank_n(p_host, 2, 2);
			ines_set_vram_bank_n(p_host, 3, 3);
			ines_set_vram_bank_n(p_host, 4, 0);
			ines_set_vram_bank_n(p_host, 5, 1);
			ines_set_vram_bank_n(p_host, 6, 2);
			ines_set_vram_bank_n(p_host, 7, 3);
		}
	}
}

void mapper163_fini(ines_mapper_t* p_mapper)
{
	INES_LOG(LOG_DBG, MOD_MMC, ISTR("mapper163_fini.\n"));
	ines_free(p_mapper->p_data);
	p_mapper->p_data = NULL;
}


ines_bool_t  mapper163_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, MMC163_data_t);
	p_mapper->reset = mapper163_reset;
	p_mapper->writehigh = mapper163_writehigh;
	p_mapper->readlow = mapper163_readlow;
	p_mapper->writelow = mapper163_writelow;
	p_mapper->hsync = mapper163_hsync;
	p_mapper->fini = mapper163_fini;
	return ines_true;
}



