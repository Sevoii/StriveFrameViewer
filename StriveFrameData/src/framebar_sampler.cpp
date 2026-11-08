// Sample stage
// Reads game memory into a FrameSnapshot. This is the only stage that touches the game or carries side effects
// (projectile ownership/age tracking), so it must run exactly once per simulated game frame.
#include "framebar_p.h"

#include <Windows.h>

#define ENABLE_PRJT_DEBUG false

std::wstring convertToWide(const std::string_view &str) {
  if (str.empty())
    return std::wstring();
  int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
  std::wstring wstrTo(size_needed, 0);
  MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
  return wstrTo;
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
static bool shouldBeActive(std::pair<asw_entity *const, ProjectileTracker::ProjectileInfo> &info_pair) {
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

// ############################################################
// FrameSampler

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
