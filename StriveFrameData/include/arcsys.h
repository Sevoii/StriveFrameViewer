#pragma once

#include <Unreal/AActor.hpp>
#include "struct_util.h"
#include "bbscript.h"

void ASWInitFunctions();

// I have no fucking idea what UE4SS is doing with their FVector, but it breaks stuff
struct SimpleFVector {
  float x, y, z;
};

class AGameState : public RC::Unreal::AActor {};

class UWorld : public RC::Unreal::UObject {
public:
    FIELD(0x130, AGameState*, GameState);
};

class AREDGameState_Battle : public AGameState {
public:
  FIELD(0xBA8, class asw_engine*, Engine);
	FIELD(0xBB0, class asw_scene*, Scene);
  FIELD(0xBC8, class asw_events*, Events);
};

class player_block {
	char pad[0x160];
public:
	FIELD(0x8, class asw_player*, entity);
};

static_assert(sizeof(player_block) == 0x160);

enum BOM_EVENT
{
	BOM_EVENT_ENTRY = 0,
	BOM_EVENT_ONLYDRAMA_BATTLE_SETUP = 1,
	BOM_EVENT_ARCADE_EVENT_SETUP = 2,
	BOM_EVENT_ARCADE_EVENT_FINISH = 3,
	BOM_EVENT_ENTRY_EVENT_SETUP = 4,
	BOM_EVENT_ENTRY_EVENT_FINISH = 5,
	BOM_EVENT_ENTRY_BG = 6,
	BOM_EVENT_ENTRY_CHARA = 7,
	BOM_EVENT_RESULT_CHARA = 8,
	BOM_EVENT_RESET = 9,
	BOM_EVENT_SET_KEY_FLAG = 10,
	BOM_EVENT_BATTLE_START = 11,
	BOM_EVENT_RESULT_SCREEN_SETUP_FOR_STORY = 12,
	BOM_EVENT_WIN_ACTION = 13,
	BOM_EVENT_MATCH_WIN_ACTION = 14,
	BOM_EVENT_MATCH_RESULT_WAIT = 15,
	BOM_EVENT_DECISION = 16,
	BOM_EVENT_BATTLE_START_CAMERA = 17,
	BOM_EVENT_REQUEST_FINISH_STOP = 18,
	BOM_EVENT_REQUEST_FINISH_CAMERA = 19,
	BOM_EVENT_ENTRY_SCREEN_CONTROL = 20,
	BOM_EVENT_WIN_SCREEN_CONTROL = 21,
	BOM_EVENT_RESET_REMATCH = 22,
	BOM_EVENT_MAIN_MEMBER_CHANGE = 23,
	BOM_EVENT_RESET_ON_MEMBER_CHANGE = 24,
	BOM_EVENT_MATCH_START_EFFECT = 25,
	BOM_EVENT_FINISH_SLOW = 26,
	BOM_EVENT_DESTRUCTION_FINISH_START = 27,
	BOM_EVENT_DRAMATIC_FINISH_START = 28,
	BOM_EVENT_SHENLONG_SYSTEM = 29,
	BOM_EVENT_RESULT_VOICE_COMMON = 30,
	BOM_EVENT_RESULT_VOICE_SPECIAL = 31,
	BOM_EVENT_STOP_RESULT_VOICE = 32,
	BOM_EVENT_DRAMATIC_FINISH_UI_AND_STOP = 33,
	BOM_EVENT_HUD_TUTORIAL_START = 34,
	BOM_EVENT_ENTRY_START = 35,
	BOM_EVENT_ADV_DISP_BATTLE = 36,
	BOM_EVENT_RANNYU_SAVER = 37,
	BOM_EVENT_BBS_EVENT_SETUP = 38,
	BOM_EVENT_BBS_EVENT_FINISH = 39,
	BOM_EVENT_ROUND_RESET_FOR_BG = 40,
	BOM_EVENT_INVALID = 41,
	BOM_EVENT_MAX = 42,
};

class asw_events {
public:
  struct event_info {
    BOM_EVENT type;
    long p0;
    long p1;
    long p2;
  };

  static asw_events *get();

  ARRAY_FIELD(0x8, event_info[10], events);
  FIELD(0xA8, unsigned long, event_count);
};

