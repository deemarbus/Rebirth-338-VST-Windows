#include "processor.h"
#include "ids.h"
#include "status-registry.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;
namespace RebirthBridge {
namespace {
constexpr uint32 stateMagic = 0x52425631;
constexpr uint32 stateVersion = 1;
bool readState(IBStream* stream) {
    if (!stream) return false;
    IBStreamer reader(stream, kLittleEndian);
    uint32 magic = 0, versionNumber = 0;
    return reader.readInt32u(magic) && reader.readInt32u(versionNumber) &&
        magic == stateMagic && versionNumber == stateVersion;
}
}
Processor::Processor() : status_(std::make_shared<std::atomic<int32_t>>(Inactive)) {
    setControllerClass(controllerUID);
    processContextRequirements.needProjectTimeMusic().needTempo().needTransportState();
    adapter_.reset(0);
}
Processor::~Processor() { disconnectBridge(); }
tresult PLUGIN_API Processor::initialize(FUnknown* context) {
    const auto result = AudioEffect::initialize(context);
    if (result != kResultOk) return result;
    addEventInput(STR16("MIDI In"), 16);
    addAudioOutput(STR16("ReBirth stereo"), SpeakerArr::kStereo);
    return kResultOk;
}
tresult PLUGIN_API Processor::terminate() {
    disconnectBridge();
    return AudioEffect::terminate();
}
tresult PLUGIN_API Processor::setBusArrangements(SpeakerArrangement*, int32 inputCount,
    SpeakerArrangement* outputs, int32 outputCount) {
    if (inputCount != 0 || outputCount != 1 || !outputs || outputs[0] != SpeakerArr::kStereo)
        return kResultFalse;
    return AudioEffect::setBusArrangements(nullptr, 0, outputs, outputCount);
}
tresult PLUGIN_API Processor::canProcessSampleSize(int32 size) {
    return size == kSample32 ? kResultTrue : kResultFalse;
}
tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup) {
    configured_ = false;
    if (!SampleRateConverter::isSupportedRate(setup.sampleRate)) {
        publish(BadRate); return kResultFalse;
    }
    if (setup.processMode != kRealtime) { publish(OfflineUnsupported); return kResultFalse; }
    if (setup.symbolicSampleSize != kSample32 || setup.maxSamplesPerBlock <= 0 ||
        setup.maxSamplesPerBlock > 8192) {
        publish(BadProtocol); return kResultFalse;
    }
    const auto result = AudioEffect::setupProcessing(setup);
    if (result != kResultOk) return result;
    if (!sampleRateConverter_.configure(setup.sampleRate, setup.maxSamplesPerBlock)) {
        publish(BadRate);
        return kResultFalse;
    }
    hostSampleRate_ = setup.sampleRate;
    latencySamples_ = sampleRateConverter_.latencySamples();
    configured_ = true;
    return result;
}
uint32 PLUGIN_API Processor::getLatencySamples() { return latencySamples_; }
void Processor::publish(Status status) {
    status_->store(status, std::memory_order_relaxed);
    if (shared_ && token_ && rb_load(&shared_->client_token) == token_)
        rb_store(&shared_->client_status, static_cast<uint32_t>(status));
}
bool Processor::claim() {
    if (!shared_) return false;
    releaseLease();
    const uint32_t candidate = rb_claim_client(shared_);
    if (!candidate) { token_ = 0; publish(InstanceBusy); return false; }
    token_ = candidate;
    /* A new client is the sole reply consumer. Preserve producer counters;
     * old replies carry a different token and are drained in process(). */
    adapter_.reset(token_);
    reportedUnderruns_ = 0;
    lastHeartbeat_ = rb_load(&shared_->heartbeat);
    heartbeatSamples_ = 0;
    publish(Ready);
    return true;
}
void Processor::releaseLease() {
    if (shared_ && token_) {
        if (rb_load(&shared_->client_token) == token_)
            rb_store(&shared_->client_status, Inactive);
        rb_release_client(shared_, token_);
    }
    token_ = 0;
}
void Processor::connectBridge() {
    disconnectBridge();
    mapping_ = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, RB_IPC_NAME);
    if (!mapping_) { publish(MissingWorker); return; }
    shared_ = static_cast<RBShared*>(MapViewOfFile(mapping_, FILE_MAP_READ | FILE_MAP_WRITE,
                                                0, 0, sizeof(RBShared)));
    if (!shared_ || !rb_valid_header(shared_)) {
        disconnectBridge(); publish(BadProtocol); return;
    }
    if (rb_load(&shared_->worker_state) >= RB_STOPPING &&
        rb_load(&shared_->worker_state) <= RB_STOPPED) {
        publish(WorkerFailed); return;
    }
    claim();
}
void Processor::disconnectBridge() {
    releaseLease();
    if (shared_) UnmapViewOfFile(shared_);
    if (mapping_) CloseHandle(mapping_);
    shared_ = nullptr;
    mapping_ = nullptr;
}
tresult PLUGIN_API Processor::setActive(TBool state) {
    if (state) connectBridge();
    else {
        processing_ = false;
        sampleRateConverter_.reset();
        disconnectBridge();
        publish(Inactive);
    }
    /* Scanning and opening a project remain valid without the original app.
     * The editor explicitly reports the missing backend; no sound is claimed. */
    return AudioEffect::setActive(state);
}
tresult PLUGIN_API Processor::setProcessing(TBool state) {
    processing_ = state != 0;
    if (state) {
        if (shared_) claim();
        else publish(MissingWorker);
    } else {
        releaseLease();
        sampleRateConverter_.reset();
        publish(Inactive);
    }
    return AudioEffect::setProcessing(state);
}
tresult PLUGIN_API Processor::process(ProcessData& data) {
    if (data.numSamples == 0) return kResultOk;
    if (data.numSamples < 0 || data.numSamples > 8192 || data.symbolicSampleSize != kSample32)
        return kInvalidArgument;
    if (!data.numOutputs || !data.outputs) return kResultOk;
    auto& output = data.outputs[0];
    if (output.numChannels != 2 || !output.channelBuffers32 ||
        !output.channelBuffers32[0] || !output.channelBuffers32[1]) return kInvalidArgument;
    float* left = output.channelBuffers32[0];
    float* right = output.channelBuffers32[1];
    std::memset(left, 0, size_t(data.numSamples) * sizeof(float));
    std::memset(right, 0, size_t(data.numSamples) * sizeof(float));
    output.silenceFlags = 3;
    if (data.processMode != kRealtime) { publish(OfflineUnsupported); return kResultFalse; }
    if (!configured_ || !processing_) return kResultOk;
    if (!shared_ || !token_) { publish(shared_ ? InstanceBusy : MissingWorker); return kResultOk; }
    if (rb_load(&shared_->client_token) != token_) { publish(InstanceBusy); return kResultOk; }
    if (!rb_valid_header(shared_)) { publish(BadProtocol); return kResultOk; }
    const uint32_t heartbeat = rb_load(&shared_->heartbeat);
    if (heartbeat != lastHeartbeat_) { lastHeartbeat_ = heartbeat; heartbeatSamples_ = 0; }
    else if (heartbeatSamples_ <= static_cast<uint32_t>(hostSampleRate_ * 3.0))
        heartbeatSamples_ += static_cast<uint32_t>(data.numSamples);
    const auto workerState = rb_load(&shared_->worker_state);
    if (heartbeatSamples_ > RB_SAMPLE_RATE * 3u ||
        (workerState >= RB_STOPPING && workerState <= RB_STOPPED)) {
        publish(WorkerFailed); return kResultOk;
    }
    /* Bounded queue work only; no mapping, allocation, wait, file access,
     * native engine call or wall-clock query occurs on the audio thread. */
    for (uint32_t i = 0; i < RB_RING_CAPACITY; ++i) {
        RBReply reply {};
        const int result = rb_pop_reply(shared_, &reply);
        if (!result) break;
        if (result < 0 || !adapter_.accept(reply)) { adapter_.protocolFailure(); break; }
    }
    Timing timing;
    if (data.processContext) {
        const auto& context = *data.processContext;
        const uint32 needed = ProcessContext::kProjectTimeMusicValid | ProcessContext::kTempoValid;
        timing.valid = (context.state & needed) == needed &&
            std::isfinite(context.sampleRate) &&
            std::abs(context.sampleRate - hostSampleRate_) <= 0.001;
        timing.playing = (context.state & ProcessContext::kPlaying) != 0;
        timing.cycle = (context.state & ProcessContext::kCycleActive) != 0;
        timing.tempo = context.tempo;
        timing.sampleRate = context.sampleRate;
        timing.quarterNotes = context.projectTimeMusic;
        timing.projectSamples = context.projectTimeSamples;
    }
    const bool ready = workerState == RB_READY || workerState == RB_PLAYING;
    if (!ready) timing.playing = false;
    const int32 engineFrames = sampleRateConverter_.inputFramesForOutput(data.numSamples);
    if (engineFrames < 0 || engineFrames > static_cast<int32>(engineLeft_.size())) {
        publish(BadProtocol);
        return kResultOk;
    }
    adapter_.process(engineLeft_.data(), engineRight_.data(), engineFrames, timing,
        [this](const RBRequest& request) noexcept { return rb_push_request(shared_, &request); },
        data.numSamples);
    int32 convertedFrames = 0;
    const bool conversionComplete = sampleRateConverter_.process(engineLeft_.data(),
        engineRight_.data(), engineFrames, left, right, data.numSamples, convertedFrames);
    const uint32_t underruns = adapter_.underruns();
    const uint32_t delta = underruns - reportedUnderruns_;
    if (delta) InterlockedExchangeAdd(&shared_->underruns, static_cast<LONG>(delta));
    reportedUnderruns_ = underruns;
    publish(conversionComplete ? adapter_.status() : Underrun);
    if (workerState == RB_REARM_REQUIRED) publish(UnsupportedSeek);
    /* Non-silent finite output only. Setting the flags from actual samples
     * keeps idle/missing-worker processing silent in ordinary host scans. */
    for (int32 i = 0; i < data.numSamples; ++i) {
        if (left[i] != 0.0f) output.silenceFlags &= ~UINT64_C(1);
        if (right[i] != 0.0f) output.silenceFlags &= ~UINT64_C(2);
    }
    return kResultOk;
}
tresult PLUGIN_API Processor::getState(IBStream* state) {
    if (!state) return kInvalidArgument;
    IBStreamer writer(state, kLittleEndian);
    return writer.writeInt32u(stateMagic) && writer.writeInt32u(stateVersion) ? kResultOk : kResultFalse;
}
tresult PLUGIN_API Processor::setState(IBStream* state) { return readState(state) ? kResultOk : kResultFalse; }
tresult PLUGIN_API Processor::notify(IMessage* message) {
    if (message && message->getMessageID() &&
        std::strcmp(message->getMessageID(), "RebirthBridge.StatusSink") == 0) {
        if (processing_) return kResultFalse;
        int64 key = 0;
        if (!message->getAttributes() ||
            message->getAttributes()->getInt("statusKey", key) != kResultOk) return kResultFalse;
        auto status = findStatus(static_cast<uint64_t>(key));
        if (!status) return kResultFalse;
        status->store(status_->load(std::memory_order_relaxed), std::memory_order_relaxed);
        status_ = std::move(status);
        return kResultOk;
    }
    return AudioEffect::notify(message);
}
}
