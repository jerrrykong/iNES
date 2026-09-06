
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"

struct _MMC18_data_;
typedef struct _MMC18_data_  MMC18_data_t;


struct _MMC18_data_
{
	ines_byte_t   prg[4];
	ines_byte_t   chr[8];
	ines_word_t   irq_count;
	ines_word_t   irq_latch;
	ines_byte_t   irq_reload;
	ines_byte_t   irq_enabled;
	ines_byte_t   irq_mode;
};

#define mapper2MMC18data(mapper) (MMC18_data_t*)((mapper)->p_data)



static void mapper18_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	MMC18_data_t* p = mapper2MMC18data(p_mapper);

	p->prg[0] = 0;
	p->prg[1] = 1;
	p->prg[2] = p_host->prom_8k_num - 2;
	p->prg[3] = p_host->prom_8k_num - 1;
	
	p->chr[0] = 0;
	p->chr[1] = 0;
	p->chr[2] = 0;
	p->chr[3] = 0;
	p->chr[4] = 0;
	p->chr[5] = 0;
	p->chr[6] = 0;
	p->chr[7] = 0;



	p->irq_count = 0;
	p->irq_latch = 0;
	p->irq_reload = 0;
	p->irq_enabled = 0;
	p->irq_mode = 0;

	ines_set_prom_bank_4(p_host, p->prg[0], p->prg[1], p->prg[2], p->prg[3]);
}

static void mapper18_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t  val)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	MMC18_data_t* p = mapper2MMC18data(p_mapper);
	int index;

	INES_LOG(LOG_DBG, MOD_MMC, ISTR("%s: $%04X = #$%02X.\n"), __TFUNCTION__, addr, val);	

	switch(addr & 0xf003)
	{
	case 0x8000:
		p->prg[0] = (p->prg[0] & 0xf0) | (val & 0x0f);
		ines_set_prom_bank_n(p_host, 4, p->prg[0]);
		break;
	case 0x8001:
		p->prg[0] = (p->prg[0] & 0x0f) | ((val & 0x0f)<<4);
		ines_set_prom_bank_n(p_host, 4, p->prg[0]);
		break;
	case 0x8002:
		p->prg[1] = (p->prg[1] & 0xf0) | (val & 0x0f);
		ines_set_prom_bank_n(p_host, 5, p->prg[1]);
		break;
	case 0x8003:
		p->prg[1] = (p->prg[1] & 0x0f) | ((val & 0x0f)<<4);
		ines_set_prom_bank_n(p_host, 5, p->prg[1]);
		break;
	case 0x9000:
		p->prg[2] = (p->prg[2] & 0xf0) | (val & 0x0f);
		ines_set_prom_bank_n(p_host, 6, p->prg[2]);
		break;
	case 0x9001:
		p->prg[2] = (p->prg[2] & 0x0f) | ((val & 0x0f)<<4);
		ines_set_prom_bank_n(p_host, 6, p->prg[2]);
		break;
	case 0xa000:
	case 0xa002:
	case 0xb000:
	case 0xb002:
	case 0xc000:
	case 0xc002:
	case 0xd000:
	case 0xd002:
		index = ((addr>>12)-0x0a) * 2 + ((addr>>1)&1);
		p->chr[index] = (p->chr[index] & 0xf0) | (val & 0x0f);
		ines_set_vrom_bank_n(p_host, index, p->chr[index]);
		break;
	case 0xa001:
	case 0xa003:
	case 0xb001:
	case 0xb003:
	case 0xc001:
	case 0xc003:
	case 0xd001:
	case 0xd003:
		index = ((addr>>12)-0x0a) * 2 + ((addr>>1)&1);
		p->chr[index] = (p->chr[index] & 0x0f) | ((val & 0x0f)<<4);
		ines_set_vrom_bank_n(p_host, index, p->chr[index]);
		break;	
	case 0xe000:
	case 0xe001:
	case 0xe002:
	case 0xe003:
		// set irq counter
		index = (addr & 0x03) * 4;
		p->irq_latch = ( p->irq_latch & ~(0x0f<<(index)) ) | ( (val & 0x0f) <<(index) );
		break;
	case 0xf000:
		// reset irq counter
		p->irq_count = p->irq_latch;
		ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false);
		break;
	case 0xf001:
		p->irq_enabled = val & 1;
		p->irq_mode = (val >> 1) & 0x07;
		ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false);
		break;
	case 0xf002:
		switch(val & 0x03 )
		{
		case 0:
			ines_ppu_set_mirror_type(&p_host->ppu, MIRROR_HORZ);
			break;
		case 1:
			ines_ppu_set_mirror_type(&p_host->ppu, MIRROR_VERT);
			break;
		case 2:
			ines_ppu_set_mirror(&p_host->ppu, 0, 0, 0, 0);
			break;
		case 3:
			ines_ppu_set_mirror(&p_host->ppu, 1, 1, 1, 1);
			break;
		}
	
	}
}


static void mapper18_hsync(ines_mapper_t*  p_mapper, ines_int_t  line)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	MMC18_data_t* p = mapper2MMC18data(p_mapper);

	if(p->irq_enabled )
	{



		if(p->irq_count < 113)
		{
			p->irq_count = p->irq_latch;
			ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_true);
		}
		else
		{
			int irq = 0;
			ines_word_t old = p->irq_count;
			p->irq_count -= 113;

			if(p->irq_mode & 0x04)
			{
				if( (p->irq_count & 0xfff0) != (old & 0xfff0) )
				{
					irq = 1;
				}
			}
			else if(p->irq_mode & 0x02)
			{
				if( (p->irq_count & 0xff00) != (old & 0xff00) )
				{
					irq = 1;
				}
			}
			else if(p->irq_mode & 0x01)
			{
				if( (p->irq_count & 0xf000) != (old & 0xf000) )
				{
					irq = 1;
				}
			}

			if(irq)
			{
				p->irq_count = p->irq_latch;
				ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_true);
			}
		}
	}
}


void mapper18_fini(ines_mapper_t* p_mapper)
{
	INES_LOG(LOG_DBG, MOD_MMC, ISTR("mapper18_fini.\n"));
	ines_free(p_mapper->p_data);
	p_mapper->p_data = NULL;
}



ines_bool_t  mapper18_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, MMC18_data_t);
	p_mapper->reset = mapper18_reset;
	p_mapper->writehigh = mapper18_writehigh;
	p_mapper->hsync = mapper18_hsync;
	p_mapper->fini = mapper18_fini;
	return ines_true;
}



