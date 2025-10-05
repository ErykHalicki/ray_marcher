#include <SDL3/SDL.h>
#include <fftw3.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>

int main(int argc, char* argv[]) {
    // Initialize SDL audio subsystem
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        std::cerr << "Failed to initialize SDL: " << SDL_GetError() << std::endl;
        return 1;
    }

    // List available recording devices
    int count;
    SDL_AudioDeviceID *devices = SDL_GetAudioRecordingDevices(&count);

    std::cout << "Available recording devices (" << count << "):" << std::endl;
    for (int i = 0; i < count; i++) {
        const char* name = SDL_GetAudioDeviceName(devices[i]);
        std::cout << "  [" << i << "] " << (name ? name : "Unknown") << std::endl;
    }

    // Prompt user to select device
    int selected_device = 0;
    if (count > 1) {
        std::cout << "\nSelect device number (0-" << (count - 1) << "): ";
        std::cin >> selected_device;
        if (selected_device < 0 || selected_device >= count) {
            std::cerr << "Invalid device selection" << std::endl;
            SDL_free(devices);
            SDL_Quit();
            return 1;
        }
    }

    SDL_AudioDeviceID selected = devices[selected_device];
    SDL_free(devices);

    // Set up audio specification
    SDL_AudioSpec spec = {
        .format = SDL_AUDIO_F32,  // 32-bit float samples
        .channels = 1,             // Mono
        .freq = 44100              // 44.1kHz sample rate
    };

    // Open selected recording device
    SDL_AudioDeviceID mic = SDL_OpenAudioDevice(selected, &spec);

    if (!mic) {
        std::cerr << "Failed to open recording device: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    // Create audio stream for capturing
    SDL_AudioStream* stream = SDL_CreateAudioStream(&spec, &spec);
    if (!stream) {
        std::cerr << "Failed to create audio stream: " << SDL_GetError() << std::endl;
        SDL_CloseAudioDevice(mic);
        SDL_Quit();
        return 1;
    }

    // Bind stream to device
    SDL_BindAudioStream(mic, stream);

    std::cout << "\nRecording started (5 seconds)..." << std::endl;

    // Start recording
    SDL_ResumeAudioDevice(mic);

    // Record for 5 seconds and process audio
    std::vector<float> audioBuffer;
    const int duration_ms = 5000;
    const int chunk_ms = 100;
    const int samples_per_chunk = (spec.freq * chunk_ms) / 1000;
    std::vector<float> chunk_buffer(samples_per_chunk);

    for (int i = 0; i < duration_ms / chunk_ms; i++) {
        SDL_Delay(chunk_ms);

        // Get available audio from stream
        int available = SDL_GetAudioStreamAvailable(stream);
        if (available > 0) {
            int to_read = std::min(available, (int)(chunk_buffer.size() * sizeof(float)));
            int bytes_read = SDL_GetAudioStreamData(stream, chunk_buffer.data(), to_read);

            if (bytes_read > 0) {
                int sample_count = bytes_read / sizeof(float);

                // Calculate RMS (volume level)
                float rms = 0.0f;
                for (int j = 0; j < sample_count; j++) {
                    rms += chunk_buffer[j] * chunk_buffer[j];
                }
                rms = std::sqrt(rms / sample_count);

                // Print volume meter
                int bars = (int)(rms * 50);
                std::cout << "\rVolume: [";
                for (int k = 0; k < 50; k++) {
                    std::cout << (k < bars ? '=' : ' ');
                }
                std::cout << "] " << rms << "    " << std::flush;

                audioBuffer.insert(audioBuffer.end(), chunk_buffer.begin(), chunk_buffer.begin() + sample_count);
            }
        }
    }

    std::cout << "\n\nRecording stopped." << std::endl;
    std::cout << "Captured " << audioBuffer.size() << " samples" << std::endl;

    // Perform FFT analysis
    if (audioBuffer.size() > 0) {
        int fft_size = 4096;
        int num_bins = fft_size / 2 + 1;

        // Allocate FFT buffers
        double *fft_in = fftw_alloc_real(fft_size);
        fftw_complex *fft_out = fftw_alloc_complex(num_bins);

        // Create FFT plan
        fftw_plan plan = fftw_plan_dft_r2c_1d(fft_size, fft_in, fft_out, FFTW_ESTIMATE);

        // Copy audio samples to FFT input
        int samples_to_process = std::min((int)audioBuffer.size(), fft_size);
        for (int i = 0; i < samples_to_process; i++) {
            fft_in[i] = audioBuffer[i];
        }
        // Zero-pad if needed
        for (int i = samples_to_process; i < fft_size; i++) {
            fft_in[i] = 0.0;
        }

        // Execute FFT
        fftw_execute(plan);

        // Calculate magnitudes
        std::vector<double> magnitudes(num_bins);
        double max_magnitude = 0.0;

        for (int i = 0; i < num_bins; i++) {
            double real = fft_out[i][0];
            double imag = fft_out[i][1];
            magnitudes[i] = std::sqrt(real * real + imag * imag);
            max_magnitude = std::max(max_magnitude, magnitudes[i]);
        }

        // Display frequency histogram
        std::cout << "\n=== Frequency Histogram ===\n" << std::endl;

        const int num_display_bins = 40;
        const int bar_width = 50;
        const double freq_per_bin = (double)spec.freq / fft_size;

        for (int i = 0; i < num_display_bins && i < num_bins; i++) {
            double freq_start = i * freq_per_bin;
            double freq_end = (i + 1) * freq_per_bin;

            // Normalize magnitude
            double normalized = (max_magnitude > 0) ? (magnitudes[i] / max_magnitude) : 0.0;
            int bars = (int)(normalized * bar_width);

            // Print frequency range and bar
            printf("%6.0f-%6.0f Hz [", freq_start, freq_end);
            for (int j = 0; j < bar_width; j++) {
                std::cout << (j < bars ? '=' : ' ');
            }
            printf("] %.2f\n", magnitudes[i]);
        }

        // Find dominant frequency
        int max_bin = 0;
        for (int i = 1; i < num_bins; i++) {
            if (magnitudes[i] > magnitudes[max_bin]) {
                max_bin = i;
            }
        }
        double dominant_freq = max_bin * freq_per_bin;

        std::cout << "\nDominant frequency: " << dominant_freq << " Hz" << std::endl;

        // Clean up FFT
        fftw_destroy_plan(plan);
        fftw_free(fft_in);
        fftw_free(fft_out);
    }

    // Clean up
    SDL_DestroyAudioStream(stream);
    SDL_CloseAudioDevice(mic);
    SDL_Quit();

    return 0;
}
