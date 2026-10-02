
/**
 * core/mapper/213.c -- iNES Mapper 058 的同名（重复）编号
 *
 * NESDev（Disch 笔记）原文：
 *   "iNES Mapper 213 is a duplicate of INES Mapper 058, assigned to by:
 *      9999999-in-1 (Can You Feel the Love Tonight menu music)
 *      168-in-1 (My Way menu music)
 *    ... Both ROM files run well as mapper 58."
 * 即 213 与 58 是同一类卡带，只是早期模拟器（FCEUX/Mesen）各自编了号；
 * 资料明确建议按 58 跑，因此本编号**直接复用 `core/mapper/58.c` 的实现**，
 * 不另写一套，免得两处行为漂移。
 *
 * 58/213 的硬件要点（详见 `core/mapper/58.c` 文件头）：
 * 简单 NROM-/CNROM 型多合一（GK-192 118-in-1、HKX5268 68-in-1、168-in-1 等），
 * **Address Latch** —— bank 号由写入的**地址**译码，写入的数据只用来选镜像。
 *
 * 注意：BMC-411120-C 板虽然曾与 213 混淆，但那是 NES 2.0 Mapper 287（MMC3 变体），
 * 与本编号无关。
 */

#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"


/* 058 的实现是本编号的实体，声明见 core/mapper_creator.c 的 extern 列表 */
extern ines_bool_t  mapper58_create(ines_mapper_t* p_mapper);

/**
 * 创建：直接套用 058 的实现（213 是 058 的重复编号）。
 * @param p_mapper mapper 对象
 * @return 058 实现的返回值（ines_true = 已实现）
 */
ines_bool_t  mapper213_create(ines_mapper_t* p_mapper)
{
	if(p_mapper == NULL)
		return ines_false;

	INES_LOG(LOG_INF, MOD_MMC, ISTR("mapper213: iNES 058 的同名编号，共用 mapper 58 实现\n"));

	return mapper58_create(p_mapper);
}

