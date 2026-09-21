
#include "nes.h"
#include "../comm/log.h"
#include "mapper_creator.h"



// 初始化
void ines_host_init(ines_host_t* p_host, int is_ntsc)
{
	if(p_host == NULL)
		return;

	memset(p_host, 0, sizeof(*p_host));

	ines_host_init_setting(p_host, is_ntsc);

	ines_rom_init(&p_host->rom);
	ines_mapper_init(&p_host->mapper);
	ines_cpu_init(&p_host->cpu);
	ines_ppu_init(&p_host->ppu);
	ines_apu_init(&p_host->apu);
	ines_joypad_init(&p_host->joypad);

}

// 删除
void ines_host_free(ines_host_t* p_host)
{
	if(p_host == NULL)
		return;
 
	ines_rom_free(&p_host->rom);
	ines_mapper_free(&p_host->mapper);
	ines_cpu_free(&p_host->cpu);
	ines_ppu_free(&p_host->ppu);
	ines_apu_free(&p_host->apu);
	ines_joypad_free(&p_host->joypad);

	p_host->status = NES_STATUS_OFF;
}

// 加载ROM文件
ines_bool_t ines_host_load_rom(ines_host_t* p_host, ines_cstr_t strNesFileName, ines_cstr_t strRAMFileName)
{
	ines_bool_t ret;

	if(p_host == NULL)
		return  ines_false;

	// first free it 
	if(p_host->status != NES_STATUS_OFF)
	{
		return  ines_false;
		// ines_host_free(p_host);
		// ines_host_init(p_host);
	}
	
	ret = ines_rom_load_from_file(&p_host->rom, strNesFileName);

	if(!ret)
		return ines_false;


	// 清空，后面需要从文件读取
	memset(p_host->SRAM, 0, sizeof(p_host->SRAM));

	// LOAD SaveSRAM;
	if(p_host->rom.has_sram)
	{
		// TODO: load from file
		ines_host_load_sram(p_host, strRAMFileName);
	}

	// 如果有trainer, 则写入 0x7000,
	if(p_host->rom.has_trainer)
	{
		memcpy(p_host->SRAM + 0x1000, p_host->rom.trainer_data, sizeof(p_host->rom.trainer_data));
	}

	// 计算地址掩码
	p_host->prom_8k_num = p_host->rom.PROM_block_num * 2;
	p_host->vrom_1k_num = p_host->rom.VROM_block_num * 8;
	p_host->prom_8k_mask = addr_mask(p_host->prom_8k_num);
	p_host->vrom_1k_mask = addr_mask(p_host->vrom_1k_num);

	if(!ines_mapper_create(&p_host->mapper, p_host->rom.mapper_num))
	{
		INES_LOG(LOG_ERR, MOD_SYS, ISTR("create mapper [%d] failed!\n"), (int)p_host->rom.mapper_num);
		return ines_false;
	}



	p_host->status = NES_STATUS_RUNNING;

	return ines_true;
}

// 软件复位
void ines_host_reset(ines_host_t* p_host)
{
	if(p_host == NULL || p_host->status == NES_STATUS_OFF)
		return;

	// do save sram

	// ines_host_save_sram(p_host);

	ines_rom_reset(&p_host->rom);
	
	// 前3块8K地址空间都有分配特殊用途。
	p_host->cpu.mem_bank[0] = NULL;
	p_host->cpu.mem_bank[1] = NULL;
	p_host->cpu.mem_bank[2] = NULL;
	p_host->cpu.mem_bank[3] = p_host->cpu.dead_mem;
	p_host->cpu.mem_bank[4] = p_host->cpu.dead_mem;
	p_host->cpu.mem_bank[5] = p_host->cpu.dead_mem;
	p_host->cpu.mem_bank[6] = p_host->cpu.dead_mem;
	p_host->cpu.mem_bank[7] = p_host->cpu.dead_mem;


	ines_ppu_reset(&p_host->ppu);
	// p_host->ppu.used_vrom = p_host->rom.VROM_block_num > 0 ? 1: 0;
	ines_joypad_reset(&p_host->joypad);

	// 复位入口先清零：mapper 如需指定(如 mapper 17 的 trainer)会在自己的 reset 里重设
	p_host->reset_entry = 0;

	ines_mapper_reset(&p_host->mapper);

	ines_cpu_reset(&p_host->cpu);
	ines_apu_reset(&p_host->apu);

	// mapper 指定了复位入口时覆盖 PC(ines_cpu_reset() 取的是 ROM 复位向量)
	if(p_host->reset_entry != 0)
	{
		INES_LOG(LOG_INF, MOD_SYS, ISTR("hard reset entry = $%04X (mapper %d).\n"),
			(ines_int_t)p_host->reset_entry, (ines_int_t)p_host->rom.mapper_num);
		p_host->cpu.reg_PC = p_host->reset_entry;
	}
	

	if(p_host->ppu.mem_bank[0] == NULL) ines_set_vram_bank_n(p_host, 0, 0);
	if(p_host->ppu.mem_bank[1] == NULL) ines_set_vram_bank_n(p_host, 1, 1);
	if(p_host->ppu.mem_bank[2] == NULL) ines_set_vram_bank_n(p_host, 2, 2);
	if(p_host->ppu.mem_bank[3] == NULL) ines_set_vram_bank_n(p_host, 3, 3);
	if(p_host->ppu.mem_bank[4] == NULL) ines_set_vram_bank_n(p_host, 4, 4);
	if(p_host->ppu.mem_bank[5] == NULL) ines_set_vram_bank_n(p_host, 5, 5);
	if(p_host->ppu.mem_bank[6] == NULL) ines_set_vram_bank_n(p_host, 6, 6);
	if(p_host->ppu.mem_bank[7] == NULL) ines_set_vram_bank_n(p_host, 7, 7);

	//p_host->cpu.mem_bank[3] = p_host->SRAM;
	///ines_ppu_set_mirror_type(&p_host->ppu, p_host->rom.mirror_type);
	p_host->DMA_high = 0;
	
	p_host->frame_irq_enabled = 1;
	p_host->frame_irq_disenabled = 0;
	p_host->base_cycles = 0;
	p_host->cpu_cycles = 0;

	// 保持状态
	//p_host->status = NES_STATUS_RUNNING;
	p_host->frame_count = 0;
}

