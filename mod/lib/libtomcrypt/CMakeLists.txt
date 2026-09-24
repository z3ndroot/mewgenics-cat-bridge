project(libtomcrypt)

# Set path anchors
set(LIBTOMCRYPT_DIR third_party)

# List build targets
add_library(${PROJECT_NAME} STATIC
    ${LIBTOMCRYPT_DIR}/sha256_desc.c
    ${LIBTOMCRYPT_DIR}/sha256.c
    ${LIBTOMCRYPT_DIR}/sha256_x86.c
)

# List include directories
target_include_directories(${PROJECT_NAME} PUBLIC
    ${LIBTOMCRYPT_DIR}
)

# List definitions
target_compile_definitions(${PROJECT_NAME} PRIVATE
)

# Set language modes
set_target_properties(${PROJECT_NAME} PROPERTIES
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
