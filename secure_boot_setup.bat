<#
Soapivisor Secure Boot Helper (PowerShell GUI)
- Generates Code Signing cert (PFX + CER)
- Signs Soapivisor.efi with signtool if present
- Optionally attempts WSL+sbsign fallback (best-effort)
- Prepares USB and copies CER for UEFI enrollment
Run as Administrator.
#>

Add-Type -AssemblyName System.Windows.Forms, System.Drawing

function Show-Error($text) {
    [System.Windows.Forms.MessageBox]::Show($text, "Error", 'OK','Error') | Out-Null
}
function Show-Info($text) {
    [System.Windows.Forms.MessageBox]::Show($text, "Info", 'OK','Information') | Out-Null
}
function Log($text) {
    $global:logbox.AppendText("$text`r`n")
}

# UI
$form = New-Object System.Windows.Forms.Form
$form.Text = "Soapivisor Secure Boot Helper"
$form.Width = 760
$form.Height = 540
$form.StartPosition = 'CenterScreen'
$form.Topmost = $false
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false

# Labels & controls
$lblEfi = New-Object System.Windows.Forms.Label
$lblEfi.Text = "1) Soapivisor.efi path:"
$lblEfi.Location = New-Object System.Drawing.Point(10,10)
$lblEfi.AutoSize = $true
$form.Controls.Add($lblEfi)

$txtEfi = New-Object System.Windows.Forms.TextBox
$txtEfi.Location = New-Object System.Drawing.Point(10,30)
$txtEfi.Width = 560
$form.Controls.Add($txtEfi)

$btnBrowseEfi = New-Object System.Windows.Forms.Button
$btnBrowseEfi.Text = "Browse..."
$btnBrowseEfi.Location = New-Object System.Drawing.Point(580,28)
$btnBrowseEfi.Width = 140
$form.Controls.Add($btnBrowseEfi)

$lblOut = New-Object System.Windows.Forms.Label
$lblOut.Text = "Output folder (certs & signed EFI):"
$lblOut.Location = New-Object System.Drawing.Point(10,64)
$lblOut.AutoSize = $true
$form.Controls.Add($lblOut)

$txtOut = New-Object System.Windows.Forms.TextBox
$txtOut.Location = New-Object System.Drawing.Point(10,84)
$txtOut.Width = 560
$txtOut.Text = (Join-Path $env:USERPROFILE "Soapivisor-Signed")
$form.Controls.Add($txtOut)

$btnBrowseOut = New-Object System.Windows.Forms.Button
$btnBrowseOut.Text = "Browse..."
$btnBrowseOut.Location = New-Object System.Drawing.Point(580,82)
$btnBrowseOut.Width = 140
$form.Controls.Add($btnBrowseOut)

# Options
$chkUseWSL = New-Object System.Windows.Forms.CheckBox
$chkUseWSL.Text = "Enable WSL sbsign fallback (requires sbsign+openssl in WSL)"
$chkUseWSL.Location = New-Object System.Drawing.Point(10,120)
$chkUseWSL.AutoSize = $true
$form.Controls.Add($chkUseWSL)

$lblHz = New-Object System.Windows.Forms.Label
$lblHz.Text = "Default enroll instructions: copy .CER to USB and enroll in UEFI (manual step)."
$lblHz.Location = New-Object System.Drawing.Point(10,150)
$lblHz.AutoSize = $true
$form.Controls.Add($lblHz)

# USB selection
$lblUsb = New-Object System.Windows.Forms.Label
$lblUsb.Text = "Target USB drive (for .CER copy):"
$lblUsb.Location = New-Object System.Drawing.Point(10,180)
$lblUsb.AutoSize = $true
$form.Controls.Add($lblUsb)

$comboUsb = New-Object System.Windows.Forms.ComboBox
$comboUsb.Location = New-Object System.Drawing.Point(10,200)
$comboUsb.Width = 480
$form.Controls.Add($comboUsb)

$btnRefreshUsb = New-Object System.Windows.Forms.Button
$btnRefreshUsb.Text = "Refresh"
$btnRefreshUsb.Location = New-Object System.Drawing.Point(500,198)
$btnRefreshUsb.Width = 80
$form.Controls.Add($btnRefreshUsb)

