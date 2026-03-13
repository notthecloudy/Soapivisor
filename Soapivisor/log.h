#ifndef Soapivisor_LOG_H_
#define Soapivisor_LOG_H_

#include "common.h"

// For stealth requirements, we completely suppress all logging in release builds.
// In debug builds, you may hook these into UEFI Print if desired.

#if defined(DEBUG)
#define Soapivisor_LOG_DEBUG(format, ...) 
#define Soapivisor_LOG_INFO(format, ...) 
#define Soapivisor_LOG_WARN(format, ...)
#define Soapivisor_LOG_ERROR(format, ...) 
#define Soapivisor_LOG_DEBUG_SAFE(format, ...) 
#define Soapivisor_LOG_INFO_SAFE(format, ...) 
#define Soapivisor_LOG_WARN_SAFE(format, ...) 
#define Soapivisor_LOG_ERROR_SAFE(format, ...) 
#else
#define Soapivisor_LOG_DEBUG(format, ...) 
#define Soapivisor_LOG_INFO(format, ...) 
#define Soapivisor_LOG_WARN(format, ...) 
#define Soapivisor_LOG_ERROR(format, ...) 
#define Soapivisor_LOG_DEBUG_SAFE(format, ...) 
#define Soapivisor_LOG_INFO_SAFE(format, ...) 
#define Soapivisor_LOG_WARN_SAFE(format, ...) 
#define Soapivisor_LOG_ERROR_SAFE(format, ...) 
#endif

extern "C" {
}

#endif  // Soapivisor_LOG_H_
