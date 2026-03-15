// Copyright (c) 2024, Soapivisor. All rights reserved.
// Use of this source code is governed by a MIT-style license that can be
// found in the LICENSE file.

/// @file
/// Implements advanced stealth subsystem for undetectable hypervisor operation

#include "stealth.h"
#include "common.h"
#include "vmm.h"

extern "C" {

////////////////////////////////////////////////////////////////////////////////
//
// Constants
//

static constexpr ULONG kStealthPoolTag = 'lthS';  // 'Sthl' in little-endian

////////////////////////////////////////////////////////////////////////////////
//
// Global State
//

static StealthConfig g_stealth_config = {};
static bool g_stealth_initialized = false;

// Per-processor contexts (allocated per CPU during initialization)
static NmiSpoofContext* g_nmi_contexts = nullptr;
static TscCompensationContext* g_tsc_contexts = nullptr;
static EptStealthContext* g_ept_contexts = nullptr;

////////////////////////////////////////////////////////////////////////////////
//
// Internal Prototypes
//

static void StealthpInitializeNmiContext(_Inout_ NmiSpoofContext* context);
static void StealthpInitializeTscContext(
    _Inout_ TscCompensationContext* context, _In_ TscCompensationMode mode);
static void StealthpInitializeEptContext(_Inout_ EptStealthContext* context);

////////////////////////////////////////////////////////////////////////////////
//
// Implementation
//

/// Get default stealth configuration (maximum stealth)
void StealthGetDefaultConfig(_Out_ StealthConfig* config) {
  if (!config) return;

  // NMI stealth - full protection
  config->nmi_spoof_mode = NmiSpoofMode::kFull;
  config->nmi_stack_fabrication = true;
  config->nmi_spoof_target_module = 0;  // Will use ntoskrnl

  // TSC stealth - adaptive compensation
  config->tsc_mode = TscCompensationMode::kAdaptive;
  config->rdtsc_exiting = true;
  config->rdtscp_exiting = true;

  // CPUID stealth - hide all virtualization traces
  config->cpuid_flags = CpuidSpoofFlags::kAll;
  config->vendor_spoof = VendorIdSpoof::kNone;  // Keep real vendor

  // EPT stealth - full memory hiding
  config->ept_dummy_page_swapping = true;
  config->ept_mtrr_synchronization = true;
  config->dynamic_ept_construction = true;

  // MSR stealth
  config->msr_feature_control_spoof = true;
  config->msr_vmx_capability_spoof = true;

  // Debug stealth
  config->hide_from_kernel_debugger = true;
  config->prevent_memory_scanning = true;
}

/// Get paranoid stealth configuration (undetectable mode)
void StealthGetParanoidConfig(_Out_ StealthConfig* config) {
  StealthGetDefaultConfig(config);

  if (!config) return;

  // Even more aggressive settings for paranoid mode
  config->nmi_spoof_mode = NmiSpoofMode::kAdaptive;
  config->tsc_mode = TscCompensationMode::kFull;
  config->vendor_spoof = VendorIdSpoof::kRandom;
  config->hide_from_kernel_debugger = true;
}

/// Initialize stealth subsystem
_Use_decl_annotations_ NTSTATUS
StealthInitialization(const StealthConfig* config) {
  PAGED_CODE()

  if (g_stealth_initialized) {
    return STATUS_SUCCESS;  // Already initialized
  }

  if (!config) {
    StealthGetDefaultConfig(&g_stealth_config);
  } else {
    g_stealth_config = *config;
  }

  // Get processor count
  const auto processor_count =
      KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);

  // Allocate per-processor contexts
  const SIZE_T nmi_context_size = sizeof(NmiSpoofContext) * processor_count;
  const SIZE_T tsc_context_size =
      sizeof(TscCompensationContext) * processor_count;
  const SIZE_T ept_context_size = sizeof(EptStealthContext) * processor_count;

  g_nmi_contexts = static_cast<NmiSpoofContext*>(
      ExAllocatePoolZero(NonPagedPool, nmi_context_size, kStealthPoolTag));
  if (!g_nmi_contexts) {
    return STATUS_MEMORY_NOT_ALLOCATED;
  }

  g_tsc_contexts = static_cast<TscCompensationContext*>(
      ExAllocatePoolZero(NonPagedPool, tsc_context_size, kStealthPoolTag));
  if (!g_tsc_contexts) {
    ExFreePoolWithTag(g_nmi_contexts, kStealthPoolTag);
    g_nmi_contexts = nullptr;
    return STATUS_MEMORY_NOT_ALLOCATED;
  }

  g_ept_contexts = static_cast<EptStealthContext*>(
      ExAllocatePoolZero(NonPagedPool, ept_context_size, kStealthPoolTag));
  if (!g_ept_contexts) {
    ExFreePoolWithTag(g_nmi_contexts, kStealthPoolTag);
    ExFreePoolWithTag(g_tsc_contexts, kStealthPoolTag);
    g_nmi_contexts = nullptr;
    g_tsc_contexts = nullptr;
    return STATUS_MEMORY_NOT_ALLOCATED;
  }

  // Initialize contexts for each processor
  for (SIZE_T i = 0; i < processor_count; ++i) {
    StealthpInitializeNmiContext(&g_nmi_contexts[i]);
    StealthpInitializeTscContext(&g_tsc_contexts[i], g_stealth_config.tsc_mode);
    StealthpInitializeEptContext(&g_ept_contexts[i]);
  }

  g_stealth_initialized = true;
  Soapivisor_LOG_INFO("Stealth subsystem initialized (%lu processors)",
                      static_cast<ULONG>(processor_count));

  return STATUS_SUCCESS;
}

