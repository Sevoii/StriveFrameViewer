#include "menu.h"
#include <fstream>
#include <string>

class RedInputChecker {
  unsigned short novel_inputs;

public:
  enum InputFlag {
    IF_Up    = 0x1,
    IF_Down  = 0x2,
    IF_Left  = 0x4,
    IF_Right = 0x8,
    IF_P     = 0x10,
    IF_K     = 0x20,
    IF_S     = 0x40,
    IF_H     = 0x80,
    IF_Dust  = 0x100,
    IF_ANY   = 0x1FF
  };
  RedInputChecker(asw_inputs &inputs) {
    novel_inputs = inputs.m_CurRecFlg & (~inputs.m_PreRecFlg);
  }
  bool checkInput(InputFlag flag) const { return novel_inputs & flag; }

  /*const IconLoc& getDirLoc() const {
    int offset = 0;
    if(checkInput(IF_Up)) offset += 3;
    else if(checkInput(IF_Down)) offset += 6;

    if(checkInput(IF_Left)) offset += 1;
    else if(checkInput(IF_Right)) offset += 2;

    return ICON_DIR_LOCS[offset];
  }
  const IconLoc& getButtonLoc() const {
    if(checkInput(IF_P)) return ICON_BUTTON_P_LOC;
    if(checkInput(IF_K)) return ICON_BUTTON_K_LOC;
    if(checkInput(IF_S)) return ICON_BUTTON_S_LOC;
    if(checkInput(IF_H)) return ICON_BUTTON_H_LOC;
    if(checkInput(IF_Dust)) return ICON_BUTTON_D_LOC;
    return ICON_NULL_LOC;
  }*/
  void debug() const {
    RC::Output::send<LogLevel::Warning>(STR("Input: {}\n"), novel_inputs);
  }
};

// Color Stuff
float OneOver255 = 1.0f / 255.0f;
float sRGBToLinear(int pc) {
  float c = pc * OneOver255;
  return c > 0.04045 ? pow(c * (1.0 / 1.055) + 0.0521327, 2.4) : c * (1.0 / 12.92);
}

FLinearColor convSRGB(int r, int g, int b, int a) {
  return FLinearColor{sRGBToLinear(r), sRGBToLinear(g), sRGBToLinear(b), a * OneOver255};
}

struct OptionData {
  const wchar_t *title;
  size_t count;
  std::array<const wchar_t *, 5> values;
};

struct SettingsEntry {
  const OptionData display;
  const std::string id_;
  size_t value;
};

namespace Settings {
  SettingsEntry FRAMEBAR = SettingsEntry{
      OptionData{
          L"Frame Bar:", 2, {L"<  Disabled  >", L"<  Enabled   >"}},
      "framebar",
      1};
  SettingsEntry HITBOXES = SettingsEntry{
      OptionData{L"Hitboxes:", 2, {L"<  Disabled  >", L"<  Enabled   >"}},
      "hitboxes",
      0};
  SettingsEntry FADE = SettingsEntry{
      OptionData{L"Fade Effect:", 2, {L"<  Disabled  >", L"<  Enabled   >"}},
      "fade",
      1};
  SettingsEntry DELIM = SettingsEntry{
      OptionData{L"Delim Segments:", 2, {L"<  Disabled  >", L"<  Enabled   >"}},
      "delim",
      0};
  SettingsEntry SHOW_CANCEL = SettingsEntry{
      OptionData{L"Show Cancel Window:", 2, {L"<  Disabled  >", L"<  Enabled   >"}},
      "show_cancel",
      0};
  SettingsEntry COLOR_SCHEME = SettingsEntry{
      OptionData{L"Color Scheme:", 4, {L"<    SF6     >", L"<  Classic   >", L"<  Dustloop  >", L"< Colorblind >"}},
      "color_scheme",
      1};
  SettingsEntry SHOW_DASH_FRAMES = SettingsEntry{
      OptionData{L"Show Dash:", 2, {L"<  Disabled  >", L"<  Enabled   >"}},
      "show_dash",
      0};
  SettingsEntry PAUSE_TYPE = SettingsEntry{
      OptionData{L"Pause Type: ", 2, {L"<   Default   >", L"<     WIP     >"}},
      "pause_type",
      0};
  SettingsEntry DELAY_AMOUNT = SettingsEntry{
      OptionData{L"Delay Frames: ", 4, {L"<     0ms     >", L"<     20ms    >", L"<     30ms    >", L"<     60ms    >"}},
      "delay_amount",
      0};
  SettingsEntry CROSSUP_ENABABLED = SettingsEntry{
      OptionData{L"Show Crossup: ", 2, {L"<  Disabled  >", L"<  Enabled   >"}},
      "crossup_enabled",
      0};

