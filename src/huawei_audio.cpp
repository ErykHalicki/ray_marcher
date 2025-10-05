#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <array>
#include "AudioAnalyzer.h"

class HuaweiAudioDemo {
private:
    SDL_Window* window = nullptr;
    SDL_GPUDevice* gpu_device = nullptr;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUBuffer* vertex_buffer = nullptr;
    SDL_GPUBuffer* camera_buffer = nullptr;
    SDL_GPUBuffer* audio_buffer = nullptr;
    bool running = true;

    Uint64 last_time = 0;
    int frame_count = 0;

    // Camera state
    float cam_x = 0.0f;
    float cam_y = 2.0f;
    float cam_z = 0.0f;
    float cam_yaw = 0.0f;
    float cam_pitch = 0.0f;

    // Mouse state
    bool mouse_captured = false;
    float mouse_sensitivity = 0.002f;

    // Keyboard state
    bool key_w = false;
    bool key_s = false;
    bool key_a = false;
    bool key_d = false;
    bool key_space = false;
    bool key_shift = false;

    // Audio analyzer
    AudioAnalyzer audio_analyzer;

    struct Vertex {
        float x, y;
        float u, v;
    };

    struct CameraParams {
        float pos_x, pos_y, pos_z;
        float yaw;
        float pitch;
        float padding[3];  // Alignment
    };

    struct AudioParams {
        float bass;
        float mid;
        float high;
        float padding;  // Alignment
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
        return "main0";
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
            "Huawei Ray Marcher with Audio",
            1024, 1024,
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

        // Initialize audio analyzer
        std::cout << "Initializing audio analyzer...\n";
        if (!audio_analyzer.initialize(0)) {
            std::cerr << "Warning: Failed to initialize audio analyzer\n";
            // Continue anyway - demo will work without audio
        }

        if (!createPipeline()) {
            return false;
        }

        createVertexBuffer();
        createCameraBuffer();
        createAudioBuffer();
        return true;
    }

    bool createPipeline() {
        std::string vert_path = std::string("src/shaders/huawei_audio/huawei_audio.vert") + getShaderExtension();
        std::string frag_path = std::string("src/shaders/huawei_audio/huawei_audio.frag") + getShaderExtension();

        auto vert_code = loadShader(vert_path.c_str());
        auto frag_code = loadShader(frag_path.c_str());

        if (vert_code.empty() || frag_code.empty()) {
            std::cerr << "Failed to load shader files\n";
            return false;
        }

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

        SDL_GPUShaderCreateInfo frag_info = {};
        frag_info.code = frag_code.data();
        frag_info.code_size = frag_code.size();
        frag_info.entrypoint = getShaderEntrypoint();
        frag_info.format = getShaderFormat();
        frag_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        frag_info.num_samplers = 0;
        frag_info.num_storage_textures = 0;
        frag_info.num_storage_buffers = 2;  // Changed from 1 to 2 (camera + audio)
        frag_info.num_uniform_buffers = 0;

        SDL_GPUShader* frag_shader = SDL_CreateGPUShader(gpu_device, &frag_info);
        if (!frag_shader) {
            std::cerr << "Failed to create fragment shader: " << SDL_GetError() << "\n";
            SDL_ReleaseGPUShader(gpu_device, vert_shader);
            return false;
        }

        SDL_GPUVertexAttribute vertex_attributes[2] = {};
        vertex_attributes[0].location = 0;
        vertex_attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        vertex_attributes[0].offset = 0;
        vertex_attributes[0].buffer_slot = 0;

        vertex_attributes[1].location = 1;
        vertex_attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        vertex_attributes[1].offset = sizeof(float) * 2;
        vertex_attributes[1].buffer_slot = 0;

        SDL_GPUVertexBufferDescription vertex_buffer_desc = {};
        vertex_buffer_desc.slot = 0;
        vertex_buffer_desc.pitch = sizeof(Vertex);
        vertex_buffer_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        vertex_buffer_desc.instance_step_rate = 0;

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
        Vertex vertices[] = {
            {-1.0f, -1.0f, 0.0f, 0.0f},
            { 1.0f, -1.0f, 1.0f, 0.0f},
            {-1.0f,  1.0f, 0.0f, 1.0f},
            { 1.0f,  1.0f, 1.0f, 1.0f},
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

    void createCameraBuffer() {
        SDL_GPUBufferCreateInfo buffer_info = {};
        buffer_info.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
        buffer_info.size = sizeof(CameraParams);

        camera_buffer = SDL_CreateGPUBuffer(gpu_device, &buffer_info);
    }

    void createAudioBuffer() {
        SDL_GPUBufferCreateInfo buffer_info = {};
        buffer_info.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
        buffer_info.size = sizeof(AudioParams);

        audio_buffer = SDL_CreateGPUBuffer(gpu_device, &buffer_info);
    }

    void updateCameraBuffer() {
        if (!camera_buffer) return;

        CameraParams params = {cam_x, cam_y, cam_z, cam_yaw, cam_pitch, {0, 0, 0}};

        SDL_GPUTransferBufferCreateInfo transfer_info = {};
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = sizeof(CameraParams);

        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(gpu_device, &transfer_info);
        void* data = SDL_MapGPUTransferBuffer(gpu_device, transfer, false);
        SDL_memcpy(data, &params, sizeof(CameraParams));
        SDL_UnmapGPUTransferBuffer(gpu_device, transfer);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu_device);
        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);

        SDL_GPUTransferBufferLocation src = {};
        src.transfer_buffer = transfer;
        src.offset = 0;

        SDL_GPUBufferRegion dst = {};
        dst.buffer = camera_buffer;
        dst.offset = 0;
        dst.size = sizeof(CameraParams);

        SDL_UploadToGPUBuffer(copy_pass, &src, &dst, false);
        SDL_EndGPUCopyPass(copy_pass);
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(gpu_device, transfer);
    }

