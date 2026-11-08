// Classify
// Pure function from a PlayerSnapshot (plus the previous PlayerState) to the next PlayerState.
// Does not read game memory, globals, or menu settings.
#include "framebar_p.h"

#include <cstring>

std::string safeRead(const char *target) {
  const size_t len = strnlen_s(target, 30);
  return std::string(target, len);
}

// Currently unused (see can_cancel below). Reads game memory, so if re-enabled it belongs in the sampler.
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
