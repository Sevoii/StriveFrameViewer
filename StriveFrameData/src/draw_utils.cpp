#include "draw_utils.h"

namespace {
  constexpr double EXPECTED_DISP_RATIO = 16.0 / 9.0;
  constexpr double PROJECTED_RATIO     = 0.0003f;
  constexpr double OUTLINE_THICKNESS   = 1.5;

  FLinearColor color_white{1.f, 1.f, 1.f, 1.f};
  FLinearColor color_black{0.f, 0.f, 0.f, 1.f};
}

DrawTool::DrawTool()  = default;
DrawTool::~DrawTool() = default;

DrawTool &DrawTool::instance() {
  static DrawTool me;
  return me;
}

Unreal::UObject *getFont() {
  static auto ufont_class_name = Unreal::FName(STR("Font"), Unreal::FNAME_Add);
  std::vector<RC::Unreal::UObject *> all_fonts;
  UObjectGlobals::FindAllOf(ufont_class_name, all_fonts);
  for (auto *font : all_fonts) {
    if (font->GetName() == L"RobotoDistanceField") {
      return font;
    }
  }
  return nullptr;
}

void DrawTool::initialize() {
  valid = false;

  static auto hud_class_name           = Unreal::FName(STR("REDHUD_Battle"), Unreal::FNAME_Add);
  static auto hud_drawrect_func_name   = Unreal::FName(STR("DrawRect"), Unreal::FNAME_Add);
  static auto hud_drawtext_func_name   = Unreal::FName(STR("DrawText"), Unreal::FNAME_Add);
  static auto hud_drawline_func_name   = Unreal::FName(STR("DrawLine"), Unreal::FNAME_Add);
  static auto hud_project_func_name    = Unreal::FName(STR("Project"), Unreal::FNAME_Add);
  static auto hud_getplayer_func_name  = Unreal::FName(STR("GetOwningPlayerController"), Unreal::FNAME_Add);
  static auto player_getsize_func_name = Unreal::FName(STR("GetViewportSize"), Unreal::FNAME_Add);

  ref_hud = UObjectGlobals::FindFirstOf(hud_class_name);
  if (!ref_hud)
    return;

  ref_drawrect                      = ref_hud->GetFunctionByNameInChain(hud_drawrect_func_name);
  ref_drawtext                      = ref_hud->GetFunctionByNameInChain(hud_drawtext_func_name);
  ref_drawline                      = ref_hud->GetFunctionByNameInChain(hud_drawline_func_name);
  ref_project                       = ref_hud->GetFunctionByNameInChain(hud_project_func_name);
  Unreal::UFunction *getplayer_func = ref_hud->GetFunctionByNameInChain(hud_getplayer_func_name);
  if (!ref_drawrect || !ref_drawtext || !getplayer_func || !ref_project || !ref_drawline)
    return;

  ref_hud->ProcessEvent(getplayer_func, &ref_player);
  if (!ref_player)
    return;

  ref_getsize = ref_player->GetFunctionByNameInChain(player_getsize_func_name);
  if (!ref_getsize)
    return;

  ref_font = getFont();
  if (!ref_font)
    return;

  valid = true;
  update(ref_hud);
}

bool DrawTool::update(void *actual_hud) {
  if (!valid || actual_hud != ref_hud)
    return false;

  DrawParams::Size size_data;
  ref_player->ProcessEvent(ref_getsize, &size_data);

  screen_width  = size_data.SizeX;
  screen_height = size_data.SizeY;

  // Strive always renders to a 16:9 region, we need to fix this here since our size data is the "true" screen space window size
  const double actual_ratio = screen_width / screen_height;
  if (actual_ratio > EXPECTED_DISP_RATIO) { // 21:9 Ultrawide monitor probably
    screen_width = screen_height * EXPECTED_DISP_RATIO;
  } else if (actual_ratio < EXPECTED_DISP_RATIO) { // 4:3 or 16:10 monitor probably
    screen_height = screen_width / EXPECTED_DISP_RATIO;
  }
  units = screen_width * PROJECTED_RATIO;
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