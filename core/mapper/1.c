
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../ppu.h"
#include "../mapper.h"

/*

REG[0] :   Control (internal, $8000-$9FFF)

4bit0
-----
CPPMM
|||||
|||++- Mirroring (0: one-screen, lower bank; 1: one-screen, upper bank;
|||               2: vertical; 3: horizontal)
|++--- PRG ROM bank mode (0, 1: switch 32 KB at $8000, ignoring low bit of bank number;
|                         2: fix first bank at $8000 and switch 16 KB bank at $C000;
|                         3: fix last bank at $C000 and switch 16 KB bank at $8000)
+----- CHR ROM bank mode (0: switch 8 KB at a time; 1: switch two separate 4 KB banks)

REG[1] :  CHR bank 0 (internal, $A000-$BFFF)

4bit0
-----
CCCCC
|||||
+++++- Select 4 KB or 8 KB CHR bank at PPU $0000 (low bit ignored in 8 KB mode)

REG[2] :  CHR bank 1 (internal, $C000-$DFFF)

4bit0
-----
CCCCC
|||||
+++++- Select 4 KB CHR bank at PPU $1000 (ignored in 8 KB mode)

REG[3] :  PRG bank (internal, $E000-$FFFF)

4bit0
-----
RPPPP
|||||
|++++- Select 16 KB PRG ROM bank (low bit ignored in 32 KB mode)
+----- PRG RAM chip enable (0: enabled; 1: disabled; ignored on MMC1A)


*/



#define MMC1_SMALL   0
#define MMC1_512K    1
#define MMC1_1024K   2

struct _MMC1_data_ {
	ines_int_t    patch;
	ines_byte_t   reg[4];

	ines_byte_t   bits;
	ines_word_t   bank4;
	ines_word_t   bank5;
	ines_word_t   bank6;
	ines_word_t   bank7;

	ines_word_t   hi0;
	ines_word_t   hi1;

	ines_int_t    count;
	ines_word_t   last_write_addr;
	ines_word_t   prom_256k_base;
	ines_byte_t   prom_size_type;
	ines_byte_t   prom_swap;

};
typedef struct _MMC1_data_   MMC1_data_t;


#define mapper2MMC1data(mapper)   (MMC1_data_t*)((mapper)->p_data)


static void MMC1_set_banks(MMC1_data_t*  p_data, ines_host_t* p_host)
{
	ines_set_prom_bank_4(p_host, 
		p_data->prom_256k_base + p_data->bank4, 
		p_data->prom_256k_base + p_data->bank5, 
		p_data->prom_256k_base + p_data->bank6, 
		p_data->prom_256k_base + p_data->bank7);
}

static void mapper1_reset(ines_mapper_t* p_mapper)
{
	ines_word_t  prom_1k_num;
	MMC1_data_t*  p_data = mapper2MMC1data(p_mapper);
	ines_host_t* p_host = mapper2host(p_mapper);

	prom_1k_num = p_host->prom_8k_num * 8;


	if(prom_1k_num == 1024)
		p_data->prom_size_type = MMC1_1024K;
	else if(prom_1k_num == 512)
		p_data->prom_size_type = MMC1_512K;
	else
		p_data->prom_size_type = MMC1_SMALL;

	

	if(p_data->prom_size_type == MMC1_SMALL)
	{
		// last 2 blocks
		p_data->hi0 = p_host->rom.PROM_block_num * 2 - 2 ;
		p_data->hi1 = p_host->rom.PROM_block_num * 2 - 1 ;
	}
	else
	{
		// first 256k last 2 blocks
		p_data->hi0 = 256 / 8 - 2 ;
		p_data->hi1 = 256 / 8 - 1 ;
	}

	p_data->bank4 = 0;
	p_data->bank5 = 1;
	p_data->bank6 = p_data->hi0;
	p_data->bank7 = p_data->hi1;

	p_data->last_write_addr = 0;
	p_data->prom_256k_base = 0;
	p_data->reg[0] = 0x0c;
	p_data->reg[1] = 0x00;
	p_data->reg[2] = 0x00;
	p_data->reg[3] = 0x00;
	p_data->bits = 0;

	MMC1_set_banks(p_data, p_host);
}


