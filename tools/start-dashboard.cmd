@echo off
REM Start the RPRMS Node-RED dashboard.
REM   Editor:    http://127.0.0.1:1880
REM   Dashboard: http://127.0.0.1:1880/ui
REM
REM Works in two situations:
REM   1) The author's portable bundle (<workspace>\tools\node + <workspace>\tools\nodered),
REM      which sits two levels above this script.
REM   2) A normal machine with Node-RED installed globally:
REM         npm install -g --unsafe-perm node-red node-red-dashboard
setlocal
set "WS=%~dp0..\.."

if exist "%WS%\tools\nodered\node_modules\.bin\node-red.cmd" (
  set "PATH=%WS%\tools\node;%PATH%"
  cd /d "%WS%\tools\nodered"
  echo Starting Node-RED from the portable bundle...
  echo   Editor    http://127.0.0.1:1880
  echo   Dashboard http://127.0.0.1:1880/ui
  echo.
  call "%WS%\tools\nodered\node_modules\.bin\node-red.cmd" --settings "%WS%\tools\nodered\data\settings.js"
  goto :done
)

where node-red >nul 2>nul
if %errorlevel%==0 (
  echo Starting Node-RED from the global install...
  echo   First run only: menu - Import - paste dashboards\node-red-flows.json - Deploy.
  echo   Editor    http://127.0.0.1:1880
  echo   Dashboard http://127.0.0.1:1880/ui
  echo.
  node-red
  goto :done
)

echo Node-RED was not found.
echo Install it with:  npm install -g --unsafe-perm node-red node-red-dashboard
echo Then run this script again, import dashboards\node-red-flows.json and Deploy.
echo See docs\NODE_RED_SETUP.md for the full steps.
exit /b 1

:done
endlocal
