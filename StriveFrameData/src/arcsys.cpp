#include "arcsys.h"

#include <UnrealDef.hpp>
#include <DynamicOutput/Output.hpp>
#include <Unreal/World.hpp>
#include "sigscan.h"

RC::UClass* Class;

UWorld** GWorld;

using asw_scene_camera_transform_t = void (*)(const asw_scene*, SimpleFVector*, SimpleFVector*, SimpleFVector*);
asw_scene_camera_transform_t asw_scene_camera_transform;

using asw_entity_is_active_t = bool (*)(const asw_entity*, int);
asw_entity_is_active_t asw_entity_is_active;

using asw_entity_is_pushbox_active_t = bool (*)(const asw_entity*);
asw_entity_is_pushbox_active_t asw_entity_is_pushbox_active;

using asw_entity_get_pos_x_t = int (*)(const asw_entity*);
asw_entity_get_pos_x_t asw_entity_get_pos_x;

using asw_entity_get_pos_y_t = int (*)(const asw_entity*);
asw_entity_get_pos_y_t asw_entity_get_pos_y;

using asw_entity_pushbox_width_t = int (*)(const asw_entity*);
asw_entity_pushbox_width_t asw_entity_pushbox_width;

using asw_entity_pushbox_height_t = int (*)(const asw_entity*);
asw_entity_pushbox_height_t asw_entity_pushbox_height;

using asw_entity_pushbox_bottom_t = int (*)(const asw_entity*);
asw_entity_pushbox_bottom_t asw_entity_pushbox_bottom;

using asw_entity_get_pushbox_t = void (*)(const asw_entity*, int*, int*, int*, int*);
asw_entity_get_pushbox_t asw_entity_get_pushbox;

// BATTLE_CRollbackManager (see asw_rollback). RVAs are for the 2026-09-24 build.
using rollback_ctor_t = asw_rollback *(*)(asw_rollback *, int);      // 0xBBFB50, ctor(maxFrame)
using rollback_dtor_t = void (*)(asw_rollback *, unsigned int);      // vtable[0], scalar deleting dtor; flag 1 = free
using rollback_save_backup_t = void (*)(asw_rollback *);              // 0xBD02B0
using rollback_rollback_t = bool (*)(asw_rollback *);                 // 0xBCFE50
rollback_ctor_t rollback_ctor;
rollback_save_backup_t rollback_save_backup;
rollback_rollback_t rollback_rollback;
using rollback_clear_cache_t = void (*)(asw_rollback *);
rollback_clear_cache_t rollback_clear_psc_cache;  // 0xBC6A50 ClearPSCCache
rollback_clear_cache_t rollback_clear_pawn_cache; // 0xBC6CB0 ClearPawnCache

// &m_Replaybuffer[0].m_ReadHead
char *replay_read_heads;