class asw_inputs {
public:
  unsigned short m_PreRecFlg; // 0x0
	unsigned short m_CurRecFlg; // 0x2
	unsigned short m_RecFlgList[30]; // 0x4
	unsigned short m_RecFlgTime[30]; // 0x40
	unsigned short m_CurIndex; // 0x7C
};

// Used by the shared GG/BB/DBFZ engine code
class asw_engine {
public:
	static constexpr auto COORD_SCALE = .458f;

	static asw_engine *get();

	ARRAY_FIELD(0x0, player_block[2], players);
	FIELD(0x8A0, int, entity_count);
	ARRAY_FIELD(0xC10, class asw_entity* [107], entities);
	ARRAY_FIELD(0x1498, RC::Unreal::AActor* [7], pawns);
  ARRAY_FIELD(0x37A8, asw_inputs[6], inputs);
};

class asw_scene {
public:
	static asw_scene *get();

	// "delta" is the difference between input and output position
	// position gets written in place
	// position/angle can be null
	void camera_transform(SimpleFVector *delta, SimpleFVector *position, SimpleFVector *angle) const;
	void camera_transform(SimpleFVector *position, SimpleFVector *angle) const;
};

class hitbox {
public:
	enum class box_type : int {
		hurt = 0,
		hit = 1,
		grab = 2 // Not used by the game
	};

	box_type type;
	float x, y;
	float w, h;
};

enum class direction : int {
	right = 0,
	left = 1
};

class event_handler {
	char pad[0x58];

public:
	FIELD(0x0, char*, script);
    FIELD(0x8, char*, action_name);
	FIELD(0x28, int, trigger_value);
	FIELD(0x2C, int, trigger_value_2);
    FIELD(0x30, char*, label);
    FIELD(0x50, unsigned int, int_flag);
};

static_assert(sizeof(event_handler) == 0x58);

class atk_param {
    char pad[0x3F8];

public:
    FIELD(0x0, int, atk_type);
    FIELD(0x4, int, atk_level);
    FIELD(0x8, int, atk_level_clash);
    FIELD(0xC, int, damage);
    FIELD(0x24, int*, hitstop_enemy_addition);
    FIELD(0x30, int, hitstop);
    FIELD(0x34, int, grab_wait_time);
    FIELD(0x38, int, guard_time);
    FIELD(0x12C, int, pushback);
    FIELD(0x130, int, fd_min_pushback);
    FIELD(0x134, int, guard_break_pushback);
    FIELD(0x138, int, hit_pushback);
    FIELD(0x13C, int, wall_pushback);
    FIELD(0x14C, int, stagger_time);
    FIELD(0x254, int, atk_level_guard);
    FIELD(0x258, int, risc);
    FIELD(0x25C, int, proration);
    FIELD(0x260, int, proration_rate_first);
    FIELD(0x264, int, proration_rate_once);
    FIELD(0x268, int, proration_first);
    FIELD(0x26C, int, proration_once);
    FIELD(0x270, int, chip_damage);
    FIELD(0x274, int, chip_damage_rate);
    FIELD(0x278, int, unburst_time);
    FIELD(0x304, int, guard_crush_time);
};

static_assert(sizeof(atk_param) == 0x3F8);

class atk_param_ex {
    char pad[0xB8];

public:
    FIELD(0x0, int, air_pushback_x);
    FIELD(0x4, int, air_pushback_y);
    FIELD(0x8, int, atk_gravity);
    FIELD(0x14, int, atk_hitstun);
    FIELD(0x18, int, atk_untech);
    FIELD(0x24, int, atk_knockdown_time);
    FIELD(0x30, int, atk_wallstick_time);
    FIELD(0x38, int, atk_roll_time);
    FIELD(0x3C, int, atk_slide_time);
    FIELD(0x48, int, atk_soft_knockdown);
};

static_assert(sizeof(atk_param_ex) == 0xB8);

class ActionReqInfo {
  char pad[0x50];
public:
  FIELD(0x44, int, skill_id);
};

