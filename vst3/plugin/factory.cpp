#include "controller.h"
#include "processor.h"
#include "ids.h"
#include "public.sdk/source/main/pluginfactory_constexpr.h"

BEGIN_FACTORY_DEF("ReBirth Bridge Project", "https://github.com/deemarbus/Rebirth-338-VST-Windows", "", 2)
DEF_CLASS(RebirthBridge::processorUID, Steinberg::PClassInfo::kManyInstances,
    kVstAudioEffectClass, "ReBirth Bridge", 0, "Instrument|Synth", RebirthBridge::version,
    kVstVersionString, RebirthBridge::Processor::create, nullptr)
DEF_CLASS(RebirthBridge::controllerUID, Steinberg::PClassInfo::kManyInstances,
    kVstComponentControllerClass, "ReBirth Bridge Controller", 0, "", RebirthBridge::version,
    kVstVersionString, RebirthBridge::Controller::create, nullptr)
END_FACTORY
