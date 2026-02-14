@echo off
setlocal

set "ROOT_DIR=%~dp0"
set "CORE_DIR=%ROOT_DIR%AlpacaCore"
set "HTTP_DIR=%ROOT_DIR%AlpacaHTTP"
set "AGENT_DIR=%ROOT_DIR%AlpacaAgent"
set "CORE_BUILD_DIR=%CORE_DIR%\\build"
set "HTTP_BUILD_DIR=%HTTP_DIR%\\build"
set "AGENT_BUILD_DIR=%AGENT_DIR%\\build"
if "%ALPACAHTTP_USE_BOOST_BEAST%"=="" set "ALPACAHTTP_USE_BOOST_BEAST=OFF"
if "%ALPACACORE_ENABLE_ALL_VENDORS%"=="" set "ALPACACORE_ENABLE_ALL_VENDORS=ON"

if not exist "%CORE_DIR%" (
  echo AlpacaCore not found at %CORE_DIR%
  exit /b 1
)

if not exist "%HTTP_DIR%" (
  echo AlpacaHTTP not found at %HTTP_DIR%
  exit /b 1
)

if not exist "%AGENT_DIR%" (
  echo AlpacaAgent not found at %AGENT_DIR%
  exit /b 1
)

if exist "%CORE_BUILD_DIR%" rmdir /s /q "%CORE_BUILD_DIR%"
if exist "%HTTP_BUILD_DIR%" rmdir /s /q "%HTTP_BUILD_DIR%"
if exist "%AGENT_BUILD_DIR%" rmdir /s /q "%AGENT_BUILD_DIR%"

set "BUILD_CONFIG_ARG="
set "CTEST_CONFIG_ARG="
if not "%ALPACA_BUILD_CONFIG%"=="" (
  set "BUILD_CONFIG_ARG=--config %ALPACA_BUILD_CONFIG%"
)

set "PARALLEL_ARG="
if not "%NUMBER_OF_PROCESSORS%"=="" (
  set "PARALLEL_ARG=--parallel %NUMBER_OF_PROCESSORS%"
)

echo == AlpacaCore ==
cmake -S "%CORE_DIR%" -B "%CORE_BUILD_DIR%" -DALPACACORE_ENABLE_ALL_VENDORS=%ALPACACORE_ENABLE_ALL_VENDORS%
if errorlevel 1 exit /b 1
cmake --build "%CORE_BUILD_DIR%" --target clean %BUILD_CONFIG_ARG%
if errorlevel 1 exit /b 1
cmake --build "%CORE_BUILD_DIR%" %BUILD_CONFIG_ARG% %PARALLEL_ARG%
if errorlevel 1 exit /b 1

echo == AlpacaHTTP ==
cmake -S "%HTTP_DIR%" -B "%HTTP_BUILD_DIR%" -DALPACAHTTP_USE_BOOST_BEAST=%ALPACAHTTP_USE_BOOST_BEAST% -DALPACACORE_ENABLE_ALL_VENDORS=%ALPACACORE_ENABLE_ALL_VENDORS%
if errorlevel 1 exit /b 1
cmake --build "%HTTP_BUILD_DIR%" --target clean %BUILD_CONFIG_ARG%
if errorlevel 1 exit /b 1
cmake --build "%HTTP_BUILD_DIR%" %BUILD_CONFIG_ARG% %PARALLEL_ARG%
if errorlevel 1 exit /b 1

echo == AlpacaAgent ==
cmake -S "%AGENT_DIR%" -B "%AGENT_BUILD_DIR%"
if errorlevel 1 exit /b 1
cmake --build "%AGENT_BUILD_DIR%" --target clean %BUILD_CONFIG_ARG%
if errorlevel 1 exit /b 1
cmake --build "%AGENT_BUILD_DIR%" %BUILD_CONFIG_ARG% %PARALLEL_ARG%
if errorlevel 1 exit /b 1

set "SERVER_EXE=%HTTP_BUILD_DIR%\alpacahttp_server.exe"
if not exist "%SERVER_EXE%" (
  if not "%ALPACA_BUILD_CONFIG%"=="" (
    set "SERVER_EXE=%HTTP_BUILD_DIR%\%ALPACA_BUILD_CONFIG%\alpacahttp_server.exe"
  ) else (
    for %%C in (Debug Release RelWithDebInfo MinSizeRel) do (
      if exist "%HTTP_BUILD_DIR%\%%C\alpacahttp_server.exe" (
        set "SERVER_EXE=%HTTP_BUILD_DIR%\%%C\alpacahttp_server.exe"
      )
    )
  )
)
if not exist "%SERVER_EXE%" (
  echo Could not find alpacahttp_server.exe in %HTTP_BUILD_DIR%
  exit /b 1
)

set "AGENT_PID="
set "AGENT_EXE=%AGENT_BUILD_DIR%\alpacaagent_server.exe"
if not exist "%AGENT_EXE%" (
  if not "%ALPACA_BUILD_CONFIG%"=="" (
    set "AGENT_EXE=%AGENT_BUILD_DIR%\%ALPACA_BUILD_CONFIG%\alpacaagent_server.exe"
  ) else (
    for %%C in (Debug Release RelWithDebInfo MinSizeRel) do (
      if exist "%AGENT_BUILD_DIR%\%%C\alpacaagent_server.exe" (
        set "AGENT_EXE=%AGENT_BUILD_DIR%\%%C\alpacaagent_server.exe"
      )
    )
  )
)
if not exist "%AGENT_EXE%" (
  echo Could not find alpacaagent_server.exe in %AGENT_BUILD_DIR%
  exit /b 1
)

for /f %%I in (`powershell -NoProfile -Command "$exe='%AGENT_EXE%';$cfg='%AGENT_DIR%\agent_config.json';if(Test-Path $cfg){$p=Start-Process -FilePath $exe -ArgumentList @('--config',$cfg) -PassThru}else{$p=Start-Process -FilePath $exe -PassThru};$p.Id"`) do set "AGENT_PID=%%I"
if "%AGENT_PID%"=="" (
  echo Failed to start AlpacaAgent process
  exit /b 1
)
echo AlpacaAgent is running on http://localhost:6810/

echo AlpacaHTTP is running. Open http://localhost:6800/ in your browser.
"%SERVER_EXE%"
set "SERVER_EXIT_CODE=%ERRORLEVEL%"

if not "%AGENT_PID%"=="" (
  taskkill /PID %AGENT_PID% /T /F >nul 2>&1
)

exit /b %SERVER_EXIT_CODE%

endlocal
