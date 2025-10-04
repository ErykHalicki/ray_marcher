cmake_minimum_required(VERSION 3.10)
project(SDL3Project)

set(CMAKE_CXX_STANDARD 11)
find_package(SDL3 REQUIRED)

# Find glslangValidator for shader compilation
find_program(GLSLANG_VALIDATOR glslangValidator)
find_program(SPIRV_CROSS spirv-cross)

if(NOT GLSLANG_VALIDATOR)
    message(WARNING "glslangValidator not found. Shaders will not be compiled.")
    message(WARNING "Install with: sudo apt install glslang-tools (Ubuntu/Debian)")
    message(WARNING "             sudo pacman -S glslang (Arch)")
    message(WARNING "             brew install glslang (macOS)")
endif()

if(NOT SPIRV_CROSS)
    message(WARNING "spirv-cross not found. MSL shaders will not be generated.")
    message(WARNING "Install with: brew install spirv-cross (macOS)")
endif()

# Shader compilation function - generates both SPIR-V and MSL
function(compile_shader SHADER_SOURCE SPIRV_OUTPUT MSL_OUTPUT)
    if(GLSLANG_VALIDATOR)
        # Compile GLSL to SPIR-V
        add_custom_command(
            OUTPUT ${SPIRV_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
            COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER_SOURCE} -o ${SPIRV_OUTPUT}
            DEPENDS ${SHADER_SOURCE}
            COMMENT "Compiling GLSL to SPIR-V: ${SHADER_SOURCE}"
        )

        # Convert SPIR-V to MSL
        if(SPIRV_CROSS)
            add_custom_command(
                OUTPUT ${MSL_OUTPUT}
                COMMAND ${SPIRV_CROSS} ${SPIRV_OUTPUT} --msl --output ${MSL_OUTPUT}
                DEPENDS ${SPIRV_OUTPUT}
                COMMENT "Converting SPIR-V to MSL: ${SPIRV_OUTPUT}"
            )
        endif()
    endif()
endfunction()

# Compile shaders
set(SHADER_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(COMPILED_SHADER_DIR ${CMAKE_BINARY_DIR}/shaders)

compile_shader(
    ${SHADER_DIR}/color.vert
    ${COMPILED_SHADER_DIR}/color.vert.spv
    ${COMPILED_SHADER_DIR}/color.vert.metal
)

compile_shader(
    ${SHADER_DIR}/color.frag
    ${COMPILED_SHADER_DIR}/color.frag.spv
    ${COMPILED_SHADER_DIR}/color.frag.metal
)

# Custom target to build all shaders
if(GLSLANG_VALIDATOR)
    set(SHADER_OUTPUTS
        ${COMPILED_SHADER_DIR}/color.vert.spv
        ${COMPILED_SHADER_DIR}/color.frag.spv
    )

    if(SPIRV_CROSS)
        list(APPEND SHADER_OUTPUTS
            ${COMPILED_SHADER_DIR}/color.vert.metal
            ${COMPILED_SHADER_DIR}/color.frag.metal
        )
    endif()

    add_custom_target(shaders ALL DEPENDS ${SHADER_OUTPUTS})
endif()

add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} SDL3::SDL3)

add_executable(color src/color.cpp)
target_link_libraries(color SDL3::SDL3)
if(GLSLANG_VALIDATOR)
    add_dependencies(color shaders)
endif()