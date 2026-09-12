@echo off
setlocal
rem Runtime asset paths are relative to the application source directory.
pushd "%~dp0..\%~2" || exit /b 1
"%~dp0..\bin\%~1\%~2\%~2.exe"
set "GENGINE_RUN_EXIT=%ERRORLEVEL%"
popd
exit /b %GENGINE_RUN_EXIT%
