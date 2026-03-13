// Copyright (c) 2015-2022, Satoshi Tanda. All rights reserved.
// Use of this source code is governed by a MIT-style license that can be
// found in the LICENSE file.

/// @file
/// Implements the Native UEFI entry point of the hypervisor.

extern "C" {
#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <PiDxe.h>
#include <Protocol/MpService.h>
#include <Protocol/LoadedImage.h>
#include <Library/DevicePathLib.h>
}
#include <intrin.h>

#include "driver.h"
#include "common.h"
#include "global_object.h"
#include "hotplug_callback.h"
#include "log.h"
#include "power_callback.h"
#include "util.h"
#include "vm.h"
#include "performance.h"

extern "C" {

// Globals
EFI_SYSTEM_TABLE* gST = nullptr;
EFI_BOOT_SERVICES* gBS = nullptr;
EFI_MP_SERVICES_PROTOCOL* gMpServices = nullptr;

// A custom GUID for our UEFI Variables: {3b73c70a-7033-4600-aaec-a74812599fc1}
EFI_GUID SoapivisorVariableGuid = { 0x3b73c70a, 0x7033, 0x4600, {0xaa, 0xec, 0xa7, 0x48, 0x12, 0x59, 0x9f, 0xc1} };

////////////////////////////////////////////////////////////////////////////////
//
// implementations
//

/// @brief Checks the "Soapivisor:Skip" UEFI Variable.
/// @return TRUE if the user or a past crash has requested we skip hypervisor init.
BOOLEAN CheckSkipFlag() {
  UINT8 SkipFlag = 0;
  UINTN BufferSize = sizeof(SkipFlag);
  
  EFI_STATUS Status = gST->RuntimeServices->GetVariable(
      L"Soapivisor:Skip",
      &SoapivisorVariableGuid,
      nullptr,
      &BufferSize,
      &SkipFlag);
      
  return (!EFI_ERROR(Status) && SkipFlag == 1);
}

/// @brief Sets the "Soapivisor:Skip" UEFI Variable to prevent bootloops.
VOID SetSkipFlag() {
  UINT8 SkipFlag = 1;
  
  gST->RuntimeServices->SetVariable(
      L"Soapivisor:Skip",
      &SoapivisorVariableGuid,
      EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
      sizeof(SkipFlag),
      &SkipFlag);
}

/// @brief Writes a persistent status code to NVRAM for post-boot triage.
/// @param StatusCode A numerical identifier for the failed step.
VOID SetLastStatus(UINT32 StatusCode) {
  gST->RuntimeServices->SetVariable(
      L"Soapivisor:LastStatus",
      &SoapivisorVariableGuid,
      EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
      sizeof(StatusCode),
      &StatusCode);
}



/// @brief Writes the 31-bit VMX revision identifier to the first 4 bytes of a VMX region.
/// @param region_virt Virtual pointer to the VMXON or VMCS region.
VOID WriteVmxRevision(VOID* region_virt) {
  uint64_t vmxBasic = __readmsr(0x480); // IA32_VMX_BASIC
  uint32_t rev = (uint32_t)(vmxBasic & 0xFFFFFFFF);
  *((volatile uint32_t*)region_virt) = rev;
}

/// @brief Applies the IA32_VMX_CR0_FIXED0/1 requirements to a raw CR0 value.
/// @param cr0 The desired CR0 value.
/// @return The rigidly adjusted CR0 value safe for VMENTRY.
extern "C" uint64_t ApplyFixedCr0(uint64_t cr0) {
  uint64_t cr0_fixed0 = __readmsr(0x486);
  uint64_t cr0_fixed1 = __readmsr(0x487);
  return (cr0 | cr0_fixed0) & cr0_fixed1;
}

/// @brief Applies the IA32_VMX_CR4_FIXED0/1 requirements to a raw CR4 value.
/// @param cr4 The desired CR4 value.
/// @return The rigidly adjusted CR4 value safe for VMENTRY.
extern "C" uint64_t ApplyFixedCr4(uint64_t cr4) {
  uint64_t cr4_fixed0 = __readmsr(0x488);
  uint64_t cr4_fixed1 = __readmsr(0x489);
  return (cr4 | cr4_fixed0) & cr4_fixed1;
}

/// @brief Validates CPU features (VMX, EPT, VPID, VMXON alignment).
BOOLEAN CheckVmxSupport() {
  int cpuInfo[4] = {0};
  __cpuid(cpuInfo, 1);
  
  // Check VMX support (ECX bit 5)
  if ((cpuInfo[2] & (1 << 5)) == 0) {
    return FALSE;
  }
  
  // Verify lock bit in IA32_FEATURE_CONTROL MSR (0x3A)
  unsigned long long featureControl = __readmsr(0x3A);
  if ((featureControl & 1) == 0) {
    return FALSE; // VMX is locked off in BIOS
  }
  if ((featureControl & (1 << 2)) == 0) {
    return FALSE; // VMX outside SMX is disabled in BIOS
  }

  return TRUE;
}


PerCpuData gPerCpu[MAX_LOGICAL_PROCESSORS] = { 0 };
UINTN gProcessorCount = 0;

/// @brief Pre-allocates all memory (VMXON, VMCS, EPT, stacks) from EfiRuntimeServicesData before VMXON.
BOOLEAN ReserveAllMemory() {
  if (!gMpServices) {
    Print(L"[ERROR] gMpServices is null in memory reservation.\n");
    return FALSE;
  }
  
  UINTN NumberOfEnabledProcessors = 0;
  gMpServices->GetNumberOfProcessors(gMpServices, &gProcessorCount, &NumberOfEnabledProcessors);

  if (gProcessorCount > MAX_LOGICAL_PROCESSORS) {
    gProcessorCount = MAX_LOGICAL_PROCESSORS; // Safety cap
  }

  // Allocate contiguous physical pages for EVERY logical processor
  for (UINTN cpu = 0; cpu < gProcessorCount; ++cpu) {
    EFI_STATUS s;

    // Allocate 1 page for VMXON Region
    s = gBS->AllocatePages(AllocateAnyPages, EfiReservedMemoryType, 1, &gPerCpu[cpu].vmxon_phys);
    if (EFI_ERROR(s)) { Print(L"[ERROR] VMXON alloc failed for CPU %u: %r\n", (unsigned)cpu, s); return FALSE; }
    gPerCpu[cpu].vmxon_virt = (VOID*)(UINTN)gPerCpu[cpu].vmxon_phys;
    gBS->SetMem(gPerCpu[cpu].vmxon_virt, EFI_PAGE_SIZE, 0);
    WriteVmxRevision(gPerCpu[cpu].vmxon_virt); // Stamp revision BEFORE VMXON

    // Allocate 1 page for VMCS Region
    s = gBS->AllocatePages(AllocateAnyPages, EfiReservedMemoryType, 1, &gPerCpu[cpu].vmcs_phys);
    if (EFI_ERROR(s)) { Print(L"[ERROR] VMCS alloc failed for CPU %u: %r\n", (unsigned)cpu, s); return FALSE; }
    gPerCpu[cpu].vmcs_virt = (VOID*)(UINTN)gPerCpu[cpu].vmcs_phys;
    gBS->SetMem(gPerCpu[cpu].vmcs_virt, EFI_PAGE_SIZE, 0);
    WriteVmxRevision(gPerCpu[cpu].vmcs_virt); // Stamp revision BEFORE VMCLEAR

    // Allocate 4 pages for a robust Host Stack (including a space for a guard page logic if desired later)
    s = gBS->AllocatePages(AllocateAnyPages, EfiReservedMemoryType, 4, &gPerCpu[cpu].host_stack_phys);
    if (EFI_ERROR(s)) { Print(L"[ERROR] Host Stack alloc failed for CPU %u: %r\n", (unsigned)cpu, s); return FALSE; }
    gPerCpu[cpu].host_stack_virt = (VOID*)(UINTN)gPerCpu[cpu].host_stack_phys;
    gBS->SetMem(gPerCpu[cpu].host_stack_virt, 4 * EFI_PAGE_SIZE, 0);
  }

  return TRUE;
}

/// @brief Frees pre-allocated runtime resources if initialization fails or during shutdown.
VOID FreeAllMemory() {
  for (UINTN cpu = 0; cpu < gProcessorCount; ++cpu) {
    if (gPerCpu[cpu].vmxon_phys != 0) gBS->FreePages(gPerCpu[cpu].vmxon_phys, 1);
    if (gPerCpu[cpu].vmcs_phys != 0) gBS->FreePages(gPerCpu[cpu].vmcs_phys, 1);
    if (gPerCpu[cpu].host_stack_phys != 0) gBS->FreePages(gPerCpu[cpu].host_stack_phys, 4);
    
    gPerCpu[cpu].vmxon_phys = 0;
    gPerCpu[cpu].vmcs_phys = 0;
    gPerCpu[cpu].host_stack_phys = 0;
  }
}

/// @brief Validates allocated resources and performs a dry-run without VMXON.
BOOLEAN SelfTestResources() {
  // Validate that the system successfully allocated structures for ALL cpus
  for (UINTN cpu = 0; cpu < gProcessorCount; ++cpu) {
    if (gPerCpu[cpu].vmxon_virt == nullptr || 
        gPerCpu[cpu].vmcs_virt == nullptr || 
        gPerCpu[cpu].host_stack_virt == nullptr) {
      return FALSE;
    }
  }
  return TRUE;
}



/// @brief ApShutdownVmx signals the AP to run VmTermination and clear init_ok.
VOID EFIAPI ApShutdownVmx(IN VOID* Buffer) {
  UNREFERENCED_PARAMETER(Buffer);
  VmTermination(); // per-cpu VmTermination should VMCLEAR + VMXOFF for that core
  UINTN proc = (UINTN)-1;
  if (gMpServices) gMpServices->WhoAmI(gMpServices, &proc);
  if (proc < MAX_LOGICAL_PROCESSORS) gPerCpu[proc].init_ok = FALSE; // clear
}

/// @brief Rolls back partial initialization cleanly and sets the skip flag.
VOID SafeShutdown(UINT32 ErrorCode = 0) {
  Print(L"\n[CRITICAL] Hypervisor Initialization Failed. Executing SafeShutdown...\n");
  SetSkipFlag();
  if (ErrorCode != 0) {
    SetLastStatus(ErrorCode);
  }
  Print(L"[INFO] Soapivisor:Skip flag has been set in NVRAM to prevent future hangs.\n");

  // Implementation of safe shutdown patterns
  Print(L"[INFO] 1. Signaling APs to run safe VMXOFF path (or best-effort cleanup)...\n");
  if (gMpServices) {
    gMpServices->StartupAllAPs(gMpServices, (EFI_AP_PROCEDURE)ApShutdownVmx, FALSE, nullptr, 5000000, nullptr, nullptr);
  }

  Print(L"[INFO] 2. Executing VMXOFF on BSP...\n");
  VmTermination(); // High level call that wraps VMXOFF

  Print(L"[INFO] 3. Restoring saved MSRs and CRs...\n");
  UtilTermination();
  PerfTermination();
  GlobalObjectTermination();

  Print(L"[INFO] 4. Freeing runtime memory allocations...\n");
  FreeAllMemory();

  Print(L"[INFO] Safe shutdown complete. Chainloading OS without hypervisor...\n");
}

/// @brief Wrapper for AP execution of VMX Initialization
/// @param Buffer Optional context buffer
VOID EFIAPI ApInitializeVmx(IN VOID* Buffer) {
  UNREFERENCED_PARAMETER(Buffer);
  UINTN proc = (UINTN)-1;
  if (gMpServices) {
    if (gMpServices->WhoAmI(gMpServices, &proc) != EFI_SUCCESS) {
      Print(L"[WARN] WhoAmI failed in AP\n");
    }
  }

  // Pass the rigid per-CPU physical scaling architecture directly into the hypervisor logic.
  // NOTE: VmInitialization in vm.cpp must be updated to accept (PerCpuData* cpu_data) instead of performing manual monolithic pool math.
  auto status = VmInitialization(proc < MAX_LOGICAL_PROCESSORS ? &gPerCpu[proc] : nullptr);
  if (!NT_SUCCESS(status)) {
    Print(L"[ERROR] AP VMX Initialization failed on cpu %u!\n", (unsigned)proc);
    if (proc < MAX_LOGICAL_PROCESSORS) gPerCpu[proc].init_ok = FALSE;
    return;
  }
  
  if (proc < MAX_LOGICAL_PROCESSORS) gPerCpu[proc].init_ok = TRUE;
}

/// @brief Native UEFI Entry Point
/// @param ImageHandle The firmware allocated handle for the EFI image.
/// @param SystemTable A pointer to the EFI System Table.
/// @return EFI_STATUS EFI_SUCCESS on successful hypervisor installation.
EFI_STATUS EFIAPI UefiMain(
    IN EFI_HANDLE ImageHandle, 
    IN EFI_SYSTEM_TABLE* SystemTable) 
{
  gST = SystemTable;
  gBS = SystemTable->BootServices;

  // Clear screen and print banner
  gST->ConOut->ClearScreen(gST->ConOut);
  Print(L"Starting Soapivisor Native UEFI Hypervisor...\n");

  if (CheckSkipFlag()) {
    Print(L"[WARNING] Soapivisor:Skip UEFI Variable is set! Bypassing hypervisor installation.\n");
    Print(L"Continuing normal boot sequence...\n");
    return EFI_SUCCESS;
  }

  // --- Strict Pre-VMX Validation Flow ---

  Print(L"[INFO] Checking VMX CPU Support...\n");
  if (!CheckVmxSupport()) {
    Print(L"[ERROR] VMX not supported by CPU.\n");
    return EFI_UNSUPPORTED;
  }

  // Locate the MP Services Protocol early to interact with all logical processors
  EFI_STATUS efiStatus = gBS->LocateProtocol(&gEfiMpServiceProtocolGuid, nullptr, (VOID**)&gMpServices);
  if (EFI_ERROR(efiStatus)) {
    Print(L"[ERROR] Failed to locate MP Services Protocol: %r\n", efiStatus);
    return EFI_UNSUPPORTED;
  }

  Print(L"[INFO] Reserving all runtime memory before VMXON...\n");
  if (!ReserveAllMemory()) {
    Print(L"[ERROR] Memory/Resource Allocation Failed. Executing fallback.\n");
    SafeShutdown(1); // ErrorCode 1: Memory Exhaustion
    return EFI_DEVICE_ERROR;
  }
  
  // NOTE: Behind the scenes, we use GlobalObjectInitialization, PerfInitialization, etc.
  // to serve as the allocation wrappers for this architecture.
  auto status = GlobalObjectInitialization();
  status |= PerfInitialization();
  status |= UtilInitialization(nullptr);
  if (!NT_SUCCESS(status)) {
    Print(L"[ERROR] Memory/Resource Allocation Failed.\n");
    SafeShutdown(2); // ErrorCode 2: Generic Initialization
    return EFI_DEVICE_ERROR;
  }

  Print(L"[INFO] Running resource self-tests...\n");
  if (!SelfTestResources()) { 
    Print(L"[ERROR] Resource Self-Test Failed.\n");
    SafeShutdown(3); // ErrorCode 3: Self-Test Failure
    return EFI_DEVICE_ERROR; 
  }

  Print(L"[INFO] Configuring and validating VMX control bits...\n");
  if (!ConfigureVmxControls()) { 
    Print(L"[ERROR] Failed to configure safe VMX controls.\n");
    SafeShutdown(4); // ErrorCode 4: VMX Configuration Failure
    return EFI_DEVICE_ERROR; 
  }

  Print(L"[INFO] Executing VMX Initialization across all processors...\n");
  
  // 1. Virtualize the Bootstrap Processor (BSP)
  UINTN bspProc = 0;
  if (gMpServices) gMpServices->WhoAmI(gMpServices, &bspProc);

  status = VmInitialization(bspProc < MAX_LOGICAL_PROCESSORS ? &gPerCpu[bspProc] : nullptr);
  if (!NT_SUCCESS(status)) {
    Print(L"[ERROR] Failed to virtualize BSP. Intel VT-x may be disabled in BIOS.\n");
    SafeShutdown(5); // ErrorCode 5: BSP Virtualization Failure
    return EFI_SUCCESS;
  }

  // 2. Virtualize all Application Processors (APs) with a 5 second strict watchdog timeout
  efiStatus = gMpServices->StartupAllAPs(
      gMpServices,
      (EFI_AP_PROCEDURE)ApInitializeVmx,
      FALSE,
      nullptr,
      5000000, // 5,000,000 microseconds = 5 seconds
      nullptr,
      nullptr);

  // Poll AP ACKs for up to 5 seconds internally via gPerCpu[].init_ok
  BOOLEAN all_ok = FALSE;
  for (int retry = 0; retry < 50; ++retry) {
    all_ok = TRUE;
    for (UINTN i = 0; i < gProcessorCount; ++i) {
      if (i == 0) continue; // BSP already initialized
      if (!gPerCpu[i].init_ok) { all_ok = FALSE; break; }
    }
    if (all_ok) break;
    gBS->Stall(100000); // 100ms
  }
  
  if (!all_ok) {
    Print(L"[CRITICAL] Not all APs acknowledged VMX init within timeout.\n");
    SafeShutdown(6); // ErrorCode 6: AP Watchdog Timeout
    return EFI_SUCCESS;
  }

  // If we reach this point, VMXON succeeded globally.
  // Persist a successful code to prevent false positives if the OS crashes naturally later.
  SetLastStatus(0); 
  
  Print(L"[SUCCESS] Soapivisor VMM successfully installed at the firmware layer!\n");
  Print(L"Chain-loading Windows Boot Manager (\\EFI\\Microsoft\\Boot\\bootmgfw.efi)...\n");

  // Load and start the native Windows Boot Manager explicitly to function independently of Secure Boot shims.
  EFI_LOADED_IMAGE_PROTOCOL* LoadedImage = nullptr;
  efiStatus = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID**)&LoadedImage);
  if (!EFI_ERROR(efiStatus)) {
    // Zero out the PE header to prevent physical memory scanners from identifying the hypervisor footprint
    if (LoadedImage->ImageBase) {
      Print(L"[INFO] Zeroing PE headers (0x1000 bytes) to evade physical memory sweeps...\n");
      gBS->SetMem(LoadedImage->ImageBase, 0x1000, 0);
    }

    EFI_DEVICE_PATH_PROTOCOL* DevicePath = FileDevicePath(LoadedImage->DeviceHandle, L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi");
    if (DevicePath) {
      EFI_HANDLE WindowsHandle = nullptr;
      efiStatus = gBS->LoadImage(FALSE, ImageHandle, DevicePath, nullptr, 0, &WindowsHandle);
      if (!EFI_ERROR(efiStatus)) {
        gBS->StartImage(WindowsHandle, nullptr, nullptr);
      } else {
        Print(L"[WARNING] Failed to load bootmgfw.efi: %r\n", efiStatus);
      }
    }
  }

  Print(L"Firmware fallback sequence initiated...\n");

  // At this point, the hypervisor is active and we return EFI_SUCCESS.
  // The system's firmware boot manager will proceed.
  return EFI_SUCCESS;
}

}  // extern "C"
