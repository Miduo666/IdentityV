@echo off

REM Generate randomized build vars (module name / opt level / build entropy)
powershell -NoProfile -ExecutionPolicy Bypass -File "jni\tools\gen_build_vars.ps1"
if not exist "build_vars.bat" (
    echo [-] failed to generate random build vars, aborting
    pause
    exit /b 1
)
call build_vars.bat
del build_vars.bat

REM Run ndk-build and wait for completion
start /wait "" "E:\android-ndk-r27d-windows\android-ndk-r27d\ndk-build.cmd" NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=jni\Android.mk NDK_APPLICATION_MK=jni\Application.mk MODULE_NAME=%MODNAME% BUILD_OPT_LEVEL=%OPTLVL%

REM Move the built artifact to the bat's directory
if not exist "libs\arm64-v8a\%MODNAME%.sh" (
    echo [-] build artifact not found at libs\arm64-v8a\%MODNAME%.sh
    echo [-] ndk-build likely failed, or antivirus quarantined/locked it - check Windows Defender history
    pause
    exit /b 1
)

move /Y "libs\arm64-v8a\%MODNAME%.sh" ".\"
if not exist "%MODNAME%.sh" (
    echo [-] move failed even though the artifact existed under libs\arm64-v8a\
    echo [-] check antivirus quarantine / file lock on that file
    pause
    exit /b 1
)

echo [*] output: %MODNAME%.sh
pause