/// Terminate stealth subsystem
_Use_decl_annotations_ void StealthTermination() {
  PAGED_CODE()

  if (!g_stealth_initialized) {
    return;
  }

  if (g_nmi_contexts) {
    ExFreePoolWithTag(g_nmi_contexts, kStealthPoolTag);
    g_nmi_contexts = nullptr;
  }

  if (g_tsc_contexts) {
    ExFreePoolWithTag(g_tsc_contexts, kStealthPoolTag);
    g_tsc_contexts = nullptr;
  }

  if (g_ept_contexts) {
    ExFreePoolWithTag(g_ept_contexts, kStealthPoolTag);
    g_ept_contexts = nullptr;
  }

  g_stealth_initialized = false;
  Soapivisor_LOG_INFO("Stealth subsystem terminated");
}

/// Initialize NMI spoof context
static void StealthpInitializeNmiContext(NmiSpoofContext* context) {
  if (!context) return;

  context->spoofed_rip = 0;
  context->spoofed_rsp = 0;
  context->original_rip = 0;
  context->original_rsp = 0;
  context->fabricated_ret = 0;
  context->restore_pending = false;
  context->restore_tsc = 0;
}

/// Initialize TSC compensation context
static void StealthpInitializeTscContext(TscCompensationContext* context,
                                         TscCompensationMode mode) {
  if (!context) return;

  context->base_offset = 0;
  context->last_exit_tsc = 0;
  context->accumulated_latency = 0;
  context->exit_count = 0;
  context->mode = mode;
}

/// Initialize EPT stealth context
static void StealthpInitializeEptContext(EptStealthContext* context) {
  if (!context) return;

  context->hidden_region_count = 0;
  context->dummy_page_index = 0;
  context->dynamic_remapping = true;

  for (SIZE_T i = 0; i < kMaxHiddenRegions; ++i) {
    context->hidden_regions[i].active = false;
  }

  for (SIZE_T i = 0; i < kDummyPagePoolSize; ++i) {
    context->dummy_page_pool[i] = 0;
  }
}

/// Get NMI spoof context for current processor
NmiSpoofContext* StealthGetNmiContext() {
  if (!g_stealth_initialized || !g_nmi_contexts) {
    return nullptr;
  }

  const auto proc_num = KeGetCurrentProcessorNumberEx(nullptr);
  return &g_nmi_contexts[proc_num];
}

/// Get TSC compensation context for current processor
TscCompensationContext* StealthGetTscContext() {
  if (!g_stealth_initialized || !g_tsc_contexts) {
    return nullptr;
  }

  const auto proc_num = KeGetCurrentProcessorNumberEx(nullptr);
  return &g_tsc_contexts[proc_num];
}

/// Register a memory region for hiding
_Use_decl_annotations_ bool StealthRegisterHiddenRegion(ULONG64 physical_base,
                                                        ULONG64 size,
                                                        EptHookType hook_type) {
  if (!g_stealth_initialized || !g_ept_contexts) {
    return false;
  }

  const auto proc_num = KeGetCurrentProcessorNumberEx(nullptr);
  auto* context = &g_ept_contexts[proc_num];

  if (context->hidden_region_count >= kMaxHiddenRegions) {
    return false;
  }

  // Find free slot
  for (SIZE_T i = 0; i < kMaxHiddenRegions; ++i) {
    if (!context->hidden_regions[i].active) {
      context->hidden_regions[i].physical_base = physical_base;
      context->hidden_regions[i].size = size;
      context->hidden_regions[i].hook_type = hook_type;
      context->hidden_regions[i].dummy_page_pfn = 0;  // Allocated on first use
      context->hidden_regions[i].active = true;
      context->hidden_region_count++;
      return true;
    }
  }

  return false;
}

/// Unregister a hidden memory region
_Use_decl_annotations_ bool StealthUnregisterHiddenRegion(
    ULONG64 physical_base) {
  if (!g_stealth_initialized || !g_ept_contexts) {
    return false;
  }

  const auto proc_num = KeGetCurrentProcessorNumberEx(nullptr);
  auto* context = &g_ept_contexts[proc_num];

  for (SIZE_T i = 0; i < kMaxHiddenRegions; ++i) {
    if (context->hidden_regions[i].active &&
        context->hidden_regions[i].physical_base == physical_base) {
      context->hidden_regions[i].active = false;
      context->hidden_region_count--;
      return true;
    }
  }

  return false;
}

}  // extern "C"
