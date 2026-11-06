#include "controller.h"
#include "editor.h"
#include "status.h"
#include "status-registry.h"
#include "base/source/fstreamer.h"
#include <cstring>
namespace RebirthBridge {
Controller::Controller() : status_(std::make_shared<std::atomic<int32_t>>(Inactive)) {
    statusKey_ = registerStatus(status_);
}
Controller::~Controller() { unregisterStatus(statusKey_); }
Steinberg::IPlugView* PLUGIN_API Controller::createView(Steinberg::FIDString name) {
    if (name && std::strcmp(name, Steinberg::Vst::ViewType::kEditor) == 0)
        return new StatusView(status_);
    return nullptr;
}
Steinberg::tresult PLUGIN_API Controller::setComponentState(Steinberg::IBStream* state) {
    if (!state) return Steinberg::kInvalidArgument;
    Steinberg::IBStreamer reader(state, kLittleEndian);
    Steinberg::uint32 magic = 0, version = 0;
    return reader.readInt32u(magic) && reader.readInt32u(version) &&
        magic == 0x52425631 && version == 1 ? Steinberg::kResultOk : Steinberg::kResultFalse;
}
Steinberg::tresult PLUGIN_API Controller::connect(Steinberg::Vst::IConnectionPoint* other) {
    const auto result = EditController::connect(other);
    if (result == Steinberg::kResultOk && statusKey_) {
        auto* message = allocateMessage();
        if (message) {
            message->setMessageID("RebirthBridge.StatusSink");
            if (message->getAttributes()) {
                message->getAttributes()->setInt("statusKey", static_cast<Steinberg::int64>(statusKey_));
                sendMessage(message);
            }
            message->release();
        }
    }
    return result;
}
}
