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

// Functions
using funcAHUDPostRender_t = void (*)(void *);
using funcMatchStart_t = void (*)(AREDGameState_Battle *);
using funcUpdateBattle_t = void (*)(AREDGameState_Battle *, float);
using funcUpdateBattleSub_t = void (*)(AREDGameState_Battle *, float, bool);
using funcUpdatePauseState_t = void (*)(AREDGameState_Battle *);

// Hooks
void hook_AHUDPostRender(void *);
void hook_MatchStart(AREDGameState_Battle *);
void hook_UpdateBattle(AREDGameState_Battle *, float);
void hook_UpdateBattleSub(AREDGameState_Battle *, float, bool);
void hook_UpdatePauseState(AREDGameState_Battle *);

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
funcAHUDPostRender_t orig_AHUDPostRender;
funcMatchStart_t orig_MatchStart;
funcUpdateBattle_t orig_UpdateBattle;
funcUpdateBattleSub_t orig_UpdateBattleSub;
funcUpdatePauseState_t orig_UpdatePauseState;

// State Data
UE4SSProgram *Program;

// Trackers
class StateMgr {
  GAME_MODE last_mode = GAME_MODE_INVALID;
  bool in_allowed_mode = false;
  std::vector<GAME_MODE> allowed_modes = {GAME_MODE_TRAINING, GAME_MODE_REPLAY, GAME_MODE_MISSION, GAME_MODE_UNDECIDED};

public:
  bool checkMode() {
    if (GAME_MODE current_mode = getGameMode(); current_mode != last_mode) {
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

  void checkBinds() {
    auto inputs = BindWatcherI::getInputs(false);
    for (const auto &input : inputs) {
      setButtonState(input, true);
    }
  }

private:
  bool buttonStates[BUTTON_COUNT] = {};
} keybindings;

/*
  Pauses the battle through the engine's own pause path, the same one replay mode uses.

  Every tick UpdateOnPreTick calls AREDGameState_Battle::UpdatePauseState, which ends with
  PC->SetPause(pause || bPauseByFrameAdvantage). We set that flag right before UpdatePauseState runs, so the
  whole world pauses (UpdateBattleSub skips its simulation block, particles/animation/camera freeze) while the
  game state keeps ticking, since it is SetTickableWhenPaused. That keeps UpdateBattle (and our input handling)
  running while paused.

  A step lets exactly one tick through unpaused, like CReplayManager's m_StepPlay: F4 is read in UpdateBattle on
  tick N, the next UpdatePauseState (tick N+1) leaves the flag clear so that tick simulates one frame, and the
  flag is held again from tick N+2.
*/
class PauseManager {
  bool paused = false;
  bool step_requested = false;

public:
  void toggle() {
    paused = !paused;
    step_requested = false;
  }

  void step() {
    if (paused) step_requested = true;
  }

  // called once per UpdatePauseState; returns whether to hold the engine paused this tick
  bool consumeTick() {
    const bool hold = paused && !step_requested;
    step_requested = false;
    return hold;
  }

  void reset() {
    paused = false;
    step_requested = false;
  }
} pause_manager;

class UeTracker {
  bool renderingHooked = false;

  void hookFuncs() {
    if (renderingHooked) return;
    renderingHooked = true;

    /* HUD Rendering vtable hook*/
    const auto **AHUD_vtable = (const void **)get_rip_relative(sigscan::get().scan("\x48\x8D\x05\x00\x00\x00\x00\xC6\x83\x18\x03", "xxx????xxxx") + 3);
    orig_AHUDPostRender = (funcAHUDPostRender_t)vtable_hook(AHUD_vtable, 214, hook_AHUDPostRender);
  }

public:
  void setup() {
    hookFuncs();
  }
} tracker;

/* Hooks */

FrameBar the_bar;

void hook_MatchStart(AREDGameState_Battle *GameState) {
  game_state.matchStarted = true;
  game_state.roundActive = false;
  pause_manager.reset();

  orig_MatchStart(GameState);
}
void hook_AHUDPostRender(void *hud) {
  if (!game_state.checkMode()) {
    orig_AHUDPostRender(hud);
    return;
  }

  // draw only on the battle's own HUD, and only once the battle has ticked since MatchStart (matchStarted is cleared
  // by the first UpdateBattle). other HUDs and the loading screen before the battle runs are left alone.
  const bool battle_hud = !game_state.matchStarted && hud == getBattleHUD();
  if (battle_hud && DrawTool::instance().update(hud)) {
    auto &menu = ModMenu::instance();
    menu.draw();
    if (menu.hitboxEnabled()) drawAllBoxes();
    if (menu.barEnabled()) the_bar.draw();
  }

  orig_AHUDPostRender(hud);
}
void hook_UpdatePauseState(AREDGameState_Battle *GameState) {
  // never touch the flag online: there it is the engine's frame-advantage wait for rollback netcode
  if (game_state.checkMode() && !GameState->bNetworkBattle) {
    GameState->bPauseByFrameAdvantage = pause_manager.consumeTick();
  }

  orig_UpdatePauseState(GameState);
}
void hook_UpdateBattle(AREDGameState_Battle *GameState, float DeltaTime) {
  if (!game_state.checkMode()) {
    orig_UpdateBattle(GameState, DeltaTime);
    return;
  }

  // slow motion: stretch every tick by a fixed delay
  std::this_thread::sleep_for(std::chrono::milliseconds(ModMenu::instance().delayAmount()));

  // this still runs while the engine is paused (the battle game state ticks when paused)
  keybindings.checkBinds();
  if (keybindings.getButtonState(keybindings.PAUSE_BUTTON)) pause_manager.toggle();
  if (keybindings.getButtonState(keybindings.ADVANCE_BUTTON)) pause_manager.step();

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
    tracker.setup();
  }
}
void hook_UpdateBattleSub(AREDGameState_Battle *GameState, float DeltaTime, bool bSkip) {
  orig_UpdateBattleSub(GameState, DeltaTime, bSkip);

  // sample after every battle sub-update; the history only records when GameFrame advanced,
  // so engine pauses and skipped updates need no special handling here
  if (game_state.checkMode() && game_state.roundActive) {
    the_bar.addFrame();
  }
}

/* Mod Definition */
class StriveFrameData : public CppUserModBase {
public:
  PLH::x64Detour *UpdateBattle_Detour;
  PLH::x64Detour *UpdateBattleSub_Detour;
  PLH::x64Detour *UpdatePauseState_Detour;
  PLH::x64Detour *MatchStart_Detour;

