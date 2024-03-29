#include "drawing.h"
#include <UE4SSProgram.hpp>
#include <DynamicOutput/DynamicOutput.hpp>

#include <format>
#include <set>

/* Debug Stuff */
#if 0
#define DEBUG_PRINT(...) RC::Output::send<LogLevel::Warning>(__VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#endif
#define ENABLE_PRJT_DEBUG false
#define ENABLE_STATE_DEBUG false

/* Combo Triger Settings */
constexpr int COMBO_ENDED_TIME = 20;  // time idle before combo is considered over
constexpr int COMBO_NUM_TIME = 5;     // minimum time of unchanging states to display state length
constexpr int COMBO_TRUNC_TIME = 10;  // max segments for unchanging state, after which the segments are truncated
constexpr double COLOR_DECAY = 0.9f;
constexpr double BAR_TEXT_SIZE = 1.4;

/* Unprojected coordinate Display Settings */
constexpr int FRAME_SEGMENTS = 120;
constexpr int FADE_DISTANCE = 5;
constexpr int SEG_WIDTH = 20;
constexpr int SEG_HEIGHT = 36;
constexpr int SEG_SPACING = 4;
constexpr int BAR_SPACING = 8;
constexpr int BORDER_THICKNESS = 3;
constexpr double OUTLINE_THICKNESS = 1.5;

constexpr int SEG_TOTAL = SEG_SPACING + SEG_WIDTH;
constexpr int BAR_WIDTH = (FRAME_SEGMENTS * SEG_TOTAL) + SEG_SPACING;
constexpr int BAR_HEIGHT = 2 * SEG_SPACING + SEG_HEIGHT;
constexpr int BAR_RIGHT = BAR_WIDTH / 2;
constexpr int BAR_LEFT = -1 * BAR_RIGHT;

constexpr int BAR_TWO_TOP = BAR_SPACING / 2;
constexpr int BAR_TWO_CENTER = BAR_TWO_TOP + (BAR_HEIGHT / 2);
constexpr int BAR_TWO_BOTTOM = BAR_TWO_TOP + BAR_HEIGHT;

constexpr int BAR_ONE_BOTTOM = -1 * BAR_TWO_TOP;
constexpr int BAR_ONE_CENTER = -1 * BAR_TWO_CENTER;
constexpr int BAR_ONE_TOP = -1 * BAR_TWO_BOTTOM;

constexpr int SEGS_TWO_TOP = BAR_TWO_TOP + SEG_SPACING;
constexpr int SEGS_TWO_BOTTOM = BAR_TWO_BOTTOM - SEG_SPACING;

constexpr int SEGS_ONE_TOP = BAR_ONE_TOP + SEG_SPACING;
constexpr int SEGS_ONE_BOTTOM = BAR_ONE_BOTTOM - SEG_SPACING;

constexpr int INFO_ONE_LOC = BAR_ONE_TOP - BAR_HEIGHT;
constexpr int INFO_TWO_LOC = BAR_TWO_BOTTOM + 2;

/* Icon info */
constexpr double ICON_SHEET_SIZE = 4096;
constexpr double ICON_SPRITE_SIZE = 84;
constexpr double ICON_SPRITE_REL = ICON_SPRITE_SIZE / ICON_SHEET_SIZE;

using IconLoc = std::pair<int,int>;
constexpr IconLoc ICON_NULL_LOC = {0,0};
constexpr IconLoc ICON_BUTTON_P_LOC = {6,0};
constexpr IconLoc ICON_BUTTON_K_LOC = {7,0};
constexpr IconLoc ICON_BUTTON_S_LOC = {8,0};
constexpr IconLoc ICON_BUTTON_H_LOC = {9,0};
constexpr IconLoc ICON_BUTTON_D_LOC = {10,0};

constexpr IconLoc ICON_DIR_N_LOC = {0,6};
constexpr IconLoc ICON_DIR_D_LOC = {1,6};
constexpr IconLoc ICON_DIR_DL_LOC = {2,6};
constexpr IconLoc ICON_DIR_L_LOC = {3,6};
constexpr IconLoc ICON_DIR_UL_LOC = {4,6};
constexpr IconLoc ICON_DIR_U_LOC = {5,6};
constexpr IconLoc ICON_DIR_UR_LOC = {6,6};
constexpr IconLoc ICON_DIR_R_LOC = {7,6};
constexpr IconLoc ICON_DIR_DR_LOC = {8,6};
constexpr IconLoc ICON_DIR_LOCS[] = {
  ICON_DIR_N_LOC,
  ICON_DIR_L_LOC, 
  ICON_DIR_R_LOC,
  ICON_DIR_U_LOC,
  ICON_DIR_UL_LOC,
  ICON_DIR_UR_LOC,
  ICON_DIR_D_LOC,
  ICON_DIR_DL_LOC,
  ICON_DIR_DR_LOC
};

