// Layout
// Pure function of the frame history and options: replays every recorded frame through the classifier and folds
// the result into the display model (combo start/end, segment ring buffer, truncation, move stats, advantage).
#include "framebar_p.h"

/* Debug Stuff */
#if 0
#define DEBUG_PRINT(...) RC::Output::send<LogLevel::Warning>(__VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#endif
#define ENABLE_STATE_DEBUG false

namespace {
  constexpr int COMBO_ENDED_TIME = 20; // time idle before combo is considered over
  constexpr int COMBO_NUM_TIME = 9;    // minimum time of unchanging states to display state length
  constexpr int COMBO_TRUNC_TIME = 10; // max segments for unchanging state, after which the segments are truncated
  constexpr double COLOR_DECAY = 0.9f;
  constexpr int FADE_DISTANCE = 5;     // gap of cleared segments ahead of the write head
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

// ############################################################
// PlayerData

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

// ############################################################
// FrameLayout

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
