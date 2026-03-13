// Copyright (c) 2015-2022, Satoshi Tanda. All rights reserved.
// Use of this source code is governed by a MIT-style license that can be
// found in the LICENSE file.

/// @file
/// Implements primitive utility functions.
#include "common.h"
#include <ntimage.h>
#include "util.h"
#include <intrin.h>
#include "asm.h"
#include "log.h"

extern "C" {
extern EFI_BOOT_SERVICES* gBS;
extern EFI_MP_SERVICES_PROTOCOL* gMpServices;
}

extern "C" {
////////////////////////////////////////////////////////////////////////////////
//
// macro utilities
//

////////////////////////////////////////////////////////////////////////////////
//
// constants and macros
//

// Use RtlPcToFileHeader if available. Using the API causes a broken font bug
// on the 64 bit Windows 10 and should be avoided. This flag exist for only
// further investigation.
static const auto kUtilpUseRtlPcToFileHeader = false;

////////////////////////////////////////////////////////////////////////////////
//
// types
//

NTKERNELAPI PVOID NTAPI RtlPcToFileHeader(_In_ PVOID PcValue,
                                          _Out_ PVOID *BaseOfImage);

using RtlPcToFileHeaderType = decltype(RtlPcToFileHeader);

_Must_inspect_result_ _IRQL_requires_max_(DISPATCH_LEVEL) NTKERNELAPI
    _When_(return != NULL, _Post_writable_byte_size_(NumberOfBytes)) PVOID
    MmAllocateContiguousNodeMemory(
        _In_ SIZE_T NumberOfBytes,
        _In_ PHYSICAL_ADDRESS LowestAcceptableAddress,
        _In_ PHYSICAL_ADDRESS HighestAcceptableAddress,
        _In_opt_ PHYSICAL_ADDRESS BoundaryAddressMultiple, _In_ ULONG Protect,
        _In_ NODE_REQUIREMENT PreferredNode);

using MmAllocateContiguousNodeMemoryType =
    decltype(MmAllocateContiguousNodeMemory);

//
// DRIVER_OBJECT.DriverSection type
// see Reverse Engineering site:
// https://revers.engineering/author/daax/
//
struct KLdrDataTableEntry {
  LIST_ENTRY in_load_order_links;
  PVOID exception_table;
  UINT32 exception_table_size;
  // ULONG padding on IA64
  PVOID gp_value;
  PNON_PAGED_DEBUG_INFO non_paged_debug_info;
  PVOID dll_base;
  PVOID entry_point;
  UINT32 size_of_image;
  UNICODE_STRING full_dll_name;
  UNICODE_STRING base_dll_name;
  UINT32 flags;
  UINT16 load_count;

  union {
    UINT16 signature_level : 4;
    UINT16 signature_type : 3;
    UINT16 unused : 9;
    UINT16 entire_field;
  } u;

  PVOID section_pointer;
  UINT32 checksum;
  UINT32 coverage_section_size;
  PVOID coverage_section;
  PVOID loaded_imports;
  PVOID spare;
  UINT32 size_of_image_not_rouned;
  UINT32 time_date_stamp;
};

////////////////////////////////////////////////////////////////////////////////
//
// prototypes
//

_IRQL_requires_max_(PASSIVE_LEVEL) static NTSTATUS
    UtilpInitializePageTableVariables();

_IRQL_requires_max_(PASSIVE_LEVEL) static NTSTATUS
    UtilpInitializeRtlPcToFileHeader(_In_ PDRIVER_OBJECT driver_object);

_Success_(return != nullptr) static PVOID NTAPI
    UtilpUnsafePcToFileHeader(_In_ PVOID pc_value, _Out_ PVOID *base_of_image);

_IRQL_requires_max_(PASSIVE_LEVEL) static NTSTATUS
    UtilpInitializePhysicalMemoryRanges();

_IRQL_requires_max_(PASSIVE_LEVEL) static PhysicalMemoryDescriptor
    *UtilpBuildPhysicalMemoryRanges();

static bool UtilpIsCanonicalFormAddress(_In_ void *address);

static HardwarePte *UtilpAddressToPxe(_In_ const void *address);

static HardwarePte *UtilpAddressToPpe(_In_ const void *address);

static HardwarePte *UtilpAddressToPde(_In_ const void *address);

static HardwarePte *UtilpAddressToPte(_In_ const void *address);

#if defined(ALLOC_PRAGMA)
#pragma alloc_text(INIT, UtilInitialization)
#pragma alloc_text(PAGE, UtilTermination)
#pragma alloc_text(INIT, UtilpInitializePageTableVariables)
#pragma alloc_text(INIT, UtilpInitializeRtlPcToFileHeader)
#pragma alloc_text(INIT, UtilpInitializePhysicalMemoryRanges)
#pragma alloc_text(INIT, UtilpBuildPhysicalMemoryRanges)
#pragma alloc_text(PAGE, UtilForEachProcessor)
#pragma alloc_text(PAGE, UtilSleep)
#pragma alloc_text(PAGE, UtilGetSystemProcAddress)
#endif

////////////////////////////////////////////////////////////////////////////////
//
// variables
//

// Memory allocation implementation for UEFI
extern "C" void* ExAllocatePoolZero(POOL_TYPE PoolType, SIZE_T NumberOfBytes, ULONG Tag) {
  UNREFERENCED_PARAMETER(PoolType);
  UNREFERENCED_PARAMETER(Tag);
  void* p = nullptr;
  EFI_STATUS status = gBS->AllocatePool(EfiRuntimeServicesData, NumberOfBytes, &p);
  if (EFI_ERROR(status)) return nullptr;
  memset(p, 0, NumberOfBytes);
  return p;
}

extern "C" void ExFreePoolWithTag(void* P, ULONG Tag) {
  UNREFERENCED_PARAMETER(Tag);
  if (P) gBS->FreePool(P);
}

extern "C" PPHYSICAL_MEMORY_RANGE MmGetPhysicalMemoryRanges() {
  UINTN MemoryMapSize = 0;
  EFI_MEMORY_DESCRIPTOR* MemoryMap = nullptr;
  UINTN MapKey = 0;
  UINTN DescriptorSize = 0;
  UINT32 DescriptorVersion = 0;

  // Get map size
  gBS->GetMemoryMap(&MemoryMapSize, nullptr, &MapKey, &DescriptorSize, &DescriptorVersion);
  MemoryMapSize += 2 * DescriptorSize; // Buffer for growth
  MemoryMap = (EFI_MEMORY_DESCRIPTOR*)ExAllocatePoolZero(NonPagedPool, MemoryMapSize, 0);
  if (!MemoryMap) return nullptr;

  EFI_STATUS status = gBS->GetMemoryMap(&MemoryMapSize, MemoryMap, &MapKey, &DescriptorSize, &DescriptorVersion);
  if (EFI_ERROR(status)) {
    ExFreePool(MemoryMap);
    return nullptr;
  }

  UINTN DescriptorCount = MemoryMapSize / DescriptorSize;
  UINTN RangeCount = 0;
  for (UINTN i = 0; i < DescriptorCount; i++) {
    EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)MemoryMap + i * DescriptorSize);
    if (desc->Attribute & EFI_MEMORY_RUNTIME || desc->Type == EfiConventionalMemory || 
        desc->Type == EfiLoaderCode || desc->Type == EfiLoaderData ||
        desc->Type == EfiBootServicesCode || desc->Type == EfiBootServicesData) {
      RangeCount++;
    }
  }

  PPHYSICAL_MEMORY_RANGE Ranges = (PPHYSICAL_MEMORY_RANGE)ExAllocatePoolZero(NonPagedPool, sizeof(PHYSICAL_MEMORY_RANGE) * (RangeCount + 1), 0);
  if (!Ranges) {
    ExFreePool(MemoryMap);
    return nullptr;
  }

  UINTN CurrentRange = 0;
  for (UINTN i = 0; i < DescriptorCount; i++) {
    EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)MemoryMap + i * DescriptorSize);
     if (desc->Attribute & EFI_MEMORY_RUNTIME || desc->Type == EfiConventionalMemory || 
        desc->Type == EfiLoaderCode || desc->Type == EfiLoaderData ||
        desc->Type == EfiBootServicesCode || desc->Type == EfiBootServicesData) {
      Ranges[CurrentRange].BaseAddress.QuadPart = desc->PhysicalStart;
      Ranges[CurrentRange].NumberOfBytes.QuadPart = desc->NumberOfPages * PAGE_SIZE;
      CurrentRange++;
    }
  }
  // Terminator is zeroed by ExAllocatePoolZero
  ExFreePool(MemoryMap);
  return Ranges;
}