/* Projected Display Settings */
constexpr double EXPECTED_DISP_RATIO = 16.0 / 9.0;
constexpr double PROJECTED_RATIO = 0.0003f;
constexpr double CENTER_X_RATIO = 0.5f;
constexpr double CENTER_Y_RATIO = 0.8f;

/* Unreal Structs */
struct FLinearColor {
  float R = 0.f, G = 0.f, B = 0.f, A = 0.f;

  FLinearColor operator*(float mod) const {
    FLinearColor result = *this;
    result.R *= mod;
    result.G *= mod;
    result.B *= mod;
    return result;
  }
};

struct FVector2D {
  float x;
  float y;
};

enum EBlendMode {
  BLEND_Opaque,
  BLEND_Masked,
  BLEND_Translucent,
  BLEND_Additive,
  BLEND_Modulate,
  BLEND_AlphaComposite,
  BLEND_AlphaHoldout,
  BLEND_MAX
};

struct DrawRectParams {
  FLinearColor RectColor;
  float ScreenX;
  float ScreenY;
  float ScreenW;
  float ScreenH;
};

struct DrawTextParams {
  RC::Unreal::FString Text;
  FLinearColor TextColor;
  float ScreenX;
  float ScreenY;
  void* Font;
  float Scale;
  bool bScalePosition;
};

struct DrawIconParams {
  void* Texture;
  float ScreenX;
  float ScreenY;
  float ScreenW;
  float ScreenH;
  float TextureU;
  float TextureV;
  float TextureUWidth;
  float TextureVHeight;
  FLinearColor TintColor;
  EBlendMode BlendMode;
  float Scale;
  bool bScalePosition;
  float Rotation;
  FVector2D RotPivot;
};

/* Unreal Constants */
FLinearColor color_invisible{1.f, 1.f, 1.f, 0.f};
FLinearColor color_white{1.f, 1.f, 1.f, 1.f};
FLinearColor color_black{0.f, 0.f, 0.f, 1.f};
FLinearColor projectile_color{.8f, .1f, .1f, 1.f};
FLinearColor background_color{0.05f, 0.05f, 0.05f, 0.7f};
static const FLinearColor state_colors[] = {
    FLinearColor{.2f, .2f, .2f, .9f},  // Gray
    FLinearColor{.1f, .1f, .8f, .9f},  // Blue
    FLinearColor{.1f, .6f, .1f, .9f},  // Green
    FLinearColor{.7f, .7f, .1f, .9f},  // Yellow
    FLinearColor{.8f, .1f, .1f, .9f},  // Red
    FLinearColor{.8f, .4f, .1f, .9f},  // Orange
    FLinearColor{.8f, .4f, .1f, .9f}   // Orange
};

/* Classes */

class DrawTool {
  double center_x = 0.0;
  double center_y = 0.0;
  double units = 0.0;
  RC::Unreal::UObject* hud_actor = nullptr;
  RC::Unreal::UObject* font = nullptr;
  RC::Unreal::UFunction* drawrect_func = nullptr;
  RC::Unreal::UFunction* drawtext_func = nullptr;

 public:
  DrawTool() = default;
  DrawTool(const GetSizeParams& SizeData, RC::Unreal::UFunction* drawrect, RC::Unreal::UFunction* drawtext, RC::Unreal::UObject* fontobject) {
    hud_actor = nullptr;
    font = fontobject;
    drawrect_func = drawrect;
    drawtext_func = drawtext;
    update(SizeData);
  }

