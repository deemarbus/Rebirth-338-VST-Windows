#pragma once
#include "../shared/bridge_ipc.h"
#include <samplerate.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace RebirthBridge {
class SampleRateConverter final {
public:
    static constexpr int32_t maxBlockFrames = 8192;

    static bool isSupportedRate(double rate) noexcept {
        constexpr std::array<double, 6> rates = {
            44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0
        };
        for (const double supported : rates)
            if (std::abs(rate - supported) < 0.001) return true;
        return false;
    }

    SampleRateConverter() = default;
    ~SampleRateConverter() { release(); }
    SampleRateConverter(const SampleRateConverter&) = delete;
    SampleRateConverter& operator=(const SampleRateConverter&) = delete;

    bool configure(double hostRate, int32_t maxOutputFrames) noexcept {
        release();
        if (!isSupportedRate(hostRate) || maxOutputFrames <= 0 ||
            maxOutputFrames > maxBlockFrames) return false;
        hostRate_ = hostRate;
        maxOutputFrames_ = maxOutputFrames;
        ratio_ = hostRate_ / RB_SAMPLE_RATE;
        bypass_ = std::abs(hostRate_ - RB_SAMPLE_RATE) < 0.001;
        if (bypass_) return true;

        const auto inputCapacity = static_cast<int32_t>(
            std::ceil(maxOutputFrames_ / ratio_)) + filterLookaheadFrames;
        if (inputCapacity > maxBlockFrames) return false;
        inputBuffer_.resize(static_cast<size_t>(inputCapacity) * 2);
        outputBuffer_.resize(static_cast<size_t>(maxOutputFrames_) * 2);
        int error = 0;
        state_ = src_new(SRC_SINC_FASTEST, 2, &error);
        error_ = error;
        return state_ != nullptr && error_ == 0;
    }

    int32_t inputFramesForOutput(int32_t outputFrames) const noexcept {
        if (outputFrames < 0 || outputFrames > maxOutputFrames_) return -1;
        if (bypass_) return outputFrames;
        const auto required = static_cast<int32_t>(
            std::ceil(outputFrames / ratio_)) + filterLookaheadFrames;
        return std::max<int32_t>(0, required - pendingFrames_);
    }

    bool process(const float* leftIn, const float* rightIn, int32_t inputFrames,
                 float* leftOut, float* rightOut, int32_t outputFrames,
                 int32_t& outputGenerated) noexcept {
        outputGenerated = 0;
        if (!leftIn || !rightIn || !leftOut || !rightOut || inputFrames < 0 ||
            outputFrames < 0 || outputFrames > maxOutputFrames_) return false;
        if (bypass_) {
            if (inputFrames != outputFrames) return false;
            std::memcpy(leftOut, leftIn, static_cast<size_t>(outputFrames) * sizeof(float));
            std::memcpy(rightOut, rightIn, static_cast<size_t>(outputFrames) * sizeof(float));
            outputGenerated = outputFrames;
            return true;
        }
        if (!state_ || pendingFrames_ + inputFrames >
            static_cast<int32_t>(inputBuffer_.size() / 2)) return false;

        for (int32_t i = 0; i < inputFrames; ++i) {
            const auto frame = static_cast<size_t>(pendingFrames_ + i) * 2;
            inputBuffer_[frame] = leftIn[i];
            inputBuffer_[frame + 1] = rightIn[i];
        }

        SRC_DATA data {};
        data.data_in = inputBuffer_.data();
        data.data_out = outputBuffer_.data();
        data.input_frames = pendingFrames_ + inputFrames;
        data.output_frames = outputFrames;
        data.end_of_input = 0;
        data.src_ratio = ratio_;
        error_ = src_process(state_, &data);
        if (error_ != 0 || data.input_frames_used < 0 ||
            data.input_frames_used > data.input_frames || data.output_frames_gen < 0 ||
            data.output_frames_gen > outputFrames) return false;

        pendingFrames_ = static_cast<int32_t>(data.input_frames - data.input_frames_used);
        if (pendingFrames_ > 0) {
            const auto remaining = static_cast<size_t>(pendingFrames_) * 2;
            std::memmove(inputBuffer_.data(),
                inputBuffer_.data() + static_cast<size_t>(data.input_frames_used) * 2,
                remaining * sizeof(float));
        }
        outputGenerated = static_cast<int32_t>(data.output_frames_gen);
        for (int32_t i = 0; i < outputGenerated; ++i) {
            leftOut[i] = outputBuffer_[static_cast<size_t>(i) * 2];
            rightOut[i] = outputBuffer_[static_cast<size_t>(i) * 2 + 1];
        }
        for (int32_t i = outputGenerated; i < outputFrames; ++i)
            leftOut[i] = rightOut[i] = 0.0f;
        return outputGenerated == outputFrames;
    }

    void reset() noexcept {
        pendingFrames_ = 0;
        if (state_) error_ = src_reset(state_);
    }

    uint32_t latencySamples() const noexcept {
        return static_cast<uint32_t>(std::ceil(RB_LATENCY * ratio_));
    }
    double hostRate() const noexcept { return hostRate_; }
    int error() const noexcept { return error_; }

private:
    static constexpr int32_t filterLookaheadFrames = 64;

    void release() noexcept {
        if (state_) state_ = src_delete(state_);
        inputBuffer_.clear();
        outputBuffer_.clear();
        pendingFrames_ = 0;
        maxOutputFrames_ = 0;
        error_ = 0;
        bypass_ = false;
        hostRate_ = RB_SAMPLE_RATE;
        ratio_ = 1.0;
    }

    SRC_STATE* state_ = nullptr;
    std::vector<float> inputBuffer_;
    std::vector<float> outputBuffer_;
    double hostRate_ = RB_SAMPLE_RATE;
    double ratio_ = 1.0;
    int32_t pendingFrames_ = 0;
    int32_t maxOutputFrames_ = 0;
    int error_ = 0;
    bool bypass_ = true;
};
}