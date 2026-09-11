
#include "../comm/idef.h"
#include "../comm/log.h"
#include "nes.h"
#include "mapper.h"
#include "ppu.h"


#define VRAM(p_ppu, addr)   ( (p_ppu)->mem_bank[ (addr) >> 10 ][ (addr) & 0x3ff ] )

// 初始化
void ines_ppu_init(ines_ppu_t* p_ppu)
{
	
}

// 删除
void ines_ppu_free(ines_ppu_t* p_ppu)
{


}

// 软件复位
void ines_ppu_reset(ines_ppu_t* p_ppu)
{
	p_ppu->reg_ctrl_1 = 0;
	p_ppu->reg_ctrl_2 = 0;
	p_ppu->reg_status = 0;
	//p_ppu->reg_screen_offset_x = 0;
	//p_ppu->reg_screen_offset_y = 0;
	p_ppu->reg_spr_addr = 0;
	//p_ppu->reg_VRAM_addr = 0;
	p_ppu->read_2007_buffer = 0;
	p_ppu->toggle_2005_2006 = 0;
	//p_ppu->toggle_VRAM_addr_write = 0;
	//p_ppu->used_vrom = 0;
	p_ppu->in_vblank = 0;
	p_ppu->index_t = 0;
	p_ppu->index_v = 0;
	p_ppu->index_x = 0;
	p_ppu->current_line = 0;

	memset(p_ppu->bg_pal, 0, sizeof(p_ppu->bg_pal));
	memset(p_ppu->sp_pal, 0, sizeof(p_ppu->sp_pal));
	memset(p_ppu->sp_RAM, 0, sizeof(p_ppu->sp_RAM));
	memset(p_ppu->name_table, 0, sizeof(p_ppu->name_table));
	memset(p_ppu->pattern_table, 0, sizeof(p_ppu->pattern_table));
	memset(p_ppu->pattern_type, 0, sizeof(p_ppu->pattern_type));
	memset(p_ppu->nt_type, 0, sizeof(p_ppu->nt_type));
	memset(p_ppu->pattern_table_used, 0, sizeof(p_ppu->pattern_table_used));

	// reset banks . 先不进行初始化，等mapper初始化后，如果没有设置pattern块则自动使用RAM方式
	p_ppu->mem_bank[0x00] = NULL; //p_ppu->pattern_table + (0x00 << 10);
	p_ppu->mem_bank[0x01] = NULL; //p_ppu->pattern_table + (0x01 << 10);
	p_ppu->mem_bank[0x02] = NULL; //p_ppu->pattern_table + (0x02 << 10);
	p_ppu->mem_bank[0x03] = NULL; //p_ppu->pattern_table + (0x03 << 10);
	p_ppu->mem_bank[0x04] = NULL; //p_ppu->pattern_table + (0x04 << 10);
	p_ppu->mem_bank[0x05] = NULL; //p_ppu->pattern_table + (0x05 << 10);
	p_ppu->mem_bank[0x06] = NULL; //p_ppu->pattern_table + (0x06 << 10);
	p_ppu->mem_bank[0x07] = NULL; //p_ppu->pattern_table + (0x07 << 10);

	p_ppu->mem_bank[0x08] = p_ppu->name_table;  // to name table #0
	p_ppu->mem_bank[0x09] = p_ppu->name_table;  // to name table #0
	p_ppu->mem_bank[0x0a] = p_ppu->name_table;  // to name table #0
	p_ppu->mem_bank[0x0b] = p_ppu->name_table;  // to name table #0
}


void ines_ppu_set_mirror(ines_ppu_t* p_ppu, ines_byte_t  n0, ines_byte_t  n1, ines_byte_t  n2, ines_byte_t  n3)
{
	INES_LOG(LOG_DBG, MOD_PPU, ISTR("SET_PPU_MIRROR:(%d,%d,%d,%d)\n"), n0,n1,n2,n3);
	p_ppu->mem_bank[0x08] = p_ppu->name_table + ((n0 & 0x3) << 10);  // to name table #n0
	p_ppu->mem_bank[0x09] = p_ppu->name_table + ((n1 & 0x3) << 10);  // to name table #n1
	p_ppu->mem_bank[0x0a] = p_ppu->name_table + ((n2 & 0x3) << 10);  // to name table #n2
	p_ppu->mem_bank[0x0b] = p_ppu->name_table + ((n3 & 0x3) << 10);  // to name table #n3
	// 这 4 个窗口全部回到内部 CIRAM(可写)，清掉可能残留的"CHR 页当 nametable"标记
	p_ppu->nt_type[0] = 0;
	p_ppu->nt_type[1] = 0;
	p_ppu->nt_type[2] = 0;
	p_ppu->nt_type[3] = 0;
}

