@echo off
set QT=D:\devtools\leafyrino-build\Qt\6.9.3\msvc2022_64
set SRC=D:\Sources\leafyrino
set OUT=D:\devtools\leafyrino-usercard-stage1\client-tests
call "D:\devtools\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist "%OUT%" mkdir "%OUT%"
"%QT%\bin\moc.exe" "%SRC%\src\providers\archive\ArchiveClient.hpp" -o "%OUT%\moc_ArchiveClient.cpp" || exit /b 1
cl /nologo /EHsc /std:c++20 /permissive- /Zc:__cplusplus /utf-8 /O2 /MD ^
 /DQT_NO_DEBUG /DQT_NO_KEYWORDS ^
 /I"%SRC%\src" /I"%QT%\include" /I"%QT%\include\QtCore" /I"%QT%\include\QtNetwork" ^
 "%SRC%\tests\ArchiveClientStandalone.cpp" "%SRC%\src\providers\archive\ArchiveClient.cpp" "%OUT%\moc_ArchiveClient.cpp" ^
 /Fo"%OUT%\\" /Fe"%OUT%\archive-client-tests.exe" ^
 /link "%QT%\lib\Qt6Core.lib" "%QT%\lib\Qt6Network.lib" || exit /b 1
copy /y "%QT%\bin\Qt6Core.dll" "%OUT%\" >nul
copy /y "%QT%\bin\Qt6Network.dll" "%OUT%\" >nul
"%OUT%\archive-client-tests.exe"
exit /b %ERRORLEVEL%
