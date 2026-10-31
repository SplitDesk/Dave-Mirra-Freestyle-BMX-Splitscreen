#pragma once
// Addresses in NgBMX.exe (US retail, see game_version.hpp) that the mod uses.
// docs/ADDRESS.md lists them with further addresses the mod does not use.
//
// Entries marked [inferred] are deduced from how the game uses them; all
// others are read from the decompiled code.
//
// The exe is built with incremental linking: most calls go through 5-byte jmp
// thunks at 0x401000..0x4026xx. Hooks go on the function bodies listed here.

#include <cstdint>

namespace bmx::game::addr {

// The exe's code.
inline constexpr uintptr_t kCodeBegin         = 0x00401000;
inline constexpr uintptr_t kCodeEnd           = 0x00540000;

// ----------------------------------------------------------------- riders --
// The riders are a static array indexed by player slot. The rider update,
// input copy and camera code loop over kRiderCount entries.
inline constexpr uintptr_t kRiderArray        = 0x00691F40;  // RiderState[2]
inline constexpr uint32_t  kRiderStride       = 0x0F70;      // sizeof(RiderState)
inline constexpr int       kRiderSlotsInExe   = 2;           // rider slots the exe allocates
inline constexpr uintptr_t kRiderCount        = 0x0069621C;  // uint8 riders in play
inline constexpr uintptr_t kCurrentTurnPlayer = 0x0056B615;  // uint8 the rider whose turn it is
// Rider model definitions (NgIFroller.c): {+4 character, outfit, bike, ...}
// per rider created, 2 entries.
inline constexpr uintptr_t kPlayerDefs        = 0x005FB6F0;
inline constexpr uintptr_t kPlayerDefCount    = 0x005FB738;  // int entries in use
inline constexpr uint32_t  kPlayerDefSize     = 0x10;
// uint8 game state: 0 riding, 1 paused or in-game screen, 2/3 replay,
// 4 attract demo, 255 front end.
inline constexpr uintptr_t kGameState         = 0x00551A30;

// Offsets into RiderState.
namespace rider {
inline constexpr uint32_t kButtonsHeld  = 0x00C;  // controller +0x10, copied each frame
inline constexpr uint32_t kFlags        = 0x038;  // bit 0: the rider is active (updated each frame)
inline constexpr uint32_t kTrickName    = 0x04E;  // uint8 trick shown by HUD element 8
inline constexpr uint32_t kTrickName2   = 0x04F;  // uint8 trick shown by HUD element 7 in sub-state 6
inline constexpr uint32_t kRunScore     = 0x05C;  // int32 points banked this run
// int32 session total: SessionStart (0x40A8C0) zeroes it and the run end
// (0x40B2D4) adds kRunScore of the turn rider.
inline constexpr uint32_t kSessionScore = 0x060;
inline constexpr uint32_t kComboValue   = 0x064;  // int32 current combo, added to kRunScore when banked
inline constexpr uint32_t kState        = 0x3DC;  // uint8 index into the state handler tables
inline constexpr uint32_t kMatrix       = 0x3E0;  // orientation matrix; position float[3] at +0x420
// actor* of the bike and of the body; actor +4 bit 0 is visibility
// (0x475A30 shows, 0x475A60 hides).
inline constexpr uint32_t kBikeActor    = 0xB94;
inline constexpr uint32_t kRiderActor   = 0xD90;
}  // namespace rider

// ------------------------------------------------------- in-game screens --
// The in-game menus, opened by ScreenOpen(id) from the table of entries
// {create(show), tick() -> acted, draw(), u8 item count, u8 selected item}.
// Ids by title: 1 PAUSE, 2 OPTIONS, 3 AUDIO OPTIONS, 4-6 TRICK LIST, 7 END
// RUN? / EXIT?, 8/9 next rider's turn, 0xA RUN FINISHED, 0xB SAVE, 0xC level
// intro, 0xD-0xF CHALLENGES, 0x10 RESULTS, 0x11 new record name, 0x12-0x15
// RECORDS. The tick runner (0x415140) runs the tick once the items show and
// moves the selection itself when there are 2 or more items.
inline constexpr uintptr_t kGameScreen        = 0x0056BEBA;  // uint8 id of the screen showing (0 none)
inline constexpr uintptr_t kGameScreenPtr     = 0x0056BF20;  // its table entry
inline constexpr uintptr_t kGameScreenTable   = 0x005571B8;  // entry*[0x1A] by id (0x1A reopens the last one)
inline constexpr uintptr_t kScreenItems       = 0x0056C028;  // {u16 action, u16 string}[] of the screen showing
inline constexpr int       kScreenItemsMax    = 10;
inline constexpr uintptr_t kScreenFrames      = 0x0056BE14;  // uint32 frames since the screen opened
inline constexpr uintptr_t kScreenItemsDelay  = 0x0056BFA4;  // uint32 frames before the items show and the tick runs
// u16[24] the screen's text handles, all freed when a screen opens. On a
// screen without a draw function, kMenuTexts[selected] is the selected item's
// text, which the HUD tick colours (0x414F82).
inline constexpr uintptr_t kMenuTexts         = 0x0056BEBC;
inline constexpr uintptr_t kScreenTitleText   = 0x0056BEEA;  // u16 title text handle (the last kMenuTexts slot)
inline constexpr int       kRunFinishedScreen = 0x0A;
inline constexpr uintptr_t kSessionMode       = 0x0056B701;  // uint8 set by the mode menu
inline constexpr uintptr_t kSessionSubMode    = 0x0056B702;  // uint8 game type: 0 ProQuest, 1 Session, 2 Freeride
inline constexpr uintptr_t kLevel             = 0x0056B614;  // uint8 park 0..11

// ---------------------------------------------------------------- physics --
// bikePhysics* per slot, filled modulo 2 (CreateBikePhysics 0x452E80).
inline constexpr uintptr_t kBikePhysicsTable  = 0x00574BC4;  // void*[2]
inline constexpr uintptr_t kBikePhysicsCount  = 0x00574CA4;  // int next slot (0..1)

// ------------------------------------------------------------------ sound --
// Looping 3-D sound sources per rider slot (bmxsound.c), updated each frame
// by GameEvent(0xFA0, pos, slot << 28 | flags, tire, pedal).
inline constexpr uintptr_t kSfxVolume         = 0x0055B538;  // uint8 master effects volume
inline constexpr uintptr_t kGameSfxBank       = 0x0055B540;  // uint16 in-game effects bank (0xFFFF none)
inline constexpr uintptr_t kPedalLoopHandles  = 0x0057466C;  // uint16[2] pedal and chain loop per slot
inline constexpr uintptr_t kTireLoopHandles   = 0x00574670;  // uint16[2] tyre and grind loop per slot
inline constexpr uintptr_t kSndSrcPool        = 0x0064FCD0;  // the 24 3-D sound sources (zsndsrc.c)
inline constexpr uint32_t  kSndSrcStride      = 0x28;
inline constexpr uintptr_t kSndSrcMaxDistSq   = 0x00650094;  // uint32 hearing distance squared (2500^2)

// -------------------------------------------------------- players, input --
inline constexpr uintptr_t kPlayerConfigArray  = 0x006952A0;  // PlayerConfig[2]
inline constexpr uint32_t  kPlayerConfigStride = 0x4C;
inline constexpr uint32_t  kPlayerCfgFlags     = 0x00;        // uint8 flags
inline constexpr uint8_t   kPlayerCfgHints     = 0x02;        // flag: tutorial hints on (OPTIONS, TutorialTips)
inline constexpr uint32_t  kPlayerCfgCharacter = 0x03;        // uint8 character, which also picks the career
inline constexpr uint32_t  kPlayerCfgCamera    = 0x06;        // uint8 chase camera type

// Controller records: +0 device*, +4 mask of buttons ignored until released,
// +8 held at pause, +0xC flags, +0x10 held, +0x14 changed, +0x18 analog
// x/y/x2/y2 (float), +0x28 previous analog.
inline constexpr uintptr_t kControllerArray   = 0x0056B628;  // Controller[2]
inline constexpr uint32_t  kControllerStride  = 0x3C;
inline constexpr int       kControllersInExe  = 2;
inline constexpr uintptr_t kControllersPaused = 0x0056B620;  // uint8 set by ControllersSetPaused
// uint8[256] DirectInput keyboard state, read by each device poll (0x493910).
inline constexpr uintptr_t kKeyboardState     = 0x00674054;
// Player 1's keys: {DIK, input index} pairs ending with 0xFF (player 2's at
// 0x561E88).
inline constexpr uintptr_t kKeyboardLayoutP1  = 0x00561E68;
// The front end's controller record: +0 device*, +4 held, +8 changed.
inline constexpr uintptr_t kMenuController    = 0x0056D298;

// -------------------------------------------------------------------- HUD --
inline constexpr uintptr_t kOverlayPendingA   = 0x005D24FC;  // int: OverlayDrawA draws on the next camera
inline constexpr uintptr_t kOverlayPendingB   = 0x005D2504;  // int: OverlayDrawB draws on the next camera
inline constexpr uintptr_t kOverlayPrimCursor = 0x0068EBE0;  // next free per-frame primitive (0xFFFFFFFF none)
inline constexpr uintptr_t kHudElements       = 0x00556F00;  // 17 x {flags, show, update, enable, set, get}
inline constexpr uintptr_t kHudFont           = 0x0056BF38;  // the HUD's font, also the score's

// The HUD's state, 0x56BD48..0x56C080, which a HUD instance of the mod's
// copies. The two fonts inside (0xC bytes each) are shared.
namespace hud {
inline constexpr uintptr_t kStateBegin  = 0x0056BD48;
inline constexpr uintptr_t kStateEnd    = 0x0056C080;
inline constexpr uintptr_t kFontA       = 0x0056BE98;
inline constexpr uintptr_t kFontB       = kHudFont;
inline constexpr uint32_t  kFontSize    = 0x0C;
inline constexpr uintptr_t kMeterBase   = 0x0056BD48;  // float, the height meter's zero
inline constexpr uintptr_t kScoreShown  = 0x0056BD50;  // int, the score as it counts up
inline constexpr uintptr_t kScoreText   = 0x0056BE2C;  // u16 handle of the score text
inline constexpr uintptr_t kResetByte   = 0x0056BE2A;  // uint8 the HUD setup sets to 0xFF
inline constexpr uintptr_t kHandleA     = 0x0056BE90;  // u16 object handle
inline constexpr uintptr_t kHandleB     = 0x0056BE94;  // u16 object handle
inline constexpr uintptr_t kFadeBox     = 0x0056BEF8;  // u16 handle of the full-screen fade box
inline constexpr uintptr_t kMeterMarker = 0x0056BF2E;  // u16 handle of the height meter's marker
inline constexpr uintptr_t kTimerValue  = 0x0056BF58;  // the timer element's value, set by the session
inline constexpr uintptr_t kMeterHeight = 0x0056BFC0;  // float, 0 while the meter is hidden
inline constexpr uintptr_t kMeterValue  = 0x0056BFC4;  // float, the height shown
inline constexpr uintptr_t kHandleC     = 0x0056BFA8;  // u16 object handle
inline constexpr uintptr_t kHandleD     = 0x0056BFFC;  // u16 object handle
inline constexpr uintptr_t kTimerText   = 0x0056C000;  // u16 handle of the run timer text
inline constexpr uintptr_t kFadeFrames  = 0x0056C054;  // u16 frames left of the screen fade (element 16), 0 none
}  // namespace hud
inline constexpr uintptr_t kMeterScale        = 0x0054D0DC;  // float, the height meter's scale
// The overlay pools of boxes (pool 0, 0: 2 banks of 16) and of text glyphs
// (pool 0, 1: 2 banks of 128); entries are 0xA0 bytes.
inline constexpr uintptr_t kBoxPool           = 0x005A0A38;
inline constexpr uintptr_t kGlyphPool         = 0x00596A38;

// ------------------------------------------------------- career and goals --
// One career block per character (trick book, goals per level); a player's
// career is the block of his PlayerConfig character.
inline constexpr uintptr_t kCareerArray       = 0x006955E8;
inline constexpr uint32_t  kCareerStride      = 0x104;
// ProQuest goal progress: one 0x27-byte block per level 0..5 in each career
// (core/goal_states.hpp), at kGoalBlocks + character * kCareerStride +
// level * 0x27.
inline constexpr uintptr_t kGoalBlocks        = 0x006955EF;
inline constexpr int       kGoalLevels        = 6;
// 2 x 64 object* (zero-terminated) that glow for the shown tip's goal;
// PositionGoals refills them when a tip shows (0x40F5C2).
inline constexpr uintptr_t kGoalObjectLists   = 0x0056BA28;
inline constexpr size_t    kGoalObjectListsSize = 0x200;
inline constexpr uintptr_t kGoalGlowPulse     = 0x0056BC2C;  // uint32 frame counter of the goal glow

// -------------------------------------------------------------- front end --
inline constexpr uintptr_t kGuiElementCount   = 0x0057323C;  // int elements on the current screen
inline constexpr uintptr_t kGuiElements       = 0x005730A4;  // element*[]: +2 u16 flags (bit 0 hidden), +4 id
inline constexpr uint32_t  kGuiElementSize    = 0xF0;        // +0 type, +2 flags, +4 id, +0x34 image, +0xD8 text, +0xDC its size
inline constexpr uintptr_t kZPrimSlots        = 0x00678418;  // void*[4] loaded asset groups
// GUI asset handles (128 of 16 bytes), linked at +4: the free ones, and the
// loaded assets and groups (+0 id, +2 flags, +8 memory, +0xC offset in it).
inline constexpr uintptr_t kGuiAssetFree      = 0x00678408;
inline constexpr uintptr_t kGuiAssetList      = 0x0067840C;

// The main menu: a 7-slot text wheel over 6 items.
inline constexpr uintptr_t kMainMenuSelected  = 0x005745A0;  // uint16 item in the centre slot, 0..5
inline constexpr int       kMainMenuItems     = 6;           // 0 ProQuest, 1 Session, 2 Freeride, ...
inline constexpr uint32_t  kMainMenuSlotMid   = 0xA005000D;  // centre slot; the slots are 0xA005000A..10
inline constexpr uintptr_t kMainMenuStrings   = 0x0055A35C;  // uint16[6] string ids of the items
inline constexpr uintptr_t kScreenStep        = 0x0057458C;  // fn* the screen runs each frame (0 idle)
inline constexpr uintptr_t kScreenPending     = 0x005745A4;  // fn* main menu action waiting to run
inline constexpr uintptr_t kTitleTextSet      = 0x005745AE;  // uint16 set once the title's tick set its text

// The controller options screen (page 0xA00C): a row per action (0..10),
// then the keyboard layout, OK and CANCEL rows (11..13). Each action row's
// JOYPAD text is element 0xA00C0037 + row.
inline constexpr uintptr_t kControlsPlayer    = 0x00691E08;  // int the player whose controls it edits
inline constexpr uintptr_t kControlsRow       = 0x005745E0;  // u8 the selected row
inline constexpr uintptr_t kControlsMasks     = 0x005745B4;  // u32[11] the screen's copy of the rows' masks
// u8 the joypad button capture: 0 off, 1 waiting until the device's buttons
// are all released, 2 waiting for a button.
inline constexpr uintptr_t kControlsCapture   = 0x005745E1;

// The rider, bike and level select (screen 0xA009), kept from the
// PlayStation 2-player mode: with kRiderCount = 2 each step is done once per
// player, player 2 driven by GUI events 0x2B..0x37.
inline constexpr uintptr_t kSelectStep        = 0x00691CB0;  // int -1 back, 0 rider, 1 bike, 2 style, 3 level, 4 go
inline constexpr uintptr_t kSelectRider       = 0x00691CA0;  // int[2], to PlayerConfig +3
inline constexpr uintptr_t kSelectOutfit      = 0x00691D58;  // int[2] style, to PlayerConfig +4
inline constexpr uintptr_t kSelectBike        = 0x00691D98;  // int[2], to PlayerConfig +5
inline constexpr uintptr_t kSelectTimer       = 0x00691704;  // uint frame counter
inline constexpr uintptr_t kSelectWaitUntil   = 0x0069182C;  // uint the steps wait for kSelectTimer to pass it
// uint8 step the select opens on next: 0 rider, 3 level (set by the in-game
// menus, e.g. LEVEL SELECT).
inline constexpr uintptr_t kSelectReturnStep  = 0x0056B617;
inline constexpr int       kSelectPortraitGroup = 3;         // GUI asset group of the rider step (bank 0xE004)
// The select's own state, which a second copy of the screen needs of its own
// (docs/ADDRESS.md, "Rider select").
inline constexpr uintptr_t kSelectState       = 0x00691700;  // its globals: timer, steps, picks, cards, images
inline constexpr size_t    kSelectStateSize   = 0x6D4;
// +0 the yellow frames' glow (0..0x40), +4..+0xC fn*: change handlers and the
// next step (kScreenStep).
inline constexpr uintptr_t kSelectCallbacks   = 0x00574584;
inline constexpr uintptr_t kGlowStep          = 0x0055A354;  // int +1/-1: direction of the frames' glow
inline constexpr uintptr_t kSelectImages      = 0x005745EC;  // the images of the outfit and bike steps
inline constexpr size_t    kSelectImagesSize  = 0x44;
inline constexpr uintptr_t kSelectCardImages[] = {0x00691D00, 0x00691CF8, 0x00691CFC};  // images of the first step

// ---------------------------------------------------------- GUI primitives --
// The GUI's primitives of a frame come from one buffer, rewound by
// OverlayBeginB, and each takes an ordering-table entry, counting down from
// the top. Both hold 1024 primitives and nothing checks: past that,
// primitives overwrite the variables after the buffer (0x5D24FC..) and the
// entries before the ordering table (0x68EBE0).
inline constexpr uintptr_t kGuiPrimCursor     = 0x005D2510;  // next GUI primitive; x 0..512 across, y 0..480
inline constexpr uintptr_t kGuiPrimBase       = 0x005D250C;  // the buffer (0 until GuiPrimSetup sets the static one, 0x5AB4FC)
inline constexpr uint32_t  kGuiPrimBufferSize = 0x27000;
// uint32* the ordering table (static, 0x68EC00) the HUD and GUI primitives
// are linked into, drawn top down.
inline constexpr uintptr_t kOrderingTable     = 0x0055C698;
inline constexpr int       kOrderingTableEntries = 0x400;
inline constexpr uintptr_t kGuiOtCursor       = 0x0068C3D0;  // uint32* entry of the next GUI primitive
inline constexpr uintptr_t kScreenSize        = 0x0055C690;  // float width, height in pixels (GUI clip areas)

// ------------------------------------------------------- cameras (camera.c) --
// One camera controller per rider; the active one drives the game camera.
inline constexpr uintptr_t kCameraCtlArray    = 0x0056B448;  // CameraCtl[2]
inline constexpr uint32_t  kCameraCtlStride   = 0xB8;
inline constexpr uintptr_t kActiveCameraCtl   = 0x0056B408;  // uint8 index of the active one
inline constexpr uintptr_t kGameCameraObject  = 0x0068FF98;  // the zobjCamera the game renders with

// ---------------------------------------------- engine (ZGE, RenderWare 3) --
inline constexpr uintptr_t kScreenWidth       = 0x00697770;  // int
inline constexpr uintptr_t kScreenHeight      = 0x00697774;  // int
inline constexpr uintptr_t kCameraPool        = 0x0067401C;  // the pool of every zrwCamera
inline constexpr uintptr_t kButtonTextureSlot = 0x0056BFFA;  // uint16 slot of the button font texture (id 0x33)
// Texture slots (512 of 20 bytes); the material pointer of slot n is at
// kTextureTableMaterial + n * kTextureTableStride.
inline constexpr uintptr_t kTextureTableMaterial = 0x0068C3F0;
inline constexpr uint32_t  kTextureTableStride   = 20;
// zrwCam interface entry for SetFov (0x492A60), which the widescreen fix
// replaces.
inline constexpr uintptr_t kCamApiSetFov      = 0x00561D98;

}  // namespace bmx::game::addr