  void update(const GetSizeParams& SizeData, RC::Unreal::UObject* hud = nullptr) {
    hud_actor = hud;
    auto x = SizeData.SizeX;
    auto y = SizeData.SizeY;

    // Strive always renders to a 16:9 region, we need to fix this here since our size data is the "true" screen space window size
    const double actual_ratio = x / y;
    if (actual_ratio > EXPECTED_DISP_RATIO) {  // 21:9 Ultrawide monitor probably
      x = y * EXPECTED_DISP_RATIO;
    } else if (actual_ratio < EXPECTED_DISP_RATIO) {  // 4:3 or 16:10 monitor probably
      y = x / EXPECTED_DISP_RATIO;
    }

    center_x = x * CENTER_X_RATIO;
    center_y = y * CENTER_Y_RATIO;
    units = x * PROJECTED_RATIO;
  }

  void drawRect(int x, int y, int width, int height, const FLinearColor& color) const {
    double prj_x = center_x + (units * x);
    double prj_y = center_y + (units * y);
    double prj_width = units * width;
    double prj_height = units * height;

    DrawRectParams rect_params{color, prj_x, prj_y, prj_width, prj_height};
    hud_actor->ProcessEvent(drawrect_func, &rect_params);
  }

  void drawText(int x, int y, const std::wstring& text, double scale) const {
    RC::Unreal::FString Text(text.c_str());

    double prj_x = center_x + (units * x);
    double prj_y = center_y + (units * y);
    double front_scale = units * scale;

    const auto doCall = [&](const FLinearColor& used_color, const double used_x, const double used_y) {
      DrawTextParams params{Text, used_color, used_x, used_y, font, front_scale, false};
      hud_actor->ProcessEvent(drawtext_func, &params);
    };

    // simulate outline
#if 1  // cardinals
    doCall(color_black, prj_x - OUTLINE_THICKNESS, prj_y);
    doCall(color_black, prj_x, prj_y - OUTLINE_THICKNESS);
    doCall(color_black, prj_x + OUTLINE_THICKNESS, prj_y);
    doCall(color_black, prj_x, prj_y + OUTLINE_THICKNESS);
#endif
#if 1  // diagonals
    doCall(color_black, prj_x - OUTLINE_THICKNESS, prj_y - OUTLINE_THICKNESS);
    doCall(color_black, prj_x + OUTLINE_THICKNESS, prj_y - OUTLINE_THICKNESS);
    doCall(color_black, prj_x + OUTLINE_THICKNESS, prj_y + OUTLINE_THICKNESS);
    doCall(color_black, prj_x - OUTLINE_THICKNESS, prj_y + OUTLINE_THICKNESS);
#endif

    // actual text
    doCall(color_white, prj_x, prj_y);
  }
};

class RedInputChecker {
  unsigned short novel_inputs;
public:
  enum InputFlag {
    IF_Up = 0x1,
    IF_Down = 0x2,
    IF_Left = 0x4,
    IF_Right = 0x8,
    IF_P = 0x10,
    IF_K = 0x20,
    IF_S = 0x40,
    IF_H = 0x80,
    IF_Dust = 0x100,
    IF_ANY = 0x1FF
  };
  RedInputChecker(asw_inputs& inputs){
    novel_inputs = inputs.m_CurRecFlg & (~inputs.m_PreRecFlg);
  }
  bool checkInput(InputFlag flag) const { return novel_inputs & flag; }

  const IconLoc& getDirLoc() const {
    int offset = 0;
    if(checkInput(IF_Up)) offset += 3;
    else if(checkInput(IF_Down)) offset += 6;

    if(checkInput(IF_Left)) offset += 1;
    else if(checkInput(IF_Right)) offset += 2;

    return ICON_DIR_LOCS[offset];
  }
  const IconLoc& getButtonLoc() const {
    if(checkInput(IF_P)) return ICON_BUTTON_P_LOC;
    if(checkInput(IF_K)) return ICON_BUTTON_K_LOC;
    if(checkInput(IF_S)) return ICON_BUTTON_S_LOC;
    if(checkInput(IF_H)) return ICON_BUTTON_H_LOC;
    if(checkInput(IF_Dust)) return ICON_BUTTON_D_LOC;
    return ICON_NULL_LOC;
  }
  void debug() const {
    RC::Output::send<LogLevel::Warning>(STR("Input: {}\n"), novel_inputs);
  }
};

enum PlayerStateType {
  PST_Idle = 0,
  PST_BlockStunned,
  PST_HitStunned,
  PST_Busy,
  PST_Attacking,
  PST_ProjectileAttacking,
  PST_Recovering,
  PST_None,
  PST_End
};