enum SKILL_ACTIVE_FLAG
{
	SAF_CHAIN = 1,
	SAF_FLEX_CHAIN = 16,
	SAF_PD_CHAIN = 32,
	SAF_CONDITION_READY = 64,
	SAF_REQUESTED = 128,
	SAF_LOW_ESTIMATION = 256,
	SAF_ARENDA_CHAIN = 512,
	SAF_ARENDA_FLEX_CHAIN = 1024,
	SAF_ARENDA_CHAIN_PD = 2048,
	SAF_BRENDA_CHAIN = 4096,
	SAF_BRENDA_FLEX_CHAIN = 8192,
	SAF_BRENDA_CHAIN_PD = 16384,
	SAF_CRENDA_CHAIN = 32768,
	SAF_CRENDA_FLEX_CHAIN = 65536,
	SAF_CRENDA_CHAIN_PD = 131072,
	SAF_REQUEST_RESERVE = -2147483648,
};

class MoveData {
  char pad[0x220]; // ghidra says 0x200, experimentation show 0x220
public:
  ARRAY_FIELD(0x0, char[20], move_name); // m_SkillName
  FIELD(0x1B8, SKILL_ACTIVE_FLAG, active_flag); // m_SkillActiveFlag

  const char* get_name() const { return &move_name[0]; }
};

// old moves size: 0x200 * 180 = 0x16800
// new moves size: 0x220 * 180 = 0x17e80
// move size diff: 0x17e80 - 0x16800 = 0x1680
// old MDC size: 0x1aafc
// new MDC size: 0x1aafc + 0x1680 = 0x1C17C
// old m_SkillInfoCount offset: 0x18A1C
// new m_SkillInfoCount offset: 0x18A1C + 0x1680 = 0x1A09C
class MoveDataCollection {
  char pad[0x1C17C];
public:
  ARRAY_FIELD(0x0, MoveData[180], moves); // m_SkillInfo
  //ARRAY_FIELD(0x16804, int[180], indicies); // m_SkillRegisterIndexList
  //FIELD(0x16AD4, long, index_count); // m_SkillRegisterNum
  FIELD(0x1A09C, long, move_count); // m_SkillInfoCount,  Ghidra: 0x18A1C
  //0x18A28 Hash of Skill Names
};

class asw_entity {

public:
    FIELD(0x18, bool, is_player);
	FIELD(0x44, unsigned char, player_index);
	FIELD(0x78, hitbox*, hitboxes);
	FIELD(0x10C, int, hurtbox_count);
	FIELD(0x110, int, hitbox_count);
	//   _____    ____    _    _   _   _   _______   ______   _____
	//  / ____|  / __ \  | |  | | | \ | | |__   __| |  ____| |  __ \
	// | |      | |  | | | |  | | |  \| |    | |    | |__    | |__) |
	// | |      | |  | | | |  | | | . ` |    | |    |  __|   |  _  /
	// | |____  | |__| | | |__| | | |\  |    | |    | |____  | | \ \
	//  \_____|  \____/   \____/  |_| \_|    |_|    |______| |_|  \_\
	BIT_FIELD(0x1A8, 0x4000000, cinematic_counter);
	FIELD(0x1C4, int, action_time);
	FIELD(0x1E0, int, act_reg_0);
    FIELD(0x280, int, hitstop);
    FIELD(0x2AC, int, ground_height);
    FIELD(0x2B0, asw_entity*, parent_ply);
    FIELD(0x2B8, asw_entity*, parent_obj);
	FIELD(0x2C0, asw_player*, opponent);
	FIELD(0x318, asw_entity*, attached);
    BIT_FIELD(0x390, 1, airborne);
	BIT_FIELD(0x390, 256, counterhit);
	BIT_FIELD(0x394, 16, strike_invuln);
	BIT_FIELD(0x394, 32, throw_invuln);
	BIT_FIELD(0x394, 64, wakeup);
  BIT_FIELD(0x398, 32, landed_hit);
	FIELD(0x3A4, direction, facing);
	FIELD(0x3A8, int, pos_x);
	FIELD(0x3AC, int, pos_y);
	FIELD(0x3B0, int, pos_z);
	FIELD(0x3B4, int, angle_x);
	FIELD(0x3B8, int, angle_y);
	FIELD(0x3BC, int, angle_z);
	FIELD(0x3C4, int, scale_x);
	FIELD(0x3C8, int, scale_y);
	FIELD(0x3CC, int, scale_z);
	FIELD(0x4C8, int, vel_x);
    FIELD(0x4CC, int, vel_y);
    FIELD(0x4D0, int, gravity);
    FIELD(0x4FC, int, pushbox_front_offset);
    FIELD(0x6F8, atk_param, atk_param_hit);
    FIELD(0x73C, int, throw_box_top);
    FIELD(0x744, int, throw_box_bottom);
    FIELD(0x748, int, throw_range);
    FIELD(0xAFC, atk_param_ex, atk_param_ex_normal);
    FIELD(0xBA0, atk_param_ex, atk_param_ex_counter);
    FIELD(0xC78, atk_param, atk_param_defend);
    FIELD(0x1070, atk_param_ex, atk_param_ex_defend);
    FIELD(0x1168, int, backdash_invuln);
    // bbscript
    FIELD(0x11E0, bbscript::event_bitmask, event_handler_bitmask);
    FIELD(0x1220, char*, bbs_file);
    FIELD(0x1228, char*, script_base);
    FIELD(0x1230, char*, next_script_cmd);
    FIELD(0x1238, char*, first_script_cmd);
    ARRAY_FIELD(0x1240, char[32], sprite_name);
    FIELD(0x1260, int, sprite_duration);
    FIELD(0x1268, int, sprite_total_duration);
    FIELD(0x134C, int, sprite_changes);
    ARRAY_FIELD(0x1358, event_handler[(size_t)bbscript::event_type::MAX], event_handlers);
    ARRAY_FIELD(0x3740, char[20], state_name); // m_CurActionName (old: 0x3628, 0x118 offset)

