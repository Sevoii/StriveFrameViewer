#include <polyhook2/Detour/x64Detour.hpp>
#include "sigscan.h"
#include "arcsys.h"
#include "drawing.h"
#include "bind_watcher.h"

#include <UE4SSProgram.hpp>
#include <UnrealDef.hpp>
#include <Unreal/World.hpp>
#include <Mod/CppUserModBase.hpp>
#include <DynamicOutput/DynamicOutput.hpp>

#define WIN32_LEAN_AND_MEAN
#include "Windows.h"

#define STRIVEFRAMEDATA_API __declspec(dllexport)

/* Definitions */

// Classes
class UREDGameCommon : public Unreal::UObject {};

// Functions
using funcAHUDPostRender_t = void (*)(void*);
using funcACamUpdateCamera_t = void (*)(void*, float);
using funcMatchStart_t = void (*)(AREDGameState_Battle*);
using funcGetGameMode_t = int (*)(UREDGameCommon*);
using funcUpdateBattle_t = void (*)(AREDGameState_Battle*, float);

// Hooks
void hook_AHUDPostRender(void*);
void hook_ACamUpdateCamera(void*, float);
void hook_MatchStart(AREDGameState_Battle*);
void hook_UpdateBattle(AREDGameState_Battle*, float);

// Enums
enum GAME_MODE : int32_t {
  GAME_MODE_DEBUG_BATTLE = 0x0,
  GAME_MODE_ADVERTISE = 0x1,
  GAME_MODE_MAINTENANCEVS = 0x2,
  GAME_MODE_ARCADE = 0x3,
  GAME_MODE_MOM = 0x4,
  GAME_MODE_SPARRING = 0x5,
  GAME_MODE_VERSUS = 0x6,
  GAME_MODE_VERSUS_PREINSTALL = 0x7,
  GAME_MODE_TRAINING = 0x8,
  GAME_MODE_TOURNAMENT = 0x9,
  GAME_MODE_RANNYU_VERSUS = 0xA,
  GAME_MODE_EVENT = 0xB,
  GAME_MODE_SURVIVAL = 0xC,
  GAME_MODE_STORY = 0xD,
  GAME_MODE_MAINMENU = 0xE,
  GAME_MODE_TUTORIAL = 0xF,
  GAME_MODE_LOBBYTUTORIAL = 0x10,
  GAME_MODE_CHALLENGE = 0x11,
  GAME_MODE_KENTEI = 0x12,
  GAME_MODE_MISSION = 0x13,
  GAME_MODE_GALLERY = 0x14,
  GAME_MODE_LIBRARY = 0x15,
  GAME_MODE_NETWORK = 0x16,
  GAME_MODE_REPLAY = 0x17,
  GAME_MODE_LOBBYSUB = 0x18,
  GAME_MODE_MAINMENU_QUICK_BATTLE = 0x19,
  GAME_MODE_UNDECIDED = 0x1A,
  GAME_MODE_INVALID = 0x1B,
};

/* Utilities */

const void* vtable_hook(const void** vtable, const int index, const void* hook) {
  DWORD old_protect;
  VirtualProtect(&vtable[index], sizeof(void*), PAGE_READWRITE, &old_protect);
  const auto* orig = vtable[index];
  vtable[index] = hook;
  VirtualProtect(&vtable[index], sizeof(void*), old_protect, &old_protect);
  return orig;
}

/* Variables */

// Functions
funcGetGameMode_t orig_GetGameMode;
funcAHUDPostRender_t orig_AHUDPostRender;
funcACamUpdateCamera_t orig_ACamUpdateCamera;
funcMatchStart_t orig_MatchStart;
funcUpdateBattle_t orig_UpdateBattle;

// State Data
UE4SSProgram* Program;

bool logEvents = false;

// Trackers
class StateMgr {
  UREDGameCommon* GameCommon = nullptr;
  ;
  int last_mode = GAME_MODE_DEBUG_BATTLE;
  bool in_allowed_mode = false;
  std::vector<int> allowed_modes = {GAME_MODE_TRAINING, GAME_MODE_REPLAY};

