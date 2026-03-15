# Native UEFI Hypervisor Architecture: Bypassing Secure Boot and Evading Advanced Kernel Telemetry

## Executive Summary

The paradigm of kernel-level system administration, telemetry, and security enforcement has undergone a radical transformation over the past decade. As operating system vendors and anti-cheat developers lock down the kernel space through mechanisms like Hypervisor-Protected Code Integrity (HVCI), Kernel PatchGuard, and strict driver signature enforcement, the offensive research community has shifted its focus to the lowest levels of the hardware and firmware layers. The development of native Unified Extensible Firmware Interface (UEFI) hypervisors represents the vanguard of this subversion.

By initializing prior to the operating system loader, a UEFI hypervisor establishes a root of trust below the operating system, enveloping the host in a hardware-assisted virtualization sandbox.

This comprehensive report provides an exhaustive architectural analysis of constructing a stealth-oriented UEFI hypervisor. Specifically, it addresses the methodologies required to execute the hypervisor without disabling Secure Boot, spoof Trusted Platform Module (TPM) measurements, and evade advanced kernel forensics deployed by enterprise "blue teams" and aggressive anti-cheat engines (such as Riot Vanguard and FACEIT).

---

## The Architectural Shift to Native UEFI Execution

Historically, hypervisors and rootkits were deployed as standard Windows kernel drivers (.sys), relying on exploiting vulnerable, signed drivers—a technique known as Bring Your Own Vulnerable Driver (BYOVD)—to map memory and execute arbitrary code. However, this approach leaves significant forensic footprints, including mapped driver objects, altered page tables, and vulnerable driver artifacts residing in memory. Furthermore, modern security engines actively scan for these vulnerable drivers and flag their execution.

The architectural shift to a pure native UEFI application (.efi) grants the hypervisor earlier execution rights. By initializing at the firmware level, the hypervisor executes before the Windows Boot Manager (bootmgfw.efi) or the Windows OS Loader (winload.efi) ever launch.

Because the UEFI application cannot utilize Windows Kernel APIs, all execution, physical memory allocation, and processor management must be handled natively via:
- **UEFI Boot Services** (EFI_BOOT_SERVICES)
- **MP Services Protocol** (EFI_MP_SERVICES_PROTOCOL)

This absolute control over the physical memory map prior to the operating system's awareness allows the hypervisor to:
- Allocate contiguous physical memory
- Construct Virtual Machine Control Structures (VMCS)
- Initialize VMX root operations across all logical processors without interference

The second-order implication of this architecture is the **complete invalidation of OS-level memory integrity checks**. Because the hypervisor reserves its memory pages via UEFI allocation, the Windows memory manager never assumes control over these regions. Consequently, subsequent memory sweeps conducted by the operating system or third-party telemetry agents are blind to the hypervisor's footprint.

To further reduce forensic artifacts, the hypervisor purposefully zeroes out its Portable Executable (PE) headers (typically a 0x1000 byte region) immediately after initialization, eliminating the MZ and PE magic bytes that physical memory scanners aggressively hunt for.

---

## Subverting the Chain of Trust: Secure Boot and TPM Attestation

### The Kaspersky Shim Bypass (CVE-2022-21894)

A fundamental challenge in deploying a custom UEFI hypervisor is bypassing the UEFI Secure Boot mechanism. Secure Boot ensures that any application executed by the firmware is cryptographically signed by a trusted authority. Modern anti-cheat systems, such as Riot Vanguard and FACEIT, strictly enforce the presence of Secure Boot and TPM 2.0 to ensure a trusted execution environment. Disabling Secure Boot is no longer a viable option for stealth operations, as these platforms query the Secure Boot state via environment variables and will deny access if the system is not compliant.

The objective is to execute an unsigned hypervisor payload while the motherboard firmware technically reports that Secure Boot is fully enabled.

To operate within these constraints, modern UEFI hypervisors leverage the **Kaspersky Shim Bypass**, documented under **CVE-2022-21894** (also known as "Baton Drop"), and related BootHole variants. This vulnerability allows an unsigned .efi payload to execute natively on a system while Secure Boot remains fully enabled in the firmware settings, satisfying external security checks.

The bypass exploits the chain of trust by utilizing a vulnerable, officially signed intermediate bootloader (shim.efi). These shims, historically shipped by antivirus vendors or Linux distributions, are signed by the Microsoft Third-Party UEFI Certificate Authority. While the firmware correctly verifies the shim's signature, the vulnerable shim fails to properly validate the subsequent payload in the boot chain.

