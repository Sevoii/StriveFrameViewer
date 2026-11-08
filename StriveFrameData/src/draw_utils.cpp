#include "draw_utils.h"

namespace {
  constexpr double EXPECTED_DISP_RATIO = 16.0 / 9.0;
  constexpr double PROJECTED_RATIO = 0.0003f;
  constexpr double OUTLINE_THICKNESS = 1.5;

  FLinearColor color_white{1.f, 1.f, 1.f, 1.f};
  FLinearColor color_black{0.f, 0.f, 0.f, 1.f};
}

DrawTool::DrawTool() = default;
DrawTool::~DrawTool() = default;

DrawTool &DrawTool::instance() {
  static DrawTool me;
  return me;
}

// direct lookup by object path (hashed), instead of scanning every UFont in the object array.
// engine content fonts are always loaded and rooted, so the pointer is cached for the whole session.
Unreal::UObject *getFont() {
  static Unreal::UObject *font = nullptr;
  if (!font) {
    font = UObjectGlobals::StaticFindObject<Unreal::UObject *>(nullptr, nullptr, STR("/Engine/EngineFonts/RobotoDistanceField.RobotoDistanceField"));
    if (!font) Output::send<LogLevel::Error>(STR("StriveFrameViewer: RobotoDistanceField font not found\n"));
  }
  return font;
}

// UFunctions belong to the class (AHUD / APlayerController natives), not the instance, so they are the same for
// every HUD the game creates and only need resolving once. the font is rooted engine content, also permanent.
bool DrawTool::resolveFunctions(Unreal::UObject *hud, Unreal::UObject *player) {
  if (!ref_drawrect) {
    static auto hud_drawrect_func_name = Unreal::FName(STR("DrawRect"), Unreal::FNAME_Add);
    static auto hud_drawtext_func_name = Unreal::FName(STR("DrawText"), Unreal::FNAME_Add);
    static auto hud_drawline_func_name = Unreal::FName(STR("DrawLine"), Unreal::FNAME_Add);
    static auto hud_project_func_name = Unreal::FName(STR("Project"), Unreal::FNAME_Add);

    auto *drawrect = hud->GetFunctionByNameInChain(hud_drawrect_func_name);
    auto *drawtext = hud->GetFunctionByNameInChain(hud_drawtext_func_name);
    auto *drawline = hud->GetFunctionByNameInChain(hud_drawline_func_name);
    auto *project = hud->GetFunctionByNameInChain(hud_project_func_name);
    if (!drawrect || !drawtext || !drawline || !project) return false;

    ref_drawtext = drawtext;
    ref_drawline = drawline;
    ref_project = project;
    ref_drawrect = drawrect; // set last: it doubles as the "hud functions resolved" flag
  }
  if (!ref_getsize) {
    static auto player_getsize_func_name = Unreal::FName(STR("GetViewportSize"), Unreal::FNAME_Add);
    ref_getsize = player->GetFunctionByNameInChain(player_getsize_func_name);
    if (!ref_getsize) return false;
  }
  if (!ref_font) {
    ref_font = getFont();
    if (!ref_font) return false;
  }
  return true;
}

// called from AHUD::PostRender with the HUD that is drawing right now. its canvas is only valid during this call,
// so we always draw on exactly this HUD and never keep it across frames; a HUD recreated mid-match just works.
bool DrawTool::update(void *actual_hud) {
  valid = false;
  ref_hud = static_cast<Unreal::UObject *>(actual_hud);
  ref_player = nullptr;
  if (!ref_hud) return false;

  if (!ref_getplayer) {
    static auto hud_getplayer_func_name = Unreal::FName(STR("GetOwningPlayerController"), Unreal::FNAME_Add);
    ref_getplayer = ref_hud->GetFunctionByNameInChain(hud_getplayer_func_name);
    if (!ref_getplayer) return false;
  }

  ref_hud->ProcessEvent(ref_getplayer, &ref_player);
  if (!ref_player) return false;

  if (!resolveFunctions(ref_hud, ref_player)) return false;

  DrawParams::Size size_data;
  ref_player->ProcessEvent(ref_getsize, &size_data);

  screen_width = size_data.SizeX;
  screen_height = size_data.SizeY;

  // Strive always renders to a 16:9 region, we need to fix this here since our size data is the "true" screen space window size
  const double actual_ratio = screen_width / screen_height;
  if (actual_ratio > EXPECTED_DISP_RATIO) { // 21:9 Ultrawide monitor probably
    screen_width = screen_height * EXPECTED_DISP_RATIO;
  } else if (actual_ratio < EXPECTED_DISP_RATIO) { // 4:3 or 16:10 monitor probably
    screen_height = screen_width / EXPECTED_DISP_RATIO;
  }
  units = screen_width * PROJECTED_RATIO;
  valid = true;
  return true;
}

void DrawTool::drawOutlinedText(double left, double top, const Unreal::FString &text, double scale) const {
#if 1 // cardinals
  drawText(left - OUTLINE_THICKNESS, top, text, color_black, scale);
  drawText(left, top - OUTLINE_THICKNESS, text, color_black, scale);
  drawText(left + OUTLINE_THICKNESS, top, text, color_black, scale);
  drawText(left, top + OUTLINE_THICKNESS, text, color_black, scale);
#endif
#if 1 // diagonals
  drawText(left - OUTLINE_THICKNESS, top - OUTLINE_THICKNESS, text, color_black, scale);
  drawText(left + OUTLINE_THICKNESS, top - OUTLINE_THICKNESS, text, color_black, scale);
  drawText(left + OUTLINE_THICKNESS, top + OUTLINE_THICKNESS, text, color_black, scale);
  drawText(left - OUTLINE_THICKNESS, top + OUTLINE_THICKNESS, text, color_black, scale);
#endif

  drawText(left, top, text, color_white, scale);
}