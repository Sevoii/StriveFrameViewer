#include "rewind.h"
#include "stage_sync.h"

#include <DynamicOutput/Output.hpp>
#include <UnrealDef.hpp>

#include <algorithm>
#include <chrono>

using RC::Output::send;
namespace LogLevel = RC::LogLevel;

/* Slots */

bool ReplayRewind::saveSlot(int slot, const FrameMeta &m) {
  // SaveBackup writes slot (Current + 1) % Max
  int before = (slot + SLOTS - 1) % SLOTS; // non-const: FIELD setters take T&
  store->CurrentBackupDataIndex = before;
  // SaveBackup has a per-tick save cap that the engine only resets on the network path; keep it from latching
  store->InputSameFrames = 0;
  store->HalfFrames = 0;
  store->SavedFrameCountThisFrame = 0;
  store->bSaveLimitReached = false;
  store->save_backup();

  if (store->CurrentBackupDataIndex != slot || !store->slot_set(slot)) return false;
  meta[slot] = m;
  meta[slot].valid = true;
  return true;
}

bool ReplayRewind::restoreSlot(int slot, int frames_back) {
  // Rollback() restores slot Current - RollbackFrame and clears bSet on every slot after it up to Current. Point
  // Current at the slot right after the target with RollbackFrame 1, so only that one flag is touched, and put it
  // back afterwards (the data itself is only read).
  const int next = (slot + 1) % SLOTS;
  const bool next_set = store->slot_set(next);
  int current = next; // non-const: FIELD setters take T&
  store->CurrentBackupDataIndex = current;

  // PreRollBack/PostRollBack read the GAME's RollbackFrame to age particles/audio and to decide which effect pawns
  // were spawned inside the rolled-back window. Offline it is 0, which leaves effects from after the target attached
  // or orphaned and re-links pawns to the wrong objects. Give it the real distance for the duration of the restore.
  // This runs inside UpdateBattle, after UpdateOnPreTick has already checked the field this tick.
  auto *game = asw_rollback::get();
  int game_rollback_frame = game ? game->RollbackFrame : 0; // non-const: FIELD setters take T&
  if (game) game->RollbackFrame = frames_back;

  store->RollbackFrame = 1;
  store->IsRollBackFinish = false;
  const bool ok = store->rollback();
  store->RollbackFrame = 0;
  store->RemainingFastForwardFrames = 0;
  store->IsRollBackFinish = false;
  store->set_slot_flag(next, next_set);

  if (game) {
    game->RollbackFrame = game_rollback_frame;
    // Netplay clears these after resimulating to drop what the resimulation did not reclaim. We skip the
    // intermediate frames, so nothing will reclaim them: clear now.
    game->clear_psc_cache();
    game->clear_pawn_cache();
  }

  return ok && store->CurrentBackupDataIndex == slot;
}

/* ReplayRewind */

void ReplayRewind::reset() {
  asw_rollback::destroy_owned(store);
  store = nullptr;
  owner = nullptr;
  invalidate();
}

void ReplayRewind::invalidate() {
  for (auto &m : meta) m.valid = false;
  key_head = KEYFRAMES - 1;
  key_count = 0;
  last_frame = -1;
  forward_target = -1;
  forward_stalls = 0;
  wall_breaks = 0;
  last_box = -1;
}

void ReplayRewind::trackWallBreaks(AREDGameState_Battle *GameState, int frame) {
  const auto *bg = GameState->BGLocation;
  if (!bg) return;
  const int box = bg->BoxIndex;
  if (last_box >= 0 && box != last_box) {
    ++wall_breaks;
    send<LogLevel::Verbose>(STR("[rewind] wall break at frame {}: box {} -> {}\n"), frame, last_box, box);
  }
  last_box = box;
}

void ReplayRewind::resyncStage(const FrameMeta &m) {
  // the restored snapshot is from before a wall break that already played out on the stage
  if (m.wall_breaks == wall_breaks) return;
  // walk the blueprint into the restored box the way a wall break does
  const int called = m.box >= 0 ? stage_sync::enterBox(m.box, true) : 0;
  send<LogLevel::Verbose>(STR("[rewind] restore crosses {} wall break(s): stage re-entered box {} ({} blueprint calls)\n"),
                          wall_breaks - m.wall_breaks, m.box, called);
}

void ReplayRewind::disable(const wchar_t *reason) {
  send<LogLevel::Warning>(STR("[rewind] disabled: {}\n"), reason);
  disabled = true;
  reset();
}

