#pragma once
#include "../shared/bridge_ipc.h"
#include "status.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace RebirthBridge {
struct Timing {
    bool valid = false, playing = false, cycle = false;
    double tempo = 140.0, quarterNotes = 0.0;
    int64_t projectSamples = 0;
};

inline bool toPositionTicks(double quarterNotes, int32_t& ticks) noexcept {
    if (!std::isfinite(quarterNotes)) return false;
    if (quarterNotes <= 0.0) { ticks = 0; return true; }
    const double position = quarterNotes * RB_PPQ;
    if (position > (std::numeric_limits<int32_t>::max)()) return false;
    ticks = static_cast<int32_t>(position);
    return true;
}

/* Pointer-free protocol adapter. No allocation, locks, waits, clock queries or
 * operating-system calls occur here. Replies are keyed per sample so transport
 * starts at non-64-aligned host offsets cannot replay another generation. */
class Adapter {
public:
    static constexpr uint32_t cacheSize = 16384;
    void reset(uint32_t token) noexcept {
        token_ = token;
        epoch_ = 1;
        cursor_ = playStart_ = engineFrame_ = 0;
        partial_ = 0;
        active_ = failedSeek_ = failedQueue_ = protocolError_ = haveLast_ = false;
        status_ = Ready;
        underruns_ = 0;
        for (auto& sample : cache_) sample.epoch = 0;
    }
    uint32_t epoch() const noexcept { return epoch_; }
    uint64_t cursor() const noexcept { return cursor_; }
    Status status() const noexcept { return status_; }
    uint32_t underruns() const noexcept { return underruns_; }

    bool accept(const RBReply& reply) noexcept {
        if (reply.client_token != token_ || reply.epoch != epoch_) return true;
        if ((reply.served_channels != 0 && reply.served_channels != 2) ||
            reply.state > RB_REARM_REQUIRED || reply.reserved[0] || reply.reserved[1] ||
            reply.stream_sample > cursor_ ||
            reply.stream_sample > UINT64_MAX - RB_BLOCK_FRAMES) {
            protocolError_ = true;
            return false;
        }
        if (!reply.served_channels) return true;
        for (uint32_t i = 0; i < RB_BLOCK_FRAMES; ++i) {
            if (!std::isfinite(reply.left[i]) || !std::isfinite(reply.right[i])) {
                protocolError_ = true;
                return false;
            }
        }
        for (uint32_t i = 0; i < RB_BLOCK_FRAMES; ++i) {
            const uint64_t tag = reply.stream_sample + i;
            /* Completed audio that has already missed its deadline is stale. */
            if (cursor_ >= RB_LATENCY && tag < cursor_ - RB_LATENCY) continue;
            auto& sample = cache_[tag % cacheSize];
            sample.tag = tag;
            sample.epoch = epoch_;
            sample.left = reply.left[i];
            sample.right = reply.right[i];
        }
        return true;
    }

    void protocolFailure() noexcept { protocolError_ = true; }

