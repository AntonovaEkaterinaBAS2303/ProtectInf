@echo off
echo ========================================
echo Force Regenerating RPC Stubs
echo ========================================

set MIDL="C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\midl.exe"

if not exist %MIDL% (
    echo ERROR: MIDL not found at %MIDL%
    pause
    exit /b 1
)

echo Using: %MIDL%
echo.

REM Создаем чистую директорию
if exist generated rmdir /s /q generated
mkdir generated

echo Compiling IDL...
%MIDL% ^
    /out generated ^
    /h generated\service_rpc.h ^
    /cstub generated\rpc_c.c ^
    /sstub generated\rpc_s.c ^
    src\common\rpc_interface.idl

if %ERRORLEVEL% == 0 (
    echo.
    echo SUCCESS!
    echo Files generated:
    dir /b generated\*
    echo.
    echo Copying to build directory placeholder...
    if not exist "out\build\x64-debug\generated" mkdir "out\build\x64-debug\generated"
    copy /Y generated\* out\build\x64-debug\generated\
    echo Done!
) else (
    echo.
    echo FAILED with error %ERRORLEVEL%
    echo.
    echo Checking IDL file...
    if exist src\common\rpc_interface.idl (
        echo IDL file exists
        echo Content:
        type src\common\rpc_interface.idl
    ) else (
        echo IDL FILE MISSING!
    )
)
pause