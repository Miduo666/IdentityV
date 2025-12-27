@echo off
cd /d C:\Users\31297\Desktop\stable\IdentityV

REM 启动 ndk-build 并等待完成
start /wait "" "E:\android-ndk-r27d-windows\android-ndk-r27d\ndk-build.cmd" NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=jni\Android.mk NDK_APPLICATION_MK=jni\Application.mk

REM 编译完成后移动文件到 bat 所在目录
move /Y "libs\arm64-v8a\Identity5.sh" ".\"

pause