**Implementation Sequence:**
1. Place the signed shim.efi on the EFI System Partition (ESP) as the primary boot target (e.g., \EFI\BOOT\BOOTX64.EFI)
2. Place the custom hypervisor payload alongside it and rename to match the hardcoded target the shim expects to load (frequently grubx64.efi)

Upon boot, the UEFI firmware successfully cryptographically verifies shim.efi. The shim executes, but due to its flawed internal validation logic, it blindly loads and executes the unsigned hypervisor payload.

Furthermore, CVE-2022-21894 exposes a critical flaw in how the Windows Boot Configuration Data (BCD) handles physical memory boundaries. The vulnerability allows an attacker to inject the `truncatememory` BCD element, which explicitly removes all memory above a specified physical address from the memory map during the initial application loading phase. Because the serialized Secure Boot policy is read from memory during this phase, truncatememory can be strategically weaponized to carve the Secure Boot policy entirely out of the accessible memory map.

### Manipulating TPM Platform Configuration Registers (PCR)

In conjunction with Secure Boot, security telemetry heavily relies on the Trusted Platform Module (TPM) for measured boot attestation. The TPM records the cryptographic hashes of all boot components into Platform Configuration Registers (PCRs). If a hypervisor intercepts the boot process, the TPM logs will typically reflect the anomaly.

Anti-cheat engines and enterprise blue teams read these PCR banks to detect virtualization and boot-level tampering.

| PCR Index | Description of Measured Component | Telemetry Relevance |
|-----------|-----------------------------------|---------------------|
| PCR 0-3 | Core System Firmware executable code and data | Detects BIOS/UEFI firmware modifications |
| PCR 4 | Boot Manager Code | Detects chain-loading of unauthorized boot managers |
| PCR 7 | Secure Boot State | Validates that Secure Boot is enforced and active |
| PCR 11 | BitLocker Access | Monitors changes to the encrypted drive access policies |
| PCR 14 | CustomKernelSigners / Boot Authorities | Validates public keys of authorities that signed kernel-level drivers |

To spoof the TPM attestation, a traditional hypervisor must intercept and manipulate the EV_EFI_BOOT_SERVICES_APPLICATION events before they are finalized. This involves hooking the UEFI functions responsible for extending the PCR hashes—specifically the EFI_TCG2_PROTOCOL functions—to feed the TPM the expected, pristine hashes of the standard Windows bootloader (bootmgfw.efi).

However, when utilizing the Baton Drop vulnerability (CVE-2022-21894), complex TPM hooking becomes largely unnecessary. Because the Secure Boot policy is truncated from memory, the hypervisor can rely on the fact that the serialized policy reallocation triggers under specific conditions.

---

## Virtual Machine Extensions (VT-x) and VMCS Construction

Once the hypervisor achieves execution via the shim bypass, it must transition the physical processor from standard operation into a hardware-assisted virtualization state. This requires rigorous configuration of the Intel Virtual Machine Extensions (VT-x).

### Multiprocessor Initialization

The hypervisor achieves multiprocessor initialization by leveraging the **EFI_MP_SERVICES_PROTOCOL**:

1. Query `gMpServices->GetNumberOfProcessors` to map the exact CPU topology
2. Invoke `gMpServices->StartupAllAPs` to awaken all Application Processors (APs)

Each logical core independently executes a setup routine that:
- Allocates a 4-kilobyte page for its VMXON region
- Allocates a secondary 4-kilobyte page for its VMCS region

These pages are explicitly requested as `EfiReservedMemoryType` to ensure they are persistently excluded from the memory map subsequently passed to the Windows kernel.

### Control Register Requirements

Before executing the VMXON instruction, the hypervisor must strictly adhere to the control register requirements dictated by the Intel architecture:

- The `ApplyFixedCr4` function reads the Model-Specific Registers (MSRs):
  - 0x488 (IA32_VMX_CR4_FIXED0)
  - 0x489 (IA32_VMX_CR4_FIXED1)
- Applies a bitwise mask ensuring mandatory bits in the CR4 register (such as CR4.VMXE) are set to 1, while prohibited bits are cleared

### VMCS Configuration

Following VMXON, the hypervisor populates the VMCS. The VMCS is partitioned into:

