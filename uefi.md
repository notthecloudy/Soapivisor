
# Native UEFI Hypervisor & Secure Boot Bypass Guide

## Introduction

Soapivisor has been re-architected from a Windows Kernel Driver (`.sys`) into a pure **native UEFI Application** (`.efi`). This architectural shift grants the hypervisor earlier execution rights, allowing it to initialize at the firmware level before the Windows Boot Manager (`bootmgfw.efi`) or the Windows OS Loader (`winload.efi`) ever launch.

Because Soapivisor is a native UEFI application, it cannot use Windows Kernel APIs. All execution, memory allocation, and processor management are handled natively via UEFI Boot Services (`EFI_BOOT_SERVICES`) and MP Services Protocol.

## The Secure Boot Challenge

UEFI Secure Boot ensures that any application executed by the firmware (`.efi`) is cryptographically signed by a trusted authority (typically Microsoft).

Since Soapivisor is entirely custom and unsigned, modern firmware will refuse to execute it if Secure Boot is enabled.

## The Bypass: Vulnerable Signed Bootloaders (`shim.efi`)

To execute an unsigned UEFI hypervisor without disabling Secure Boot or disabling Windows HVCI, we exploit the chain of trust using a vulnerable, cryptographically signed bootloader. 

The most robust and common approach is the **"Kaspersky Shim" Bypass (CVE-2022-21894 / BootHole variants)**.

### How it Works

1. **The Trusted Shim**: Antivirus vendors and Linux distributions often ship an intermediate UEFI bootloader called `shim.efi`. These shims are explicitly signed by the *Microsoft Third-Party UEFI CA* and will execute natively under Secure Boot.
2. **The Vulnerability**: Certain older versions of these `shim.efi` executables (like the one originally shipped with Kaspersky Rescue Disk) suffer from design flaws. Once the firmware executes the signed shim, the shim is supposed to verify the *next* binary in the boot chain before running it. However, the vulnerable shim has a bug or explicitly allows loading of an unsigned payload (or a payload with a self-generated certificate) alongside it.
3. **Chain-Loading**: 
   - We place the vulnerable, signed `shim.efi` as the primary boot target on a FAT32 EFI partition (e.g., `\EFI\BOOT\BOOTX64.EFI`).
   - The firmware verifies `shim.efi` against the Microsoft CA. Secure Boot passes.
   - `shim.efi` executes and loads *our unsigned* `Soapivisor.efi` (often renamed to `grubx64.efi` or another expected target for that specific shim version).
   - `Soapivisor.efi` now executes natively within the Secure Boot environment.

## Execution Flow (Runtime)

Once Soapivisor achieves execution via the shim bypass:

1. **Initialization (`UefiMain`)**: The UEFI firmware hands execution to Soapivisor's primary entry point.
2. **Hypervisor Setup**: Soapivisor allocates contiguous physical memory, sets up VMCS regions, and utilizes the MP Services Protocol to initialize VMX root operations across all logical processors.
3. **OS Handoff**: Once the hypervisor is successfully installed, Soapivisor locates and loads the original Windows Boot Manager (`\EFI\Microsoft\Boot\bootmgfw.efi`), handing control back to the operating system.
4. **Seamless Boot**: Windows boots normally, completely unaware that it is running entirely inside a virtualization sandbox constructed by Soapivisor before the OS even started.

## Conclusion

This method creates an immensely powerful and stealthy native execution environment. By running entirely as a UEFI application chain-loaded through a vulnerable signed shim, Soapivisor avoids the complexity of evading Windows Kernel PatchGuard, HVCI, or winload Code Integrity checks entirely.
