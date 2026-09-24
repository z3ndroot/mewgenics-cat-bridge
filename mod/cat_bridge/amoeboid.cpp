#include "amoeboid.hpp"
#include "utilities/checksum.hpp"
#include "utilities/debug_console.hpp"
#include "utilities/function_hook.hpp"
#include "utilities/memory.hpp"
#include "utilities/strings.hpp"
#include "utilities/portal.hpp"
#include "utilities/stopwatch.hpp"
#include "utilities/pe_view.hpp"

#include <windows.h>

#include "detours.h"

// Amoeboid
//
// A base DLL implementation for Mewgenics modding.
//
// polymeric 2026

GlobalContext G;

// Defined in cat_bridge.cpp
void cat_bridge_on_attach();
void cat_bridge_on_detach();

enum class AmoeboidErrorCode {
    Success,
    HashMismatch,
    FailedToResolveSymbol,
    FailedToHook,
    FailedToUnhook,
};

std::string get_user_facing_error_message(AmoeboidErrorCode error_code) {
    std::string builder;
    switch(error_code) {
        case AmoeboidErrorCode::Success:
            builder += "A supposedly impossible error occurred where something failed successfully.\n";
            builder += "(but seriously, the author of this mod probably did something very silly if you are seeing this message)\n";
            break;
        case AmoeboidErrorCode::HashMismatch:
            builder += std::format("{} is not compatible with the version of Mewgenics on your computer. Mewgenics version {} is expected.\n", MOD_NAME, EXE_VERSION);
            break;
        case AmoeboidErrorCode::FailedToResolveSymbol:
            builder += std::format("A function/data symbol failed to resolve.\n");
            break;
        case AmoeboidErrorCode::FailedToHook:
            builder += std::format("A function hook failed to install.\n");
            break;
        case AmoeboidErrorCode::FailedToUnhook:
            builder += std::format("A function hook failed to uninstall.\n");
            break;
    }

    builder += std::format("\nPlease check for a mod update or report an issue at:\n{}\n\n", MOD_URL);

    builder += std::format("Additional information:\n");
    builder += std::format("Mod: {} version {}\n", MOD_NAME, MOD_VERSION);
    builder += std::format("Mewgenics.exe expected version: {}\n", EXE_VERSION);
    builder += std::format("Mewgenics.exe expected SHA-256: {}\n", hash256bit_to_string(EXE_SHA256));
    builder += std::format("Mewgenics.exe actual SHA-256: {}", G.exe_actual_sha256.has_value() ? hash256bit_to_string(G.exe_actual_sha256.value()) : "<unknown>");

    return builder;
}

AmoeboidErrorCode on_attach() {
    // Fetch the Mewjector API if it is present
    MJ_SUPPORT_InitAPI(MOD_IDENTIFIER);

    // Actual virtual address where mapped executable begins
    HMODULE host_exec_module = GetModuleHandle(NULL);
    uintptr_t host_exec_base_va = reinterpret_cast<uintptr_t>(host_exec_module);
    size_t host_exec_image_size = get_pe_image_mapped_size(host_exec_module);
    G.host_exec_base_va = host_exec_base_va;
    G.host_exec_image_size = host_exec_image_size;

    // Map the raw executable for hashing and signature scanning
    PeView host_exec_pe_view;
    host_exec_pe_view.open(get_module_file_path(NULL));

    // Create a Win32 console window with which to print log messages. ENABLE_CONSOLE_LOGGING disables this for public release.
    ALLOC_CONSOLE();

    // Calculate the SHA-256 digest of the executable
    // If the act of calculating the hash fails, continue optimistically
    {
        MAKE_STOPWATCH_SCOPE(sct, "sha256 calculation");
        if(host_exec_pe_view.is_opened()) {
            G.exe_actual_sha256 = sha256_span(host_exec_pe_view.get_file_span());
            G.exe_hash_mismatch_detected = (G.exe_actual_sha256.value() != EXE_SHA256);
        }
    }

    D::info("Initializing {} version {}", MOD_NAME, MOD_VERSION);
    // D::info("Hook base VA: 0x{:x}", G.dll_base_va);
    // D::info("Hook mapped size: {}\n", G.dll_image_size);
    // D::info("Executable base VA: 0x{:x}", host_exec_base_va);
    // D::info("Executable mapped size: {}\n", host_exec_image_size);
    // D::info("Executable SHA-256: {}", G.exe_actual_sha256.has_value() ? hash256bit_to_string(G.exe_actual_sha256.value()) : "<unknown>");

    // Do not install any hooks if a hash mismatch was detected.
    // Instead exit this function. The DLL will be loaded, but it will effectively be inactive.
    if(G.exe_hash_mismatch_detected) {
        return AmoeboidErrorCode::HashMismatch;
    }

    // Resolve symbols (calculate VAs, do proc lookups, do signature scans)
    {
        MAKE_STOPWATCH_SCOPE(sct, "symbol resolution");
        // Resolve portals (trampolines to functions and data)
        if(!SPortalRegistry::resolve_portals(host_exec_base_va, host_exec_pe_view)) {
            return AmoeboidErrorCode::FailedToResolveSymbol;
        }
        // Resolve function hook targets
        if(!SFunctionHookRegistry::resolve_hooks(host_exec_base_va, host_exec_pe_view, 0)) {
            return AmoeboidErrorCode::FailedToResolveSymbol;
        }
    }

    // Try to install function hooks
    {
        MAKE_STOPWATCH_SCOPE(sct, "function hook installation");
        G.dll_can_self_eject = true;
        if(SFunctionHookRegistry::api_is_present(EFunctionHookProvider::Mewjector)) {
            // Use Mewjector if present for coordinated hooking
            if(!SFunctionHookRegistry::install_hooks(EFunctionHookProvider::Mewjector, 0)) {
                return AmoeboidErrorCode::FailedToHook;
            }
            G.dll_can_self_eject = false;
        } else {
            if(!SFunctionHookRegistry::install_hooks(EFunctionHookProvider::Detours, 0)) {
                return AmoeboidErrorCode::FailedToHook;
            }
        }
    }

    // Start the named pipe server now that hooks are in place.
    cat_bridge_on_attach();

    return AmoeboidErrorCode::Success;
}

