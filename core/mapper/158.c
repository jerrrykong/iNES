
/**
 * core/mapper/158.c -- RAMBO-1 的镜像变体（iNES Mapper 158）
 *
 * NESDev "RAMBO-1" 的 Variants 一节原文：
 *   "Mapper 158, used for Alien Syndrome, has mirroring like mapper 118 (TLSROM),
 *    where CIRAM A10 is connected to CHR A17, and bit 7 of each CHR bank mapped into
 *    PPU $0000-$0FFF controls which page of CIRAM is used for the corresponding
 *    nametable in $2000-$2FFF."
 *
 * 即：bank 切换、IRQ 等全部与 mapper 64 完全一致，唯一区别是**镜像不由 $A000 控制**，
 * 而是 CIRAM A10 接到 CHR A17 —— PPU $0000-$0FFF 四个 1KB 页的 bit7 分别决定
 * $2000/$2400/$2800/$2C00 用内部 CIRAM 的哪一页。
 *
 * 因此本文件不复制实现，只以 tlsrom = 1 转发到 core/mapper/64.c，
 * 免得两处行为漂移（与 213 转发到 58 的做法一致）。
 *
 * 注意：本机 ROM 清单里没有标 158 的卡带（Alien Syndrome 常见的是 mapper 118 版），
 * 故本编号**无实测 ROM**，逻辑依据仅有上述资料。
 */

#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"


/* 064 的实现是本编号的实体，声明见 core/mapper_creator.c 的 extern 列表 */
extern ines_bool_t  mapper64_create_variant(ines_mapper_t* p_mapper, ines_int_t tlsrom);

/**
 * 创建：套用 064（RAMBO-1）的实现，镜像改用 TLSROM 接法。
 * @param p_mapper mapper 对象
 * @return 064 实现的返回值（ines_true = 已实现）
 */
ines_bool_t  mapper158_create(ines_mapper_t* p_mapper)
{
	if(p_mapper == NULL)
		return ines_false;

	INES_LOG(LOG_INF, MOD_MMC, ISTR("mapper158: RAMBO-1 的 TLSROM 镜像变体，共用 mapper 64 实现\n"));

	return mapper64_create_variant(p_mapper, 1);
}

