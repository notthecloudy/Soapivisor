@echo off
setlocal
echo ========================================================
echo Soapivisor Secure Boot Key Enrollment Setup
echo ========================================================
echo.
echo This script will help you sign Soapivisor.efi for Secure Boot.
echo.
echo PREREQUISITES:
echo 1. OpenSSL (for certificate generation)
echo 2. sbsign (Linux/WSL) OR SignTool (Windows SDK)
echo.

if exist sb_cert.crt (
    echo [INFO] Existing certificate found. Skipping generation.
) else (
    echo 1. Generating self-signed root certificate...
    openssl req -newkey rsa:2048 -nodes -keyout sb_key.key -new -x509 -days 3650 -subj "/CN=Soapivisor Research Key" -out sb_cert.crt
    if %ERRORLEVEL% NEQ 0 (
        echo [ERROR] Failed to generate certificate. 
        echo TIP: You can also use PowerShell: New-SelfSignedCertificate -Type CodeSigningCert
        exit /b 1
    )
)

echo.
echo 2. Signing Soapivisor.efi...
if not exist Soapivisor.efi (
    echo [ERROR] Soapivisor.efi not found in current directory.
    exit /b 1
)

sbsign --key sb_key.key --cert sb_cert.crt --output Soapivisor_signed.efi Soapivisor.efi 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [WARN] sbsign failed or not found. 
    echo Attempting Windows SignTool...
    signtool sign /f sb_cert.pfx /p password /t http://timestamp.digicert.com Soapivisor.efi
    if %ERRORLEVEL% NEQ 0 (
        echo [ERROR] Signing failed. Please sign manually or use WSL sbsign.
        exit /b 1
    )
)

echo.
echo [SUCCESS] Soapivisor signed!
echo.
echo CRITICAL NEXT STEPS:
echo 1. Copy 'sb_cert.crt' to a FAT32 USB drive.
echo 2. Reboot into UEFI/BIOS -> Secure Boot Settings.
echo 3. Change "Secure Boot Mode" to 'Custom' (if necessary).
echo 4. Find "Key Management" or "Authorized Signatures".
echo 5. Choose "Append" or "Enroll New Key" for the 'db' database.
echo 6. Select 'sb_cert.crt' from the USB drive.
echo 7. Your machine will now trust and execute Soapivisor with Secure Boot ON.
echo ========================================================
pause