bool ReplayRewind::ensureInstance(AREDGameState_Battle *GameState) {
  // buffer sizes come from this battle's object manager; a new battle means a new buffer
  if (owner && owner != GameState) reset();
  if (owner) return true;
  if (disabled || !asw_rollback::owned_available() || !replay_peer::get(0)) return false;

  store = asw_rollback::create_owned(SLOTS);
  if (!store) {
    disable(STR("could not create rollback manager"));
    return false;
  }
  // sanity check the constructor did what the source says before trusting the layout
  if (store->MaxBackupFrame != SLOTS || store->ObjVectorBufferSize == 0 || store->CharVectorBufferSize == 0) {
    send<LogLevel::Warning>(STR("[rewind] unexpected manager layout: max {} obj {} char {}\n"), store->MaxBackupFrame,
                            store->ObjVectorBufferSize, store->CharVectorBufferSize);
    disable(STR("rollback manager layout mismatch"));
    return false;
  }
  owner = GameState;
  invalidate();

  const auto per_slot = asw_rollback::SLOT_SIZE + 0x1848 + store->ObjVectorBufferSize + store->CharVectorBufferSize;
  send<LogLevel::Verbose>(STR("[rewind] manager created: {} slots (round start + {} keyframes), ~{} KB per slot, ~{} MB total\n"),
                          SLOTS, KEYFRAMES, per_slot / 1024, per_slot * SLOTS / (1024 * 1024));
  return true;
}

int ReplayRewind::newestSavedFrame() const {
  return key_count > 0 ? meta[keySlot(0)].game_frame : meta[START_SLOT].game_frame;
}

void ReplayRewind::afterUpdate(AREDGameState_Battle *GameState) {
  auto *engine = asw_engine::get();
  if (!engine || !ensureInstance(GameState)) return;

  const int frame = engine->GameFrame;
  if (frame == last_frame) return;      // paused / nothing simulated
  if (frame < last_frame) invalidate(); // went backwards without us (round reset); old snapshots are stale
  trackWallBreaks(GameState, frame);

  FrameMeta next;
  next.game_frame = frame;
  next.wall_breaks = wall_breaks;
  next.box = last_box;
  int reading = 0;
  for (int i = 0; i < replay_peer::COUNT; ++i) {
    const auto *peer = replay_peer::get(i);
    next.heads[i] = peer->ReadHead;
    if (!peer->is_reading()) continue;
    ++reading;
    // unused team slots sit in read mode without advancing; only check streams that move
    if (last_frame >= 0 && mismatch_logs < 20) {
      const int moved = static_cast<int>(next.heads[i].solid_position - last_heads[i].solid_position);
      if (moved != 0 && moved != frame - last_frame) {
        ++mismatch_logs;
        send<LogLevel::Warning>(STR("[rewind] peer {} read head moved {} over {} frames (frame {})\n"), i, moved,
                                frame - last_frame, frame);
      }
    }
  }
  if (reading == 0) {
    disable(STR("no replay key stream is being read"));
    return;
  }

  // the first frame seen in a round is its start; after that a keyframe every KEYFRAME_INTERVAL, also while seeking
  bool ok = true;
  if (!meta[START_SLOT].valid) {
    ok = saveSlot(START_SLOT, next);
  } else if (frame - newestSavedFrame() >= KEYFRAME_INTERVAL) {
    const int head = (key_head + 1) % KEYFRAMES;
    ok = saveSlot(1 + head, next);
    if (ok) {
      key_head = head;
      if (key_count < KEYFRAMES) ++key_count;
    }
  }
  if (!ok) {
    disable(STR("SaveBackup did not fill its slot"));
    return;
  }

  for (int i = 0; i < replay_peer::COUNT; ++i) last_heads[i] = next.heads[i];
  last_frame = frame;
  if (forward_target >= 0 && frame >= forward_target) forward_target = -1;
}

void ReplayRewind::startForward(int target) {
  forward_target = target > last_frame ? target : -1;
  forward_stalls = 0;
}

