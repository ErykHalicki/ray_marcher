#include "AudioAnalyzer.h"
#include <iostream>
#include <cmath>
#include <algorithm>

AudioAnalyzer::AudioAnalyzer() {
    num_bins = fft_size / 2 + 1;
}

AudioAnalyzer::~AudioAnalyzer() {
    cleanup();
}

bool AudioAnalyzer::initialize(int device_index) {
    if (initialized) {
        std::cerr << "AudioAnalyzer already initialized\n";
        return false;
    }

    // Initialize SDL audio subsystem
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        std::cerr << "Failed to initialize SDL audio: " << SDL_GetError() << std::endl;
        return false;
    }

    // Get available recording devices
    int count;
    SDL_AudioDeviceID* devices = SDL_GetAudioRecordingDevices(&count);

    if (count == 0) {
        std::cerr << "No audio recording devices found\n";
        SDL_free(devices);
        return false;
    }

    if (device_index < 0 || device_index >= count) {
        std::cerr << "Invalid device index\n";
        SDL_free(devices);
        return false;
    }

    SDL_AudioDeviceID selected = devices[device_index];
    SDL_free(devices);

    // Set up audio specification
    spec.format = SDL_AUDIO_F32;
    spec.channels = 1;
    spec.freq = 44100;

    // Open recording device
    mic = SDL_OpenAudioDevice(selected, &spec);
    if (!mic) {
        std::cerr << "Failed to open recording device: " << SDL_GetError() << std::endl;
        return false;
    }

    // Create audio stream
    stream = SDL_CreateAudioStream(&spec, &spec);
    if (!stream) {
        std::cerr << "Failed to create audio stream: " << SDL_GetError() << std::endl;
        SDL_CloseAudioDevice(mic);
        return false;
    }

    // Bind stream to device
    SDL_BindAudioStream(mic, stream);

    // Allocate FFT buffers
    fft_in = fftw_alloc_real(fft_size);
    fft_out = fftw_alloc_complex(num_bins);
    plan = fftw_plan_dft_r2c_1d(fft_size, fft_in, fft_out, FFTW_ESTIMATE);

    // Start recording
    SDL_ResumeAudioDevice(mic);

    initialized = true;
    return true;
}

void AudioAnalyzer::update() {
    if (!initialized) return;

    // Clear old buffer
    audioBuffer.clear();

    // Capture audio from stream
    int available = SDL_GetAudioStreamAvailable(stream);
    if (available > 0) {
        std::vector<float> temp_buffer(available / sizeof(float));
        int bytes_read = SDL_GetAudioStreamData(stream, temp_buffer.data(), available);

        if (bytes_read > 0) {
            int sample_count = bytes_read / sizeof(float);
            audioBuffer.insert(audioBuffer.end(), temp_buffer.begin(), temp_buffer.begin() + sample_count);
        }
    }
}

AudioAnalyzer::FrequencyBands AudioAnalyzer::getFrequencyBands() {
    FrequencyBands bands = {0.0f, 0.0f, 0.0f};

    if (!initialized || audioBuffer.empty()) {
        return bands;
    }

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

    // Calculate magnitudes for each bin
    std::vector<double> magnitudes(num_bins);
    for (int i = 0; i < num_bins; i++) {
        double real = fft_out[i][0];
        double imag = fft_out[i][1];
        magnitudes[i] = std::sqrt(real * real + imag * imag);
    }

    // Frequency per bin
    double freq_per_bin = (double)spec.freq / fft_size;

    // Sum magnitudes for each frequency band
    double bass_sum = 0.0;
    double mid_sum = 0.0;
    double high_sum = 0.0;
    int bass_count = 0;
    int mid_count = 0;
    int high_count = 0;

    for (int i = 0; i < num_bins; i++) {
        double freq = i * freq_per_bin;

        if (freq >= 20 && freq < 250) {
            bass_sum += magnitudes[i];
            bass_count++;
        } else if (freq >= 250 && freq < 4000) {
            mid_sum += magnitudes[i];
            mid_count++;
        } else if (freq >= 4000 && freq <= 20000) {
            high_sum += magnitudes[i];
            high_count++;
        }
    }

    // Average each band
    if (bass_count > 0) bands.bass = bass_sum / bass_count;
    if (mid_count > 0) bands.mid = mid_sum / mid_count;
    if (high_count > 0) bands.high = high_sum / high_count;

    // Track maximum and minimum in a sliding window (~5 seconds)
    float current_max = std::max({bands.bass, bands.mid, bands.high});
    float current_min = std::min({bands.bass, bands.mid, bands.high});
    max_history.push_back(current_max);
    min_history.push_back(current_min);

    // Keep only the last ~5 seconds of history
    if (max_history.size() > max_history_size) {
        max_history.erase(max_history.begin());
    }
    if (min_history.size() > max_history_size) {
        min_history.erase(min_history.begin());
    }

    // Calculate rolling average of the sliding window
    float max_average = 0.0f;
    float min_average = 0.0f;
    for (float val : max_history) {
        max_average += val;
    }
    for (float val : min_history) {
        min_average += val;
    }
    if (max_history.size() > 0) {
        max_average /= max_history.size();
    }
    if (min_history.size() > 0) {
        min_average /= min_history.size();
    }

    // Calculate range and normalize by it
    float range = max_average - min_average;

    if (range > 0.0f) {
        bands.bass = (bands.bass ) / range*2.5;
        bands.mid = (bands.mid ) / range*5.0;
        bands.high = (bands.high ) / range*5.0;
    }

    return bands;
}

std::array<float, 3> AudioAnalyzer::getCoefficients() {
    FrequencyBands bands = getFrequencyBands();
    return {bands.bass, bands.mid, bands.high};
}

void AudioAnalyzer::cleanup() {
    if (!initialized) return;

    // Clean up FFT
    if (plan) {
        fftw_destroy_plan(plan);
        plan = nullptr;
    }
    if (fft_in) {
        fftw_free(fft_in);
        fft_in = nullptr;
    }
    if (fft_out) {
        fftw_free(fft_out);
        fft_out = nullptr;
    }

    // Clean up SDL
    if (stream) {
        SDL_DestroyAudioStream(stream);
        stream = nullptr;
    }
    if (mic) {
        SDL_CloseAudioDevice(mic);
        mic = 0;
    }

    initialized = false;
}
