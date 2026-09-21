#include "nes.h"
#include "cpu.h"
#include "../comm/log.h"


// 初始化
void ines_mapper_init(ines_mapper_t* p_mapper)
{
	if(p_mapper == NULL)
		return;

	memset(p_mapper, 0, sizeof(*p_mapper));
}

// 删除
void ines_mapper_free(ines_mapper_t* p_mapper)
{
	if(p_mapper == NULL)
		return;

	if(p_mapper->fini != NULL)
	{
		(*p_mapper->fini)(p_mapper);
	}

	if(p_mapper->p_data != NULL)
	{
		ines_free(p_mapper->p_data);
		p_mapper->p_data = NULL;
	}

	memset(p_mapper, 0, sizeof(*p_mapper));

}



// 软件复位
void ines_mapper_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	//if(p_host->rom.has_sram) // 此标志表示是否带有RAM电池. 即使无电池，也可能有扩展的SRAM。只是断电后内容消失。
	if(p_mapper->custom_sram == 0) // 如果 mapper 需要自己维护SRAM段的读写，则不进行SRAM设置。 比如 mapper16 使用 EEPROM 24c01/02
	{
		// 默认指定 $6000~$7fff  指向用户RAM 共一块，8K空间
		ines_set_sram_bank_n(p_host, 3, 0);
	}
	ines_ppu_set_mirror_type(&p_host->ppu, p_host->rom.mirror_type);

	if(p_mapper && p_mapper->reset)
	{
		(*p_mapper->reset)(p_mapper);
	}
}

ines_int_t ines_mapper_save_state(ines_mapper_t* p_mapper, FILE* fSave)
{
	if(p_mapper->savestate)
		return (*p_mapper->savestate)(p_mapper, fSave, ines_true);
	else if(p_mapper->p_data && p_mapper->data_len > 0)
	{
		ines_dword_t len = p_mapper->data_len;
		fwrite(&len, sizeof(len), 1, fSave);
		fwrite(p_mapper->p_data, p_mapper->data_len, 1, fSave);
	}
	else
	{
		ines_dword_t len = 0;
		fwrite(&len, sizeof(len), 1, fSave);
	}
	return 0;
}

ines_int_t ines_mapper_load_state(ines_mapper_t* p_mapper, FILE* fSave)
{
	if(p_mapper->savestate)
		return (*p_mapper->savestate)(p_mapper, fSave, ines_false);
	else
	{
		ines_dword_t len = 0;
		if(1 != fread(&len, sizeof(len), 1, fSave))
			return -1;
		if(len != p_mapper->data_len)
			return -1;
		if(len == 0)
			return 0;
		if(p_mapper->p_data == NULL)
			return -1;
		if(1 != fread(p_mapper->p_data, p_mapper->data_len, 1, fSave))
			return -1;
	}
	return 0;
}

ines_word_t  addr_mask(ines_word_t  num)
{
	ines_word_t  mask = 0;
	if( num > 0 )
	{
		num -= 1; // last addr index

		while( (mask & num) != num)
			mask = ((mask<<1)|1);
	}
	return mask;
}