 public:
  bool checkMode() {
    if (!GameCommon) {
      GameCommon = reinterpret_cast<UREDGameCommon*>(UObjectGlobals::FindFirstOf(FName(STR("REDGameCommon"))));
    }
    if (!GameCommon) return false;
    if (int current_mode = orig_GetGameMode(GameCommon); current_mode != last_mode) {
      // RC::Output::send<LogLevel::Warning>(STR("Mode Change: {}\n"), current_mode);
      last_mode = current_mode;
      in_allowed_mode = (std::find(allowed_modes.begin(), allowed_modes.end(), current_mode) != allowed_modes.end());
    }
    return in_allowed_mode;
  }
  void checkRound() {
    resetting = false;
    auto* events = asw_events::get();
    auto count = events->event_count;
    if (count > 10) count = 10;
    for (unsigned int idx = 0; idx < count; ++idx) {
      auto e_type = events->events[idx].type;
      //if(e_type < 41) RC::Output::send<LogLevel::Warning>(STR("Event {}: {}\n"), idx, (int)e_type);
      if(e_type == BOM_EVENT_RESET || e_type == BOM_EVENT_DECISION) {
        resetting = true;
        roundActive = false;
      }
      if (e_type == BOM_EVENT_BATTLE_START) {
        resetting = true;
        roundActive = true;
      };
    }
  }

  bool resetting = false;
  bool matchStarted = true;
  bool roundActive = false;

} game_state;

struct ConfigMgr {
  bool overlayEnabled = true;
  bool truncEnabled = true;
  bool dustloopEnabled = false;
  bool fadeEnabled = true;
  int resetButton = 7;
} cfg;

class UeTracker {
  Unreal::AActor* input_actor = nullptr;
  Unreal::UObject* hud_actor = nullptr;
  Unreal::UObject* player_actor = nullptr;
  Unreal::UObject* font_object = nullptr;
  Unreal::UObject* worldsets_actor = nullptr;
  Unreal::UWorld* world_actor = nullptr;

  Unreal::UFunction* drawrect_func = nullptr;
  Unreal::UFunction* drawtext_func = nullptr;
  Unreal::UFunction* getplayer_func = nullptr;
  Unreal::UFunction* getsize_func = nullptr;
  Unreal::UFunction* getworldsets_func = nullptr;

  Unreal::FProperty* paused_prop = nullptr;

  GetSizeParams SizeData;

  bool renderingHooked = false;
  bool success = false;

  void hookHUD() {
    if (renderingHooked) return;
    renderingHooked = true;

    /* HUD Rendering vtable hook*/
    const auto** AHUD_vtable = (const void**)get_rip_relative(sigscan::get().scan("\x48\x8D\x05\x00\x00\x00\x00\xC6\x83\x18\x03", "xxx????xxxx") + 3);
    orig_AHUDPostRender = (funcAHUDPostRender_t)vtable_hook(AHUD_vtable, 214, hook_AHUDPostRender);

    const auto** ACamera_vtable = (const void**)get_rip_relative(sigscan::get().scan("\x48\x8D\x05\x00\x00\x00\x00\x48\x8d\x8f\x20\x28\x00\x00", "xxx????xxxxxxx") + 3);
    orig_ACamUpdateCamera = (funcACamUpdateCamera_t)vtable_hook(ACamera_vtable, 208, hook_ACamUpdateCamera);
  }
  void hookCameras() {
    static auto ucam_class_name = Unreal::FName(STR("Font"), Unreal::FNAME_Add);
  }
  bool setupFont() {
    static auto ufont_class_name = Unreal::FName(STR("Font"), Unreal::FNAME_Add);
    std::vector<RC::Unreal::UObject*> all_fonts;
    UObjectGlobals::FindAllOf(ufont_class_name, all_fonts);
    for (auto* font : all_fonts) {
      RC::Output::send<LogLevel::Warning>(STR("-font: {}\n"), font->GetName());
      if (font->GetName() == L"RobotoDistanceField") {
        font_object = font;
        return true;
      }
    }
    return false;
  }
  bool setupHud() {
    static auto hud_class_name = Unreal::FName(STR("REDHUD_Battle"), Unreal::FNAME_Add);
    static auto hud_drawrect_func_name = Unreal::FName(STR("DrawRect"), Unreal::FNAME_Add);
    static auto hud_drawtext_func_name = Unreal::FName(STR("DrawText"), Unreal::FNAME_Add);
    static auto hud_getplayer_func_name = Unreal::FName(STR("GetOwningPlayerController"), Unreal::FNAME_Add);
    static auto player_getsize_func_name = Unreal::FName(STR("GetViewportSize"), Unreal::FNAME_Add);

    hud_actor = UObjectGlobals::FindFirstOf(hud_class_name);
    if (!hud_actor) return false;

    drawrect_func = hud_actor->GetFunctionByNameInChain(hud_drawrect_func_name);
    drawtext_func = hud_actor->GetFunctionByNameInChain(hud_drawtext_func_name);
    getplayer_func = hud_actor->GetFunctionByNameInChain(hud_getplayer_func_name);

    if (!drawrect_func || !drawtext_func || !getplayer_func) return false;

    hud_actor->ProcessEvent(getplayer_func, &player_actor);
    if (!player_actor) return false;

    getsize_func = player_actor->GetFunctionByNameInChain(player_getsize_func_name);
    if (!getsize_func) return false;

    player_actor->ProcessEvent(getsize_func, &SizeData);

    return true;
  }
  bool setupState() {
    static auto input_class_name = Unreal::FName(STR("REDPlayerController_Battle"), Unreal::FNAME_Add);
    static auto getworldsets_func_name = Unreal::FName(STR("K2_GetWorldSettings"), Unreal::FNAME_Add);

    input_actor = static_cast<Unreal::AActor*>(UObjectGlobals::FindFirstOf(input_class_name));
    if (!input_actor) return false;

    world_actor = input_actor->GetWorld();
    if (!world_actor) return false;

    getworldsets_func = world_actor->GetFunctionByNameInChain(getworldsets_func_name);
    if (!getworldsets_func) return false;

    world_actor->ProcessEvent(getworldsets_func, &worldsets_actor);
    if (!worldsets_actor) return false;

    paused_prop = worldsets_actor->GetPropertyByName(STR("PauserPlayerState"));
    if (!paused_prop) return false;

    return true;
  }