namespace bmx::game::fn {

// ------------------------------------------------------------- level start --
// Creates kRiderCount riders and loads the assets. It reads kRiderCount again
// at 0x403F88 to pick the flow: 1 single player, 2+ the unused multi-rider
// flow (screen 0x16).
inline constexpr uintptr_t GameStart            = 0x00403D10;
// (lang) loads the HUD strings, a different table for more than one rider;
// called only by GameStart.
inline constexpr uintptr_t LoadHudStrings       = 0x00414DD0;
inline constexpr uintptr_t ReplaysDestroy       = 0x00409CD0;  // level end: destroys the riders' replays

// ----------------------------------------------------------------- cameras --
inline constexpr uintptr_t CameraInitAll        = 0x004056C0;  // inits each rider's CameraCtl, registers a draw callback
inline constexpr uintptr_t CameraCtlInit        = 0x00407620;  // (player, type) inits the controller with that chase camera type
inline constexpr uintptr_t CameraCtlSetMode     = 0x00407760;  // (player, mode); GameStart uses (0, 4)
inline constexpr uintptr_t CameraUpdateAll      = 0x00405780;  // per frame: full update of the active camera only

// ------------------------------------------------------------------- sound --
// 3-D sources take their volume (distance, range 2500) and pan from their
// position in the listener's space, the game camera's: looping sources at
// 0x47E200 every tick, one-shot sounds at 0x47EA10.
inline constexpr uintptr_t SoundListenerSpace   = 0x0045A340;  // (world pos*, out*) into kGameCameraObject's space
inline constexpr uintptr_t SfxBankSample        = 0x004704A0;  // (bank, sample index) -> sample*
inline constexpr uintptr_t SndSrcCreate         = 0x0047E520;  // (sample*, pos*, flags) -> u16 handle (0xFFFF failed)
inline constexpr uintptr_t SndSrcDestroy        = 0x0047E650;  // (handle)
inline constexpr uintptr_t SndSrcSetVolume      = 0x0047E910;  // (handle, volume)
inline constexpr uintptr_t ZobjCameraMatrix     = 0x00482310;  // (zobjCamera, 0) -> its cached view matrix
inline constexpr uintptr_t TransformPoint       = 0x00481640;  // (out*, matrix*, in*)
// (event, a, b, c, d): announcer, sounds and HUD pop-ups; event 2000 is the
// menu move sound, 0x7D4 the refused sound.
inline constexpr uintptr_t GameEvent            = 0x0044E300;

// ------------------------------------------------------------- controllers --
// Creates Controller[0..1] with device type 2, else 0. The End addresses
// mark the function ends for return-address checks.
inline constexpr uintptr_t InitControllers      = 0x00408030;
inline constexpr uintptr_t InitControllersEnd   = 0x004080A0;
inline constexpr uintptr_t MenuControllerInit   = 0x00437200;  // creates the front end's controller (type 2)
inline constexpr uintptr_t MenuControllerInitEnd = 0x00437268;
inline constexpr uintptr_t MenuControllerUpdate = 0x004372B0;  // per front-end frame: device -> kMenuController
// (type) 0 DirectInput joystick, 1 keyboard, 2 both; type 2 always takes the
// first joystick.
inline constexpr uintptr_t CreateController     = 0x00480370;
// Per frame: device -> Controller[i] (+0x10 held, +0x14 changed), remapping
// 7 bits through the PlayerConfig masks.
inline constexpr uintptr_t ControllerUpdate     = 0x004080F0;
// (paused) called by SetGameState (0x404F40): on resume, buttons pressed
// during the pause are ignored until released.
inline constexpr uintptr_t ControllersSetPaused = 0x004083E0;
inline constexpr uintptr_t ControllerCheck      = 0x00408590;  // [inferred] (controller, mask, mode) -> pressed

// --------------------------------------------------------- runs and scoring --
// Per frame: trick tracking for the turn rider, the run timer and the run end.
inline constexpr uintptr_t RunUpdate            = 0x00409D20;
inline constexpr uintptr_t TrickTracker         = 0x0040D010;  // (slot) combo tracking; banks with BankCombo(turn player)
inline constexpr uintptr_t BankCombo            = 0x0040DAE0;  // (player, landed) combo -> run score, totals, trick book
inline constexpr uintptr_t RecordCheck          = 0x0040E030;  // (player, category, value, ...) level records and goals
inline constexpr uintptr_t TutorialTips         = 0x0040E380;  // (player) tutorial hints (HUD 10) when kPlayerCfgHints is set
inline constexpr uintptr_t GetSessionMode       = 0x0040A9F0;  // returns kSessionMode zero-extended in EAX
inline constexpr uintptr_t TurnSystem           = 0x0040AA50;  // (event) run start and end, the turns, hides waiting riders
// Return addresses of TurnSystem's two GetSessionMode calls that test for
// mode 11, in which every rider rides at once (unused on PC): at the turn end
// (!= 11 passes the turn on) and the turn start (== 11 places and shows the
// other riders instead of hiding them).
inline constexpr uintptr_t TurnEndModeCheck     = 0x0040AA6D;
inline constexpr uintptr_t TurnStartModeCheck   = 0x0040ADA7;
// TurnSystem(5), the run end. With 2 riders it takes the PlayStation
// turn-based path and opens screen 8 (then 9, the next rider's turn) instead
// of saving (TurnSystem(6)) and opening RUN FINISHED.
inline constexpr int       kTurnRunEnd          = 5;

// ------------------------------------------------------------------- goals --
inline constexpr uintptr_t PositionGoals        = 0x0040EC40;  // (player) ProQuest goals tied to places and gaps
inline constexpr uintptr_t PositionGoalsEnd     = 0x0040F855;
inline constexpr uintptr_t GoalCheckLanding     = 0x0040C300;  // (player, flag) goal checks on landings and grinds
inline constexpr uintptr_t GoalCheckObject      = 0x0040CA60;  // (player, object) prop hit: type 0xC goals
inline constexpr uintptr_t GoalCheckProp        = 0x0040CBE0;  // (player, object) prop event: type 0x15 goals
inline constexpr uintptr_t GoalBlockFor         = 0x00407FC0;  // (player, level) -> the goal block of the player's character
// (player) at the run start (0x40C190, after the props reset): resets the
// player's goal items and opens the props of goals done in his career.
inline constexpr uintptr_t GoalsRunStart        = 0x0040CD40;
// Once per frame (draw callback list 0x5FB6C8): the turn player's goal items
// in state 2 make the goal's objects glow.
inline constexpr uintptr_t GoalHighlights       = 0x0040A400;
// (player, goal, item, state) writes the career block; state 5 also unlocks
// the rewards.
inline constexpr uintptr_t GoalSet              = 0x00410350;
// () writes save\SAVE.DAT (careers, PlayerConfig, records); returns 0 on
// success.
inline constexpr uintptr_t SaveCareer           = 0x00412340;

// --------------------------------------------------------------------- HUD --
inline constexpr uintptr_t HudLevelInit         = 0x004147E0;  // creates the HUD elements, registers the HUD tick (0x414F60)
inline constexpr uintptr_t HudLevelShutdown     = 0x004148B0;  // destroys them
inline constexpr uintptr_t HudTexturesInit      = 0x00414970;  // () allocates and loads the HUD textures
inline constexpr uintptr_t HudEnable            = 0x00415300;  // (element, on) shows or hides an element
inline constexpr uintptr_t HudSet               = 0x00415370;  // (id, data*) sets an element of the one shared HUD
inline constexpr int       kHudMessage          = 0x0C;        // HudSet id of the message line
inline constexpr uintptr_t MatCreateFromImage   = 0x004896D0;  // (data*, size, name, type) -> material*
inline constexpr uintptr_t RiderSubState        = 0x00431DD0;  // (rider*, level) -> state at that depth of the state stack

// The HUD (bmx_zov.c), a port of the PlayStation HUD: text and sprites are
// PlayStation GPU primitives in double-buffered pools, linked into an
// ordering table that is drawn in screen pixels (x * width / 640,
// y * height / 480).
inline constexpr uintptr_t OverlayPool          = 0x00457F70;  // (type, size, bank, &base, &count, &kind)
inline constexpr uintptr_t OverlayAlloc         = 0x004580E0;  // (type, size) -> handle of a free pool entry
inline constexpr uintptr_t OverlayDestroy       = 0x004581A0;  // (handle)
// (handle, on) shows or hides an object; hiding clears the length of each of
// its primitives' tags, so the ordering table skips them.
inline constexpr uintptr_t OverlayShow          = 0x00458400;
// Functions that change an object by handle. They do not check for handle
// 0xFFFF and write through a wrong pool address when given it.
inline constexpr uintptr_t OverlayMove          = 0x00458B00;
inline constexpr uintptr_t OverlaySetA          = 0x00458CC0;
inline constexpr uintptr_t OverlaySetB          = 0x00459940;
inline constexpr uintptr_t OverlaySetC          = 0x00459BE0;
inline constexpr uintptr_t OverlayTextCreate    = 0x004594C0;  // (0, font*, text, x, y, rgb, 0) -> u16 handle
inline constexpr uintptr_t OverlayImmediateText = 0x00459DF0;  // (font*, text, x, y, rgb, flags) text for one frame
inline constexpr uintptr_t OverlayImmediateBox  = 0x00458EA0;  // (x, y, w, h, rgb) filled box for one frame
inline constexpr uintptr_t OverlayTextWidth     = 0x00459F30;  // (text, font*) -> width in 640x480 units
inline constexpr uintptr_t OverlayInit          = 0x004578F0;  // links each pool's entries (28-bit links), marks them free
// A new HUD frame: clears the ordering table, rewinds kOverlayPrimCursor and
// sets its pending flag; B does the same for the GUI.
inline constexpr uintptr_t OverlayBeginA        = 0x00463820;
inline constexpr uintptr_t OverlayBeginB        = 0x00463AB0;
inline constexpr uintptr_t GuiPrimSetup         = 0x004639F0;  // () sets kGuiPrimBase to the static buffer if 0
inline constexpr uintptr_t OverlayDrawPrim      = 0x00462050;  // (prim) one primitive -> 2-D quads and lines
// Per-camera draw callbacks: each draws the ordering table if its pending
// flag is set, then clears it, so the HUD shows in the first camera drawn.
inline constexpr uintptr_t OverlayDrawA         = 0x00463860;
inline constexpr uintptr_t OverlayDrawB         = 0x004639A0;
// The ordering table size in code: the entry counts (push 0x400) and the top
// entry's offset (0xFFC) in OverlayBeginA/B and OverlayDrawA/B.
inline constexpr uintptr_t kOtCountA            = 0x00463826;  // imm32 of push 0x400 (OverlayBeginA)
inline constexpr uintptr_t kOtTopDrawA          = 0x00463874;  // imm32 of add eax, 0xFFC (OverlayDrawA)
inline constexpr uintptr_t kOtCountB            = 0x00463AB6;  // imm32 of push 0x400 (OverlayBeginB)
inline constexpr uintptr_t kOtTopB              = 0x00463AC7;  // disp32 of lea ecx, [eax + 0xFFC]: the GUI's first entry
inline constexpr uintptr_t kOtTopDrawB          = 0x004639B4;  // imm32 of add eax, 0xFFC (OverlayDrawB)
// 2-D draws: (flags, items, n, m), flags & 0x2000000 meaning screen space; a
// vertex is x, y, z, ?, u, v, rgba.
inline constexpr uintptr_t Zgl2DLines           = 0x0048CCF0;  // items {tex, v0, v1}
inline constexpr uintptr_t Zgl2DTris            = 0x0048D120;  // items {tex, v0, v1, v2}
inline constexpr uintptr_t Zgl2DQuads           = 0x0048D660;  // items {tex, v0..v3}

// --------------------------------------------------------- in-game screens --
inline constexpr uintptr_t ScreenOpen           = 0x00415400;  // (id) closes the screen showing and opens `id` (0 none)
inline constexpr uintptr_t SelectedItemColour   = 0x0041E9C0;  // () -> pulsing colour of the selected item
// RUN FINISHED (screen 0xA, entry 0x557108). Score lines from y 0x6C, 0x1D
// apart: label at x 0x35, value right-aligned to 0x24A. Items centred on
// x 0x13F from y 0x1A4 - count * 0x1D. Its tick acts on confirm or Start from
// any rider, by item action 0..8 (6 trick list).
inline constexpr uintptr_t RunFinishedCreate    = 0x0041A480;  // (show) title and items
inline constexpr uintptr_t RunFinishedTick      = 0x0041AD00;  // () -> acted
inline constexpr uintptr_t RunFinishedDraw      = 0x0041A830;  // () one score line per rider, then the items
// OPTIONS (screen 2, entry 0x5570A8, no draw function). Rows from
// y 0xF0 - count * 0x21 / 2, 0x21 apart: "label:" at x 0x56, values
// right-aligned to 0x229. Its tick reads the turn player's pad: left/right
// change item 0 (camera type) or 1 (tutorial hints), confirm on item 2 opens
// AUDIO OPTIONS, other confirms and Start return to PAUSE.
inline constexpr uintptr_t OptionsCreate        = 0x00415B70;  // (show) title, labels, then the values (0x416060)
inline constexpr uintptr_t OptionsTick          = 0x00415CF0;  // () -> acted

// ------------------------------------------------------------------ riders --
inline constexpr uintptr_t RiderRestart         = 0x0040C190;  // (slot, show) reset, start position, RidersUpdate
inline constexpr uintptr_t RidersUpdate         = 0x0041FC40;  // updates kRiderCount riders
inline constexpr uintptr_t kRidersUpdateFromMain = 0x0040421B; // return address of the main update's call while riding
inline constexpr uintptr_t CreateRider          = 0x004316C0;  // (slot, character, outfit, bike) queues the models, sets up RiderState
inline constexpr uintptr_t RiderReset           = 0x00431AA0;  // (slot, show) physics reset, actors shown or hidden, active bit
inline constexpr uintptr_t RiderRunSetup        = 0x0040CF50;  // [inferred] (slot) per rider in the run-start reset
inline constexpr uintptr_t RidersReadInput      = 0x00430E70;  // controller -> RiderState, per rider
inline constexpr uintptr_t RidersApplyInput     = 0x00431000;  // RiderState input -> rider controls, per active rider

// ------------------------------------------------- shadow (zrwShadow.c) --
// One shadow descriptor, overwritten by each rider created and drawn by one
// render callback (list 5): +0 model count (2), +4 models (rider, bike), +8
// bike model (centres the shadow), +0xC light direction; 0x20 bytes.
inline constexpr uintptr_t kShadowDesc          = 0x00678440;
// (desc*) list-5 callback: builds the shared shadow grid around the
// descriptor's rider and sets 0x6751FC.
inline constexpr uintptr_t ShadowDraw           = 0x0049B520;
inline constexpr uintptr_t ShadowGridDraw       = 0x0049B3D0;  // () list-2 callback: draws the grid if 0x6751FC, clears it
inline constexpr uintptr_t kShadowDrawFlags     = 0x0049B4B2;  // low byte of push 0x4103 (zgl flags; bit 0 no depth test)
// (flags, raster) zgl flags -> RenderWare render states, called through
// 0x561490: bit 0 clear turns depth test and depth write on.
inline constexpr uintptr_t ZglSetRenderFlags    = 0x0048BC60;
inline constexpr uintptr_t kZglCurrentFlags     = 0x00697778;  // uint32 flags ZglSetRenderFlags last applied
inline constexpr uintptr_t RwRenderStateSet     = 0x004DB820;  // (state, value) in this RenderWare's numbering
inline constexpr int       kRwStateZWrite       = 6;           // D3DRENDERSTATE_ZWRITEENABLE

// ------------------------------------------------------------ engine (ZGE) --
// Per frame: poses the active camera rider's bike and body only.
inline constexpr uintptr_t PoseActiveRider      = 0x004659E0;
inline constexpr uintptr_t PoseActor            = 0x004654B0;  // (actor) skeleton from the animation, then model transform
inline constexpr uintptr_t ZobjCameraCreate     = 0x004821C0;  // () -> zobjCamera*, its zrwCamera at +0x2C
inline constexpr uintptr_t ZobjCameraUpdate     = 0x004820C0;  // (zobj) pushes the object's transform into its zrwCamera
inline constexpr uintptr_t ZobjDestroy          = 0x00487400;  // (zobj) virtual destroy and free
inline constexpr uintptr_t RenderFrame          = 0x0048B690;  // renders every enabled camera, then flips
inline constexpr uintptr_t ZrwCameraCreate      = 0x004925F0;  // RwCamera with full-screen sub-rasters
inline constexpr uintptr_t ZrwCameraBeginUpdate = 0x00492F00;  // (zrwCamera) before each camera's draw
// Re-subrasters a camera to a rectangle; assumes 4:3 and is not patched by
// the widescreen fix.
inline constexpr uintptr_t ZrwCameraSetViewport = 0x00492CC0;
inline constexpr uintptr_t ZrwCameraDestroy     = 0x00492970;  // also the camera pool's destructor
inline constexpr uintptr_t ZrwCameraSetMatrix   = 0x00492AC0;  // (cam, float[16] matrix, float[3] pos)
inline constexpr uintptr_t FovScale             = 0x00480F30;  // field-of-view units -> scale, returned in ST0
inline constexpr uintptr_t RwCameraSetNearClipPlane = 0x004DE5E0;  // writes +0x80, rebuilds the frustum
inline constexpr uintptr_t RwCameraSetFarClipPlane  = 0x004DE620;  // writes +0x84, rebuilds the frustum
inline constexpr uintptr_t RwCameraSetFogDistance   = 0x004DE660;  // writes +0x88
inline constexpr uintptr_t RwCameraSetViewWindow    = 0x004DE9B0;  // (cam, RwV2d*) writes +0x68

// --------------------------------------------------------------- front end --
// Screen event handlers are int(event, a, b, c); see core/menu_buttons.hpp
// for the events.
inline constexpr uintptr_t GuiFindElement       = 0x004383A0;  // (id) -> element* of the current screen, or 0
inline constexpr uintptr_t GuiSetText           = 0x00438DB0;  // (element*, text) copies the text
inline constexpr uintptr_t GuiSendEvent         = 0x004395E0;  // (event, a, b) to the elements and the screen handler
inline constexpr uintptr_t SendElement          = 0x00439460;  // (event, element, a, b) to one element (0xC draws it)
inline constexpr uintptr_t GuiSetClip           = 0x00477DA0;  // (x0, y0, x1, y1); (0, 0, 639, 479) is none
inline constexpr uintptr_t GuiAssetAlloc        = 0x0047B830;  // () -> a free asset handle linked into the list, or 0
inline constexpr uintptr_t GuiFreeImage         = 0x00479CD0;  // (image) frees an image the screen made
inline constexpr uintptr_t ZPrimUnloadGroup     = 0x0047B5C0;  // (slot) frees the group and each of its assets
inline constexpr uintptr_t MemAlloc             = 0x0046B030;  // (size) -> handle
inline constexpr uintptr_t MemFree              = 0x0046B140;  // (handle)
inline constexpr uintptr_t MemLock              = 0x0046B470;  // (handle) -> pointer
inline constexpr uintptr_t MemUnlock            = 0x0046B4F0;  // (handle)
inline constexpr uintptr_t GetString            = 0x004824F0;  // (u16 id) -> localised text
// Main menu events; confirm acts on kMainMenuSelected: 0..2 game types,
// 3 0x44CA00, 4 0x4433E0, 5 quit.
inline constexpr uintptr_t MainMenuHandler      = 0x0043FB80;
inline constexpr uintptr_t MainMenuLabels       = 0x004404E0;  // lays out and labels the 7 wheel slots
inline constexpr uintptr_t MainMenuEnter        = 0x0043FA20;  // (fromTitle) handler and wheel; 0 also loads SAVE.DAT
// () installs the main menu's handler and wheel labels; MainMenuEnter calls
// it, and so does the screen after the attract demo (0x43F0C0).
inline constexpr uintptr_t MainMenuInstall      = 0x0043FB10;
// () starts the attract demo (rider count 0), run as the pending action of
// the title screen or the main menu after 0x708 idle ticks. While it plays
// (kGameState 4), a fresh press on Controller[0] or [1] ends it (0x40477B).
inline constexpr uintptr_t AttractDemoStart     = 0x00440D30;
// The title screen ("PRESS START BUTTON") shares the main menu's page with
// its own handler: Start runs MainMenuEnter(1) 2 ticks later, 0x708 idle
// ticks start the attract demo.
inline constexpr uintptr_t TitleEnter           = 0x0043F9B0;  // () installs TitleHandler on page 0xA005
inline constexpr uintptr_t TitleEnterThunk      = 0x00401FAA;  // jmp TitleEnter, as stored in kScreenPending
inline constexpr uintptr_t TitleHandler         = 0x0043F7A0;  // (event, a, b, c)
inline constexpr uintptr_t kScreenPendingTicks  = 0x00691EB8;  // int ticks since kScreenPending was set (runs after 2)
inline constexpr uintptr_t SelectEnter          = 0x00447540;  // main menu -> rider select
inline constexpr uintptr_t SelectHandler        = 0x00447BF0;  // rider select events; tests kRiderCount == 2
inline constexpr uintptr_t SelectLeave          = 0x00447600;  // tears the select down (back or start)
inline constexpr uintptr_t SelectFirstStep      = 0x00402491;  // thunk to 0x449A80, the step that sets the screen up
// (dir) next or previous chooser; the step changes when it wraps. Sets
// "PLAYER n" (string 0x3B) in element 0xA0090002.
inline constexpr uintptr_t SelectMove           = 0x004479D0;
// The controller options screen. ControlsShow makes its rows from the
// player's PlayerConfig masks and installs ControlsHandler (event, a, b, c).
// Confirm on an action row starts the capture (sound 0x7D1); during it the
// handler stores the first button held on InputDevice(player) in the screen's
// copy of the masks, sets the row's text and plays 0x7D2. Confirm on OK
// copies the masks into PlayerConfig; back or CANCEL leave without it.
inline constexpr uintptr_t ControlsShow         = 0x00441CB0;
inline constexpr uintptr_t ControlsHandler      = 0x004420A0;
inline constexpr uintptr_t InputDevice          = 0x00480400;  // (n) -> the n-th input device made; +8 its raw buttons
inline constexpr uintptr_t FrontEndExit         = 0x0044DD10;  // (what) leaves the front end: 0 starts the game, 1 quits

}  // namespace bmx::game::fn