  std::array<SettingsEntry *, 9> settings = {
      &FRAMEBAR,
      &HITBOXES,
      &FADE,
      &DELIM,
      //      &SHOW_CANCEL,
      &COLOR_SCHEME,
      &SHOW_DASH_FRAMES,
      &PAUSE_TYPE,
      &DELAY_AMOUNT,
      &CROSSUP_ENABABLED,
  };

  const std::filesystem::path WORKING_DIRECTORY = UE4SSProgram::get_program().get_working_directory();
  const auto CONFIG_PATH                        = WORKING_DIRECTORY / "StriveFrameViewer.txt";

  // TODO: figure out if I want to statically define or just do a name find
  size_t FRAMEBAR_INDEX = 0;
  size_t HITBOX_INDEX   = 1;

  int indexById(const std::string &id_) {
    for (int i = 0; i < settings.size(); i++) {
      if (id_ == settings[i]->id_) {
        return i;
      }
    }

    return -1;
  }

  void readConfig() {
    std::ifstream configFile(CONFIG_PATH);

    if (!configFile) {
      return;
    }

    std::string line;
    while (std::getline(configFile, line)) {
      size_t pos = line.find('=');
      if (pos == std::string::npos) continue;

      std::string key = line.substr(0, pos);
      int val;

      try {
        val = std::stoi(line.substr(pos + 1));
      } catch (...) {
        //        RC::Output::send<LogLevel::Verbose>(STR("Could not find {}\n"), line);
        continue;
      }

      int index = indexById(key);
      if (index == -1) {
        //        RC::Output::send<LogLevel::Verbose>(STR("Could not find {}\n"), line);
        continue;
      }

      settings[index]->value = val;
    }

    configFile.close();
  }

  void saveConfig() {
    std::ofstream configFile(CONFIG_PATH);

    if (!configFile) {
      RC::Output::send<LogLevel::Error>(STR("Unable to save config file\n"));
      return;
    }

    for (auto &entry : settings) {
      configFile << entry->id_ << "=" << entry->value << "\n";
    }

    configFile.close();
    RC::Output::send<LogLevel::Verbose>(STR("Saved config file\n"));
  }
}

namespace {
  size_t OPTION_COUNT = Settings::settings.size();

  int buildTitleWidth() {
    int max_width = 0;
    for (const auto &entry : Settings::settings) {
      max_width = std::wstring_view(entry->display.title).size();
    }
    return max_width;
  }

  int buildValueWidth() {
    int max_width = 0;
    for (const auto &entry : Settings::settings) {
      for (size_t idx = 0; idx < entry->display.count; idx++) {
        max_width = std::wstring_view(entry->display.values[idx]).size();
      }
    }
    return max_width;
  }

  constexpr double CENTER_X_RATIO = 0.5f;
  constexpr double CENTER_Y_RATIO = 0.5f;
  constexpr double MENU_PADDING   = 30;
  constexpr double OPTION_HEIGHT  = 60;
  constexpr double OPTION_SPACING = 20;
  constexpr double COL_SPACING    = 20;

  double TITLE_WIDTH = buildTitleWidth() * 24;
  double VALUE_WIDTH = buildValueWidth() * 24;
  double MENU_WIDTH  = TITLE_WIDTH + COL_SPACING + VALUE_WIDTH + (2 * MENU_PADDING);

