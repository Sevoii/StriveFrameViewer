#pragma once

/*
  Stage (level blueprint) sync for restores across a wall break.

  A wall break moves the battle to another stage box. The engine half of that (BattleBGLocation, screen manager,
  camera) is in the rollback snapshot; the stage half is not: the level blueprint reacts to AREDLevelEvent's
  delegates (OnBoxMoveStart -> OnBgChangeInBoxMove -> OnBoxMoveEnd) by swapping background pieces, visibility and
  loaded assets, and nothing ever tells it to undo that. enterBox() replays that same sequence for the restored
  box, which is how the blueprint gets into a box in the first place.
*/
namespace stage_sync {
// Broadcast the box-entry level events for `box` on every AREDLevelEvent in the world. Returns the number of bound
// blueprint functions called.
int enterBox(int box, bool from_left);
} // namespace stage_sync
