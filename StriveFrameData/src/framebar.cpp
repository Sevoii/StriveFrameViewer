#include "framebar_p.h"

#include <Windows.h>

/* Debug Stuff */
#if 0
#define DEBUG_PRINT(...) RC::Output::send<LogLevel::Warning>(__VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#endif
#define ENABLE_PRJT_DEBUG false
#define ENABLE_STATE_DEBUG false

namespace {
  /* Combo Triger Settings */
  constexpr int COMBO_ENDED_TIME = 20; // time idle before combo is considered over
  constexpr int COMBO_NUM_TIME = 9;    // minimum time of unchanging states to display state length
  constexpr int COMBO_TRUNC_TIME = 10; // max segments for unchanging state, after which the segments are truncated
  constexpr double COLOR_DECAY = 0.9f;

  /* Draw-Space Settings */
  constexpr int FADE_DISTANCE = 5;
  constexpr int SEG_WIDTH = 20;
  constexpr int SEG_HEIGHT = 36;
  constexpr int SEG_SPACING = 4;
  constexpr int BAR_SPACING = 8;
  constexpr int BORDER_THICKNESS = 3;
  constexpr double BAR_TEXT_SIZE = 1.4;

  /* Scree-Space Settings */
  constexpr double CENTER_X_RATIO = 0.5f;
  constexpr double CENTER_Y_RATIO = 0.8f;

  /* Draw-Space Calculations */
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

  /* Unreal Constants */
  FLinearColor color_invisible{1.f, 1.f, 1.f, 0.f};
  FLinearColor color_white{1.f, 1.f, 1.f, 1.f};
  FLinearColor color_red{1.f, 0.f, 0.f, 1.f};
  FLinearColor color_black{0.f, 0.f, 0.f, 1.f};
}

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

std::wstring makeStatsText(const MoveStats &stats, int advantage) {
  std::wstringstream builder;
  builder << L"Startup: " << stats.startup << ", Active: ";
  for (int idx = 0; idx < stats.actives.size() - 1; ++idx) {
    auto &focus = stats.actives[idx];
    builder << focus.first << L"(" << focus.second << ")";
  }
  auto &last = stats.actives.back();
  builder << last.first << ", Recovery: " << last.second << L", Advantage: " << advantage;
  return builder.str();
}

std::string safeRead(const char* target){
  const size_t len = strnlen_s(target,30);
  return std::string(target,len);
}


bool hasCancelOptions(const asw_player& player) {
  const auto& datas = player.move_datas;

  for(size_t idx = 0; idx < datas.move_count; ++idx){
    const auto& move = datas.moves[idx];
    if((move.active_flag & SAF_FLEX_CHAIN && player.can_whiff_cancel()) || (move.active_flag & SAF_CHAIN && player.can_gatling_cancel())){
      auto name = convertToWide(safeRead(datas.moves[idx].get_name()));
//      RC::Output::send<LogLevel::Warning>(STR("    whiff: Idx: {}, Name: {}\n"), idx, name);
      return true;
    }
  }
  return false;
}

// ############################################################
// ProjectileTracker

void ProjectileTracker::markOld(asw_player &player) {
  // assumes a move will never spawn a projectile frame 1
  for (auto &iter : ownership) {
    if (iter.second.root_parent == &player && iter.second.old != 0) {
      iter.second.old = 2;
    }
  }
}

