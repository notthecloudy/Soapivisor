@echo off
setlocal

rem Check for admin rights
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo [ERROR] Please run this script as Administrator.
    pause
    exit /b 1
)

set DRIVER_PATH=%~dp0Soapivisor.sys
if not exist "%DRIVER_PATH%" (
    echo [ERROR] Soapivisor.sys not found in %~dp0
    pause
    exit /b 1
)

echo Installing Soapivisor as a UEFI bootkit hypervisor...
sc create Soapivisor type= kernel start= boot binPath= "%DRIVER_PATH%"

if %errorLevel% equ 0 (
    echo [SUCCESS] Soapivisor installed successfully. Please restart your system to boot with the winload.efi bootkit.
) else (
    echo [ERROR] Failed to install Soapivisor.
)

pause
