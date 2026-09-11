#ifndef __INES_CONFIG_H__
#define __INES_CONFIG_H__


#include "../comm/idef.h"


#ifdef __cplusplus
extern "C"
{
#endif


// 指定配置文件(通常为 <数据目录>/config.ini)。会立即载入内容, 之后自动按需保存。
// 传入 NULL 或空串表示不持久化(仅内存)。
void iNES_config_set_file(ines_cstr_t szPath);

// 取字符串配置项。返回进程内的静态缓冲区(内部有 4 个轮转缓冲, 支持在同一次
// 调用中拼接多个结果), 未找到时返回 szDefault(可能为 NULL -> 返回空串)。
ines_cstr_t GetConfigStr(ines_cstr_t szSection, ines_cstr_t szKey, ines_cstr_t szDefault);

// 取整型配置项, 未找到或无法解析时返回 iDefault。
ines_int_t GetConfigInt(ines_cstr_t szSection, ines_cstr_t szKey, ines_int_t iDefault);

// 写入配置项并立即落盘(与 win32 的 WritePrivateProfileString 语义一致)。
void SetConfigStr(ines_cstr_t szSection, ines_cstr_t szKey, ines_cstr_t szValue);
void SetConfigInt(ines_cstr_t szSection, ines_cstr_t szKey, ines_int_t iValue);


#ifdef __cplusplus
};
#endif


#endif