$chkFormatUsb = New-Object System.Windows.Forms.CheckBox
$chkFormatUsb.Text = "Format USB to FAT32 (WARNING: Erases drive)"
$chkFormatUsb.Location = New-Object System.Drawing.Point(590,200)
$chkFormatUsb.AutoSize = $true
$form.Controls.Add($chkFormatUsb)

# Buttons
$btnGenerate = New-Object System.Windows.Forms.Button
$btnGenerate.Text = "Generate Cert + Sign EFI"
$btnGenerate.Location = New-Object System.Drawing.Point(10,240)
$btnGenerate.Width = 220
$form.Controls.Add($btnGenerate)

$btnCopyCer = New-Object System.Windows.Forms.Button
$btnCopyCer.Text = "Copy .CER to USB"
$btnCopyCer.Location = New-Object System.Drawing.Point(240,240)
$btnCopyCer.Width = 160
$form.Controls.Add($btnCopyCer)

$btnOpenOut = New-Object System.Windows.Forms.Button
$btnOpenOut.Text = "Open Output Folder"
$btnOpenOut.Location = New-Object System.Drawing.Point(410,240)
$btnOpenOut.Width = 140
$form.Controls.Add($btnOpenOut)

$btnExit = New-Object System.Windows.Forms.Button
$btnExit.Text = "Exit"
$btnExit.Location = New-Object System.Drawing.Point(560,240)
$btnExit.Width = 160
$form.Controls.Add($btnExit)

# Log box
$logbox = New-Object System.Windows.Forms.TextBox
$logbox.Location = New-Object System.Drawing.Point(10,290)
$logbox.Width = 710
$logbox.Height = 210
$logbox.Multiline = $true
$logbox.ScrollBars = 'Vertical'
$logbox.ReadOnly = $true
$form.Controls.Add($logbox)

# Helpers
function Refresh-UsbList {
    $comboUsb.Items.Clear()
    $drives = Get-WmiObject Win32_Volume | Where-Object { $_.DriveType -eq 2 -and $_.FileSystem -ne $null } # removable and formatted
    foreach ($d in $drives) {
        $display = "$($d.DriveLetter)  -  $($d.Label)  ($([math]::Round($d.Capacity/1GB,2)))GB"
        $comboUsb.Items.Add($display) | Out-Null
    }
    if ($comboUsb.Items.Count -gt 0) { $comboUsb.SelectedIndex = 0 }
    Log "Refreshed USB list (found $($comboUsb.Items.Count) removable volumes)."
}

function Ensure-Admin {
    $isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $isAdmin) {
        Show-Error "This tool may require Administrator privileges for some operations (creating certificates, formatting drives). Please re-run PowerShell as Administrator."
        return $false
    }
    return $true
}

function Find-Signtool {
    $paths = @(
        "$Env:ProgramFiles(x86)\Windows Kits\10\bin",
        "$Env:ProgramFiles(x86)\Windows Kits\10\bin\x64",
        "$Env:ProgramFiles\Windows Kits\10\bin",
        "$Env:ProgramFiles\Windows Kits\10\bin\x64"
    )
    $found = $null
    foreach ($p in $paths) {
        if (Test-Path $p) {
            $f = Get-ChildItem -Path $p -Filter signtool.exe -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($f) { $found = $f.FullName; break }
        }
    }
    # try where.exe
    if (-not $found) {
        try { $w = (where.exe signtool.exe 2>$null) -split "`r`n" | Select-Object -First 1; if ($w) { $found = $w } } catch {}
    }
    return $found
}

# Event handlers
$btnBrowseEfi.Add_Click({
    $ofd = New-Object System.Windows.Forms.OpenFileDialog
    $ofd.Filter = "EFI Files (*.efi)|*.efi|All files (*.*)|*.*"
    if ($ofd.ShowDialog() -eq 'OK') { $txtEfi.Text = $ofd.FileName }
})

$btnBrowseOut.Add_Click({
    $fbd = New-Object System.Windows.Forms.FolderBrowserDialog
    if ($fbd.ShowDialog() -eq 'OK') { $txtOut.Text = $fbd.SelectedPath }
})

$btnRefreshUsb.Add_Click({ Refresh-UsbList })

