#include "framebar.h"

#include "arcsys.h"
#include "draw_utils.h"
#include "menu.h"

#include <cstdint>
#include <cstring>
#include <deque>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Windows.h>

/*
  Frame bar architecture

    Sample   - once per simulated game frame, reads game memory into a plain FrameSnapshot.
               Owns the projectile tracker; the only stage with side effects.
    History  - ring buffer of FrameRecords keyed by the engine's GameFrame counter.
               This is the source of truth: what actually happened, not how it is drawn.
    Classify - pure function from a snapshot (plus the previous state) to the next PlayerState.
    Layout   - pure function of History + options. Classifies each frame and folds the result into display
               segments, truncation, combo boundaries, move stats, and advantage.
               Rebuilt only when the history or the options change.
    Draw     - renders the current Layout.

  Everything except FrameBar is internal to this file.
*/

/* Debug Stuff */
#if 0
#define DEBUG_PRINT(...) RC::Output::send<LogLevel::Warning>(__VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#endif
#define ENABLE_PRJT_DEBUG false
#define ENABLE_STATE_DEBUG false

namespace {
  constexpr int FRAME_SEGMENTS = 120;

  /* Layout Settings */
  constexpr int COMBO_ENDED_TIME = 20; // time idle before combo is considered over
  constexpr int COMBO_NUM_TIME = 9;    // minimum time of unchanging states to display state length
  constexpr int COMBO_TRUNC_TIME = 10; // max segments for unchanging state, after which the segments are truncated
  constexpr double COLOR_DECAY = 0.9f;
  constexpr int FADE_DISTANCE = 5;     // gap of cleared segments ahead of the write head

  /* Draw-Space Settings */
  constexpr int SEG_WIDTH = 20;
  constexpr int SEG_HEIGHT = 36;
  constexpr int SEG_SPACING = 4;
  constexpr int BAR_SPACING = 8;
  constexpr int BORDER_THICKNESS = 3;
  constexpr double BAR_TEXT_SIZE = 1.4;

  /* Screen-Space Settings */
  constexpr double CENTER_X_RATIO = 0.5f;
  constexpr double CENTER_Y_RATIO = 0.8f;

  /* Draw-Space Calculations */
  constexpr int SEG_TOTAL = SEG_SPACING + SEG_WIDTH;
  constexpr int BAR_WIDTH = (FRAME_SEGMENTS * SEG_TOTAL) + SEG_SPACING;
  constexpr int BAR_HEIGHT = 2 * SEG_SPACING + SEG_HEIGHT;
  constexpr int BAR_RIGHT = BAR_WIDTH / 2;
  constexpr int BAR_LEFT = -1 * BAR_RIGHT;
  constexpr int BAR_TWO_TOP = BAR_SPACING / 2;
  constexpr int BAR_TWO_BOTTOM = BAR_TWO_TOP + BAR_HEIGHT;
  constexpr int BAR_ONE_BOTTOM = -1 * BAR_TWO_TOP;
  constexpr int BAR_ONE_TOP = -1 * BAR_TWO_BOTTOM;
  constexpr int SEGS_TWO_TOP = BAR_TWO_TOP + SEG_SPACING;
  constexpr int SEGS_ONE_TOP = BAR_ONE_TOP + SEG_SPACING;
  constexpr int INFO_ONE_LOC = BAR_ONE_TOP - BAR_HEIGHT;
  constexpr int INFO_TWO_LOC = BAR_TWO_BOTTOM + 2;

  /* Unreal Constants */
  FLinearColor color_white{1.f, 1.f, 1.f, 1.f};
  FLinearColor color_red{1.f, 0.f, 0.f, 1.f};

  // ############################################################
  // Helpers

  std::wstring convertToWide(const std::string_view &str) {
    if (str.empty())
      return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
  }

  std::string safeRead(const char *target) {
    const size_t len = strnlen_s(target, 30);
    return std::string(target, len);
  }

  // Currently unused (see can_cancel in classify). Reads game memory, so if re-enabled it belongs in the sampler.
  bool hasCancelOptions(const asw_player &player) {
    const auto &datas = player.move_datas;

    for (size_t idx = 0; idx < datas.move_count; ++idx) {
      const auto &move = datas.moves[idx];
      if ((move.active_flag & SAF_FLEX_CHAIN && player.can_whiff_cancel()) || (move.active_flag & SAF_CHAIN && player.can_gatling_cancel())) {
        return true;
      }
    }
    return false;
  }

