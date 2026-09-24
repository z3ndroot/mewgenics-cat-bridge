#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type" // GCC/ClangCL warn on GetProcAddress FP casts
#endif
#include "mewjector.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

// Provides support functions for interacting with Mewjector.
//
// polymeric 2026

#ifdef __cplusplus
extern "C" {
#endif

// Initializes a cached instance of the Mewjector API struct.
int MJ_SUPPORT_InitAPI(const char *owner);

// Gets the cached instance of the Mewjector API struct resolved by MJ_SUPPORT_InitAPI().
const MewjectorAPI *MJ_SUPPORT_GetAPI(void);

// Gets the owner string that was passed to MJ_SUPPORT_InitAPI().
const char *MJ_SUPPORT_GetOwner(void);

#ifdef __cplusplus
}
#endif