struct ProjectileTracker {
  struct ProjectileInfo {
    asw_entity* direct_parent;
    asw_entity* root_parent;
    bool alive;
    int old;            // 0: brand new (protected), 1: not new, 2: marked old
    int hit_delay = 2;  // this entity has hit something already and should not be considered for further damage

    ProjectileInfo(asw_entity* source) {
      direct_parent = source->parent_obj;
      root_parent = direct_parent;
      alive = true;
      old = 0;
    }
    ProjectileInfo(asw_entity* source, const std::pair<asw_entity*, ProjectileInfo>& parent_info) {
      direct_parent = parent_info.first;
      root_parent = parent_info.second.root_parent;
      alive = true;
      old = parent_info.second.old;
    }
  };

  std::unordered_map<asw_entity*, ProjectileInfo> ownership;

  void markOld(asw_player& player) {
    // assumes a move will never spawn a projectile frame 1
    for (auto& iter : ownership) {
      if (iter.second.root_parent == &player && iter.second.old != 0) {
        iter.second.old = 2;
      }
    }
  }

  void processFrame() {
    // assumes if an entity at &p was destroyed and a new entity is created at &p, any new entities that point to &p as their parent refer to the new one
    const auto engine = asw_engine::get();
    if (!engine) return;

    asw_player* player_one = engine->players[0].entity;
    asw_player* player_two = engine->players[1].entity;
    if (!player_one || !player_two) return;

    // mark all old pointers as dead until seen in the current frame
    for (auto& iter : ownership) {
      iter.second.alive = false;
      if (iter.second.old == 0) iter.second.old = 1;
    }

    // check for in-frame reassignment
    for (int idx = 0; idx < engine->entity_count; ++idx) {
      auto *focus = engine->entities[idx], *parent = focus->parent_obj;
      if (auto iter = ownership.find(focus); iter != ownership.end()) {
        if (iter->second.direct_parent != parent) {
          RC::Output::send<LogLevel::Warning>(STR("PSFP re-assign: {}, Old Parent: {}, New Parent: {}\n"), (void*)focus, (void*)iter->second.direct_parent, (void*)parent);
          ownership.erase(focus);
        } else {
          iter->second.alive = true;
        }
      }
    }

    // create new infos and link parents
    for (int idx = 0; idx < engine->entity_count; ++idx) {
      auto *focus = engine->entities[idx], *parent = focus->parent_obj;
      if (focus == player_one || focus == player_two) continue;
      bool old = false;
      if (ownership.find(focus) == ownership.end()) {
        if (auto parent_iter = ownership.find(parent); parent_iter != ownership.end()) {
          old = parent_iter->second.old;
          ownership.emplace(focus, ProjectileInfo(focus, *parent_iter));
        } else {
          ownership.emplace(focus, ProjectileInfo(focus));
        }
      }
      for (auto& iter : ownership) {
        if (iter.second.root_parent == focus) {
          iter.second.root_parent = parent;
          iter.second.old = old;
        }
      }
    }

    // remove any dead elements
    for (auto first = ownership.begin(), last = ownership.end(); first != last;) {
      if (!first->second.alive) first = ownership.erase(first);
      else ++first;
    }
  }
  void debugDump() {
    constexpr const wchar_t* age_vals[] = {L"NEW", L"EXIST", L"OLD"};

    const auto engine = asw_engine::get();
    if (!engine) return;

    asw_player* player_one = engine->players[0].entity;
    asw_player* player_two = engine->players[1].entity;

    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    auto format = STR("Prjt script:{}, sprite:{}, ptr:{}, par:{}, pt:{}, old:{}, dmg:{}, hit:{}, act:{}\n");
    for (auto& iter : ownership) {
      auto *focus = iter.first, *top_parent = iter.second.root_parent;
      auto bb_script = converter.from_bytes(focus->get_BB_state());
      auto sprite = converter.from_bytes(focus->get_sprite_name());
      void* direct_parent = (void*)iter.second.direct_parent;
      const wchar_t* parent_type = (top_parent == player_one) ? L"ONE" : ((top_parent == player_two) ? L"TWO" : L"FREE");
      const wchar_t* age = age_vals[iter.second.old];
      int dmg = focus->atk_param_hit.damage;
      int hitbox_count = focus->hitbox_count;
      const wchar_t* active = focus->is_active() ? L"Y" : L"N";
      
      RC::Output::send<LogLevel::Warning>(format, bb_script, sprite, (void*)focus, direct_parent, parent_type, age, dmg, hitbox_count, active);
    }
  }

