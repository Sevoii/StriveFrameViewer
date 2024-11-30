#pragma once

#include "draw_utils.h"

struct Palette {
  FLinearColor projectile_color;
  FLinearColor background_color;
  FLinearColor state_colors[8];
};

struct CurrentOptions {
  const Palette &palette;
  bool show_fade;
  bool show_delim;
  bool show_cancels;
  bool show_crossups;
};

struct PressedKeys {
  bool toggle_framebar;
  bool toggle_hitbox;
  bool toggle_menu;
  bool go_up;
  bool go_down;
  bool rotate_right;
  bool rotate_left;
};

class ModMenu {
  DrawContext tool;

  //  std::vector<int> settings;

  bool is_showing     = false;
  int cursor_position = 0;

  void changeSetting(size_t idx, bool right = true);

  ModMenu();

public:
  ~ModMenu();
  static ModMenu &instance();

  void update(PressedKeys data);
  void draw();

  bool barEnabled() const;
  bool hitboxEnabled() const;
  bool fadeEnabled() const;
  bool delimEnabled() const;
  bool cancelEnabled() const;
  bool dashEnabled() const;
  int pauseType() const;
  int delayAmount() const;
  bool crossupEnabled() const;

  CurrentOptions getScheme() const;
};
