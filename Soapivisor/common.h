// Copyright (c) 2015-2019, Satoshi Tanda. All rights reserved.
// Use of this source code is governed by a MIT-style license that can be
// found in the LICENSE file.

/// @file
/// Declares and implements common things across the project

/// @mainpage
/// @section whats About
/// These pages serve as a programmer's reference manual for Soapivisor and
/// were automatically generated from the source using Doxygen.
///
/// For compilation and installation of Soapivisor, see the Soapivisor
/// project page. For more general information about development using
/// Soapivisor, see User's Documents in the project page.
/// @li https://github.com/tandasat/Soapivisor
///
/// Some of good places to start are the files page that provides a brief
/// description of each files, the DriverEntry() function where is an entry
/// point
/// of Soapivisor, and the VmmVmExitHandler() function, a high-level entry
/// point of VM-exit handlers.
///
/// @subsection links External Document
/// This document often refers to the Intel 64 and IA-32 Architectures Software
/// Developer Manuals (Intel SDM). Any descriptions like
/// "See: CONTROL REGISTERS" implies that details are explained in a page or a
/// table titled as "CONTROL REGISTERS" in the Intel SDM.
/// @li
/// http://www.intel.com/content/www/us/en/processors/architectures-software-developer-manuals.html
///
/// @copyright Use of this source code is governed by a MIT-style license that
///            can be found in the LICENSE file.

#ifndef Soapivisor_COMMON_H_
#define Soapivisor_COMMON_H_

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

// C30030: Calling a memory allocating function and passing a parameter that
// indicates executable memory
//
// Disable C30030 since POOL_NX_OPTIN + ExInitializeDriverRuntime is in place.
// This warning is false positive and can be seen when Target Platform Version
// equals to 10.0.14393.0.
#pragma prefast(disable : 30030)

////////////////////////////////////////////////////////////////////////////////
//
// macro utilities
//

/// Sets a break point that works only when a debugger is present
#if !defined(Soapivisor_COMMON_DBG_BREAK)
#define Soapivisor_COMMON_DBG_BREAK() 
#endif

/// Issues a bug check (For UEFI, we just hang to preserve state or optionally reset)
#if !defined(Soapivisor_COMMON_BUG_CHECK)
#define Soapivisor_COMMON_BUG_CHECK(hp_bug_check_code, param1, param2, param3) \
  while (true) {}
#endif

// NT API Stubs for UEFI
#define PAGED_CODE()
#define NT_ASSERT(exp)
#define PASSIVE_LEVEL 0
#define APC_LEVEL 1
#define DISPATCH_LEVEL 2

typedef ULONG KIRQL;
inline KIRQL KeGetCurrentIrql() { return PASSIVE_LEVEL; }

// Pool Types (for ExAllocatePoolZero compatibility)
typedef enum _POOL_TYPE {
  NonPagedPool,
  NonPagedPoolExecute,
  PagedPool,
  NonPagedPoolMustSucceed,
  DontUseThisType,
  NonPagedPoolCacheAligned,
  PagedPoolCacheAligned,
  NonPagedPoolMustSucceedSession,
  DontUseThisTypeSession,
  NonPagedPoolCacheAlignedSession,
  PagedPoolCacheAlignedSession,
  NonPagedPoolNx,
  NonPagedPoolSessionNx,
  NonPagedPoolNxCacheAligned,
  NonPagedPoolSession
} POOL_TYPE;

// Memory allocation stubs
extern "C" void* ExAllocatePoolZero(POOL_TYPE PoolType, SIZE_T NumberOfBytes, ULONG Tag);
extern "C" void ExFreePoolWithTag(void* P, ULONG Tag);
#define ExFreePool(P) ExFreePoolWithTag(P, 0)

typedef struct _PHYSICAL_MEMORY_RANGE {
  PHYSICAL_ADDRESS BaseAddress;
  LARGE_INTEGER NumberOfBytes;
} PHYSICAL_MEMORY_RANGE, *PPHYSICAL_MEMORY_RANGE;

extern "C" PPHYSICAL_MEMORY_RANGE MmGetPhysicalMemoryRanges();
#define MmFreePhysicalMemoryRanges(ranges) ExFreePoolWithTag(ranges, 0)

////////////////////////////////////////////////////////////////////////////////
//
// constants and macros
//

/// Enable or disable performance monitoring globally
///
/// Enables #Soapivisor_PERFORMANCE_MEASURE_THIS_SCOPE() which measures
/// an elapsed time of the scope when set to non 0. Enabling it introduces
/// negative performance impact.
#define Soapivisor_PERFORMANCE_ENABLE_PERFCOUNTER 0

/// A pool tag (Randomized at compile time to avoid signature scanning)
constexpr ULONG CompileTimeHash(const char* str) {
    ULONG hash = 5381;
    for (int i = 0; str[i] != '\0'; ++i) {
        hash = ((hash << 5) + hash) + str[i];
    }
    return hash;
}
static constexpr ULONG kSoapivisorCommonPoolTag = CompileTimeHash(__TIME__);

////////////////////////////////////////////////////////////////////////////////
//
// types
//

/// BugCheck codes for #Soapivisor_COMMON_BUG_CHECK().
enum class SoapivisorBugCheck : ULONG {
  kUnspecified,                    //!< An unspecified bug occurred
  kUnexpectedVmExit,               //!< An unexpected VM-exit occurred
  kTripleFaultVmExit,              //!< A triple fault VM-exit occurred
  kExhaustedPreallocatedEntries,   //!< All pre-allocated entries are used
  kCriticalVmxInstructionFailure,  //!< VMRESUME or VMXOFF has failed
  kEptMisconfigVmExit,             //!< EPT misconfiguration VM-exit occurred
  kCritialPoolAllocationFailure,   //!< Critical pool allocation failed
};

////////////////////////////////////////////////////////////////////////////////
//
// prototypes
//

////////////////////////////////////////////////////////////////////////////////
//
// variables
//

////////////////////////////////////////////////////////////////////////////////
//
// implementations
//

/// Checks if a system is x64
/// @return true if a system is x64
constexpr bool IsX64() {
#if defined(_AMD64_)
  return true;
#else
  return false;
#endif
}

/// Checks if the project is compiled as Release
/// @return true if the project is compiled as Release
constexpr bool IsReleaseBuild() {
#if defined(DBG)
  return false;
#else
  return true;
#endif
}
extern "C" bool IsHypervisorPage(ULONG64 physical_address);

#endif  // Soapivisor_COMMON_H_
