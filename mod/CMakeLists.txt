cmake_minimum_required(VERSION 3.15)

project(mewgenics_mcp_bridge_root)

# Set global compile options
if(MSVC)
    # append "-- /p:CL_MPcount=<n>" to "cmake --build" to control TU-level parallelism
    add_compile_options(/MP)
endif()

# Read third-party library CMakeLists (vendored from the randomize_item_picks
# reference mod: https://github.com/p0lymeric/mewgenics_randomize_item_picks , MIT licensed)
add_subdirectory(lib/Detours EXCLUDE_FROM_ALL)
add_subdirectory(lib/libtomcrypt EXCLUDE_FROM_ALL)
add_subdirectory(lib/mewjector EXCLUDE_FROM_ALL)
add_subdirectory(lib/SDL EXCLUDE_FROM_ALL)

# Read our CMakeLists
add_subdirectory(cat_bridge)
