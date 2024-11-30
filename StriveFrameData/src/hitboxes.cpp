#include "hitboxes.h"

#include "arcsys.h"
#include "draw_utils.h"

#if 0
#define TOO_MUCH_DEBUG(...) RC::Output::send<LogLevel::Warning>(__VA_ARGS__)
#else
#define TOO_MUCH_DEBUG(...)
#endif

static void clip_line_y(
    const FVector2D &line_a, const FVector2D &line_b,
    float min_x, float max_x,
    float *min_y, float *max_y) {
  const auto delta = line_b - line_a;

  if (abs(delta.x) > FLT_EPSILON) {
    const auto slope     = delta.y / delta.x;
    const auto intercept = line_a.y - slope * line_a.x;
    *min_y               = slope * min_x + intercept;
    *max_y               = slope * max_x + intercept;
  } else {
    *min_y = line_a.y;
    *max_y = line_b.y;
  }

  if (*min_y > *max_y)
    std::swap(*min_y, *max_y);
}

// in math_utils
bool line_box_intersection(
    const FVector2D &box_min, const FVector2D &box_max,
    const FVector2D &line_a, const FVector2D &line_b,
    float *entry_fraction, float *exit_fraction) {
  // No intersection if line runs along the edge of the box
  if (line_a.x == line_b.x && (line_a.x == box_min.x || line_a.x == box_max.x))
    return false;

  if (line_a.y == line_b.y && (line_a.y == box_min.y || line_a.y == box_max.y))
    return false;

  // Clip X values to segment within box_min.x and box_max.X
  const auto min_x = std::max(std::min(line_a.x, line_b.x), box_min.x);
  const auto max_x = std::min(std::max(line_a.x, line_b.x), box_max.x);

  // Check if the line is in the bounds of the box on the X axis
  if (min_x > max_x)
    return false;

  // Clip Y values to segment within min_x and max_x
  float min_y, max_y;
  clip_line_y(line_a, line_b, min_x, max_x, &min_y, &max_y);

  // Clip Y values to segment within box_min.y and box_max.Y
  min_y = std::max(min_y, box_min.y);
  max_y = std::min(max_y, box_max.y);

  // Check if the clipped line is in the bounds of the box on the Y axis
  if (min_y > max_y)
    return false;

  const FVector2D entry(
      line_a.x < line_b.x ? min_x : max_x,
      line_a.y < line_b.y ? min_y : max_y);

  const FVector2D exit(
      line_a.x > line_b.x ? min_x : max_x,
      line_a.y > line_b.y ? min_y : max_y);

  const auto length = (line_b - line_a).Size();
  *entry_fraction   = (entry - line_a).Size() / length;
  *exit_fraction    = (exit - line_a).Size() / length;

  return true;
}

struct DrawnHitbox {
  hitbox::box_type type;

  // Unclipped corners of filled box
  std::array<FVector2D, 4> corners;

  // Boxes to fill, clipped against other boxes
  std::vector<std::array<FVector2D, 4>> fill;

  // Outlines
  std::vector<std::array<FVector2D, 2>> lines;

  DrawnHitbox(const hitbox &box)
  : type(box.type)
  , corners{
        FVector2D(box.x, box.y),
        FVector2D(box.x + box.w, box.y),
        FVector2D(box.x + box.w, box.y + box.h),
        FVector2D(box.x, box.y + box.h)} {
    for (auto i = 0; i < 4; i++)
      lines.push_back(std::array{corners[i], corners[(i + 1) % 4]});

    fill.push_back(corners);
  }

  // Clip outlines against another hitbox
  void clip_lines(const DrawnHitbox &other) {
    auto old_lines = std::move(lines);
    lines.clear();

    for (auto &line : old_lines) {
      float entry_fraction, exit_fraction;
      auto intersected = line_box_intersection(
          other.corners[0], other.corners[2],
          line[0], line[1],
          &entry_fraction, &exit_fraction);

      if (!intersected) {
        lines.push_back(line);
        continue;
      }

      const auto delta = line[1] - line[0];

      if (entry_fraction != 0.f)
        lines.push_back(std::array{line[0], line[0] + delta * entry_fraction});

      if (exit_fraction != 1.f)
        lines.push_back(std::array{line[0] + delta * exit_fraction, line[1]});
    }
  }

