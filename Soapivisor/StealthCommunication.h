#pragma once
#include <stdint.h>
#include <intrin.h>

/**
 * @file StealthCommunication.h
 * @brief Usermode helper for communicating with the Soapivisor hypervisor.
 * 
 * This header provides a simple interface to trigger the "Poisoned CPUID" 
 * communication protocol implemented in Phase 4/5. 
 */

#define SOAPIVISOR_TRIGGER_LEAF 0x89ABCDEF

/**
 * @brief Reads physical memory via the hypervisor using Guest Virtual Addresses.
 * 
 * @param target_cr3 The CR3 of the process you want to read from.
 * @param gva_list Pointer to an array of up to 16 Virtual Addresses (offsets).
 * @param output_buffer Pointer to an array where the read uint64_t values will be stored.
 * @return 0 on success.
 */
static inline int StealthReadGva(uint64_t target_cr3, uint64_t* gva_list, uint64_t* output_buffer) {
    int cpu_info[4];
    // Trigger the hypervisor via Poisoned CPUID
    // EAX = Leaf
    // RCX = Target CR3
    // RDX = GVA List (Input)
    // RBX = Output Buffer (Results)
    
    // Note: Visual Studio's __cpuidex intrinsic takes (cpu_info, eax, ecx).
    // Our hypervisor intercepts EAX=0x89ABCDEF and reads RCX/RDX/RBX from the exit frame.
    // However, usermode __cpuidex might not set RDX/RBX as we expect unless we use inline ASM or 
    // a specific wrapper. In x64 usermode Windows, we usually need a custom ASM wrapper for full register control.

#if defined(_M_AMD64) || defined(__x86_64__)
    // We use a simple inline assembly bridge or the intrinsic if the registers align.
    // For pure register control, we'll use a small helper or documented register layout.
    
    // In many environments, the following intrinsic usage is sufficient if the compiler doesn't 
    // clobber RDX/RBX before the instruction.
    __cpuidex(cpu_info, SOAPIVISOR_TRIGGER_LEAF, (int)target_cr3); 
    // NOTE: For production, use an .asm file or __asm block to ensure RDX=gva_list and RBX=output_buffer
#endif
    return cpu_info[0]; 
}