// Global page table state (stubs for UEFI)
static ULONG_PTR g_utilp_pxe_base = 0;
static ULONG_PTR g_utilp_ppe_base = 0;
static ULONG_PTR g_utilp_pde_base = 0;
static ULONG_PTR g_utilp_pte_base = 0;

static ULONG_PTR g_utilp_pxi_shift = 39;
static ULONG_PTR g_utilp_ppi_shift = 30;
static ULONG_PTR g_utilp_pdi_shift = 21;
static ULONG_PTR g_utilp_pti_shift = 12;

static ULONG_PTR g_utilp_pxi_mask = 0x1ff;
static ULONG_PTR g_utilp_ppi_mask = 0x1ff;
static ULONG_PTR g_utilp_pdi_mask = 0x1ff;
static ULONG_PTR g_utilp_pti_mask = 0x1ff;

////////////////////////////////////////////////////////////////////////////////
//
// implementations
//

// Initializes utility functions
_Use_decl_annotations_ NTSTATUS
UtilInitialization(PDRIVER_OBJECT driver_object) {
  UNREFERENCED_PARAMETER(driver_object);
  
  // In UEFI, we don't need to find Windows page table bases yet.
  // We just initialize physical memory ranges.
  auto status = UtilpInitializePhysicalMemoryRanges();
  return status;
}

