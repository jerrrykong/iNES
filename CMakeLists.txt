# =====================================================================
# iNES -- NES(FC) 模拟器  CMake 构建脚本
#
# 移植自 project/win32 下的 Visual C++ 2008 工程:
#   iNES.vcproj         -> 目标 iNES    (Win32 GUI 模拟器, 输出 iNES[_d].exe)
#   iNesCore_DLL.vcproj -> 目标 inescore(模拟核心共享库, 输出 inescore.dll/.so)
#
# 用法示例(在仓库根目录执行; 原 VC2008 工程目录 project/ 未被 git 跟踪(.gitignore)且本机副本已删除,
# 下面第 4~6 行提到它只是说明本脚本的移植来源):
#   cmake -S . -B build                          # 配置, 生成 build/ 构建树
#   cmake --build build --config Release         # Release -> bin/iNES.exe
#   cmake --build build --config Debug           # Debug   -> bin/iNES_d.exe
#   构建产物统一输出到仓库 bin/ 目录(与原 VC 工程一致)
#
# Windows 上默认生成器为 "Visual Studio 17 2022"(多配置: Debug/Release/MinSizeRel/RelWithDebInfo),
# 构建类型由 --config 决定: **不带 --config 时默认构建 Debug**。因此"只看到 Debug 产物刷新、
# Release 还是旧的"是预期行为, 更新 Release 必须显式 --config Release
# (也可在配置时传 -DCMAKE_DEFAULT_BUILD_TYPE=Release 改掉默认值, 需 CMake 3.29+)。
# 单配置生成器(Unix Makefiles / Ninja)才不需要 --config, 构建类型由 CMAKE_BUILD_TYPE 决定
# (默认 Release); 只有这类生成器切换 Debug/Release 才需要用独立构建目录, 避免缓存互相污染。
#
# x64(已评估安全):
#   cmake -S . -B build-x64 -A x64
#   cmake --build build-x64 --config Release
#
# 注意事项:
#   1) 与原始 VC 工程一致: iNES.exe 按 Unicode 字符集编译, inescore 按
#      多字节(MBCS)字符集编译, 因此 core/comm 会被分别编译进两个目标。
#   2) 所有配置都输出到同一个 bin/: iNES 用 DEBUG_POSTFIX="_d" 区分(iNES.exe / iNES_d.exe),
#      而 inescore 的输出名不带配置后缀, Debug/Release 的 inescore.dll 会互相覆盖
#      (详见下面的 ines_set_output_dirs)。
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
# 注意: 该变量只对 Windows 生成器(Visual Studio)有效, 非 Windows 平台设置它会导致
#       配置失败, 因此必须用 if(WIN32) 守卫。
if(WIN32)
    if(NOT DEFINED CMAKE_GENERATOR_PLATFORM OR "${CMAKE_GENERATOR_PLATFORM}" STREQUAL "")
        set(CMAKE_GENERATOR_PLATFORM "Win32" CACHE STRING "目标平台(原 VC 工程为 Win32)" FORCE)
    endif()
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

# macOS 前端使用 Objective-C(非 ObjC++, 不引入 C++ 运行时), 需显式启用该语言。
# 必须在 project() 之后调用。
if(APPLE)
    enable_language(OBJC)
endif()

# ---------------------------------------------------------------------
# 默认构建类型: Release(全优化)
#
# 单配置生成器(Unix Makefiles / Ninja / MinGW Makefiles)下, 若配置时不带
# -DCMAKE_BUILD_TYPE=..., CMake 会得到"空构建类型": 既没有 -O3 也没有
# -DNDEBUG, 且 core/ 内的 assert 全部生效, 模拟器帧率会明显下降。
# 这里统一默认成 Release(即 -O3 -DNDEBUG 全优化), 只有显式传入
# -DCMAKE_BUILD_TYPE=Debug(或 RelWithDebInfo/MinSizeRel)时才按指定值构建。
#
# 注意: 多配置生成器(Visual Studio / Xcode / Ninja Multi-Config)的构建类型
#       由 --config 决定, 不受本变量影响, 仍需 cmake --build <dir> --config Release;
#       且不带 --config 时默认构建的是 Debug(实测 VS 2022 如此), 想改默认值请配
#       CMAKE_DEFAULT_BUILD_TYPE(CMake 3.29+)。
# ---------------------------------------------------------------------
if(NOT CMAKE_CONFIGURATION_TYPES)
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "构建类型" FORCE)
        message(STATUS "iNES: 未指定 CMAKE_BUILD_TYPE, 默认按 Release(全优化)构建")
    endif()
    # 让 ccmake / cmake-gui 以下拉框形式列出可选值
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