void ines_ppu_set_mirror_type(ines_ppu_t* p_ppu, ines_byte_t mt)
{
	switch(mt& 0x3)
	{
	case MIRROR_SINGLE_SCREEN:  ines_ppu_set_mirror(p_ppu, 0,0,0,0);  break;
	case MIRROR_VERT:           ines_ppu_set_mirror(p_ppu, 0,1,0,1);  break;
	case MIRROR_HORZ:           ines_ppu_set_mirror(p_ppu, 0,0,1,1);  break;
	case MIRROR_FOUR_SCREEN:    ines_ppu_set_mirror(p_ppu, 0,1,2,3);  break;
	}
}


static ines_byte_t read2007(ines_ppu_t* p_ppu)
{
	ines_byte_t  temp;
	ines_word_t  addr;

	addr = p_ppu->index_v & 0x3fff;

	// VRAM access inc with ctrlreg1.2 , when set means write with vert
	p_ppu->index_v += (p_ppu->reg_ctrl_1 & 0x04) ? 32 : 1;


	if(addr  >= 0x3f00)
	{
		// if read 
		if(addr & 0x0010)
		{
			return p_ppu->sp_pal[addr & 0xf];
		}
		else
		{
			return p_ppu->bg_pal[addr & 0xf];
		}

	}
	else if(addr >= 0x3000)
	{
		addr &= 0xefff;
	}
	temp = p_ppu->read_2007_buffer;
	p_ppu->read_2007_buffer = VRAM(p_ppu, addr);
	return temp;
}

static void write2007(ines_ppu_t* p_ppu, ines_byte_t  val)
{
	ines_word_t  addr;

	addr = p_ppu->index_v & 0x3fff;
	p_ppu->index_v += (p_ppu->reg_ctrl_1 & PPU_RW_VERT) ? 32 : 1;

	INES_LOG(LOG_DBG, MOD_PPU, ISTR("WRITE_PPU_VRAM($%04X)=$%02X\n"), addr, val);

	if(addr  >= 0x3f00)
	{
		val &= 0x3f;
		if( (addr & 0xf) == 0)
		{
			p_ppu->bg_pal[0] = val;
			p_ppu->sp_pal[0] = val;
		}
		else if(addr & 0x0010)
		{
			p_ppu->sp_pal[addr & 0xf] = val;
		}
		else
		{
			p_ppu->bg_pal[addr & 0xf] = val;
		}
		return;
	}
	else if(addr >= 0x3000)
	{
		addr &= 0xefff;
	}

	// pattern_type: 0=VRAM(可写) 1=VROM(只读) 2=卡带内部 NT RAM 当作 CHR(可写，mapper 19)
	// nt_type:      0=CIRAM(可写) 1=CHR-ROM 页(只读，Namco 163 的 ROM nametable) 2=CHR-RAM 页(可写)
	if(addr >= 0x2000)
	{
		if(p_ppu->nt_type[(addr >> 10) & 0x03] != 1)
		{
			VRAM(p_ppu, addr) = val;
		}
	}
	else if(p_ppu->pattern_type[addr>>10] != 1)
	{
		VRAM(p_ppu, addr) = val;
	}
}



ines_byte_t  ines_ppu_readlow(ines_ppu_t* p_ppu, ines_word_t addr)
{
	ines_byte_t bt = 0;
	switch(addr)
	{
	case 0x2000:   // ppu control reg 1
		bt = p_ppu->reg_ctrl_1;
		break;
	case 0x2001:   // ppu control reg 2
		bt = p_ppu->reg_ctrl_2;
		break;
	case 0x2002:   // ppu status reg
		bt = p_ppu->reg_status;
		p_ppu->reg_status &= ~PPU_STATUS_ENTER_VBLANK; // remove v_blank flag;
		p_ppu->toggle_2005_2006 = 0;
		break;
	case 0x2004:   // spr ram data
		bt = p_ppu->sp_RAM[p_ppu->reg_spr_addr++];
		break;
	case 0x2007:   // VRAM data
		bt = /* p_ppu->read_2007_buffer;
		p_ppu->read_2007_buffer = */ read2007(p_ppu);
		break;
	}
	return bt;
}



