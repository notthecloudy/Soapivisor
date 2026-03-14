@echo off
echo ========================================================
echo Soapivisor Secure Boot Key Enrollment Setup
echo ========================================================
echo.
echo This script will generate a custom Secure Boot key and sign the EFI executable.
echo You must run this in an environment where OpenSSL and sbsign are available (e.g. WSL).
echo.
echo 1. Generating self-signed certificate...
openssl req -newkey rsa:2048 -nodes -keyout sb_key.key -new -x509 -days 3650 -subj "/CN=Soapivisor Research Key" -out sb_cert.crt
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Failed to generate certificate. Ensure OpenSSL is installed.
    exit /b %ERRORLEVEL%
)

echo.
echo 2. Signing Soapivisor.efi...
sbsign --key sb_key.key --cert sb_cert.crt --output Soapivisor_signed.efi Soapivisor.efi
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Failed to sign EFI. Ensure sbsign is installed and Soapivisor.efi exists.
    exit /b %ERRORLEVEL%
)

echo.
echo [SUCCESS] Soapivisor_signed.efi created!
echo.
echo Next steps:
echo 1. Reboot into your UEFI/BIOS firmware settings.
echo 2. Enter the Secure Boot key management section.
echo 3. Enroll 'sb_cert.crt' into the 'db' (Authorized Signature Database).
echo 4. Boot using 'Soapivisor_signed.efi'.
echo ========================================================
pause