void ines_host_init_setting(ines_host_t* p_host, ines_int_t is_ntsc)
{
	if(p_host == NULL)
		return;

	if(is_ntsc)
	{
		p_host->setting.is_ntsc = 1;
		p_host->setting.base_clock = 21477270.0;
		p_host->setting.cpu_clock = 1789772.5;
		p_host->setting.total_scan_lines = 262;
		p_host->setting.scan_line_cycles = 1364.0;
		p_host->setting.h_draw_cycles = 1024.0;
		p_host->setting.h_blank_cycles = 340.0;
		p_host->setting.scan_end_cycles = 4.0;
		p_host->setting.frame_cycles = 1364 * 262;
		p_host->setting.frame_rate = 1.0 / 60.0;

	}
	else
	{
		p_host->setting.is_ntsc = 0;
		p_host->setting.base_clock = 21281364.0;
		p_host->setting.cpu_clock = 1773447.0;
		p_host->setting.total_scan_lines = 312;
		p_host->setting.scan_line_cycles = 1362.0;
		p_host->setting.h_draw_cycles = 1024.0;
		p_host->setting.h_blank_cycles = 338.0;
		p_host->setting.scan_end_cycles = 2.0;
		p_host->setting.frame_cycles = 1362 * 312;
		p_host->setting.frame_rate = 1.0 / 50.0;
	}
}

static void exec_base_cycles(ines_host_t* p_host, double cycles)
{
	ines_int_t  this_cycles;
	
	p_host->base_cycles += cycles;
	this_cycles = (ines_int_t) ( (p_host->base_cycles) / 12.0) - p_host->cpu_cycles;
	if(this_cycles > 0)
	{
		p_host->cpu_cycles += ines_cpu_exec(&p_host->cpu, this_cycles);
	}
}


ines_int_t ines_host_doframe(ines_host_t* p_host, ines_byte_t* p_screen)
{
	int scanline;
	ines_byte_t* p_line;
	ines_int64_t cup_start_cycles;

	if(p_host == NULL)
		return 0;

	//if(p_host->base_cycles > p_host->setting.frame_cycles)
	{
		p_host->base_cycles -= p_host->cpu_cycles * 12;
		p_host->cpu_cycles = 0;
	}

	cup_start_cycles = p_host->cpu.total_cycles;
	(void)cup_start_cycles;

	ines_apu_start_frame(&p_host->apu);
	ines_ppu_start_frame(&p_host->ppu);

	// 0-239
	for(scanline = 0; scanline < 240; scanline++)
	{
		p_line = p_screen + (239 - scanline) * 256;
		//memset(p_line, scanline & 0x3f, 256);
		exec_base_cycles(p_host, p_host->setting.h_draw_cycles);
		ines_mapper_hsync(&p_host->mapper, scanline);
		//exec_base_cycles(p_host, FETCH_CYCLES * 32);
		// H_blank
		exec_base_cycles(p_host, p_host->setting.h_blank_cycles + 4);
		//exec_base_cycles(p_host, FETCH_CYCLES * 10 + p_host->setting.scan_end_cycles);
		ines_ppu_render_line(&p_host->ppu, p_line);
	}

	ines_ppu_end_frame(&p_host->ppu);

	// 240~
	for (scanline = 240; scanline < p_host->setting.total_scan_lines; scanline++)
	{
		if(scanline == 241)
		{
			// enter V_blank
			ines_ppu_start_vblank(&p_host->ppu);
			ines_mapper_vsync(&p_host->mapper);

		}
		else if(scanline == p_host->setting.total_scan_lines -  1)
		{
			ines_ppu_end_vblank(&p_host->ppu);
		}

		exec_base_cycles(p_host, p_host->setting.h_draw_cycles);
		ines_mapper_hsync(&p_host->mapper, scanline);
		// H_blank
		exec_base_cycles(p_host, p_host->setting.h_blank_cycles);		
	}

	// apu render a frame
	ines_apu_render_frame(&p_host->apu, p_host->setting.frame_rate);

	p_host->frame_count++;

	return (ines_int_t)0;
}




