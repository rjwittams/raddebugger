@echo off
setlocal EnableExtensions
pushd "%~dp0..\..\.."
if errorlevel 1 exit /b 1
if not exist build mkdir build
cl /nologo /Z7 /Od /Zc:preprocessor /I src %IPC_TEST_CFLAGS% src\ipc\tests\requests.c /Fo:build\ipc_requests.obj /Fe:build\ipc_requests.exe /link /INCREMENTAL:NO
if errorlevel 1 exit /b 1
py -3 -c "import subprocess,sys; subprocess.run(['build/ipc_requests.exe']+sys.argv[1:],check=True,timeout=30)" %*
set "task_result=%ERRORLEVEL%"
popd
exit /b %task_result%