  // ############################################################
  // Sample
  // Reads game memory into a FrameSnapshot. This is the only stage that touches the game or carries side effects
  // (projectile ownership/age tracking), so it must run exactly once per simulated game frame.

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

  void ProjectileTracker::markOld(asw_player &player) {
    // assumes a move will never spawn a projectile frame 1
    for (auto &iter : ownership) {
      if (iter.second.root_parent == &player && iter.second.old != 0) {
        iter.second.old = 2;
      }
    }
  }

  void ProjectileTracker::processFrame(const asw_engine &engine) {
    // assumes if an entity at &p was destroyed and a new entity is created at &p, any new entities that point to &p as their parent refer to the new one
    asw_player *player_one = engine.players[0].entity;
    asw_player *player_two = engine.players[1].entity;
    if (!player_one || !player_two)
      return;

    // mark all old pointers as dead until seen in the current frame
    for (auto &iter : ownership) {
      iter.second.alive = false;
      if (iter.second.old == 0)
        iter.second.old = 1;
    }

    // check for in-frame reassignment
    for (int idx = 0; idx < engine.entity_count; ++idx) {
      auto *focus = engine.entities[idx], *parent = focus->parent_obj;
      if (auto iter = ownership.find(focus); iter != ownership.end()) {
        if (iter->second.direct_parent != parent) {
          RC::Output::send<LogLevel::Warning>(STR("PSFP re-assign: {}, Old Parent: {}, New Parent: {}\n"), (void *)focus, (void *)iter->second.direct_parent, (void *)parent);
          ownership.erase(focus);
        } else {
          iter->second.alive = true;
        }
      }
    }

    // create new infos and link parents
    for (int idx = 0; idx < engine.entity_count; ++idx) {
      auto *focus = engine.entities[idx], *parent = focus->parent_obj;
      if (focus == player_one || focus == player_two)
        continue;
      bool old = false;
      if (ownership.find(focus) == ownership.end()) {
        if (auto parent_iter = ownership.find(parent); parent_iter != ownership.end()) {
          old = parent_iter->second.old;
          ownership.emplace(focus, ProjectileInfo(focus, *parent_iter));
        } else {
          ownership.emplace(focus, ProjectileInfo(focus));
        }
      }
      for (auto &iter : ownership) {
        if (iter.second.root_parent == focus) {
          iter.second.root_parent = parent;
          iter.second.old = old;
        }
      }
    }

    // remove any dead elements
    for (auto first = ownership.begin(), last = ownership.end(); first != last;) {
      if (!first->second.alive)
        first = ownership.erase(first);
      else
        ++first;
    }
  }

  void ProjectileTracker::debugDump(const asw_engine &engine) {
    constexpr const wchar_t *age_vals[] = {L"NEW", L"EXIST", L"OLD"};

    asw_player *player_one = engine.players[0].entity;
    asw_player *player_two = engine.players[1].entity;

    auto format = STR("Prjt script:{}, sprite:{}, ptr:{}, par:{}, pt:{}, old:{}, dmg:{}, hit:{}, act:{}\n");
    for (auto &iter : ownership) {
      auto *focus = iter.first, *top_parent = iter.second.root_parent;
      auto bb_script = convertToWide(focus->get_BB_state());
      auto sprite = convertToWide(focus->get_sprite_name());
      void *direct_parent = (void *)iter.second.direct_parent;
      const wchar_t *parent_type = (top_parent == player_one) ? L"ONE" : ((top_parent == player_two) ? L"TWO" : L"FREE");
      const wchar_t *age = age_vals[iter.second.old];
      int dmg = focus->atk_param_hit.damage;
      int hitbox_count = focus->hitbox_count;
      const wchar_t *active = focus->is_active() ? L"Y" : L"N";

      RC::Output::send<LogLevel::Warning>(format, bb_script, sprite, (void *)focus, direct_parent, parent_type, age, dmg, hitbox_count, active);
    }
  }

