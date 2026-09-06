
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"


struct _MMC3_v1_data_;
typedef struct _MMC3_v1_data_   MMC3_v1_data_t; 


struct _MMC3_v1_data_
{
	ines_byte_t   patch;
	ines_byte_t   reg[8];
	ines_word_t   prg0, prg1;
	ines_word_t   chr01, chr23, chr4, chr5, chr6, chr7;
	ines_byte_t   irq_enabled;
	ines_byte_t   irq_counter;
	ines_byte_t   irq_latch;
	ines_byte_t   irq_reload;
	ines_byte_t   vs_index;
	ines_word_t   vb0, vb1;  // externed chr bank switchs
};


#define mapper2MMC3v1data(mapper) (MMC3_v1_data_t*)((mapper)->p_data)
#define MMC3_v1_chr_swap(p)  (0!=((p)->reg[0]&0x80))
#define MMC3_v1_prg_swap(p)  (0!=((p)->reg[0]&0x40))


static void MMC3_v1_set_cpu_bank(MMC3_v1_data_t* p, ines_host_t* p_host)
{
	ines_word_t   prom_8k_num = p_host->rom.PROM_block_num * 2;

	if(MMC3_v1_prg_swap(p))
	{
		ines_set_prom_bank_4(p_host, prom_8k_num - 2, p->prg1, p->prg0, prom_8k_num - 1);
	}
	else
	{
		ines_set_prom_bank_4(p_host, p->prg0, p->prg1, prom_8k_num - 2, prom_8k_num - 1);
	}
}

static void MMC3_v1_set_ppu_bank(MMC3_v1_data_t* p, ines_host_t* p_host)
{
	ines_word_t   vrom_1k_num = p_host->prom_8k_num;

	if(vrom_1k_num > 0)
	{
		if(MMC3_v1_chr_swap(p))
		{
			ines_set_vrom_bank_8(p_host, p->vb0 + p->chr4, p->vb0 + p->chr5, p->vb0 + p->chr6, p->vb0 + p->chr7, 
				p->vb1 + p->chr01, p->vb1 + p->chr01 + 1, p->vb1 + p->chr23, p->vb1 + p->chr23 + 1);
		}
		else
		{
			ines_set_vrom_bank_8(p_host, p->vb0 + p->chr01, p->vb0 + p->chr01 + 1, p->vb0 + p->chr23, p->vb0 + p->chr23 + 1, 
				p->vb1 + p->chr4, p->vb1 + p->chr5, p->vb1 + p->chr6, p->vb1 + p->chr7);

		}
	}
	else 
	{
		if(MMC3_v1_chr_swap(p))
		{
			ines_set_vram_bank_n(p_host, 0, p->chr4);
			ines_set_vram_bank_n(p_host, 1, p->chr5);
			ines_set_vram_bank_n(p_host, 2, p->chr6);
			ines_set_vram_bank_n(p_host, 3, p->chr7);
			ines_set_vram_bank_n(p_host, 4, p->chr01);
			ines_set_vram_bank_n(p_host, 5, p->chr01+1);
			ines_set_vram_bank_n(p_host, 6, p->chr23);
			ines_set_vram_bank_n(p_host, 7, p->chr23+1);
		}
		else
		{
			ines_set_vram_bank_n(p_host, 0, p->chr01);
			ines_set_vram_bank_n(p_host, 1, p->chr01+1);
			ines_set_vram_bank_n(p_host, 2, p->chr23);
			ines_set_vram_bank_n(p_host, 3, p->chr23+1);
			ines_set_vram_bank_n(p_host, 4, p->chr4);
			ines_set_vram_bank_n(p_host, 5, p->chr5);
			ines_set_vram_bank_n(p_host, 6, p->chr6);
			ines_set_vram_bank_n(p_host, 7, p->chr7);
		}
	}
}


static void mapper12_reset(ines_mapper_t*  p_mapper)
{
	ines_int_t  n;
	MMC3_v1_data_t* p = mapper2MMC3v1data(p_mapper);
	ines_host_t* p_host = mapper2host(p_mapper);

	p->patch = 0;


	for(n = 0; n < 8; n++)
	{
		p->reg[n] = 0;
	}

	p->prg0 = 0;
	p->prg1 = 1;
	MMC3_v1_set_cpu_bank(p, p_host);

	p->vb0 = 0;
	p->vb1 = 0;

	if(p_host->rom.VROM_block_num > 0)
	{
		p->chr01 = 0;
		p->chr23 = 2;
		p->chr4 = 4;
		p->chr5 = 5;
		p->chr6 = 6;
		p->chr7 = 7;
		MMC3_v1_set_ppu_bank(p, p_host);
	}
	else
	{
		p->chr01 = 0;
		p->chr23 = 0;
		p->chr4 = 0;
		p->chr5 = 0;
		p->chr6 = 0;
		p->chr7 = 0;
	}
}


static ines_byte_t mapper12_readlow(ines_mapper_t*  p_mapper, ines_word_t  addr)
{

	return addr>>8;
}