// 总线读写（2000h以上空间，CPU内部RAM的读写不通过此接口
ines_byte_t  ines_host_read(ines_host_t* p_host, ines_word_t addr)
{
	if(p_host == NULL)
		return 0;
	switch(addr>>13)
	{
	case 0:  /* 0x0000-0x1fff */  /*  RAM  8K space 2K, mirror x 4 */
		return p_host->cpu.RAM[addr & 0x7ff];
		break;
	case 1:  /* 0x2000-0x3fff */  /* LowReg space  2000~2007 for ppu */ 
		return ines_ppu_readlow(&p_host->ppu, addr & 0xe007);
		break;
	case 2:  /* 0x4000-0x5fff */  /* HiReg & Low PROM */
		if(addr<0x4020)  /* 4000~4020 for apu */
		{
			if(addr == 0x4014)   /*  ppu dma  */
			{
				INES_LOG(LOG_WAR, MOD_SYS, ISTR("Read from SPR-RAM DMA Reg???\n"));
				// return ines_ppu_read4014(&p_host->ppu);
				return p_host->DMA_high;
			}
			//else if(addr == 0x4015 && !(p_host->frame_irq_enabled & 0xc0)) /*  */
			//{
			//	return ines_apu_read(&p_host->apu, addr) | 0x40;
			//}
			else if(addr < 0x4016)
			{
				return ines_apu_read(&p_host->apu, addr);
			}
			else if(addr == 0x4016)
			{
				// joypad#1 regs
				return ines_joypad_read(&p_host->joypad, 0) | 0x40;
			}
			else if(addr == 0x4017)
			{
				// joypad#2 regs
				return ines_joypad_read(&p_host->joypad, 1);
			}
		}
		else
		{
			/*  0x4020~5fff */
			return ines_mapper_readlow(&p_host->mapper, addr); 
		}
		break;
	case 3:  /* 0x6000-0x7fff */  /* SRAM  with battery */
		if(NES_BANK_CAN_READ(p_host->cpu.bank_writeable[3]))
			return p_host->cpu.mem_bank[addr>>13][addr&0x1fff];
		else
			return ines_mapper_readlow(&p_host->mapper, addr);
	case 4:  /* 0x8000-0x9fff */  /* PROM Block 0 */
	case 5:  /* 0xa000-0xbfff */  /* PROM Block 0 */
	case 6:  /* 0xc000-0xdfff */  /* PROM Block 1 */
	case 7:  /* 0xe000-0xffff */  /* PROM Block 1 */
		return p_host->cpu.mem_bank[addr>>13][addr&0x1fff];
		break;
	}

	return 0;
}

void ines_host_write(ines_host_t* p_host, ines_word_t addr, ines_byte_t  val)
{
	ines_int_t   bn;
	if(p_host == NULL)
		return;
	bn = addr>>13;
	switch(bn)
	{
	case 0:  /* 0x0000-0x1fff */  /*  RAM  8K space 2K, mirror x 4 */
		//INES_LOG(LOG_DBG,MOD_SYS, "WRITE_CPU_RAM($%04X)=$%02X.\n", addr, val);
		p_host->cpu.RAM[addr & 0x7ff] = val;
		break;
	case 1:  /* 0x2000-0x3fff */  /* LowReg space  2000~2007 for ppu */ 
		ines_ppu_writelow(&p_host->ppu, addr & 0xe007, val);
		break;
	case 2:  /* 0x4000-0x5fff */  /* HiReg & Low PROM */
		if(addr<0x4020)  /* 4000~4020 for apu */
		{
			if(addr == 0x4014)   /*  ppu dma  */
			{
				ines_word_t  spr_addr;
				ines_word_t  dma_addr;
				INES_LOG(LOG_DBG, MOD_SYS, ISTR("WRITE SPR-RAM DMA ($4014)=%02X\n"), val);
				// return ines_ppu_read4014(&p_host->ppu);
				p_host->DMA_high = val;
				dma_addr = (((ines_word_t)val)<<8); 
				for(spr_addr = 0; spr_addr < 0x100; spr_addr++, dma_addr++)
				{
					p_host->ppu.sp_RAM[spr_addr] = ines_host_read(p_host, dma_addr); 
				}
				p_host->cpu.burn_cycles += 514; // DMA addition cycles
			}
			else if(addr < 0x4016 || addr == 0x4017)
			{
				ines_apu_write(&p_host->apu, addr, val);
			}
			else if(addr == 0x4016)
			{
				// joypad regs
				ines_joypad_input_brush(&p_host->joypad, val);
			}
		}
		else
		{
			/*  0x4020~5fff */
			ines_mapper_writelow(&p_host->mapper, addr, val); 
		}
		break;
	case 3:  /* 0x6000-0x7fff */  /* SRAM  with battery */
	case 4:  /* 0x8000-0x9fff */  /* PROM Block 0 */
	case 5:  /* 0xa000-0xbfff */  /* PROM Block 0 */
	case 6:  /* 0xc000-0xdfff */  /* PROM Block 1 */
	case 7:  /* 0xe000-0xffff */  /* PROM Block 1 */
		if(NES_BANK_CAN_WRITE(p_host->cpu.bank_writeable[bn] ) )
		{
			//INES_LOG(LOG_DBG,MOD_SYS, "WRITE_CPU_SRAM($%04X)=$%02X.\n", addr, val);
			p_host->cpu.mem_bank[bn][addr&0x1fff] = val;
			p_host->SRAM_write_flag = 1; // modified
		}
		else
		{
			if(bn==3)
				ines_mapper_writelow(&p_host->mapper, addr, val);
			else
				ines_mapper_writehigh(&p_host->mapper, addr, val);
		}
		break;
	}
}

void ines_host_write_sram_raw(ines_host_t* p_host, ines_word_t addr, ines_byte_t  val)
{
	p_host->SRAM[addr] = val;
	p_host->SRAM_used[addr>>13] = 1; // used SRAM BLOCK flag
	p_host->SRAM_write_flag = 1; // modified
}


#pragma pack(push, 1)
struct _ines_save_header_
{
	ines_dword_t   dwMagic;
	ines_dword_t   dwPROMCRC32;
	ines_byte_t    btBlocks;
	ines_byte_t    btMask;
	ines_byte_t    reserved[6];
};

typedef struct _ines_save_header_   ines_save_header_t;

#define INES_SAVE_HEADER_MAGIC  0x56415349  // 'ISAV'


#pragma pack(pop)


int bits_count(unsigned int n)
{
	int c = 0;
	while(n != 0)
	{
		n &= n-1;
		c++;
	}
	return c;
}

