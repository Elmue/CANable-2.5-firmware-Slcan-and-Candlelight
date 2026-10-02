
REM Cleanup all intermediate files from Visual Studio

attrib -h "*.suo"
del "*.suo"
del "*.user"

copy Output\CANableDemo.exe ..\CANableDemo.exe

rmdir "Output" /S /Q
rmdir "bin" /S /Q
rmdir "obj" /S /Q
