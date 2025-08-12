#include "AudioProcessor.h"
#include <iostream>
#include <cstring>
#include <algorithm>

AudioProcessor::AudioProcessor()
    : input_stream_(nullptr),
      output_stream_(nullptr),
      playback_buffer_(nullptr),
      recording_(false),
      playing_(false),
      initialized_(false),
      sample_rate_(44100),
      frames_per_buffer_(256),
      inputDeviceId_(-1),
      outputDeviceId_(-1),
      channels_(2) {
}

AudioProcessor::~AudioProcessor() {
    cleanup();
}

bool AudioProcessor::initialize(int inputDeviceId, int outputDeviceId, int sample_rate, int channels, int frames_per_buffer) {
    if (initialized_) {
        cleanup();
    }

    inputDeviceId_ = inputDeviceId;
    outputDeviceId_ = outputDeviceId;
    sample_rate_ = sample_rate;
    channels_ = channels;
    frames_per_buffer_ = frames_per_buffer;

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "Failed to initialize PortAudio: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    // Verify input device
    const PaDeviceInfo* inputInfo = Pa_GetDeviceInfo(inputDeviceId_);
    if (!inputInfo) {
        std::cerr << "Invalid input device ID: " << inputDeviceId_ << std::endl;
        Pa_Terminate();
        return false;
    }

    // Verify output device
    const PaDeviceInfo* outputInfo = Pa_GetDeviceInfo(outputDeviceId_);
    if (!outputInfo) {
        std::cerr << "Invalid output device ID: " << outputDeviceId_ << std::endl;
        Pa_Terminate();
        return false;
    }

    // Initialize playback buffer with capacity for 1 second of audio
    size_t bufferCapacity = sample_rate_ * channels_;
    playback_buffer_ = new AudioBuffer(bufferCapacity);

    std::cout << "Initializing AudioProcessor:" << std::endl;
    std::cout << "  Input Device: " << inputInfo->name << std::endl;
    std::cout << "  Output Device: " << outputInfo->name << std::endl;
    std::cout << "  Sample Rate: " << sample_rate_ << "Hz" << std::endl;
    std::cout << "  Channels: " << channels_ << std::endl;
    std::cout << "  Buffer Size: " << frames_per_buffer_ << " frames" << std::endl;

    initialized_ = true;
    return true;
}

