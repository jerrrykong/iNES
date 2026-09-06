# =====================================================================
# iNES -- NES(FC) 模拟器  CMake 构建脚本
#
# 移植自 project/win32 下的 Visual C++ 2008 工程:
#   iNES.vcproj         -> 目标 iNES    (Win32 GUI 模拟器, 输出 iNES[_d].exe)
#   iNesCore_DLL.vcproj -> 目标 inescore(模拟核心共享库, 输出 inescore.dll/.so)
#
# 用法示例(在仓库根目录执行, project/ 已被 .gitignore 忽略):
#   cmake -S . -B project/build
#   cmake --build project/build --config Release
#   构建产物默认输出到仓库 bin/ 目录(与原 VC 工程一致)
#
# 注意事项:
#   2) 与原始 VC 工程一致: iNES.exe 按 Unicode 字符集编译, inescore 按
#      多字节(MBCS)字符集编译, 因此 core/comm 会被分别编译进两个目标。
#   3) 本脚本以 Windows/MSVC 为主要目标; UNIX 上仅构建 inescore 共享库
#      (对应 Android/jni/Android.mk 的做法, 为尽力支持而未经过充分验证)。
# =====================================================================

cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------
# 目标平台: 原 VC 工程(iNES.vcproj / iNesCore_DLL.vcproj)均为 Win32(x86),
# 这里默认沿用, 可用 cmake -A x64 或 -DCMAKE_GENERATOR_PLATFORM=x64 覆盖。
# 注意: 该变量必须在 project() 之前设置才会生效。
# ---------------------------------------------------------------------
# 必须以缓存变量形式设置: 普通变量会在 project() 内部被消费掉(实测不生效)。
# 用户可用 cmake -A x64 显式覆盖。
if(NOT DEFINED CMAKE_GENERATOR_PLATFORM OR "${CMAKE_GENERATOR_PLATFORM}" STREQUAL "")
    set(CMAKE_GENERATOR_PLATFORM "Win32" CACHE STRING "目标平台(原 VC 工程为 Win32)" FORCE)
endif()

if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja" AND
   NOT "$ENV{VSCMD_ARG_TGT_ARCH}" STREQUAL "x86")
    message(WARNING
        "当前使用 Ninja 生成器, 其目标架构取决于命令行环境(vcvarsall)。\n"
        "若要生成 32 位程序, 请在 \"x86 Native Tools Command Prompt for VS\" 中\n"
        "执行 cmake, 或改用: cmake -G \"Visual Studio 17 2022\" -A Win32")
endif()

project(iNES
    VERSION 1.0.0
    DESCRIPTION "iNES NES (Family Computer) emulator"
    LANGUAGES C)

