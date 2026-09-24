#pragma once

// Support function hooking via MinHook
// #define SUPPORT_MINHOOK_HOOK_IMPL
// Support function hooking via Detours
#define SUPPORT_DETOURS_HOOK_IMPL
// Support function hooking via Mewjector
#define SUPPORT_MEWJECTOR_HOOK_IMPL

// Use Detours' disassembler to measure stolenBytes for Mewjector
#define COMPAT_MEWJECTOR_MEASURE_STOLENBYTES

#include "utilities/pe_view.hpp"

#include <cstddef>
#include <cstdint>
#include <bit>
#include <optional>
#include <unordered_map>
#include <vector>

#include <windows.h>

#ifdef SUPPORT_MINHOOK_HOOK_IMPL
#include "MinHook.h"
#endif
#ifdef SUPPORT_DETOURS_HOOK_IMPL
#include "detours.h"
#endif
#ifdef SUPPORT_MEWJECTOR_HOOK_IMPL
#include "utilities/mewjector_support.h"
#endif

// Wraps much of the boilerplate and declarations required
// to hook a function with MinHook, Detours, or Mewjector.
//
// polymeric 2026

// Makes a hook that will be managed by FunctionHookRegistry
#define MAKE_HOOK(group, address, ret_type, call_conv, name, ...) \
    static ret_type call_conv name##_detour(__VA_ARGS__); \
    static RvaFunctionHookDescriptor<ret_type (call_conv *)(__VA_ARGS__), true, group> name##_hook(address, &name##_detour); \
    static ret_type call_conv name##_detour(__VA_ARGS__)

#define MAKE_VHOOK(group, ret_type, call_conv, name, ...) \
    static ret_type call_conv name##_detour(__VA_ARGS__); \
    static VaFunctionHookDescriptor<ret_type (call_conv *)(__VA_ARGS__), true, group> name##_hook(&name, &name##_detour); \
    static ret_type call_conv name##_detour(__VA_ARGS__)

#define MAKE_PHOOK(group, lp_proc_name, ret_type, call_conv, name, ...) \
    static ret_type call_conv name##_detour(__VA_ARGS__); \
    static ProcFunctionHookDescriptor<ret_type (call_conv *)(__VA_ARGS__), true, group> name##_hook(lp_proc_name, &name##_detour); \
    static ret_type call_conv name##_detour(__VA_ARGS__)

#define MAKE_SHOOK(group, sig, ret_type, call_conv, name, ...) \
    static ret_type call_conv name##_detour(__VA_ARGS__); \
    static SigFunctionHookDescriptor<ret_type (call_conv *)(__VA_ARGS__), true, group, decltype(sig)> name##_hook(sig, &name##_detour); \
    static ret_type call_conv name##_detour(__VA_ARGS__)

enum class EFunctionHookProvider {
    Uninstalled,
    MinHook,
    Detours,
    Mewjector,
};

class IFunctionHookDescriptor {
public:
    virtual bool resolve(uintptr_t offset, size_t size) = 0;
    virtual bool resolve(uintptr_t offset, PeView &pe_view) = 0;
    virtual bool install(EFunctionHookProvider api_provider) = 0;
    virtual bool uninstall(EFunctionHookProvider api_provider) = 0;
};

class FunctionHookRegistryIndex {
public:
    std::vector<IFunctionHookDescriptor*> hook_descriptors;
    EFunctionHookProvider provider = EFunctionHookProvider::Uninstalled;
};

class SFunctionHookRegistry {
public:
    // Instances of FunctionHookDescriptor whose classes were templated with RegisterMe==true
    // are pushed into this registry during static init.
    static std::unordered_map<int, FunctionHookRegistryIndex>& get_registries() {
        static std::unordered_map<int, FunctionHookRegistryIndex> registries;
        return registries;
    }

    static FunctionHookRegistryIndex& get_registry(int group) {
        return SFunctionHookRegistry::get_registries()[group];
    }

    #ifdef SUPPORT_MINHOOK_HOOK_IMPL
    // TODO this really should be placed behind a global per-provider object
    static int& get_minhook_init_count() {
        static int count;
        return count;
    }
    #endif

