#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <ctime>

class ColoredUVDemo {
private:
    SDL_Window* window = nullptr;
    SDL_GPUDevice* gpu_device = nullptr;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUBuffer* vertex_buffer = nullptr;
    SDL_GPUBuffer* uniform_buffer = nullptr;

    bool running = true;
    float amplitude = 10.0f;
    float frequency = 0.05f;

    Uint64 last_time = 0;
    int frame_count = 0;

    struct Vertex {
        float x, y;
        float u, v;
    };

    struct FBMParams {
        float amplitude;
        float frequency;
    };

    std::vector<uint8_t> loadShader(const char* filename) {
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "Failed to open shader file: " << filename << "\n";
            return {};
        }

        size_t size = file.tellg();
        std::vector<uint8_t> buffer(size);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(buffer.data()), size);
        file.close();

        return buffer;
    }

	int now = time(0);

    SDL_GPUShaderFormat getShaderFormat() {
#ifdef __APPLE__
        return SDL_GPU_SHADERFORMAT_MSL;
#else
        return SDL_GPU_SHADERFORMAT_SPIRV;
#endif
    }

    const char* getShaderExtension() {
#ifdef __APPLE__
        return ".metal";
#else
        return ".spv";
#endif
    }

    const char* getShaderEntrypoint() {
#ifdef __APPLE__
        return "main0";  // spirv-cross renames main to main0 for MSL
#else
        return "main";
#endif
    }