  /* Known Issues:
    Several Asuka spells will mark inactive and set sprite to null on frame-0 hit:
      "Howling Metron" / "AttackMagic_01"
      "Howling Metron MS Processing" / "AttackMagic_03"
      "Bit Shift Metron" / "AttackMagic_10"
      "RMS Boost Metron" / "AttackMagic_11"
  */
  // Note: consumes hit_delay, so it must only be called once per projectile per game frame.
  bool shouldBeActive(std::pair<asw_entity *const, ProjectileTracker::ProjectileInfo> &info_pair) {
    const auto bbscript = std::string_view(info_pair.first->get_BB_state());

    // see above, these 4 break the assumptions that cover every other projectile
    if (bbscript.size() == 14 && bbscript.substr(0, 12) == "AttackMagic_") {
      auto AM_num = bbscript.substr(12, 2);
      if (AM_num == "01" || AM_num == "03" || AM_num == "10" || AM_num == "11")
        return true;
    }
    /* null check for two reasons:
        strive leaves active but nulled sprites around FOREVER (metron 808 lasts 50 frames)
        we don't want to apply the frame-0 hack to null sprites that haven't come out yet
    */
    const auto sprite = std::string_view(info_pair.first->get_sprite_name());
    if (sprite == "null")
      return false;
    if (info_pair.first->is_active() && info_pair.first->hitbox_count > 0)
      return true;

    /* another hack... some projectiles spawn deactived and activate later on
      Baikan's Tatami Mat
      either of Ram's Bajoneto
    */
    // tatami mat is spawned WAY before its active, don't apply frame-0 hack to it
    if (bbscript == "TatamiLandObj" || bbscript == "404wepL_summon_short" || bbscript == "404wepR_summon_short")
      return false;

    // to compensate for projectiles that "should" be active but are self deactivating frame-0
    // we artificially extend their lifetime just long enough for our purposes.
    int damage = info_pair.first->atk_param_hit.damage;
    if (damage > 0 && info_pair.second.hit_delay) {
      --info_pair.second.hit_delay;
      return true;
    }
    return false;
  }

  class FrameSampler {
    ProjectileTracker ptracker;

    void samplePlayer(asw_player &player, PlayerSnapshot &out);

  public:
    // Reads both players into a snapshot. Must be called exactly once per simulated game frame.
    // Returns false if the players are not available.
    bool sample(const asw_engine &engine, FrameSnapshot &out);

    void reset() { ptracker.reset(); }
  };

  void FrameSampler::samplePlayer(asw_player &player, PlayerSnapshot &out) {
    out.action_time = player.action_time;
    out.pos_x = player.pos_x;

    // assumes a sprite won't come out on this frame, this is false
    const bool same_script = out.action_time > 1;
    if (!same_script) {
      ptracker.markOld(player);
    }

    out.can_act = player.can_act();
    out.stance_idle = player.is_stance_idle();
    out.block_stunned = player.is_in_blockstun() || player.is_stagger() || player.is_guard_crush();
    out.hit_stunned = player.is_in_hitstun();
    out.knockdown = player.is_knockdown();
    out.hitbox_active = player.is_active() && (player.hitbox_count > 0 || player.throw_range >= 0);
    out.jump_recovery = player.is_jump_recovery();
    out.fdash = player.is_fdash();
    out.cinematic = player.cinematic_counter;

    out.projectile_active = false;
    out.old_projectile_active = false;
    for (auto &iter : ptracker.ownership) {
      if (iter.second.root_parent != &player || !shouldBeActive(iter))
        continue;
      if (iter.second.old == 2)
        out.old_projectile_active = true;
      else
        out.projectile_active = true;
    }
  }

  bool FrameSampler::sample(const asw_engine &engine, FrameSnapshot &out) {
    asw_player *player_one = engine.players[0].entity;
    asw_player *player_two = engine.players[1].entity;
    if (!player_one || !player_two)
      return false;

    // update projectiles, even during hitstun
    ptracker.processFrame(engine);
    if constexpr (ENABLE_PRJT_DEBUG) {
      RC::Output::send<LogLevel::Warning>(STR("PTracker:\n"));
      ptracker.debugDump(engine);
    }

    samplePlayer(*player_one, out.players[0]);
    samplePlayer(*player_two, out.players[1]);
    return true;
  }

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

  void FrameHistory::push(int game_frame, const FrameSnapshot &snapshot) {
    if (records.size() >= CAPACITY) records.pop_front();
    records.push_back(FrameRecord{game_frame, snapshot});
    ++revision;
  }