AmoeboidErrorCode on_unload_detach() {
    D::info("Uninitializing {} (Unload)", MOD_NAME);
    cat_bridge_on_detach();
    // Try to gracefully remove our hooks if this dll was ejected by an external tool (e.g. via System Informer).
    if(!SFunctionHookRegistry::uninstall_hooks_all(true)) {
        return AmoeboidErrorCode::FailedToUnhook;
    }
    return AmoeboidErrorCode::Success;
}

AmoeboidErrorCode on_exitprocess_detach() {
    D::info("Uninitializing {} (ExitProcess)", MOD_NAME);
    cat_bridge_on_detach();
    // May as well clean up resources properly, even if the process is going down.
    // Remove hooks, but don't fail if the API is unable to uninstall hooks.
    if(!SFunctionHookRegistry::uninstall_hooks_all(false)) {
        return AmoeboidErrorCode::FailedToUnhook;
    }
    return AmoeboidErrorCode::Success;
}

void on_error(bool is_detach, AmoeboidErrorCode error_code) {
    std::string user_facing_error_message = get_user_facing_error_message(error_code);
    if(is_detach) {
        D::error("An unrecoverable error occurred during uninitialization.");
        D::error("{}", user_facing_error_message);
        // don't pop up an error message on uninit
    } else {
        D::error("An unrecoverable error occurred during initialization.");
        D::error("{}", user_facing_error_message);

        // would be good to flash a user-visible message, but an exclusive fullscreen window will aggressively fight to be on top and obscure the error message
        // std::wstring caption_wstring = std::format(L"Error - {}", convert_utf8_string_to_utf16_wstring(MOD_NAME));
        // std::wstring user_facing_error_message_wstring = convert_utf8_string_to_utf16_wstring(user_facing_error_message);
        // MessageBoxW(NULL, user_facing_error_message_wstring.c_str(), caption_wstring.c_str(), MB_OK | MB_ICONERROR | MB_TOPMOST);
    }
}

void final_rites() {
    FREE_CONSOLE();
}

BOOL WINAPI DllMain(
    HINSTANCE hinstDLL,  // handle to DLL module
    DWORD fdwReason,     // reason for calling function
    LPVOID lpReserved    // reserved
) {
    if(DetourIsHelperProcess()) {
        return TRUE;
    }

    AmoeboidErrorCode error_code = AmoeboidErrorCode::Success;

    // Perform actions based on the reason for calling.
    switch(fdwReason) {
        case DLL_PROCESS_ATTACH:
            // Initialize once for each new process.
            // Return FALSE to fail DLL load.
            DetourRestoreAfterWith();

            G.dll_base_va = reinterpret_cast<uintptr_t>(hinstDLL);
            G.dll_image_size = get_pe_image_mapped_size(hinstDLL);

            error_code = on_attach();

            break;

        case DLL_THREAD_ATTACH:
            // Do thread-specific initialization.
            break;

        case DLL_THREAD_DETACH:
            // Do thread-specific cleanup.
            break;

        case DLL_PROCESS_DETACH:
            // Perform any necessary cleanup.
            if(lpReserved == NULL) {
                // traversed when the dll is ejected
                error_code = on_unload_detach();
            } else {
                // traversed when Mewgenics is exiting
                error_code = on_exitprocess_detach();
            }
            break;
    }

    if(error_code != AmoeboidErrorCode::Success) {
        on_error(fdwReason == DLL_PROCESS_DETACH, error_code);
    }

    if(fdwReason == DLL_PROCESS_DETACH) {
        final_rites();
    }

    // Successful DLL_PROCESS_ATTACH.
    return TRUE;
}
