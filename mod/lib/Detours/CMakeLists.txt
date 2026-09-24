project(Detours)

# Set path anchors
set(DETOURS_DIR third_party)

# List build targets
add_library(${PROJECT_NAME} STATIC
    ${DETOURS_DIR}/detours.cpp
    ${DETOURS_DIR}/modules.cpp
    ${DETOURS_DIR}/disasm.cpp
    ${DETOURS_DIR}/creatwth.cpp
)

# List include directories
target_include_directories(${PROJECT_NAME} PUBLIC
    ${DETOURS_DIR}
)

# Set language modes
set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Set warnings
if(MSVC)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        # not clean with Clang in MSVC mode, suppress
        target_compile_options(${PROJECT_NAME} PRIVATE /W0)
    else()
        target_compile_options(${PROJECT_NAME} PRIVATE /W4)
    endif()
elseif(MINGW)
    # not clean with GCC/Clang in GCC mode, suppress
    target_compile_options(${PROJECT_NAME} PRIVATE -w)
endif()