static PhysicalMemoryDescriptor *g_utilp_physical_memory_ranges;

// Initializes g_utilp_p*e_base, g_utilp_p*i_shift and g_utilp_p*i_mask.
_Use_decl_annotations_ static NTSTATUS UtilpInitializePageTableVariables() {
  return STATUS_SUCCESS;
}

// Terminates utility functions
_Use_decl_annotations_ void UtilTermination() {
  if (g_utilp_physical_memory_ranges) {
    ExFreePoolWithTag(g_utilp_physical_memory_ranges,
                      kSoapivisorCommonPoolTag);
  }
}



// Initializes the physical memory ranges
_Use_decl_annotations_ static NTSTATUS UtilpInitializePhysicalMemoryRanges() {
  PAGED_CODE()

  const auto ranges = UtilpBuildPhysicalMemoryRanges();
  if (!ranges) {
    return STATUS_UNSUCCESSFUL;
  }

  g_utilp_physical_memory_ranges = ranges;

  for (auto i = 0ul; i < ranges->number_of_runs; ++i) {
    const auto base_addr =
        static_cast<ULONG64>(ranges->run[i].base_page) * PAGE_SIZE;
    Soapivisor_LOG_DEBUG("Physical Memory Range: %016llx - %016llx",
                            base_addr,
                            base_addr + ranges->run[i].page_count * PAGE_SIZE);
  }

  const auto pm_size =
      static_cast<ULONG64>(ranges->number_of_pages) * PAGE_SIZE;
  Soapivisor_LOG_DEBUG("Physical Memory Total: %llu KB", pm_size / 1024);

  return STATUS_SUCCESS;
}