static void InitRewindFunctions() {
  try {
    rollback_ctor = reinterpret_cast<rollback_ctor_t>(sigscan::get().scan(
        "\x48\x89\x5C\x24\x08\x48\x89\x6C\x24\x10\x48\x89\x74\x24\x18\x57\x41\x54\x41\x55\x41\x56\x41\x57\x48\x83\xEC\x20\x45\x33\xF6\x48\x63\xEA\x4C\x8B\xF9\x4C\x8D\x61\x48\x48\x8D\x05",
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"));
    rollback_save_backup = reinterpret_cast<rollback_save_backup_t>(sigscan::get().scan(
        "\x48\x89\x74\x24\x10\x57\x48\x83\xEC\x20\x8B\x41\x20\x48\x8B\xF9\xFF\xC0\x99\xF7\x79\x08\x89\x51\x20\x48\x63\xC2\x48\x69\xF0\x50\x87\x00\x00\x48\x03\x71\x10\xC6\x06\x00",
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"));
    rollback_rollback = reinterpret_cast<rollback_rollback_t>(sigscan::get().scan(
        "\x48\x89\x5C\x24\x18\x48\x89\x6C\x24\x20\x41\x56\x48\x83\xEC\x20\x8B\x81\xA8\x04\x00\x00\x48\x8B\xD9\x85\xC0\x0F\x8E\x00\x00\x00\x00\x01\x41\x38\x8D\x50\x01\xFF\x41\x3C\x44\x8B\x41\x08\x41\x3B\xC0",
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxx"));
    rollback_clear_psc_cache = reinterpret_cast<rollback_clear_cache_t>(sigscan::get().scan(
        "\x48\x89\x5C\x24\x08\x48\x89\x74\x24\x10\x48\x89\x7C\x24\x18\x55\x41\x54\x41\x55\x41\x56\x41\x57\x48\x8D\x6C\x24\xC9\x48\x81\xEC\x90\x00\x00\x00\x45\x33\xE4\xC7\x45\xCB\x01\x00\x00\x00\x4C\x8D\x71\x48\x44\x89",
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"));
    rollback_clear_pawn_cache = reinterpret_cast<rollback_clear_cache_t>(sigscan::get().scan(
        "\x48\x89\x5C\x24\x08\x48\x89\x74\x24\x10\x48\x89\x7C\x24\x18\x4C\x89\x64\x24\x20\x55\x41\x56\x41\x57\x48\x8D\x6C\x24\xB9\x48\x81\xEC\x90\x00\x00\x00\x45\x33\xE4\xC7\x45\xDB\x01\x00\x00\x00\x4C\x8D\xB9\x38\x01",
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"));
    // ReplayManager_Internal::SetReadPosition(round): `lea rbx, [rip+X]` at +0x2C loads &m_Replaybuffer[0].m_ReadHead
    const auto set_read_position = sigscan::get().scan(
        "\x48\x89\x5C\x24\x08\x48\x89\x6C\x24\x10\x48\x89\x74\x24\x18\x48\x89\x7C\x24\x20\x41\x56\x48\x83\xEC\x20\x48\x8B\x01\x48\x63\xDA\xFF\x50\x70\x48\x8B\xCB\x33\xED\x48\xC1\xE1\x08\x48\x8D\x1D",
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    replay_read_heads = static_cast<char *>(get_rip_relative(set_read_position + 0x2F));
  } catch (const std::exception &) {
    rollback_ctor = nullptr;
    rollback_save_backup = nullptr;
    rollback_rollback = nullptr;
    rollback_clear_psc_cache = nullptr;
    rollback_clear_pawn_cache = nullptr;
    replay_read_heads = nullptr;
    RC::Output::send<RC::LogLevel::Warning>(STR("[rewind] signature scan failed, replay rewind disabled\n"));
  }
}

void ASWInitFunctions() {
  InitRewindFunctions();

  auto GWorldPat = sigscan::get().scan("\x0F\x2E\x00\x74\x00\x48\x8B\x1D\x00\x00\x00\x00\x48\x85\xDB\x74", "xx?x?xxx????xxxx");
  auto GWorldAddr = *reinterpret_cast<uint32_t*>(GWorldPat + 8);
  GWorld = reinterpret_cast<UWorld**>(GWorldPat + 12 + GWorldAddr);

  asw_scene_camera_transform = reinterpret_cast<asw_scene_camera_transform_t>(sigscan::get().scan("\x4D\x85\xC0\x74\x15\xF2\x41\x0F", "xxxxxxxx") - 0x56);
  asw_entity_is_active = reinterpret_cast<asw_entity_is_active_t>(sigscan::get().scan("\xF7\x81\x00\x00\x00\x00\x00\x00\x00\x04\x0F", "xx????xxxxx") - 0x12);
  asw_entity_is_pushbox_active = reinterpret_cast<asw_entity_is_pushbox_active_t>(sigscan::get().scan("\x48\x8B\xCB\xE8\x00\x00\x00\x00\x84\xC0\x74\x00\xF7\x83", "xxxx????xxx?xx") - 0x26);
  asw_entity_get_pos_x = reinterpret_cast<asw_entity_get_pos_x_t>(sigscan::get().scan("\xF3\x0F\x59\xC2\xF3\x0F\x5C\xD8\xF3\x0F\x2C\xFB", "xxxxxxxxxxxx") - 0x15D);
  asw_entity_get_pos_y = reinterpret_cast<asw_entity_get_pos_y_t>(sigscan::get().scan("\x3D\x00\x08\x04\x00\x75\x18", "xxxxxxx") - 0x3D);
  asw_entity_pushbox_width = reinterpret_cast<asw_entity_pushbox_width_t>(sigscan::get().scan("\xB8\x78\x00\x00\x00\x48\x83\xC4\x20\x5B\xC3", "xxxxxxxxxxx") - 0x77);
  asw_entity_pushbox_height = reinterpret_cast<asw_entity_pushbox_height_t>(sigscan::get().scan("\xB8\x64\x00\x00\x00\x48\x83\xC4\x20\x5B\xC3", "xxxxxxxxxxx") - 0x77);
  asw_entity_pushbox_bottom = reinterpret_cast<asw_entity_pushbox_bottom_t>(sigscan::get().scan("\x48\x0F\x44\xD8\x8B\x83\x00\x00\x00\x00\x48\x83\xC4\x20\x5B\xC3\x33\xC0\x48\x83\xC4\x20\x5B\xC3", "xxxxxx????xxxxxxxxxxxxxx") - 0x33);
  asw_entity_get_pushbox = reinterpret_cast<asw_entity_get_pushbox_t>(sigscan::get().scan("\x99\x48\x8B\xCB\x2B\xC2\xD1\xF8\x44", "xxxxxxxxx") - 0x5B);
}

GAME_MODE getGameMode() {
  if (!GWorld || !*GWorld) return GAME_MODE_INVALID;
  auto* GameCommon = (*GWorld)->OwningGameInstance;
  if (!GameCommon) return GAME_MODE_INVALID;
  return static_cast<GAME_MODE>(GameCommon->GameMode);
}

AREDGameState_Battle* getGameState() {
  if (Class == nullptr) {
    Class = RC::UObjectGlobals::StaticFindObject<RC::UClass*>(nullptr, nullptr, STR("/Script/RED.REDGameState_Battle"));
  }

  auto GameState = reinterpret_cast<AREDGameState_Battle*>((*GWorld)->GameState);

  if (GameState == nullptr || !GameState->IsA(Class)) return nullptr;
  return GameState;
}

AREDHUD_Battle* getBattleHUD() {
  auto* GameState = getGameState();
  if (!GameState) return nullptr;
  auto* PC = GameState->PrimaryPC;
  return PC ? PC->MyHUD : nullptr;
}

asw_rollback* asw_rollback::get()
{
  auto* GameState = getGameState();
  return GameState ? GameState->Rollback : nullptr;
}

bool asw_rollback::owned_available() {
  return rollback_ctor && rollback_save_backup && rollback_rollback && rollback_clear_psc_cache && rollback_clear_pawn_cache;
}

void asw_rollback::clear_psc_cache() {
  rollback_clear_psc_cache(this);
}

void asw_rollback::clear_pawn_cache() {
  rollback_clear_pawn_cache(this);
}

asw_rollback *asw_rollback::create_owned(int max_frames) {
  if (!owned_available()) return nullptr;
  void *memory = _aligned_malloc(OBJECT_SIZE, 16);
  if (!memory) return nullptr;
  memset(memory, 0, OBJECT_SIZE);
  return rollback_ctor(static_cast<asw_rollback *>(memory), max_frames);
}

void asw_rollback::destroy_owned(asw_rollback *instance) {
  if (!instance) return;
  // run the engine's destructor without its free (flag 0), the memory is ours
  const auto dtor = reinterpret_cast<rollback_dtor_t>((*reinterpret_cast<void ***>(instance))[0]);
  dtor(instance, 0);
  _aligned_free(instance);
}

void asw_rollback::save_backup() {
  rollback_save_backup(this);
}

bool asw_rollback::rollback() {
  return rollback_rollback(this);
}

bool asw_rollback::slot_set(int index) const {
  auto *slots = *reinterpret_cast<char *const *>(reinterpret_cast<const char *>(this) + 0x10);
  if (!slots || index < 0 || index >= MaxBackupFrame) return false;
  return *reinterpret_cast<const bool *>(slots + index * SLOT_SIZE);
}

void asw_rollback::set_slot_flag(int index, bool set) {
  auto *slots = *reinterpret_cast<char **>(reinterpret_cast<char *>(this) + 0x10);
  if (!slots || index < 0 || index >= MaxBackupFrame) return;
  *reinterpret_cast<bool *>(slots + index * SLOT_SIZE) = set;
}

replay_peer *replay_peer::get(int index) {
  if (!replay_read_heads || index < 0 || index >= COUNT) return nullptr;
  // ReadHead is at +0x8 inside the peer
  return reinterpret_cast<replay_peer *>(replay_read_heads - 0x8 + index * STRIDE);
}

asw_engine* asw_engine::get() {
  auto* GameState = getGameState();
  return GameState ? GameState->Engine : nullptr;
}

asw_scene* asw_scene::get() {
  auto* GameState = getGameState();
  return GameState ? GameState->Scene : nullptr;
}

asw_events* asw_events::get() {
  auto* GameState = getGameState();
  return GameState ? GameState->Events : nullptr;
}

void asw_scene::camera_transform(SimpleFVector* delta, SimpleFVector* position, SimpleFVector* angle) const {
  asw_scene_camera_transform(this, delta, position, angle);
}

void asw_scene::camera_transform(SimpleFVector* position, SimpleFVector* angle) const {
  SimpleFVector delta;
  asw_scene_camera_transform(this, &delta, position, angle);
}

asw_state* asw_state::get()
{
  auto* GameState = getGameState();
  return GameState ? GameState->State : nullptr;
}

bool asw_entity::is_active() const {
  // Otherwise returns false during COUNTER
  return cinematic_counter || asw_entity_is_active(this, !is_player);
}

bool asw_entity::is_pushbox_active() const {
  return asw_entity_is_pushbox_active(this);
}

bool asw_entity::is_strike_invuln() const {
  return strike_invuln || wakeup || strike_invuln_time > 0;
}

bool asw_entity::is_throw_invuln() const {
  return throw_invuln || wakeup || throw_invuln_time > 0;
}

int asw_entity::get_pos_x() const {
  return asw_entity_get_pos_x(this);
}

int asw_entity::get_pos_y() const {
  return asw_entity_get_pos_y(this);
}

int asw_entity::pushbox_width() const {
  return asw_entity_pushbox_width(this);
}

int asw_entity::pushbox_height() const {
  return asw_entity_pushbox_height(this);
}

int asw_entity::pushbox_bottom() const {
  return asw_entity_pushbox_bottom(this);
}

void asw_entity::get_pushbox(int* left, int* top, int* right, int* bottom) const {
  asw_entity_get_pushbox(this, left, top, right, bottom);
}

int asw_player::calc_advantage() {
  int opponent_disadvantage = 0;
  int knockdown_time = 11;
  if (opponent->atk_param_ex_defend.atk_knockdown_time != 0 && opponent->atk_param_ex_defend.atk_knockdown_time != 0x7FFFFFFF) {
    knockdown_time = opponent->atk_param_ex_defend.atk_knockdown_time;
  }

  if (opponent->is_roll()) {
    if (opponent->atk_param_ex_defend.atk_soft_knockdown) opponent_disadvantage = 27 + knockdown_time + opponent->atk_param_ex_defend.atk_roll_time - opponent->action_time;
    else opponent_disadvantage = 79 + knockdown_time + opponent->atk_param_ex_defend.atk_roll_time - opponent->action_time;
  } else if (opponent->is_stagger()) {
    int stagger_end_time = 20;
    if (opponent->act_reg_0 == 1) {
      stagger_end_time = 14;
    } else if (opponent->act_reg_0 == 2) {
      stagger_end_time = 17;
    }
    opponent_disadvantage = stagger_end_time + opponent->atk_param_defend.stagger_time - opponent->action_time;
  } else if (opponent->is_guard_crush()) {
    opponent_disadvantage = opponent->atk_param_defend.guard_crush_time + 1 - opponent->action_time;
  } else if (opponent->is_down_bound()) {
    if (opponent->atk_param_ex_defend.atk_soft_knockdown) opponent_disadvantage = 27 + knockdown_time - opponent->action_time;
    else opponent_disadvantage = 45 + knockdown_time - opponent->action_time;
  } else if (opponent->is_quick_down_1()) opponent_disadvantage = 20 + knockdown_time - opponent->action_time;
  else if (opponent->is_quick_down_2()) opponent_disadvantage = 20 - opponent->action_time;
  else if (opponent->is_down_loop()) opponent_disadvantage = 38 + knockdown_time - opponent->action_time;
  else if (opponent->is_down_2_stand()) opponent_disadvantage = 38 - opponent->action_time;
  else if (opponent->is_in_hitstun()) opponent_disadvantage = opponent->hitstun;
  else if (opponent->is_in_blockstun()) opponent_disadvantage = opponent->blockstun;
  else if (!opponent->can_act()) {
    bbscript::code_pointer pointer = bbscript::code_pointer();
    pointer.owner = reinterpret_cast<char*>(opponent);
    pointer.base_ptr = opponent->script_base;
    pointer.ptr = opponent->next_script_cmd;

    pointer.read_script();
    opponent_disadvantage = pointer.state_remaining_time + (sprite_total_duration - sprite_duration);
  }

  opponent_disadvantage += opponent->hitstop - hitstop + slowdown_timer;

  bbscript::code_pointer pointer = bbscript::code_pointer();
  pointer.owner = reinterpret_cast<char*>(this);
  pointer.base_ptr = script_base;
  pointer.ptr = next_script_cmd;

  pointer.read_script();

  if (!can_act()) {
    opponent_disadvantage -= pointer.state_remaining_time + (sprite_total_duration - sprite_duration);
  }

  return opponent_disadvantage;
}

bool asw_player::is_in_hitstun() const {
  switch (cur_cmn_action_id) {
    case ID_CmnActNokezoriHighLv1:
    case ID_CmnActNokezoriHighLv2:
    case ID_CmnActNokezoriHighLv3:
    case ID_CmnActNokezoriHighLv4:
    case ID_CmnActNokezoriHighLv5:
    case ID_CmnActNokezoriLowLv1:
    case ID_CmnActNokezoriLowLv2:
    case ID_CmnActNokezoriLowLv3:
    case ID_CmnActNokezoriLowLv4:
    case ID_CmnActNokezoriLowLv5:
    case ID_CmnActNokezoriCrouchLv1:
    case ID_CmnActNokezoriCrouchLv2:
    case ID_CmnActNokezoriCrouchLv3:
    case ID_CmnActNokezoriCrouchLv4:
    case ID_CmnActNokezoriCrouchLv5:
    case ID_CmnActBDownUpper:
    case ID_CmnActBDownUpperEnd:
    case ID_CmnActBDownDown:
    case ID_CmnActBDownLoop:
    case ID_CmnActFDownUpper:
    case ID_CmnActFDownUpperEnd:
    case ID_CmnActFDownDown:
    case ID_CmnActFDownLoop:
    case ID_CmnActVDownUpper:
    case ID_CmnActVDownUpperEnd:
    case ID_CmnActVDownDown:
    case ID_CmnActVDownLoop:
    case ID_CmnActBlowoff:
    case ID_CmnActBlowoffUpper90:
    case ID_CmnActBlowoffUpper60:
    case ID_CmnActBlowoffUpper30:
    case ID_CmnActBlowoffDown30:
    case ID_CmnActBlowoffDown60:
    case ID_CmnActBlowoffDown90:
    case ID_CmnActKirimomiUpper:
    case ID_CmnActWallBound:
    case ID_CmnActWallBoundDown:
    case ID_CmnActWallHaritsuki:
    case ID_CmnActWallHaritsukiLand:
    case ID_CmnActWallHaritsukiGetUp:
    case ID_CmnActJitabataLoop:
    case ID_CmnActKizetsu:
    case ID_CmnActHizakuzure:
    case ID_CmnActKorogari:
    case ID_CmnActZSpin:
    case ID_CmnActFuttobiFinish:
    case ID_CmnActFuttobiBGTrans:
    case ID_CmnActUkemi:
    case ID_CmnActLandUkemi:
    case ID_CmnActVUkemi:
    case ID_CmnActFUkemi:
    case ID_CmnActBUkemi:
    case ID_CmnActKirimomiLand:
    case ID_CmnActKirimomiLandEnd:
    case ID_CmnActSlideDown:
    case ID_CmnActRushFinishDown:
    case ID_CmnActKirimomiVert:
    case ID_CmnActKirimomiVertEnd:
    case ID_CmnActKirimomiSide:
    case ID_CmnActLockWait:
    case ID_CmnActLockReject:
    case ID_CmnActAirLockWait:
    case ID_CmnActAirLockReject:
    case ID_CmnActExDamage:
    case ID_CmnActExDamageLand:
    case ID_CmnActRushRejectWait:
    case ID_CmnActNokezoriBottomLv1:
    case ID_CmnActNokezoriBottomLv2:
    case ID_CmnActNokezoriBottomLv3:
    case ID_CmnActNokezoriBottomLv4:
    case ID_CmnActNokezoriBottomLv5:
    case ID_CmnActFloatDamage:
    case ID_CmnActGuardLanding:
      return true;
    default:
      return false;
  }
}

bool asw_player::is_in_blockstun() const {
  switch (cur_cmn_action_id) {
    case ID_CmnActMidGuardPre:
    case ID_CmnActMidGuardLoop:
    case ID_CmnActMidGuardEnd:
    case ID_CmnActHighGuardPre:
    case ID_CmnActHighGuardLoop:
    case ID_CmnActHighGuardEnd:
    case ID_CmnActCrouchGuardPre:
    case ID_CmnActCrouchGuardLoop:
    case ID_CmnActCrouchGuardEnd:
    case ID_CmnActAirGuardPre:
    case ID_CmnActAirGuardLoop:
    case ID_CmnActAirGuardEnd:
      return true;
    default:
      return false;
  }
}

bool asw_player::can_act() const {
  return enable_flag & ENABLE_NORMALATTACK;
}

bool asw_player::is_down_bound() const {
  switch (cur_cmn_action_id) {
    case ID_CmnActBDownBound:
    case ID_CmnActFDownBound:
    case ID_CmnActVDownBound:
      return true;
    default:
      return false;
  }
}

bool asw_player::is_quick_down_1() const {
  return cur_cmn_action_id == ID_CmnActQuickDown;
}

bool asw_player::is_quick_down_2() const {
  return cur_cmn_action_id == ID_CmnActQuickDown2Stand;
}

bool asw_player::is_down_loop() const {
  switch (cur_cmn_action_id) {
    case ID_CmnActBDownLoop:
    case ID_CmnActFDownLoop:
    case ID_CmnActVDownLoop:
      return true;
    default:
      return false;
  }
}

bool asw_player::is_down_2_stand() const {
  switch (cur_cmn_action_id) {
    case ID_CmnActBDown2Stand:
    case ID_CmnActFDown2Stand:
      return true;
    default:
      return false;
  }
}

bool asw_player::is_knockdown() const {
  return is_down_bound() || is_quick_down_1() || is_quick_down_2() || is_down_loop() || is_down_2_stand();
}

bool asw_player::is_roll() const {
  return cur_cmn_action_id == ID_CmnActKorogari;
}

bool asw_player::is_stagger() const {
  return cur_cmn_action_id == ID_CmnActJitabataLoop;
}

bool asw_player::is_guard_crush() const {
  switch (cur_cmn_action_id) {
    case ID_CmnActHajikareStand:
    case ID_CmnActHajikareCrouch:
    case ID_CmnActHajikareAir:
      return true;
    default:
      return false;
  }
}

bool asw_player::is_stunned() const {
  return is_in_hitstun() || is_in_blockstun() || is_knockdown() || is_roll() || is_stagger() || is_guard_crush();
}

bool asw_player::is_jump_recovery() const {
  return cur_cmn_action_id == ID_CmnActJumpLanding || cur_cmn_action_id == ID_CmnActLandingStiff;
}

const char* asw_entity::get_BB_state() const {
  if (!first_script_cmd) return nullptr;

  bbscript::opcode value = *reinterpret_cast<bbscript::opcode*>(first_script_cmd);
  if (value != bbscript::opcode::begin_state) return nullptr;
  const char* state_name = first_script_cmd + 4;
  return state_name;
}

bool asw_player::is_stance_idle() const {
  const char* player_state_raw = get_BB_state();
  if (!player_state_raw) return false;
  std::string_view player_state(player_state_raw);

  // Check for Leo stance idles
  if (player_state == "Semuke" || player_state == "SemukeFWalk" || player_state == "SemukeBWalk") {
    return true;
  }
  // Check for HC stance idles
  if (player_state == "Sniper") {
    std::string_view player_sprite(get_sprite_name(),32);
    if(player_sprite.size() < 8) return false;
    std::string_view sprite_stub = player_sprite.substr(0, 7);
    bool busy = false;
    busy |= (sprite_stub == "cos410_" && player_sprite.substr(0, 9) != "cos410_06"); // Startup animation
    busy |= (sprite_stub == "cos425_" || sprite_stub == "cos412_" || sprite_stub == "cos426_" || sprite_stub == "cos424_"); // Shooting animations
    return !busy;
  }
  return false;
}

bool asw_player::is_fdash() const {
  return cur_cmn_action_id == ID_CmnActFDash;
}

bool asw_player::is_move(std::string_view moveId) const {
  const char* player_state_raw = get_BB_state();
  if (!player_state_raw) return false;
  std::string_view player_state(player_state_raw);

  return player_state == moveId;
}

MoveData* asw_player::get_current_move() const {
  int current_skill = action_info_reg.skill_id;
  if(current_skill != -1){
    return &move_datas.moves[current_skill];
  }
  return nullptr;
}