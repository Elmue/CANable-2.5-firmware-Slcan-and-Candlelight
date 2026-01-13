
REM Cleanup all intermediate files from Visual Studio

attrib -h "*.suo"
del "*.suo"

rmdir "Output" /S /Q
rmdir "bin" /S /Q
rmdir "obj" /S /Q