// Builds the physical memory ranges
_Use_decl_annotations_ static PhysicalMemoryDescriptor *
UtilpBuildPhysicalMemoryRanges() {
  PAGED_CODE()

  const auto pm_ranges = MmGetPhysicalMemoryRanges();
  if (!pm_ranges) {
    return nullptr;
  }

  PFN_COUNT number_of_runs = 0;
  PFN_NUMBER number_of_pages = 0;
  for (/**/; /**/; ++number_of_runs) {
    const auto range = &pm_ranges[number_of_runs];
    if (!range->BaseAddress.QuadPart && !range->NumberOfBytes.QuadPart) {
      break;
    }
    number_of_pages +=
        static_cast<PFN_NUMBER>(BYTES_TO_PAGES(range->NumberOfBytes.QuadPart));
  }
  if (number_of_runs == 0) {
    ExFreePoolWithTag(pm_ranges, 'hPmM');
    return nullptr;
  }

  const auto memory_block_size =
      sizeof(PhysicalMemoryDescriptor) +
      sizeof(PhysicalMemoryRun) * (number_of_runs - 1);
  const auto pm_block =
      static_cast<PhysicalMemoryDescriptor *>(ExAllocatePoolZero(
          NonPagedPool, memory_block_size, kSoapivisorCommonPoolTag));
  if (!pm_block) {
    ExFreePoolWithTag(pm_ranges, 'hPmM');
    return nullptr;
  }
  RtlZeroMemory(pm_block, memory_block_size);

  pm_block->number_of_runs = number_of_runs;
  pm_block->number_of_pages = number_of_pages;

  for (auto run_index = 0ul; run_index < number_of_runs; run_index++) {
    auto current_run = &pm_block->run[run_index];
    auto current_block = &pm_ranges[run_index];
    current_run->base_page = static_cast<ULONG_PTR>(
        UtilPfnFromPa(current_block->BaseAddress.QuadPart));
    current_run->page_count = static_cast<ULONG_PTR>(
        BYTES_TO_PAGES(current_block->NumberOfBytes.QuadPart));
  }

  ExFreePoolWithTag(pm_ranges, 'hPmM');
  return pm_block;
}

// Returns the physical memory ranges
/*_Use_decl_annotations_*/ const PhysicalMemoryDescriptor *
UtilGetPhysicalMemoryRanges() {
  return g_utilp_physical_memory_ranges;
}

// Execute a given callback routine on all processors.
_Use_decl_annotations_ NTSTATUS
UtilForEachProcessor(NTSTATUS (*callback_routine)(void *), void *context) {
  if (!gMpServices) return STATUS_UNSUCCESSFUL;

  UINTN NumberOfProcessors = 0;
  UINTN NumberOfEnabledProcessors = 0;
  gMpServices->GetNumberOfProcessors(gMpServices, &NumberOfProcessors, &NumberOfEnabledProcessors);

  for (UINTN i = 0; i < NumberOfProcessors; i++) {
    // Note: In UEFI, StartupThisAP can be used, but for initialization we often 
    // just want to run on each one sequentially if we are already in a context 
    // that allows it, or use the Startup protocol.
    // Simplifying: we expect the caller to handle AP startup if needed, 
    // or we use the protocol here.
    if (i == 0) {
      callback_routine(context);
    } else {
      gMpServices->StartupThisAP(gMpServices, (EFI_AP_PROCEDURE)callback_routine, i, nullptr, 0, context, nullptr);
    }
  }
  return STATUS_SUCCESS;
}

// Queues a given DPC routine on all processors. Returns STATUS_SUCCESS when DPC
// is queued for all processors.
_Use_decl_annotations_ NTSTATUS
UtilForEachProcessorDpc(PKDEFERRED_ROUTINE deferred_routine, void *context) {
  UNREFERENCED_PARAMETER(deferred_routine);
  UNREFERENCED_PARAMETER(context);
  return STATUS_NOT_IMPLEMENTED;
}

// Sleep the current thread's execution for Millisecond milliseconds.
_Use_decl_annotations_ NTSTATUS UtilSleep(LONG Millisecond) {
  gBS->Stall(Millisecond * 1000); // Stall takes microseconds
  return STATUS_SUCCESS;
}

// memmem().
_Use_decl_annotations_ void *UtilMemMem(const void *search_base,
                                        SIZE_T search_size, const void *pattern,
                                        SIZE_T pattern_size) {
  if (pattern_size > search_size) {
    return nullptr;
  }
  auto base = static_cast<const char *>(search_base);
  for (SIZE_T i = 0; i <= search_size - pattern_size; i++) {
    if (RtlCompareMemory(pattern, &base[i], pattern_size) == pattern_size) {
      return const_cast<char *>(&base[i]);
    }
  }
  return nullptr;
}

// A wrapper of MmGetSystemRoutineAddress
_Use_decl_annotations_ void *UtilGetSystemProcAddress(
    const wchar_t *proc_name) {
  UNREFERENCED_PARAMETER(proc_name);
  return nullptr; // No NT routines in UEFI
}