  void FrameHistory::rewindTo(int game_frame) {
    bool changed = false;
    while (!records.empty() && records.back().game_frame >= game_frame) {
      records.pop_back();
      changed = true;
    }
    if (changed) ++revision;
  }

  void FrameHistory::clear() {
    records.clear();
    ++revision;
  }

  // ############################################################
  // Classify
  // Pure function from a PlayerSnapshot (plus the previous PlayerState) to the next PlayerState.
  // Does not read game memory, globals, or menu settings.

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

  PlayerState classify(const PlayerSnapshot &self, const PlayerSnapshot &opp, const PlayerState &last, bool combo_active, const LayoutOptions &options) {
    PlayerState state;
    state.time = self.action_time;
    state.show_crossup = options.show_crossup;

    if (self.pos_x < opp.pos_x) {
      state.side_state = -2;
    } else if (self.pos_x > opp.pos_x) {
      state.side_state = 2;
    } else if (last.side_state == -2) {
      state.side_state = -1;
    } else if (last.side_state == 2) {
      state.side_state = 1;
    } else {
      state.side_state = last.side_state;
    }

    // assumes a sprite won't come out on this frame, this is false
    const bool same_script = state.time > 1;

    // outline: an owned projectile is active while we are not, or one from an earlier move is still active
    state.any_projectile = (!self.hitbox_active && (self.projectile_active || self.old_projectile_active)) || self.old_projectile_active;

    // TODO: account for 0-lifetime projectiles that spawn and immediately self destruct, such as Asuka's 808

    if (self.can_act || self.stance_idle || (self.jump_recovery && !combo_active))
      state.type = PST_Idle;
    else if (self.block_stunned)
      state.type = PST_BlockStunned;
    else if (self.hit_stunned || self.knockdown)
      state.type = PST_HitStunned;
    else if (self.hitbox_active && last.active_stall)
      state.type = PST_Attacking;
    else if (self.projectile_active && last.active_stall)
      state.type = PST_ProjectileAttacking;
    else if (same_script && last.type != PST_Busy)
      state.type = PST_Recovering;
    else
      state.type = PST_Busy;

    // Quick hotfix for gio/leo dashes
    if (state.type == PST_Idle && self.fdash && options.show_dash) {
      state.type = PST_Dash;
    }

    //  state.can_cancel = (state.type == PST_Recovering || state.type == PST_Busy) && hasCancelOptions(player);

    // active stall prevents the first active frame (before the hit is registered) from appearing active
    // this helps match Dustloop and looks more intuitive
    if (self.hitbox_active || self.projectile_active) {
      state.active_stall = true;
    }

    // state_time is used for determining how long was spent in each PST state for a single BB state script
    // type != PST_Busy to prevent interuptible post move animations (that are idle equivalent) or chained stuns from breaking segments
    // make sure not to trunc sideswap stuff
    if ((same_script || state.type != PST_Busy) && last.type == state.type && (((state.side_state < 0) == (last.side_state < 0)) || !options.show_crossup)) {
      state.state_time = (last.state_time < 1000) ? last.state_time + 1 : last.state_time;
      bool cancel_break = options.cancel_break && last.can_cancel != state.can_cancel;
      state.trunc_time = cancel_break ? 1 : last.trunc_time + 1;
    } else {
      state.state_time = 1;
      state.trunc_time = 1;
    }

    return state;
  }

  // ############################################################
  // Layout
  // Pure function of the frame history and options: replays every recorded frame through the classifier and folds
  // the result into the display model (combo start/end, segment ring buffer, truncation, move stats, advantage).

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

  void MoveStats::update(const PlayerState &current) {
    if (current.type == PST_Busy) {
      startup = current.state_time + 1;
    } else if (current.type == PST_Attacking || current.type == PST_ProjectileAttacking) {
      // this is a new attack
      if (actives.back().second > 0)
        actives.push_back({0, 0});
      actives.back().first = current.state_time;
    } else if (current.type == PST_Recovering) {
      actives.back().second = current.state_time;
    }
  }

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

