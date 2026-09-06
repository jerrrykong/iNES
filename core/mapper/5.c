
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"

struct _MMC5_data_;
typedef struct _MMC5_data_   MMC5_data_t; 


struct _MMC5_data_
{
	ines_byte_t   patch;
	ines_byte_t   reg[8][2];
	ines_byte_t   irq_enabled;
	ines_byte_t   irq_line;
	ines_byte_t   irq_status;

	ines_word_t   value0;
	ines_word_t   value1;

	ines_byte_t   wram_protect0;
	ines_byte_t   wram_protect1;

	ines_byte_t   split_control;
	ines_word_t   split_bank;

	ines_byte_t    prg_bank_mode;
	ines_byte_t    chr_bank_mode;
	ines_word_t    chr_bank_high;
	ines_byte_t    gfx_mode;
};


#define mapper2MMC5data(mapper) (MMC5_data_t*)((mapper)->p_data)


static void mapper5_reset(ines_mapper_t*  p_mapper)
{
	MMC5_data_t* p = mapper2MMC5data(p_mapper);
	ines_host_t* p_host = mapper2host(p_mapper);
	ines_word_t  prom_8k_num = p_host->rom.PROM_block_num * 2;
	//ines_word_t  vrom_1k_num = p_host->rom.VROM_block_num * 8;
	ines_int_t   n;


	ines_set_sram_bank_n(p_host, 3, 0); // 设置默认SRAM块
	ines_set_prom_bank_4(p_host, prom_8k_num - 1, prom_8k_num - 1, prom_8k_num - 1, prom_8k_num - 1);
	ines_set_vrom_bank_8(p_host, 0, 1, 2, 3, 4, 5, 6, 7);

	for(n = 0; n < 8; n++)
	{
		p->reg[n][0] = n;
		p->reg[n][1] = (n & 0x03) + 4;
	}


	p->prg_bank_mode = 3;
	p->chr_bank_mode = 3;
	p->chr_bank_high = 0;
	p->gfx_mode = 0;

	p->wram_protect0 = 0x02;
	p->wram_protect1 = 0x01;


	p->irq_enabled = 0;
	p->irq_status = 0;
	p->irq_line = 0;

}

static ines_byte_t mapper5_readlow(ines_mapper_t*  p_mapper, ines_word_t  addr)
{
	MMC5_data_t* p = mapper2MMC5data(p_mapper);
	ines_host_t* p_host = mapper2host(p_mapper);
	ines_byte_t val = 0;

	//val = addr>>8;

	if(addr == 0x5204)
	{
		val = p->irq_status;
		p->irq_status &= ~0x80;
		ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false);
	}
	else if(addr == 0x5205)
	{
		val = (ines_byte_t)((p->value0 * p->value1) & 0xff);
	}
	else if(addr == 0x5206)
	{
		val = (ines_byte_t)(((p->value0 * p->value1) >> 8) & 0xff);
	}
	else if(addr >= 0x5c00 && addr <= 0x5fff)
	{
		val = p_host->ppu.name_table[0x800 + (addr & 0x3ff)];	
	}
	return val;
}

static void MMC5_switch_SRAM_write_protect(MMC5_data_t* p, ines_host_t* p_host)
{
	ines_int_t   protect = !(p->wram_protect0 == 0x02 && p->wram_protect1 == 0x01);
	if(protect)
	{
		p_host->cpu.bank_writeable[3] |= NES_BANK_WRITE_PROTECTED;
		p_host->cpu.bank_writeable[4] |= NES_BANK_WRITE_PROTECTED;
		p_host->cpu.bank_writeable[5] |= NES_BANK_WRITE_PROTECTED;
		p_host->cpu.bank_writeable[6] |= NES_BANK_WRITE_PROTECTED;
	}
	else
	{
		p_host->cpu.bank_writeable[3] &= ~NES_BANK_WRITE_PROTECTED;
		p_host->cpu.bank_writeable[4] &= ~NES_BANK_WRITE_PROTECTED;
		p_host->cpu.bank_writeable[5] &= ~NES_BANK_WRITE_PROTECTED;
		p_host->cpu.bank_writeable[6] &= ~NES_BANK_WRITE_PROTECTED;
	}
}