// Returns true when a system is on the x86 PAE mode
/*_Use_decl_annotations_*/ bool UtilIsX86Pae() {
  return (!IsX64() && Cr4{__readcr4()}.fields.pae);
}

// Return true if the given address is accessible.
_Use_decl_annotations_ bool UtilIsAccessibleAddress(void *address) {
  if (!UtilpIsCanonicalFormAddress(address)) {
    return false;
  }

// UtilpAddressToPxe, UtilpAddressToPpe defined for x64
#if defined(_AMD64_)
  const auto pxe = UtilpAddressToPxe(address);
  const auto ppe = UtilpAddressToPpe(address);
  if (!pxe->valid || !ppe->valid) {
    return false;
  }
#endif

  const auto pde = UtilpAddressToPde(address);
  const auto pte = UtilpAddressToPte(address);
  if (!pde->valid) {
    return false;
  }
  if (pde->large_page) {
    return true;  // A large page is always memory resident
  }
  if (!pte || !pte->valid) {
    return false;
  }
  return true;
}

// Checks whether the address is the canonical address
_Use_decl_annotations_ static bool UtilpIsCanonicalFormAddress(void *address) {
  if (!IsX64()) {
    return true;
  } else {
    return !UtilIsInBounds(reinterpret_cast<ULONG64>(address),
                           0x0000800000000000ull, 0xffff7fffffffffffull);
  }
}

#if defined(_AMD64_)
// Return an address of PXE
_Use_decl_annotations_ static HardwarePte *UtilpAddressToPxe(
    const void *address) {
  const auto addr = reinterpret_cast<ULONG_PTR>(address);
  const auto pxe_index = (addr >> g_utilp_pxi_shift) & g_utilp_pxi_mask;
  const auto offset = pxe_index * sizeof(HardwarePte);
  return reinterpret_cast<HardwarePte *>(g_utilp_pxe_base + offset);
}

// Return an address of PPE
_Use_decl_annotations_ static HardwarePte *UtilpAddressToPpe(
    const void *address) {
  const auto addr = reinterpret_cast<ULONG_PTR>(address);
  const auto ppe_index = (addr >> g_utilp_ppi_shift) & g_utilp_ppi_mask;
  const auto offset = ppe_index * sizeof(HardwarePte);
  return reinterpret_cast<HardwarePte *>(g_utilp_ppe_base + offset);
}
#endif

// Return an address of PDE
_Use_decl_annotations_ static HardwarePte *UtilpAddressToPde(
    const void *address) {
  const auto addr = reinterpret_cast<ULONG_PTR>(address);
  const auto pde_index = (addr >> g_utilp_pdi_shift) & g_utilp_pdi_mask;
  const auto offset = pde_index * sizeof(HardwarePte);
  return reinterpret_cast<HardwarePte *>(g_utilp_pde_base + offset);
}

// VA -> PA implementation for UEFI (Identity mapping)
extern "C" PHYSICAL_ADDRESS MmGetPhysicalAddress(void* va) {
  PHYSICAL_ADDRESS pa;
  pa.QuadPart = (LONGLONG)va;
  return pa;
}

extern "C" void* MmGetVirtualForPhysical(PHYSICAL_ADDRESS pa) {
  return (void*)pa.QuadPart;
}

// Return an address of PTE
_Use_decl_annotations_ static HardwarePte *UtilpAddressToPte(
    const void *address) {
  const auto addr = reinterpret_cast<ULONG_PTR>(address);
  const auto pte_index = (addr >> g_utilp_pti_shift) & g_utilp_pti_mask;
  const auto offset = pte_index * sizeof(HardwarePte);
  return reinterpret_cast<HardwarePte *>(g_utilp_pte_base + offset);
}

// VA -> PA
_Use_decl_annotations_ ULONG64 UtilPaFromVa(void *va) {
  return (ULONG64)va;
}

// VA -> PFN
_Use_decl_annotations_ PFN_NUMBER UtilPfnFromVa(void *va) {
  return UtilPfnFromPa(UtilPaFromVa(va));
}

// PA -> PFN
_Use_decl_annotations_ PFN_NUMBER UtilPfnFromPa(ULONG64 pa) {
  return static_cast<PFN_NUMBER>(pa >> PAGE_SHIFT);
}

