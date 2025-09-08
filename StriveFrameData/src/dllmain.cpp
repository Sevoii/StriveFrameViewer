#include <polyhook2/Detour/x64Detour.hpp>

#include "arcsys.h"
#include "bind_watcher.h"
#include "draw_utils.h"
#include "framebar.h"
#include "hitboxes.h"
#include "menu.h"
#include "sigscan.h"
#include <chrono>
#include <thread>
using namespace std::chrono_literals;

#include <DynamicOutput/DynamicOutput.hpp>
#include <Mod/CppUserModBase.hpp>
#include <UE4SSProgram.hpp>
#include <Unreal/World.hpp>
#include <UnrealDef.hpp>

#define WIN32_LEAN_AND_MEAN
#include "Windows.h"

#define STRIVEFRAMEDATA_API __declspec(dllexport)

/* Definitions */

// Classes
class UREDGameCommon : public Unreal::UObject {};

// Functions
using funcAHUDPostRender_t = void (*)(void *);
using funcACamUpdateCamera_t = void (*)(void *, float);
using funcMatchStart_t = void (*)(AREDGameState_Battle *);
using funcGetGameMode_t = int (*)(UREDGameCommon *);
using funcUpdateBattle_t = void (*)(AREDGameState_Battle *, float);

// Hooks
void hook_AHUDPostRender(void *);
void hook_ACamUpdateCamera(void *, float);
void hook_MatchStart(AREDGameState_Battle *);
void hook_UpdateBattle(AREDGameState_Battle *, float);

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

