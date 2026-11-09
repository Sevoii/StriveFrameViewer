#pragma once

#include "arcsys.h"

#include <chrono>

/*
  Replay rewind: seek back and forward through a replay, paused or playing.

  The mod owns one BATTLE_CRollbackManager (asw_rollback::create_owned) and snapshots the battle into it, together
  with the replay key-stream read heads. A snapshot is ~2.5-2.7 MB. Slots:
    slot 0:       the first frame of the round, kept for the whole round
    slots 1..30:  a ring of keyframes, one every KEYFRAME_INTERVAL frames (the last 30 s), oldest overwritten

  Every seek ends by simulating forward:
    back:    restore the newest keyframe before the target and drop every keyframe newer than it, then simulate one
             frame to draw it (so a back seek lands up to KEYFRAME_INTERVAL frames before the target). If the ring
             does not reach that far, restore the round start and fast-simulate up to the target instead; those
             frames refill the ring.
    forward: fast-simulate up to the target; keyframes are saved along the way like during normal playback.
  Fast simulation goes through UpdateBattle's own multi-frame loop (the one netplay resimulation and replay
  fast-forward use), with the whole distance in a single tick, so only the final frame is drawn. The replay speed is
  held at normal for that tick (2x/3x would cap the loop). While paused the caller lets that tick through.

  Engine side effects: Rollback() runs the object manager's PreRollBack/PostRollBack, which use the GAME's rollback
  manager (red::btl::GetRollbackManager()) for particle/pawn/audio cleanup. For the duration of the restore its
  RollbackFrame is set to the real distance, then its particle/pawn caches are cleared, as netplay does after a
  resimulation. Its snapshot slots and the replay key buffer are never written.

  Wall breaks: the stage box (BattleBGLocation) is in the snapshot, but the stage blueprint's response to the break
  (OnBoxMoveStart/OnBgChangeInBoxMove/OnBoxMoveEnd: background swap, loaded/visible assets) is not. A restore to a
  snapshot from before a break that happened since replays those level events for the restored box (stage_sync).
*/
class ReplayRewind {
public:
  static constexpr int KEYFRAMES = 30;          // keyframe ring size
  static constexpr int KEYFRAME_INTERVAL = 60;  // one keyframe per second
  static constexpr int SLOTS = 1 + KEYFRAMES;   // + the round start
  static constexpr int START_SLOT = 0;
  static constexpr int MAX_FORWARD_FRAMES = 100 * 60; // frames simulated in one tick while seeking (a full round)

  // drop the snapshot buffer (frees memory)
  void reset();
  // snapshots are only valid within one round
  void invalidate();
  // after each UpdateBattleSub in replay mode; saves when GameFrame advanced
  void afterUpdate(class AREDGameState_Battle *GameState);
  // Seek `frames` frames from the current position (negative = back), relative to where a seek in progress will
  // end. Clamped to the round start. Returns true if a seek was started.
  bool seek(int frames);

  bool forwardPending() const { return forward_target >= 0 && last_frame >= 0 && last_frame < forward_target; }
  // around orig_UpdateBattle on a tick that is allowed to simulate: arms the extra frames, then puts the game's
  // rollback manager fields back
  void beginForwardTick(AREDGameState_Battle *GameState);
  void endForwardTick(AREDGameState_Battle *GameState);

private:
  struct FrameMeta {
    bool valid = false;
    int game_frame = 0;
    int wall_breaks = 0; // wall breaks this round before this frame
    int box = -1;        // stage box at this frame
    replay_head heads[replay_peer::COUNT] = {};
  };

  bool ensureInstance(AREDGameState_Battle *GameState);
  void disable(const wchar_t *reason);
  bool saveSlot(int slot, const FrameMeta &m);
  // restore one slot without disturbing the others; `frames_back` is the real frame distance (for the engine's cleanup)
  bool restoreSlot(int slot, int frames_back);

  // keyframe ring, chronological: i = 0 is the newest
  int keySlot(int i) const { return 1 + (key_head - i + KEYFRAMES) % KEYFRAMES; }
  int newestSavedFrame() const;
  // where the replay will be once a seek in progress finishes
  int position() const { return forwardPending() ? forward_target : last_frame; }
  void startForward(int target);

  asw_rollback *store = nullptr;
  FrameMeta meta[SLOTS];
  int key_head = KEYFRAMES - 1; // ring position of the newest keyframe
  int key_count = 0;

  AREDGameState_Battle *owner = nullptr; // the buffer is sized for this battle; rebuilt when it changes
  int last_frame = -1;
  replay_head last_heads[replay_peer::COUNT] = {};
  bool disabled = false;
  int mismatch_logs = 0;

  // Wall breaks: the stage blueprint swaps background/assets per box from level events that snapshots do not hold,
  // so a restore across a break has to make the stage re-sync itself to the restored box.
  int wall_breaks = 0;
  int last_box = -1;
  void trackWallBreaks(AREDGameState_Battle *GameState, int frame);
  void resyncStage(const FrameMeta &m);

  int forward_target = -1; // GameFrame the seek ends on
  int forward_stalls = 0;  // forward ticks in a row that did not advance GameFrame
  struct {
    bool armed = false;
    int frames = 0;
    int from_frame = 0;
    std::chrono::steady_clock::time_point started;
    int play_scale = 0;
    int rollback_frame = 0;
    bool fast_update_finish = false;
    int remaining_fast_forward = 0;
  } forward_saved;
};
