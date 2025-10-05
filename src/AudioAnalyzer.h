#ifndef AUDIO_ANALYZER_H
#define AUDIO_ANALYZER_H

#include <SDL3/SDL.h>
#include <fftw3.h>
#include <vector>
#include <array>

class AudioAnalyzer {
public:
    struct FrequencyBands {
        float bass;      // 20-250 Hz
        float mid;       // 250-4000 Hz
        float high;      // 4000-20000 Hz
    };

private:
    SDL_AudioDeviceID mic = 0;
    SDL_AudioStream* stream = nullptr;
    SDL_AudioSpec spec;

    std::vector<float> audioBuffer;
    int fft_size = 4096;
    int num_bins;

    double* fft_in = nullptr;
    fftw_complex* fft_out = nullptr;
    fftw_plan plan = nullptr;

    bool initialized = false;

    // Sliding window for maximum and minimum value tracking
    std::vector<float> max_history;
    std::vector<float> min_history;
    int max_history_size = 300;  // ~5 seconds at 30 fps

public:
    AudioAnalyzer();
    ~AudioAnalyzer();

    bool initialize(int device_index = 0);
    void update();
    FrequencyBands getFrequencyBands();
    std::array<float, 3> getCoefficients();  // Returns [bass, mid, high]
    void cleanup();
};

#endif
