@echo off
rem ============================================================
rem ApolloCalc Desktop Demo - build the bridge DLL (ApolloCalc.dll)
rem
rem Physically isolated from the core library build system: this script
rem compiles the core sources directly and never touches the core
rem Makefiles (C/128KB-ProTEXT/, C/128KB/ stay byte-identical).
rem
rem Prerequisite: MinGW-w64 gcc in PATH (repo baseline: gcc 8.1.0).
rem Usage:        c_bridge\build.bat   (from any working directory)
rem Output:       c_bridge\ApolloCalc.dll
rem ============================================================
setlocal

set "HERE=%~dp0"
set "PRO_SRC=%HERE%..\..\..\C\128KB-ProTEXT\src"

where gcc >nul 2>nul
if errorlevel 1 (
    echo [ERROR] gcc not found in PATH. Install MinGW-w64 first.
    exit /b 1
)

if not exist "%PRO_SRC%\ac_pro.h" (
    echo [ERROR] core sources not found: %PRO_SRC%
    exit /b 1
)

rem Flags mirror the core Makefile: -std=c99 -O2 -Wall -Wextra
rem -Wstrict-prototypes -Werror -DAC_PRO=1.
rem MinGW gcc expands the *.c wildcard itself (cmd.exe does not).
rem Compile list = bridge TU + ALL ProTEXT wrapper/extension TUs.
rem NEVER add C/128KB/src/*.c to this list: upstream .c files are already
rem compiled once inside the wrapper TUs via #include - linking them again
rem means "multiple definition" (see core Makefile header note 3).
gcc -std=c99 -O2 -Wall -Wextra -Wstrict-prototypes -Werror -DAC_PRO=1 ^
    -DAC_GUI_BUILDING_DLL -static-libgcc ^
    -shared -o "%HERE%ApolloCalc.dll" ^
    "%HERE%ac_gui_bridge.c" "%PRO_SRC%\*.c"

if errorlevel 1 (
    echo [ERROR] DLL build failed.
    exit /b 1
)

echo [OK] built "%HERE%ApolloCalc.dll"
endlocal
