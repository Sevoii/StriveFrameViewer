#pragma once

#include <UE4SSProgram.hpp>
#include <UnrealDef.hpp>

#include "arcsys.h"

struct FLinearColor {
  float R = 0.f, G = 0.f, B = 0.f, A = 0.f;

  FLinearColor operator*(float mod) const {
    FLinearColor result = *this;
    result.R *= mod;
    result.G *= mod;
    result.B *= mod;
    return result;
  }
};

enum EBlendMode {
  BLEND_Opaque,
  BLEND_Masked,
  BLEND_Translucent,
  BLEND_Additive,
  BLEND_Modulate,
  BLEND_AlphaComposite,
  BLEND_AlphaHoldout,
  BLEND_MAX
};

struct FVector2D {
  float x;
  float y;

  FVector2D()
  : x(0)
  , y(0) {}
  FVector2D(float X, float Y)
  : x(X)
  , y(Y) {}
  FVector2D(double X, double Y)
  : x(static_cast<float>(X))
  , y(static_cast<float>(Y)) {}
  FVector2D(int X, int Y)
  : x(static_cast<float>(X))
  , y(static_cast<float>(Y)) {}

  FVector2D Rotate(const float angle) const {
    const auto ca = cosf(angle);
    const auto sa = sinf(angle);
    return FVector2D(x * ca + y * -sa, x * sa + y * ca);
  }
  float SizeSquared() const {
    return x * x + y * y;
  }

  float Size() const {
    return sqrt(SizeSquared());
  }

  FVector2D operator+(const FVector2D &other) const {
    return FVector2D(x + other.x, y + other.y);
  }

  FVector2D operator-(const FVector2D &other) const {
    return FVector2D(x - other.x, y - other.y);
  }

  FVector2D operator*(float scalar) const {
    return FVector2D(x * scalar, y * scalar);
  }

  FVector2D operator/(float scalar) const {
    return FVector2D(x / scalar, y / scalar);
  }
};

namespace DrawParams {
  struct Size {
    int32_t SizeX = 0;
    int32_t SizeY = 0;
  };
  struct Rect {
    FLinearColor RectColor;
    float ScreenX;
    float ScreenY;
    float ScreenW;
    float ScreenH;
  };
  struct Text {
    RC::Unreal::FString Text;
    FLinearColor TextColor;
    float ScreenX;
    float ScreenY;
    void *Font;
    float Scale;
    bool bScalePosition;
  };
  struct Line {
    float StartScreenX;
    float StartScreenY;
    float EndScreenX;
    float EndScreenY;
    FLinearColor LineColor;
    float LineThickness;
  };
  struct Icon {
    void *Texture;
    float ScreenX;
    float ScreenY;
    float ScreenW;
    float ScreenH;
    float TextureU;
    float TextureV;
    float TextureUWidth;
    float TextureVHeight;
    FLinearColor TintColor;
    EBlendMode BlendMode;
    float Scale;
    bool bScalePosition;
    float Rotation;
    FVector2D RotPivot;
  };
  struct Project {
    SimpleFVector location;
    SimpleFVector result;
  };
}

class DrawTool {
  bool valid                      = false;
  Unreal::UObject *ref_hud        = nullptr;
  Unreal::UObject *ref_player     = nullptr;
  Unreal::UObject *ref_font       = nullptr;
  Unreal::UFunction *ref_getsize  = nullptr;
  Unreal::UFunction *ref_project  = nullptr;
  Unreal::UFunction *ref_drawrect = nullptr;
  Unreal::UFunction *ref_drawtext = nullptr;
  Unreal::UFunction *ref_drawline = nullptr;

  double screen_width  = 0.0;
  double screen_height = 0.0;
  double units         = 0.0;

  DrawTool();
  DrawTool(DrawTool &)        = delete;
  DrawTool(const DrawTool &&) = delete;

public:
  ~DrawTool();

  static DrawTool &instance();

  void initialize();
  bool update(void *actual_hud);
  double getWidth() const { return screen_width; }
  double getHeight() const { return screen_height; }
  double getUnits() const { return units; }

  SimpleFVector project(const SimpleFVector &world_coords) const {
    DrawParams::Project params{
        world_coords,
        SimpleFVector{-1, -1, -1}};
    ref_hud->ProcessEvent(ref_project, &params);
    return params.result;
  }

  // params are in screen space coordinates
  void drawRect(double left, double top, double width, double height, const FLinearColor &color) const {
    DrawParams::Rect params{
        color,
        static_cast<float>(left),
        static_cast<float>(top),
        static_cast<float>(width),
        static_cast<float>(height)};
    ref_hud->ProcessEvent(ref_drawrect, &params);
  }
  void drawText(double left, double top, const Unreal::FString &text, const FLinearColor &color, double scale) const {
    DrawParams::Text params{
        text,
        color,
        static_cast<float>(left),
        static_cast<float>(top),
        ref_font,
        static_cast<float>(scale),
        false};
    ref_hud->ProcessEvent(ref_drawtext, &params);
  }
  void drawLine(FVector2D start, FVector2D end, const FLinearColor &color, float thickness) const {
    DrawParams::Line params{
        start.x,
        start.y,
        end.x,
        end.y,
        color,
        thickness};
    ref_hud->ProcessEvent(ref_drawline, &params);
  }
  void drawOutlinedText(double left, double top, const Unreal::FString &text, double scale) const;
};

// handles transformation from draw space to screen space
class DrawContext {
  double ratio_x;
  double ratio_y;
  double center_offset_x = 0.0;
  double center_offset_y = 0.0;
  const DrawTool *tool;

public:
  DrawContext(double x, double y)
  : ratio_x(x)
  , ratio_y(y)
  , tool(&DrawTool::instance()) // storing it is faster than the singleton access due to imposed thread safety
  {
  }
  void update() {
    center_offset_x = ratio_x * tool->getWidth();
    center_offset_y = ratio_y * tool->getHeight();
  }

  // params are in draw space coordinates
  void drawRect(int x, int y, int width, int height, const FLinearColor &color) const {
    double units      = tool->getUnits();
    double prj_x      = center_offset_x + (units * x);
    double prj_y      = center_offset_y + (units * y);
    double prj_width  = units * width;
    double prj_height = units * height;
    tool->drawRect(prj_x, prj_y, prj_width, prj_height, color);
  }
  void drawOutlinedText(int x, int y, const Unreal::FString &text, double scale) const {
    double units     = tool->getUnits();
    double prj_x     = center_offset_x + (units * x);
    double prj_y     = center_offset_y + (units * y);
    double prj_scale = units * scale;
    tool->drawOutlinedText(prj_x, prj_y, text, prj_scale);
  }
};