  double MENU_HEIGHT = 2 * MENU_PADDING + OPTION_COUNT * (OPTION_HEIGHT + OPTION_SPACING) - OPTION_SPACING;
  double MENU_TOP    = -MENU_HEIGHT / 2;
  double MENU_LEFT   = -MENU_WIDTH / 2;
  double OPTION_LEFT = MENU_LEFT + MENU_PADDING;
  double VALUE_LEFT  = OPTION_LEFT + TITLE_WIDTH + COL_SPACING;

  FLinearColor background_color{0.2f, 0.2f, 0.2f, 0.8f};
  FLinearColor cursor_color{0.5f, 0.0f, 0.0f, 1.0f};

  static const Palette color_palettes[] = {
      Palette{// SF6
              convSRGB(201, 128, 0, 255),
              convSRGB(0, 0, 0, 255),
              {
                  convSRGB(26, 26, 26, 255),   // IDLE
                  convSRGB(253, 177, 46, 255), // Block
                  convSRGB(253, 245, 46, 255), // Hit
                  convSRGB(1, 182, 149, 255),  // Busy
                  convSRGB(205, 43, 103, 255), // Attacking
                  convSRGB(1, 111, 188, 255),  // Projectile
                  convSRGB(1, 111, 188, 255),  // Recovering
                  convSRGB(26, 26, 26, 255),   // Forward Dash
              }},
      Palette{// CLASSIC
              FLinearColor{.8f, .1f, .1f, 1.f},
              FLinearColor{0.05f, 0.05f, 0.05f, 0.7f},
              {
                  FLinearColor{.2f, .2f, .2f, .9f}, // IDLE
                  FLinearColor{.1f, .1f, .8f, .9f}, // Block
                  FLinearColor{.1f, .6f, .1f, .9f}, // Hit
                  FLinearColor{.7f, .7f, .1f, .9f}, // Busy
                  FLinearColor{.8f, .1f, .1f, .9f}, // Attacking
                  FLinearColor{.8f, .4f, .1f, .9f}, // Projectile
                  FLinearColor{.8f, .4f, .1f, .9f}, // Recovering
                  FLinearColor{.2f, .2f, .2f, .9f}, // Forward Dash
              }},
      Palette{// DUSTLOOP
              convSRGB(255, 0, 0, 255),
              convSRGB(23, 28, 38, 255),
              {
                  convSRGB(128, 128, 128, 255), // IDLE
                  convSRGB(233, 172, 4, 255),   // Block
                  convSRGB(233, 215, 4, 255),   // Hit
                  convSRGB(54, 179, 126, 255),  // Busy
                  convSRGB(255, 93, 93, 255),   // Attacking
                  convSRGB(0, 105, 182, 255),   // Projectile
                  convSRGB(0, 105, 182, 255),   // Recovering
                  convSRGB(128, 128, 128, 255), // Forward Dash
              }},
      Palette{// COLORBLIND
              FLinearColor{.8f, .1f, .1f, 1.f},
              FLinearColor{0.05f, 0.05f, 0.05f, 0.7f},
              {
                  FLinearColor{.2f, .2f, .2f, .9f}, // IDLE
                  FLinearColor{.1f, .1f, .8f, .9f}, // Block
                  FLinearColor{.1f, .6f, .1f, .9f}, // Hit
                  FLinearColor{.7f, .7f, .1f, .9f}, // Busy
                  FLinearColor{.8f, .1f, .1f, .9f}, // Attacking
                  FLinearColor{.8f, .4f, .1f, .9f}, // Projectile
                  FLinearColor{.8f, .4f, .1f, .9f}, // Recovering
                  FLinearColor{.2f, .2f, .2f, .9f}, // Forward Dash
              }},
  };
}

size_t rotateVal(size_t val, bool positive, size_t max) {
  if (positive) {
    return (val + 1) % max;
  } else if (val == 0) {
    return max - 1;
  } else {
    return val - 1;
  }
}