void  ines_ppu_writelow(ines_ppu_t* p_ppu, ines_word_t addr, ines_byte_t val)
{
	if(p_ppu->in_vblank)
		INES_LOG(LOG_DBG, MOD_PPU, ISTR("PPU_WRITE($%04X)=#$%02X, VBLANK\n"), addr, val);
	else
		INES_LOG(LOG_DBG, MOD_PPU, ISTR("PPU_WRITE($%04X)=#$%02X, SCANLINE=#%d\n"), addr, val, p_ppu->current_line);
	switch(addr)
	{
	case 0x2000:  // write ctrl reg 1
		//INES_LOG(LOG_DBG, MOD_PPU, ISTR(" Write CtrlReg#1 = $%02X"), val);
		if(!(p_ppu->reg_ctrl_1 & PPU_VBLANK_ENABLED) && (val & PPU_VBLANK_ENABLED)
			&&  (p_ppu->reg_status & PPU_STATUS_ENTER_VBLANK))
		{
			p_ppu->reg_ctrl_1 = val;
			// trigger vblank NMI ?
			// ines_cpu_NMI(&(ppu2host(p_ppu)->cpu));
		}
		else
		{
			p_ppu->reg_ctrl_1 = val;
		}
		// t:d10~11 = val:d1~d0
		p_ppu->index_t = (p_ppu->index_t & 0xf3ff) | (( (ines_word_t)val & 0x3 ) << 10);
		break;
	case 0x2001:
		//INES_LOG(LOG_DBG, MOD_PPU, ISTR(" Write CtrlReg#2 = $%02X"), val);
		p_ppu->reg_ctrl_2 = val;
		break;
	case 0x2002: // cannot write status reg
		break;
	case 0x2003:
		//INES_LOG(LOG_DBG, MOD_PPU, ISTR(" Write RegSprAddr$2003 = $%02X"), p_ppu->reg_spr_addr, val);
		p_ppu->reg_spr_addr = val;
		break;
	case 0x2004:
		INES_LOG(LOG_DBG, MOD_PPU, ISTR("WRITE_PPU_SPRAM($%02X)=$%02X\n"), p_ppu->reg_spr_addr, val);
		p_ppu->sp_RAM[p_ppu->reg_spr_addr++] = val;
		break;
	case 0x2005: // screen offset x, y
		//INES_LOG(LOG_DBG, MOD_PPU, ISTR(" Write RegOffset$2005 = $%02X"), val);
		/**/
		if(p_ppu->toggle_2005_2006 == 0)
		{
			p_ppu->toggle_2005_2006 = 1;
			// t:d4~d0 = val:d7~d3
			p_ppu->index_t = (p_ppu->index_t & 0xffe0) | ( ((ines_word_t)val & 0xf8) >> 3 );
			// x = val:d2~d0
			p_ppu->index_x = val & 0x07;
		}
		else
		{
			p_ppu->toggle_2005_2006 = 0;
			// t:d9~d5 = val:d7~d3
			// t:d14~d12 = val:d2~d0
			p_ppu->index_t = (p_ppu->index_t & 0x8c1f) | ( ((ines_word_t)val & 0xf8) << 2 ) | ( ((ines_word_t)val & 0x07) << 12 );
		}
		//*/
		break;
	case 0x2006: // 
		//INES_LOG(LOG_DBG, MOD_PPU, ISTR(" Write RegPRAMAddr$2006 = $%02X"), val);
		if(p_ppu->toggle_2005_2006 == 0)
		{
			p_ppu->toggle_2005_2006 = 1;
			p_ppu->index_t = (p_ppu->index_t & 0xff) | ((ines_word_t)(val & 0x3f) << 8);
//			p_ppu->byte_index = (p_ppu->byte_index & 0xf3ff) | ( ((ines_word_t)val & 0x0c) << 8 );
		}
		else
		{
			p_ppu->toggle_2005_2006 = 0;
			p_ppu->index_t = (p_ppu->index_t & 0xff00) | ((ines_word_t)(val));
			p_ppu->index_v = p_ppu->index_t;
		}
		break;
	case 0x2007:
		write2007(p_ppu, val);
		break;
	}
}



void ines_ppu_start_frame(ines_ppu_t* p_ppu)
{
	//INES_LOG(LOG_DBG, MOD_PPU, ISTR("ines_ppu_start_frame"));
	if(p_ppu->reg_ctrl_2 & (PPU_ENABLE_BG|PPU_ENABLE_SPR ))
	{
		p_ppu->index_v = p_ppu->index_t;
	}
	p_ppu->current_line = 0;
}


void ines_ppu_end_frame(ines_ppu_t* p_ppu)
{
	//INES_LOG(LOG_DBG, MOD_PPU, ISTR("ines_ppu_end_frame"));

}

void ines_ppu_start_vblank(ines_ppu_t* p_ppu)
{
	//INES_LOG(LOG_DBG, MOD_PPU, ISTR("ines_ppu_start_vblank"));
	p_ppu->in_vblank = 1;
	p_ppu->reg_status |= PPU_STATUS_ENTER_VBLANK;
	if(p_ppu->reg_ctrl_1 & PPU_VBLANK_ENABLED)
	{
		ines_cpu_NMI(&ppu2host(p_ppu)->cpu);
	}
}

