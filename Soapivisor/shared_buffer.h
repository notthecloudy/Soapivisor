#pragma once

#include <stdint.h>

// Use a fixed physical address for the shared buffer for simplicity.
// The user mode application will map this physical address to read/write.
// We use a high physical address that is typically RAM but can be reserved.
#define SHARED_BUFFER_PHYSICAL_ADDRESS 0x7E000000 // Example: ~2GB mark

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
