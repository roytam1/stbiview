set TCCPATH=F:\tinycc-win32
set TCCLPATH=%TCCPATH%\lib
%TCCPATH%\tcc -O2 picview.c %TCCLPATH%\wincrt1-crtdll.c %TCCLPATH%\crtdll-chkstk.S %TCCLPATH%\udivdi3.S %TCCLPATH%\umoddi3.S %TCCLPATH%\libm.c -nostdlib -lkernel32 -lcrtdll -lcomdlg32 -lshell32 -lgdi32 -luser32
%TCCPATH%\LinkRes2Exe picview.res picview.exe
set TCCPATH=
set TCCLPATH=
