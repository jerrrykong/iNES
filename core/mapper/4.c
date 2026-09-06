
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"

struct _MMC3_data_;
typedef struct _MMC3_data_   MMC3_data_t; 


struct _MMC3_data_
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
};


#define mapper2MMC3data(mapper) (MMC3_data_t*)((mapper)->p_data)
#define MMC3_chr_swap(p)  (0!=((p)->reg[0]&0x80))
#define MMC3_prg_swap(p)  (0!=((p)->reg[0]&0x40))


static void MMC3_set_cpu_bank(MMC3_data_t* p, ines_host_t* p_host)
{
	ines_word_t   prom_8k_num = p_host->prom_8k_num;

	if(MMC3_prg_swap(p))
	{
		ines_set_prom_bank_4(p_host, prom_8k_num - 2, p->prg1, p->prg0, prom_8k_num - 1);
	}
	else
	{
		ines_set_prom_bank_4(p_host, p->prg0, p->prg1, prom_8k_num - 2, prom_8k_num - 1);
	}
}

static void MMC3_set_ppu_bank(MMC3_data_t* p, ines_host_t* p_host)
{
	ines_word_t   vrom_1k_num = p_host->vrom_1k_num;

	if(vrom_1k_num > 0)
	{
		if(MMC3_chr_swap(p))
		{
			ines_set_vrom_bank_8(p_host, p->chr4, p->chr5, p->chr6, p->chr7, 
					p->chr01, p->chr01 + 1, p->chr23, p->chr23 + 1);
		}
		else
		{
			ines_set_vrom_bank_8(p_host, p->chr01, p->chr01 + 1, p->chr23, p->chr23 + 1, 
				p->chr4, p->chr5, p->chr6, p->chr7);

		}
	}
	else 
	{
		if(MMC3_chr_swap(p))
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


static void mapper4_reset(ines_mapper_t*  p_mapper)
{
	ines_int_t  n;
	MMC3_data_t* p = (MMC3_data_t*)p_mapper->p_data;
	ines_host_t* p_host = mapper2host(p_mapper);

	p->patch = 0;

	if(p_host->rom.crc32_p == 0xdebea5a6 || p_host->rom.crc32_p == 0xc5fea9f2)
	{
		//// Ninja Ryukenden 2 - Ankoku no Jashin Ken 
		//// Dai2Ji - Super Robot Taisen
		p->patch = 1;
	}
	else if(p_host->rom.crc32_p == 0xd7a97b38)
	{
		//// Chou Jinrou Senki - Warwolf
		p->patch = 2;
	}
	else if(p_host->rom.crc32_p == 0xeb2dba63)
	{
		//// VS TKO Boxing
		p->patch = 3;
		p->vs_index = 0;
	}
	else if(p_host->rom.crc32_p == 0x135adf7c)
	{
		//// VS Atari RBI Baseball
		p->patch = 4;
		p->vs_index = 0;
	}

	for(n = 0; n < 8; n++)
	{
		p->reg[n] = 0;
	}
	
	p->prg0 = 0;
	p->prg1 = 1;
	MMC3_set_cpu_bank(p, p_host);


	if(p_host->rom.VROM_block_num > 0)
	{
		p->chr01 = 0;
		p->chr23 = 2;
		p->chr4 = 4;
		p->chr5 = 5;
		p->chr6 = 6;
		p->chr7 = 7;
		MMC3_set_ppu_bank(p, p_host);
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

static const ines_byte_t sec_code_patch3[0x20] =
{
	0xff, 0xbf, 0xb7, 0x97, 0x97, 0x17, 0x57, 0x4f,
	0x6f, 0x6b, 0xeb, 0xa9, 0xb1, 0x90, 0x94, 0x14,
	0x56, 0x4e, 0x6f, 0x6b, 0xeb, 0xa9, 0xb1, 0x90,
	0xd4, 0x5c, 0x3e, 0x26, 0x87, 0x83, 0x13, 0x00
};

static ines_byte_t mapper4_readlow(ines_mapper_t*  p_mapper, ines_word_t  addr)
{
	MMC3_data_t* p = mapper2MMC3data(p_mapper);
	if(p->patch == 3)
	{
		// VS TKO Boxing security
		if(addr == 0x5e00)
		{
			p->vs_index = 0;
			return 0x00;
		}
		else if(addr == 0x5e01)
		{
			return sec_code_patch3[p->vs_index++ & 0x1f];
		}
	}
	else if(p->patch == 4)
	{
		// VS Atari RBI Baseball security
		if(addr == 0x5e00)
		{
			p->vs_index = 0;
			return 0xff;
		}
		else if(addr == 0x5e01)
		{
			if(p->vs_index++ == 0x09)
				return 0x6f;
			else
				return 0xb4;
		}
	}


	return addr>>8;
}

static void mapper4_writehigh(ines_mapper_t*  p_mapper, ines_word_t  addr, ines_byte_t   val)
{
	ines_word_t  bank_num;
	MMC3_data_t* p = mapper2MMC3data(p_mapper);
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
			MMC3_set_cpu_bank(p, p_host);
			MMC3_set_ppu_bank(p, p_host);
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
			MMC3_set_ppu_bank(p, p_host);
			break;
		case 1:
			bank_num &= 0xfe;
			p->chr23 = bank_num;
			MMC3_set_ppu_bank(p, p_host);
			break;
		case 2:
			p->chr4 = bank_num;
			MMC3_set_ppu_bank(p, p_host);
			break;
		case 3:
			p->chr5 = bank_num;
			MMC3_set_ppu_bank(p, p_host);
			break;
		case 4:
			p->chr6 = bank_num;
			MMC3_set_ppu_bank(p, p_host);
			break;
		case 5:
			p->chr7 = bank_num;
			MMC3_set_ppu_bank(p, p_host);
			break;
		case 6:
			p->prg0 = bank_num;
			MMC3_set_cpu_bank(p, p_host);
			break;
		case 7:
			p->prg1 = bank_num;
			MMC3_set_cpu_bank(p, p_host);
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

static void mapper4_hsync(ines_mapper_t*  p_mapper, ines_int_t  line)
{
	MMC3_data_t* p = mapper2MMC3data(p_mapper);
	ines_host_t* p_host = mapper2host(p_mapper);
	
	if(p->irq_enabled)
	{
		if(line >= 0 && line <= 239)
		{
			if(p_host->ppu.reg_ctrl_2 & (PPU_ENABLE_BG|PPU_ENABLE_SPR))
			{
				if(p->irq_reload)
				{
					p->irq_reload = 0;
					p->irq_counter = p->irq_latch;
				}
				else
				{
					p->irq_counter--;
				}

				if(0 == p->irq_counter)
				{
					p->irq_reload = 1;
					if(p->irq_enabled)
					{
						ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_true);
					}
				}
				/*
				if(p->patch == 1)
				{
					if(0 == --p->irq_counter)
					{
						p->irq_counter = p->irq_latch;
						ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_true);
					}
				}
				else if(p->patch == 2)
				{
					if(1 == --p->irq_counter)
					{
						p->irq_counter = p->irq_latch;
						ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_true);
					}
				}
				else
				{
					if(0 == p->irq_counter--)
					{
						p->irq_counter = p->irq_latch;
						ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_true);
					}
				}
				*/
			}
		}
	}
}

void mapper4_fini(ines_mapper_t* p_mapper)
{
	INES_LOG(LOG_DBG, MOD_MMC, ISTR("mapper4_fini.\n"));
	ines_free(p_mapper->p_data);
	p_mapper->p_data = NULL;
}

ines_bool_t  mapper4_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, MMC3_data_t);
	p_mapper->reset = mapper4_reset;
	p_mapper->writehigh = mapper4_writehigh;
	p_mapper->readlow = mapper4_readlow;
	p_mapper->hsync = mapper4_hsync;
	p_mapper->fini = mapper4_fini;
	return ines_true;
}