 public:
  bool isValid() { return success; }
  void reset() {
    success = false;
    input_actor = nullptr;
    hud_actor = nullptr;
    player_actor = nullptr;
    font_object = nullptr;
    worldsets_actor = nullptr;
    world_actor = nullptr;

    drawrect_func = nullptr;
    drawtext_func = nullptr;
    getplayer_func = nullptr;
    getsize_func = nullptr;
    getworldsets_func = nullptr;

    paused_prop = nullptr;
  }
  void setup() {
    hookHUD();
    success = true;
    success &= setupFont();
    success &= setupHud();
    success &= setupState();
    if (!success) return;

    initFrames(SizeData, drawrect_func, drawtext_func, font_object);
    return;
  }

  void draw(void* validate_hud) {
    if (!isValid()) return;
    if (hud_actor == validate_hud) {
      player_actor->ProcessEvent(getsize_func, &SizeData);
      drawFrames(hud_actor, SizeData);
    } else {
      RC::Output::send<LogLevel::Warning>(STR("Resetting: HUD actor expired \n"));
      reset();
    }
  }
  bool isUePaused() {
    if (!isValid()) return false;
    Unreal::AActor** val = static_cast<Unreal::AActor**>(paused_prop->ContainerPtrToValuePtr<void>(worldsets_actor));
    return (bool)val ? ((bool)*val) : false;
  }
} tracker;

class AsyncInputChecker {
  int pauseButton = VK_F2;
  int advanceButton = VK_F3;
  bool isPaused = false;
  bool shouldAdvance = false;

  void checkBinds(bool await = false) {
    auto inputs = BindWatcherI::getInputs(await);
    for (const auto& input : inputs) {
      if (input == pauseButton) {
        isPaused = !isPaused;
      } else if (input == advanceButton) {
        //        shouldAdvance = true;

        logEvents = !logEvents;

        Output::send<LogLevel::Verbose>(STR("Test Frame 1\n"));

        Output::send<LogLevel::Verbose>(tracker.isUePaused() ? STR("Tracker UE Paused\n") : STR("Tracker Not UE Pasued\n"));
        Output::send<LogLevel::Verbose>(game_state.roundActive ? STR("Gamestate Round Active\n") : STR("Gamestate Round Not Active\n"));
        addFrame();

      }
    }
  }

public:
  bool advancing() const { return isPaused && shouldAdvance; }
  void pause() {
    if(advancing()){
      shouldAdvance = false;
      return;
    }
    checkBinds();
    while (isPaused && !shouldAdvance) {
      checkBinds(true);
    }
  }
  void reset() {
    isPaused = false;
    shouldAdvance = false;
  }
} input_checker;