static void MMC5_set_cpu_bank(MMC5_data_t* p, ines_host_t* p_host, ines_word_t page, ines_word_t  bank)
{
	if(bank & 0x80) // bit8: 0-RAM; 1-ROM
	{
		// set ROM bank
		switch(p->prg_bank_mode)
		{
		case 0:  // 
			if(page == 7)
			{
				bank &= 0x7c;
				ines_set_prom_bank_n(p_host, 4, bank + 0);
				ines_set_prom_bank_n(p_host, 5, bank + 1);
				ines_set_prom_bank_n(p_host, 6, bank + 2);
				ines_set_prom_bank_n(p_host, 7, bank + 3);
			}
			break;
		case 1:
			if(page == 5)
			{
				bank &= 0x7e;
				ines_set_prom_bank_n(p_host, 4, bank + 0);
				ines_set_prom_bank_n(p_host, 5, bank + 1);
			}
			else if(page == 7)
			{
				bank &= 0x7e;
				ines_set_prom_bank_n(p_host, 6, bank + 0);
				ines_set_prom_bank_n(p_host, 7, bank + 1);
			}
			break;
		case 2:
			if(page == 5)
			{
				bank &= 0x7e;
				ines_set_prom_bank_n(p_host, 4, bank + 0);
				ines_set_prom_bank_n(p_host, 5, bank + 1);
			}
			else if(page == 6 || page == 7)
			{
				bank &= 0x7f;
				ines_set_prom_bank_n(p_host, page, bank + 0);
			}
			break;
		case 3:
			bank &= 0x7f;
			ines_set_prom_bank_n(p_host, page, bank + 0);
			break;
		}

	}
	else
	{
		// Set RAM Bank
		switch(p->prg_bank_mode)
		{
		case 0:  // ignore
			break; 
		case 1:
			if(page == 5)
			{
				bank &= 0x06;
				ines_set_sram_bank_n(p_host, 4, bank + 0);
				ines_set_sram_bank_n(p_host, 5, bank + 1);
			}
			break;
		case 2:
			if(page == 5)
			{
				bank &= 0x06;
				ines_set_sram_bank_n(p_host, 4, bank + 0);
				ines_set_sram_bank_n(p_host, 5, bank + 1);
			}
			else if(page == 6)
			{
				bank &= 0x07;
				ines_set_sram_bank_n(p_host, 6, bank);
			}
			break;
		case 3:
			if(page == 4)
			{
				bank &= 0x07;
				ines_set_sram_bank_n(p_host, 4, bank);
			}
			else if(page == 5)
			{
				bank &= 0x07;
				ines_set_sram_bank_n(p_host, 5, bank);
			}
			else if(page == 6)
			{
				bank &= 0x07;
				ines_set_sram_bank_n(p_host, 6, bank);
			}
			break;
		}

		MMC5_switch_SRAM_write_protect(p, p_host);
	}
}