set(INES_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# ---------------------------------------------------------------------
# 选项与缓存变量
# ---------------------------------------------------------------------
option(INES_BUILD_CORE "构建核心共享库 inescore(对应 iNesCore_DLL.vcproj)" ON)
option(INES_BUILD_GUI  "构建 Win32 GUI 模拟器 iNES(对应 iNES.vcproj)"     ON)
# 输出目录: 原 VC 工程把可执行与 dll 统一输出到仓库 bin/
set(INES_OUTPUT_DIR "${INES_DIR}/bin" CACHE PATH
    "可执行文件/共享库输出目录(默认 <仓库>/bin)")

# ---------------------------------------------------------------------
# 源文件列表(逐一对应 vcproj 中的文件分组)
# ---------------------------------------------------------------------
file(GLOB INES_MAPPER_SOURCES CONFIGURE_DEPENDS
     "${INES_DIR}/core/mapper/*.c")

# core 模拟核心(apu/cpu/joypad/mapper/mapper_creator/nes/ppu/rom + 全部 mapper)
set(INES_CORE_SOURCES
    "${INES_DIR}/core/apu.c"
    "${INES_DIR}/core/cpu.c"
    "${INES_DIR}/core/joypad.c"
    "${INES_DIR}/core/mapper.c"
    "${INES_DIR}/core/mapper_creator.c"
    "${INES_DIR}/core/nes.c"
    "${INES_DIR}/core/ppu.c"
    "${INES_DIR}/core/rom.c"
    ${INES_MAPPER_SOURCES})

# comm 公共库(iNES.vcproj 与 iNesCore_DLL.vcproj 均包含 buf/log/net/platform)
set(INES_COMMON_COMM_SOURCES
    "${INES_DIR}/comm/buf.c"
    "${INES_DIR}/comm/log.c"
    "${INES_DIR}/comm/net.c"
    "${INES_DIR}/comm/platform.c")

# comm/thread.c 仅被 iNesCore_DLL 使用(iNES.exe 不包含)
set(INES_THREAD_SOURCE "${INES_DIR}/comm/thread.c")

# win32 GUI 源文件(iNES.vcproj 的 win32 分组)
set(INES_WIN32_SOURCES
    "${INES_DIR}/win32/dlgNetPlay.c"
    "${INES_DIR}/win32/iNES.c"
    "${INES_DIR}/win32/stdafx.c"
    "${INES_DIR}/win32/wMemory.c"
    "${INES_DIR}/win32/wNameTable.c"
    "${INES_DIR}/win32/wPalette.c"
    "${INES_DIR}/win32/wPatternTable.c"
    "${INES_DIR}/win32/wSPmemory.c"
    "${INES_DIR}/win32/wVmemory.c")

# ---------------------------------------------------------------------
# 辅助函数: 输出目录对齐原工程 bin/ 布局
# ---------------------------------------------------------------------
function(ines_set_output_dirs TGT)
    set_target_properties(${TGT} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${INES_OUTPUT_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${INES_OUTPUT_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY "${INES_OUTPUT_DIR}")
    foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_target_properties(${TGT} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${INES_OUTPUT_DIR}"
            LIBRARY_OUTPUT_DIRECTORY_${_cfg} "${INES_OUTPUT_DIR}"
            ARCHIVE_OUTPUT_DIRECTORY_${_cfg} "${INES_OUTPUT_DIR}")
    endforeach()
endfunction()

# =====================================================================
# 目标 1: inescore 核心共享库   (移植自 iNesCore_DLL.vcproj)
# 字符集: 多字节(MBCS, 不定义 UNICODE); 仅依赖 ws2_32 / pthread
# =====================================================================
if(INES_BUILD_CORE)
    add_library(inescore SHARED
        ${INES_CORE_SOURCES}
        ${INES_COMMON_COMM_SOURCES}
        ${INES_THREAD_SOURCE}
        "${INES_DIR}/libinescore.c")

    # 源文件全部以相对路径互相引用, 加入仓库根目录仅为兼容可能出现的
    # "core/xxx.h" / "comm/xxx.h" 形式包含
    target_include_directories(inescore PRIVATE "${INES_DIR}")

    if(WIN32)
        # 对应 VCCLCompilerTool PreprocessorDefinitions
        target_compile_definitions(inescore PRIVATE
            WIN32
            _WINDOWS
            _USRDLL
            INESCORE_DLL_EXPORTS
            $<$<CONFIG:Debug>:_DEBUG>)
        # 对应 VCLinkerTool AdditionalDependencies="ws2_32.lib"
        target_link_libraries(inescore PRIVATE ws2_32)
        # 输出 inescore.dll / inescore.lib(不添加 lib 前缀)
        set_target_properties(inescore PROPERTIES
            OUTPUT_NAME "inescore"
            PREFIX "")
    else()
        # UNIX/Android 风格: comm/net.c、thread.c 走 POSIX 分支
        # (源码以小写 "linux" 宏区分平台, 需手动定义)
        target_compile_definitions(inescore PRIVATE linux)
        find_package(Threads REQUIRED)
        target_link_libraries(inescore PRIVATE Threads::Threads m)
        set_target_properties(inescore PROPERTIES OUTPUT_NAME "inescore")
    endif()

    ines_set_output_dirs(inescore)

    if(MSVC)
        # 与 VC 工程 WarningLevel=3 保持一致
        # /utf-8: 源码已统一为 UTF-8(无 BOM), 需显式告知 MSVC 源/执行字符集,
        #         否则中文注释/字符串会触发 C4819 或被误按本地代码页解释。
        target_compile_options(inescore PRIVATE /W3 /utf-8)
    endif()

    message(STATUS "iNES: 构建核心共享库 inescore")
endif()

# =====================================================================
# 目标 2: iNES Win32 GUI 模拟器   (移植自 iNES.vcproj)
# 字符集: Unicode(UNICODE/_UNICODE); 仅支持 Windows
# 依赖:   winmm/ws2_32/comdlg32/comctl32/shell32(无第三方依赖)
# =====================================================================
if(INES_BUILD_GUI)
    if(NOT WIN32)
        message(WARNING "iNES GUI 目标仅支持 Windows, 已跳过(可继续构建 inescore)。")
    else()
        add_executable(iNES WIN32)
        target_sources(iNES PRIVATE
            ${INES_CORE_SOURCES}
            ${INES_COMMON_COMM_SOURCES}
            ${INES_WIN32_SOURCES}
            "${INES_DIR}/win32/iNES.rc")

        target_include_directories(iNES PRIVATE "${INES_DIR}")

        # 对应 iNES.vcproj 的宏: WIN32;_WINDOWS;_CRT_SECURE_NO_WARNINGS
        # + Unicode 字符集(UNICODE/_UNICODE) + Debug 下的 _DEBUG
        target_compile_definitions(iNES PRIVATE
            WIN32
            # _WIN32: 资源编译器(rc.exe)默认只预定义 RC_INVOKED, 这里显式给出,
            #         使 iNES.rc 中 #ifdef _WIN32 段的 LANGUAGE 与
            #         #pragma code_page(65001)(UTF-8 资源) 生效。
            _WIN32
            _WINDOWS
            _CRT_SECURE_NO_WARNINGS
            UNICODE
            _UNICODE
            $<$<CONFIG:Debug>:_DEBUG>)

        # stdafx.c 用 #pragma comment 引入了 winmm/ws2_32, 这里显式列出
        target_link_libraries(iNES PRIVATE
            winmm
            ws2_32
            comdlg32
            comctl32
            shell32)

        # 输出命名对齐原工程: Release=iNES.exe, Debug=iNES_d.exe
        set_target_properties(iNES PROPERTIES
            OUTPUT_NAME "iNES"
            DEBUG_POSTFIX "_d")

        ines_set_output_dirs(iNES)

        if(MSVC)
            target_compile_options(iNES PRIVATE /W3 /utf-8)
        endif()

        message(STATUS "iNES: 构建 Win32 GUI 模拟器 iNES")
    endif()
endif()

# =====================================================================
# 构建配置摘要
# =====================================================================
message(STATUS "iNES: 输出目录         = ${INES_OUTPUT_DIR}")
message(STATUS "iNES: 目标平台/生成器  = ${CMAKE_GENERATOR_PLATFORM} / ${CMAKE_GENERATOR}")
message(STATUS "iNES: 核心库(inescore) = ${INES_BUILD_CORE}")
message(STATUS "iNES: GUI  (iNES)      = ${INES_BUILD_GUI}")
