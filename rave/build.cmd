@echo off
rem BC7Enc.dll (RAVE's texture cache block encoder - rave_bc7enc.cpp on Rich Geldreich's bc7enc / rgbcx, MIT).
rem   build.cmd        -> BC7Enc.dll here
rem   build.cmd test   -> also builds rave_bc7enc_test.exe and runs it against the new DLL
rem The encoder sources: bc7enc\ next to this file (RAVE repo: RaveNative/BC7Enc) or the folder above (the bc7enc fork's
rem rave\ folder). The game's copy is RaveURP + RaveHD Assets/RaveGame/Plugins/x86_64/BC7Enc.dll (copied by hand after
rem a passing test).
setlocal
cd /d "%~dp0"
set PATH=F:\WORK\_tools\msys64\mingw64\bin;%PATH%
set SRC=..
if exist bc7enc\rgbcx.h set SRC=bc7enc
gcc -O2 -c %SRC%\bc7enc.c -o bc7enc.o || goto :fail
g++ -O2 -std=c++17 -I%SRC% -shared -o BC7Enc.dll rave_bc7enc.cpp bc7enc.o -static -static-libgcc -static-libstdc++ || goto :fail
del bc7enc.o
echo built BC7Enc.dll
if /i not "%1"=="test" goto :eof
g++ -O2 -std=c++17 -I%SRC% -o rave_bc7enc_test.exe rave_bc7enc_test.cpp %SRC%\bc7decomp.cpp -static -static-libgcc -static-libstdc++ || goto :fail
.\rave_bc7enc_test.exe BC7Enc.dll
exit /b %errorlevel%
:fail
echo BUILD FAILED
exit /b 1