  void reset() {
    ownership.clear();
  }
};

ProjectileTracker ptracker;

/* Known Issues:
    Several Asuka spells will mark inactive and set sprite to null on frame-0 hit:
      "Howling Metron" / "AttackMagic_01"
      "Howling Metron MS Processing" / "AttackMagic_03"
      "Bit Shift Metron" / "AttackMagic_10"
      "RMS Boost Metron" / "AttackMagic_11"
  */
bool shouldBeActive(std::pair<asw_entity* const, ProjectileTracker::ProjectileInfo>& info_pair) {
  const auto bbscript = std::string_view(info_pair.first->get_BB_state());

  // see above, these 4 break the assumptions that cover every other projectile, and I'm fucking tired
  if (bbscript.size() == 14 && bbscript.substr(0, 12) == "AttackMagic_") {
    auto AM_num = bbscript.substr(12, 2);
    if (AM_num == "01" || AM_num == "03" || AM_num == "10" || AM_num == "11") return true;
  }
  /* null check for two reasons:
      strive leaves active but nulled sprites around FOREVER (metron 808 lasts 50 frames)
      we don't want to apply the frame-0 hack to null sprites that haven't come out yet
  */
  const auto sprite = std::string_view(info_pair.first->get_sprite_name());
  if (sprite == "null") return false;
  if (info_pair.first->is_active() && info_pair.first->hitbox_count > 0) return true;

  /* another hack... some projectiles spawn deactived and activate later on
    Baikan's Tatami Mat
    either of Ram's Bajoneto
  */
  // tatami mat is spawned WAY before its active, don't apply frame-0 hack to it
  if(bbscript == "TatamiLandObj" || bbscript == "404wepL_summon_short" || bbscript == "404wepR_summon_short") return false;

  // to compensate for projectiles that "should" be active but are self deactivating frame-0
  // we artificially extend their lifetime just long enough for our purposes.
  int damage = info_pair.first->atk_param_hit.damage;
  if (damage > 0 && info_pair.second.hit_delay) {
    --info_pair.second.hit_delay;
    return true;
  }
  return false;
}

class PlayerState {
  bool any_prjt = false;  // if there are any active projectiles, including old ones

 public:
  int time = -1;
  PlayerStateType type = PST_Idle;
  int state_time = 0;
  bool active_stall = true;

  PlayerState() {}

  PlayerState(asw_player& player, const PlayerState& last, const bool debug = false) {
    time = player.action_time;

    // assumes a sprite won't come out on this frame, this is false
    const bool same_script = time > 1;
    if (!same_script) {
      ptracker.markOld(player);
    }

    const bool normal_canact = player.can_act();
    const bool stance_canact = player.is_stance_idle();
    const bool block_stunned = player.is_in_blockstun() || player.is_stagger() || player.is_guard_crush();
    const bool hit_stunned = player.is_in_hitstun();
    const bool knockdown = player.is_knockdown();
    const bool player_active = player.is_active() && (player.hitbox_count > 0 || player.throw_range >= 0);

    bool projectile_active = false;
    for (auto& iter : ptracker.ownership) {
      if (iter.second.root_parent != &player || !shouldBeActive(iter)) continue;
      any_prjt |= !player_active || (iter.second.old == 2);
      projectile_active |= !(iter.second.old == 2);
    }

    // TODO: account for 0-lifetime projectiles that spawn and immediately self destruct, such as Asuka's 808

    if (normal_canact || stance_canact) type = PST_Idle;
    else if (block_stunned) type = PST_BlockStunned;
    else if (hit_stunned || knockdown) type = PST_HitStunned;
    else if (player_active && last.active_stall) type = PST_Attacking;
    else if (projectile_active && last.active_stall) type = PST_ProjectileAttacking;
    else if (same_script && last.type != PST_Busy) type = PST_Recovering;
    else type = PST_Busy;

    // active stall prevents the first active frame (before the hit is registered) from appearing active
    // this helps match Dustloop and looks more intuitive
    if (player_active || projectile_active) {
      active_stall = true;
    }

    // state_time is used for determining how long was spent in each PST state for a single BB state script
    // type != PST_Busy to prevent interuptible post move animations (that are idle equivalent) or chained stuns from breaking segments
    if ((same_script || type != PST_Busy) && last.type == type) {
      state_time = (last.state_time < 1000) ? last.state_time + 1 : last.state_time;
    } else {
      state_time = 1;
    }

    if (debug) {
      auto format = STR("script:{}, time:{}, sprite:{}, can:{}, stance:{} bstun:{}, hstun:{}, plact:{}, pjact:{}, any:{}, st:{}, cin:{}, hbc:{}, trw:{}\n");
      std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
      std::wstring local_script = converter.from_bytes(player.get_BB_state());
      std::wstring local_sprite = converter.from_bytes(player.get_sprite_name());
      auto nca = normal_canact ? L"Y" : L"N";
      auto sca = stance_canact ? L"Y" : L"N";
      auto bs = block_stunned ? L"Y" : L"N";
      auto hs = hit_stunned ? L"Y" : L"N";
      auto pla = player_active ? L"Y" : L"N";
      auto pja = projectile_active ? L"Y" : L"N";
      auto aja = any_prjt ? L"Y" : L"N";
      auto cin = player.cinematic_counter ? L"Y" : L"N";
      RC::Output::send<LogLevel::Warning>(format, local_script, time, local_sprite, nca, sca, bs, hs, pla, pja, aja, state_time, cin, player.hitbox_count, player.throw_range);
    }
  }

