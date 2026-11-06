#include "../adapter.h"
#include <algorithm>
#include <cstdio>
#include <deque>
#include <stdexcept>
#include <vector>

using namespace RebirthBridge;
static unsigned checks = 0;
static void require(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
struct FakeTransport {
    std::deque<RBReply> replies;
    uint64_t requests = 0;
    uint32_t lastTempoMilli = 0;
    int32_t lastPositionTicks = -1;
    int push(const RBRequest& request) {
        ++requests;
        lastTempoMilli = request.tempo_milli;
        lastPositionTicks = request.position_ticks;
        RBReply reply {};
        reply.client_token = request.client_token;
        reply.epoch = request.epoch;
        reply.stream_sample = request.stream_sample;
        reply.state = request.playing ? RB_PLAYING : RB_READY;
        reply.served_channels = 2;
        for (uint32_t i = 0; i < RB_BLOCK_FRAMES; ++i) {
            reply.left[i] = request.playing ? float(request.engine_frame + i + 1) / 1000000.0f : 0;
            reply.right[i] = -reply.left[i];
        }
        replies.push_back(reply);
        return 1;
    }
    void drain(Adapter& adapter) {
        while (!replies.empty()) {
            require(adapter.accept(replies.front()), "valid fake reply refused");
            replies.pop_front();
        }
    }
};
static Timing timing(bool play, int64_t sample, double tempo = 140.0) {
    Timing t;
    t.valid = true;
    t.playing = play;
    t.tempo = tempo;
    t.projectSamples = sample;
    t.quarterNotes = double(sample) * tempo / (60.0 * RB_SAMPLE_RATE);
    return t;
}
int main() {
    try {
        Adapter a;
        a.reset(27);
        FakeTransport transport;
        const int sizes[] = {1, 17, 63, 65, 127, 511, 1023};
        int64_t project = 0;
        std::vector<float> left(8192), right(8192);
        for (int block = 0; block < 200; ++block) {
            const int count = sizes[block % 7];
            transport.drain(a);
            a.process(left.data(), right.data(), count, timing(true, project),
                      [&](const RBRequest& r) { return transport.push(r); });
            for (int i = 0; i < count; ++i) {
                const int64_t outputSample = project + i - RB_LATENCY;
                const float expected = outputSample >= 0 ? float(outputSample + 1) / 1000000.0f : 0;
                require(left[i] == expected && right[i] == -expected,
                        "arbitrary block adapter lost timing/interleave");
            }
            project += count;
        }
        require(a.underruns() == 0, "responsive fake worker underrun");
        require(a.status() == Playing, "playing status missing");

        a.reset(43);
        transport = {};
        a.process(left.data(), right.data(), 3902, timing(true, 0, 120.0),
              [&](const RBRequest& r) { return transport.push(r); });
        require(a.cursor() == 3902, "large host block was not processed");
        require(transport.requests == 60, "large host block request count mismatch");
        require(transport.lastTempoMilli == 120000, "120 BPM was not sent to worker");
        require(transport.lastPositionTicks == 32, "120 BPM request position mismatch");
        transport.drain(a);

        a.reset(67);
        transport = {};
        a.process(left.data(), right.data(), 128, timing(true, 0, 120.0),
              [&](const RBRequest& r) { return transport.push(r); });
        transport.drain(a);
        Timing changedTempo = timing(true, 128, 150.0);
        changedTempo.quarterNotes = 128.0 * 120.0 / (60.0 * RB_SAMPLE_RATE);
        a.process(left.data(), right.data(), 64, changedTempo,
              [&](const RBRequest& r) { return transport.push(r); });
        require(a.status() == Playing, "tempo change interrupted continuous transport");
        require(transport.lastTempoMilli == 150000, "updated BPM was not sent to worker");
        require(transport.lastPositionTicks == 1, "tempo change lost absolute position");
        transport.drain(a);

        /* The transport changes at an offset unrelated to the engine block.
         * Old in-flight packets must never survive the generation boundary. */
        const uint32_t beforeStop = a.epoch();
        a.process(left.data(), right.data(), 123, timing(false, project),
                  [&](const RBRequest& r) { return transport.push(r); });
        require(a.epoch() != beforeStop, "stop did not invalidate epoch");
        for (int i = 0; i < 123; ++i) require(left[i] == 0, "old audio after stop");
        project = 0;
        for (int block = 0; block < 30; ++block) {
            transport.drain(a);
            a.process(left.data(), right.data(), 511, timing(true, project),
                      [&](const RBRequest& r) { return transport.push(r); });
            for (int i = 0; i < 511; ++i) {
                const int64_t position = project + i - RB_LATENCY;
                const float expected = position >= 0 ? float(position + 1) / 1000000.0f : 0;
                require(left[i] == expected, "nonaligned restart leaked/wrong samples");
            }
            project += 511;
        }
        transport.drain(a);
        a.process(left.data(), right.data(), 127, timing(true, project + 10000),
                  [&](const RBRequest& r) { return transport.push(r); });
        require(a.status() == UnsupportedSeek, "seek not rejected");
        for (int i = 0; i < 127; ++i) require(left[i] == 0 && right[i] == 0, "seek leaked audio");
        a.process(left.data(), right.data(), 1, timing(false, 0),
                  [&](const RBRequest& r) { return transport.push(r); });
        Timing bad = timing(true, 0);
        bad.tempo = 600;
        a.process(left.data(), right.data(), 65, bad,
                  [&](const RBRequest& r) { return transport.push(r); });
        require(a.status() == BadTempo, "unsupported tempo not rejected");
        bad = timing(true, 0); bad.cycle = true;
        a.process(left.data(), right.data(), 65, bad,
                  [&](const RBRequest& r) { return transport.push(r); });
        require(a.status() == UnsupportedCycle, "cycle not rejected");
        bad = timing(true, 0); bad.valid = false;
        a.process(left.data(), right.data(), 65, bad,
                  [&](const RBRequest& r) { return transport.push(r); });
        require(a.status() == MissingContext, "missing context not reported");

        a.reset(99);
        transport = {};
        project = -123;
        for (int block = 0; block < 30; ++block) {
            transport.drain(a);
            a.process(left.data(), right.data(), 511, timing(true, project),
                      [&](const RBRequest& r) { return transport.push(r); });
            for (int i = 0; i < 511; ++i) {
                const int64_t position = project + i - RB_LATENCY;
                const float expected = position >= 0 ? float(position + 1) / 1000000.0f : 0;
                require(left[i] == expected, "negative preroll lost bar-one alignment");
            }
            project += 511;
        }
        a.reset(55);
        for (int block = 0; block < 20; ++block)
            a.process(left.data(), right.data(), 511, timing(true, block * 511),
                      [](const RBRequest&) { return 0; });
        require(a.underruns() != 0 && a.status() == Underrun, "full ring not observable");
        for (int i = 0; i < 511; ++i) require(left[i] == 0, "missed reply produced stale audio");
        /* A single lost PLAY packet must not skip an engine frame and then
         * resume the old run when capacity becomes available again. */
        a.reset(71);
        transport = {};
        unsigned attempts = 0;
        bool dropped = false, playingAfterDrop = false;
        for (int block = 0; block < 30; ++block) {
            transport.drain(a);
            a.process(left.data(), right.data(), 511, timing(true, block * 511),
                [&](const RBRequest& request) {
                    if (request.playing && ++attempts == 3) { dropped = true; return 0; }
                    if (dropped && request.playing) playingAfterDrop = true;
                    return transport.push(request);
                });
        }
        require(dropped && !playingAfterDrop, "single dropped PLAY continued with frame gap");
        require(a.status() == Underrun, "dropped PLAY run was not latched");
        a.process(left.data(), right.data(), 17, timing(false, 0),
                  [&](const RBRequest& request) { return transport.push(request); });
        bool restartedAtZero = false;
        for (int block = 0; block < 30; ++block) {
            transport.drain(a);
            a.process(left.data(), right.data(), 511, timing(true, block * 511),
                [&](const RBRequest& request) {
                    if (request.playing && !restartedAtZero) {
                        require(request.engine_frame == 0, "rearmed run did not restart engine frame zero");
                        restartedAtZero = true;
                    }
                    return transport.push(request);
                });
            for (int i = 0; i < 511; ++i) {
                const int64_t position = block * 511 + i - RB_LATENCY;
                const float expected = position >= 0 ? float(position + 1) / 1000000.0f : 0;
                require(left[i] == expected, "queue recovery did not restore exact delayed audio");
            }
        }
        require(restartedAtZero && a.status() == Playing, "stop/restart failed to rearm dropped run");
        a.reset(55);
        RBReply malformed {};
        malformed.client_token = 55;
        malformed.epoch = a.epoch();
        malformed.stream_sample = a.cursor() + 64;
        malformed.served_channels = 2;
        require(!a.accept(malformed), "future/unsolicited reply accepted");
        a.process(left.data(), right.data(), 64, timing(true, 10220),
                  [](const RBRequest&) { return 1; });
        require(a.status() == BadProtocol, "malformed IPC did not latch error");
        std::printf("ADAPTER_TESTS_PASSED checks=%u\n", checks);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ADAPTER_TEST_FAILED after=%u message=%s\n", checks, e.what());
        return 1;
    }
}
