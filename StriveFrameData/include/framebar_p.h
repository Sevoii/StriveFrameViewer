#pragma once
#include "framebar.h"

#include "arcsys.h"
#include "draw_utils.h"
#include "menu.h"

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

/*
  Frame bar architecture

    Sample  (framebar_sampler.cpp)  - once per simulated game frame, reads game memory into a plain FrameSnapshot.
                                      Owns the projectile tracker; the only stage with side effects.
    History (framebar.cpp)          - ring buffer of FrameRecords keyed by the engine's GameFrame counter.
                                      This is the source of truth: what actually happened, not how it is drawn.
    Layout  (framebar_layout.cpp)   - pure function of History + options. Classifies each frame
                                      (framebar_classify.cpp) and folds the result into display segments,
                                      truncation, combo boundaries, move stats, and advantage.
                                      Rebuilt only when the history or the options change.
    Draw    (framebar.cpp)          - renders the current Layout.
*/

constexpr int FRAME_SEGMENTS = 120;

std::wstring convertToWide(const std::string_view &str);

// ############################################################
// Sample

// Raw per-player facts read from the game for a single game frame.
struct PlayerSnapshot {
  int action_time = 0;
  int pos_x = 0;

  bool can_act = false;       // ENABLE_NORMALATTACK
  bool stance_idle = false;   // Leo / Happy Chaos stance idles
  bool block_stunned = false; // blockstun, stagger, or guard crush
  bool hit_stunned = false;
  bool knockdown = false;
  bool hitbox_active = false; // own hitbox or throw is active
  bool jump_recovery = false;
  bool fdash = false;
  bool cinematic = false;

  bool projectile_active = false;     // an owned projectile from the current move is active
  bool old_projectile_active = false; // an owned projectile from an earlier move is active
};

struct FrameSnapshot {
  PlayerSnapshot players[2];

  bool anyCinematic() const { return players[0].cinematic || players[1].cinematic; }
};

struct ProjectileTracker {
  struct ProjectileInfo {
    asw_entity *direct_parent;
    asw_entity *root_parent;
    bool alive;
    int old;           // 0: brand new (protected), 1: not new, 2: marked old
    int hit_delay = 2; // this entity has hit something already and should not be considered for further damage

    ProjectileInfo(asw_entity *source) {
      direct_parent = source->parent_obj;
      root_parent = direct_parent;
      alive = true;
      old = 0;
    }
    ProjectileInfo(asw_entity *source, const std::pair<asw_entity *, ProjectileInfo> &parent_info) {
      direct_parent = parent_info.first;
      root_parent = parent_info.second.root_parent;
      alive = true;
      old = parent_info.second.old;
    }
  };

  std::unordered_map<asw_entity *, ProjectileInfo> ownership;

  void markOld(asw_player &player);
  void processFrame(const asw_engine &engine);
  void debugDump(const asw_engine &engine);
  void reset() { ownership.clear(); }
};

class FrameSampler {
  ProjectileTracker ptracker;

  void samplePlayer(asw_player &player, PlayerSnapshot &out);

public:
  // Reads both players into a snapshot. Must be called exactly once per simulated game frame.
  // Returns false if the players are not available.
  bool sample(const asw_engine &engine, FrameSnapshot &out);

  void reset() { ptracker.reset(); }
};

// ############################################################
// History

struct FrameRecord {
  int game_frame;
  FrameSnapshot snapshot;
};

class FrameHistory {
  std::deque<FrameRecord> records;
  uint64_t revision = 0;

public:
  static constexpr size_t CAPACITY = 3600; // one minute at 60fps

  void push(int game_frame, const FrameSnapshot &snapshot);
  void rewindTo(int game_frame); // drop every record at or after game_frame
  void clear();

  const std::deque<FrameRecord> &all() const { return records; }
  uint64_t getRevision() const { return revision; } // changes whenever the contents change
};

// ############################################################
// Classify

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

struct LayoutOptions {
  bool show_dash = false;
  bool show_crossup = false;
  bool cancel_break = false; // break truncation when the cancel state changes

  bool operator==(const LayoutOptions &) const = default;
};

struct PlayerState {
  int time = -1;
  PlayerStateType type = PST_Idle;
  int state_time = 0;
  int trunc_time = 0;
  bool active_stall = true;
  bool can_cancel = false;
  bool show_crossup = false;
  bool any_projectile = false;

  // -2 means our x < other x
  // -1 means our x = other x but we just came from -2
  // 1 means our x = other x but we just came from 2
  // 2 means our x > other x
  int side_state = 0;

  bool isStunned() const { return type == PST_HitStunned || type == PST_BlockStunned; }
  bool anyProjectiles() const { return (type == PST_ProjectileAttacking) || any_projectile; }
};

PlayerState classify(const PlayerSnapshot &self, const PlayerSnapshot &opponent, const PlayerState &last, bool combo_active, const LayoutOptions &options);

// ############################################################
// Layout

enum ModifierType {
  MT_None = 0x0,
  MT_Cancellable = 0x1,
  MT_Projectile = 0x2,
  MT_SegmentEnd = 0x4,
  MT_CrossUp = 0x8
};
inline ModifierType operator|(ModifierType a, ModifierType b) { return (ModifierType)((int)a | (int)b); }
inline ModifierType operator&(ModifierType a, ModifierType b) { return (ModifierType)((int)a & (int)b); }
inline ModifierType &operator|=(ModifierType &a, ModifierType b) { return (a = a | b); }
inline ModifierType &operator&=(ModifierType &a, ModifierType b) { return (a = a & b); }

struct FrameInfo {
  PlayerStateType state = PST_None;
  ModifierType mods = MT_None;
  double decay = 1.0;
  int trunc = 0;
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

// Display model of the frame bar. Built from scratch from the history; never mutated by the game hooks.
class FrameLayout {
  template <class... ArgT>
  void doBoth(void (PlayerData::*func)(ArgT...), ArgT... args) {
    (data.first.*func)(args...);
    (data.second.*func)(args...);
  }

  // true for frames where the game is visually frozen (cinematics, hitstop) and nothing should be recorded
  bool isFrozen(const std::pair<PlayerState, PlayerState> &next, bool cinematic) const;
  void resetFrames();
  void addFrame(const FrameSnapshot &snapshot, const LayoutOptions &options);

public:
  bool combo_active = false;
  int current_segment_idx = 0;
  bool tracking_advantage = false;
  int advantage = 0;
  std::pair<PlayerData, PlayerData> data;

  FrameLayout() { resetFrames(); }

  static FrameLayout build(const FrameHistory &history, const LayoutOptions &options);
};

// ############################################################
// Glue + drawing

struct FrameBar::Data {
  DrawContext tool;
  FrameSampler sampler;
  FrameHistory history;
  int last_game_frame = -1;

  // cached layout
  FrameLayout layout;
  uint64_t layout_revision = UINT64_MAX;
  LayoutOptions layout_options;

  const FrameLayout &currentLayout();
  void drawFrame(const CurrentOptions &scheme, const FrameInfo &info, int top, int left);

  Data();
  void addFrame();
  void reset();
  void draw();
};