    static bool api_is_present(EFunctionHookProvider api_provider) {
        switch(api_provider) {
            #ifdef SUPPORT_MINHOOK_HOOK_IMPL
            case EFunctionHookProvider::MinHook:
                return true;
            #endif
            #ifdef SUPPORT_DETOURS_HOOK_IMPL
            case EFunctionHookProvider::Detours:
                return true;
            #endif
            #ifdef SUPPORT_MEWJECTOR_HOOK_IMPL
            case EFunctionHookProvider::Mewjector:
                if(MJ_SUPPORT_GetAPI() == NULL) {
                    return false;
                } else {
                    return true;
                }
            #endif
            default:
                // invalid enum level or provider is unsupported
                // or provider is EFunctionHookProvider::Uninstalled
                return false;
        }
    }

    static bool resolve_hooks(uintptr_t host_exec_base_va, size_t host_exec_image_size, int group) {
        FunctionHookRegistryIndex &registry = SFunctionHookRegistry::get_registry(group);
        bool success = true;
        for(auto hook : registry.hook_descriptors) {
            if (!hook->resolve(host_exec_base_va, host_exec_image_size)) {
                success = false;
            }
        }
        return success;
    }

    static bool resolve_hooks(uintptr_t host_exec_base_va, PeView &pe_view, int group) {
        FunctionHookRegistryIndex &registry = SFunctionHookRegistry::get_registry(group);
        bool success = true;
        for(auto hook : registry.hook_descriptors) {
            if (!hook->resolve(host_exec_base_va, pe_view)) {
                success = false;
            }
        }
        return success;
    }

    static bool install_hooks(EFunctionHookProvider api_provider, int group) {
        // SFunctionHookRegistry will generally:
        // - check if the API is present if necessary
        // - call any global init functions if required (SFunctionHookRegistry assumes it owns global init/deinit routines)
        // - start a transaction if the API is transaction-based
        // - install all hooks in the given group
        // - end the transaction if the API is transaction-based

        FunctionHookRegistryIndex &registry = SFunctionHookRegistry::get_registry(group);

        // not reasonable for the user to attempt to double-install hooks
        if(registry.provider != EFunctionHookProvider::Uninstalled) {
            return false;
        }

        switch(api_provider) {
            #ifdef SUPPORT_MINHOOK_HOOK_IMPL
            case EFunctionHookProvider::MinHook:
                SFunctionHookRegistry::get_minhook_init_count()++;
                if(SFunctionHookRegistry::get_minhook_init_count() == 1) {
                    if(MH_Initialize() != MH_OK) {
                        return false;
                    }
                }
                for(auto hook : registry.hook_descriptors) {
                    if (!hook->install(api_provider)) {
                        return false;
                    }
                }
                break;
            #endif
            #ifdef SUPPORT_DETOURS_HOOK_IMPL
            case EFunctionHookProvider::Detours:
                if(DetourTransactionBegin() != NO_ERROR) {
                    return false;
                }
                if(DetourUpdateThread(GetCurrentThread()) != NO_ERROR) {
                    return false;
                }
                for(auto hook : registry.hook_descriptors) {
                    if (!hook->install(api_provider)) {
                        return false;
                    }
                }
                if(DetourTransactionCommit() != NO_ERROR) {
                    return false;
                }
                break;
            #endif
            #ifdef SUPPORT_MEWJECTOR_HOOK_IMPL
            case EFunctionHookProvider::Mewjector:
                if(MJ_SUPPORT_GetAPI() == NULL) {
                    return false;
                }
                for(auto hook : registry.hook_descriptors) {
                    if (!hook->install(api_provider)) {
                        return false;
                    }
                }
                break;
            #endif
            default:
                // invalid enum level or provider is unsupported
                // or provider is EFunctionHookProvider::Uninstalled
                return false;
                break;
        }

        registry.provider = api_provider;
        return true;
    }