static void MMC5_set_ppu_bank(MMC5_data_t* p, ines_host_t* p_host, ines_int_t  mode)
{
	switch(p->chr_bank_mode)
	{
	case 0:  // 8K Mode
		ines_set_vrom_bank_n(p_host, 0, ((ines_word_t)p->reg[7][mode] * 8 + 0));
		ines_set_vrom_bank_n(p_host, 1, ((ines_word_t)p->reg[7][mode] * 8 + 1));
		ines_set_vrom_bank_n(p_host, 2, ((ines_word_t)p->reg[7][mode] * 8 + 2));
		ines_set_vrom_bank_n(p_host, 3, ((ines_word_t)p->reg[7][mode] * 8 + 3));
		ines_set_vrom_bank_n(p_host, 4, ((ines_word_t)p->reg[7][mode] * 8 + 4));
		ines_set_vrom_bank_n(p_host, 5, ((ines_word_t)p->reg[7][mode] * 8 + 5));
		ines_set_vrom_bank_n(p_host, 6, ((ines_word_t)p->reg[7][mode] * 8 + 6));
		ines_set_vrom_bank_n(p_host, 7, ((ines_word_t)p->reg[7][mode] * 8 + 7));
		break;
	case 1: // 4K Mode
		ines_set_vrom_bank_n(p_host, 0, ((ines_word_t)p->reg[3][mode] * 4 + 0));
		ines_set_vrom_bank_n(p_host, 1, ((ines_word_t)p->reg[3][mode] * 4 + 1));
		ines_set_vrom_bank_n(p_host, 2, ((ines_word_t)p->reg[3][mode] * 4 + 2));
		ines_set_vrom_bank_n(p_host, 3, ((ines_word_t)p->reg[3][mode] * 4 + 3));
		ines_set_vrom_bank_n(p_host, 4, ((ines_word_t)p->reg[7][mode] * 4 + 0));
		ines_set_vrom_bank_n(p_host, 5, ((ines_word_t)p->reg[7][mode] * 4 + 1));
		ines_set_vrom_bank_n(p_host, 6, ((ines_word_t)p->reg[7][mode] * 4 + 2));
		ines_set_vrom_bank_n(p_host, 7, ((ines_word_t)p->reg[7][mode] * 4 + 3));
		break;
	case 2: // 2K Mode
		ines_set_vrom_bank_n(p_host, 0, (((ines_word_t)p->reg[1][mode] | p->chr_bank_high) * 2 + 0));
		ines_set_vrom_bank_n(p_host, 1, (((ines_word_t)p->reg[1][mode] | p->chr_bank_high) * 2 + 1));
		ines_set_vrom_bank_n(p_host, 2, (((ines_word_t)p->reg[3][mode] | p->chr_bank_high) * 2 + 0));
		ines_set_vrom_bank_n(p_host, 3, (((ines_word_t)p->reg[3][mode] | p->chr_bank_high) * 2 + 1));
		ines_set_vrom_bank_n(p_host, 4, (((ines_word_t)p->reg[5][mode] | p->chr_bank_high) * 2 + 0));
		ines_set_vrom_bank_n(p_host, 5, (((ines_word_t)p->reg[5][mode] | p->chr_bank_high) * 2 + 1));
		ines_set_vrom_bank_n(p_host, 6, (((ines_word_t)p->reg[7][mode] | p->chr_bank_high) * 2 + 0));
		ines_set_vrom_bank_n(p_host, 7, (((ines_word_t)p->reg[7][mode] | p->chr_bank_high) * 2 + 1));
		break;
	case 3: // 1K Mode
		ines_set_vrom_bank_n(p_host, 0, (((ines_word_t)p->reg[0][mode] | p->chr_bank_high)));
		ines_set_vrom_bank_n(p_host, 1, (((ines_word_t)p->reg[1][mode] | p->chr_bank_high)));
		ines_set_vrom_bank_n(p_host, 2, (((ines_word_t)p->reg[2][mode] | p->chr_bank_high)));
		ines_set_vrom_bank_n(p_host, 3, (((ines_word_t)p->reg[3][mode] | p->chr_bank_high)));
		ines_set_vrom_bank_n(p_host, 4, (((ines_word_t)p->reg[4][mode] | p->chr_bank_high)));
		ines_set_vrom_bank_n(p_host, 5, (((ines_word_t)p->reg[5][mode] | p->chr_bank_high)));
		ines_set_vrom_bank_n(p_host, 6, (((ines_word_t)p->reg[6][mode] | p->chr_bank_high)));
		ines_set_vrom_bank_n(p_host, 7, (((ines_word_t)p->reg[7][mode] | p->chr_bank_high)));
		break;
	}
}