void ines_ppu_end_vblank(ines_ppu_t* p_ppu)
{
	//INES_LOG(LOG_DBG, MOD_PPU, ISTR("ines_ppu_end_vblank"));
	p_ppu->in_vblank = 0;

	p_ppu->reg_status &= ~(PPU_STATUS_ENTER_VBLANK|PPU_STATUS_SPR_0_SCAN);
}


/*
bits 12-14 are the tile Y offset.
you can think of bits 5,6,7,8,9 as the "y scroll"(*8).  this functions
slightly different from the X.  it wraps to 0 and bit 11 is switched when
it's incremented from _29_ instead of 31.  there are some odd side effects
from this.. if you manually set the value above 29 (from either 2005 or
2006), the wrapping from 29 obviously won't happen, and attrib data will be
used as name table data.  the "y scroll" still wraps to 0 from 31, but
without switching bit 11.  this explains why writing 240+ to 'Y' in 2005
appeared as a negative scroll value.
*/
#define LOOPY_NEXT_LINE(v) \
{ \
	if((v & 0x7000) == 0x7000) /* is subtile y offset == 7? */ \
{ \
	v &= 0x8FFF; /* subtile y offset = 0 */ \
	if((v & 0x03E0) == 0x03A0) /* name_tab line == 29? */ \
{ \
	v ^= 0x0800;  /* switch nametables (bit 11) */ \
	v &= 0xFC1F;  /* name_tab line = 0 */ \
} \
	  else \
{ \
	if((v & 0x03E0) == 0x03E0) /* line == 31? */ \
{ \
	v &= 0xFC1F;  /* name_tab line = 0 */ \
} \
		else \
{ \
	v += 0x0020; \
} \
} \
} \
	else \
{ \
	v += 0x1000; /* next subtile y offset */ \
} \
}


static void render_bg(ines_ppu_t* p_ppu, ines_byte_t* p_line,  ines_byte_t* solid_flags);
static void render_spr(ines_ppu_t* p_ppu, ines_byte_t* p_line,  ines_byte_t* solid_flags);

// case p_line is NULL indicated don't draw really
void ines_ppu_render_line(ines_ppu_t* p_ppu, ines_byte_t* p_line)
{
	ines_byte_t     solid_flags[SCREEN_WIDTH];   // 测试当前行某个点是否已绘制背景，或者 后台精灵，或者前台精灵。
	ines_byte_t     dummy_line[SCREEN_WIDTH];   // 如果不输出，则使用一个假的输出缓冲。


	//INES_LOG(LOG_DBG, MOD_PPU, ISTR("ines_ppu_render_line: #%d"), p_ppu-> current_line);

	if(p_line == NULL)
		p_line = dummy_line;


	if((p_ppu->reg_ctrl_2 & PPU_ENABLE_BG) == 0)
	{
		memset(p_line, p_ppu->bg_pal[0], SCREEN_WIDTH);
	}
	
	if(p_ppu->reg_ctrl_2 & (PPU_ENABLE_BG|PPU_ENABLE_SPR))
	{
		memset(solid_flags, 0, sizeof(solid_flags));

		// reset x start and Name Table Addr
		p_ppu->index_v = (p_ppu->index_v & 0xFBE0) | (p_ppu->index_t & 0x041F);

		if(p_ppu->reg_ctrl_2 & PPU_ENABLE_BG)
		{
			render_bg(p_ppu, p_line, solid_flags);
		}
		if(p_ppu->reg_ctrl_2 & PPU_ENABLE_SPR)
		{
			render_spr(p_ppu, p_line, solid_flags);
		}
		LOOPY_NEXT_LINE(p_ppu->index_v);
	}
	p_ppu->current_line++;
}