    static bool uninstall_hooks(int group, bool lights_on_uninstall) {
        FunctionHookRegistryIndex &registry = SFunctionHookRegistry::get_registry(group);

        switch(registry.provider) {
            case EFunctionHookProvider::Uninstalled:
                // do nothing when we know we have nothing to uninstall (That Was Easy (TM))
                // this indirectly bypasses issuing zero transactions to Detours
                // unlike double-installing, double-uninstalling is expected with dll eject
                break;
            #ifdef SUPPORT_MINHOOK_HOOK_IMPL
            case EFunctionHookProvider::MinHook:
                for(auto hook: registry.hook_descriptors) {
                    if (!hook->uninstall(registry.provider)) {
                        return false;
                    }
                }
                if(SFunctionHookRegistry::get_minhook_init_count() == 0) {
                    // underflow!
                    return false;
                } else if (SFunctionHookRegistry::get_minhook_init_count() == 1) {
                    SFunctionHookRegistry::get_minhook_init_count()--;
                    if(MH_Uninitialize() != MH_OK) {
                        return false;
                    }
                } else {
                    SFunctionHookRegistry::get_minhook_init_count()--;
                }
                break;
            #endif
            #ifdef SUPPORT_DETOURS_HOOK_IMPL
            case EFunctionHookProvider::Detours:
                // NB Detours will return a failure code if zero items are queued in a transaction
                if(DetourTransactionBegin() != NO_ERROR) {
                    return false;
                }

                if(DetourUpdateThread(GetCurrentThread()) != NO_ERROR) {
                    return false;
                }

                for(auto hook: registry.hook_descriptors) {
                    if (!hook->uninstall(registry.provider)) {
                        return false;
                    }
                }

                if(DetourTransactionCommit() != NO_ERROR) {
                    return false;
                }
                break;
            #endif
            #ifdef SUPPORT_MEWJECTOR_HOOK_IMPL
            case EFunctionHookProvider::Mewjector:
                // MJ does not support function hook uninstallation
                if(lights_on_uninstall) {
                    // matters as we could be dll ejecting
                    return false;
                } else {
                    // does not matter as process is going down
                    return true;
                }
                break;
            #endif
            default:
                // invalid enum level or provider is missing
                return false;
                break;
        }

        registry.provider = EFunctionHookProvider::Uninstalled;
        return true;
    }

    static bool uninstall_hooks_all(bool lights_on_uninstall) {
        for(auto &registry : SFunctionHookRegistry::get_registries()) {
            if(!uninstall_hooks(registry.first, lights_on_uninstall)) {
                return false;
            }
        }
        return true;
    }
};

template<typename FP, bool RegisterMe, int Group>
class BFunctionHookDescriptor : IFunctionHookDescriptor {
public:
    // VA of the targeted function. This is the function's relocated address seen in this program instance.
    FP target;
    // VA of the detour function. This FP references the function we declared with MAKE_HOOK.
    const FP detour;
    // VA of the trampoline function. The detour function calls this FP to execute the targeted function's original implementation.
    FP orig;

    BFunctionHookDescriptor(FP detour) :
        target(nullptr), detour(detour), orig(nullptr)
    {
        if constexpr(RegisterMe) {
            SFunctionHookRegistry::get_registry(Group).hook_descriptors.push_back(this);
        }
    }

    virtual FP calculate_target(uintptr_t offset, size_t size) = 0;
    virtual FP calculate_target(uintptr_t offset, PeView &pe_view) = 0;

    bool resolve(uintptr_t offset, size_t size) override {
        this->target = this->calculate_target(offset, size);
        if(this->target == nullptr) {
            // e.g. if GetProcAddress were to fail
            return false;
        }
        return true;
    }

    bool resolve(uintptr_t offset, PeView &pe_view) override {
        this->target = this->calculate_target(offset, pe_view);
        if(this->target == nullptr) {
            // e.g. if GetProcAddress were to fail
            return false;
        }
        return true;
    }