    template <typename Push>
    void process(float* left, float* right, int32_t frames, const Timing& timing,
                 Push&& push) noexcept {
        if (frames <= 0) return;
        const bool tempoValid = std::isfinite(timing.tempo) &&
            timing.tempo >= RB_MIN_TEMPO_MILLI / 1000.0 &&
            timing.tempo <= RB_MAX_TEMPO_MILLI / 1000.0;
        const double safeTempo = tempoValid ? timing.tempo : RB_DEFAULT_TEMPO_MILLI / 1000.0;
        const double quartersPerSample = safeTempo / (60.0 * RB_SAMPLE_RATE);
        const double positionTolerance = quartersPerSample * 2.0;
        const uint32_t tempoMilli = tempoValid
            ? static_cast<uint32_t>(std::llround(timing.tempo * 1000.0))
            : RB_DEFAULT_TEMPO_MILLI;
        Status blockError = Ready;
        if (protocolError_) blockError = BadProtocol;
        else if (!timing.valid || !std::isfinite(timing.tempo) ||
                 !std::isfinite(timing.quarterNotes)) blockError = MissingContext;
        else if (!tempoValid) blockError = BadTempo;
        else if (timing.cycle) blockError = UnsupportedCycle;
        if (blockError == Ready && timing.playing && timing.quarterNotes >= 0.0 &&
            timing.quarterNotes + frames * quartersPerSample >
                double((std::numeric_limits<int32_t>::max)()) / RB_PPQ)
            blockError = BadProtocol;

        if (!timing.playing) failedSeek_ = failedQueue_ = false;
        if (active_ && blockError == Ready && timing.playing && haveLast_) {
            if (timing.projectSamples != lastProjectEnd_ ||
                std::abs(timing.quarterNotes - lastQuarterEnd_) > positionTolerance)
                failedSeek_ = true;
        }
        if (failedSeek_ && blockError == Ready && timing.playing)
            blockError = UnsupportedSeek;
        if (failedQueue_ && blockError == Ready && timing.playing)
            blockError = Underrun;
        if (active_ && (!timing.playing || blockError != Ready)) stopEpoch();

        bool missedThisBlock = false;
        for (int32_t i = 0; i < frames; ++i) {
            const double quarter = timing.quarterNotes + i * quartersPerSample;
            if (!active_ && timing.playing && blockError == Ready && quarter >= 0.0) {
                if (quarter > positionTolerance) {
                    failedSeek_ = true;
                    blockError = UnsupportedSeek;
                } else {
                    beginEpoch();
                    active_ = true;
                    engineFrame_ = 0;
                    playStart_ = cursor_;
                }
            }
            left[i] = right[i] = 0.0f;
            if (active_ && blockError == Ready && cursor_ >= RB_LATENCY &&
                cursor_ - RB_LATENCY >= playStart_) {
                const uint64_t tag = cursor_ - RB_LATENCY;
                const auto& sample = cache_[tag % cacheSize];
                if (sample.tag == tag && sample.epoch == epoch_) {
                    left[i] = sample.left;
                    right[i] = sample.right;
                } else missedThisBlock = true;
            }

            if (!partial_) {
                pending_ = {};
                pending_.client_token = token_;
                pending_.epoch = epoch_;
                pending_.stream_sample = cursor_;
                pending_.engine_frame = active_ ? engineFrame_ : 0;
                pending_.playing = active_ ? 1u : 0u;
                pending_.tempo_milli = tempoMilli;
                if (!toPositionTicks(timing.quarterNotes + i * quartersPerSample,
                                    pending_.position_ticks)) {
                    protocolError_ = true;
                    blockError = BadProtocol;
                    pending_.position_ticks = 0;
                    pending_.playing = 0;
                    pending_.engine_frame = 0;
                    if (active_) stopEpoch();
                }
            }
            if (++partial_ == RB_BLOCK_FRAMES) {
                const int outcome = push(pending_);
                partial_ = 0;
                if (outcome < 0) {
                    protocolError_ = true;
                    blockError = BadProtocol;
                    if (active_) stopEpoch();
                } else if (!outcome) {
                    missedThisBlock = true;
                    if (active_) {
                        failedQueue_ = true;
                        stopEpoch();
                        blockError = Underrun;
                    }
                }
                if (active_) engineFrame_ += RB_BLOCK_FRAMES;
            }
            ++cursor_;
        }
        if (missedThisBlock) ++underruns_;
        if (blockError != Ready) status_ = blockError;
        else if (missedThisBlock) status_ = Underrun;
        else if (active_) status_ = Playing;
        else if (timing.playing) status_ = WaitingForBarOne;
        else status_ = Ready;
        haveLast_ = timing.valid;
        if (timing.projectSamples <= INT64_MAX - frames)
            lastProjectEnd_ = timing.projectSamples + frames;
        else haveLast_ = false;
        lastQuarterEnd_ = timing.quarterNotes + frames * quartersPerSample;
    }
private:
    struct Sample {
        uint64_t tag = 0;
        uint32_t epoch = 0;
        float left = 0, right = 0;
    };
    void beginEpoch() noexcept {
        ++epoch_;
        if (!epoch_) ++epoch_;
        partial_ = 0;
    }
    void stopEpoch() noexcept { beginEpoch(); active_ = false; engineFrame_ = 0; }
    std::array<Sample, cacheSize> cache_ {};
    RBRequest pending_ {};
    uint32_t token_ = 0, epoch_ = 1, partial_ = 0, underruns_ = 0;
    uint64_t cursor_ = 0, playStart_ = 0, engineFrame_ = 0;
    int64_t lastProjectEnd_ = 0;
    double lastQuarterEnd_ = 0;
    bool active_ = false, failedSeek_ = false, failedQueue_ = false;
    bool protocolError_ = false, haveLast_ = false;
    Status status_ = Inactive;
};
}