const void *vtable_hook(const void **vtable, const int index, const void *hook) {
  DWORD old_protect;
  VirtualProtect(&vtable[index], sizeof(void *), PAGE_READWRITE, &old_protect);
  const auto *orig = vtable[index];
  vtable[index] = hook;
  VirtualProtect(&vtable[index], sizeof(void *), old_protect, &old_protect);
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
UE4SSProgram *Program;

// Trackers
class StateMgr {
  UREDGameCommon *GameCommon = nullptr;
  int last_mode = GAME_MODE_DEBUG_BATTLE;
  bool in_allowed_mode = false;
  std::vector<int> allowed_modes = {GAME_MODE_TRAINING, GAME_MODE_REPLAY, GAME_MODE_MISSION, GAME_MODE_UNDECIDED};

public:
  bool checkMode() {
    if (!GameCommon) {
      GameCommon = reinterpret_cast<UREDGameCommon *>(UObjectGlobals::FindFirstOf(FName(STR("REDGameCommon"))));
    }
    if (!GameCommon) return false;
    if (int current_mode = orig_GetGameMode(GameCommon); current_mode != last_mode) {
      //       RC::Output::send<LogLevel::Warning>(STR("Mode Change: {}\n"), current_mode);
      last_mode = current_mode;
      in_allowed_mode = (std::find(allowed_modes.begin(), allowed_modes.end(), current_mode) != allowed_modes.end());
    }
    return in_allowed_mode;
  }
  void checkRound() {
    resetting = false;
    auto *events = asw_events::get();
    auto count = events->event_count;
    if (count > 10) count = 10;
    for (unsigned int idx = 0; idx < count; ++idx) {
      auto e_type = events->events[idx].type;
      // if(e_type < 41) RC::Output::send<LogLevel::Warning>(STR("Event {}: {}\n"), idx, (int)e_type);
      if (e_type == BOM_EVENT_RESET || e_type == BOM_EVENT_DECISION) {
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

class Keybinds {
public:
  static const int BUTTON_COUNT = 9;

  int TOGGLE_FRAMEBAR_BUTTON = VK_F1;
  int TOGGLE_HITBOX_BUTTON = VK_F2;
  int PAUSE_BUTTON = VK_F3;
  int ADVANCE_BUTTON = VK_F4;
  int TOGGLE_MENU_BUTTON = VK_F5;

  int MENU_UP_BUTTON = VK_UP;
  int MENU_DOWN_BUTTON = VK_DOWN;
  int MENU_LEFT_BUTTON = VK_LEFT;
  int MENU_RIGHT_BUTTON = VK_RIGHT;

  int buttons[BUTTON_COUNT] = {TOGGLE_FRAMEBAR_BUTTON, TOGGLE_HITBOX_BUTTON, PAUSE_BUTTON, ADVANCE_BUTTON, TOGGLE_MENU_BUTTON, MENU_UP_BUTTON, MENU_DOWN_BUTTON, MENU_LEFT_BUTTON, MENU_RIGHT_BUTTON};

  int getButtonIndex(int input) {
    for (int i = 0; i < BUTTON_COUNT; i++) {
      if (buttons[i] == input) {
        return i;
      }
    }
    return -1;
  }

  bool setButtonState(int input, bool val) {
    int index = getButtonIndex(input);
    if (index != -1) {
      buttonStates[index] = val;
      return true;
    } else {
      return false;
    }
  }

  bool getButtonState(int input) {
    int index = getButtonIndex(input);
    if (index != -1) {
      return buttonStates[index];
    }
    return false;
  }

  void resetButton(int input) {
    int index = getButtonIndex(input);
    if (index != -1) {
      buttonStates[index] = false;
    }
  }

  void resetButtons() {
    for (int i = 0; i < BUTTON_COUNT; i++) {
      buttonStates[i] = false;
    }
  }

  void loadButtons() {
    for (int i = 0; i < BUTTON_COUNT; i++) {
      BindWatcherI::addToFilter(buttons[i]);
    }
  }

  void checkBinds(bool await = false) {
    auto inputs = BindWatcherI::getInputs(await);
    for (const auto &input : inputs) {
      setButtonState(input, true);
    }
  }

private:
  bool buttonStates[BUTTON_COUNT] = {};
} keybindings;

class PauseManager {
  bool isPaused = false;
  bool shouldAdvance = false;

public:
  bool advancing() const {
    return isPaused && shouldAdvance;
  }

  bool cinematicShouldAdvance() {
    return isPaused && !shouldAdvance;
  }

  void updateBinds() {
    if (keybindings.getButtonState(keybindings.PAUSE_BUTTON)) {
      keybindings.resetButton(keybindings.PAUSE_BUTTON);
      isPaused = !isPaused;
    }

    if (keybindings.getButtonState(keybindings.ADVANCE_BUTTON)) {
      keybindings.resetButton(keybindings.ADVANCE_BUTTON);
      shouldAdvance = true;
    }
  }

  void checkPause() {
    if (advancing()) {
      shouldAdvance = false;
      return;
    }

    updateBinds();

    if (ModMenu::instance().pauseType() == 0) {
      while (isPaused && !shouldAdvance) {
        keybindings.checkBinds(true);
        updateBinds();
      }
    }
  }

  void reset() {
    isPaused = false;
    shouldAdvance = false;
  }
} pause_manager;

class UeTracker {
  Unreal::UObject *worldsets_actor = nullptr;
  Unreal::FProperty *paused_prop = nullptr;
  bool renderingHooked = false;

  void hookFuncs() {
    if (renderingHooked) return;
    renderingHooked = true;

    /* HUD Rendering vtable hook*/
    const auto **AHUD_vtable = (const void **)get_rip_relative(sigscan::get().scan("\x48\x8D\x05\x00\x00\x00\x00\xC6\x83\x18\x03", "xxx????xxxx") + 3);
    orig_AHUDPostRender = (funcAHUDPostRender_t)vtable_hook(AHUD_vtable, 214, hook_AHUDPostRender);

    const auto **ACamera_vtable = (const void **)get_rip_relative(sigscan::get().scan("\x48\x8D\x05\x00\x00\x00\x00\x48\x89\x07\x48\x8D\x8F\x00\x00\x00\x00\xC7\x87\x00\x00\x00\x00\xFF\xFF\xFF\xFF", "xxx????xxxxxx????xx????xxxx") + 3);


    orig_ACamUpdateCamera = (funcACamUpdateCamera_t)vtable_hook(ACamera_vtable, 208, hook_ACamUpdateCamera);
  }
  void findProp() {
    static auto input_class_name = Unreal::FName(STR("REDPlayerController_Battle"), Unreal::FNAME_Add);
    static auto getworldsets_func_name = Unreal::FName(STR("K2_GetWorldSettings"), Unreal::FNAME_Add);

    auto *input_actor = static_cast<Unreal::AActor *>(UObjectGlobals::FindFirstOf(input_class_name));
    if (!input_actor) return;

    auto *world_actor = input_actor->GetWorld();
    if (!world_actor) return;

    auto *getworldsets_func = world_actor->GetFunctionByNameInChain(getworldsets_func_name);
    if (!getworldsets_func) return;

    world_actor->ProcessEvent(getworldsets_func, &worldsets_actor);
    if (!worldsets_actor) return;

    paused_prop = worldsets_actor->GetPropertyByName(STR("PauserPlayerState"));
  }

public:
  void reset() {
    worldsets_actor = nullptr;
    paused_prop = nullptr;
  }
  void setup() {
    reset();
    hookFuncs();
    findProp();
    return;
  }
  bool isUePaused() {
    if (!paused_prop) return false;
    Unreal::AActor **val = static_cast<Unreal::AActor **>(paused_prop->ContainerPtrToValuePtr<void>(worldsets_actor));
    return (bool)val ? ((bool)*val) : false;
  }
} tracker;

/* Hooks */

FrameBar the_bar;

void hook_MatchStart(AREDGameState_Battle *GameState) {
  game_state.matchStarted = true;
  game_state.roundActive = false;
  pause_manager.reset();
  tracker.reset();

  orig_MatchStart(GameState);
}
void hook_AHUDPostRender(void *hud) {
  if (!game_state.checkMode()) {
    orig_AHUDPostRender(hud);
    return;
  }

  if (DrawTool::instance().update(hud)) {
    auto &menu = ModMenu::instance();
    menu.draw();
    if (menu.hitboxEnabled()) drawAllBoxes();
    if (menu.barEnabled()) the_bar.draw();
  }

  if (pause_manager.advancing()) return;
  orig_AHUDPostRender(hud);
}
void hook_ACamUpdateCamera(void *cam, float DeltaTime) {
  if (!game_state.checkMode()) {
    orig_ACamUpdateCamera(cam, DeltaTime);
    return;
  }

  if (pause_manager.advancing()) return;
  orig_ACamUpdateCamera(cam, DeltaTime);
}
void hook_UpdateBattle(AREDGameState_Battle *GameState, float DeltaTime) {
  if (!game_state.checkMode()) {
    orig_UpdateBattle(GameState, DeltaTime);
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(ModMenu::instance().delayAmount()));

  keybindings.checkBinds(false);
  pause_manager.checkPause();

  if (ModMenu::instance().pauseType() == 0 && pause_manager.advancing()) return;
  if (ModMenu::instance().pauseType() == 1 && pause_manager.cinematicShouldAdvance()) return;

  ModMenu::instance().update(PressedKeys{
      keybindings.getButtonState(keybindings.TOGGLE_FRAMEBAR_BUTTON),
      keybindings.getButtonState(keybindings.TOGGLE_HITBOX_BUTTON),
      keybindings.getButtonState(keybindings.TOGGLE_MENU_BUTTON),
      keybindings.getButtonState(keybindings.MENU_UP_BUTTON),
      keybindings.getButtonState(keybindings.MENU_DOWN_BUTTON),
      keybindings.getButtonState(keybindings.MENU_RIGHT_BUTTON),
      keybindings.getButtonState(keybindings.MENU_LEFT_BUTTON)});
  keybindings.resetButtons();

  game_state.checkRound();
  if (game_state.resetting) {
    the_bar.reset();
  }

  orig_UpdateBattle(GameState, DeltaTime);

  if (game_state.matchStarted) {
    game_state.matchStarted = false;
    DrawTool::instance().initialize();
    tracker.setup();
  }

  if (!tracker.isUePaused() && game_state.roundActive) {
    the_bar.addFrame();
  }
}

/* Mod Definition */
class StriveFrameData : public CppUserModBase {
public:
  PLH::x64Detour *UpdateBattle_Detour;
  PLH::x64Detour *MatchStart_Detour;

  StriveFrameData()
  : CppUserModBase() {
    ModName = STR("Strive Frame Data");
    ModVersion = STR("1.04");
    ModDescription = STR("A tool to display frame advantage.");
    ModAuthors = STR("pbozai, Sevoi");
    UpdateBattle_Detour = nullptr;
    MatchStart_Detour = nullptr;
    // Do not change this unless you want to target a UE4SS version
    // other than the one you're currently building with somehow.
    // ModIntendedSDKVersion = STR("2.6");

    Output::send<LogLevel::Verbose>(STR("Strive Frame Viewer Started\n"));
  }

  ~StriveFrameData() override {}

  auto on_update() -> void override {}

  auto on_unreal_init() -> void override {
    Program = &UE4SSProgram::get_program();

    const uint64_t UpdateBattle_Addr = sigscan::get().scan("\x40\x53\x57\x41\x54\x41\x55\x48\x81\xEC\x88\x00\x00\x00", "xxxxxxxxxxxxxx");
    UpdateBattle_Detour = new PLH::x64Detour(UpdateBattle_Addr, reinterpret_cast<uint64_t>(&hook_UpdateBattle), reinterpret_cast<uint64_t *>(&orig_UpdateBattle));
    UpdateBattle_Detour->hook();

    // https://github.com/TheLettuceClub/StriveSAMMI/blob/7292cc225bd4de2b389e92b227c1b2793609c4e1/GGSTSammi/src/dllmain.cpp#L435
    const uint64_t MatchStart_Addr = sigscan::get().scan("\x48\x89\x5c\x24\x10\x55\x56\x57\x41\x54\x41\x55\x41\x56\x41\x57\x48\x8d\x6c\x24\xf0\x48\x81\xec\x10\x01\x00\x00\x0f\x29\xb4\x24", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    MatchStart_Detour = new PLH::x64Detour(MatchStart_Addr, reinterpret_cast<uint64_t>(&hook_MatchStart), reinterpret_cast<uint64_t *>(&orig_MatchStart));
    MatchStart_Detour->hook();

    const uintptr_t GetGameMode_Addr = sigscan::get().scan("\x0F\xB6\x81\xF0\x02\x00\x00\xC3", "xxxxxxxx");
    orig_GetGameMode = reinterpret_cast<funcGetGameMode_t>(GetGameMode_Addr);

    ASWInitFunctions();
    //    bbscript::BBSInitializeFunctions();
    keybindings.loadButtons();
  }
};

extern "C" {
STRIVEFRAMEDATA_API CppUserModBase *start_mod() {
  return new StriveFrameData();
}

STRIVEFRAMEDATA_API void uninstall_mod(CppUserModBase *mod) {
  delete mod;
}
}