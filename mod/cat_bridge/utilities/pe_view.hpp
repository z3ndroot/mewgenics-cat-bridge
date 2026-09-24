#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <filesystem>
#include <span>

// Simple PE file view
//
// polymeric 2026

class PeView {
public:
    PeView();
    ~PeView();
    PeView(const PeView &other) = delete;
    PeView &operator=(const PeView &other) = delete;
    PeView(PeView &&other);
    PeView &operator=(PeView &&other);

    void open(std::filesystem::path path);
    void open(const uint8_t *data, size_t size);
    void close();

    bool is_opened() const;

    std::span<const uint8_t> get_file_span() const;
    // may want to map the file like a PE loader, but not a problem for sigscanning most images
    // std::span<uint8_t> get_loaded_span() const;
    std::optional<uintptr_t> file_offset_to_rva(uintptr_t offset) const;

private:
    using HANDLE = void *;
    enum class State {
        Closed,
        MMapFile,
        ExternalBuffer,
    };
    State state;
    std::filesystem::path file_path;
    HANDLE file_handle;
    HANDLE mmap_handle;
    const uint8_t *mmap_view;
    size_t file_size;

    bool post_open_bounds_check();
};
