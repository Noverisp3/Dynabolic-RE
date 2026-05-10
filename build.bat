@echo off
REM Build script for Windows with Visual Studio
REM Organized project structure with separate include, src, and build directories

echo Building Dynabolic-LM - Pure C++ Graph-Based AI Architecture
echo ============================================================

REM Check if cl.exe is available
where cl.exe >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo Error: Visual Studio C++ compiler not found
    echo Please run this script from Visual Studio Developer Command Prompt
    echo Or run: "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    exit /b 1
)

REM Create build directory
if not exist build mkdir build

REM Compile the project
echo Compiling source files...
cl /std:c++17 /EHsc /O2 /MD /I.\include src\graph_node.cpp src\reasoning_engine.cpp src\json_parser.cpp examples\demo.cpp /Fe:build\dynabolic_demo.exe

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Build successful! Executable: build\dynabolic_demo.exe
    echo.
    echo Running demo...
    cd build
    dynabolic_demo.exe
    cd ..
) else (
    echo.
    echo Build failed. Please check the error messages above.
    exit /b 1
)

pause