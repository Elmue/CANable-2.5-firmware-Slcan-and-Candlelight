
REM Cleanup all intermediate files from Visual Studio

attrib -h "*.suo"
del "*.suo"
del "*.sdf"
del "*.user"
del "Output\*.pdb"
del "Output\*.ilk"
del "*.vshost.exe"
del "*.vshost.exe.manifest"

rmdir "Debug" /S /Q
rmdir "Release" /S /Q
rmdir "ipch" /S /Q