  void PlayerData::resetFrames() {
    current_state.state_time = 1;
    previous_state.state_time = 1;
    working_stats = MoveStats();
    displayed_stats = MoveStats();
    for (int idx = 0; idx < FRAME_SEGMENTS; ++idx)
      segments[idx] = FrameInfo();
  }

  void PlayerData::shift(const PlayerState &next) {
    previous_state = current_state;
    current_state = next;
  }

  void PlayerData::updateMove() {
    if (current_state.time > 1) {
      working_stats.update(current_state);
      if (working_stats.actives.front().first > 0) {
        displayed_stats = working_stats;
      }
    } else {
      working_stats = MoveStats();
    }
  }

  void PlayerData::updateSegment(int prev_idx, int curr_idx) {
    // previous section has ended
    auto &previous = segments[prev_idx];
    auto &active = segments[curr_idx];
    previous.trunc = 0;

    active.state = current_state.type;
    active.mods = MT_None;
    if (current_state.can_cancel) {
      active.mods |= MT_Cancellable;
    }

    if (current_state.state_time == 1) {
      DEBUG_PRINT(STR("New Section\n"));

      // don't delim starts of cancel frames
      previous.mods |= MT_SegmentEnd;
      active.decay = 1.0;

      // ... and was long enough that we want trucated
      if (previous_state.state_time > COMBO_NUM_TIME) {
        DEBUG_PRINT(STR("Last section requires truncating\n"));
        previous.trunc = previous_state.state_time;
      }
    } else {
      DEBUG_PRINT(STR("Same Section\n"));
      if (previous_state.can_cancel != current_state.can_cancel && previous_state.trunc_time > COMBO_NUM_TIME) {
        previous.trunc = previous_state.state_time;
      }
      // we are drawing this section, fade its color slightly
      active.decay = previous.decay * COLOR_DECAY;
    }

    if (current_state.anyProjectiles()) {
      active.mods |= MT_Projectile;
    }

    if (previous_state.show_crossup && !(previous_state.side_state == 0 || current_state.side_state == 0) && ((previous_state.side_state < 0) != (current_state.side_state < 0))) {
      previous.mods |= MT_CrossUp;
    }
  }

  void PlayerData::initSegment(int curr_idx) {
    auto &active = segments[curr_idx];
    active.state = current_state.type;
    active.mods = MT_None;
    if (current_state.anyProjectiles()) {
      active.mods |= MT_Projectile;
    }
    if (current_state.can_cancel) {
      active.mods |= MT_Cancellable;
    }
  }

  void PlayerData::truncSegment(int prev_idx) {
    auto &prev_segment = segments[prev_idx];
    prev_segment.trunc = current_state.state_time;
  }

  void PlayerData::fadeSegment(int fade_idx) {
    auto &fade_segment = segments[fade_idx];
    fade_segment.state = PST_None;
    fade_segment.mods = MT_None;
  }

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

  bool FrameLayout::isFrozen(const std::pair<PlayerState, PlayerState> &next, bool cinematic) const {
    if (cinematic) {
      if constexpr (ENABLE_STATE_DEBUG) {
        RC::Output::send<LogLevel::Warning>(STR("CINEMATIC SKIP\n"));
      }
      return true;
    }

    // covers COUNTER HIT, this might cover all cases
    const auto &curr_one = data.first.current_state;
    const auto &curr_two = data.second.current_state;
    const bool one_frozen = next.first.time == curr_one.time && next.first.time > 1 && next.second.isStunned();
    const bool two_frozen = next.second.time == curr_two.time && next.second.time > 1 && next.first.isStunned();
    if (one_frozen || two_frozen) {
      if constexpr (ENABLE_STATE_DEBUG) {
        RC::Output::send<LogLevel::Warning>(STR("STUN SKIP\n"));
      }
      return true;
    }

    return false;
  }

  void FrameLayout::resetFrames() {
    current_segment_idx = 0;
    doBoth(&PlayerData::resetFrames);
    advantage = 0;
  }