bool AudioProcessor::startRecording() {
    if (!initialized_ || recording_) {
        return false;
    }

    const PaDeviceInfo* inputInfo = Pa_GetDeviceInfo(inputDeviceId_);
    if (!inputInfo) {
        std::cerr << "Input device not found" << std::endl;
        return false;
    }

    // Set up input parameters
    PaStreamParameters inputParameters;
    inputParameters.device = inputDeviceId_;
    inputParameters.channelCount = channels_;
    inputParameters.sampleFormat = paFloat32;
    inputParameters.suggestedLatency = inputInfo->defaultLowInputLatency;
    inputParameters.hostApiSpecificStreamInfo = nullptr;

    // Open input stream
    PaError err = Pa_OpenStream(
        &input_stream_,
        &inputParameters,
        nullptr,  // No output
        sample_rate_,
        frames_per_buffer_,
        paClipOff,
        &AudioProcessor::recordCallback,
        this
    );

    if (err != paNoError) {
        std::cerr << "Failed to open input stream: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    // Start the stream
    err = Pa_StartStream(input_stream_);
    if (err != paNoError) {
        std::cerr << "Failed to start input stream: " << Pa_GetErrorText(err) << std::endl;
        Pa_CloseStream(input_stream_);
        input_stream_ = nullptr;
        return false;
    }

    recording_ = true;
    std::cout << "Recording started on device: " << inputInfo->name << std::endl;
    return true;
}

bool AudioProcessor::startPlayback() {
    if (!initialized_ || playing_) {
        return false;
    }

    const PaDeviceInfo* outputInfo = Pa_GetDeviceInfo(outputDeviceId_);
    if (!outputInfo) {
        std::cerr << "Output device not found" << std::endl;
        return false;
    }

    // Set up output parameters
    PaStreamParameters outputParameters;
    outputParameters.device = outputDeviceId_;
    outputParameters.channelCount = channels_;
    outputParameters.sampleFormat = paFloat32;
    outputParameters.suggestedLatency = outputInfo->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = nullptr;

    // Open output stream
    PaError err = Pa_OpenStream(
        &output_stream_,
        nullptr,  // No input
        &outputParameters,
        sample_rate_,
        frames_per_buffer_,
        paClipOff,
        &AudioProcessor::playCallback,
        this
    );

    if (err != paNoError) {
        std::cerr << "Failed to open output stream: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    // Start the stream
    err = Pa_StartStream(output_stream_);
    if (err != paNoError) {
        std::cerr << "Failed to start output stream: " << Pa_GetErrorText(err) << std::endl;
        Pa_CloseStream(output_stream_);
        output_stream_ = nullptr;
        return false;
    }

    playing_ = true;
    std::cout << "Playback started on device: " << outputInfo->name << std::endl;
    return true;
}

void AudioProcessor::stop() {
    if (recording_ && input_stream_) {
        PaError err = Pa_StopStream(input_stream_);
        if (err != paNoError) {
            std::cerr << "Error stopping input stream: " << Pa_GetErrorText(err) << std::endl;
        }
        
        err = Pa_CloseStream(input_stream_);
        if (err != paNoError) {
            std::cerr << "Error closing input stream: " << Pa_GetErrorText(err) << std::endl;
        }
        
        input_stream_ = nullptr;
        recording_ = false;
        std::cout << "Recording stopped" << std::endl;
    }

    if (playing_ && output_stream_) {
        PaError err = Pa_StopStream(output_stream_);
        if (err != paNoError) {
            std::cerr << "Error stopping output stream: " << Pa_GetErrorText(err) << std::endl;
        }
        
        err = Pa_CloseStream(output_stream_);
        if (err != paNoError) {
            std::cerr << "Error closing output stream: " << Pa_GetErrorText(err) << std::endl;
        }
        
        output_stream_ = nullptr;
        playing_ = false;
        std::cout << "Playback stopped" << std::endl;
    }
}

void AudioProcessor::cleanup() {
    stop();
    
    if (playback_buffer_) {
        delete playback_buffer_;
        playback_buffer_ = nullptr;
    }
    
    if (initialized_) {
        Pa_Terminate();
        initialized_ = false;
        std::cout << "AudioProcessor cleaned up" << std::endl;
    }
}

void AudioProcessor::setAudioCaptureCallback(std::function<void(const float*, size_t)> callback) {
    capture_callback_ = callback;
}

bool AudioProcessor::addPlaybackData(const float* data, size_t samples) {
    if (!playback_buffer_) {
        return false;
    }
    
    // Write samples to playback buffer using the correct signature
    return playback_buffer_->write(data, samples);
}

int AudioProcessor::recordCallback(const void* inputBuffer,
                                  void* outputBuffer,
                                  unsigned long framesPerBuffer,
                                  const PaStreamCallbackTimeInfo* timeInfo,
                                  PaStreamCallbackFlags statusFlags,
                                  void* userData) {
    (void)outputBuffer;    // Unused
    (void)timeInfo;        // Unused
    (void)statusFlags;     // Unused

    AudioProcessor* processor = static_cast<AudioProcessor*>(userData);
    const float* input = static_cast<const float*>(inputBuffer);

    if (input && processor->capture_callback_) {
        size_t samples = framesPerBuffer * processor->channels_;
        processor->capture_callback_(input, samples);
    }

    return paContinue;
}

int AudioProcessor::playCallback(const void* inputBuffer,
                                void* outputBuffer,
                                unsigned long framesPerBuffer,
                                const PaStreamCallbackTimeInfo* timeInfo,
                                PaStreamCallbackFlags statusFlags,
                                void* userData) {
    (void)inputBuffer;     // Unused
    (void)timeInfo;        // Unused
    (void)statusFlags;     // Unused

    AudioProcessor* processor = static_cast<AudioProcessor*>(userData);
    float* output = static_cast<float*>(outputBuffer);

    size_t samplesNeeded = framesPerBuffer * processor->channels_;

    if (!processor->playback_buffer_) {
        // Fill with silence if no buffer
        std::fill(output, output + samplesNeeded, 0.0f);
        return paContinue;
    }

    // Read from buffer using the correct signature
    if (!processor->playback_buffer_->read(output, samplesNeeded)) {
        // If read fails (not enough data), fill remainder with silence
        std::fill(output, output + samplesNeeded, 0.0f);
    }

    return paContinue;
}