static void render_bg(ines_ppu_t* p_ppu, ines_byte_t* p_line,  ines_byte_t* solid_flags)
{
	ines_word_t    nt_addr, attr_addr, pattern_addr, pattern_addr_base;
	ines_int_t     tile_x, tile_y;
	ines_byte_t    attr_bits, pattern_lo, pattern_hi, pattern_mask, cl;
	ines_int_t     pixel_x;
	ines_byte_t    MMC_attr;
 
	tile_x = p_ppu->index_v & 0x1F;
	tile_y = (p_ppu->index_v & 0x3E0) >> 5;
	nt_addr = 0x2000 + (p_ppu->index_v & 0x0FFF);
	attr_addr = 0x2000 + (p_ppu->index_v & 0x0C00) + 0x03C0 + ((tile_y & 0xFFFC) << 1) + ((tile_x >> 2 ));

	if(0 == (tile_y & 0x2))
	{
		if(0 == (tile_x &2))
		{
			attr_bits = (VRAM(p_ppu, attr_addr) & 0x3) << 2;
		}
		else
		{
			attr_bits = (VRAM(p_ppu, attr_addr) & 0xc);
		}
	}
	else
	{
		if(0 == (tile_x &2))
		{
			attr_bits = (VRAM(p_ppu, attr_addr) & 0x30) >> 2;
		}
		else
		{
			attr_bits = (VRAM(p_ppu, attr_addr) & 0xc0) >> 4;
		}
	}

	pixel_x = -(ines_int_t)p_ppu->index_x;
	// render up 33 tile
	pattern_addr_base = (p_ppu->reg_ctrl_1 & PPU_BG_MEM_MASK) ? 0x1000 : 0;

	while(pixel_x <= SCREEN_WIDTH)
	{
		pattern_addr = pattern_addr_base + ((ines_word_t)VRAM(p_ppu, nt_addr) << 4) + ((p_ppu->index_v & 0x7000) >> 12);

		MMC_attr = ines_mapper_PPU_latch(&(ppu2host(p_ppu)->mapper), nt_addr & 0x03ff, 1);


		if(MMC_attr != 0)
		{
			attr_bits = MMC_attr & 0x0c;
		}

		pattern_lo = VRAM(p_ppu, pattern_addr);
		pattern_hi = VRAM(p_ppu, pattern_addr + 8);

		// for MMC2/MMC4 Latch FD/FE
		ines_mapper_PPU_latch_FDFE(&ppu2host(p_ppu)->mapper, pattern_addr);

		// render 8 pixels
		for(pattern_mask = 0x80; pattern_mask; pattern_mask>>=1)
		{
			if(pixel_x >= 0 && pixel_x < SCREEN_WIDTH)
			{
				if((p_ppu->reg_ctrl_2 & PPU_MASK_BGLEFT8) == 0 && pixel_x < 8)
				{
					p_line[pixel_x] = p_ppu->bg_pal[0];
					solid_flags[pixel_x] = 0;
				}
				else
				{
					cl = attr_bits;
					if(pattern_lo & pattern_mask) cl |= 0x01;
					if(pattern_hi & pattern_mask) cl |= 0x02;

					if(0 == (cl & 0x3) )
					{
						p_line[pixel_x] = p_ppu->bg_pal[0];
						solid_flags[pixel_x] = 0;
					}
					else
					{
						if(p_ppu->reg_ctrl_2 & PPU_SINGCOLOR)
							p_line[pixel_x] = p_ppu->bg_pal[cl] & 0xF0;
						else
							p_line[pixel_x] = p_ppu->bg_pal[cl];
						solid_flags[pixel_x] = BG_WHITE_MASK;
					}
				}
			}
			pixel_x++;
		}

		// next tile
		tile_x++;
		nt_addr++;

		if(0 == (tile_x & 0x1))
		{
			if(0 == (tile_x & 0x3))
			{
				if(0 == (tile_x & 0x1f))
				{
					nt_addr ^= 0x400;
					attr_addr ^= 0x400;
					nt_addr -= 0x20;
					attr_addr -= 0x08;
					tile_x -= 0x20;
				}
				attr_addr++;
			}
			if(0 == (tile_y & 0x2))
			{
				if(0 == (tile_x &2))
				{
					attr_bits = (VRAM(p_ppu, attr_addr) & 0x3) << 2;
				}
				else
				{
					attr_bits = (VRAM(p_ppu, attr_addr) & 0xc);
				}
			}
			else
			{
				if(0 == (tile_x &2))
				{
					attr_bits = (VRAM(p_ppu, attr_addr) & 0x30) >> 2;
				}
				else
				{
					attr_bits = (VRAM(p_ppu, attr_addr) & 0xc0) >> 4;
				}
			}
		}
	}

	// next line
}