  void FrameLayout::addFrame(const FrameSnapshot &snapshot, const LayoutOptions &options) {
    const std::pair<PlayerState, PlayerState> next = {
        classify(snapshot.players[0], snapshot.players[1], data.first.current_state, combo_active, options),
        classify(snapshot.players[1], snapshot.players[0], data.second.current_state, combo_active, options)};

    if (isFrozen(next, snapshot.anyCinematic())) return;

    // shift states
    data.first.shift(next.first);
    data.second.shift(next.second);

    // end combo if we've been idle for a long time
    if (data.first.current_state.type == PST_Idle && data.second.current_state.type == PST_Idle) {
      if (!combo_active) {
        return;
      }
      if (data.first.current_state.state_time > COMBO_ENDED_TIME && data.second.current_state.state_time > COMBO_ENDED_TIME) {
        DEBUG_PRINT(STR("Combo ended\n"));
        combo_active = false;
        return;
      }
    }
    // reset if this is a new combo
    else if (!combo_active) {
      DEBUG_PRINT(STR("Combo reset\n"));
      resetFrames();
    }

    // update move info
    doBoth(&PlayerData::updateMove);

    // update advantage
    if (!tracking_advantage) {
      if (data.first.current_state.isStunned() || data.second.current_state.isStunned()) {
        advantage = 0;
        tracking_advantage = true;
      }
    } else {
      if (data.first.current_state.type == PST_Idle) {
        if (data.second.current_state.type == PST_Idle) {
          tracking_advantage = false;
        } else {
          ++advantage;
        }
      } else {
        if (data.second.current_state.type == PST_Idle) {
          --advantage;
        } else {
          advantage = 0;
        }
      }
    }

    // update frame display
    if (combo_active) { // only trigger truncation logic if mid-combo
      int prev_idx = (current_segment_idx + FRAME_SEGMENTS - 1) % FRAME_SEGMENTS;

      // we are truncating, update truncated
      if (data.first.current_state.trunc_time > COMBO_TRUNC_TIME && data.second.current_state.trunc_time > COMBO_TRUNC_TIME) {
        doBoth(&PlayerData::truncSegment, prev_idx);
        return;
      }

      doBoth(&PlayerData::updateSegment, prev_idx, current_segment_idx);
    } else {
      doBoth(&PlayerData::initSegment, current_segment_idx);
    }

    // fade effect, clear segments near tail
    int fade_idx = (current_segment_idx + FADE_DISTANCE) % FRAME_SEGMENTS;
    doBoth(&PlayerData::fadeSegment, fade_idx);

    // advance to next segment
    if (++current_segment_idx >= FRAME_SEGMENTS) {
      current_segment_idx = 0;
    }

    combo_active = true;
  }

  FrameLayout FrameLayout::build(const FrameHistory &history, const LayoutOptions &options) {
    FrameLayout layout;
    for (const auto &record : history.all()) {
      layout.addFrame(record.snapshot, options);
    }
    return layout;
  }

  // ############################################################
  // Draw helpers

  std::wstring makeStatsText(const MoveStats &stats, int advantage) {
    std::wstringstream builder;
    builder << L"Startup: " << stats.startup << ", Active: ";
    for (size_t idx = 0; idx + 1 < stats.actives.size(); ++idx) {
      auto &focus = stats.actives[idx];
      builder << focus.first << L"(" << focus.second << ")";
    }
    auto &last = stats.actives.back();
    builder << last.first << ", Recovery: " << last.second << L", Advantage: " << advantage;
    return builder.str();
  }

  LayoutOptions currentLayoutOptions() {
    const auto &menu = ModMenu::instance();
    LayoutOptions options;
    options.show_dash = menu.dashEnabled();
    options.show_crossup = menu.crossupEnabled();
    options.cancel_break = menu.cancelEnabled();
    return options;
  }
}

// ############################################################
// FrameBar::Data (glue + drawing)

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

FrameBar::Data::Data()
: tool(CENTER_X_RATIO, CENTER_Y_RATIO) {}

void FrameBar::Data::addFrame() {
  const auto engine = asw_engine::get();
  if (!engine) return;

  // BATTLE_CObjectManager::m_GameFrame is incremented once at the end of every simulated battle frame and never
  // while the battle is paused, so an unchanged value means nothing was simulated since the last call.
  const int game_frame = engine->GameFrame;
  if (game_frame == last_game_frame) return;

  // counter went backwards: new object manager (match restart) or a rewind; drop the frames that no longer happened
  if (game_frame < last_game_frame) history.rewindTo(game_frame);

  // TODO: when this is driven from UpdateBattle (rather than once per UpdateBattleSub), a call can cover several
  // simulated frames (replay fast forward, rollback catch-up). Only the final state is visible here, so those
  // frames collapse into a single record. Hooking UpdateBattleSub removes this limitation.
  last_game_frame = game_frame;

  FrameSnapshot snapshot;
  if (!sampler.sample(*engine, snapshot)) return;
  history.push(game_frame, snapshot);
}

