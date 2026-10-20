@echo off
setlocal EnableExtensions
pushd "%~dp0..\..\.."
if errorlevel 1 exit /b 1
if not exist build mkdir build
cl /nologo /Z7 /Od /Zc:preprocessor /I src %SOCKET_TEST_CFLAGS% src\socket\tests\sessions.c /Fo:build\socket_sessions.obj /Fe:build\socket_sessions.exe /link /INCREMENTAL:NO
if errorlevel 1 exit /b 1
py -3 -c "import subprocess,sys; subprocess.run(['build/socket_sessions.exe']+sys.argv[1:],check=True,timeout=30)" %*
set "task_result=%ERRORLEVEL%"
popd
exit /b %task_result%
