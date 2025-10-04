cmake_minimum_required(VERSION 3.10)
project(SDL3Project)

set(CMAKE_CXX_STANDARD 11)
find_package(SDL3 REQUIRED)

# Find glslangValidator for shader compilation
find_program(GLSLANG_VALIDATOR glslangValidator)
if(NOT GLSLANG_VALIDATOR)
    message(WARNING "glslangValidator not found. Shaders will not be compiled.")
    message(WARNING "Install with: sudo apt install glslang-tools (Ubuntu/Debian)")
    message(WARNING "             sudo pacman -S glslang (Arch)")
    message(WARNING "             brew install glslang (macOS)")
endif()

# Shader compilation function
function(compile_shader SHADER_SOURCE OUTPUT_FILE)
    if(GLSLANG_VALIDATOR)
        add_custom_command(
            OUTPUT ${OUTPUT_FILE}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
            COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER_SOURCE} -o ${OUTPUT_FILE}
            DEPENDS ${SHADER_SOURCE}
            COMMENT "Compiling shader ${SHADER_SOURCE}"
        )
    endif()
endfunction()

# Compile shaders
set(SHADER_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(COMPILED_SHADER_DIR ${CMAKE_BINARY_DIR}/shaders)

compile_shader(${SHADER_DIR}/color.vert ${COMPILED_SHADER_DIR}/color.vert.spv)
compile_shader(${SHADER_DIR}/color.frag ${COMPILED_SHADER_DIR}/color.frag.spv)

# Custom target to build all shaders
if(GLSLANG_VALIDATOR)
    add_custom_target(shaders ALL
        DEPENDS
            ${COMPILED_SHADER_DIR}/color.vert.spv
            ${COMPILED_SHADER_DIR}/color.frag.spv
    )
endif()

add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} SDL3::SDL3)

add_executable(color src/color.cpp)
target_link_libraries(color SDL3::SDL3)
if(GLSLANG_VALIDATOR)
    add_dependencies(color shaders)
endif()