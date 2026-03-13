@echo off
setlocal

echo [INFO] Soapivisor is a Native UEFI Hypervisor (Bootkit). 
echo [INFO] It cannot be installed using standard NT driver service control (sc.exe).
echo.
echo === Deployment Instructions ===
echo 1. Mount the EFI System Partition (ESP):
echo    mountvol X: /s
echo 2. Copy Soapivisor.efi to the ESP:
echo    copy x64\Release\Soapivisor.efi X:\EFI\Boot\
echo 3. Configure your Boot Manager (e.g., bcdedit) or Motherboard BIOS to load Soapivisor.efi before Windows bootmgfw.efi.
echo 4. Restart your system.
echo.
echo For manual removal, mount the ESP and delete the EFI file.
pause