bool ReplayRewind::seek(int frames) {
  if (!owner || frames == 0 || last_frame < 0 || !meta[START_SLOT].valid) return false;

  // Restoring snapshot S and simulating one frame shows S + 1, so the earliest frame that can be shown is start + 1
  const int start = meta[START_SLOT].game_frame;
  const int target = (std::max)(position() + frames, start + 1);

  if (target >= last_frame) {
    // ahead of (or at) what has been simulated: just simulate there
    startForward(target);
    return forwardPending();
  }

  // Back: newest snapshot at or before target - 1, falling back to the round start
  int keep = 0; // keyframes that stay (the chosen one and older)
  int slot = START_SLOT;
  for (int i = 0; i < key_count; ++i) {
    if (meta[keySlot(i)].game_frame <= target - 1) {
      slot = keySlot(i);
      keep = key_count - i;
      break;
    }
  }

  const FrameMeta m = meta[slot];
  if (!restoreSlot(slot, last_frame - m.game_frame)) {
    disable(STR("Rollback did not restore the expected slot"));
    return false;
  }

  // put the replay readers back where they were at that frame
  for (int i = 0; i < replay_peer::COUNT; ++i) {
    replay_head head = m.heads[i]; // copy: FIELD setters take T&
    replay_peer::get(i)->ReadHead = head;
    last_heads[i] = m.heads[i];
  }

  // keyframes newer than the restored one are in the future now: drop them, re-simulating saves them again
  for (int i = 0; i < key_count - keep; ++i) meta[keySlot(i)].valid = false;
  key_head = keep > 0 ? slot - 1 : KEYFRAMES - 1;
  key_count = keep;

  auto *engine = asw_engine::get();
  if (engine && engine->GameFrame != m.game_frame) {
    send<LogLevel::Warning>(STR("[rewind] GameFrame after restore is {}, expected {}\n"), engine->GameFrame, m.game_frame);
  }
  last_frame = m.game_frame;
  resyncStage(m);
  wall_breaks = m.wall_breaks;
  last_box = m.box;
  // A keyframe is close enough: land on it (one simulated frame draws it). Only the round-start fallback simulates
  // up to the target, since it can be far from it.
  startForward(slot == START_SLOT ? target : m.game_frame + 1);
  return true;
}

void ReplayRewind::beginForwardTick(AREDGameState_Battle *GameState) {
  forward_saved.armed = false;
  auto *game = GameState->Rollback;
  auto *replay = GameState->ReplayManager;
  if (!forwardPending() || !game || !replay) return;

  // The whole distance runs in this one tick, so only the last frame is drawn. Progress is measured from GameFrame
  // afterwards, not from what was requested.
  const int frames = (std::min)(forward_target - last_frame, MAX_FORWARD_FRAMES);
  forward_saved.armed = true;
  forward_saved.frames = frames;
  forward_saved.from_frame = last_frame;
  forward_saved.started = std::chrono::steady_clock::now();
  forward_saved.rollback_frame = game->RollbackFrame;
  forward_saved.fast_update_finish = game->IsFastUpdateFinish;
  forward_saved.remaining_fast_forward = game->RemainingFastForwardFrames;
  forward_saved.play_scale = replay->PlayScale;

  // At 2x/3x UpdateBattle overwrites the tick's frame count with 2 or 3 after adding RollBackFrame; run this tick at
  // normal speed so the seek frames go through.
  if (forward_saved.play_scale > 0) {
    int normal = 0; // non-const: FIELD setters take T&
    replay->PlayScale = normal;
  }

  // UpdateBattle adds RollBackFrame to this tick's frame count when RollBackFrame > 0 && !IsFastUpdateFinish, then
  // sets IsFastUpdateFinish. Offline nothing else in the loop reads RollBackFrame, and UpdateOnPreTick (the only
  // place that would start a rollback from it) already ran this tick.
  if (frames > 1) {
    int extra = frames - 1; // non-const: FIELD setters take T&
    game->RollbackFrame = extra;
    game->IsFastUpdateFinish = false;
  }
}

void ReplayRewind::endForwardTick(AREDGameState_Battle *GameState) {
  if (!forward_saved.armed) return;
  forward_saved.armed = false;
  auto *game = GameState->Rollback;
  auto *replay = GameState->ReplayManager;
  if (game) {
    game->RollbackFrame = forward_saved.rollback_frame;
    game->IsFastUpdateFinish = forward_saved.fast_update_finish;
    game->RemainingFastForwardFrames = forward_saved.remaining_fast_forward;
    game->IsFastUpdateDoing = false;
  }
  // put the replay speed back, unless the speed button changed it during the tick
  if (replay && forward_saved.play_scale > 0 && replay->PlayScale == 0) replay->PlayScale = forward_saved.play_scale;

  if (last_frame < 0) return; // the round reset during the tick; invalidate() already dropped the seek
  const int advanced = last_frame - forward_saved.from_frame;
  if (forward_saved.frames > 1) {
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - forward_saved.started).count();
    send<LogLevel::Verbose>(STR("[rewind] simulated {} of {} frames in one tick: {:.1f} ms\n"), advanced,
                            forward_saved.frames, ms);
  }
  // the battle loop does not always run (outside BATTLE_STATE_BATTLE); give up instead of retrying forever
  if (advanced > 0) forward_stalls = 0;
  else if (++forward_stalls >= 3) {
    send<LogLevel::Warning>(STR("[rewind] seek to frame {} made no progress, cancelled\n"), forward_target);
    forward_target = -1;
  }
}