  bool isStunned() const { return type == PST_HitStunned || type == PST_BlockStunned; }
  bool anyProjectiles() const { return (type == PST_ProjectileAttacking) || any_prjt; }
};

struct FrameState {
  struct FrameInfo {
    FLinearColor color;
    FLinearColor border;
    int trunc = 0;
  };
  struct MoveStats {
    int startup = 0;
    std::vector<std::pair<int, int>> actives{{0, 0}};

    void update(const PlayerState& current) {
      if (current.type == PST_Busy) {
        startup = current.state_time + 1;
      } else if (current.type == PST_Attacking || current.type == PST_ProjectileAttacking) {
        // this is a new attack
        if (actives.back().second > 0) actives.push_back({0, 0});
        actives.back().first = current.state_time;
      } else if (current.type == PST_Recovering) {
        actives.back().second = current.state_time;
      }
    }
  };
  std::pair<FrameInfo, FrameInfo> segments[FRAME_SEGMENTS];
  std::pair<MoveStats, MoveStats> working_stats;
  std::pair<MoveStats, MoveStats> displayed_stats;
  std::pair<PlayerState, PlayerState> previous_state;
  std::pair<PlayerState, PlayerState> current_state;

  // last move stats
  bool tracking_advantage = false;
  int advantage = 0;

  bool active = false;
  int current_segment_idx = 0;

  FrameState() { resetFrames(); }

  void resetFrames() {
    current_segment_idx = 0;
    current_state.first.state_time = 1;
    current_state.second.state_time = 1;
    previous_state.first.state_time = 1;
    previous_state.second.state_time = 1;
    working_stats = std::make_pair(MoveStats(), MoveStats());
    for (int idx = 0; idx < FRAME_SEGMENTS; ++idx) {
      segments[idx] = std::make_pair(FrameInfo(), FrameInfo());
    }
  }

  void updateActiveSection(const PlayerState& state, int previous_time, FrameInfo& active, FrameInfo& previous) {
    // previous section has ended
    previous.trunc = 0;

    if (state.state_time == 1) {
      DEBUG_PRINT(STR("New Section for One\n"));
      active.color = state_colors[state.type];
      // ... and was long enough that we want to print length
      if (previous_time > COMBO_NUM_TIME) {
        DEBUG_PRINT(STR("Last section requires truncating\n"));
        previous.trunc = previous_time;
      }
    } else {
      DEBUG_PRINT(STR("Same Section for One\n"));
      // we are drawing this section, fade its color slightly
      // active_segment.color_one = state_colors[type_one] * COLOR_DECAY;
      active.color = previous.color * COLOR_DECAY;
    }
    active.border = state.anyProjectiles() ? projectile_color : color_invisible;
  }

