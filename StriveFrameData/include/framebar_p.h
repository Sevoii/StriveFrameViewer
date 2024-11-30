#pragma once
#include "framebar.h"

#include "arcsys.h"
#include "draw_utils.h"
#include "menu.h"

#include <utility>

constexpr int FRAME_SEGMENTS = 120;

enum PlayerStateType {
  PST_Idle = 0,
  PST_BlockStunned,
  PST_HitStunned,
  PST_Busy,
  PST_Attacking,
  PST_ProjectileAttacking,
  PST_Recovering,
  PST_Dash,
  PST_None,
  PST_End
};

enum ModifierType {
  MT_None        = 0x0,
  MT_Cancellable = 0x1,
  MT_Projectile  = 0x2,
  MT_SegmentEnd  = 0x4,
  MT_CrossUp     = 0x8
};
inline ModifierType operator|(ModifierType a, ModifierType b) { return (ModifierType)((int)a | (int)b); }
inline ModifierType operator&(ModifierType a, ModifierType b) { return (ModifierType)((int)a & (int)b); }
inline ModifierType &operator|=(ModifierType &a, ModifierType b) { return (a = a | b); }
inline ModifierType &operator&=(ModifierType &a, ModifierType b) { return (a = a & b); }

struct ProjectileTracker {
  struct ProjectileInfo {
    asw_entity *direct_parent;
    asw_entity *root_parent;
    bool alive;
    int old;           // 0: brand new (protected), 1: not new, 2: marked old
    int hit_delay = 2; // this entity has hit something already and should not be considered for further damage

    ProjectileInfo(asw_entity *source) {
      direct_parent = source->parent_obj;
      root_parent   = direct_parent;
      alive         = true;
      old           = 0;
    }
    ProjectileInfo(asw_entity *source, const std::pair<asw_entity *, ProjectileInfo> &parent_info) {
      direct_parent = parent_info.first;
      root_parent   = parent_info.second.root_parent;
      alive         = true;
      old           = parent_info.second.old;
    }
  };

  std::unordered_map<asw_entity *, ProjectileInfo> ownership;

  void markOld(asw_player &player);
  void processFrame();
  void debugDump();
  void reset() { ownership.clear(); }
};

struct PlayerState {
  bool any_prjt = false;

public:
  int time             = -1;
  PlayerStateType type = PST_Idle;
  int state_time       = 0;
  int trunc_time       = 0;
  bool active_stall    = true;
  bool can_cancel      = false;
  bool show_crossup    = false;

  // -2 means our x < other x
  // -1 means our x = other x but we just came from -2
  // 1 means our x = other x but we just came from 2
  // 2 means our x > other x
  int side_state = 0;

  PlayerState() = default;
  PlayerState(asw_player &curr_player, asw_player &opp_player, const PlayerState &last, bool combo_active, bool show_crossup);

  bool isStunned() const { return type == PST_HitStunned || type == PST_BlockStunned; }
  bool anyProjectiles() const { return (type == PST_ProjectileAttacking) || any_prjt; }
};

struct FrameInfo {
  PlayerStateType state = PST_None;
  ModifierType mods     = MT_None;
  double decay          = 1.0;
  int trunc             = 0;
};

struct MoveStats {
  int startup = 0;
  std::vector<std::pair<int, int>> actives{{0, 0}};
  void update(const PlayerState &current);
};

struct PlayerData {
  FrameInfo segments[FRAME_SEGMENTS];
  MoveStats working_stats;
  MoveStats displayed_stats;
  PlayerState previous_state;
  PlayerState current_state;

  void resetFrames();
  void initSegment(int curr_idx);
  void truncSegment(int prev_idx);
  void fadeSegment(int fade_idx);
  void updateSegment(int prev_idx, int curr_idx);
  void updateMove();
  void shift(const PlayerState &data);
};

struct FrameBar::Data {
  DrawContext tool;
  bool combo_active       = false;
  int current_segment_idx = 0;
  bool tracking_advantage = false;
  int advantage           = 0;
  std::pair<PlayerData, PlayerData> data;

  /*template <class RetT, std::enable_if<!std::is_void_v<RetT>>, class... ArgT>
  std::pair<RetT, RetT> doBoth(RetT (PlayerData::*func)(ArgT...), ArgT... args)
  {
    return {data.first.*func(args...), data.second.*func(args...)};
  }*/
  template <class... ArgT>
  void doBoth(void (PlayerData::*func)(ArgT...), ArgT... args) {
    (data.first.*func)(args...);
    (data.second.*func)(args...);
  }

  /*template <class FuncT, class... ArgT>
  void doBoth(const std::function<FuncT> &func, ArgT... args)
  {
    func(&data.first, args...);
    func(&data.second, args...);
  }*/

  void drawFrame(const CurrentOptions &scheme, const FrameInfo &info, int top, int left);
  void resetFrames();

  Data();
  void addFrame();
  void reset();
  void draw();
};