static void mapper1_writehigh(ines_mapper_t* p_mapper, ines_word_t  addr, ines_byte_t val)
{
	ines_int_t  reg_num;
	ines_byte_t bank_num;
	MMC1_data_t*  p_data = mapper2MMC1data(p_mapper);
	ines_host_t*   p_host = mapper2host(p_mapper);

	INES_LOG(LOG_DBG, MOD_MMC, ISTR("%s: $%04X = #$%02X, count=%d, bits=#$%02X.\n"), __TFUNCTION__, addr, val, p_data->count, p_data->bits);	

	if( (p_data->last_write_addr & 0x6000) != (addr & 0x6000) )
	{
		p_data->count = 0;
		p_data->bits = 0;
	}

	p_data->last_write_addr = addr;

	// bit 7 for reset

	if(val & 0x80)
	{
		p_data->count = 0;
		p_data->bits = 0;
		return;
	}

	p_data->bits |= ( (val & 0x01) << p_data->count );

	if(++p_data->count < 5)
		return;

	reg_num = (addr & 0x6000) >> 13;

	p_data->reg[reg_num]  = p_data->bits;
	p_data->count = 0;
	p_data->bits = 0;

	INES_LOG(LOG_DBG, MOD_MMC, ISTR("Mapper1 SET_REG[%d] = #$%02X\n"), reg_num, p_data->reg[reg_num]);

	switch(reg_num)
	{
	case 0:  // reg for set ppu mirror type
		switch(p_data->reg[0] & 0x03)
		{
		case 0:
			ines_ppu_set_mirror(&p_host->ppu, 0,0,0,0);
			break;
		case 1:
			ines_ppu_set_mirror(&p_host->ppu, 1,1,1,1);
			break;
		case 2:
			ines_ppu_set_mirror_type(&p_host->ppu, MIRROR_VERT);
			break;
		case 3:
			ines_ppu_set_mirror_type(&p_host->ppu, MIRROR_HORZ);
			break;
		}
		break;
	case 1:  // 
		bank_num = p_data->reg[1];
		if(p_data->prom_size_type == MMC1_1024K)  
		{
			if(p_data->reg[0] & 0x10)
			{
				if(p_data->prom_swap)
				{
					p_data->prom_256k_base = ((ines_word_t)p_data->reg[1] & 0x10) << 1;
					if(p_data->reg[0] & 0x08)
					{
						p_data->prom_256k_base |= ((ines_word_t)p_data->reg[2] & 0x10) << 2;
					}
					MMC1_set_banks(p_data, p_host);
					p_data->prom_swap = 0;
				}
				else
				{
					p_data->prom_swap = 1;
				}
			}
			else //  !(p_data->reg[0] & 0x10)
			{
				p_data->prom_256k_base = ((ines_word_t)p_data->reg[1] & 0x10) ? (3 << 5) : 0;
				MMC1_set_banks(p_data, p_host);
			}
		}
		else if(p_data->prom_size_type == MMC1_512K && p_host->rom.VROM_block_num == 0)
		{
			p_data->prom_256k_base = ((ines_word_t)p_data->reg[1] & 0x10) << 1;
			MMC1_set_banks(p_data, p_host);
		}
		else if(p_host->rom.VROM_block_num > 0)
		{
			if(p_data->reg[0] & 0x10)
			{
				bank_num <<=2;
				ines_set_vrom_bank_n(p_host, 0, bank_num + 0);
				ines_set_vrom_bank_n(p_host, 1, bank_num + 1);
				ines_set_vrom_bank_n(p_host, 2, bank_num + 2);
				ines_set_vrom_bank_n(p_host, 3, bank_num + 3);
			}
			else
			{
				bank_num <<=2;
				ines_set_vrom_bank_8(p_host,  
					bank_num + 0, bank_num + 1, bank_num + 2, bank_num + 3,
					bank_num + 4, bank_num + 5, bank_num + 6, bank_num + 7);
			}
		}
		else
		{
			if(p_data->reg[0] & 0x10)
			{
				bank_num <<=2;
				ines_set_vram_bank_n(p_host, 0, bank_num + 0);
				ines_set_vram_bank_n(p_host, 1, bank_num + 1);
				ines_set_vram_bank_n(p_host, 2, bank_num + 2);
				ines_set_vram_bank_n(p_host, 3, bank_num + 3);
			}

		}
		break;
	case 2:
		bank_num = p_data->reg[2];
		if(p_data->prom_size_type == MMC1_1024K && (p_data->reg[0] & 0x08))
		{
			if(p_data->prom_swap)
			{
				p_data->prom_256k_base = ((ines_word_t)p_data->reg[1] & 0x10) << 1;
				p_data->prom_256k_base |= ((ines_word_t)p_data->reg[2] & 0x10) << 2;
				MMC1_set_banks(p_data, p_host);
				p_data->prom_swap = 0;
			}
			else
			{
				p_data->prom_swap = 1;
			}
		}

		if(p_host->rom.VROM_block_num == 0)
		{
			if(p_data->reg[0] & 0x10)
			{
				bank_num <<=2;
				ines_set_vram_bank_n(p_host, 4, bank_num + 0);
				ines_set_vram_bank_n(p_host, 5, bank_num + 1);
				ines_set_vram_bank_n(p_host, 6, bank_num + 2);
				ines_set_vram_bank_n(p_host, 7, bank_num + 3);
			}
		}
		else
		{
			if(p_data->reg[0] & 0x10)
			{
				bank_num <<=2;
				ines_set_vrom_bank_n(p_host, 4, bank_num + 0);
				ines_set_vrom_bank_n(p_host, 5, bank_num + 1);
				ines_set_vrom_bank_n(p_host, 6, bank_num + 2);
				ines_set_vrom_bank_n(p_host, 7, bank_num + 3);
			}
		}
		break;
	case 3:
		bank_num = p_data->reg[3];
		if(p_data->reg[0] & 0x08)
		{
			bank_num <<= 1;

			if(p_data->reg[0] & 0x04)
			{
				// 16K ROM at 0x8000
				p_data->bank4 = bank_num;
				p_data->bank5 = bank_num + 1;
				p_data->bank6 = p_data->hi0;
				p_data->bank7 = p_data->hi1;
			}
			else
			{
				if(p_data->prom_size_type == MMC1_SMALL)
				{
					// 16K ROM at 0xc000
					p_data->bank4 = 0;
					p_data->bank5 = 1;
					p_data->bank6 = bank_num;
					p_data->bank7 = bank_num + 1;
				}
			}
		}
		else
		{
			// 32K ROM at 0x8000
			bank_num <<= 1;
			p_data->bank4 = bank_num;
			p_data->bank5 = bank_num + 1;
			if(p_data->prom_size_type == MMC1_SMALL)
			{
				p_data->bank6 = bank_num + 2;
				p_data->bank7 = bank_num + 3;
			}

		}
		MMC1_set_banks(p_data, p_host);
		break;
	}
}

void mapper1_fini(ines_mapper_t* p_mapper)
{
	INES_LOG(LOG_DBG, MOD_MMC, ISTR("mapper1_fini.\n"));
	ines_free(p_mapper->p_data);
	p_mapper->p_data = NULL;
}

ines_bool_t  mapper1_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, MMC1_data_t);
	p_mapper->reset = mapper1_reset;
	p_mapper->writehigh = mapper1_writehigh;
	p_mapper->fini = mapper1_fini;
	return ines_true;
}