$btnOpenOut.Add_Click({
    if (-not (Test-Path $txtOut.Text)) { New-Item -Path $txtOut.Text -ItemType Directory -Force | Out-Null }
    explorer.exe $txtOut.Text
})

$btnExit.Add_Click({ $form.Close() })

# Core action: generate cert and sign
$btnGenerate.Add_Click({
    if (-not (Ensure-Admin)) { return }
    $efiPath = $txtEfi.Text.Trim()
    if (-not (Test-Path $efiPath)) { Show-Error "Soapivisor.efi not found. Choose the file."; return }
    $outDir = $txtOut.Text.Trim()
    if (-not (Test-Path $outDir)) { New-Item -Path $outDir -ItemType Directory -Force | Out-Null }
    Log "Starting certificate generation + signing..."
    try {
        # 1. Generate self-signed cert into CurrentUser store
        $subj = "CN=Soapivisor Research Key"
        $cert = New-SelfSignedCertificate -Subject $subj -Type CodeSigningCert `
            -KeyAlgorithm RSA -KeyLength 2048 -CertStoreLocation "Cert:\CurrentUser\My" `
            -KeyExportPolicy Exportable -NotAfter ((Get-Date).AddYears(10)) -HashAlgorithm SHA256
        if (-not $cert) { throw "Failed to create certificate." }
        Log "Certificate created in CurrentUser\\My store: $($cert.Thumbprint)"
        # Ask for PFX password
        $pwd = [System.Windows.Forms.MessageBox]::Show("You will be prompted to enter a password to protect the exported PFX. Click OK to continue.","PFX Password", 'OK','Information')
        $secure = Read-Host -AsSecureString "Enter password to protect PFX file (will not be shown)"
        $pfxPath = Join-Path $outDir "sb_key.pfx"
        Export-PfxCertificate -Cert $cert -FilePath $pfxPath -Password $secure -Force
        Log "Exported PFX to $pfxPath"
        # Export DER .cer for UEFI enrollment
        $cerPath = Join-Path $outDir "sb_cert.cer"
        Export-Certificate -Cert $cert -FilePath $cerPath -Type CERT -Force | Out-Null
        Log "Exported CER to $cerPath (DER encoded)"
        # Save PEM key and cert (optional) for WSL sbsign usage
        $pemKeyPath = Join-Path $outDir "sb_key.pem"
        $pemCertPath = Join-Path $outDir "sb_cert.pem"
        try {
            # try using OpenSSL if installed in Windows path
            $tmpPfx = $pfxPath
            $plainPass = [Runtime.InteropServices.Marshal]::PtrToStringAuto([Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure))
            $openssl = (where.exe openssl.exe 2>$null) -split "`r`n" | Select-Object -First 1
            if ($openssl) {
                Log "OpenSSL found: extracting PEM key/cert for optional sbsign usage."
                & $openssl pkcs12 -in $tmpPfx -nocerts -nodes -passin pass:$plainPass -out $pemKeyPath 2>$null
                & $openssl pkcs12 -in $tmpPfx -nokeys -passin pass:$plainPass -out $pemCertPath 2>$null
                Log "Created PEM key and cert at $pemKeyPath and $pemCertPath"
            } else {
                Log "OpenSSL not found on PATH. Skipping PEM export (needed for sbsign fallback)."
            }
        } catch {
            Log "PEM export failed: $_"
        }

        # 2. Try to sign using signtool
        $signtool = Find-Signtool
        $signedEfiPath = Join-Path $outDir "Soapivisor_signed.efi"
        $plainPass = [Runtime.InteropServices.Marshal]::PtrToStringAuto([Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure))
        if ($signtool) {
            Log "Found signtool: $signtool"
            $args = "sign", "/fd", "SHA256", "/f", "`"$pfxPath`"", "/p", "$plainPass", "`"$efiPath`"", "/v"
            $proc = Start-Process -FilePath $signtool -ArgumentList $args -PassThru -Wait -NoNewWindow
            if ($proc.ExitCode -eq 0) {
                Copy-Item -Path $efiPath -Destination $signedEfiPath -Force
                Log "Signed with signtool (signature embedded). Copying signed file to $signedEfiPath (original file preserved)."
            } else {
                Log "signtool returned exit code $($proc.ExitCode)."
            }
        } else {
            Log "signtool not found on this machine."
        }

        # 3. If user enabled WSL fallback and we have PEMs, try to call sbsign in WSL
        if ($chkUseWSL.Checked) {
            $wslout = Join-Path $outDir "Soapivisor_wsl_signed.efi"
            if ((Test-Path $pemKeyPath) -and (Test-Path $pemCertPath)) {
                Log "Attempting WSL sbsign fallback (requires sbsign + openssl in WSL)."
                # convert Windows paths to WSL paths: simple attempt (C:\ -> /mnt/c/)
                $toWsl = { param($p) $p -replace '\\','/' -replace '^([A-Za-z]):','/mnt/$($matches[1].ToLower())' }
                $wslKey = & $toWsl $pemKeyPath
                $wslCert = & $toWsl $pemCertPath
                $wslEfi = & $toWsl $efiPath
                $wslOut = & $toWsl $wslout
                try {
                    $cmd = "sbsign --key `"$wslKey`" --cert `"$wslCert`" --output `"$wslOut`" `"$wslEfi`""
                    Log "Running in WSL: $cmd"
                    wsl.exe sh -lc $cmd
                    if (Test-Path $wslout) {
                        Log "WSL sbsign succeeded: $wslout"
                    } else {
                        Log "WSL sbsign did not produce an output file. Check sbsign availability in WSL."
                    }
                } catch {
                    Log "WSL sbsign attempt failed: $_"
                }
            } else {
                Log "PEM files unavailable for WSL sbsign; skipping."
            }
        }

        Show-Info "Done. Files exported to: $outDir`n\n- PFX (private key): sb_key.pfx`n- CER (DER): sb_cert.cer`n- Signed EFI (if signtool succeeded): Soapivisor_signed.efi"
        Log "Operation completed. Remember: to enroll the certificate, copy 'sb_cert.cer' to a FAT32 USB and enroll in UEFI Secure Boot 'db' via Key Management."
    } catch {
        Show-Error "Operation failed: $_"
        Log "Exception: $_"
    }
})

# Copy CER to USB
$btnCopyCer.Add_Click({
    if (-not (Ensure-Admin)) { return }
    $outDir = $txtOut.Text.Trim(); if (-not (Test-Path $outDir)) { Show-Error "Output folder not found."; return }
    $cerPath = Join-Path $outDir "sb_cert.cer"
    if (-not (Test-Path $cerPath)) { Show-Error "sb_cert.cer not found in output folder. Generate cert first."; return }
    if ($comboUsb.SelectedIndex -lt 0) { Show-Error "Select a USB target first."; return }
    # parse drive letter from combo text (very basic)
    $selected = $comboUsb.SelectedItem.ToString()
    if ($selected -match '^([A-Z]:)') { $driveLetter = $matches[1] } else {
        Show-Error "Unable to parse USB drive letter. Refresh list and try again."
        return
    }
    $targetRoot = $driveLetter + "\"
    if ($chkFormatUsb.Checked) {
        if (-not (Ensure-Admin)) { return }
        $confirm = [System.Windows.Forms.MessageBox]::Show("Formatting will erase all data on $driveLetter. Continue?","Confirm Format","YesNo","Warning")
        if ($confirm -ne 'Yes') { return }
        try {
            Format-Volume -FileSystem FAT32 -DriveLetter $driveLetter.TrimEnd(':') -Confirm:$false -Force
            Log "Formatted $driveLetter as FAT32."
        } catch {
            Show-Error "Formatting failed: $_"
            return
        }
    }
    try {
        Copy-Item -Path $cerPath -Destination (Join-Path $targetRoot (Split-Path $cerPath -Leaf)) -Force
        Show-Info "Copied $($cerPath) to $targetRoot. Now reboot to firmware and enroll the certificate into Secure Boot 'db' (Key Management)."
        Log "Copied CER to USB $targetRoot."
    } catch {
        Show-Error "Failed copying CER to USB: $_"
        Log "Copy failed: $_"
    }
})

# Init
Refresh-UsbList
Log "Soapivisor Secure Boot Helper started."

# Run form
[void] $form.ShowDialog()