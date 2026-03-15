# Soapivisor (Native UEFI Hypervisor)

Soapivisor is a high-performance, stealth-optimized UEFI hypervisor designed for hardware-level memory subversion. 

## 🛡️ Zero-Detection Stealth Architecture
Soapivisor has been re-engineered for evasion against advanced "Blue-Side" heuristics (e.g., Vanguard):
- **Event-Driven Passive Mode**: No persistent shared memory, no polling loops, and no constant CPU overhead.
- **Deep MSR & CPUID Spoofing**: Reports zero trace of VMX/Virtualization to the guest. Masked VMX instructions (`VMXON`, `VMLAUNCH`) trigger `#UD` (Invalid Opcode).
- **Global TSC Compensation**: Subtracts hypervisor micro-latency from the guest's view of time, eliminating timing-based profiling.
- **NMI Hardening**: Safely re-injects NMIs to prevent BSODs during anti-cheat thread inspection.

## 🚀 Usage & Usability

### 1. Installation (Secure Boot OK)
You do **not** need to disable Secure Boot or use a compromised shim. Soapivisor supports custom certificate enrollment:
1. Run `secure_boot_setup.bat` to generate a self-signed certificate and sign the EFI.
2. Enroll the generated `sb_cert.crt` into your BIOS `db` (Authorized Signatures) database via a USB drive.
3. Your machine will now natively trust and boot Soapivisor with full Secure Boot protection.

### 2. Usermode Communication
Communication is triggered via a "Poisoned CPUID" leaf (`0x89ABCDEF`). This method is unprivileged and invisible to standard hypercall scanners.

Include `Soapivisor/StealthCommunication.h` in your project to use the helper:

```cpp
#include "Soapivisor/StealthCommunication.h"

uint64_t gva_list[16] = { 0x140000000, 0x140001ADC, ... };
uint64_t results[16];

// Perform a stealth read via the hypervisor
StealthReadGva(target_cr3, gva_list, results);
```

## 📚 Documentation
- **[Documents/ARCHITECTURE.md](Documents/ARCHITECTURE.md)**: Comprehensive technical analysis of the UEFI hypervisor architecture, covering:
  - Secure Boot bypass techniques (CVE-2022-21894)
  - TPM attestation and PCR manipulation
  - VT-x/VMX initialization and VMCS construction
  - L0/L1 nested virtualization for VBS compatibility
  - Extended Page Tables (EPT) with MTRR synchronization
  - NMI stack walking evasion
  - RDTSC/RDTSCP timing attack mitigation
  - Zero-VM-Exit shared memory architecture

## 🛠️ Build & Project Structure
- **/Soapivisor**: Core hypervisor logic (`vmm.cpp`, `driver.cpp`).
- **/Documents**: Technical documentation and architecture analysis.
- **secure_boot_setup.bat**: Automated signing utility.
- **clean.bat**: Full project workspace cleanup.

## ⚠️ Requirements
- **Hardware**: Intel CPU with VT-x and EPT support.
- **Environment**: UEFI Shell or custom BIOS boot entry.

## License
MIT License. Created for red-team research and hardware education.