    // m_ActionRequestInfo, Ghidra: 0x3674, 0x118 offset?)
    // Ghidra + 0x118 = 0x378C (from state_name)
    FIELD(0x378C, ActionReqInfo, action_info);

    // m_ActionRequestInfo, Ghidra: 0x36c4, 0x118 offset?)
    // Ghidra + 0x118 = 0x37DC (from state_name)
    FIELD(0x37DC, ActionReqInfo, action_info_reg);

    bool is_active() const;
    bool is_pushbox_active() const;
    bool is_strike_invuln() const;
    bool is_throw_invuln() const;
    int get_pos_x() const;
    int get_pos_y() const;
    int pushbox_width() const;
    int pushbox_height() const;
    int pushbox_bottom() const;
    void get_pushbox(int* left, int* top, int* right, int* bottom) const;

    const char* get_sprite_name() const { return &sprite_name[0]; }
    const char* get_BB_state() const;
};

enum PLAYER_ENABLE_FLAG : uint32_t
{
    ENABLE_STANDING = 0x1,
    ENABLE_CROUCHING = 0x2,
    ENABLE_FORWARDWALK = 0x4,
    ENABLE_FORWARDDASH = 0x8,
    ENABLE_FORWARDCROUCHWALK = 0x10,
    ENABLE_BACKWALK = 0x20,
    ENABLE_BACKDASH = 0x40,
    ENABLE_BACKCROUCHWALK = 0x80,
    ENABLE_JUMP = 0x100,
    ENABLE_BARRIER_CANCEL = 0x200,
    ENABLE_AIRJUMP = 0x400,
    ENABLE_AIRFORWARDDASH = 0x800,
    ENABLE_NORMALATTACK = 0x1000,
    ENABLE_SPECIALATTACK = 0x2000,
    ENABLE_STANDTURN = 0x4000,
    ENABLE_DEAD = 0x8000,
    ENABLE_GUARD = 0x10000,
    ENABLE_AIRBACKDASH = 0x40000,
    ENABLE_CROUCHTURN = 0x80000,
    ENABLE_AIRTURN = 0x200000,
    ENABLE_ROMANCANCEL = 0x800000,
    ENABLE_NAMA_FAULT = 0x1000000,
    ENABLE_BARRIER = 0x2000000,
    ENABLE_LOCKREJECT = 0x4000000,
    ENABLE_AUTOLOCKREJECT = 0x8000000,
    ENABLE_DEMO = 0x10000000,
    ENABLE_PRE_GUARD = 0x20000000,
    ENABLE_AUTO_GUARD = 0x40000000,
    ENABLE_BURST = 0x80000000,
};