  // Clip filled rectangle against another hitbox
  void clip_fill(const DrawnHitbox &other) {
    auto old_fill = std::move(fill);
    fill.clear();

    for (const auto &box : old_fill) {
      const auto &box_min = box[0];
      const auto &box_max = box[2];

      const auto clip_min = FVector2D(
          std::max(box_min.x, other.corners[0].x),
          std::max(box_min.y, other.corners[0].y));

      const auto clip_max = FVector2D(
          std::min(box_max.x, other.corners[2].x),
          std::min(box_max.y, other.corners[2].y));

      if (clip_min.x > clip_max.x || clip_min.y > clip_max.y) {
        // No intersection
        fill.push_back(box);
        continue;
      }

      if (clip_min.x > box_min.x) {
        // Left box
        fill.push_back(std::array{
            FVector2D(box_min.x, box_min.y),
            FVector2D(clip_min.x, box_min.y),
            FVector2D(clip_min.x, box_max.y),
            FVector2D(box_min.x, box_max.y)});
      }

      if (clip_max.x < box_max.x) {
        // Right box
        fill.push_back(std::array{
            FVector2D(clip_max.x, box_min.y),
            FVector2D(box_max.x, box_min.y),
            FVector2D(box_max.x, box_max.y),
            FVector2D(clip_max.x, box_max.y)});
      }

      if (clip_min.y > box_min.y) {
        // Top box
        fill.push_back(std::array{
            FVector2D(clip_min.x, box_min.y),
            FVector2D(clip_max.x, box_min.y),
            FVector2D(clip_max.x, clip_min.y),
            FVector2D(clip_min.x, clip_min.y)});
      }

      if (clip_max.y < box_max.y) {
        // Bottom box
        fill.push_back(std::array{
            FVector2D(clip_min.x, clip_max.y),
            FVector2D(clip_max.x, clip_max.y),
            FVector2D(clip_max.x, box_max.y),
            FVector2D(clip_min.x, box_max.y)});
      }
    }
  }
};

void asw_coords_to_screen(const DrawTool &tool, FVector2D &pos) {
  pos.x *= asw_engine::COORD_SCALE / 1000.F;
  pos.y *= asw_engine::COORD_SCALE / 1000.F;
  TOO_MUCH_DEBUG(STR("---asw scaled: {},{}\n"), pos.x, pos.y);

  SimpleFVector pos3d{pos.x, 0.f, pos.y};
  asw_scene::get()->camera_transform(&pos3d, nullptr);
  TOO_MUCH_DEBUG(STR("---cam transformed: {},{},{}\n"), pos3d.x, pos3d.y, pos3d.z);

  const auto proj = tool.project(pos3d);
  TOO_MUCH_DEBUG(STR("---projected: {},{},{}\n"), proj.x, proj.y, proj.z);
  pos = FVector2D(proj.x, proj.y);
}

void transform_hitbox_point(const DrawTool &tool, const asw_entity &entity, FVector2D &pos, bool is_throw) {
  TOO_MUCH_DEBUG(STR("-transform. input: {},{}, entity: {},{}\n"), pos.x, pos.y, entity.get_pos_x(), entity.get_pos_y());
  if (!is_throw) {
    TOO_MUCH_DEBUG(STR("--not throw. scale: {},{}, angle: {}\n"), entity.scale_x, entity.scale_y, entity.angle_x);
    pos.x *= -entity.scale_x;
    pos.y *= -entity.scale_y;
    TOO_MUCH_DEBUG(STR("--scaled: {},{}\n"), pos.x, pos.y);

    pos = pos.Rotate((float)entity.angle_x * (float)M_PI / 180000.f);

    TOO_MUCH_DEBUG(STR("--rotated: {},{}\n"), pos.x, pos.y);

    if (entity.facing == direction::left)
      pos.x *= -1.f;
    TOO_MUCH_DEBUG(STR("--flipped: {},{}\n"), pos.x, pos.y);
  } else if (entity.opponent != nullptr) {
    // Throw direction is based off of your facing direction
    if (entity.facing == direction::left)
      pos.x *= -1.f;

    TOO_MUCH_DEBUG(STR("--throw. flipped: {},{}\n"), pos.x, pos.y);
  }

  pos.x += entity.get_pos_x();
  pos.y += entity.get_pos_y();
  TOO_MUCH_DEBUG(STR("--offset: {},{}\n"), pos.x, pos.y);

  asw_coords_to_screen(tool, pos);
}