void ines_host_load_sram(ines_host_t* p_host, ines_cstr_t szRAMFileName)
{
	int n, sz;
	FILE* fSave;
	//ines_char_t szPath[4096];
	ines_save_header_t  header;
	
	//getSavePath(szPath, count_of(szPath));

	fSave = _tfopen(szRAMFileName, ISTR("rb"));
	if(fSave == NULL)
	{
		if(errno != ENOENT)
		{
			INES_LOG(LOG_WAR, MOD_SYS, ISTR("Load SRAM file \'%s\' failed (%d) %s\n"), szRAMFileName, errno, _tcserror(errno));
		}
		return;
	}

	do {

		fseek(fSave, 0, SEEK_END);
		sz = ftell(fSave);
		fseek(fSave, 0, SEEK_SET);

		if(1 != fread( &header, sizeof(header), 1, fSave ))
		{
			INES_LOG(LOG_WAR, MOD_SYS, ISTR("Load SRAM file \'%s\' failed (%d) %s\n"), szRAMFileName, errno, _tcserror(errno));
			break;
		}

		if(header.dwMagic != INES_SAVE_HEADER_MAGIC)
		{
			INES_LOG(LOG_WAR, MOD_SYS, ISTR("Load SRAM file \'%s\' failed : invalid file header!\n"), szRAMFileName);
			break;
		}

		if(header.dwPROMCRC32 != p_host->rom.crc32_p)
		{
			INES_LOG(LOG_WAR, MOD_SYS, ISTR("Load SRAM file \'%s\' failed : unmatched crc32!\n"), szRAMFileName);
			break;
		}

		n = bits_count(header.btMask);
		if(n != header.btBlocks || sz != 0x2000 * n + sizeof(header))
		{
			INES_LOG(LOG_WAR, MOD_SYS, ISTR("Load SRAM file \'%s\' failed : invalid blocks!\n"), szRAMFileName);
			break;
		}
		for(n = 0; n < 8; n++)
		{
			if(header.btMask & (1<<n))
			{
				if(1 != fread(p_host->SRAM+n*0x2000, 0x2000, 1, fSave))
				{
					INES_LOG(LOG_WAR, MOD_SYS, ISTR("Load SRAM file \'%s\' failed (%d) %s\n"), szRAMFileName, errno, _tcserror(errno));
				}
			}
		}

	} while(0);
	fclose(fSave);

	INES_LOG(LOG_INF, MOD_SYS, ISTR("Load SRAM file \'%s\' ok!\n"), szRAMFileName);

}

void ines_host_save_sram(ines_host_t* p_host, ines_cstr_t szRAMFileName)
{
	int n;
	FILE* fSave;
	//ines_char_t szPath[4096];
	ines_save_header_t  header;

	if(p_host->SRAM_write_flag != 1 || p_host->rom.has_sram == 0)
		return;

	//getSavePath(szPath, count_of(szPath));

	fSave = _tfopen(szRAMFileName, ISTR("wb"));
	if(fSave == NULL)
	{
		INES_LOG(LOG_WAR, MOD_SYS, ISTR("Save SRAM to '%s' failed (%d) %s\n"), szRAMFileName, errno, _tcserror(errno));
		return;
	}

	memset(&header, 0, sizeof(header));
	header.dwMagic = INES_SAVE_HEADER_MAGIC;
	header.dwPROMCRC32 = p_host->rom.crc32_p;
	header.btBlocks = 0;
	header.btMask = 0;
	for(n = 0; n < NES_MAX_SRAM_BANKS; n++)
	{
		if(p_host->SRAM_used[n]==1)
		{
			header.btBlocks++;
			header.btMask |= (1<<n);
		}
	}

	fwrite(&header,   sizeof(header), 1, fSave);

	for(n = 0; n < NES_MAX_SRAM_BANKS; n++)
	{
		if(p_host->SRAM_used[n]==1)
		{
			fwrite(p_host->SRAM + (n<<13),  (1<<13) , 1,  fSave);
		}
	}

	fclose(fSave);
	INES_LOG(LOG_DBG, MOD_SYS, ISTR("Save SRAM file \'%s\' ok!\n"), szRAMFileName);

	p_host->SRAM_write_flag = 2; // saved
}


#define ines_assert(b)   if(!(b)) { INES_LOG(LOG_ERR, MOD_SYS, ISTR("ines_assert(") ISTR(#b) ISTR(") failed!\n") ); return;  }

#define ines_validate_prom_bank(n, bn)    \
	do {  (bn) &=  p_host->prom_8k_mask;   \
		if( (bn) >= p_host->prom_8k_num ) { \
			INES_LOG(LOG_ERR, MOD_SYS, ISTR("Illegal PROM Bank Switch - bank[%d]=%d.\n"), (n), (bn) ); \
			return; \
		}  \
	} while(0)

#define ines_validate_vrom_bank(n, bn)    \
	do {  (bn) &=  p_host->vrom_1k_mask;   \
		if( (bn) >= p_host->vrom_1k_num ) { \
			INES_LOG(LOG_ERR, MOD_SYS, ISTR("Illegal VROM Bank Switch - bank[%d]=%d.\n"), (n), (bn) ); \
			return; \
		}  \
	} while(0)

#define ines_validate_sram_bank(n, bn)    \
	do {  (bn) &=  NES_SRAM_8K_MASK;       \
		if( (bn) >= NES_MAX_SRAM_BANKS ) {  \
			INES_LOG(LOG_ERR, MOD_SYS, ISTR("Illegal SRAM Bank Switch - bank[%d]=%d.\n"), (n), (bn) ); \
			return; \
		}  \
	} while(0)


