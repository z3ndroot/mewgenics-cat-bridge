#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>

// Virtual memory utilities
//
// polymeric 2026

// Performs a potentially unsound read behind an address, without risk of triggering a fault.
// "judgment-free read" or, "just f'ing read"
// template<class T>
// bool jf_read(const void *addr, T *buf) {
//     return ReadProcessMemory(GetCurrentProcess(), addr, buf, sizeof(T), NULL);
// }

// Read size can be returned in case the user is interested in knowing if the read managed to scrape some data before
// faulting at a page boundary.
// template<class T>
// size_t jf_readn(const void *addr, T *buf) {
//     size_t bytes_read = 0;
//     ReadProcessMemory(GetCurrentProcess(), addr, buf, sizeof(T), &bytes_read);
//     return bytes_read;
// }

// Gets the base pointer for a thread-local-storage slot
inline uint8_t *get_tls_base(DWORD slot) {
    return reinterpret_cast<uint8_t **>(__readgsqword(0x58))[slot];
}

// Allocate onto the host process' heap
inline void *host_alloc(size_t size) {
    return HeapAlloc(GetProcessHeap(), 0, size);
}

// Free from the host process' heap
inline void host_free(void *ptr) {
    HeapFree(GetProcessHeap(), 0, ptr);
}

// Reallocate within the host process' heap
inline void *host_realloc(void *ptr, size_t size) {
    // based off MSVC's internal realloc (which conditionally dispatches to HeapAlloc or HeapReAlloc)
    if(ptr == nullptr) {
        return HeapAlloc(GetProcessHeap(), 0, size);
    } else {
        return HeapReAlloc(GetProcessHeap(), 0, ptr, size);
    }
}

// Get the size of a mapped image
inline size_t get_pe_image_mapped_size(HMODULE module) {
    auto base = reinterpret_cast<uintptr_t>(module);

    IMAGE_DOS_HEADER *dos_header = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    IMAGE_NT_HEADERS *nt_headers = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos_header->e_lfanew);

    return nt_headers->OptionalHeader.SizeOfImage;
}
