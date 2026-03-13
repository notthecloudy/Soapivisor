# Soapivisor (UEFI Hypervisor)

## Introduction
Soapivisor has been radically re-architected from a traditional Windows kernel driver into a **Native UEFI Hypervisor** (`.efi`). It provides a highly stealthy, zero-VM-exit memory access interface with minimal overhead, purpose-built for real-time external tool integration (e.g., memory scanners, ESP overlays).

Unlike traditional virtualization-based research platforms, the new Soapivisor focuses entirely on **subversion and stealth**:
- Built to run prior to the Windows OS loader (`winload.efi`).
- Specifically designed to evade detection mechanisms like Timing Attacks, CPUID anomalies, and memory scanning.
- Avoids the use of `VMCALL` instructions for routine memory access to keep CPU overhead near 0%.

## Key Features

### 1. Zero Direct OS Indicators
- **CPUID Masking**: Strips hypervisor-present bits and returns genuine hardware signatures. 
- **Timing Analysis Defeat**: Mitigates `RDTSC/RDTSCP` timing anomalies by safely offsetting the Time Stamp Counter to perfectly emulate a bare-metal environment.
- **EPT Physical Memory Hiding**: Conceals the hypervisor's physical pages from the guest OS by leveraging EPT violations to transparently substitute dummy pages on malicious reads.

### 2. Realtime On-Demand Physical Memory Mapping
To avoid constant EPT page-table walking, massive VM-exit overhead, and traditional hypervisor detection mechanisms, Soapivisor uses a **Zero-VM-Exit Shared Memory Architecture**.

#### How it works:
1. Usermode application requests memory reads by populating a unified `HvRequest` data structure in shared physical memory and incrementing a `request_id`.
2. The hypervisor intercepts the request via a lightweight detection mechanism or a dedicated core polling loop.
3. The hypervisor natively translates the requested Virtual-to-Physical mapping using its EPT cache.
4. The hypervisor reads the target memory directly and populates an `HvResult` buffer, incrementing the result ID to signal completion.
5. The Usermode application reads the requested data instantly without triggering a VM-exit pipeline. 

### 3. Hyper-V and VBS Compatibility
Windows Virtualization-Based Security (VBS) and Hyper-V normally conflict with external hypervisors. Soapivisor utilizes an **L0/L1 Nested Virtualization Architecture**:
- Soapivisor runs as the Root Hypervisor (L0).
- Hyper-V believes it is the root and initializes as an L1 guest over `Shadow VMCS` structures.
- No need to disable Hyper-V or Windows Defender Credential Guard to use Soapivisor.

### 4. Zero Test-Signing Mode (Secure Boot Bypass)
By operating under the **Kaspersky Shim Bypass (CVE-2022-21894)**, Soapivisor runs fully natively on Secure Boot enabled systems without disabling signature signing checks or loading a test-signed `.sys` driver.

## Usage & Installation

Because Soapivisor is now a UEFI Payload, it is initialized via a vulnerable `shim.efi` payload rather than traditional Service Control Manager (`sc start`).

1. Place the vulnerable `shim.efi` (e.g., from Kaspersky Rescue Disk) onto your EFI System Partition (ESP) at `\EFI\BOOT\BOOTX64.EFI`.
2. Rename the compiled `Soapivisor.efi` to the expected chain-loading filename (usually `grubx64.efi`) and place it alongside the shim.
3. Configure your BIOS/UEFI to boot the `shim.efi` file first.
4. **Boot**: The system will load `shim.efi`, chainload `Soapivisor.efi`, initialize the VT-x environment, and safely hand off execution to `bootmgfw.efi` (Windows). 

## Shared Buffer API Example
```c
struct HvRequest {
    uint32_t request_id;
    uint32_t count;       // number of addresses (max 64)
    uint64_t addresses[64];
};

struct HvResult {
    uint32_t request_id;
    uint64_t values[64];
};

// 1. Usermode maps the shared physical memory region.
// 2. Writes addresses to `HvRequest::addresses`.
// 3. Increments `HvRequest::request_id`.
// 4. Waits for `HvResult::request_id` to match.
// 5. Reads output from `HvResult::values`.
```

## Compilation
Requires EDK II or VisualUefi for natively compiling `.efi` applications. 

## Supported Platforms
- Supported OS: Windows 10, Windows 11 (x64)
- CPU: Intel Processors only (requires VT-x and EPT).

## License
Released under the MIT License.
