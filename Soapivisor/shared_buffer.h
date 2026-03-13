#pragma once

#include <stdint.h>

// The hypervisor dynamically allocates this address in EfiReservedMemoryType 
// and exposes it to usermode via CPUID leaf 0x77777777.
extern UINT64 g_SharedBufferPhysicalAddress;

struct HvRequest {
    uint32_t request_id;
    uint32_t count;       // number of addresses
    uint64_t addresses[64];
};

struct HvResult {
    uint32_t request_id;
    uint64_t values[64];  // results
};

// We place both in a single page
struct SharedBufferPage {
    struct HvRequest request;
    struct HvResult result;
};