void FrameBar::Data::reset() {
  sampler.reset();
  history.clear();
}

const FrameLayout &FrameBar::Data::currentLayout() {
  const auto options = currentLayoutOptions();
  if (history.getRevision() != layout_revision || !(options == layout_options)) {
    layout = FrameLayout::build(history, options);
    layout_revision = history.getRevision();
    layout_options = options;
  }
  return layout;
}

void FrameBar::Data::drawFrame(const CurrentOptions &scheme, const FrameInfo &info, int top, int left) {
  if (info.state == PST_None) return;

  if (info.mods & MT_Projectile) {
    tool.drawRect(left - BORDER_THICKNESS, top - BORDER_THICKNESS, SEG_WIDTH + 2 * BORDER_THICKNESS, SEG_HEIGHT + 2 * BORDER_THICKNESS, scheme.palette.background_color);
  }

  auto color = scheme.palette.state_colors[info.state];
  if (scheme.show_fade) color = color * info.decay;
  tool.drawRect(left, top, SEG_WIDTH, SEG_HEIGHT, color);

  if (scheme.show_delim && info.mods & MT_SegmentEnd) {
    tool.drawRect(left + SEG_WIDTH, top - BORDER_THICKNESS, BORDER_THICKNESS, SEG_HEIGHT + 2 * BORDER_THICKNESS, color_white);
  }

  if (scheme.show_cancels && info.mods & MT_Cancellable) {
    constexpr int CANCEL_HEIGHT = SEG_HEIGHT / 3;
    tool.drawRect(left, top + SEG_HEIGHT - CANCEL_HEIGHT, SEG_WIDTH, CANCEL_HEIGHT, color_white);
  }

  if (scheme.show_crossups && info.mods & MT_CrossUp) {
    tool.drawRect(left + SEG_WIDTH - BORDER_THICKNESS, top - BORDER_THICKNESS, 2 * BORDER_THICKNESS, SEG_HEIGHT + 2 * BORDER_THICKNESS, color_red);
  }

  if (info.trunc > 0) {
    auto text = std::to_wstring(info.trunc);
    int text_left = left - (static_cast<int>(text.size()) - 1) * 17 + 2;
    tool.drawOutlinedText(text_left, top, Unreal::FString(text.c_str()), BAR_TEXT_SIZE);
  }
}

void FrameBar::Data::draw() {
  tool.update();

  const auto &current = currentLayout();
  const auto scheme = ModMenu::instance().getScheme();

  auto player_one_info = makeStatsText(current.data.first.displayed_stats, current.advantage);
  auto player_two_info = makeStatsText(current.data.second.displayed_stats, -current.advantage);

  tool.drawOutlinedText(BAR_LEFT, INFO_ONE_LOC, Unreal::FString(player_one_info.c_str()), BAR_TEXT_SIZE);
  tool.drawOutlinedText(BAR_LEFT, INFO_TWO_LOC, Unreal::FString(player_two_info.c_str()), BAR_TEXT_SIZE);

  tool.drawRect(BAR_LEFT, BAR_ONE_TOP, BAR_WIDTH, BAR_HEIGHT, scheme.palette.background_color);
  tool.drawRect(BAR_LEFT, BAR_TWO_TOP, BAR_WIDTH, BAR_HEIGHT, scheme.palette.background_color);

  for (int idx = 0; idx < FRAME_SEGMENTS; ++idx) {
    int left = BAR_LEFT + (SEG_TOTAL * idx) + SEG_SPACING;
    drawFrame(scheme, current.data.first.segments[idx], SEGS_ONE_TOP, left);
    drawFrame(scheme, current.data.second.segments[idx], SEGS_TWO_TOP, left);
  }
}

// ############################################################
// FrameBar

FrameBar::~FrameBar() = default;
FrameBar::FrameBar()
: data(new Data()) {}
void FrameBar::addFrame() { d().addFrame(); }
void FrameBar::reset() { d().reset(); }
void FrameBar::draw() { d().draw(); }
