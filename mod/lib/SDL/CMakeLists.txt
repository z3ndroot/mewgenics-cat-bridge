# just the headers from SDL3 (nothing else to avoid invoking expensive configure routines)
project(SDL3)

# Set path anchors
set(SDL3_DIR third_party)

# the following is excerpted from SDL's CMakeLists

# List build targets
add_library(SDL3_Headers INTERFACE)
add_library(SDL3::Headers ALIAS SDL3_Headers)
set_property(TARGET SDL3_Headers PROPERTY EXPORT_NAME "Headers")

# List include directories
target_include_directories(SDL3_Headers INTERFACE
    "${SDL3_DIR}"
)