enum ID_CMNACT : uint32_t
{
    ID_CmnActStand = 0x0,
    ID_CmnActStandTurn = 0x1,
    ID_CmnActStand2Crouch = 0x2,
    ID_CmnActCrouch = 0x3,
    ID_CmnActCrouchTurn = 0x4,
    ID_CmnActCrouch2Stand = 0x5,
    ID_CmnActJumpPre = 0x6,
    ID_CmnActJump = 0x7,
    ID_CmnActJumpLanding = 0x8,
    ID_CmnActLandingStiff = 0x9,
    ID_CmnActFWalk = 0xA,
    ID_CmnActBWalk = 0xB,
    ID_CmnActFDash = 0xC,
    ID_CmnActFDashStop = 0xD,
    ID_CmnActBDash = 0xE,
    ID_CmnActBDashStop = 0xF,
    ID_CmnActAirFDash = 0x10,
    ID_CmnActAirBDash = 0x11,
    ID_CmnActNokezoriHighLv1 = 0x12,
    ID_CmnActNokezoriHighLv2 = 0x13,
    ID_CmnActNokezoriHighLv3 = 0x14,
    ID_CmnActNokezoriHighLv4 = 0x15,
    ID_CmnActNokezoriHighLv5 = 0x16,
    ID_CmnActNokezoriLowLv1 = 0x17,
    ID_CmnActNokezoriLowLv2 = 0x18,
    ID_CmnActNokezoriLowLv3 = 0x19,
    ID_CmnActNokezoriLowLv4 = 0x1A,
    ID_CmnActNokezoriLowLv5 = 0x1B,
    ID_CmnActNokezoriCrouchLv1 = 0x1C,
    ID_CmnActNokezoriCrouchLv2 = 0x1D,
    ID_CmnActNokezoriCrouchLv3 = 0x1E,
    ID_CmnActNokezoriCrouchLv4 = 0x1F,
    ID_CmnActNokezoriCrouchLv5 = 0x20,
    ID_CmnActBDownUpper = 0x21,
    ID_CmnActBDownUpperEnd = 0x22,
    ID_CmnActBDownDown = 0x23,
    ID_CmnActBDownBound = 0x24,
    ID_CmnActBDownLoop = 0x25,
    ID_CmnActBDown2Stand = 0x26,
    ID_CmnActFDownUpper = 0x27,
    ID_CmnActFDownUpperEnd = 0x28,
    ID_CmnActFDownDown = 0x29,
    ID_CmnActFDownBound = 0x2A,
    ID_CmnActFDownLoop = 0x2B,
    ID_CmnActFDown2Stand = 0x2C,
    ID_CmnActVDownUpper = 0x2D,
    ID_CmnActVDownUpperEnd = 0x2E,
    ID_CmnActVDownDown = 0x2F,
    ID_CmnActVDownBound = 0x30,
    ID_CmnActVDownLoop = 0x31,
    ID_CmnActBlowoff = 0x32,
    ID_CmnActBlowoffUpper90 = 0x33,
    ID_CmnActBlowoffUpper60 = 0x34,
    ID_CmnActBlowoffUpper30 = 0x35,
    ID_CmnActBlowoffDown30 = 0x36,
    ID_CmnActBlowoffDown60 = 0x37,
    ID_CmnActBlowoffDown90 = 0x38,
    ID_CmnActKirimomiUpper = 0x39,
    ID_CmnActWallBound = 0x3A,
    ID_CmnActWallBoundDown = 0x3B,
    ID_CmnActWallHaritsuki = 0x3C,
    ID_CmnActWallHaritsukiLand = 0x3D,
    ID_CmnActWallHaritsukiGetUp = 0x3E,
    ID_CmnActJitabataLoop = 0x3F,
    ID_CmnActKizetsu = 0x40,
    ID_CmnActHizakuzure = 0x41,
    ID_CmnActKorogari = 0x42,
    ID_CmnActZSpin = 0x43,
    ID_CmnActFuttobiFinish = 0x44,
    ID_CmnActFuttobiBGTrans = 0x45,
    ID_CmnActUkemi = 0x46,
    ID_CmnActLandUkemi = 0x47,
    ID_CmnActVUkemi = 0x48,
    ID_CmnActFUkemi = 0x49,
    ID_CmnActBUkemi = 0x4A,
    ID_CmnActKirimomiLand = 0x4B,
    ID_CmnActKirimomiLandEnd = 0x4C,
    ID_CmnActSlideDown = 0x4D,
    ID_CmnActRushFinishDown = 0x4E,
    ID_CmnActKirimomiVert = 0x4F,
    ID_CmnActKirimomiVertEnd = 0x50,
    ID_CmnActKirimomiSide = 0x51,
    ID_CmnActMidGuardPre = 0x52,
    ID_CmnActMidGuardLoop = 0x53,
    ID_CmnActMidGuardEnd = 0x54,
    ID_CmnActHighGuardPre = 0x55,
    ID_CmnActHighGuardLoop = 0x56,
    ID_CmnActHighGuardEnd = 0x57,
    ID_CmnActCrouchGuardPre = 0x58,
    ID_CmnActCrouchGuardLoop = 0x59,
    ID_CmnActCrouchGuardEnd = 0x5A,
    ID_CmnActAirGuardPre = 0x5B,
    ID_CmnActAirGuardLoop = 0x5C,
    ID_CmnActAirGuardEnd = 0x5D,
    ID_CmnActHajikareStand = 0x5E,
    ID_CmnActHajikareCrouch = 0x5F,
    ID_CmnActHajikareAir = 0x60,
    ID_CmnActAirTurn = 0x61,
    ID_CmnActLockWait = 0x62,
    ID_CmnActLockReject = 0x63,
    ID_CmnActAirLockWait = 0x64,
    ID_CmnActAirLockReject = 0x65,
    ID_CmnActItemUse = 0x66,
    ID_CmnActBurst = 0x67,
    ID_CmnActRomanCancel = 0x68,
    ID_CmnActEntry = 0x69,
    ID_CmnActRoundWin = 0x6A,
    ID_CmnActMatchWin = 0x6B,
    ID_CmnActLose = 0x6C,
    ID_CmnActResultWin = 0x6D,
    ID_CmnActResultLose = 0x6E,
    ID_CmnActEntryWait = 0x6F,
    ID_CmnActSubEntry = 0x70,
    ID_CmnActSpecialFinishWait = 0x71,
    ID_CmnActExDamage = 0x72,
    ID_CmnActExDamageLand = 0x73,
    ID_CmnActHide = 0x74,
    ID_CmnActChangeEnter = 0x75,
    ID_CmnActChangeEnterCutscene = 0x76,
    ID_CmnActChangeEnterCutsceneRecv = 0x77,
    ID_CmnActChangeEnterAttack = 0x78,
    ID_CmnActChangeEnterStiff = 0x79,
    ID_CmnActChangeLeave = 0x7A,
    ID_CmnActEnterAfterDestruction = 0x7B,
    ID_CmnActEnterAfterBGTransLeftIn = 0x7C,
    ID_CmnActEnterAfterBGTransRightIn = 0x7D,
    ID_CmnActRushStart = 0x7E,
    ID_CmnActRushRush = 0x7F,
    ID_CmnActRushFinish = 0x80,
    ID_CmnActRushFinishChaseLand = 0x81,
    ID_CmnActRushFinishChaseAir = 0x82,
    ID_CmnActRushFinishChaseEnd = 0x83,
    ID_CmnActRushFinishChange = 0x84,
    ID_CmnActRushSousai = 0x85,
    ID_CmnActRushSousaiPrimary = 0x86,
    ID_CmnActHomingDash = 0x87,
    ID_CmnActHomingDashCurve = 0x88,
    ID_CmnActHomingDashBrake = 0x89,
    ID_CmnActMikiwameMove = 0x8A,
    ID_CmnActShotHajiki = 0x8B,
    ID_CmnActGuardCancelAttack = 0x8C,
    ID_CmnActLimitBurst = 0x8D,
    ID_CmnActSparkingBurst = 0x8E,
    ID_CmnActRushRejectWait = 0x8F,
    ID_CmnActRushFinishDamage = 0x90,
    ID_CmnActQuickDown = 0x91,
    ID_CmnActQuickDown2Stand = 0x92,
    ID_CmnActNokezoriBottomLv1 = 0x93,
    ID_CmnActNokezoriBottomLv2 = 0x94,
    ID_CmnActNokezoriBottomLv3 = 0x95,
    ID_CmnActNokezoriBottomLv4 = 0x96,
    ID_CmnActNokezoriBottomLv5 = 0x97,
    ID_CmnActFloatDamage = 0x98,
    ID_CmnActEntryToStand1P = 0x99,
    ID_CmnActEntryToStand2P = 0x9A,
    ID_CmnActGuardLanding = 0x9B,
    ID_CmnAct_NUM = 0x9C,
    ID_CmnAct_NULL = 0xFFFFFFFF,
};