// PA -> VA
_Use_decl_annotations_ void *UtilVaFromPa(ULONG64 pa) {
  PHYSICAL_ADDRESS pa2 = {};
  pa2.QuadPart = pa;
  return MmGetVirtualForPhysical(pa2);
}

// PNF -> PA
_Use_decl_annotations_ ULONG64 UtilPaFromPfn(PFN_NUMBER pfn) {
  return static_cast<ULONG64>(pfn) << PAGE_SHIFT;
}

// PFN -> VA
_Use_decl_annotations_ void *UtilVaFromPfn(PFN_NUMBER pfn) {
  return UtilVaFromPa(UtilPaFromPfn(pfn));
}

// Allocates continuous physical memory
_Use_decl_annotations_ void *UtilAllocateContiguousMemory(
    SIZE_T number_of_bytes) {
  UNREFERENCED_PARAMETER(number_of_bytes);
  return nullptr;
}

// Frees an address allocated by UtilAllocateContiguousMemory()
_Use_decl_annotations_ void UtilFreeContiguousMemory(void *base_address) {
  UNREFERENCED_PARAMETER(base_address);
}

// Executes VMCALL
_Use_decl_annotations_ NTSTATUS UtilVmCall(HypercallNumber hypercall_number,
                                           void *context) {
  __try {
    const auto vmx_status = static_cast<VmxStatus>(
        AsmVmxCall(static_cast<ULONG>(hypercall_number), context));
    return (vmx_status == VmxStatus::kOk) ? STATUS_SUCCESS
                                          : STATUS_UNSUCCESSFUL;

#pragma prefast(suppress : __WARNING_EXCEPTIONEXECUTEHANDLER, "Catch all.");
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    const auto status = GetExceptionCode();
    Soapivisor_COMMON_DBG_BREAK();
    Soapivisor_LOG_WARN_SAFE("Exception thrown (code %08x)", status);
    return status;
  }
}

// Debug prints registers
_Use_decl_annotations_ void UtilDumpGpRegisters(const AllRegisters *all_regs,
                                                ULONG_PTR stack_pointer) {
  const auto current_irql = KeGetCurrentIrql();
  if (current_irql < DISPATCH_LEVEL) {
    KeRaiseIrqlToDpcLevel();
  }

#if defined(_AMD64_)
  Soapivisor_LOG_DEBUG_SAFE(
      "Context at %p: "
      "rax= %016Ix rbx= %016Ix rcx= %016Ix "
      "rdx= %016Ix rsi= %016Ix rdi= %016Ix "
      "rsp= %016Ix rbp= %016Ix "
      " r8= %016Ix  r9= %016Ix r10= %016Ix "
      "r11= %016Ix r12= %016Ix r13= %016Ix "
      "r14= %016Ix r15= %016Ix efl= %08Ix",
      _ReturnAddress(), all_regs->gp.ax, all_regs->gp.bx, all_regs->gp.cx,
      all_regs->gp.dx, all_regs->gp.si, all_regs->gp.di, stack_pointer,
      all_regs->gp.bp, all_regs->gp.r8, all_regs->gp.r9, all_regs->gp.r10,
      all_regs->gp.r11, all_regs->gp.r12, all_regs->gp.r13, all_regs->gp.r14,
      all_regs->gp.r15, all_regs->flags.all);
#else
  Soapivisor_LOG_DEBUG_SAFE(
      "Context at %p: "
      "eax= %08Ix ebx= %08Ix ecx= %08Ix "
      "edx= %08Ix esi= %08Ix edi= %08Ix "
      "esp= %08Ix ebp= %08Ix efl= %08x",
      _ReturnAddress(), all_regs->gp.ax, all_regs->gp.bx, all_regs->gp.cx,
      all_regs->gp.dx, all_regs->gp.si, all_regs->gp.di, stack_pointer,
      all_regs->gp.bp, all_regs->flags.all);
#endif

  if (current_irql < DISPATCH_LEVEL) {
    KeLowerIrql(current_irql);
  }
}