/* Hooks */

void hook_MatchStart(AREDGameState_Battle* GameState) {
  game_state.matchStarted = true;
  game_state.roundActive = false;
  input_checker.reset();
  tracker.reset();

  orig_MatchStart(GameState);
}
void hook_AHUDPostRender(void* hud) {
  if(input_checker.advancing()) return;
  orig_AHUDPostRender(hud);
  if (tracker.isValid() && cfg.overlayEnabled) {
    tracker.draw(hud);
  }
}
void hook_ACamUpdateCamera(void* cam, float DeltaTime) {
  if(input_checker.advancing()) return;
  orig_ACamUpdateCamera(cam, DeltaTime);
}
void hook_UpdateBattle(AREDGameState_Battle* GameState, float DeltaTime) {
  if (!game_state.checkMode()) {
    orig_UpdateBattle(GameState, DeltaTime);
    return;
  }

  input_checker.pause();
  if(input_checker.advancing()) return;

  game_state.checkRound();
  if (game_state.resetting) {
    resetFrames();
  }

  orig_UpdateBattle(GameState, DeltaTime);

  if (game_state.matchStarted) {
    game_state.matchStarted = false;
    tracker.setup();
  }

  if (!tracker.isUePaused() && game_state.roundActive) {
    addFrame();
  }
}

/* Mod Definition */
class StriveFrameData : public CppUserModBase {
 public:
  PLH::x64Detour* UpdateBattle_Detour;
  PLH::x64Detour* MatchStart_Detour;

  StriveFrameData()
  : CppUserModBase() {
    ModName = STR("Strive Frame Data");
    ModVersion = STR("1.04");
    ModDescription = STR("A tool to display frame advantage.");
    ModAuthors = STR("pbozai");
    UpdateBattle_Detour = nullptr;
    MatchStart_Detour = nullptr;

    Output::send<LogLevel::Verbose>(STR("Strive Frame Viewer Started\n"));
    Output::send<LogLevel::Verbose>(STR("Strive Frame Viewer Started\n"));
  }

  ~StriveFrameData() override {}

  auto on_update() -> void override {}

  auto on_unreal_init() -> void override {
    Program = &UE4SSProgram::get_program();

    const uint64_t UpdateBattle_Addr = sigscan::get().scan("\x40\x53\x57\x41\x54\x41\x55\x48\x81\xEC\x88\x00\x00\x00", "xxxxxxxxxxxxxx");
    UpdateBattle_Detour = new PLH::x64Detour(UpdateBattle_Addr, reinterpret_cast<uint64_t>(&hook_UpdateBattle), reinterpret_cast<uint64_t*>(&orig_UpdateBattle));
    UpdateBattle_Detour->hook();

    const uint64_t MatchStart_Addr = sigscan::get().scan("\x44\x39\x68\x08\x74\x00\x48\x8B\x18\xEB\x00\x48\x8B\xDF", "xxxxx?xxxx?xxx") - 0x8D;
    MatchStart_Detour = new PLH::x64Detour(MatchStart_Addr, reinterpret_cast<uint64_t>(&hook_MatchStart), reinterpret_cast<uint64_t*>(&orig_MatchStart));
    MatchStart_Detour->hook();

    const uintptr_t GetGameMode_Addr = sigscan::get().scan("\x0F\xB6\x81\xF0\x02\x00\x00\xC3", "xxxxxxxx");
    orig_GetGameMode = reinterpret_cast<funcGetGameMode_t>(GetGameMode_Addr);

    ASWInitFunctions();
    bbscript::BBSInitializeFunctions();

    BindWatcherI::addToFilter(VK_F2);
    BindWatcherI::addToFilter(VK_F3);
  }
};

extern "C" {
STRIVEFRAMEDATA_API CppUserModBase* start_mod() {
  return new StriveFrameData();
}

STRIVEFRAMEDATA_API void uninstall_mod(CppUserModBase* mod) {
  delete mod;
}
}