// Copyright (c) 2024, Soapivisor. All rights reserved.
// Use of this source code is governed by a MIT-style license that can be
// found in the LICENSE file.

/// @file
/// Defines advanced stealth structures and constants for undetectable operation

#ifndef Soapivisor_STEALTH_H_
#define Soapivisor_STEALTH_H_

#include "common.h"

////////////////////////////////////////////////////////////////////////////////
//
// Stealth Configuration Constants
//

/// Maximum number of GVA cache entries per processor
static constexpr SIZE_T kGvaCacheSize = 16;

/// Maximum number of hidden memory regions
static constexpr SIZE_T kMaxHiddenRegions = 32;

/// TSC offset adjustment granularity (cycles)
static constexpr ULONG64 kTscAdjustmentGranularity = 150;

/// Dummy page pool size (number of pages for EPT spoofing)
static constexpr SIZE_T kDummyPagePoolSize = 8;

////////////////////////////////////////////////////////////////////////////////
//
// NMI Stealth Configuration
//

/// NMI spoofing modes
enum class NmiSpoofMode : ULONG {
  kDisabled = 0,  ///< No NMI spoofing
  kBasic = 1,     ///< Basic RIP spoofing only
  kFull = 2,      ///< Full stack frame fabrication
  kAdaptive = 3,  ///< Adaptive based on caller context
};

/// Structure to store legitimate-looking stack frame for NMI spoofing
struct NmiSpoofContext {
  ULONG64 spoofed_rip;     ///< Fake instruction pointer
  ULONG64 spoofed_rsp;     ///< Fake stack pointer
  ULONG64 original_rip;    ///< Real instruction pointer
  ULONG64 original_rsp;    ///< Real stack pointer
  ULONG64 fabricated_ret;  ///< Fabricated return address
  bool restore_pending;    ///< Whether restoration is pending
  ULONG64 restore_tsc;     ///< TSC when restoration should occur
};

////////////////////////////////////////////////////////////////////////////////
//
// Timing Attack Mitigation
//

/// TSC compensation modes
enum class TscCompensationMode : ULONG {
  kNone = 0,      ///< No TSC compensation
  kBasic = 1,     ///< Simple offset subtraction
  kAdaptive = 2,  ///< Adaptive based on exit reason
  kFull = 3,      ///< Full timing model emulation
};

/// Structure for TSC tracking and compensation
struct TscCompensationContext {
  ULONG64 base_offset;          ///< Base TSC offset applied to guest
  ULONG64 last_exit_tsc;        ///< TSC at last VM-exit
  ULONG64 accumulated_latency;  ///< Accumulated latency to compensate
  ULONG64 exit_count;           ///< Number of VM-exits handled
  TscCompensationMode mode;     ///< Current compensation mode
};

////////////////////////////////////////////////////////////////////////////////
//
// EPT Stealth Configuration
//

/// EPT hook types
enum class EptHookType : ULONG {
  kNone = 0,
  kRead = 1,
  kWrite = 2,
  kExecute = 4,
  kAccess = kRead | kWrite | kExecute,
};

/// Hidden memory region descriptor
struct HiddenRegion {
  ULONG64 physical_base;   ///< Physical base address
  ULONG64 size;            ///< Size in bytes
  ULONG64 dummy_page_pfn;  ///< PFN of dummy page for spoofing
  EptHookType hook_type;   ///< Type of hook applied
  bool active;             ///< Whether this region is currently hidden
};

/// EPT stealth context per processor
struct EptStealthContext {
  HiddenRegion hidden_regions[kMaxHiddenRegions];
  ULONG64 dummy_page_pool[kDummyPagePoolSize];
  SIZE_T dummy_page_index;
  SIZE_T hidden_region_count;
  bool dynamic_remapping;  ///< Enable dynamic remapping on access
};

////////////////////////////////////////////////////////////////////////////////
//
// CPUID Spoofing Configuration
//

/// CPUID spoofing flags
enum class CpuidSpoofFlags : ULONG {
  kNone = 0,
  kHideHypervisorPresent = 1 << 0,
  kHideVmxSupport = 1 << 1,
  kHideX2Apic = 1 << 2,
  kSpoofVendorId = 1 << 3,
  kFilterLeaf40000000 = 1 << 4,
  kAll = kHideHypervisorPresent | kHideVmxSupport | kHideX2Apic |
         kSpoofVendorId | kFilterLeaf40000000,
};

/// Vendor ID spoofing options
enum class VendorIdSpoof : ULONG {
  kNone = 0,          ///< Don't spoof (return real)
  kGenuineIntel = 1,  ///< "GenuineIntel"
  kAuthenticAmd = 2,  ///< "AuthenticAMD"
  kRandom = 3,        ///< Random plausible vendor
};

////////////////////////////////////////////////////////////////////////////////
//
// Global Stealth State
//

/// Master stealth configuration
struct StealthConfig {
  // NMI stealth
  NmiSpoofMode nmi_spoof_mode;
  bool nmi_stack_fabrication;
  ULONG64 nmi_spoof_target_module;  ///< Base of module to attribute to

  // TSC stealth
  TscCompensationMode tsc_mode;
  bool rdtsc_exiting;
  bool rdtscp_exiting;

  // CPUID stealth
  CpuidSpoofFlags cpuid_flags;
  VendorIdSpoof vendor_spoof;

  // EPT stealth
  bool ept_dummy_page_swapping;
  bool ept_mtrr_synchronization;
  bool dynamic_ept_construction;

  // MSR stealth
  bool msr_feature_control_spoof;
  bool msr_vmx_capability_spoof;

  // Debug stealth
  bool hide_from_kernel_debugger;
  bool prevent_memory_scanning;
};

////////////////////////////////////////////////////////////////////////////////
//
// Function Prototypes
//

#ifdef __cplusplus
extern "C" {
#endif

/// Initialize stealth subsystem
_IRQL_requires_max_(PASSIVE_LEVEL) NTSTATUS
    StealthInitialization(_In_ const StealthConfig* config);

/// Terminate stealth subsystem
_IRQL_requires_max_(PASSIVE_LEVEL) void StealthTermination();

/// Get default stealth configuration (maximum stealth)
void StealthGetDefaultConfig(_Out_ StealthConfig* config);

/// Get paranoid stealth configuration (undetectable mode)
void StealthGetParanoidConfig(_Out_ StealthConfig* config);

/// Register a memory region for hiding
_IRQL_requires_max_(DISPATCH_LEVEL) bool StealthRegisterHiddenRegion(
    _In_ ULONG64 physical_base, _In_ ULONG64 size, _In_ EptHookType hook_type);

/// Unregister a hidden memory region
_IRQL_requires_max_(DISPATCH_LEVEL) bool StealthUnregisterHiddenRegion(
    _In_ ULONG64 physical_base);

/// Get NMI spoof context for current processor
NmiSpoofContext* StealthGetNmiContext();

/// Get TSC compensation context for current processor
TscCompensationContext* StealthGetTscContext();

#ifdef __cplusplus
}
#endif

#endif  // Soapivisor_STEALTH_H_
