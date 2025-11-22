
REM Cleanup all intermediate files from Visual Studio

attrib -h "*.suo"
del "*.suo"
del "Output\*.pdb"
del "Output\*.vshost.exe"
del "Output\*.vshost.exe.manifest"

rmdir "bin" /S /Q
rmdir "obj" /S /Q