    bool install(EFunctionHookProvider api_provider) override {
        if(this->target == nullptr) {
            // e.g. if GetProcAddress were to fail
            return false;
        }
        switch(api_provider) {
            #ifdef SUPPORT_MINHOOK_HOOK_IMPL
            case EFunctionHookProvider::MinHook:
                if(MH_CreateHook(reinterpret_cast<LPVOID>(this->target), reinterpret_cast<LPVOID>(this->detour), reinterpret_cast<LPVOID *>(&this->orig)) != MH_OK) {
                    // ppOriginal is only written if hooking succeeded
                    return false;
                }
                if(MH_EnableHook(reinterpret_cast<LPVOID>(this->target)) != MH_OK) {
                    return false;
                }
                break;
            #endif
            #ifdef SUPPORT_DETOURS_HOOK_IMPL
            case EFunctionHookProvider::Detours:
                this->orig = this->target;
                if (DetourAttach(reinterpret_cast<PVOID *>(&this->orig), reinterpret_cast<PVOID>(this->detour)) != NO_ERROR) {
                    return false;
                }
                break;
            #endif
            #ifdef SUPPORT_MEWJECTOR_HOOK_IMPL
            case EFunctionHookProvider::Mewjector:
                if(const MewjectorAPI *mj = MJ_SUPPORT_GetAPI(); mj != NULL) {
                    #ifdef COMPAT_MEWJECTOR_MEASURE_STOLENBYTES
                    // Versions of Mewjector prior to v3.1 required an externally calculated value for stolenBytes
                    const size_t MIN_BYTES_TO_STEAL = 14;
                    uintptr_t target_start = reinterpret_cast<uintptr_t>(this->target);
                    uintptr_t target_end_of_stolen_region = target_start;
                    while(target_end_of_stolen_region - target_start < MIN_BYTES_TO_STEAL) {
                        PVOID target_end_of_stolen_region_void = DetourCopyInstruction(NULL, NULL, reinterpret_cast<PVOID>(target_end_of_stolen_region), NULL, NULL);
                        target_end_of_stolen_region = reinterpret_cast<uintptr_t>(target_end_of_stolen_region_void);
                    }
                    int bytes_to_steal = static_cast<int>(target_end_of_stolen_region - target_start);
                    #else
                    int bytes_to_steal = 0;
                    #endif
                    if(mj->InstallHook(
                        reinterpret_cast<UINT_PTR>(this->target) - reinterpret_cast<UINT_PTR>(GetModuleHandle(NULL)),
                        bytes_to_steal,
                        reinterpret_cast<void *>(this->detour),
                        reinterpret_cast<void **>(&this->orig),
                        10,
                        MJ_SUPPORT_GetOwner()
                    ) == 0) {
                        return false;
                    }
                } else {
                    return false;
                }
                break;
            #endif
            default:
                // invalid enum level or EFunctionHookProvider::Uninstalled
                return false;
                break;
        }

        return true;
    }

    bool uninstall(EFunctionHookProvider api_provider) override {
        switch(api_provider) {
            #ifdef SUPPORT_MINHOOK_HOOK_IMPL
            case EFunctionHookProvider::MinHook:
                // MH has a queue API to allow it to operate similarly to Detours transactions, but we don't use it
                // as it complicates uninstall implementation
                // Disabling is covered by Remove
                if(MH_RemoveHook(reinterpret_cast<LPVOID>(this->target)) != MH_OK) {
                    return false;
                }
                break;
            #endif
            #ifdef SUPPORT_DETOURS_HOOK_IMPL
            case EFunctionHookProvider::Detours:
                // &this->orig is captured and isn't written back until DetourTransactionCommit... very evil
                if (DetourDetach(reinterpret_cast<PVOID *>(&this->orig), reinterpret_cast<PVOID>(this->detour)) != NO_ERROR) {
                    return false;
                }
                break;
            #endif
            #ifdef SUPPORT_MEWJECTOR_HOOK_IMPL
            case EFunctionHookProvider::Mewjector:
                // MJ does not support function hook uninstallation
                return false;
                break;
            #endif
            default:
                // invalid enum level or EFunctionHookProvider::Uninstalled
                return false;
                break;
        }

        return true;
    }
};

// NB For the case of hooking imports, ProcFunctionHookDescriptor should be used.
// Directly using an imported symbol ref from this program may point to a jump
// instruction in the dll's space instead of the host exe.
template<typename FP, bool RegisterMe, int Group>
class VaFunctionHookDescriptor : public BFunctionHookDescriptor<FP, RegisterMe, Group> {
public:
    VaFunctionHookDescriptor(FP target, FP detour) :
        BFunctionHookDescriptor<FP, RegisterMe, Group>(detour)
    {
        this->target = target;
    }

    FP calculate_target(uintptr_t offset, size_t size) override {
        (void)offset;
        (void)size;
        return this->target;
    }

