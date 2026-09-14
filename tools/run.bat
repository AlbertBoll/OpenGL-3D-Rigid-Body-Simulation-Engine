@echo off
setlocal
rem Assets resolve in the application from its executable location.
pushd "%~dp0..\bin\%~1\%~2" || exit /b 1
"%~dp0..\bin\%~1\%~2\%~2.exe"
set "GENGINE_RUN_EXIT=%ERRORLEVEL%"
popd
exit /b %GENGINE_RUN_EXIT%