  void processFrame() {
    const auto engine = asw_engine::get();
    if (!engine) return;

    auto& p_one = *engine->players[0].entity;
    auto& p_two = *engine->players[1].entity;

    // update projectiles, even during hitstun
    ptracker.processFrame();
    if constexpr(ENABLE_PRJT_DEBUG){
      RC::Output::send<LogLevel::Warning>(STR("PTracker Pre:\n"));
      ptracker.debugDump();
    }

    // crate updated states
    std::pair<PlayerState, PlayerState> next = {
      PlayerState(p_one, current_state.first, ENABLE_STATE_DEBUG),
      PlayerState(p_two, current_state.second, ENABLE_STATE_DEBUG)
    };

    if(p_one.cinematic_counter || p_two.cinematic_counter){
      if constexpr(ENABLE_STATE_DEBUG) {
        RC::Output::send<LogLevel::Warning>(STR("CINEMATIC SKIP {} {} {} {}\n"), p_one.hitstop, p_one.atk_param_hit.hitstop, p_two.hitstop, p_two.hitstop);
      }
      return;
    }
    // covers COUNTER HIT, this might cover all cases
    else if((next.first.time == current_state.first.time && next.first.time > 1 && (next.second.type == PST_HitStunned || next.second.type == PST_BlockStunned))
      ||    (next.second.time == current_state.second.time && next.second.time > 1 && (next.first.type == PST_HitStunned || next.first.type == PST_BlockStunned))){
      if constexpr(ENABLE_STATE_DEBUG) {
        RC::Output::send<LogLevel::Warning>(STR("STUN SKIP {} {} {} {}\n"), p_one.hitstop, p_one.atk_param_hit.hitstop, p_two.hitstop, p_two.hitstop);
      }
      return;
    }
    else if constexpr(ENABLE_STATE_DEBUG) {
      RC::Output::send<LogLevel::Warning>(STR("KEEP {} {} {} {}\n"), p_one.hitstop, p_one.atk_param_hit.hitstop, p_two.hitstop, p_two.hitstop);
    }

    // shift states
    previous_state.first = current_state.first;
    previous_state.second = current_state.second;
    current_state = next;

    if constexpr(ENABLE_PRJT_DEBUG){
      RC::Output::send<LogLevel::Warning>(STR("PTracker Post:\n"));
      ptracker.debugDump();
    }

    // end combo if we've been idle for a long time
    if (current_state.first.type == PST_Idle && current_state.second.type == PST_Idle) {
      if (!active) {
        return;
      }
      if (current_state.first.state_time > COMBO_ENDED_TIME && current_state.second.state_time > COMBO_ENDED_TIME) {
        DEBUG_PRINT(STR("Combo ended\n"));
        active = false;
        return;
      }
    }
    // reset if this is a new combo
    else if (!active) {
      DEBUG_PRINT(STR("Combo reset\n"));
      resetFrames();
    }

    DEBUG_PRINT(STR("Frame {}\n"), current_segment_idx);

    // update move info
    if (current_state.first.time > 1) {
      working_stats.first.update(current_state.first);
      if (working_stats.first.actives.front().first > 0) {
        displayed_stats.first = working_stats.first;
      }
    }
    else {
      working_stats.first = MoveStats();
    }
    
    if (current_state.second.time > 1) {
      working_stats.second.update(current_state.second);
      if (working_stats.second.actives.front().first > 0) {
        displayed_stats.second = working_stats.second;
      }
    }
    else {
      working_stats.second = MoveStats();
    }
    

    // update advantage
    if (!tracking_advantage) {
      if (current_state.first.isStunned() || current_state.second.isStunned()) {
        advantage = 0;
        tracking_advantage = true;
      }
    } else {
      if (current_state.first.type == PST_Idle) {
        if (current_state.second.type == PST_Idle) {
          tracking_advantage = false;
        } else {
          ++advantage;
        }
      } else {
        if (current_state.second.type == PST_Idle) {
          --advantage;
        } else {
          advantage = 0;
        }
      }
    }

    // update frame display

    auto& active_segment = segments[current_segment_idx];

    // only trigger truncation logic if mid-combo
    if (active) {
      DEBUG_PRINT(STR("Is Active\n"));
      auto& prev_segment = segments[(current_segment_idx + FRAME_SEGMENTS - 1) % FRAME_SEGMENTS];

      // we are truncating, update truncated
      if (current_state.first.state_time > COMBO_TRUNC_TIME && current_state.second.state_time > COMBO_TRUNC_TIME) {
        prev_segment.first.trunc = current_state.first.state_time;
        prev_segment.second.trunc = current_state.second.state_time;
//        DEBUG_PRINT(STR("Truncating 1:{}, 2:{}\n"), prev_segment.trunc_one, prev_segment.trunc_two);
        return;
      }

      updateActiveSection(current_state.first, previous_state.first.state_time, active_segment.first, prev_segment.first);
      updateActiveSection(current_state.second, previous_state.second.state_time, active_segment.second, prev_segment.second);
    } else {
      DEBUG_PRINT(STR("Was not Active\n"));
      active_segment.first.color = state_colors[current_state.first.type];
      active_segment.first.border = current_state.first.anyProjectiles() ? projectile_color : color_invisible;
      active_segment.second.color = state_colors[current_state.second.type];
      active_segment.second.border = current_state.second.anyProjectiles() ? projectile_color : color_invisible;
    }

    // fade effect, clear segments near tail
    auto& fade_segment = segments[(current_segment_idx + FADE_DISTANCE) % FRAME_SEGMENTS];
    fade_segment.first.color.A = 0.f;
    fade_segment.second.color.A = 0.f;

    // advance to next segment
    if (++current_segment_idx >= FRAME_SEGMENTS) {
      DEBUG_PRINT(STR("Cycling segment index\n"));
      current_segment_idx = 0;
    }

    active = true;
  }
};