static void mapper12_writelow(ines_mapper_t*  p_mapper, ines_word_t  addr, ines_byte_t val)
{
	MMC3_v1_data_t* p = mapper2MMC3v1data(p_mapper);
	ines_host_t* p_host = mapper2host(p_mapper);

	INES_LOG(LOG_DBG, MOD_MMC, ISTR("%s: $%04X = #$%02X.\n"), __TFUNCTION__, addr, val);	

	if(addr < 0x6000)
	{
		p->vb0 = (val & 0x01) * 256;
		p->vb1 = (val & 0x10) * 16;
		MMC3_v1_set_ppu_bank(p, p_host);
	}
}


static void mapper12_writehigh(ines_mapper_t*  p_mapper, ines_word_t  addr, ines_byte_t   val)
{
	ines_word_t  bank_num;
	MMC3_v1_data_t* p = mapper2MMC3v1data(p_mapper);
	ines_host_t* p_host = mapper2host(p_mapper);
	//ines_word_t  chr_mask;

	INES_LOG(LOG_DBG, MOD_MMC, ISTR("%s: $%04X = #$%02X.\n"), __TFUNCTION__, addr, val);	

	switch(addr & 0xe001)
	{
	case 0x8000:
		if( (p->reg[0] & 0xc0) == (val & 0xc0) )
		{
			p->reg[0] = val;
		}
		else
		{
			p->reg[0] = val;
			MMC3_v1_set_cpu_bank(p, p_host);
			MMC3_v1_set_ppu_bank(p, p_host);
		}
		break;
	case 0x8001:
		bank_num = p->reg[1] = val;
		//chr_mask = addr_mask(p_host->rom.VROM_block_num << 3);
		switch(p->reg[0] & 0x07)
		{
		case 0:
			bank_num &= 0xfe;
			p->chr01 = bank_num;
			MMC3_v1_set_ppu_bank(p, p_host);
			break;
		case 1:
			bank_num &= 0xfe;
			p->chr23 = bank_num;
			MMC3_v1_set_ppu_bank(p, p_host);
			break;
		case 2:
			p->chr4 = bank_num;
			MMC3_v1_set_ppu_bank(p, p_host);
			break;
		case 3:
			p->chr5 = bank_num;
			MMC3_v1_set_ppu_bank(p, p_host);
			break;
		case 4:
			p->chr6 = bank_num;
			MMC3_v1_set_ppu_bank(p, p_host);
			break;
		case 5:
			p->chr7 = bank_num;
			MMC3_v1_set_ppu_bank(p, p_host);
			break;
		case 6:
			p->prg0 = bank_num;
			MMC3_v1_set_cpu_bank(p, p_host);
			break;
		case 7:
			p->prg1 = bank_num;
			MMC3_v1_set_cpu_bank(p, p_host);
			break;
		}

		break;
	case 0xa000:
		p->reg[2] = val;
		if(val & 0x40)
		{
			INES_LOG(LOG_DBG, MOD_MMC, ISTR("MAPPER 4 SET 0x40 ?\n"));
		}
		if(p_host->rom.mirror_type != MIRROR_FOUR_SCREEN)
		{
			if(val & 0x01)
			{
				ines_ppu_set_mirror_type(&p_host->ppu, MIRROR_HORZ);
			}
			else
			{
				ines_ppu_set_mirror_type(&p_host->ppu, MIRROR_VERT);
			}
		}
		break;
	case 0xa001:
		p->reg[3] = val;
		if(val & 0x80)
		{
			// enable save RAM 0x6000~0x7fff
		}
		else
		{
			// disable save RAM 0x6000~0x7fff
		}
		break;
	case 0xc000:
		p->reg[4] = val;
		p->irq_latch = val;
		break;
	case 0xc001:
		p->reg[5] = val;
		p->irq_reload = 1;
		break;
	case 0xe000:
		p->reg[6] = val;
		p->irq_enabled = 0;
		ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false);
		break;
	case 0xe001:
		p->reg[7] = val;
		p->irq_enabled = 1;
		break;
	}
}

static void mapper12_hsync(ines_mapper_t*  p_mapper, ines_int_t  line)
{
	MMC3_v1_data_t* p = mapper2MMC3v1data(p_mapper);
	ines_host_t* p_host = mapper2host(p_mapper);

	if(line >= 0 && line <= 239)
	{
		if(p_host->ppu.reg_ctrl_2 & (PPU_ENABLE_BG|PPU_ENABLE_SPR))
		{
			if(p->irq_reload)
			{
				p->irq_counter = p->irq_latch;
				p->irq_reload = 0;
			}
			else if(p->irq_counter > 0)
			{
				p->irq_counter--;
			}

			if(0 == p->irq_counter)
			{
				p->irq_reload = 1;
				if(p->irq_enabled && p->irq_latch > 0)
				{
					ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_true);
				}
			}
		}
	}
}

void mapper12_fini(ines_mapper_t* p_mapper)
{
	INES_LOG(LOG_DBG, MOD_MMC, ISTR("mapper12_fini.\n"));
	ines_free(p_mapper->p_data);
	p_mapper->p_data = NULL;
}

ines_bool_t  mapper12_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, MMC3_v1_data_t);
	p_mapper->reset = mapper12_reset;
	p_mapper->writehigh = mapper12_writehigh;
	p_mapper->readlow = mapper12_readlow;
	p_mapper->writelow = mapper12_writelow;
	p_mapper->hsync = mapper12_hsync;
	p_mapper->fini = mapper12_fini;
	return ines_true;
}