void draw_hitbox(const DrawTool &tool, const asw_entity &entity, const DrawnHitbox &box) {
  FLinearColor color;
  if (box.type == hitbox::box_type::hit)
    color = FLinearColor{1.f, 0.f, 0.f, .25f};
  else if (box.type == hitbox::box_type::grab)
    color = FLinearColor{1.f, 0.f, 1.f, .25f};
  else if (entity.counterhit)
    color = FLinearColor{0.f, 1.f, 1.f, .25f};
  else
    color = FLinearColor{0.f, 1.f, 0.f, .25f};

  const auto is_throw = box.type == hitbox::box_type::grab;

  TOO_MUCH_DEBUG(
      STR("Hitbox: {},{} . {},{} . {},{} . {},{}\n"),
      box.corners[0].x, box.corners[0].y,
      box.corners[1].x, box.corners[1].y,
      box.corners[2].x, box.corners[2].y,
      box.corners[3].x, box.corners[3].y);

  for (auto fill : box.fill) {
    TOO_MUCH_DEBUG(
        STR("  Init Fill: {},{} . {},{} . {},{} . {},{}\n"),
        fill[0].x, fill[0].y,
        fill[1].x, fill[1].y,
        fill[2].x, fill[2].y,
        fill[3].x, fill[3].y);
    for (auto &pos : fill)
      transform_hitbox_point(tool, entity, pos, is_throw);

    TOO_MUCH_DEBUG(
        STR("    Transformed Fill: {},{} . {},{} . {},{} . {},{}\n"),
        fill[0].x, fill[0].y,
        fill[1].x, fill[1].y,
        fill[2].x, fill[2].y,
        fill[3].x, fill[3].y);

    // fill_rect(canvas, fill, color);
    auto left   = fill[0].x;
    auto top    = fill[0].y;
    auto width  = fill[2].x - left;
    auto height = fill[2].y - top;

    TOO_MUCH_DEBUG(
        STR("    Drawn: L:{}, W:{}, T:{}, H:{}\n"),
        left, width,
        top, height);

    tool.drawRect(left, top, width, height, color);
  }

  for (const auto &line : box.lines) {
    auto start = line[0];
    auto end   = line[1];
    transform_hitbox_point(tool, entity, start, is_throw);
    transform_hitbox_point(tool, entity, end, is_throw);
    // canvas->K2_DrawLine(start, end, 2.F, color);
    tool.drawLine(start, end, color, 2.F);
  }
}

hitbox calc_afro_box(const asw_player &entity, int exIndex) {
  hitbox afro;
  afro.type = hitbox::box_type::hurt;

  const auto &extend = entity.hitboxes[exIndex];

  afro.h = static_cast<float>(entity.afroH);
  afro.w = static_cast<float>(entity.afroW);

  //  Output::send<LogLevel::Verbose>(STR("Extend Boxes: {} x {} y\n"), extend.x , extend.y);

  afro.x = extend.x - static_cast<float>(entity.afroW) / 2.f;
  afro.y = extend.y - static_cast<float>(entity.afroH) / 2.f;

  return afro;
}

hitbox calc_throw_box(const asw_player &entity) {
  // Create a fake hitbox for throws to be displayed
  hitbox box;
  box.type = hitbox::box_type::grab;

  const auto pushbox_front          = entity.pushbox_width() / 2 + entity.pushbox_front_offset;
  const int AIR_PUSHBOX_WIDTH_HALF  = 50000;
  const int AIR_PUSHBOX_HEIGHT_HALF = 75000;

  // Ground throws have `activation range y xxx` as -1 for both
  if (entity.activation_range_y_min == -1 && entity.activation_range_y_max == -1) { // Ground throw calcs
    box.x = 0.f;
    box.w = (float)(pushbox_front + entity.throw_range);
    box.y = 0.f;

    // No throw height, use pushbox height for display
    box.h = (float)entity.pushbox_height();
  } else { // Air calcs
    // Basically only accounting for the x_min < 0 < x_max case; you can probably also account for x_min < x_max < 0 or 0 < x_min < x_max but I'm lazy
    if (entity.activation_range_x_min < -1) {
      box.x = (float)std::max(entity.activation_range_x_min, -entity.throw_range - 2 * AIR_PUSHBOX_WIDTH_HALF) + AIR_PUSHBOX_WIDTH_HALF;
      box.w = (float)-box.x;
    } else {
      // box.w already preset
      box.x = -(float)(pushbox_front + entity.throw_range);
      box.w = -box.x;
    }

    if (entity.activation_range_x_max > -1) {
      box.w += (float)std::min(entity.activation_range_x_max, entity.throw_range + 2 * AIR_PUSHBOX_WIDTH_HALF) - AIR_PUSHBOX_WIDTH_HALF;
    } else {
      box.w += (float)(pushbox_front + entity.throw_range);
    }

    box.y = (float)(entity.pushboxYUpperAir - entity.pushboxYLowerAir) / 2.0f + (float)(entity.activation_range_y_min + AIR_PUSHBOX_HEIGHT_HALF);
    box.h = (float)(entity.activation_range_y_max - entity.activation_range_y_min) - AIR_PUSHBOX_HEIGHT_HALF * 2;
  }

  return box;
}

