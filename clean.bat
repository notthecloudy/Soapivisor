@echo off
del *.sdf
del *.VC.db
del /s *.aps
del /a:h *.suo
rmdir /s /q .vs
rmdir /s /q ipch
rmdir /s /q x64
rmdir /s /q Debug
rmdir /s /q Release
rmdir /s /q Soapivisor\x64
rmdir /s /q Soapivisor\Debug
rmdir /s /q Soapivisor\Release
rmdir /s /q doxygen
del /q Soapivisor_signed.efi
del /q sb_cert.crt
del /q sb_key.key
pause
