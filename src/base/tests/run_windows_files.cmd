@echo off
setlocal EnableExtensions
pushd "%~dp0..\..\.."
if errorlevel 1 exit /b 1
if not defined BASE_FILE_TEST_SOURCE_ROOT set "BASE_FILE_TEST_SOURCE_ROOT=."
if not exist build mkdir build
cl /nologo /Z7 /Od /Zc:preprocessor /I "%BASE_FILE_TEST_SOURCE_ROOT%\src" %BASE_FILE_TEST_CFLAGS% src\base\tests\files.c /Fo:build\base_files_tests.obj /Fe:build\base_files_tests.exe /link /INCREMENTAL:NO
if errorlevel 1 exit /b 1
py -3 src\base\tests\run_files.py build\base_files_tests.exe %*
set "task_result=%ERRORLEVEL%"
popd
exit /b %task_result%