#define ines_validate_vram_bank(n, bn)    \
	do {  (bn) &=  NES_VRAM_1K_MASK;       \
		if( (bn) >= NES_MAX_VRAM_BANKS ) {  \
			INES_LOG(LOG_ERR, MOD_SYS, ISTR("Illegal VRAM Bank Switch - bank[%d]=%d.\n"), (n), (bn) ); \
			return; \
		}  \
	} while(0)

void ines_set_prom_bank_4(ines_host_t* p_host, ines_word_t b4, ines_word_t b5, ines_word_t b6, ines_word_t b7)
{
	INES_LOG(LOG_DBG, MOD_SYS, ISTR("SET_PROM_BANKS(4-7)=(%d,%d,%d,%d)\n"), b4,b5,b6,b7);
	ines_validate_prom_bank(4, b4 );
	ines_validate_prom_bank(5, b5 );
	ines_validate_prom_bank(6, b6 );
	ines_validate_prom_bank(7, b7 );
	p_host->cpu.mem_bank[4] = p_host->rom.pPROMs + (b4 << 13);
	p_host->cpu.mem_bank[5] = p_host->rom.pPROMs + (b5 << 13);
	p_host->cpu.mem_bank[6] = p_host->rom.pPROMs + (b6 << 13);
	p_host->cpu.mem_bank[7] = p_host->rom.pPROMs + (b7 << 13);
	p_host->cpu.bank_writeable[4] &= ~NES_BANK_WRITE_ABLE;  // 指出此块内存为只读
	p_host->cpu.bank_writeable[5] &= ~NES_BANK_WRITE_ABLE;
	p_host->cpu.bank_writeable[6] &= ~NES_BANK_WRITE_ABLE;
	p_host->cpu.bank_writeable[7] &= ~NES_BANK_WRITE_ABLE;
}

void ines_set_prom_bank_5(ines_host_t* p_host, ines_word_t b3, ines_word_t b4, ines_word_t b5, ines_word_t b6, ines_word_t b7)
{
	INES_LOG(LOG_DBG, MOD_SYS, ISTR("SET_PROM_BANKS(3-7)=(%d,%d,%d,%d,%d)\n"), b3,b4,b5,b6,b7);
	ines_validate_prom_bank(3, b3);
	ines_validate_prom_bank(4, b4);
	ines_validate_prom_bank(5, b5);
	ines_validate_prom_bank(6, b6);
	ines_validate_prom_bank(7, b7);
	p_host->cpu.mem_bank[3] = p_host->rom.pPROMs + (b3 << 13); // 这里RAM不可写
	p_host->cpu.mem_bank[4] = p_host->rom.pPROMs + (b4 << 13);
	p_host->cpu.mem_bank[5] = p_host->rom.pPROMs + (b5 << 13);
	p_host->cpu.mem_bank[6] = p_host->rom.pPROMs + (b6 << 13);
	p_host->cpu.mem_bank[7] = p_host->rom.pPROMs + (b7 << 13);
	p_host->cpu.bank_writeable[3] &= ~NES_BANK_WRITE_ABLE;   // 指出此块内存为只读
	p_host->cpu.bank_writeable[4] &= ~NES_BANK_WRITE_ABLE;
	p_host->cpu.bank_writeable[5] &= ~NES_BANK_WRITE_ABLE;
	p_host->cpu.bank_writeable[6] &= ~NES_BANK_WRITE_ABLE;
	p_host->cpu.bank_writeable[7] &= ~NES_BANK_WRITE_ABLE;
}

void ines_set_prom_bank_n(ines_host_t* p_host, ines_word_t n, ines_word_t bn)
{
	INES_LOG(LOG_DBG, MOD_SYS, ISTR("SET_PROM_BANK(%d)=(%d)\n"), n, bn);
	// n = 3~7, bn=b3~b7
	ines_assert(3 <= n && n <= 7);
	ines_validate_prom_bank(n, bn);
	p_host->cpu.mem_bank[n] = p_host->rom.pPROMs + (bn << 13);
	p_host->cpu.bank_writeable[n] &= ~NES_BANK_WRITE_ABLE;   // 指出此块内存为只读

}

void ines_set_sram_bank_n(ines_host_t* p_host, ines_word_t n, ines_word_t bn)
{
	INES_LOG(LOG_DBG, MOD_SYS, ISTR("SET_SRAM_BANK(%d)=(%d)\n"), n, bn);
	// sram bank n 3~6, bn=0~7
	ines_assert(3 <= n && n <= 6);
	ines_validate_sram_bank(n, bn);
	p_host->cpu.mem_bank[n] = p_host->SRAM + (bn<<13); // 这里RAM可写
	p_host->cpu.bank_writeable[n] |= NES_BANK_WRITE_ABLE;   // 指出此块内存为可写入
	p_host->SRAM_used[bn] = 1;  // indicated which 8K SRAM is used. 
}