void draw_hitboxes(const DrawTool &tool, const asw_entity &entity, bool active) {
  const auto count = entity.hitbox_count + entity.hurtbox_count;

  std::vector<DrawnHitbox> hitboxes;
  // Collect hitbox info
  for (auto i = 0; i < count; i++) {
    const auto &box = entity.hitboxes[i];

    // Don't show inactive hitboxes
    if (box.type == hitbox::box_type::hit && !active) {
      continue;
    } else if (box.type == hitbox::box_type::hurt && entity.is_strike_invuln()) {
      continue;
    }

    hitboxes.push_back(DrawnHitbox(box));
  }

  if (entity.is_player) {
    asw_player &player = *(asw_player *)&entity;

    // hacky afro hurtbox
    // Jank way of finding the last hitbox (since entity.hitboxes isn't actually an array i don't think???)
    // for all hitboxes: -1000 < x,y < 1000 and w,h != 0
    // honestly idk what's loaded next in the memory after the hitboxes array
    if (player.afro && !player.is_strike_invuln()) {
      for (int i = 1; i < 6; i++) {
        if (-1000 > entity.hitboxes[count + i].x || -1000 > entity.hitboxes[count + i].y ||
            entity.hitboxes[count + i].x > 1000 || entity.hitboxes[count + i].y > 1000 ||
            entity.hitboxes[count + i].w != 0 || entity.hitboxes[count + i].h != 0) {

          hitboxes.push_back(calc_afro_box(player, count + i - 1));
          break;
        }
      }
    }

    // Add throw hitbox if in use
    if (player.throw_range >= 0 && active) {
      hitboxes.push_back(calc_throw_box(player));
    }
  }

  for (auto i = 0; i < hitboxes.size(); i++) {
    // Clip outlines
    for (auto j = 0; j < hitboxes.size(); j++) {
      if (i != j && hitboxes[i].type == hitboxes[j].type)
        hitboxes[i].clip_lines(hitboxes[j]);
    }

    // Clip fill against every hitbox after, since two boxes
    // shouldn't both be clipped against each other
    for (auto j = i + 1; j < hitboxes.size(); j++) {
      if (hitboxes[i].type == hitboxes[j].type)
        hitboxes[i].clip_fill(hitboxes[j]);
    }

    draw_hitbox(tool, entity, hitboxes[i]);
  }
}

void draw_rect_no_outline(
    const DrawTool &tool,
    const std::array<FVector2D, 4> &corners,
    const FLinearColor &color) {
  auto left   = corners[0].x;
  auto top    = corners[0].y;
  auto width  = corners[2].x - left;
  auto height = corners[2].y - top;

  tool.drawRect(left, top, width, height, color);
}

void draw_rect(
    const DrawTool &tool,
    const std::array<FVector2D, 4> &corners,
    const FLinearColor &color) {

  draw_rect_no_outline(tool, corners, color);
  for (auto i = 0; i < 4; i++) {
    tool.drawLine(corners[i], corners[(i + 1) % 4], color, 2.F);
  }
}

void draw_pushbox(const DrawTool &tool, const asw_entity &entity) {
  int left, top, right, bottom;
  entity.get_pushbox(&left, &top, &right, &bottom);

  std::array corners = {
      FVector2D(left, top),
      FVector2D(right, top),
      FVector2D(right, bottom),
      FVector2D(left, bottom)};

  TOO_MUCH_DEBUG(
      STR("Pushbox: {},{} . {},{} . {},{} . {},{}\n"),
      corners[0].x, corners[0].y,
      corners[1].x, corners[1].y,
      corners[2].x, corners[2].y,
      corners[3].x, corners[3].y);

  for (auto &pos : corners)
    asw_coords_to_screen(tool, pos);

  TOO_MUCH_DEBUG(
      STR("  Fill: {},{} . {},{} . {},{} . {},{}\n"),
      corners[0].x, corners[0].y,
      corners[1].x, corners[1].y,
      corners[2].x, corners[2].y,
      corners[3].x, corners[3].y);

  // Show hollow pushbox when throw invuln or can't interact
  FLinearColor color;
  if (entity.is_throw_invuln() || !entity.is_pushbox_active())
    color = FLinearColor{1.f, 1.f, 0.f, 0.f};
  else
    color = FLinearColor{1.f, 1.f, 0.f, .2f};

  // Show outlined pushbox when pushbox doesn't have intangibility
  if (entity.is_pushbox_active() || !entity.is_throw_invuln())
    draw_rect(tool, corners, color);
  //  else
  //    draw_rect_no_outline(tool, corners, color);
}

void drawAllBoxes() {
  const auto *engine = asw_engine::get();
  if (!engine) return;

  const auto &tool = DrawTool::instance();

  for (auto entidx = engine->entity_count - 1; entidx >= 0; entidx--) {
    const auto &entity = *engine->entities[entidx];

    draw_pushbox(tool, entity);

    const auto active = entity.is_active();
    draw_hitboxes(tool, entity, active);

    const auto *attached = entity.attached;
    while (attached != nullptr) {
      //      Output::send<LogLevel::Verbose>(STR("Attached Entity\n"));

      draw_hitboxes(tool, *attached, active);
      attached = attached->attached;
    }

    // draw_origin(canvas, entity);
    // draw_debuglines(canvas, entity);
  }
}