static void mapper5_writelow(ines_mapper_t*  p_mapper, ines_word_t  addr, ines_byte_t   val)
{
	MMC5_data_t* p = mapper2MMC5data(p_mapper);
	ines_host_t* p_host = mapper2host(p_mapper);

	INES_LOG(LOG_DBG, MOD_MMC, ISTR("%s: $%04X = #$%02X.\n"), __TFUNCTION__, addr, val);	

	switch(addr)
	{
	case 0x5100:
		p->prg_bank_mode = val & 0x03;
		break;
	case 0x5101:
		p->chr_bank_mode = val & 0x03;
		break;
	case 0x5102:
		p->wram_protect0 = val & 0x03;
		MMC5_switch_SRAM_write_protect(p, p_host);
		break;
	case 0x5103:
		p->wram_protect1 = val & 0x03;
		MMC5_switch_SRAM_write_protect(p, p_host);
		break;
	case 0x5104:
		p->gfx_mode = val & 0x03;
		break;
	case 0x5105:
		ines_ppu_set_mirror(&p_host->ppu, val & 0x03, (val>>2) & 0x03, (val>>4) & 0x03, (val>>6) & 0x03);
		break;
	case 0x5106: // fill mode color for nametable #3
		memset(p_host->ppu.name_table + 0x0c00, val, 0x03c0); 
		break;
	case 0x5107:  
		val &= 0x03;
		val = val | (val << 2) | (val << 4) | (val << 6);
		memset(p_host->ppu.name_table + 0x0fc0, 0x40, val);
		break;
	case 0x5113:
		// 没有迹象表明有哪个NES游戏会通过镜像地址来访问SRAM，所以即使没有游戏会带有完整的
		//  64K SRAM，也可以通过直接模拟全部的64K内存来正常运行它。写入 ox5114~0x5117 切换RAM也这样处理即可。
		//  没必要关心游戏卡带的真实RAM芯片配置。
		ines_set_sram_bank_n(p_host, 3, val & 0x07);
		break;
	case 0x5114:
	case 0x5115:
	case 0x5116:
	case 0x5117:
		MMC5_set_cpu_bank(p, p_host, addr & 0x7, val);
		break;
	case 0x5120:
	case 0x5121:
	case 0x5122:
	case 0x5123:
	case 0x5124:
	case 0x5125:
	case 0x5126:
	case 0x5127:
		p->reg[addr&0x7][0] = val;
		MMC5_set_ppu_bank(p, p_host, 0);
		break;
	case 0x5128:
	case 0x5129:
	case 0x512a:
	case 0x512b:
		p->reg[addr&0x3][1] = val;
		p->reg[(addr&0x3)+4][1] = val;
		break;
	case 0x5130:
		p->chr_bank_high = (val & 0x03) << 8;
		break;

	case 0x5200:
		p->split_control = val;
		break;
	case 0x5201:
		//p->split_scroll = val;
		break;
	case 0x5202:
		p->split_bank = val & 0x3f;
		break;
	case 0x5203:
		p->irq_line = val;
		ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false);
		break;
	case 0x5204:
		p->irq_enabled = val;
		ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false);
		break;
	case 0x5205:
		p->value0 = val;
		break;
	case 0x5206:
		p->value1 = val;
		break;
	default:
		if(addr >= 0x5000 && addr <= 0x5015)
		{
			// extern sound channel regs
		}
		else if(addr >= 0x5c00 && addr <= 0x5fff)
		{
			// write to extern NameTable 2
			p_host->ppu.name_table[0x800 + (addr & 0x3ff)] = val;
			// 
		}

	}
}

static void mapper5_writehigh(ines_mapper_t*  p_mapper, ines_word_t  addr, ines_byte_t   val)
{
	INES_LOG(LOG_DBG, MOD_MMC, ISTR("%s: $%04X = #$%02X.\n"), __TFUNCTION__, addr, val);	
	// Attempted to write PROM or protected SRAM area.
}


static void mapper5_hsync(ines_mapper_t*  p_mapper, ines_int_t  line)
{
	MMC5_data_t* p = mapper2MMC5data(p_mapper);
	ines_host_t* p_host = mapper2host(p_mapper);
	if(line <= 240)
	{
		if(line == p->irq_line)
		{
			if((p_host->ppu.reg_ctrl_2 & (PPU_ENABLE_BG|PPU_ENABLE_SPR)) == (PPU_ENABLE_BG|PPU_ENABLE_SPR))
			{
				p->irq_status |= 0x80;
			}
		}
		if((p->irq_status & 0x80) && (p->irq_enabled & 0x80))
		{
		 	ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_true);
		}
	}
	else
	{
		p->irq_status |= 0x40;
	}
}


static ines_byte_t mapper5_PPU_latch(ines_mapper_t*  p_mapper, ines_word_t addr, ines_int_t  mode)
{
	ines_word_t   bank;
	MMC5_data_t* p = mapper2MMC5data(p_mapper);
	ines_host_t* p_host = mapper2host(p_mapper);
	if(p->gfx_mode == 1 && mode == 1)
	{
		bank = (p_host->ppu.name_table[0x800 + (addr)] & 0x3f) << 2;
		ines_set_vrom_bank_8(p_host, bank, bank+1,bank+2,bank+3,bank, bank+1,bank+2,bank+3);
		return ((p_host->ppu.name_table[0x800 + (addr)] & 0xc0) >> 4) | 0x01;
	}
	else
	{
		MMC5_set_ppu_bank(p, p_host, mode);
		return 0;
	}
}


void mapper5_fini(ines_mapper_t* p_mapper)
{
	INES_LOG(LOG_DBG, MOD_MMC, ISTR("mapper5_fini.\n"));
	ines_free(p_mapper->p_data);
	p_mapper->p_data = NULL;
}


ines_bool_t  mapper5_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, MMC5_data_t);
	p_mapper->reset = mapper5_reset;
	p_mapper->writelow = mapper5_writelow;
	p_mapper->writehigh = mapper5_writehigh;
	p_mapper->readlow = mapper5_readlow;
	p_mapper->hsync = mapper5_hsync;
	p_mapper->PPU_latch = mapper5_PPU_latch;
	p_mapper->fini = mapper5_fini;
	return ines_true;
}