void ines_set_vrom_bank_8(ines_host_t* p_host, ines_word_t b0, ines_word_t b1, ines_word_t b2, ines_word_t b3, 
						  ines_word_t b4, ines_word_t b5, ines_word_t b6, ines_word_t b7)
{
	if(p_host->ppu.in_vblank)
		INES_LOG(LOG_DBG, MOD_SYS, ISTR("SET_VROM_BANKS(0-7)=(%d,%d,%d,%d,%d,%d,%d,%d), VBLANK\n"), b0,b1,b2,b3,b4,b5,b6,b7);
	else
		INES_LOG(LOG_DBG, MOD_SYS, ISTR("SET_VROM_BANKS(0-7)=(%d,%d,%d,%d,%d,%d,%d,%d), SCANLINE=%d\n"), b0,b1,b2,b3,b4,b5,b6,b7, p_host->ppu.current_line);
	ines_validate_vrom_bank(0, b0);
	ines_validate_vrom_bank(1, b1);
	ines_validate_vrom_bank(2, b2);
	ines_validate_vrom_bank(3, b3);
	ines_validate_vrom_bank(4, b4);
	ines_validate_vrom_bank(5, b5);
	ines_validate_vrom_bank(6, b6);
	ines_validate_vrom_bank(7, b7);

	p_host->ppu.mem_bank[0] = p_host->rom.pVROMs + (b0<<10);
	p_host->ppu.mem_bank[1] = p_host->rom.pVROMs + (b1<<10);
	p_host->ppu.mem_bank[2] = p_host->rom.pVROMs + (b2<<10);
	p_host->ppu.mem_bank[3] = p_host->rom.pVROMs + (b3<<10);
	p_host->ppu.mem_bank[4] = p_host->rom.pVROMs + (b4<<10);
	p_host->ppu.mem_bank[5] = p_host->rom.pVROMs + (b5<<10);
	p_host->ppu.mem_bank[6] = p_host->rom.pVROMs + (b6<<10);
	p_host->ppu.mem_bank[7] = p_host->rom.pVROMs + (b7<<10);
	p_host->ppu.pattern_type[0] = 1;
	p_host->ppu.pattern_type[1] = 1;
	p_host->ppu.pattern_type[2] = 1;
	p_host->ppu.pattern_type[3] = 1;
	p_host->ppu.pattern_type[4] = 1;
	p_host->ppu.pattern_type[5] = 1;
	p_host->ppu.pattern_type[6] = 1;
	p_host->ppu.pattern_type[7] = 1;
}

void ines_set_vrom_bank_n(ines_host_t* p_host, ines_word_t n, ines_word_t bn)
{
	if(p_host->ppu.in_vblank)
		INES_LOG(LOG_DBG, MOD_SYS, ISTR("SET_VROM_BANK(%d)=(%d), VBLANK\n"), n,bn);
	else
		INES_LOG(LOG_DBG, MOD_SYS, ISTR("SET_VROM_BANK(%d)=(%d), SCANLINE=%d\n"), n,bn, p_host->ppu.current_line);
	// n=0~8, bn = b0~b8
	ines_assert(n < 8);
	ines_validate_vrom_bank(n, bn);
	p_host->ppu.mem_bank[n] = p_host->rom.pVROMs + (bn<<10);
	p_host->ppu.pattern_type[n] = 1;
}

void ines_set_vram_bank_n(ines_host_t* p_host, ines_word_t n, ines_word_t bn)
{
	if(p_host->ppu.in_vblank)
		INES_LOG(LOG_DBG, MOD_SYS, ISTR("SET_VRAM_BANK(%d)=(%d), VBLANK\n"), n,bn);
	else
		INES_LOG(LOG_DBG, MOD_SYS, ISTR("SET_VRAM_BANK(%d)=(%d), SCANLINE=%d\n"), n,bn, p_host->ppu.current_line);

	ines_assert(n < 8);
	ines_validate_vram_bank(n, bn);
	// sram bank point
	p_host->ppu.mem_bank[n] = p_host->ppu.pattern_table + (bn<<10);
	p_host->ppu.pattern_type[n] = 0;
	p_host->ppu.pattern_table_used[bn] = 1;
}

/**
 * 把 PPU 的一个 1KB 窗口指向卡带内部的 NT RAM(即 CIRAM)页。
 * @param p_host 宿主
 * @param n      PPU 窗口号(0-7，对应 $0000-$1FFF 的 pattern 区)
 * @param page   NT RAM 的 1KB 页号(内部 2KB -> 0-1)
 * @note 供 Namco 163(mapper 19) 等支持"内部 nametable RAM 当作 CHR"的 ASIC 使用。
 *       该窗口的 pattern_type 记为 2：$2007 写入仍会落到这片 NT RAM(即可当 CHR-RAM 使用)，
 *       读取则直接读 NT RAM，因此 CIRAM 同时被当作 nametable 与 pattern 内存。
 */
void ines_set_ciram_pattern_bank_n(ines_host_t* p_host, ines_word_t n, ines_word_t page)
{
	if(p_host->ppu.in_vblank)
		INES_LOG(LOG_DBG, MOD_SYS, ISTR("SET_CIRAM_PATTERN_BANK(%d)=(%d), VBLANK\n"), n, page);
	else
		INES_LOG(LOG_DBG, MOD_SYS, ISTR("SET_CIRAM_PATTERN_BANK(%d)=(%d), SCANLINE=%d\n"), n, page, p_host->ppu.current_line);

	ines_assert(n < NES_MAX_PTMEM_BANKS);
	page &= (NES_MAX_NTRAM_BANKS - 1);
	p_host->ppu.mem_bank[n] = p_host->ppu.name_table + (page << 10);
	p_host->ppu.pattern_type[n] = 2;
}

/**
 * 把一个 nametable 窗口($2000/$2400/$2800/$2C00)指向卡带 CHR 的 1KB 页。
 * @param p_host 宿主
 * @param n      nametable 窗口号(0-3，对应 PPU 窗口 8-11，即 $2000-$2FFF 的四个 1KB 段)
 * @param bn     CHR 1KB 页号(0-255，按卡带 CHR 容量取模)
 * @note 供 Namco 163(mapper 19) 的 ROM nametable 特性使用：CHR 页可直接当作 nametable 读取。
 *       目标随卡带配置选择——有 CHR-ROM 时指向 CHR-ROM(nt_type = 1，只读，$2007 写入被忽略)；
 *       纯 CHR-RAM 卡带指向 pattern RAM(nt_type = 2，可写，并标记该页"已使用"以便即时存档保存内容)。
 *       需要窗口回到内部 CIRAM 时调用 ines_ppu_set_mirror()，它会把 4 个窗口的 nt_type 复位为 0。
 */