void ProjectileTracker::processFrame() {
  // assumes if an entity at &p was destroyed and a new entity is created at &p, any new entities that point to &p as their parent refer to the new one
  const auto engine = asw_engine::get();
  if (!engine)
    return;

  asw_player *player_one = engine->players[0].entity;
  asw_player *player_two = engine->players[1].entity;
  if (!player_one || !player_two)
    return;

  // mark all old pointers as dead until seen in the current frame
  for (auto &iter : ownership) {
    iter.second.alive = false;
    if (iter.second.old == 0)
      iter.second.old = 1;
  }

  // check for in-frame reassignment
  for (int idx = 0; idx < engine->entity_count; ++idx) {
    auto *focus = engine->entities[idx], *parent = focus->parent_obj;
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
  for (int idx = 0; idx < engine->entity_count; ++idx) {
    auto *focus = engine->entities[idx], *parent = focus->parent_obj;
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
void ProjectileTracker::debugDump() {
  constexpr const wchar_t *age_vals[] = {L"NEW", L"EXIST", L"OLD"};

  const auto engine = asw_engine::get();
  if (!engine)
    return;

  asw_player *player_one = engine->players[0].entity;
  asw_player *player_two = engine->players[1].entity;

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

ProjectileTracker ptracker;

/* Known Issues:
  Several Asuka spells will mark inactive and set sprite to null on frame-0 hit:
    "Howling Metron" / "AttackMagic_01"
    "Howling Metron MS Processing" / "AttackMagic_03"
    "Bit Shift Metron" / "AttackMagic_10"
    "RMS Boost Metron" / "AttackMagic_11"
*/
bool shouldBeActive(std::pair<asw_entity *const, ProjectileTracker::ProjectileInfo> &info_pair) {
  const auto bbscript = std::string_view(info_pair.first->get_BB_state());

  // see above, these 4 break the assumptions that cover every other projectile, and I'm fucking tired
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

// ############################################################
// MoveStats

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

void PlayerData::resetFrames() {
  current_state.state_time = 1;
  previous_state.state_time = 1;
  working_stats = MoveStats();
  displayed_stats = MoveStats();
  for (int idx = 0; idx < FRAME_SEGMENTS; ++idx)
    segments[idx] = FrameInfo();
}

// ############################################################
// PlayerData

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
  if(current_state.can_cancel){
    active.mods |= MT_Cancellable;
  }

  if (current_state.state_time == 1) {
    DEBUG_PRINT(STR("New Section for One\n"));

    // don't delim starts of cancel frames
    previous.mods |= MT_SegmentEnd;
    active.decay = 1.0;

    // ... and was long enough that we want trucated
    if (previous_state.state_time > COMBO_NUM_TIME) {
      DEBUG_PRINT(STR("Last section requires truncating\n"));
      previous.trunc = previous_state.state_time;
    }
  } else {
    DEBUG_PRINT(STR("Same Section for One\n"));
    if(previous_state.can_cancel != current_state.can_cancel && previous_state.trunc_time > COMBO_NUM_TIME){
      previous.trunc = previous_state.state_time;
    }
    // we are drawing this section, fade its color slightly
    // active_segment.color_one = state_colors[type_one] * COLOR_DECAY;
    active.decay = previous.decay * COLOR_DECAY;
  }

  if(current_state.anyProjectiles()){
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
  if(current_state.anyProjectiles()){
    active.mods |= MT_Projectile;
  }
  if(current_state.can_cancel){
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

// ############################################################
// PlayerState

PlayerState::PlayerState(asw_player &player, asw_player &opp, const PlayerState &last, bool combo_active, bool show_cu) {
  time = player.action_time;
  show_crossup = show_cu;

  if (player.pos_x < opp.pos_x) {
    side_state = -2;
  } else if (player.pos_x > opp.pos_x) {
    side_state = 2;
  } else if (last.side_state == -2) {
    side_state = -1;
  } else if (last.side_state == 2) {
    side_state = 1;
  } else {
    side_state = last.side_state;
  }

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
  const bool jump_recovery = player.is_jump_recovery();
  const bool is_dash = player.is_fdash();

  bool projectile_active = false;
  for (auto &iter : ptracker.ownership) {
    if (iter.second.root_parent != &player || !shouldBeActive(iter))
      continue;
    any_prjt |= !player_active || (iter.second.old == 2);
    projectile_active |= !(iter.second.old == 2);
  }

  // TODO: account for 0-lifetime projectiles that spawn and immediately self destruct, such as Asuka's 808

  if (normal_canact || stance_canact || (jump_recovery && !combo_active))
    type = PST_Idle;
  else if (block_stunned)
    type = PST_BlockStunned;
  else if (hit_stunned || knockdown)
    type = PST_HitStunned;
  else if (player_active && last.active_stall)
    type = PST_Attacking;
  else if (projectile_active && last.active_stall)
    type = PST_ProjectileAttacking;
  else if (same_script && last.type != PST_Busy)
    type = PST_Recovering;
  else
    type = PST_Busy;

  // Quick hotfix for gio/leo dashes
  if (type == PST_Idle && is_dash && ModMenu::instance().dashEnabled()) {
    type = PST_Dash;
  }

//  can_cancel = (type == PST_Recovering || type == PST_Busy) && hasCancelOptions(player);

//   active stall prevents the first active frame (before the hit is registered) from appearing active
//   this helps match Dustloop and looks more intuitive
  if (player_active || projectile_active) {
    active_stall = true;
  }

//   state_time is used for determining how long was spent in each PST state for a single BB state script
//   type != PST_Busy to prevent interuptible post move animations (that are idle equivalent) or chained stuns from breaking segments
//   make sure not to trunc sideswap stuff
  if ((same_script || type != PST_Busy) && last.type == type && (((side_state < 0) == (last.side_state < 0)) || !show_cu)) {
    state_time = (last.state_time < 1000) ? last.state_time + 1 : last.state_time;
    bool cancel_break = ModMenu::instance().cancelEnabled() && last.can_cancel != can_cancel;
    trunc_time = cancel_break ? 1 : last.trunc_time + 1;
  } else {
    state_time = 1;
    trunc_time = 1;
  }

//  if constexpr (ENABLE_STATE_DEBUG) {
//    auto format = STR("script:{}, time:{}, sprite:{}, can:{}, stance:{} bstun:{}, hstun:{}, plact:{}, pjact:{}, any:{}, st:{}, cin:{}, hbc:{}, trw:{}, f:{}, c:{}, atk: {}\n");
//    std::wstring local_script = convertToWide(player.get_BB_state());
//    std::wstring local_sprite = convertToWide(player.get_sprite_name());
//    auto nca = normal_canact ? L"Y" : L"N";
//    auto sca = stance_canact ? L"Y" : L"N";
//    auto bs = block_stunned ? L"Y" : L"N";
//    auto hs = hit_stunned ? L"Y" : L"N";
//    auto pla = player_active ? L"Y" : L"N";
//    auto pja = projectile_active ? L"Y" : L"N";
//    auto aja = any_prjt ? L"Y" : L"N";
//    auto cin = player.cinematic_counter ? L"Y" : L"N";
//    auto flex = player.can_whiff_cancel() ? L"Y" : L"N";
//    auto cancel = player.can_gatling_cancel()  ? L"Y" : L"N";
//    RC::Output::send<LogLevel::Warning>(format, local_script, time, local_sprite, nca, sca, bs, hs, pla, pja, aja, state_time, cin, player.hitbox_count, player.throw_range, flex, cancel, player.attack_flag);
//  }
}

// ############################################################
// FrameBar::Data

FrameBar::Data::Data()
: tool(CENTER_X_RATIO, CENTER_Y_RATIO) {
  resetFrames();
}

void FrameBar::Data::drawFrame(const CurrentOptions& scheme, const FrameInfo &info, int top, int left) {
  if (info.state != PST_None) {
    if (info.mods & MT_Projectile) {
      tool.drawRect(left - BORDER_THICKNESS, top - BORDER_THICKNESS, SEG_WIDTH + 2 * BORDER_THICKNESS, SEG_HEIGHT + 2 * BORDER_THICKNESS, scheme.palette.background_color);
    }
    
    auto color = scheme.palette.state_colors[info.state];
    if(scheme.show_fade) color = color * info.decay;
    tool.drawRect(left, top, SEG_WIDTH, SEG_HEIGHT, color);

    if(scheme.show_delim && info.mods & MT_SegmentEnd) {
      tool.drawRect(left + SEG_WIDTH, top - BORDER_THICKNESS, BORDER_THICKNESS, SEG_HEIGHT + 2 * BORDER_THICKNESS, color_white);
    }

    if(scheme.show_cancels && info.mods & MT_Cancellable) {
      constexpr int CANCEL_HEIGHT = SEG_HEIGHT / 3;
      tool.drawRect(left, top + SEG_HEIGHT - CANCEL_HEIGHT, SEG_WIDTH, CANCEL_HEIGHT, color_white);
    }

    if (scheme.show_crossups && info.mods & MT_CrossUp) {
        tool.drawRect(left + SEG_WIDTH - BORDER_THICKNESS, top - BORDER_THICKNESS, 2 * BORDER_THICKNESS, SEG_HEIGHT + 2 * BORDER_THICKNESS, color_red);
    }

    if (info.trunc > 0) {
      auto text = std::to_wstring(info.trunc);
      int text_left = left - (text.size() - 1) * 17 + 2;
      tool.drawOutlinedText(text_left, top, Unreal::FString(text.c_str()), BAR_TEXT_SIZE);
    }
  }
}

void FrameBar::Data::resetFrames() {
  current_segment_idx = 0;
  doBoth(&PlayerData::resetFrames);
  advantage = 0;
}

void FrameBar::Data::addFrame() {
  const auto engine = asw_engine::get();
  if (!engine)
    return;

  auto &p_one = *engine->players[0].entity;
  auto &p_two = *engine->players[1].entity;

  // update projectiles, even during hitstun
  ptracker.processFrame();
  if constexpr (ENABLE_PRJT_DEBUG) {
    RC::Output::send<LogLevel::Warning>(STR("PTracker Pre:\n"));
    ptracker.debugDump();
  }

  // crate updated states
  auto scheme = ModMenu::instance().getScheme();
  std::pair<PlayerState, PlayerState> next = {
      PlayerState(p_one, p_two, data.first.current_state, combo_active, scheme.show_crossups),
      PlayerState(p_two, p_one, data.second.current_state, combo_active, scheme.show_crossups)};

  if (p_one.cinematic_counter || p_two.cinematic_counter) {
    if constexpr (ENABLE_STATE_DEBUG) {
      RC::Output::send<LogLevel::Warning>(STR("CINEMATIC SKIP {} {} {} {}\n"), p_one.hitstop, p_one.atk_param_hit.hitstop, p_two.hitstop, p_two.hitstop);
    }
    return;
  }
  // covers COUNTER HIT, this might cover all cases
  else if ((next.first.time == data.first.current_state.time && next.first.time > 1 && (next.second.type == PST_HitStunned || next.second.type == PST_BlockStunned)) || (next.second.time == data.second.current_state.time && next.second.time > 1 && (next.first.type == PST_HitStunned || next.first.type == PST_BlockStunned))) {
    if constexpr (ENABLE_STATE_DEBUG) {
      RC::Output::send<LogLevel::Warning>(STR("STUN SKIP {} {} {} {}\n"), p_one.hitstop, p_one.atk_param_hit.hitstop, p_two.hitstop, p_two.hitstop);
    }
    return;
  } else if constexpr (ENABLE_STATE_DEBUG) {
    RC::Output::send<LogLevel::Warning>(STR("KEEP {} {} {} {}\n"), p_one.hitstop, p_one.atk_param_hit.hitstop, p_two.hitstop, p_two.hitstop);
  }

  // shift states
  data.first.shift(next.first);
  data.second.shift(next.second);

  if constexpr (ENABLE_PRJT_DEBUG) {
    RC::Output::send<LogLevel::Warning>(STR("PTracker Post:\n"));
    ptracker.debugDump();
  }

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

  DEBUG_PRINT(STR("Frame {}\n"), current_segment_idx);

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
    DEBUG_PRINT(STR("Is Active\n"));
    int prev_idx = (current_segment_idx + FRAME_SEGMENTS - 1) % FRAME_SEGMENTS;

    // we are truncating, update truncated
    if (data.first.current_state.trunc_time > COMBO_TRUNC_TIME && data.second.current_state.trunc_time > COMBO_TRUNC_TIME) {
      doBoth(&PlayerData::truncSegment, prev_idx);
      return;
    }

    doBoth(&PlayerData::updateSegment, prev_idx, current_segment_idx);
  } else {
    DEBUG_PRINT(STR("Was not Active\n"));

    doBoth(&PlayerData::initSegment, current_segment_idx);
  }

  // fade effect, clear segments near tail
  int fade_idx = (current_segment_idx + FADE_DISTANCE) % FRAME_SEGMENTS;
  doBoth(&PlayerData::fadeSegment, fade_idx);

  // advance to next segment
  if (++current_segment_idx >= FRAME_SEGMENTS) {
    DEBUG_PRINT(STR("Cycling segment index\n"));
    current_segment_idx = 0;
  }

  combo_active = true;
}
void FrameBar::Data::reset() {
  combo_active = false;
  data.first.current_state = PlayerState();
  data.second.current_state = PlayerState();
  ptracker.reset();
  resetFrames();
}
void FrameBar::Data::draw() {
  tool.update();

  auto& menu = ModMenu::instance();
  auto options = menu.getScheme();
  bool fade = menu.fadeEnabled();

  auto player_one_info = makeStatsText(data.first.displayed_stats, advantage);
  auto player_two_info = makeStatsText(data.second.displayed_stats, -advantage);

  tool.drawOutlinedText(BAR_LEFT, INFO_ONE_LOC, Unreal::FString(player_one_info.c_str()), BAR_TEXT_SIZE);
  tool.drawOutlinedText(BAR_LEFT, INFO_TWO_LOC, Unreal::FString(player_two_info.c_str()), BAR_TEXT_SIZE);

  tool.drawRect(BAR_LEFT, BAR_ONE_TOP, BAR_WIDTH, BAR_HEIGHT, options.palette.background_color);
  tool.drawRect(BAR_LEFT, BAR_TWO_TOP, BAR_WIDTH, BAR_HEIGHT, options.palette.background_color);

  for (int idx = 0; idx < FRAME_SEGMENTS; ++idx) {
    int left = BAR_LEFT + (SEG_TOTAL * idx) + SEG_SPACING;
    drawFrame(options,  data.first.segments[idx], SEGS_ONE_TOP, left);
    drawFrame(options, data.second.segments[idx], SEGS_TWO_TOP, left);
  }
}

FrameBar::~FrameBar() = default;
FrameBar::FrameBar()
: data(new Data()) {}
void FrameBar::addFrame() { d().addFrame(); }
void FrameBar::reset() { d().reset(); }
void FrameBar::draw() { d().draw(); }