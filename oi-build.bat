@echo off
rem Compila el fork (rama overinteractive) con MSVC + Ninja en build-msvc.
rem Deja el ejecutable en build-msvc\output\bin\thextech.exe.
rem
rem Antes vivia en la carpeta temporal de la sesion y se perdio al vaciarla: las compilaciones "sin errores"
rem en realidad no hacian nada. Ahora vive en el repo.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0build-msvc" || exit /b 1
"C:\PROGRA~2\MICROS~2\2022\BUILDT~1\Common7\IDE\COMMON~1\MICROS~1\CMake\Ninja\ninja.exe" thextech
exit /b %ERRORLEVEL%
