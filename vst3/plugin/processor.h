#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "adapter.h"
#include <windows.h>

namespace RebirthBridge {
class Processor final : public Steinberg::Vst::AudioEffect {
public:
    Processor();
    ~Processor() override;
    static Steinberg::FUnknown* create(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor);
    }
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown*) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement*,
        Steinberg::int32, Steinberg::Vst::SpeakerArrangement*, Steinberg::int32) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup&) override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool) override;
    Steinberg::tresult PLUGIN_API setProcessing(Steinberg::TBool) override;
    Steinberg::uint32 PLUGIN_API getLatencySamples() override { return RB_LATENCY; }
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData&) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream*) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream*) override;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage*) override;
private:
    void connectBridge();
    void disconnectBridge();
    bool claim();
    void releaseLease();
    void publish(Status);
    Adapter adapter_;
    std::shared_ptr<std::atomic<int32_t>> status_;
    HANDLE mapping_ = nullptr;
    RBShared* shared_ = nullptr;
    uint32_t token_ = 0, lastHeartbeat_ = 0, heartbeatSamples_ = 0;
    uint32_t reportedUnderruns_ = 0;
    bool processing_ = false, configured_ = false;
};
}
