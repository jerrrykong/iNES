
#include "rom.h"
#include "ppu.h"
#include "../comm/log.h"


static unsigned long   calc_crc32(unsigned char* p_data, unsigned long len)
{
	unsigned long crc_table[256];
	unsigned long c, i, j;

	for(i = 0; i < 256; i++)
	{
		c = i;
		for(j = 0; j < 8; j++)
		{
			if(c & 1) 
				c = ( c >> 1 ) ^ 0xedb88320;
			else
				c >>= 1;
		}
		crc_table[i] = c;
	}


	c = 0xffffffff;

	for(i = 0; i < len; i++)
	{
		c = (c >> 8) ^ crc_table[(c ^ p_data[i]) & 0xff];
	}
	return ~c & 0xffffffff;
}

void  ines_rom_init(ines_rom_t*  p_rom)
{
	memset(p_rom, 0, sizeof(ines_rom_t));
}

void  ines_rom_free(ines_rom_t*  p_rom)
{
	if(p_rom == NULL)
		return;

	if(p_rom->pPROMs)
		ines_free(p_rom->pPROMs);
	if(p_rom->pVROMs)
		ines_free(p_rom->pVROMs);

	memset(p_rom, 0, sizeof(*p_rom));
}


void  ines_rom_reset(ines_rom_t*  p_rom)
{
	// nothing
}


ines_bool_t ines_rom_load_from_file(ines_rom_t*  p_rom, ines_cstr_t strFileName)
{
	FILE*  pfnes;
	ines_file_header_t    header;
	ines_size_t    sz;
	ines_size_t    PROM_size;
	ines_size_t    VROM_size;
	ines_byte_t    mapper_num;
	ines_byte_t    trainer[INES_TRAINER_BLOCK_SIZE];
	ines_byte_t*   pPROM;
	ines_byte_t*   pVROM;

	if(!p_rom || !ines_valid_str(strFileName))
		return ines_false;

	INES_LOG(LOG_INF, MOD_ROM, ISTR("open nes file `%s`...\n"), strFileName);


	// 打开文件
	pfnes = _tfopen(strFileName, ISTR("rb")); 

	if(pfnes == NULL)
	{
		INES_LOG(LOG_ERR, MOD_ROM, ISTR("open nes file `%s` failed: (%d)%s\n"), strFileName, errno, strerror(errno));
		return ines_false;
	}

	sz = fread(&header, 1, INES_FILE_HEADER_SIZE, pfnes);

	if(sz != INES_FILE_HEADER_SIZE)
	{
		fclose(pfnes);
		INES_LOG(LOG_ERR, MOD_ROM, ISTR("open nes file `%s` failed: invalid file head size.\n"), strFileName);
		return ines_false;
	}

	if(0 != memcmp(header.tag, INES_FILE_TAG, sizeof(header.tag)))
	{
		fclose(pfnes);
		INES_LOG(LOG_ERR, MOD_ROM, ISTR("open nes file `%s` failed: invalid file head tag.\n"), strFileName);
		return ines_false;
	}

	if(header.PROM_block_num == 0 )
	{
		fclose(pfnes);
		INES_LOG(LOG_ERR, MOD_ROM, ISTR("open nes file `%s` failed: invalid file PROM block number.\n"), strFileName);
		return ines_false;
	}

	//  允许没有VROM，而进行动态写入
/*	if(header.VROM_block_num == 0 )
	{
		INES_LOG(LOG_ERR, MOD_ROM, ISTR("open nes file `%s` failed: invalid file VROM block number.\n"), strFileName);
		return ines_false;
	}
*/
	INES_LOG(LOG_INF, MOD_ROM, ISTR("PROM Blocks: %d, VROM Blocks %d, Flag1: 0x%02XH, Flag2: 0x%02XH\n"), 
		header.PROM_block_num, header.VROM_block_num, header.flag1, header.flag2);


	// 有金手指数据 512 字节
	if(header.flag1 & 0x04)
	{
		sz = fread(&trainer, 1, INES_TRAINER_BLOCK_SIZE, pfnes);

		if(sz != INES_TRAINER_BLOCK_SIZE)
		{
			fclose(pfnes);
			INES_LOG(LOG_ERR, MOD_ROM, ISTR("open nes file `%s` failed: invalid file trainer size.\n"), strFileName);
			return ines_false;
		}
	}

	pPROM = NULL;
	pVROM = NULL;
	mapper_num = (header.flag1 >> 4) | (header.flag2 & 0xF0);
	PROM_size = (ines_size_t)header.PROM_block_num * INES_PROM_BLOCK_SIZE;
	VROM_size = (ines_size_t)header.VROM_block_num * INES_VROM_BLOCK_SIZE;

	INES_LOG(LOG_NTY, MOD_ROM, ISTR("Calc PROM Size: %dK, VROM Size %dK, Mapper: %d\n"), 
		PROM_size/1024, VROM_size/1024, mapper_num);

	pPROM = ines_alloc(PROM_size);
	pVROM = ines_alloc(VROM_size);


	if(pPROM == NULL || pVROM == NULL)
	{
		ines_free(pPROM);
		ines_free(pVROM);

		fclose(pfnes);
		INES_LOG(LOG_ERR, MOD_ROM, ISTR("open nes file `%s` failed: out of memory.\n"), strFileName);
		return ines_false;
	}

	if( PROM_size != fread(pPROM, 1, PROM_size, pfnes) || 
		VROM_size != fread(pVROM, 1, VROM_size, pfnes) )
	{
		ines_free(pPROM);
		ines_free(pVROM);

		fclose(pfnes);
		INES_LOG(LOG_ERR, MOD_ROM, ISTR("open nes file `%s` failed: invalid file rom size.\n"), strFileName);
		return ines_false;
	}

	
	p_rom->mirror_type = (header.flag1 & 0x08) ? MIRROR_FOUR_SCREEN : (header.flag1 & 0x01) ? MIRROR_VERT : MIRROR_HORZ;
	p_rom->has_sram  = (header.flag1 & 0x02) ? 1 : 0;
	p_rom->has_trainer  = (header.flag1 & 0x04) ? 1 : 0;
	if(p_rom->has_trainer)
	{
		memcpy(p_rom->trainer_data, trainer, INES_TRAINER_BLOCK_SIZE); 
	}
	p_rom->mapper_num = mapper_num;
	p_rom->PROM_block_num = header.PROM_block_num;
	p_rom->VROM_block_num = header.VROM_block_num;
	p_rom->pPROMs = pPROM;
	p_rom->pVROMs = pVROM;

	p_rom->crc32_p = calc_crc32(pPROM, PROM_size);
	INES_LOG(LOG_NTY, MOD_ROM, ISTR("Calc PROM CRC32=0x%08X\n"), p_rom->crc32_p); 

	// ROM  patch 
	if(p_rom->crc32_p == 0x57970078)
	{
		p_rom->pPROMs[0x3fe1] = 0xff;
		p_rom->pPROMs[0x3fe6] = 0x00;
	}

	fclose(pfnes);

	return ines_true;
}



