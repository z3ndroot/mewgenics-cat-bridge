project(cat_bridge)

# List build targets
add_library(${PROJECT_NAME} SHARED
    amoeboid.cpp
    amoeboid.def
    cat_bridge.cpp
    pipe_server.cpp
    utilities/mewjector_support.c
    utilities/pe_view.cpp
)

# List dependencies
target_link_libraries(${PROJECT_NAME} PRIVATE
    Detours
    libtomcrypt
    mewjector
    SDL3::Headers
)

# List include directories
target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# List definitions
target_compile_definitions(${PROJECT_NAME} PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# A console window next to the game with the mod's log (for development)
option(CAT_BRIDGE_CONSOLE "Open a debug console window in the game process" OFF)
if(CAT_BRIDGE_CONSOLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_CONSOLE_LOGGING)
endif()

# Set required flags
if(MSVC)
elseif(MINGW)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_link_options(${PROJECT_NAME} PRIVATE -static-libstdc++)
        target_link_options(${PROJECT_NAME} PRIVATE -Wl,--pdb=)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(${PROJECT_NAME} PRIVATE -static-libgcc -static-libstdc++ -static -lwinpthread -dynamic)
    endif()
    set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")
endif()

# Set language modes
set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

# Set warnings
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
elseif(MINGW)
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Set install paths
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION .)
if(MSVC)
    install(FILES $<TARGET_PDB_FILE:${PROJECT_NAME}> DESTINATION . OPTIONAL)
elseif(MINGW)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        install(FILES "$<TARGET_FILE_DIR:${PROJECT_NAME}>/$<TARGET_FILE_BASE_NAME:${PROJECT_NAME}>.pdb" DESTINATION . OPTIONAL)
    endif()
endif()
