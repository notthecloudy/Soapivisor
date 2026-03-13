// Copyright (c) 2015-2022, Satoshi Tanda. All rights reserved.
// Use of this source code is governed by a MIT-style license that can be
// found in the LICENSE file.

/// @file
/// Implements global object functions.

#include "global_object.h"

// Note: MSVC specific .CRT sections are removed for native UEFI compatibility.
// Initialization is handled manually via GlobalObjectInitialization.

extern "C" {
////////////////////////////////////////////////////////////////////////////////
//
// macro utilities
//

////////////////////////////////////////////////////////////////////////////////
//
// constants and macros
//

/// A pool tag for this module
static const ULONG kGlobalObjectpPoolTag = 'jbOG';

////////////////////////////////////////////////////////////////////////////////
//
// types
//

using Destructor = void(__cdecl *)();

struct DestructorEntry {
  Destructor dtor;
  SINGLE_LIST_ENTRY list_entry;
};

////////////////////////////////////////////////////////////////////////////////
//
// prototypes
//

#if defined(ALLOC_PRAGMA)
#pragma alloc_text(INIT, GlobalObjectInitialization)
#pragma alloc_text(INIT, atexit)
#pragma alloc_text(PAGE, GlobalObjectTermination)
#endif

////////////////////////////////////////////////////////////////////////////////
//
// variables
//

// Markers for constructors removed for UEFI compatibility.
static Destructor* g_gop_ctors_begin = nullptr;
static Destructor* g_gop_ctors_end = nullptr;

// Stores pointers to dtors to be called at the exit.
static SINGLE_LIST_ENTRY g_gop_dtors_list_head = {};

////////////////////////////////////////////////////////////////////////////////
//
// implementations
//

// Calls all constructors and register all destructor
_Use_decl_annotations_ NTSTATUS GlobalObjectInitialization() {
  PAGED_CODE()

  // Call all constructors
  if (g_gop_ctors_begin && g_gop_ctors_end) {
    for (auto ctor = g_gop_ctors_begin + 1; ctor < g_gop_ctors_end; ++ctor) {
      if (*ctor) {
        (*ctor)();
      }
    }
  }
  return STATUS_SUCCESS;
}

// Calls all registered destructors
_Use_decl_annotations_ void GlobalObjectTermination() {
  PAGED_CODE()

  auto entry = PopEntryList(&g_gop_dtors_list_head);
  while (entry) {
    const auto element = CONTAINING_RECORD(entry, DestructorEntry, list_entry);
    element->dtor();
    ExFreePoolWithTag(element, kGlobalObjectpPoolTag);
    entry = PopEntryList(&g_gop_dtors_list_head);
  }
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-prototypes"

// Registers destructor; this is called through a call to constructor
_IRQL_requires_max_(PASSIVE_LEVEL) int __cdecl atexit(_In_ Destructor dtor) {
  PAGED_CODE()

  const auto element = reinterpret_cast<DestructorEntry *>(ExAllocatePoolZero(
      PagedPool, sizeof(DestructorEntry), kGlobalObjectpPoolTag));
  if (!element) {
    return 1;
  }
  element->dtor = dtor;
  PushEntryList(&g_gop_dtors_list_head, &element->list_entry);
  return 0;
}

#pragma clang diagnostic pop

}  // extern "C"
