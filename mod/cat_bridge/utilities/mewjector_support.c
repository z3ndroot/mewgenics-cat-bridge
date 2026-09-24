#include "utilities/mewjector_support.h"

#include <string.h>

#include <windows.h>

// Provides support functions for interacting with Mewjector.
//
// polymeric 2026

// A C file? In a C++ codebase!?
// More likely than you think!

static MewjectorAPI s_mj_api;
static char s_owner[64] = {0};
static int s_mj_api_present = 0;
static INIT_ONCE s_mj_resolve_once_guard = INIT_ONCE_STATIC_INIT;

typedef struct {
    const char *owner;
} MJ_SUPPORT_PRIVATE_Resolve_Parameter;

BOOL CALLBACK MJ_SUPPORT_PRIVATE_Resolve(PINIT_ONCE InitOnce, PVOID Parameter, PVOID *lpContext) {
    (void)InitOnce;
    (void)lpContext;
    MJ_SUPPORT_PRIVATE_Resolve_Parameter *p_params = (MJ_SUPPORT_PRIVATE_Resolve_Parameter *)Parameter;
    // 1. If MJ_Require fails, set s_mj_api_present to 0 and return.
    // 2. If MJ_Resolve fails, set s_mj_api_present to 0 and return.
    // 3. Otherwise &s_mj_api will be populated. Set s_mj_api_present to 1.
    s_mj_api_present = (MJ_Require(p_params->owner) != 0 && MJ_Resolve(&s_mj_api) != 0);
    // If s_mj_api_present is set, also copy the given owner string for later retrieval.
    if(s_mj_api_present) {
        strncpy_s(s_owner, sizeof(s_owner), p_params->owner, sizeof(s_owner) - 1);
    }

    return TRUE;
}

int MJ_SUPPORT_InitAPI(const char *owner) {
    MJ_SUPPORT_PRIVATE_Resolve_Parameter params = {
        .owner = owner,
    };
    InitOnceExecuteOnce(&s_mj_resolve_once_guard, &MJ_SUPPORT_PRIVATE_Resolve, &params, NULL);

    return s_mj_api_present;
}

const MewjectorAPI *MJ_SUPPORT_GetAPI(void) {
    return s_mj_api_present ? &s_mj_api : NULL;
}

const char *MJ_SUPPORT_GetOwner(void) {
    return s_owner;
}
