#include "stage_sync.h"

#include <DynamicOutput/Output.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <cstring>
#include <vector>

using RC::Output::send;
namespace LogLevel = RC::LogLevel;
using namespace RC::Unreal;

namespace {

// EBoxMovePattern (REDLevelEvent.h)
constexpr uint8_t BOX_MOVE_PATTERN_SLIDE = 2;

std::vector<UObject *> levelEvents() {
  std::vector<UObject *> found;
  UObjectGlobals::FindAllOf(STR("REDLevelEvent"), found);
  std::erase_if(found, [](UObject *o) {
    return !o || o->HasAnyFlags(static_cast<EObjectFlags>(RF_ClassDefaultObject | RF_ArchetypeObject));
  });
  return found;
}

// Call each bound function of one delegate property with `params` (laid out like the delegate's signature).
int broadcast(UObject *level_event, const TCHAR *name, const void *params, size_t params_size) {
  auto *delegate = level_event->GetValuePtrByPropertyNameInChain<FMulticastScriptDelegate>(name);
  if (!delegate) return 0;
  // copy: a bound function may rebind
  std::vector<FScriptDelegate> bound;
  for (int32 i = 0; i < delegate->InvocationList.Num(); ++i) bound.push_back(delegate->InvocationList[i]);

  int called = 0;
  for (const auto &d : bound) {
    UObject *target = d.GetUObject();
    if (!target) continue;
    UFunction *fn = target->GetFunctionByNameInChain(d.GetFunctionName());
    if (!fn) continue;
    // the function's own frame can be larger than the signature (locals live there for BP events)
    std::vector<uint8_t> frame((std::max)(static_cast<size_t>(fn->GetParmsSize()), params_size) + 16, 0);
    std::memcpy(frame.data(), params, params_size);
    target->ProcessEvent(fn, frame.data());
    ++called;
  }
  return called;
}

} // namespace

namespace stage_sync {

int enterBox(int box, bool from_left) {
  // FREDLevelEventBoxMoveStartSignature(int32 Box, bool bFromLeft, EBoxMovePattern pattern)
  struct {
    int32 box;
    bool from_left;
    uint8_t pattern;
  } move{box, from_left, BOX_MOVE_PATTERN_SLIDE};
  // FREDLevelEventBgChangeInBoxMoveSignature(int32 Box, bool bDust)
  struct {
    int32 box;
    bool dust;
  } change{box, false};

  int called = 0;
  for (UObject *e : levelEvents()) {
    called += broadcast(e, STR("OnBoxMoveStart"), &move, sizeof(move));
    called += broadcast(e, STR("OnBoxMoveStart2"), &move, sizeof(move));
    called += broadcast(e, STR("OnBgChangeInBoxMove"), &change, sizeof(change));
    called += broadcast(e, STR("OnBoxMoveEnd"), &move, sizeof(move));
  }
  return called;
}

} // namespace stage_sync
