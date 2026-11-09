@echo off
rem ============================================================
rem ApolloCalc Desktop Demo - publish a framework-dependent EXE
rem
rem Output: publish\ApolloCalc.Desktop.exe (+ ApolloCalc.dll)
rem Target: any machine with the .NET 8 Desktop Runtime - just
rem         double-click the exe, no build tools required.
rem The output directory is set by PublishDir in the csproj
rem (demos/Desktop-Windows/publish/) and is gitignored.
rem ============================================================
setlocal

set "HERE=%~dp0"

where dotnet >nul 2>nul
if errorlevel 1 (
    echo [ERROR] dotnet not found in PATH. Install the .NET 8 SDK first.
    exit /b 1
)

rem The publish output must carry the native bridge DLL; build it if absent.
if not exist "%HERE%c_bridge\ApolloCalc.dll" (
    echo [INFO] c_bridge\ApolloCalc.dll missing - building it first...
    call "%HERE%c_bridge\build.bat"
    if errorlevel 1 (
        echo [ERROR] bridge DLL build failed.
        exit /b 1
    )
)

dotnet publish "%HERE%ApolloCalc.Desktop" -c Release -r win-x64 --self-contained false
if errorlevel 1 (
    echo [ERROR] publish failed.
    exit /b 1
)

echo [OK] published to "%HERE%publish" - run publish\ApolloCalc.Desktop.exe
endlocal