- **Host-State Area**: Dictates the execution environment the processor assumes when a VM-exit occurs
- **Guest-State Area**: Defines the state the processor assumes when executing VMLAUNCH or VMRESUME

To maintain absolute stealth, the hypervisor configures the VMCS Execution Controls to intercept specific sensitive operations via **MSR Bitmaps**. The hypervisor allocates memory for the MSR bitmap, setting specific bits to trap read or write access to critical MSRs such as:
- IA32_EFER (Extended Feature Enable Register)
- IA32_LSTAR (System Call Target Address)

---

## L0/L1 Nested Virtualization Architecture

Modern Windows operating systems aggressively utilize virtualization features. Virtualization-Based Security (VBS), Hypervisor-Protected Code Integrity (HVCI), and Windows Sandbox rely entirely on Microsoft's native Hyper-V architecture. When VBS is enabled, Hyper-V takes exclusive control over the physical VT-x extensions, operating at the highest hardware privilege level.

To survive in this environment without triggering alarms by disabling VBS, an advanced UEFI hypervisor must implement an **L0/L1 nested virtualization architecture**.

In this paradigm:
- The custom UEFI hypervisor establishes itself as the absolute **Root Hypervisor (L0)** during the UEFI boot phase
- When the Windows bootloader attempts to initialize Hyper-V (which becomes the L1 hypervisor), the L0 hypervisor transparently facilitates this initialization without relinquishing its hardware dominance

To achieve this, the L0 hypervisor configures the physical VMCS to trap all virtualization instructions executed by the guest OS:
- VMXON
- VMCLEAR
- VMPTRLD
- VMREAD
- VMWRITE
- VMLAUNCH
- VMRESUME

When Hyper-V attempts to execute VMXON, the L0 hypervisor intercepts the instruction, allocates a virtualized VMXON region in the guest's physical memory, and returns success, tricking Hyper-V into believing it has entered VMX root operation.

The core of this deception relies on maintaining a **"Shadow VMCS"**. When Hyper-V executes VMPTRLD to load its VMCS, the L0 hypervisor traps the call and maps a shadow structure. Every subsequent VMREAD and VMWRITE executed by Hyper-V is trapped and redirected to read from or write to the Shadow VMCS.

| Execution Level | Component | Virtualization Authority | Hardware Access |
|-----------------|-----------|-------------------------|-----------------|
| L0 | Custom UEFI Hypervisor (Soapivisor) | Absolute Root | Direct access to physical VMCS and EPT |
| L1 | Microsoft Hyper-V | Perceived Root | Accesses Shadow VMCS; memory access subject to L0's EPT |
| L2 | Windows OS / VBS / Guest VMs | Standard Guest | Subject to hypervisor memory isolation and VM-exits managed by L1 (via L0 emulation) |

---

## Extended Page Tables (EPT) and Cache-Timing Synchronization

While managing the execution state of the processor is critical, memory isolation is the paramount objective of a stealth hypervisor. To prevent advanced kernel heuristics and memory sweeps from locating the hypervisor's physical footprint, the hypervisor relies heavily on the **Extended Page Tables (EPT)**.

### EPT Address Translation

EPT introduces a secondary layer of paging that translates Guest Physical Addresses (GPA) into Host Physical Addresses (HPA). The translation mechanism utilizes a 4-level hierarchical structure:

| Level | Selector | Shift | Mask |
|-------|----------|-------|------|
| PML4 | Page Map Level 4 | 39 bits (kEptpPxiShift) | 0x1FF (kEptpPtxMask) |
| PDPT | Page Directory Pointer | 30 bits (kEptpPpiShift) | 0x1FF |
| PD | Page Directory | 21 bits (kEptpPdiShift) | 0x1FF |
| PT | Page Table | 12 bits (kEptpPtiShift) | 0x1FF |

### Concealing Hypervisor Memory

To conceal its own memory, the hypervisor:
1. Identifies the Host Physical Addresses (HPA) containing its code, data, and VMCS regions
2. Traverses the EPT hierarchy and modifies the terminal Page Table Entries (PTE) for those addresses, clearing the read, write, and execute bits

If a kernel-level anti-cheat attempts to scan that specific physical address, the processor hardware immediately halts the access and triggers an **EPT Violation**, forcing a VM-exit.