// Reads natural-width VMCS
_Use_decl_annotations_ ULONG_PTR UtilVmRead(VmcsField field) {
  size_t field_value = 0;
  const auto vmx_status = static_cast<VmxStatus>(
      __vmx_vmread(static_cast<size_t>(field), &field_value));
  if (vmx_status != VmxStatus::kOk) {
    Soapivisor_COMMON_BUG_CHECK(
        SoapivisorBugCheck::kCriticalVmxInstructionFailure,
        static_cast<ULONG_PTR>(vmx_status), static_cast<ULONG_PTR>(field), 0);
  }
  return field_value;
}

// Reads 64bit-width VMCS
_Use_decl_annotations_ ULONG64 UtilVmRead64(VmcsField field) {
#if defined(_AMD64_)
  return UtilVmRead(field);
#else
  // Only 64bit fields should be given on x86 because it access field + 1 too.
  // Also, the field must be even number.
  NT_ASSERT(UtilIsInBounds(field, VmcsField::kIoBitmapA,
                           VmcsField::kHostIa32PerfGlobalCtrlHigh));
  NT_ASSERT((static_cast<ULONG>(field) % 2) == 0);

  ULARGE_INTEGER value64 = {};
  value64.LowPart = UtilVmRead(field);
  value64.HighPart =
      UtilVmRead(static_cast<VmcsField>(static_cast<ULONG>(field) + 1));
  return value64.QuadPart;
#endif
}

// Writes natural-width VMCS
_Use_decl_annotations_ VmxStatus UtilVmWrite(VmcsField field,
                                             ULONG_PTR field_value) {
  return static_cast<VmxStatus>(
      __vmx_vmwrite(static_cast<size_t>(field), field_value));
}

// Writes 64bit-width VMCS
_Use_decl_annotations_ VmxStatus UtilVmWrite64(VmcsField field,
                                               ULONG64 field_value) {
#if defined(_AMD64_)
  return UtilVmWrite(field, field_value);
#else
  // Only 64bit fields should be given on x86 because it access field + 1 too.
  // Also, the field must be even number.
  NT_ASSERT(UtilIsInBounds(field, VmcsField::kIoBitmapA,
                           VmcsField::kHostIa32PerfGlobalCtrlHigh));
  NT_ASSERT((static_cast<ULONG>(field) % 2) == 0);

  ULARGE_INTEGER value64 = {};
  value64.QuadPart = field_value;
  const auto vmx_status = UtilVmWrite(field, value64.LowPart);
  if (vmx_status != VmxStatus::kOk) {
    return vmx_status;
  }
  return UtilVmWrite(static_cast<VmcsField>(static_cast<ULONG>(field) + 1),
                     value64.HighPart);
#endif
}

// Reads natural-width MSR
_Use_decl_annotations_ ULONG_PTR UtilReadMsr(Msr msr) {
  return static_cast<ULONG_PTR>(__readmsr(static_cast<unsigned long>(msr)));
}

// Reads 64bit-width MSR
_Use_decl_annotations_ ULONG64 UtilReadMsr64(Msr msr) {
  return __readmsr(static_cast<unsigned long>(msr));
}

// Writes natural-width MSR
_Use_decl_annotations_ void UtilWriteMsr(Msr msr, ULONG_PTR value) {
  __writemsr(static_cast<unsigned long>(msr), value);
}

// Writes 64bit-width MSR
_Use_decl_annotations_ void UtilWriteMsr64(Msr msr, ULONG64 value) {
  __writemsr(static_cast<unsigned long>(msr), value);
}

// Executes the INVEPT instruction and invalidates EPT entry cache
/*_Use_decl_annotations_*/ VmxStatus UtilInveptGlobal() {
  InvEptDescriptor desc = {};
  return static_cast<VmxStatus>(
      AsmInvept(InvEptType::kGlobalInvalidation, &desc));
}

// Executes the INVVPID instruction (type 0)
_Use_decl_annotations_ VmxStatus UtilInvvpidIndividualAddress(USHORT vpid,
                                                              void *address) {
  InvVpidDescriptor desc = {};
  desc.vpid = vpid;
  desc.linear_address = reinterpret_cast<ULONG64>(address);
  return static_cast<VmxStatus>(
      AsmInvvpid(InvVpidType::kIndividualAddressInvalidation, &desc));
}