// TODO later: maybe move this somewhere else?
// TODO later: maybe instead of idx do SettingsEntry entry
void ModMenu::changeSetting(size_t idx, bool right) {
  auto &setting  = Settings::settings[idx];
  setting->value = rotateVal(setting->value, right, setting->display.count);

  Settings::saveConfig();
}

ModMenu &ModMenu::instance() {
  static ModMenu me;
  return me;
}

ModMenu::~ModMenu() = default;
ModMenu::ModMenu()
: tool(CENTER_X_RATIO, CENTER_Y_RATIO) {
  Settings::readConfig();
}

void ModMenu::update(PressedKeys data) {
  if (data.toggle_framebar) changeSetting(Settings::FRAMEBAR_INDEX);
  if (data.toggle_hitbox) changeSetting(Settings::HITBOX_INDEX);
  if (data.toggle_menu) is_showing = !is_showing;

  if (!is_showing) return;

  // get button inputs and update cursor
  //  auto checker = RedInputChecker(asw_engine::get()->inputs[0]);
  //  if (checker.checkInput(RedInputChecker::IF_Up)) cursor_position = rotateVal(cursor_position, false, OPTION_COUNT);
  //  if (checker.checkInput(RedInputChecker::IF_Down)) cursor_position = rotateVal(cursor_position, true, OPTION_COUNT);
  //  if (checker.checkInput(RedInputChecker::IF_Left)) changeSetting(cursor_position, false);
  //  if (checker.checkInput(RedInputChecker::IF_Right)) changeSetting(cursor_position, true);
  if (data.go_up) cursor_position = rotateVal(cursor_position, false, OPTION_COUNT);
  if (data.go_down) cursor_position = rotateVal(cursor_position, true, OPTION_COUNT);
  if (data.rotate_left) changeSetting(cursor_position, false);
  if (data.rotate_right) changeSetting(cursor_position, true);
}

void ModMenu::draw() {
  if (!is_showing) return;

  tool.update();

  tool.drawRect(MENU_LEFT, MENU_TOP, MENU_WIDTH, MENU_HEIGHT, background_color);

  // draw cursor
  double cursor_top = MENU_TOP + MENU_PADDING + cursor_position * (OPTION_HEIGHT + OPTION_SPACING);
  tool.drawRect(OPTION_LEFT, cursor_top, MENU_WIDTH - (2 * MENU_PADDING), OPTION_HEIGHT, cursor_color);

  for (size_t idx = 0; idx < OPTION_COUNT; idx++) {
    auto &relevant = Settings::settings[idx]->display;
    double top     = MENU_TOP + MENU_PADDING + idx * (OPTION_HEIGHT + OPTION_SPACING);
    tool.drawOutlinedText(OPTION_LEFT, top, FString(relevant.title), 2.0);
    tool.drawOutlinedText(VALUE_LEFT, top, FString(relevant.values[Settings::settings[idx]->value]), 2.0);
  }
}

bool ModMenu::barEnabled() const { return Settings::FRAMEBAR.value; }
bool ModMenu::hitboxEnabled() const { return Settings::HITBOXES.value; }
bool ModMenu::fadeEnabled() const { return Settings::FADE.value; }
bool ModMenu::delimEnabled() const { return Settings::DELIM.value; }
bool ModMenu::cancelEnabled() const { return Settings::SHOW_CANCEL.value; }
bool ModMenu::dashEnabled() const { return Settings::SHOW_DASH_FRAMES.value; }
bool ModMenu::crossupEnabled() const { return Settings::CROSSUP_ENABABLED.value; }
int ModMenu::pauseType() const { return Settings::PAUSE_TYPE.value; }

const int delayAmounts[4] = {0, 20, 30, 60};
int ModMenu::delayAmount() const { return delayAmounts[Settings::DELAY_AMOUNT.value]; }

CurrentOptions ModMenu::getScheme() const {
  return CurrentOptions{color_palettes[Settings::COLOR_SCHEME.value], fadeEnabled(), delimEnabled(), cancelEnabled(), crossupEnabled()};
}