void ines_set_nt_chr_bank_n(ines_host_t* p_host, ines_word_t n, ines_word_t bn)
{
	if(p_host->ppu.in_vblank)
		INES_LOG(LOG_DBG, MOD_SYS, ISTR("SET_NT_CHR_BANK(%d)=(%d), VBLANK\n"), n, bn);
	else
		INES_LOG(LOG_DBG, MOD_SYS, ISTR("SET_NT_CHR_BANK(%d)=(%d), SCANLINE=%d\n"), n, bn, p_host->ppu.current_line);

	ines_assert(n < NES_MAX_NTRAM_BANKS);

	if(p_host->vrom_1k_num > 0)
	{
		bn &= p_host->vrom_1k_mask;
		if(bn >= p_host->vrom_1k_num)
			bn %= p_host->vrom_1k_num;      // 非 2 的幂 CHR 容量：回卷，避免越界读
		p_host->ppu.mem_bank[0x08 + n] = p_host->rom.pVROMs + ((ines_dword_t)bn << 10);
		p_host->ppu.nt_type[n] = 1;
	}
	else
	{
		bn &= NES_VRAM_1K_MASK;
		p_host->ppu.mem_bank[0x08 + n] = p_host->ppu.pattern_table + ((ines_dword_t)bn << 10);
		p_host->ppu.pattern_table_used[bn] = 1;
		p_host->ppu.nt_type[n] = 2;
	}
}

/**
 * 把一个 nametable 窗口($2000/$2400/$2800/$2C00)指向 PPU pattern RAM(CHR-RAM)的 1KB 页。
 * @param p_host 宿主
 * @param n      nametable 窗口号(0-3，对应 PPU 窗口 8-11，即 $2000-$2FFF 的四个 1KB 段)
 * @param bn     pattern RAM 的 1KB 页号(0-31，按 32KB 回卷)
 * @note 供 CHR 为 RAM 但镜像里带 CHR 数据的卡带使用(如 mapper 17 Super Magic Card)：
 *       此时卡带把镜像的 CHR 数据拷进 pattern RAM，nametable 也必须指向这片 RAM(nt_type = 2，可写)，
 *       而 ines_set_nt_chr_bank_n() 在有 CHR-ROM 时会固定指向 CHR-ROM(nt_type = 1，只读)。
 *       需要窗口回到内部 CIRAM 时调用 ines_ppu_set_mirror()，它会把 4 个窗口的 nt_type 复位为 0。
 */
void ines_set_nt_pattern_bank_n(ines_host_t* p_host, ines_word_t n, ines_word_t bn)
{
	if(p_host->ppu.in_vblank)
		INES_LOG(LOG_DBG, MOD_SYS, ISTR("SET_NT_PATTERN_BANK(%d)=(%d), VBLANK\n"), n, bn);
	else
		INES_LOG(LOG_DBG, MOD_SYS, ISTR("SET_NT_PATTERN_BANK(%d)=(%d), SCANLINE=%d\n"), n, bn, p_host->ppu.current_line);

	ines_assert(n < NES_MAX_NTRAM_BANKS);

	bn &= NES_VRAM_1K_MASK;
	p_host->ppu.mem_bank[0x08 + n] = p_host->ppu.pattern_table + ((ines_dword_t)bn << 10);
	p_host->ppu.pattern_table_used[bn] = 1;
	p_host->ppu.nt_type[n] = 2;
}


#pragma pack(push, 1)
// 24 bytes
struct _ines_state_header_
{
	/* 0 - 7 : 8 bytes */
	ines_dword_t   dwMagic;
	ines_word_t    wSize;      // struct size
	ines_word_t    wVersion;   // 
	/* 8 - 15 : 8 bytes */
	ines_dword_t   dwSaveTime;
	ines_dword_t   dwPROMCRC32;
	/* 16 - 23 : 8 bytes */
	ines_byte_t    btNTSC;   // NTSC: 1 ; PAL: 0
	ines_byte_t    mapperid;  // for verifyed
	ines_byte_t    PROM_16k_blocks;  // for verifyed
	ines_byte_t    VROM_8k_blocks;  // for verifyed
	ines_byte_t    bReserved[4];
};
/* total : 24 bytes */

typedef struct _ines_state_header_   ines_state_header_t;

#define INES_STATE_HEADER_MAGIC    0x41545349
#define INES_STATE_HEADER_SIZE     sizeof(ines_state_header_t)
#define INES_STATE_HEADER_VERSION  1

// 24 bytes
struct _ines_state_host_data_
{
	/* 0 - 7 : 8 bytes */
	ines_byte_t    SRAM_used;  // each bit indicated a 8k block is used
	ines_byte_t    frame_irq_enabled;
	ines_byte_t    frame_irq_disenabled;
	ines_byte_t    DMA_high;
	ines_int_t     cpu_cycles;
	/* 8 - 15 : 8 bytes */
	ines_int64_t   base_cycles_1000;
	/* 16 - 23 : 8 bytes */
	ines_int64_t   frame_count;
};
/* total : 24 bytes */

/// ///
typedef struct _ines_state_host_data_   ines_state_host_data_t;


#pragma pack(pop)