// Executes the INVVPID instruction (type 1)
_Use_decl_annotations_ VmxStatus UtilInvvpidSingleContext(USHORT vpid) {
  InvVpidDescriptor desc = {};
  desc.vpid = vpid;
  return static_cast<VmxStatus>(
      AsmInvvpid(InvVpidType::kSingleContextInvalidation, &desc));
}

// Executes the INVVPID instruction (type 2)
/*_Use_decl_annotations_*/ VmxStatus UtilInvvpidAllContext() {
  InvVpidDescriptor desc = {};
  return static_cast<VmxStatus>(
      AsmInvvpid(InvVpidType::kAllContextInvalidation, &desc));
}

// Executes the INVVPID instruction (type 3)
_Use_decl_annotations_ VmxStatus
UtilInvvpidSingleContextExceptGlobal(USHORT vpid) {
  InvVpidDescriptor desc = {};
  desc.vpid = vpid;
  return static_cast<VmxStatus>(
      AsmInvvpid(InvVpidType::kSingleContextInvalidationExceptGlobal, &desc));
}

// Loads the PDPTE registers from CR3 to VMCS
_Use_decl_annotations_ void UtilLoadPdptes(ULONG_PTR cr3_value) {
  const auto current_cr3 = __readcr3();

  // Have to load cr3 to make UtilPfnFromVa() work properly.
  __writecr3(cr3_value);

  // Gets PDPTEs form CR3
  PdptrRegister pd_pointers[4] = {};
  for (auto i = 0ul; i < 4; ++i) {
    const auto pd_addr = g_utilp_pde_base + i * PAGE_SIZE;
    pd_pointers[i].fields.present = true;
    pd_pointers[i].fields.page_directory_pa =
        UtilPfnFromVa(reinterpret_cast<void *>(pd_addr));
  }

  __writecr3(current_cr3);
  UtilVmWrite64(VmcsField::kGuestPdptr0, pd_pointers[0].all);
  UtilVmWrite64(VmcsField::kGuestPdptr1, pd_pointers[1].all);
  UtilVmWrite64(VmcsField::kGuestPdptr2, pd_pointers[2].all);
  UtilVmWrite64(VmcsField::kGuestPdptr3, pd_pointers[3].all);
}

// Does RtlCopyMemory safely even if destination is a read only region
_Use_decl_annotations_ NTSTATUS UtilForceCopyMemory(void *destination,
                                                    const void *source,
                                                    SIZE_T length) {
  auto mdl = IoAllocateMdl(destination, static_cast<ULONG>(length), FALSE,
                           FALSE, nullptr);
  if (!mdl) {
    return STATUS_INSUFFICIENT_RESOURCES;
  }
  MmBuildMdlForNonPagedPool(mdl);

#pragma warning(push)
#pragma warning(disable : 28145)
  // Following MmMapLockedPagesSpecifyCache() call causes bug check in case
  // you are using Driver Verifier. The reason is explained as followings:
  //
  // A driver must not try to create more than one system-address-space
  // mapping for an MDL. Additionally, because an MDL that is built by the
  // MmBuildMdlForNonPagedPool routine is already mapped to the system
  // address space, a driver must not try to map this MDL into the system
  // address space again by using the MmMapLockedPagesSpecifyCache routine.
  // -- MSDN
  //
  // This flag modification hacks Driver Verifier's check and prevent leading
  // bug check.
  mdl->MdlFlags &= ~MDL_SOURCE_IS_NONPAGED_POOL;
  mdl->MdlFlags |= MDL_PAGES_LOCKED;
#pragma warning(pop)

  const auto writable_dest =
      MmMapLockedPagesSpecifyCache(mdl, KernelMode, MmCached, nullptr, FALSE,
                                   NormalPagePriority | MdlMappingNoExecute);
  if (!writable_dest) {
    IoFreeMdl(mdl);
    return STATUS_INSUFFICIENT_RESOURCES;
  }
  RtlCopyMemory(writable_dest, source, length);
  MmUnmapLockedPages(writable_dest, mdl);
  IoFreeMdl(mdl);
  return STATUS_SUCCESS;
}

}  // extern "C"
