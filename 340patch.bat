@echo off
echo patching DragQueryFileA in picview.exe
gsar -sDragQueryFileA -rDragQueryFile:x00 -o picview.exe

if not %1.==/q. pause