The hypervisor's `VmmpHandleEptViolation` routine intercepts this access. Instead of outright denying the read (which would return an error and cryptographically confirm the presence of hidden memory), the hypervisor dynamically hot-swaps the HPA in the EPT entry to point to a **"dummy page"**. This dummy page is previously filled with zeroes or benign operating system data. The hypervisor resumes the guest, allowing the read to complete successfully on the falsified data.

### Memory Type Range Registers (MTRR) Consistency

A critical vulnerability in early EPT-hiding implementations was the failure to respect Memory Type Range Registers (MTRRs). Processors cache physical memory differently based on MTRRs, applying types such as:
- Write-Back (WB)
- Uncacheable (UC)
- Write-Combining (WC)

If a hypervisor redirects an EPT entry to a dummy page, but the memory type of the dummy page mismatches the OS's expectation for that address range, a simple timing loop on memory access will reveal massive cache-timing anomalies.

Modern implementations painstakingly synchronize EPT memory types with system MTRRs. During initialization, the hypervisor's `EptInitializeMtrrEntries` routine:

1. Reads the default memory type from `Msr::kIa32MtrrDefType`
2. Iterates through all fixed MTRR ranges:
   - **64k Range**: 8 ranges starting at 0x0 with size 0x10000 each
   - **16k Range**: Ranges starting at 0x80000 and 0xA0000 with size 0x4000 each
   - **4k Range**: Ranges starting at 0xC0000 with size 0x1000 each
3. Queries variable MTRRs to map caching behavior of the entire remaining physical address space (up to 255 variable range MTRRs)

These values are cached in a global array (`g_eptp_mtrr_entries`). When an EPT Violation occurs and the hypervisor swaps in a dummy page, it queries this array to apply the exact matching memory type to the EPT entry.

Furthermore, by pre-allocating EPT entries (capped at `kEptpNumberOfPreallocatedEntries = 50`), the hypervisor eliminates the need for dynamic kernel allocations during an EPT violation.

---

## Evading Advanced Kernel Forensics and Heuristics

### Evading Non-Maskable Interrupt (NMI) Stack Walking

A premier detection vector employed by advanced anti-cheat engines is the use of **Non-Maskable Interrupts (NMIs)**. Unlike standard hardware interrupts, NMIs cannot be ignored or masked by the `cli` (Clear Interrupt Flag) instruction in the guest operating system.

Security drivers invoke the `HalSendNmi` API to broadcast a hardware NMI to all processor cores simultaneously. When the NMI fires, the operating system is violently forced to execute registered NMI callbacks. The anti-cheat's callback immediately halts the processor and captures the current Instruction Pointer (RIP) and Stack Pointer (RSP) directly from the KTRAP_FRAME.

**Countermeasures:**

1. **KTHREAD DKOM (ApcQueueable)**: Attackers may perform Direct Kernel Object Modification on the undocumented KTHREAD structure, flipping the ApcQueueable bit to 0

2. **Stack Spoofing**: Prior to performing malicious actions, the thread replaces its stack frames with fabricated return addresses pointing to legitimate modules

3. **Hypervisor NMI Interception** (Most Absolute Defense): By configuring the VMCS to exit on NMIs (via the Pin-Based VM-Execution Controls), the hypervisor traps the interrupt before the guest OS is even aware of it

When the hardware traps the NMI, the hypervisor inspects the guest's current RIP and RSP. If the guest was executing malicious code or spinning in unbacked memory, the hypervisor dynamically manipulates the guest's registers to point to a safe, backing module, pushes a perfectly fabricated trap frame onto the guest's stack, and then manually injects the NMI into the guest via the VMCS Event Injection fields.

### Mitigating Timing Attacks (RDTSC/RDTSCP Spoofing)

The most robust mechanism for detecting hardware-assisted virtualization relies on timing analysis. When a guest executes a command that requires hypervisor intervention (a VM-exit), the CPU must:
1. Transition from non-root mode to root mode
2. Save the guest state
3. Process the exit
4. Restore the guest state
5. Execute VMRESUME

This context switch induces a massive latency penalty—often exceeding 1,000 CPU cycles—compared to bare-metal execution.

To defeat timing analysis, the hypervisor configures the VMCS Primary Processor-Based Execution Controls to force a VM-exit on all RDTSC and RDTSCP instructions.