/************************************************************************
 title.st0~title.st9
 format:
 0         0x10        state_head
 0x10      0x30        state_host_data
 0x40      0x2000 * n  8K_sram[n] (0x2000 * sram_used_count)
 0x40 + n * 0x2000     mapper_data[len]
                       cpu_data
					   ppu_data
					   VRAM
					   apu_data
					   pulse_data1
					   pulse_data2
					   triangle_data
					   noise_data
					   dmc_data
					   
                       
 ************************************************************************/

// extern ines_cstr_t getStatePath(ines_cstr_t title, int index, ines_str_t szPath, size_t szLen);

ines_int_t  ines_save_state(ines_host_t* p_host, FILE* fSave)
{
	ines_state_header_t   header;


	memset(&header, 0, sizeof(header));
	header.dwMagic = INES_STATE_HEADER_MAGIC;
	header.wSize = INES_STATE_HEADER_SIZE;
	header.wVersion = INES_STATE_HEADER_VERSION;
	header.dwSaveTime = (ines_dword_t)time(NULL);
	header.dwPROMCRC32 = p_host->rom.crc32_p;
	header.btNTSC = p_host->setting.is_ntsc;
	header.mapperid = p_host->rom.mapper_num;
	header.PROM_16k_blocks = p_host->rom.PROM_block_num;
	header.VROM_8k_blocks = p_host->rom.VROM_block_num;


	fwrite(&header, sizeof(header), 1, fSave);


	ines_host_save_state(p_host, fSave);
	ines_mapper_save_state(&p_host->mapper, fSave);
	ines_cpu_save_state(&p_host->cpu, fSave);
	ines_ppu_save_state(&p_host->ppu, fSave);
	ines_apu_save_state(&p_host->apu, fSave);
	ines_joypad_save_state(&p_host->joypad, fSave);

	return 0;
}

ines_int_t ines_check_state_time(ines_host_t* p_host, FILE* fSave)
{
	ines_state_header_t   header;
	
	if(1 != fread(&header, sizeof(header), 1, fSave))
		return -1;

	if(header.dwMagic !=  INES_STATE_HEADER_MAGIC)
		return -1;
	if(header.dwPROMCRC32 != p_host->rom.crc32_p)
		return -1;

	return header.dwSaveTime;
}

ines_int_t ines_host_save_state(ines_host_t* p_host, FILE* fSave)
{
	int n;
	ines_state_host_data_t   data;
	memset(&data, 0, sizeof(data));
	if(p_host->SRAM_write_flag)
	{
		for(n = 0; n < NES_MAX_SRAM_BANKS; n++)
		{
			if(p_host->SRAM_used[n]==1)
			{
				data.SRAM_used |= (1<<n);
			}
		}
	}
	else
	{
		data.SRAM_used = 0;
	}

	data.frame_irq_enabled = p_host->frame_irq_enabled;
	data.frame_irq_disenabled = p_host->frame_irq_disenabled;
	data.DMA_high = p_host->DMA_high;
	data.cpu_cycles = p_host->cpu_cycles;
	data.base_cycles_1000 = (ines_int64_t)(p_host->base_cycles * 1000.0);
	data.frame_count = p_host->frame_count;

	fwrite(&data, sizeof(data), 1, fSave);

	for(n = 0; n < NES_MAX_SRAM_BANKS; n++)
	{
		if(data.SRAM_used & (1<<n))
		{
			fwrite(p_host->SRAM + (n<<13),  (1<<13), 1, fSave);
		}
	}

	return 0;
}

ines_int_t ines_host_load_state(ines_host_t* p_host, FILE* fSave)
{
	int n;
	ines_state_host_data_t   data;
	//memset(&data, 0, sizeof(data));

	if(1 != fread(&data, sizeof(data), 1, fSave))
		return -1;

	memset(p_host->SRAM, 0, sizeof(p_host->SRAM));

	for(n = 0; n < NES_MAX_SRAM_BANKS; n++)
	{
		if(data.SRAM_used & (1<<n))
		{
			p_host->SRAM_used[n] = 1;
			if(1 != fread(p_host->SRAM + (n<<13),  (1<<13), 1, fSave))
				return -1;
		}
	}

	p_host->SRAM_write_flag = data.SRAM_used != 0 ? 1 : 0;


	p_host->frame_irq_enabled = data.frame_irq_enabled;
	p_host->frame_irq_disenabled = data.frame_irq_disenabled;
	p_host->DMA_high = data.DMA_high;
	p_host->cpu_cycles = data.cpu_cycles;
	p_host->base_cycles = data.base_cycles_1000 / 1000.0;
	p_host->frame_count = data.frame_count;

	return 0;
}


ines_int_t ines_load_state(ines_host_t* p_host, FILE* fSave)
{
	ines_state_header_t   header;

	if(1 != fread(&header, sizeof(header), 1, fSave))
		return -1;

	if(header.dwMagic !=  INES_STATE_HEADER_MAGIC)
		return -1;

	if(header.dwPROMCRC32 != p_host->rom.crc32_p)
		return -1;

	if(header.mapperid != p_host->rom.mapper_num)
		return -1;

	if(header.PROM_16k_blocks != p_host->rom.PROM_block_num)
		return -1;

	if(header.VROM_8k_blocks != p_host->rom.VROM_block_num)
		return -1;

	ines_host_init_setting(p_host, header.btNTSC);

	if (0 != ines_host_load_state(p_host, fSave) ||
		0 != ines_mapper_load_state(&p_host->mapper, fSave) ||
		0 != ines_cpu_load_state(&p_host->cpu, fSave) ||
		0 != ines_ppu_load_state(&p_host->ppu, fSave) ||
		0 != ines_apu_load_state(&p_host->apu, fSave) ||
		0 != ines_joypad_load_state(&p_host->joypad, fSave) )
	{
		return -1;
	}

	return 0;
}