public:
    bool initialize() {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            std::cerr << "SDL initialization failed: " << SDL_GetError() << "\n";
            return false;
        }

        window = SDL_CreateWindow(
            "Colored UV Frame - GPU",
            800, 800,
            SDL_WINDOW_RESIZABLE
        );

        if (!window) {
            std::cerr << "Window creation failed: " << SDL_GetError() << "\n";
            return false;
        }

        gpu_device = SDL_CreateGPUDevice(
            getShaderFormat(),
            true,
            nullptr
        );

        if (!gpu_device) {
            std::cerr << "GPU device creation failed: " << SDL_GetError() << "\n";
            return false;
        }

        if (!SDL_ClaimWindowForGPUDevice(gpu_device, window)) {
            std::cerr << "Failed to claim window for GPU: " << SDL_GetError() << "\n";
            return false;
        }

        if (!createPipeline()) {
            return false;
        }

        createVertexBuffer();
        createUniformBuffer();
        return true;
    }

    bool createPipeline() {
        // Load compiled shaders (SPIR-V or MSL depending on platform)
        std::string vert_path = std::string("src/shaders/color.vert") + getShaderExtension();
        std::string frag_path = std::string("src/shaders/color.frag") + getShaderExtension();

        auto vert_code = loadShader(vert_path.c_str());
        auto frag_code = loadShader(frag_path.c_str());

        if (vert_code.empty() || frag_code.empty()) {
            std::cerr << "Failed to load shader files\n";
            std::cerr << "Make sure shaders are compiled and available in build/shaders/\n";
            return false;
        }

        // Create vertex shader
        SDL_GPUShaderCreateInfo vert_info = {};
        vert_info.code = vert_code.data();
        vert_info.code_size = vert_code.size();
        vert_info.entrypoint = getShaderEntrypoint();
        vert_info.format = getShaderFormat();
        vert_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        vert_info.num_samplers = 0;
        vert_info.num_storage_textures = 0;
        vert_info.num_storage_buffers = 0;
        vert_info.num_uniform_buffers = 0;

        SDL_GPUShader* vert_shader = SDL_CreateGPUShader(gpu_device, &vert_info);
        if (!vert_shader) {
            std::cerr << "Failed to create vertex shader: " << SDL_GetError() << "\n";
            return false;
        }

        // Create fragment shader
        SDL_GPUShaderCreateInfo frag_info = {};
        frag_info.code = frag_code.data();
        frag_info.code_size = frag_code.size();
        frag_info.entrypoint = getShaderEntrypoint();
        frag_info.format = getShaderFormat();
        frag_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        frag_info.num_samplers = 0;
        frag_info.num_storage_textures = 0;
        frag_info.num_storage_buffers = 1;
        frag_info.num_uniform_buffers = 0;

        SDL_GPUShader* frag_shader = SDL_CreateGPUShader(gpu_device, &frag_info);
        if (!frag_shader) {
            std::cerr << "Failed to create fragment shader: " << SDL_GetError() << "\n";
            SDL_ReleaseGPUShader(gpu_device, vert_shader);
            return false;
        }

        // Define vertex attributes
        SDL_GPUVertexAttribute vertex_attributes[2] = {};

        // Position attribute
        vertex_attributes[0].location = 0;
        vertex_attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        vertex_attributes[0].offset = 0;
        vertex_attributes[0].buffer_slot = 0;

        // UV attribute
        vertex_attributes[1].location = 1;
        vertex_attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        vertex_attributes[1].offset = sizeof(float) * 2;
        vertex_attributes[1].buffer_slot = 0;

        SDL_GPUVertexBufferDescription vertex_buffer_desc = {};
        vertex_buffer_desc.slot = 0;
        vertex_buffer_desc.pitch = sizeof(Vertex);
        vertex_buffer_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        vertex_buffer_desc.instance_step_rate = 0;

        // Create graphics pipeline
        SDL_GPUGraphicsPipelineCreateInfo pipeline_info = {};
        pipeline_info.vertex_shader = vert_shader;
        pipeline_info.fragment_shader = frag_shader;

        pipeline_info.vertex_input_state.vertex_buffer_descriptions = &vertex_buffer_desc;
        pipeline_info.vertex_input_state.num_vertex_buffers = 1;
        pipeline_info.vertex_input_state.vertex_attributes = vertex_attributes;
        pipeline_info.vertex_input_state.num_vertex_attributes = 2;

        pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;

        pipeline_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pipeline_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pipeline_info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;

        SDL_GPUColorTargetDescription color_target = {};
        color_target.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
        color_target.blend_state.enable_blend = false;

        pipeline_info.target_info.num_color_targets = 1;
        pipeline_info.target_info.color_target_descriptions = &color_target;
        pipeline_info.target_info.has_depth_stencil_target = false;

        pipeline = SDL_CreateGPUGraphicsPipeline(gpu_device, &pipeline_info);

        SDL_ReleaseGPUShader(gpu_device, vert_shader);
        SDL_ReleaseGPUShader(gpu_device, frag_shader);

        if (!pipeline) {
            std::cerr << "Failed to create graphics pipeline: " << SDL_GetError() << "\n";
            return false;
        }

        return true;
    }

    void createVertexBuffer() {
        // Fullscreen quad with UV coordinates
        Vertex vertices[] = {
            {-1.0f, -1.0f, 0.0f, 0.0f},  // Bottom-left
            { 1.0f, -1.0f, 1.0f, 0.0f},  // Bottom-right
            {-1.0f,  1.0f, 0.0f, 1.0f},  // Top-left
            { 1.0f,  1.0f, 1.0f, 1.0f},  // Top-right
        };

        SDL_GPUBufferCreateInfo buffer_info = {};
        buffer_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        buffer_info.size = sizeof(vertices);

        vertex_buffer = SDL_CreateGPUBuffer(gpu_device, &buffer_info);

        if (vertex_buffer) {
            SDL_GPUTransferBufferCreateInfo transfer_info = {};
            transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            transfer_info.size = sizeof(vertices);

            SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(gpu_device, &transfer_info);
            void* data = SDL_MapGPUTransferBuffer(gpu_device, transfer, false);
            SDL_memcpy(data, vertices, sizeof(vertices));
            SDL_UnmapGPUTransferBuffer(gpu_device, transfer);

            SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu_device);
            SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);

            SDL_GPUTransferBufferLocation src = {};
            src.transfer_buffer = transfer;
            src.offset = 0;

            SDL_GPUBufferRegion dst = {};
            dst.buffer = vertex_buffer;
            dst.offset = 0;
            dst.size = sizeof(vertices);

            SDL_UploadToGPUBuffer(copy_pass, &src, &dst, false);
            SDL_EndGPUCopyPass(copy_pass);
            SDL_SubmitGPUCommandBuffer(cmd);
            SDL_ReleaseGPUTransferBuffer(gpu_device, transfer);
        }
    }

    void createUniformBuffer() {
        SDL_GPUBufferCreateInfo buffer_info = {};
        buffer_info.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
        buffer_info.size = sizeof(FBMParams);

        uniform_buffer = SDL_CreateGPUBuffer(gpu_device, &buffer_info);
    }

    void updateUniformBuffer() {
        if (!uniform_buffer) return;

        FBMParams params = {amplitude, frequency};

        SDL_GPUTransferBufferCreateInfo transfer_info = {};
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = sizeof(FBMParams);

        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(gpu_device, &transfer_info);
        void* data = SDL_MapGPUTransferBuffer(gpu_device, transfer, false);
        SDL_memcpy(data, &params, sizeof(FBMParams));
        SDL_UnmapGPUTransferBuffer(gpu_device, transfer);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu_device);
        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);

        SDL_GPUTransferBufferLocation src = {};
        src.transfer_buffer = transfer;
        src.offset = 0;

        SDL_GPUBufferRegion dst = {};
        dst.buffer = uniform_buffer;
        dst.offset = 0;
        dst.size = sizeof(FBMParams);

        SDL_UploadToGPUBuffer(copy_pass, &src, &dst, false);
        SDL_EndGPUCopyPass(copy_pass);
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(gpu_device, transfer);
    }

    void render() {
        updateUniformBuffer();

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu_device);
        if (!cmd) return;

        SDL_GPUTexture* swapchain;
        if (!SDL_AcquireGPUSwapchainTexture(cmd, window, &swapchain, nullptr, nullptr)) {
            SDL_CancelGPUCommandBuffer(cmd);
            return;
        }

        if (swapchain) {
            SDL_GPUColorTargetInfo color_target = {};
            color_target.texture = swapchain;
            color_target.clear_color = {0.1f, 0.1f, 0.15f, 1.0f};
            color_target.load_op = SDL_GPU_LOADOP_CLEAR;
            color_target.store_op = SDL_GPU_STOREOP_STORE;

            SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, nullptr);

            if (pipeline && vertex_buffer && uniform_buffer) {
                SDL_BindGPUGraphicsPipeline(pass, pipeline);

                SDL_GPUBufferBinding vbinding = {};
                vbinding.buffer = vertex_buffer;
                vbinding.offset = 0;

                SDL_BindGPUVertexBuffers(pass, 0, &vbinding, 1);

                SDL_GPUBuffer* storage_buffers[] = {uniform_buffer};
                SDL_BindGPUFragmentStorageBuffers(pass, 0, storage_buffers, 1);

                SDL_DrawGPUPrimitives(pass, 4, 1, 0, 0);
            }

            SDL_EndGPURenderPass(pass);
        }

        SDL_SubmitGPUCommandBuffer(cmd);
    }

    void handleEvent(const SDL_Event& event) {
        if (event.type == SDL_EVENT_QUIT) {
            running = false;
        } else if (event.type == SDL_EVENT_KEY_DOWN) {
            if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_Q) {
                running = false;
            }
            // Amplitude controls (Up/Down arrows)
            else if (event.key.key == SDLK_UP) {
                amplitude += 1.0f;
                std::cout << "Amplitude: " << amplitude << ", Frequency: " << frequency << "\n";
            }
            else if (event.key.key == SDLK_DOWN) {
                amplitude = std::max(0.1f, amplitude - 1.0f);
                std::cout << "Amplitude: " << amplitude << ", Frequency: " << frequency << "\n";
            }
            // Frequency controls (Left/Right arrows)
            else if (event.key.key == SDLK_RIGHT) {
                frequency += 0.01f;
                std::cout << "Amplitude: " << amplitude << ", Frequency: " << frequency << "\n";
            }
            else if (event.key.key == SDLK_LEFT) {
                frequency = std::max(0.01f, frequency - 0.01f);
                std::cout << "Amplitude: " << amplitude << ", Frequency: " << frequency << "\n";
            }
        }
    }

    void run() {
        std::cout << "Colored UV Frame - GPU Demo\n";
        std::cout << "Controls:\n";
        std::cout << "  Up/Down arrows: Adjust amplitude\n";
        std::cout << "  Left/Right arrows: Adjust frequency\n";
        std::cout << "  ESC or Q: Quit\n";
        std::cout << "Initial - Amplitude: " << amplitude << ", Frequency: " << frequency << "\n";

        last_time = SDL_GetTicks();

        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                handleEvent(event);
            }

            render();

            frame_count++;
            Uint64 current_time = SDL_GetTicks();
            Uint64 elapsed = current_time - last_time;

            // Print FPS every second
            if (elapsed >= 1000) {
                float fps = frame_count / (elapsed / 1000.0f);
                std::cout << "FPS: " << fps << " (" << (1000.0f / fps) << " ms/frame)\n";
                frame_count = 0;
                last_time = current_time;
            }

            SDL_Delay(16);
        }
    }

    ~ColoredUVDemo() {
        if (vertex_buffer) {
            SDL_ReleaseGPUBuffer(gpu_device, vertex_buffer);
        }
        if (uniform_buffer) {
            SDL_ReleaseGPUBuffer(gpu_device, uniform_buffer);
        }
        if (pipeline) {
            SDL_ReleaseGPUGraphicsPipeline(gpu_device, pipeline);
        }
        if (gpu_device) {
            SDL_DestroyGPUDevice(gpu_device);
        }
        if (window) {
            SDL_DestroyWindow(window);
        }
        SDL_Quit();
    }
};

int main(int argc, char* argv[]) {
    ColoredUVDemo demo;

    if (!demo.initialize()) {
        return 1;
    }

    demo.run();
    return 0;
}