static void render_spr(ines_ppu_t* p_ppu, ines_byte_t* p_line,  ines_byte_t* solid_flags)
{
	ines_int_t  s, spr_y,spr_x, x_inc, start_x, end_x, x, y, prio;
	ines_byte_t  *p, *pf,  *spr, hi, lo, mask;
	ines_int_t  spr_height, spr_num, col;
	ines_word_t  spr_addr_base, addr;


	spr_num = 0;

	spr_height = (p_ppu->reg_ctrl_1 & PPU_BIG_SPR) ? 16 : 8;
	spr_addr_base = ((p_ppu->reg_ctrl_1 & (PPU_BIG_SPR|PPU_SPR_MEM_MASK) ) == PPU_SPR_MEM_MASK) ? 0x1000 : 0;

	ines_mapper_PPU_latch(&(ppu2host(p_ppu)->mapper), 0, 0);
	
	for(s = 0; s < 64; s++)
	{
		spr = &p_ppu->sp_RAM[s<<2];

		spr_y = spr[0] + 1;

		if(spr_y > p_ppu->current_line || spr_y + spr_height <= p_ppu->current_line)
			continue;
	
		// 精灵
		spr_num++;

		// 是否绘制超过8个精灵每行
		// if(spr_num > 8 && !setting.draw_all_sprite_than_8) continue; 

		// 垂直翻转
		if(0 == (spr[2] & 0x80))
			y = p_ppu->current_line - spr_y;
		else
			y = spr_height - (p_ppu->current_line - spr_y) - 1;


		// pattern addr
		if(spr_height == 16)
		{
			addr = spr[1] << 4;
			if(spr[1] & 0x1)
			{
				addr += 0x1000;
				if(y<8) addr -= 16;
			}
			else
			{
				if(y>= 8) addr += 16;
			}
		}
		else
		{
			addr = spr_addr_base + (spr[1] << 4);
		}
		addr += y&0x7;

		lo = VRAM(p_ppu, addr + 0);
		hi = VRAM(p_ppu, addr + 8);
		
		// for MMC2/MMC4 Latch FD/FE
		ines_mapper_PPU_latch_FDFE(&ppu2host(p_ppu)->mapper, addr);

		if(lo == 0 && hi == 0)
			continue;

		spr_x = spr[3];
		start_x = 0;
		end_x = 8;

		// clip right
		if(spr_x + end_x >= SCREEN_WIDTH)
		{
			end_x = SCREEN_WIDTH - spr_x;
		}

		// clip left
		if(0 == (p_ppu->reg_ctrl_2 & PPU_MASK_SPRLEFT8))
		{
			if(spr_x < 8)
				start_x = (8-spr_x);
		}

		// 先计算像素的位置
		p = p_line + spr_x + start_x;
		pf = solid_flags + spr_x + start_x;

		// 水平翻转
		if(0 == (spr[2] & 0x40))
		{
			x_inc = 1;
		}
		else
		{
			start_x = 7 - start_x;
			end_x = 7 - end_x;
			x_inc = -1;
		}

		prio = spr[2] & 0x20;

		for(x = start_x; x != end_x; x += x_inc)
		{
			if(0 == (*pf & SPR_WHITE_MASK ) )
			{
				mask = 0x80>>(x&0x07);
				col = 0;
				if(lo & mask) col |= 0x01;
				if(hi & mask) col |= 0x02;

				if(0 != col)
				{
					// no-empty color
					col |= (spr[2] & 0x03) << 2;

					if(s == 0)  // #0 sprite, hit test
					{
						if((*pf & BG_WHITE_MASK) )
						{
							// set hit flag
							p_ppu->reg_status |= PPU_STATUS_SPR_0_SCAN;
						}
					}

					if(prio )  // 后台精灵。空白的地方才可以绘制
					{
						*pf |= SPR_WHITE_MASK;
						if( (*pf & BG_WHITE_MASK) == 0)
						{
							if(p_ppu->reg_ctrl_2 & PPU_SINGCOLOR)
								*p = p_ppu->sp_pal[col] & 0xf0;
							else
								*p = p_ppu->sp_pal[col];

						}
					}
					else
					{
						if( (*pf & SPR_WHITE_MASK) == 0) // 前台精灵，
						{
							*pf |= SPR_WHITE_MASK;
							if(p_ppu->reg_ctrl_2 & PPU_SINGCOLOR)
								*p = p_ppu->sp_pal[col] & 0xf0;
							else
								*p = p_ppu->sp_pal[col];
						}
					}
				}
			}
			p++;
			pf++;
		}
	}
	if(spr_num >= 8)
	{
		p_ppu->reg_status |= PPU_STATUS_SPR_NUM_OVER;
	}
	else
	{
		p_ppu->reg_status &= ~PPU_STATUS_SPR_NUM_OVER;
	}
}


#pragma pack(push, 1)

