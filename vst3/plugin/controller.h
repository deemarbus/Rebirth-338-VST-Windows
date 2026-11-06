#pragma once
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "status.h"
namespace RebirthBridge {
class Controller final : public Steinberg::Vst::EditController {
public:
    Controller();
    ~Controller() override;
    static Steinberg::FUnknown* create(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller);
    }
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString) override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream*) override;
    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint*) override;
private:
    std::shared_ptr<std::atomic<int32_t>> status_;
    uint64_t statusKey_ = 0;
};
}