/* Statics */

FrameState state_data;
DrawTool tool;

/* ABI */

void initFrames(const GetSizeParams& sizedata, RC::Unreal::UFunction* drawrect, RC::Unreal::UFunction* drawtext, RC::Unreal::UObject* fontobject) {
  tool = DrawTool(sizedata, drawrect, drawtext, fontobject);
}

void addFrame() {
  //RedInputChecker(asw_engine::get()->inputs[0]).debug();
  state_data.processFrame();
}

void resetFrames() {
  state_data.active = false;
  state_data.current_state.first = PlayerState();
  state_data.current_state.second = PlayerState();
  ptracker.reset();
}

void drawFrame(const FrameState::FrameInfo& info, int top, int left) {
  if (info.color.A != 0.f) {
    if (info.border.A != 0.f) {
      tool.drawRect(left - BORDER_THICKNESS, top - BORDER_THICKNESS, SEG_WIDTH + 2 * BORDER_THICKNESS, SEG_HEIGHT + 2 * BORDER_THICKNESS, info.border);
    }
    tool.drawRect(left, top, SEG_WIDTH, SEG_HEIGHT, info.color);
    if (info.trunc > 0) {
      auto text = std::to_wstring(info.trunc);
      int text_left = left - (text.size() - 1) * 16 + 2;
      tool.drawText(text_left, top, text, BAR_TEXT_SIZE);
    }
  }
}

std::wstring MakeStatsText(const FrameState::MoveStats& stats, int advantage) {
  std::wstringstream builder;
  builder << L"Startup: " << stats.startup << ", Active: ";
  for(int idx = 0; idx < stats.actives.size() - 1; ++idx){
    auto& focus = stats.actives[idx];
    builder << focus.first << L"(" << focus.second << ")";
  }
  auto& last = stats.actives.back();
  builder << last.first << ", Recovery: " << last.second << L", Advantage: " << advantage;
  return builder.str();
}

void drawFrames(RC::Unreal::UObject* hud, const GetSizeParams& sizedata) {
  tool.update(sizedata, hud);

  auto player_one_info = MakeStatsText(state_data.displayed_stats.first, state_data.advantage);
  auto player_two_info = MakeStatsText(state_data.displayed_stats.second, -state_data.advantage);

  tool.drawText(BAR_LEFT, INFO_ONE_LOC, player_one_info, BAR_TEXT_SIZE);
  tool.drawText(BAR_LEFT, INFO_TWO_LOC, player_two_info, BAR_TEXT_SIZE);

  tool.drawRect(BAR_LEFT, BAR_ONE_TOP, BAR_WIDTH, BAR_HEIGHT, background_color);
  tool.drawRect(BAR_LEFT, BAR_TWO_TOP, BAR_WIDTH, BAR_HEIGHT, background_color);

  for (int idx = 0; idx < FRAME_SEGMENTS; ++idx) {
    int left = BAR_LEFT + (SEG_TOTAL * idx) + SEG_SPACING;
    const auto& segment = state_data.segments[idx];

    drawFrame(segment.first, SEGS_ONE_TOP, left);
    drawFrame(segment.second, SEGS_TWO_TOP, left);
  }
}