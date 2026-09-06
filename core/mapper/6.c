
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"


struct _MMC6_data_ ;
typedef struct _MMC6_data_ MMC6_data_t;

struct _MMC6_data_ 
{
	ines_int_t  irq_enable;
	ines_int_t  irq_counter;
};


#define mapper2MMC6data(p)   ( (MMC6_data_t*)(p)->p_data )

static void mapper6_reset(ines_mapper_t* p_mapper)
{
	MMC6_data_t* p = mapper2MMC6data(p_mapper);
	ines_host_t*  p_host = mapper2host(p_mapper);
	
	
	ines_set_prom_bank_4(p_host, 0,1,14,15);

	if(p_host->rom.VROM_block_num > 0)
	{
		ines_set_vrom_bank_8(p_host, 0,1,2,3,4,5,6,7);
	}
	else
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

	p->irq_enable = 0;
	p->irq_counter = 0;
}

static void mapper6_writelow(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t  val)
{
	MMC6_data_t* p = mapper2MMC6data(p_mapper);
	ines_host_t*  p_host = mapper2host(p_mapper);

	switch(addr)
	{
	case 0x42fe:
		if(val & 0x10) 
			ines_ppu_set_mirror(&p_host->ppu, 1,1,1,1);
		else
			ines_ppu_set_mirror(&p_host->ppu, 0,0,0,0);
		break;
	case 0x42ff:
		if(val & 0x10)
			ines_ppu_set_mirror_type(&p_host->ppu, MIRROR_HORZ);
		else
			ines_ppu_set_mirror_type(&p_host->ppu, MIRROR_VERT);
		break;
	case 0x4501:
		p->irq_enable = 0;
		ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false);
		break;
	case 0x4502:
		p->irq_counter = (p->irq_counter & 0xff00) | val;
		break;
	case 0x4503:
		p->irq_counter = (p->irq_counter & 0x00ff) | ((ines_int_t)val<<8);
		p->irq_enable = 1;
		ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false);
		break;
	}
}

static void mapper6_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t  val)
{
	//MMC6_data_t* p = mapper2MMC6data(p_mapper);
	ines_host_t*  p_host = mapper2host(p_mapper);

	ines_byte_t  prg_bank = (val & 0x3c) >> 2;
	ines_byte_t  chr_bank = (val & 0x03);

	ines_set_prom_bank_n(p_host, 4, prg_bank * 2 + 0);
	ines_set_prom_bank_n(p_host, 5, prg_bank * 2 + 1);

	ines_set_vram_bank_n(p_host, 0, chr_bank * 8 + 0);
	ines_set_vram_bank_n(p_host, 1, chr_bank * 8 + 1);
	ines_set_vram_bank_n(p_host, 2, chr_bank * 8 + 2);
	ines_set_vram_bank_n(p_host, 3, chr_bank * 8 + 3);
	ines_set_vram_bank_n(p_host, 4, chr_bank * 8 + 4);
	ines_set_vram_bank_n(p_host, 5, chr_bank * 8 + 5);
	ines_set_vram_bank_n(p_host, 6, chr_bank * 8 + 6);
	ines_set_vram_bank_n(p_host, 7, chr_bank * 8 + 7);
}

static void mapper6_hsync(ines_mapper_t* p_mapper, ines_int_t line)
{
	ines_host_t*  p_host;
	MMC6_data_t* p = mapper2MMC6data(p_mapper);
	
	if(p->irq_enable)
	{
		p->irq_counter += 133;
		if(p->irq_counter >= 0xffff)
		{
			p->irq_counter = 0;
			p_host = mapper2host(p_mapper);
			ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_true);
		}
	}
}

void mapper6_fini(ines_mapper_t* p_mapper)
{
	INES_LOG(LOG_DBG, MOD_MMC, ISTR("mapper6_fini.\n"));
	ines_free(p_mapper->p_data);
	p_mapper->p_data = NULL;
}


ines_bool_t  mapper6_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, MMC6_data_t);
	p_mapper->reset = mapper6_reset;
	p_mapper->writelow = mapper6_writelow;
	p_mapper->writehigh = mapper6_writehigh;
	p_mapper->hsync = mapper6_hsync;
	p_mapper->fini = mapper6_fini;
	return ines_true;
}