| VMX Exit Reason | Instruction | Interception Strategy |
|-----------------|-------------|----------------------|
| kRdtsc | RDTSC | VM-Exit traps instruction. Hypervisor calculates cycle cost and deducts it from returned value |
| kRdtscp | RDTSCP | Same as RDTSC, but also returns TSC_AUX MSR value in ECX register |
| kMonitorTrapFlag | MTF Injection | Used for single-stepping guest instructions to profile timing deltas |

In the main VM-exit handler, the hypervisor:
1. Immediately captures the current host TSC upon entry using `__rdtsc()`
2. Calculates the exact cycle cost of the VM-exit transition and internal processing
3. Subtracts this latency penalty from the actual hardware TSC before writing the spoofed value into the guest's EAX and EDX registers

Intel VT-x provides the **TSC_OFFSET** and **TSC_MULTIPLIER** fields within the VMCS. By dynamically adjusting these fields upon every exit, the hypervisor can perfectly emulate a bare-metal environment.

### CPUID Masking and the Zero-VM-Exit Shared Memory Architecture

The CPUID instruction is unprivileged and heavily used by the OS and applications to query processor features. By default, standard hypervisors expose their presence by setting the CPUID.1:ECX.HypervisorPresent bit (bit 31) to 1.

A stealth hypervisor must intercept all CPUID instructions. When the dispatcher routes the exit to `VmmpHandleCpuid`, the hypervisor:
1. Queries genuine hardware CPUID values
2. Applies a bitwise mask to strip the HypervisorPresent bit
3. Zeroes out the hypervisor vendor string at 0x40000000

#### Poisoned CPUID Communication Channel

Traditionally, rootkits utilized a "Poisoned CPUID" implementation for communication. Because CPUID always exits to the hypervisor, it can be abused as an invisible communication channel. A user-mode attacker application executes CPUID with highly specific, non-standard register values (e.g., EAX=0xDEADBEEF, ECX=0x1337). The hypervisor recognizes this magic sequence and processes the request.

However, modern anti-cheats actively instrument and monitor both anomalous CPUID execution and VMCALL instructions.

#### Zero-VM-Exit Shared Memory Architecture

State-of-the-art implementations have abandoned VMCALL entirely in favor of a **Zero-VM-Exit Shared Memory Architecture**. This design eliminates the VM-exit pipeline for routine memory access, bringing the hypervisor's operational CPU overhead down to near 0%.

**Mechanism:**

1. **Request Phase**: A user-mode application maps shared physical memory into its virtual address space, populates a unified HvRequest data structure, and increments a request_id

2. **Interception Phase**: Rather than trapping a CPU instruction, the hypervisor utilizes EPT to mark the specific shared page as non-readable/non-writable for the guest. When the user-mode application writes the request_id, an EPT violation occurs

3. **Execution Phase**: The hypervisor translates the requested Virtual-to-Physical mapping using its internal EPT cache, performs the requested memory operation directly in host mode

4. **Completion Phase**: The hypervisor increments a result ID. The user-mode application, spinning on this ID, reads the data instantly

This architectural paradigm shift ensures that communication between ring-3 malicious applications and the ring -1 hypervisor occurs entirely out-of-band relative to the CPU's instruction pipeline.

---

## Conclusion

The evolution of offensive hypervisor development from ring-0 .sys drivers to native .efi firmware applications marks a definitive escalation in the capabilities available to rootkit developers and security researchers. By anchoring execution in the UEFI boot phase, exploiting signed shim vulnerabilities to bypass Secure Boot enforcement, and sidestepping rigorous TPM attestation, these architectures effortlessly subvert the operating system's foundational trust metrics.

Furthermore, the implementation of:
- Zero-VM-exit shared memory communication
- L0/L1 nested virtualization for VBS compatibility
- Dynamic RDTSC offsetting
- EPT-backed physical memory spoofing

demonstrates a profound mastery over hardware-assisted virtualization. As long as the operating system and its advanced security modules operate merely as guests within a virtualized envelope, their telemetry remains subject to absolute manipulation by the underlying L0 hypervisor.

The reliance on hardware-based telemetry like Non-Maskable Interrupts and cache-timing analysis has proven insufficient, as the hypervisor maintains the ultimate authority to redefine the hardware reality perceived by the guest.

---

## References

- Intel 64 and IA-32 Architectures Software Developer Manuals
- UEFI Specification
- CVE-2022-21894 (Baton Drop / Kaspersky Shim Bypass)
- TPM 2.0 Specification
- Microsoft Hyper-V Architecture Documentation
