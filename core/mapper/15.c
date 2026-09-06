
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"

/*


There is one switchable bank. How this is mapped into depends on the current bank mode (see Registers below).
Bank mode 0 ( 32K )
CPU $8000-$BFFF: Bank B
CPU $C000-$FFFF: Bank (B bitwise ORed with 1)
If bit 0 of B equals 1, then the 16K and 32K modes are identical.
Bank mode 1 ( 128K )
CPU $8000-$BFFF: Switchable 16 KB bank B
CPU $C000-$DFFF: Fixed to last bank in the cart
This mode uses the same configuration as UNROM.
Bank mode 2 ( 8K )
CPU $8000-$9FFF: Sub-bank b of 16 KB PRG ROM bank B
CPU $A000-$FFFF: Mirrors of $8000-$9FFF
Bank mode 3 ( 16K )
CPU $8000-$BFFF: 16 KB bank B
CPU $C000-$FFFF: Mirror of $8000-$BFFF

Control ($8000-$FFFF)
This is a 10-bit register. The upper 2 bits of the value are set from address lines A1-A0.

15 bit  8 7  bit  0  Address bus
---- ---- ---- ----
1xxx xxxx xxxx xxSS
|                ||
|                ++- Select PRG ROM bank mode
|                    0: 32K; 1: 128K (UNROM style); 2: 8K; 3: 16K
+------------------- Always 1

7  bit  0  Data bus
---- ----
bMBB BBBB
|||| ||||
||++-++++- Select 16 KB PRG ROM bank
|+-------- Select nametable mirroring mode (0=vertical; 1=horizontal)
+--------- Select 8 KB half of 16 KB PRG ROM bank
(should be 0 except in 8K bank mode)
The result when b=1 outside of 8K bank mode is not specified.
Power-up state: bank mode 2, bank 0, mirroring not specified.
*/

static void mapper15_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);

	// 8 Kmode bank 0
	ines_set_prom_bank_4(p_host, 0,1,2,3);
	
}

static void mapper15_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t  val)
{
	ines_host_t*  p_host = mapper2host(p_mapper);

	ines_byte_t   mode = (addr & 0x3);
	ines_word_t   B = (val & 0x3f);
	ines_byte_t   b = (val & 0x80) >> 7;
	
	// set PROM bank
	switch(mode)
	{
	case 0: // 32k mode : use B
		ines_set_prom_bank_4(p_host, B * 2, B * 2 + 1, (B|1) * 2 , (B|1) * 2 + 1);
		break;
	case 1: // 128K mode: use B
		ines_set_prom_bank_4(p_host, B * 2, B * 2 + 1, p_host->prom_8k_num - 2 , p_host->prom_8k_num - 1);
		break;
	case 2: // 8K mode
		ines_set_prom_bank_4(p_host, B * 2 + b, B * 2 + b, B * 2 + b, B * 2 + b);
		break;
	case 3: // 16k mode : use B
		ines_set_prom_bank_4(p_host, B * 2, B * 2 + 1, B * 2, B * 2 + 1);
		break;		 
	}

	// set mirror
	if(0 == (val & 0x40))
	{
		ines_ppu_set_mirror_type(&p_host->ppu, MIRROR_VERT);
	}
	else
	{
		ines_ppu_set_mirror_type(&p_host->ppu, MIRROR_HORZ);
	}
}

ines_bool_t  mapper15_create(ines_mapper_t* p_mapper)
{
	p_mapper->reset = mapper15_reset;
	p_mapper->writehigh = mapper15_writehigh;
	return ines_true;
}