enum PLATTACK_FLAG
{
	PLATK_CHAINCANCEL = 1,
	PLATK_FLEXCANCEL = 2,
	PLATK_SPECIALCANCEL = 4,
	PLATK_JUMPCANCEL = 8,
	PLATK_SPECIALCANCEL_PD = 16,
	PLATK_JUMPCANCEL_PD = 32,
	PLATK_DASHCANCEL = 64,
	PLATK_DASHCANCEL_PD = 128,
	PLATK_FLEXCANCEL_NEXT = 256,
	PLATK_ULTIMATECANCEL = 512,
	PLATK_ULTIMATECANCEL_PD = 1024,
	PLATK_TENSION_PENALTY = 2048,
	PLATK_AIRDASHCANCEL = 4096,
	PLATK_AIRBDASHCANCEL = 8192,
	PLATK_FLG_21 = 2097152,
	PLATK_ULTIMATECHANGE_CANCEL = 4194304,
	PLATK_ULTIMATECHANGE_CANCEL_PD = 8388608,
	PLATK_MIKIWAMEMOVE_CANCEL = 16777216,
	PLATK_MIKIWAMEMOVE_CANCEL_PD = 33554432,
	PLATK_A_RENDA_CANCEL_DISABLE = 67108864,
	PLATK_B_RENDA_CANCEL_DISABLE = 134217728,
	PLATK_C_RENDA_CANCEL_DISABLE = 268435456,
	PLATK_GUARDCANCEL = 536870912,
	PLATK_ULTIMATESKILL = 1073741824,
	PLATK_ULTIMATECHANGESKILL = -2147483648,
};

