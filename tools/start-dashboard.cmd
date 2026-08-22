@echo off
REM Start the RPRMS Node-RED dashboard.
REM Editor:    http://127.0.0.1:1880
REM Dashboard: http://127.0.0.1:1880/ui
setlocal
set "PATH=%~dp0tools\node;%PATH%"
cd /d "%~dp0tools\nodered"
echo Starting Node-RED...
echo   Editor    http://127.0.0.1:1880
echo   Dashboard http://127.0.0.1:1880/ui
echo.
call "%~dp0tools\nodered\node_modules\.bin\node-red.cmd" --settings "%~dp0tools\nodered\data\settings.js"
endlocal