  StriveFrameData()
  : CppUserModBase() {
    ModName = STR("Strive Frame Data");
    ModVersion = STR("1.04");
    ModDescription = STR("A tool to display frame advantage.");
    ModAuthors = STR("pbozai, Sevoi");
    UpdateBattle_Detour = nullptr;
    UpdateBattleSub_Detour = nullptr;
    UpdatePauseState_Detour = nullptr;
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

    // AREDGameState_Battle::UpdateBattleSub (RVA 0x112B6B0), called from UpdateBattle
    const uint64_t UpdateBattleSub_Addr = sigscan::get().scan(
        "\x48\x8B\xC4\x48\x89\x58\x20\x41\x56\x48\x81\xEC\xA0\x00\x00\x00\x48\x89\x68\x08\x41\x0F\xB6\xE8\x48\x89\x78\x18\x48\x8B\xF9\x0F\x29\x70\xE8\x0F\x28\xF1",
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    UpdateBattleSub_Detour = new PLH::x64Detour(UpdateBattleSub_Addr, reinterpret_cast<uint64_t>(&hook_UpdateBattleSub), reinterpret_cast<uint64_t *>(&orig_UpdateBattleSub));
    UpdateBattleSub_Detour->hook();

    // AREDGameState_Battle::UpdatePauseState (RVA 0x112F1C0), called from UpdateOnPreTick every tick.
    // SwitchPause is inlined in its tail, so this is the hookable entry.
    const uint64_t UpdatePauseState_Addr = sigscan::get().scan(
        "\x48\x89\x5C\x24\x08\x48\x89\x6C\x24\x10\x48\x89\x74\x24\x18\x57\x48\x83\xEC\x30\x48\x8B\xF9\x40\x32\xF6\x48\x8B\x0D\x00\x00\x00\x00",
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxx????");
    UpdatePauseState_Detour = new PLH::x64Detour(UpdatePauseState_Addr, reinterpret_cast<uint64_t>(&hook_UpdatePauseState), reinterpret_cast<uint64_t *>(&orig_UpdatePauseState));
    UpdatePauseState_Detour->hook();

    // https://github.com/TheLettuceClub/StriveSAMMI/blob/7292cc225bd4de2b389e92b227c1b2793609c4e1/GGSTSammi/src/dllmain.cpp#L435
    const uint64_t MatchStart_Addr = sigscan::get().scan("\x48\x89\x5c\x24\x10\x55\x56\x57\x41\x54\x41\x55\x41\x56\x41\x57\x48\x8d\x6c\x24\xf0\x48\x81\xec\x10\x01\x00\x00\x0f\x29\xb4\x24", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    MatchStart_Detour = new PLH::x64Detour(MatchStart_Addr, reinterpret_cast<uint64_t>(&hook_MatchStart), reinterpret_cast<uint64_t *>(&orig_MatchStart));
    MatchStart_Detour->hook();

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