class asw_player : public asw_entity {

public:
    FIELD(0x6140, int, enable_flag); // original: 0x6080 -> fixed: 0x60E0 (+0x060)
    FIELD(0x614c, int, attack_flag); // original: 0x5F90 -> fixed: 0x60EC (+0x060)
    FIELD(0x6160, int, blockstun); // original: 0x60A0 + 0x060 = 0x6100
    FIELD(0x9928, int, hitstun); // original: 0x9868 + 0x060 = 0x98C8
    FIELD(0x999c, int, ply_PushColHeightLowAir);
    FIELD(0xc32c, ID_CMNACT, cur_cmn_action_id); // original: 0xC26C + 0x060 = 0xC2CC
    FIELD(0xd05c, int, slowdown_timer); // original: 0xCF9C + 0x060 = 0xCFFC
    FIELD(0xfac0, MoveDataCollection, move_datas);
    FIELD(0xf290, int, afro); // m_IsAfro Header: 0xed28, Offset: 0x508
    FIELD(0xf2c8, int, afroW);
    FIELD(0xf2cc, int, afroH);

    int calc_advantage();
    bool is_in_hitstun() const;
    bool is_in_blockstun() const;
    bool can_act() const;
    bool is_down_bound() const;
    bool is_quick_down_1() const;
    bool is_quick_down_2() const;
    bool is_down_loop() const;
    bool is_down_2_stand() const;
    bool is_knockdown() const;
    bool is_roll() const;
    bool is_stagger() const;
    bool is_guard_crush() const;
    bool is_stunned() const;

    bool is_stance_idle() const;

    MoveData* get_current_move() const;

    bool can_whiff_cancel() const { return attack_flag &  PLATK_FLEXCANCEL; }
    bool can_gatling_cancel() const { return (attack_flag & PLATK_CHAINCANCEL) && landed_hit;}
};