struct _ines_state_ppu_data_
{
/* 0 - 7 : 8 bytes */
	ines_byte_t    R1;   /// reg_1
	ines_byte_t    R2;   /// reg_2
	ines_byte_t    RST;  /// reg_status
	ines_byte_t    RSP;  /// reg_sp_addr
	ines_byte_t    RF;   /// 0:toggle_2005_2006, 1:in_vblank
	ines_byte_t    RB;   /// read_2007_buffer
	ines_byte_t    Reserved0;
/* 8- 15 : 8 bytes */
	ines_word_t    IT;   /// index_t
	ines_word_t    IV;   /// index_v
	ines_word_t    IX;   /// index_x
	ines_byte_t    NTT;  /// nametable 窗口类型(每窗口 2 bit：bit0-1=窗口8 ... bit6-7=窗口11)
	ines_byte_t    Reserved2;
/* 16 - 32 : 8 bytes  */
	ines_int_t     LN;   /// current_line
	ines_dword_t   VRAM_used;   /// each bit indicated a block of VRAM is used. -- next  num  blocks  of 1k is saved the used vram
/* 24 - 31 : 8 bytes  */
	ines_byte_t    PTRW[8];     /// 0-8 bank of pattern table block is vram(0) or VROM(1) 
/* 32 - 55 : 24 bytes  */
	ines_word_t    BANK[12];    /// vmem bank indexs
/* 56 - 79 : 16 bytes  */
	ines_byte_t    BG_PAL[NES_MAX_PALMEM_SIZE];
/* 80 - 95 : 16 bytes  */
	ines_byte_t    SP_PAL[NES_MAX_PALMEM_SIZE];
/* 96 - 351 : 256 bytes  */
	ines_byte_t    SP_RAM[NES_MAX_SPMEM_SIZE];
/* 352 - 4447 : 4096 bytes  */
	ines_byte_t    NT_RAM[NES_MAX_NTMEM_SIZE];
};
/* total : 4448 bytes */

typedef struct _ines_state_ppu_data_   ines_state_ppu_data_t;
#pragma pack(pop)



ines_int_t ines_ppu_save_state(ines_ppu_t* p_ppu, FILE* fSave)
{
	int n;
	ines_byte_t*  VRAM;
	ines_byte_t*  VROM;
	ines_byte_t*  VNTM;
	ines_state_ppu_data_t   data;
	memset(&data, 0, sizeof(data));

	data.R1 = p_ppu->reg_ctrl_1;
	data.R2 = p_ppu->reg_ctrl_2;
	data.RST = p_ppu->reg_status;
	data.RSP = p_ppu->reg_spr_addr;
	data.RF = (p_ppu->toggle_2005_2006 ? 1:0)|(p_ppu->in_vblank ? 2:0);
	data.RB = p_ppu->read_2007_buffer;
	data.IT = p_ppu->index_t;
	data.IV = p_ppu->index_v;
	data.IX = p_ppu->index_x;
	data.LN = p_ppu->current_line;
	data.NTT = (ines_byte_t)( (p_ppu->nt_type[0] & 0x3)        |
	                          ((p_ppu->nt_type[1] & 0x3) << 2) |
	                          ((p_ppu->nt_type[2] & 0x3) << 4) |
	                          ((p_ppu->nt_type[3] & 0x3) << 6) );

	for(n = 0; n < NES_MAX_VRAM_BANKS; n++)
	{
		if(p_ppu->pattern_table_used[n])
		{
			data.VRAM_used |= (1<<n);
		}
	}

	VRAM = p_ppu->pattern_table;
	VROM = ppu2host(p_ppu)->rom.pVROMs;
	VNTM = p_ppu->name_table;

#define VRAM21KNUM(p)   ( ( (p)-VRAM )>>10 )
#define VROM21KNUM(p)   ( ( (p)-VROM )>>10 )
#define VNTM21KNUM(p)   ( ( (p)-VNTM )>>10 )

	for(n = 0; n < NES_MAX_VMEM_BANKS; n++)
	{
		if(n < NES_MAX_PTMEM_BANKS)
		{
			data.PTRW[n] = p_ppu->pattern_type[n];
			if(data.PTRW[n] == 0)
			{
				data.BANK[n] = VRAM21KNUM(p_ppu->mem_bank[n]);
			}
			else if(data.PTRW[n] == 1)
			{
				data.BANK[n] = VROM21KNUM(p_ppu->mem_bank[n]);
			}
			else
			{
				data.BANK[n] = VNTM21KNUM(p_ppu->mem_bank[n]);   // 2: CIRAM 当作 CHR
			}
		}
		else
		{
			// nametable 窗口：按 nt_type 记录窗口指向的是 CIRAM / CHR-ROM / CHR-RAM 页
			switch(p_ppu->nt_type[n - NES_MAX_PTMEM_BANKS])
			{
			case 1:
				data.BANK[n] = (ines_word_t)VROM21KNUM(p_ppu->mem_bank[n]);
				break;
			case 2:
				data.BANK[n] = (ines_word_t)VRAM21KNUM(p_ppu->mem_bank[n]);
				break;
			default:
				data.BANK[n] = (ines_word_t)VNTM21KNUM(p_ppu->mem_bank[n]);
				break;
			}
		}
	}
	
	memcpy(data.BG_PAL, p_ppu->bg_pal, sizeof(data.BG_PAL));
	memcpy(data.SP_PAL, p_ppu->sp_pal, sizeof(data.SP_PAL));
	memcpy(data.SP_RAM, p_ppu->sp_RAM, sizeof(data.SP_RAM));
	memcpy(data.NT_RAM, p_ppu->name_table, sizeof(data.NT_RAM));


	fwrite(&data, sizeof(data), 1, fSave);

	for(n = 0; n < NES_MAX_VRAM_BANKS; n++)
	{
		if(p_ppu->pattern_table_used[n])
		{
			fwrite(p_ppu->pattern_table + (n<<10), (1<<10),1, fSave);
		}
	}

	return 0;
}

