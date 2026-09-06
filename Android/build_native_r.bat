@echo off
set NDK_ROOT_LOCAL=d:\android-ndk-r10e
%NDK_ROOT_LOCAL%\ndk-build  NDK_LOG=1 NDK_DEBUG=0 > build_r.log 2>&1
pause
