# iNES 文档索引

| 文档 | 内容 |
|---|---|
| [architecture.md](architecture.md) | 项目架构：分层结构、模块职责、数据流、线程与时序模型 |
| [api.md](api.md) | 核心 API 参考：host / rom / cpu / ppu / apu / joypad / mapper / 公共库 / DLL 导出接口 |
| [coding-style.md](coding-style.md) | 编码规范：命名、文件组织、类型与宏、注释、日志、错误处理 |
| [mapper-guide.md](mapper-guide.md) | **Mapper 开发规范**：接口、模板、实现步骤、寄存器与 bank 约定、IRQ、测试与提交检查表 |
| [mapper-list.md](mapper-list.md) | **已实现 Mapper 清单**：实现状态表、内部命名、特性矩阵、桩文件说明 |
| [build.md](build.md) | 构建说明：CMake 目标、选项、产物、编码与换行的工程约定 |

## 阅读顺序建议

- 新同学：先读 [architecture.md](architecture.md) → [api.md](api.md) → [coding-style.md](coding-style.md)
- 要实现/修复某个 Mapper：先读 [mapper-guide.md](mapper-guide.md)，再对照 [mapper-list.md](mapper-list.md) 找同类实现作参考
- 要改构建或加源文件：先看 [build.md](build.md)

## 术语

| 术语 | 含义 |
|---|---|
| PROM / PRG | 卡带中的程序 ROM，按 8KB 分页（`$8000-$FFFF`） |
| VROM / CHR | 卡带中的图形 ROM，按 1KB 分页（PPU `$0000-$1FFF`） |
| VRAM | PPU 名称表用的卡带 RAM（CHR-RAM），用于无 VROM 的卡带 |
| SRAM | 卡带电池记忆 RAM（`$6000-$7FFF`） |
| Mapper | 卡带上的存储器管理芯片，负责 bank 切换、镜像控制、IRQ 等 |
| Nametable Mirroring | PPU 名称表镜像方式（水平/垂直/单屏/四屏） |
| HSync / VSync | 行同步（每条扫描线）/ 场同步（每帧）回调 |