    void updateAudioBuffer() {
        if (!audio_buffer) return;

        // Update audio analyzer and get coefficients
        audio_analyzer.update();
        auto coeffs = audio_analyzer.getCoefficients();

        AudioParams params = {coeffs[0], coeffs[1], coeffs[2], 0.0f};

        SDL_GPUTransferBufferCreateInfo transfer_info = {};
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = sizeof(AudioParams);

        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(gpu_device, &transfer_info);
        void* data = SDL_MapGPUTransferBuffer(gpu_device, transfer, false);
        SDL_memcpy(data, &params, sizeof(AudioParams));
        SDL_UnmapGPUTransferBuffer(gpu_device, transfer);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu_device);
        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);

        SDL_GPUTransferBufferLocation src = {};
        src.transfer_buffer = transfer;
        src.offset = 0;

        SDL_GPUBufferRegion dst = {};
        dst.buffer = audio_buffer;
        dst.offset = 0;
        dst.size = sizeof(AudioParams);

        SDL_UploadToGPUBuffer(copy_pass, &src, &dst, false);
        SDL_EndGPUCopyPass(copy_pass);
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(gpu_device, transfer);
    }

    void updateCamera(float delta_time) {
        float move_speed = 0.5f * delta_time;

        // Calculate forward and right vectors from yaw
        float forward_x = sin(cam_yaw);
        float forward_z = cos(cam_yaw);
        float right_x = cos(cam_yaw);
        float right_z = -sin(cam_yaw);

        // WASD movement
        if (key_w) {
            cam_x += forward_x * move_speed;
            cam_z += forward_z * move_speed;
        }
        if (key_s) {
            cam_x -= forward_x * move_speed;
            cam_z -= forward_z * move_speed;
        }
        if (key_a) {
            cam_x -= right_x * move_speed;
            cam_z -= right_z * move_speed;
        }
        if (key_d) {
            cam_x += right_x * move_speed;
            cam_z += right_z * move_speed;
        }

        // Vertical movement
        if (key_space) {
            cam_y += move_speed;
        }
        if (key_shift) {
            cam_y -= move_speed;
        }

        // Clamp pitch
        if (cam_pitch > 1.5f) cam_pitch = 1.5f;
        if (cam_pitch < -1.5f) cam_pitch = -1.5f;
    }

    void render() {
        updateCameraBuffer();
        updateAudioBuffer();

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

            if (pipeline && vertex_buffer && camera_buffer && audio_buffer) {
                SDL_BindGPUGraphicsPipeline(pass, pipeline);

                SDL_GPUBufferBinding vbinding = {};
                vbinding.buffer = vertex_buffer;
                vbinding.offset = 0;

                SDL_BindGPUVertexBuffers(pass, 0, &vbinding, 1);

                SDL_GPUBuffer* storage_buffers[] = {camera_buffer, audio_buffer};
                SDL_BindGPUFragmentStorageBuffers(pass, 0, storage_buffers, 2);

                SDL_DrawGPUPrimitives(pass, 4, 1, 0, 0);
            }

            SDL_EndGPURenderPass(pass);
        }

        SDL_SubmitGPUCommandBuffer(cmd);

        // Wait for GPU to finish rendering
        SDL_WaitForGPUIdle(gpu_device);
    }

    void handleEvent(const SDL_Event& event) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                switch (event.key.key) {
                    case SDLK_ESCAPE:
                    case SDLK_Q:
                        running = false;
                        break;
                    case SDLK_W: key_w = true; break;
                    case SDLK_S: key_s = true; break;
                    case SDLK_A: key_a = true; break;
                    case SDLK_D: key_d = true; break;
                    case SDLK_SPACE: key_space = true; break;
                    case SDLK_LSHIFT:
                    case SDLK_RSHIFT:
                        key_shift = true;
                        break;
                }
                break;
            case SDL_EVENT_KEY_UP:
                switch (event.key.key) {
                    case SDLK_W: key_w = false; break;
                    case SDLK_S: key_s = false; break;
                    case SDLK_A: key_a = false; break;
                    case SDLK_D: key_d = false; break;
                    case SDLK_SPACE: key_space = false; break;
                    case SDLK_LSHIFT:
                    case SDLK_RSHIFT:
                        key_shift = false;
                        break;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (!mouse_captured) {
                    SDL_SetWindowRelativeMouseMode(window, true);
                    mouse_captured = true;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (mouse_captured) {
                    cam_yaw += event.motion.xrel * mouse_sensitivity;
                    cam_pitch -= event.motion.yrel * mouse_sensitivity;
                }
                break;
        }
    }

    void run() {
        std::cout << "Huawei Ray Marcher with Audio Reactivity\n";
        std::cout << "Controls:\n";
        std::cout << "  Click to capture mouse\n";
        std::cout << "  WASD: Move horizontally\n";
        std::cout << "  Space/Shift: Move up/down\n";
        std::cout << "  Mouse: Look around\n";
        std::cout << "  ESC or Q: Quit\n";
        std::cout << "\nAudio bands are being analyzed:\n";
        std::cout << "  Bass: 20-250 Hz\n";
        std::cout << "  Mid: 250-4000 Hz\n";
        std::cout << "  High: 4000-20000 Hz\n";

        last_time = SDL_GetTicks();
        Uint64 last_frame_time = SDL_GetPerformanceCounter();

        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                handleEvent(event);
            }

            // Calculate delta time
            Uint64 current_frame_time = SDL_GetPerformanceCounter();
            float delta_time = (current_frame_time - last_frame_time) / (float)SDL_GetPerformanceFrequency();
            last_frame_time = current_frame_time;

            updateCamera(delta_time);

            Uint64 frame_start = SDL_GetPerformanceCounter();
            render();
            Uint64 frame_end = SDL_GetPerformanceCounter();

            float frame_time_ms = (frame_end - frame_start) / (float)SDL_GetPerformanceFrequency() * 1000.0f;

            frame_count++;
            Uint64 current_time = SDL_GetTicks();
            Uint64 elapsed = current_time - last_time;

            // Print stats every second
            if (elapsed >= 1000) {
                float fps = frame_count / (elapsed / 1000.0f);
                auto coeffs = audio_analyzer.getCoefficients();
                std::cout << "FPS: " << fps << " | Frame time: " << frame_time_ms << " ms";
                std::cout << " | Audio [Bass: " << coeffs[0] << ", Mid: " << coeffs[1] << ", High: " << coeffs[2] << "]\n";
                frame_count = 0;
                last_time = current_time;
            }

        }
    }

    ~HuaweiAudioDemo() {
        audio_analyzer.cleanup();

        if (vertex_buffer) {
            SDL_ReleaseGPUBuffer(gpu_device, vertex_buffer);
        }
        if (camera_buffer) {
            SDL_ReleaseGPUBuffer(gpu_device, camera_buffer);
        }
        if (audio_buffer) {
            SDL_ReleaseGPUBuffer(gpu_device, audio_buffer);
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
    HuaweiAudioDemo demo;

    if (!demo.initialize()) {
        return 1;
    }

    demo.run();
    return 0;
}
