@echo off
echo patching DragQueryFileA in picview.exe
gsar -sDragQueryFileA -rDragQueryFile:x00 -o picview.exe
pehdr-lite picview.exe -bssfix

if not %1.==/q. pause