set(INES_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# ---------------------------------------------------------------------
# 选项与缓存变量
# ---------------------------------------------------------------------
option(INES_BUILD_CORE "构建核心共享库 inescore(对应 iNesCore_DLL.vcproj)" ON)
option(INES_BUILD_GUI  "构建 Win32 GUI 模拟器 iNES(对应 iNES.vcproj)"     ON)
option(INES_BUILD_MAC_GUI "构建 macOS GUI 模拟器 iNES.app"                ON)
# 输出目录: 原 VC 工程把可执行与 dll 统一输出到仓库 bin/
set(INES_OUTPUT_DIR "${INES_DIR}/bin" CACHE PATH
    "可执行文件/共享库输出目录(默认 <仓库>/bin)")

# macOS 最低系统版本(Universal 二进制含 arm64 时不能低于 11.0)
if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS 最低系统版本")
endif()

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
    "${INES_DIR}/comm/lan.c"
    "${INES_DIR}/comm/log.c"
    "${INES_DIR}/comm/net.c"
    "${INES_DIR}/comm/npsession.c"
    "${INES_DIR}/comm/platform.c")

# comm/thread.c 仅被 iNesCore_DLL 使用(iNES.exe 不包含)
set(INES_THREAD_SOURCE "${INES_DIR}/comm/thread.c")

# win32 GUI 源文件(iNES.vcproj 的 win32 分组)
set(INES_WIN32_SOURCES
    "${INES_DIR}/win32/dlgLanMatch.c"
    "${INES_DIR}/win32/dlgOpenRom.c"
    "${INES_DIR}/win32/dlgNetPlay.c"
    "${INES_DIR}/win32/iNES.c"
    "${INES_DIR}/win32/stdafx.c"
    "${INES_DIR}/win32/wMemory.c"
    "${INES_DIR}/win32/wNameTable.c"
    "${INES_DIR}/win32/wPalette.c"
    "${INES_DIR}/win32/wPatternTable.c"
    "${INES_DIR}/win32/wRegister.c"
    "${INES_DIR}/win32/wSPmemory.c"
    "${INES_DIR}/win32/wVmemory.c")

# mac 前端源文件(Objective-C, 仅 APPLE 参与构建; core/comm 与 win32 共用)
set(INES_MAC_OBJC_SOURCES
    "${INES_DIR}/mac/main.m"
    "${INES_DIR}/mac/iNESApp.m"
    "${INES_DIR}/mac/iNESOpenRomDialog.m"
    "${INES_DIR}/mac/iNESVideo.m"
    "${INES_DIR}/mac/iNESAudio.m"
    "${INES_DIR}/mac/iNESDebug.m"
    "${INES_DIR}/mac/iNESDebugView.m"
    "${INES_DIR}/mac/iNESRegisterView.m"
    "${INES_DIR}/mac/iNESNetPlayDialog.m"
    "${INES_DIR}/mac/iNESLanLobby.m")

set(INES_MAC_C_SOURCES
    "${INES_DIR}/mac/iNESOsd.c"
    "${INES_DIR}/mac/iNESPalette.c"
    "${INES_DIR}/mac/iNESConfig.c")

set(INES_MAC_SOURCES
    ${INES_MAC_OBJC_SOURCES}
    ${INES_MAC_C_SOURCES})