ines_int_t ines_ppu_load_state(ines_ppu_t* p_ppu, FILE* fSave)
{
	int n;
	ines_state_ppu_data_t   data;

	if(1 != fread(&data, sizeof(data), 1, fSave))
		return -1;

	memset(p_ppu, 0, sizeof(*p_ppu));
	p_ppu->reg_ctrl_1 = data.R1;
	p_ppu->reg_ctrl_2 = data.R2;
	p_ppu->reg_status = data.RST;
	p_ppu->reg_spr_addr = data.RSP;
	p_ppu->toggle_2005_2006 = (data.RF & 1) ? 1 : 0;
	p_ppu->in_vblank = (data.RF & 2) ? 1 : 0;
	p_ppu->read_2007_buffer = data.RB;
	p_ppu->index_t = data.IT;
	p_ppu->index_v = data.IV;
	p_ppu->index_x = data.IX;
	p_ppu->current_line = data.LN;

	for(n = 0; n < NES_MAX_NTRAM_BANKS; n++)
	{
		p_ppu->nt_type[n] = (ines_byte_t)((data.NTT >> (n << 1)) & 0x3);
	}

	for(n = 0; n < NES_MAX_VRAM_BANKS; n++)
	{
		if(data.VRAM_used & (1<<n))
		{
			p_ppu->pattern_table_used[n] = 1;
			if(1 != fread(p_ppu->pattern_table + (n<<10), (1<<10),1, fSave))
				return -1;
		}
	}


	for(n = 0; n < NES_MAX_VMEM_BANKS; n++)
	{
		if(n < NES_MAX_PTMEM_BANKS)
		{
			p_ppu->pattern_type[n] = data.PTRW[n];
			if(data.PTRW[n] == 0)
			{
				if(data.BANK[n] >= NES_MAX_VRAM_BANKS)
					return  -1;
				p_ppu->mem_bank[n] = p_ppu->pattern_table + ((ines_dword_t)data.BANK[n]<<10);
			}
			else if(data.PTRW[n] == 1)
			{
				if(data.BANK[n] >= ppu2host(p_ppu)->vrom_1k_num)
					return  -1;
				p_ppu->mem_bank[n] = ppu2host(p_ppu)->rom.pVROMs + ((ines_dword_t)data.BANK[n]<<10);
			}
			else if(data.PTRW[n] == 2)
			{
				// CIRAM 当作 CHR：mem_bank 指向内部 NT RAM
				if(data.BANK[n] >= NES_MAX_NTRAM_BANKS)
					return  -1;
				p_ppu->mem_bank[n] = p_ppu->name_table + ((ines_dword_t)data.BANK[n]<<10);
			}
			else
			{
				return  -1;
			}
		}
		else
		{
			ines_int_t t = p_ppu->nt_type[n - NES_MAX_PTMEM_BANKS];

			if(t == 1)          // CHR-ROM 页当 nametable(只读)
			{
				if(data.BANK[n] >= ppu2host(p_ppu)->vrom_1k_num)
					return  -1;
				p_ppu->mem_bank[n] = ppu2host(p_ppu)->rom.pVROMs + ((ines_dword_t)data.BANK[n]<<10);
			}
			else if(t == 2)     // CHR-RAM 页当 nametable(可写)
			{
				if(data.BANK[n] >= NES_MAX_VRAM_BANKS)
					return  -1;
				p_ppu->pattern_table_used[data.BANK[n]] = 1;
				p_ppu->mem_bank[n] = p_ppu->pattern_table + ((ines_dword_t)data.BANK[n]<<10);
			}
			else                // 内部 CIRAM
			{
				if(data.BANK[n] >= NES_MAX_NTRAM_BANKS)
					return -1;
				p_ppu->mem_bank[n] = p_ppu->name_table + ((ines_dword_t)data.BANK[n]<<10);
			}
		}
	}
	memcpy(p_ppu->bg_pal, data.BG_PAL, sizeof(data.BG_PAL));
	memcpy(p_ppu->sp_pal, data.SP_PAL, sizeof(data.SP_PAL));
	memcpy(p_ppu->sp_RAM, data.SP_RAM, sizeof(data.SP_RAM));
	memcpy(p_ppu->name_table, data.NT_RAM, sizeof(data.NT_RAM));

	return 0;
}