    FP calculate_target(uintptr_t offset, PeView &pe_view) override {
        (void)offset;
        (void)pe_view;
        return this->target;
    }
};

template<typename FP, bool RegisterMe, int Group>
class RvaFunctionHookDescriptor : public BFunctionHookDescriptor<FP, RegisterMe, Group> {
public:
    // Relative VA of the targeted function. This is the function's VA not including any mapping offsets.
    const uintptr_t target_canonical;

    RvaFunctionHookDescriptor(uintptr_t target_canonical, FP detour) :
        BFunctionHookDescriptor<FP, RegisterMe, Group>(detour), target_canonical(target_canonical)
    {}

    FP calculate_target(uintptr_t offset, size_t size) override {
        (void)size;
        return reinterpret_cast<FP>(this->target_canonical + offset);
    }

    FP calculate_target(uintptr_t offset, PeView &pe_view) override {
        (void)pe_view;
        return reinterpret_cast<FP>(this->target_canonical + offset);
    }
};

template<typename FP, bool RegisterMe, int Group>
class ProcFunctionHookDescriptor : public BFunctionHookDescriptor<FP, RegisterMe, Group> {
public:
    // Export name or ordinal of the targeted function. This is the function's canonical identifier in its exporter's export table.
    const LPCSTR lp_proc_name;

    ProcFunctionHookDescriptor(LPCSTR lp_proc_name, FP detour) :
        BFunctionHookDescriptor<FP, RegisterMe, Group>(detour), lp_proc_name(lp_proc_name)
    {}

    FP calculate_target(uintptr_t offset, size_t size) override {
        (void)size;
        // offset is an HMODULE retrieved with GetModuleHandle(NULL) outside this function
        // can potentially perform cross-dll hooking by storing a wide string module name too
        return std::bit_cast<FP>(GetProcAddress(reinterpret_cast<HMODULE>(offset), this->lp_proc_name));
    }

    FP calculate_target(uintptr_t offset, PeView &pe_view) override {
        (void)pe_view;
        // offset is an HMODULE retrieved with GetModuleHandle(NULL) outside this function
        // can potentially perform cross-dll hooking by storing a wide string module name too
        return std::bit_cast<FP>(GetProcAddress(reinterpret_cast<HMODULE>(offset), this->lp_proc_name));
    }
};

template<typename FP, bool RegisterMe, int Group, typename SigClass>
class SigFunctionHookDescriptor : public BFunctionHookDescriptor<FP, RegisterMe, Group> {
public:
    // Signature descriptor of the targeted function.
    const SigClass sig;

    SigFunctionHookDescriptor(SigClass sig, FP detour) :
        BFunctionHookDescriptor<FP, RegisterMe, Group>(detour), sig(sig)
    {}

    FP calculate_target(uintptr_t offset, size_t size) override {
        uint8_t *result = const_cast<uint8_t *>(
            sig.find_unique_match_or_none(reinterpret_cast<const uint8_t *>(offset), size, [](const uint8_t *addr) -> const uint8_t * {
                return addr;
            })
        );
        return reinterpret_cast<FP>(result);
    }

    FP calculate_target(uintptr_t offset, PeView &pe_view) override {
        if(pe_view.is_opened()) {
            std::span<const uint8_t> span = pe_view.get_file_span();
            const uint8_t *span_data = span.data();
            size_t span_size = span.size();

            uint8_t *result = const_cast<uint8_t *>(
                sig.find_unique_match_or_none(span_data, span_size, [&](const uint8_t *addr) -> const uint8_t * {
                    if(addr == nullptr) {
                        return nullptr;
                    }
                    std::optional<uintptr_t> rva = pe_view.file_offset_to_rva(addr - span_data);
                    if(rva.has_value()) {
                        return reinterpret_cast<const uint8_t *>(rva.value() + offset);
                    }
                    return nullptr;
                })
            );

            return reinterpret_cast<FP>(result);
        }
        return nullptr;
    }
};

#undef SUPPORT_MINHOOK_HOOK_IMPL
#undef SUPPORT_DETOURS_HOOK_IMPL
#undef SUPPORT_MEWJECTOR_HOOK_IMPL
#undef COMPAT_MEWJECTOR_MEASURE_STOLENBYTES