# macOS 应用图标: 由 mac/icon/iNES-icon.svg 生成(见 mac/icon/make-icns.sh 与 mac/icon/make-ico.ps1)。
# 借助 MACOSX_PACKAGE_LOCATION 让 CMake 把它拷进 iNES.app/Contents/Resources/。
# 注意: 只在 APPLE 下检查文件是否齐全 —— 写成 "APPLE AND EXISTS" 的话, 非 Apple 平台
#       该条件恒为假, 会误报"缺少 macOS 图标"。
set(INES_MAC_ICON "${INES_DIR}/mac/icon/iNES.icns")
if(APPLE)
    if(EXISTS "${INES_MAC_ICON}")
        set_source_files_properties("${INES_MAC_ICON}"
            PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    else()
        message(WARNING "iNES: 缺少 macOS 图标 ${INES_MAC_ICON}, 应用将显示默认图标")
    endif()
endif()

# 前端使用 ARC(自动引用计数): 视图/音频模块依赖 weak 属性与 block 捕获语义。
# 注意: -fobjc-arc 只对 Objective-C 源文件生效, 传给 .c 文件会被 clang 拒绝,
#       因此这里按"源文件属性"逐个 .m 设置, 而不是给整个目标加编译选项。
if(APPLE)
    set_source_files_properties(${INES_MAC_OBJC_SOURCES}
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()

# ---------------------------------------------------------------------
# 辅助函数: 输出目录对齐原工程 bin/ 布局
#
# 四种配置都指向同一个 bin/(与原 VC 工程一致, 不按配置分子目录)。由此带来一个
# 已知行为: 输出名不带配置后缀的目标, 其产物会被后构建的配置覆盖 ——
#   iNES:     用 DEBUG_POSTFIX="_d" 区分(iNES.exe / iNES_d.exe)
#   inescore: 无后缀, Debug 与 Release 都是 inescore.dll(含 .lib/.exp/.pdb),
#             两者互相覆盖, bin/ 下始终只保留最后构建的那一份, 这是有意沿用原工程行为。
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
        # UNIX/Android/macOS 风格: comm/net.c、thread.c 走 POSIX 分支
        # (源码统一判断 INES_POSIX, idef.h 中也会依据 __APPLE__/__linux__ 自动归一化)
        target_compile_definitions(inescore PRIVATE INES_POSIX)
        find_package(Threads REQUIRED)
        target_link_libraries(inescore PRIVATE Threads::Threads)
        if(NOT APPLE)
            # macOS 的 libSystem 已包含 libm, 显式链接 -lm 在部分 SDK 上会告警
            target_link_libraries(inescore PRIVATE m)
        endif()
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
# 目标 3: iNES macOS GUI 模拟器 (iNES.app)
# 字符集: UTF-8(NSString) ; 依赖: Cocoa/AudioToolbox/CoreAudio/QuartzCore
# 复用:   core + comm(含 thread.c, 因其线程/互斥量封装已跨平台)
# =====================================================================
if(INES_BUILD_MAC_GUI)
    if(NOT APPLE)
        message(STATUS "iNES: 跳过 macOS GUI 目标(当前平台非 APPLE)")
    else()
        add_executable(iNES MACOSX_BUNDLE
            ${INES_CORE_SOURCES}
            ${INES_COMMON_COMM_SOURCES}
            ${INES_THREAD_SOURCE}
            ${INES_MAC_SOURCES}
            ${INES_MAC_ICON})

        target_include_directories(iNES PRIVATE "${INES_DIR}")

        # 与 win32 侧一致: core/comm 依赖 INES_POSIX 判断平台分支
        target_compile_definitions(iNES PRIVATE INES_POSIX)

        target_link_libraries(iNES PRIVATE
            "-framework Cocoa"
            "-framework AudioToolbox"
            "-framework CoreAudio"
            "-framework QuartzCore")

        set_target_properties(iNES PROPERTIES
            OUTPUT_NAME "iNES"
            MACOSX_BUNDLE_INFO_PLIST "${INES_DIR}/mac/Info.plist"
            # 图标文件名需与 Info.plist 的 CFBundleIconFile 及 Resources 下的实际文件一致
            MACOSX_BUNDLE_ICON_FILE "iNES.icns"
            MACOSX_BUNDLE_BUNDLE_NAME "iNES"
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.ines.emulator"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_COPYRIGHT "Copyright (C) 2015")

        # -Wno-deprecated-declarations: NSOpenPanel.allowedFileTypes 等在
        #                              macOS 12 起被标记为弃用, 但为兼容 11.0 仍在使用
        target_compile_options(iNES PRIVATE
            -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
            -Wno-deprecated-declarations)

        ines_set_output_dirs(iNES)

        message(STATUS "iNES: 构建 macOS GUI 模拟器 iNES.app")
    endif()
endif()

# =====================================================================
# 构建配置摘要
# =====================================================================
message(STATUS "iNES: 输出目录         = ${INES_OUTPUT_DIR}")
if(CMAKE_CONFIGURATION_TYPES)
    message(STATUS "iNES: 构建类型         = 多配置(${CMAKE_CONFIGURATION_TYPES}), 构建时用 --config 指定")
else()
    message(STATUS "iNES: 构建类型         = ${CMAKE_BUILD_TYPE}")
endif()
message(STATUS "iNES: Release 优化标志 = ${CMAKE_C_FLAGS_RELEASE}")
message(STATUS "iNES: 目标平台/生成器  = ${CMAKE_GENERATOR_PLATFORM} / ${CMAKE_GENERATOR}")
message(STATUS "iNES: 核心库(inescore) = ${INES_BUILD_CORE}")
message(STATUS "iNES: GUI  (iNES)      = ${INES_BUILD_GUI}")
message(STATUS "iNES: GUI  (macOS)     = ${INES_BUILD_MAC_GUI}")
