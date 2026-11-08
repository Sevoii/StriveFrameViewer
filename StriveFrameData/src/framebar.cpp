#include "framebar_p.h"

#include <sstream>

namespace {
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
// FrameHistory

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
// FrameBar::Data

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
