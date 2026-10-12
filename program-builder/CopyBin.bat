@echo off

set Solution=
set Solution=%1

set Platform=
set Platform=%2

set Configuration=
set Configuration=%3

@rem Bail if we haven't got the parameters
if "%Solution%" == "" goto exit_noparam
if "%Platform%" == "" goto exit_noparam
if "%Configuration%" == "" goto exit_noparam

set Root=%~dp0

@rem
@rem BIN
@rem

@rem Create directory
set TargetBinDir="%Root%.\build\%Solution%\bin\%Platform%\%Configuration%"

if not exist %TargetBinDir% mkdir %TargetBinDir%

@rem Copy mpp-mesh
copy /Y "%Root%\..\mpp-mesh\build\%Solution%\bin\%Platform%\%Configuration%\*.dll" %TargetBinDir%

@rem Copy mpp-mesh-specification-parser
copy /Y "%Root%\..\mpp-mesh-specification-parser\build\%Solution%\bin\%Platform%\%Configuration%\*.dll" %TargetBinDir%

@rem Copy mpp-program
copy /Y "%Root%\..\mpp-program\build\%Solution%\bin\%Platform%\%Configuration%\*.dll" %TargetBinDir%

goto exit_success

@rem ERRORS

:exit_noparam
@echo syntax: CopyBin.bat <solution> <platform> <configuration>
exit 1

@rem SUCCESS

:exit_success
exit 0
