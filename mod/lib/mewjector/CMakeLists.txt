project(mewjector)

# Set path anchors
set(MEWJECTOR_DIR third_party)

# List build targets
add_library(${PROJECT_NAME} INTERFACE)

# List include directories
target_include_directories(${PROJECT_NAME} INTERFACE
    ${MEWJECTOR_DIR}
)
