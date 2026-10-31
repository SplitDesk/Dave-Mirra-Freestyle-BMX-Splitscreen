# NgBMX.exe addresses

Addresses in `NgBMX.exe`, US retail build (1,597,496 bytes, SHA-256
`97aa9077501f264a1f96fc2ff1ff8d4326be094dd98f2a647c913de9d60d09a2`). The
**Name** column is the constant in `src/game/addresses.hpp`; names in
*italics* are not used by the mod. Entries marked *(inferred)* are deduced
from how the game uses them; all others are read from the decompiled code.

The exe is built with incremental linking: most calls go through 5-byte `jmp`
thunks at 0x401000..0x4026xx. The functions below are the bodies, which is
where hooks go.

Contents: [General](#general) · [Riders](#riders) ·
[Input](#input) · [Cameras](#cameras) · [Engine and rendering](#engine-and-rendering) ·
[Shadow](#shadow) · [Sound](#sound) · [HUD](#hud) ·
[GUI primitives](#gui-primitives) · [Runs and scoring](#runs-and-scoring) ·
[Career and goals](#career-and-goals) · [In-game screens](#in-game-screens) ·
[Front end](#front-end) · [Title screen and attract demo](#title-screen-and-attract-demo) ·
[Rider select](#rider-select) · [Controller options](#controller-options)

## General

| Address | Name | Description |
|---|---|---|
| 0x401000..0x540000 | `kCodeBegin`, `kCodeEnd` | The exe's code. |
| 0x4040E0 | *GameFrameUpdate* | Per-frame game logic. Holding Start + Select for 60 frames quits to the menus (`0x551FD4` = 1, then `0x45A290`). |
| 0x404F40 | *SetGameState* | (state) sets `kGameState`; pauses or resumes the controllers. |
| 0x4056A0 | *GameState* | Returns `kGameState`. |
| 0x45A290 | *SetMenuMode* | () returns to the menus (zsystemSetMenuMode). |
| 0x551A30 | `kGameState` | uint8: 0 riding, 1 paused or in-game screen, 2/3 replay, 4 attract demo, 255 front end. |
| 0x551FD4 | *QuitToMenus* | *(inferred)* int set to 1 when Start + Select quits a level; `MainMenuEnter` clears it. |
| 0x56B614 | `kLevel` | uint8 park 0..11. |
| 0x56B701 | `kSessionMode` | uint8 set by the mode menu. |
| 0x56B702 | `kSessionSubMode` | uint8 game type: 0 ProQuest, 1 Session, 2 Freeride. |
| 0x412190 | *LoadCareer* | Loads `save\SAVE.DAT`. |
| 0x412340 | `SaveCareer` | () writes `save\SAVE.DAT` (careers, PlayerConfig, records); returns 0 on success. |

## Riders

The riders are a static array indexed by player slot; the rider update, input
copy and camera code loop over `kRiderCount` entries. The exe allocates 2
slots (`kRiderSlotsInExe`).

| Address | Name | Description |
|---|---|---|
| 0x403D10 | `GameStart` | Level start: creates `kRiderCount` riders and loads the assets. Reads `kRiderCount` again at 0x403F88 to pick the flow: 1 single player, 2+ the unused multi-rider flow (screen 0x16). |
| 0x40421B | `kRidersUpdateFromMain` | Return address of the main update's `RidersUpdate` call while riding. |
| 0x407BA0 | *InitPlayers* | Sets `kRiderCount` to 1. |
| 0x409CD0 | `ReplaysDestroy` | Level end: destroys the riders' replays (one per rider, plus a shared one when the count is above 1). |
| 0x40C190 | `RiderRestart` | (slot, show) reset, start position, `RidersUpdate`. |
| 0x40CF50 | `RiderRunSetup` | *(inferred)* (slot) per rider in the run-start reset. |
| 0x41FC40 | `RidersUpdate` | Updates `kRiderCount` riders. |
| 0x420C90 | *RiderGetState* | Returns RiderState +0x3DC. |
| 0x430410 | *ReplayControl* | Saves or restores a whole RiderState. |
| 0x430E70 | `RidersReadInput` | Controller → RiderState, per rider. |
| 0x431000 | `RidersApplyInput` | RiderState input → rider controls, per active rider. |
| 0x4316C0 | `CreateRider` | (slot, character, outfit, bike) queues the models and sets up the RiderState. |
| 0x431AA0 | `RiderReset` | (slot, show) physics reset, actors shown or hidden, active bit. |
| 0x431DD0 | `RiderSubState` | (rider*, level) → the state at that depth of the state stack. |
| 0x452E80 | *CreateBikePhysics* | Stores each rider's bikePhysics* in `kBikePhysicsTable`, modulo 2. |
| 0x4654B0 | `PoseActor` | (actor) skeleton from the animation, then the model transform. |
| 0x4659E0 | `PoseActiveRider` | Per frame: poses the bike and body of the active camera's rider only. |
| 0x475A30 / 0x475A60 | *ActorShow*, *ActorHide* | Set or clear an actor's visibility bit (actor +4 bit 0). |
| 0x5578D0, 0x5578EC, 0x557908, 0x557924 | *RiderStateTables* | The pre, main, post and final rider state handlers, indexed by RiderState +0x3DC. |
| 0x574BC4 | `kBikePhysicsTable` | void*[2] bikePhysics per slot. |
| 0x574CA4 | `kBikePhysicsCount` | int next slot (0..1). |
| 0x56B615 | `kCurrentTurnPlayer` | uint8 the rider whose turn it is. |
| 0x5FB6F0 | `kPlayerDefs` | Rider model definitions (NgIFroller.c), 0x10 bytes each: +4 character, outfit, bike, ...; 2 entries. |
| 0x5FB738 | `kPlayerDefCount` | int entries in use. |
| 0x691F40 | `kRiderArray` | RiderState[2], 0xF70 bytes each (`kRiderStride`). |
| 0x69621C | `kRiderCount` | uint8 riders in play (0 in the attract demo). |

**RiderState** (`addr::rider`)

| Offset | Name | Description |
|---|---|---|
| +0x00C | `kButtonsHeld` | Controller +0x10, copied each frame. |
| +0x010 | *ButtonsChanged* | *(inferred)* Controller +0x14 (pressed this frame). |
| +0x014 | *Analog* | 2 sticks × (x, y) floats, scaled 1/127. |
| +0x038 | `kFlags` | Bit 0: the rider is active (updated each frame). |
| +0x04E | `kTrickName` | uint8 trick shown by HUD element 8. |
| +0x04F | `kTrickName2` | uint8 trick shown by HUD element 7 in sub-state 6. |
| +0x05C | `kRunScore` | int32 points banked this run. |
| +0x060 | `kSessionScore` | int32 session total: SessionStart (0x40A8C0) zeroes it; the run end (0x40B2D4) adds `kRunScore` of the turn rider. |
| +0x064 | `kComboValue` | int32 current combo, added to `kRunScore` when banked. |
| +0x0EC | *ComboState* | uint8: 0 none, 1 airborne, 2 in trick, 3 landing. |
| +0x3DC | `kState` | uint8 index into the state handler tables. |
| +0x3E0 | `kMatrix` | Orientation matrix; position float[3] at +0x420. |
| +0xB94 | `kBikeActor` | actor* of the bike. |
| +0xD90 | `kRiderActor` | actor* of the body. |

## Input

The game's buttons follow a PlayStation pad: 0x1 up, 0x2 down, 0x4 left,
0x8 right, 0x10 Triangle, 0x20 Square, 0x40 Circle, 0x80 Cross, 0x100 L1,
0x200 R1, 0x400 L2, 0x800 R2, 0x4000 Start, 0x8000 Select; the keyboard adds
0x10000 (Esc) and 0x20000 (Enter) for the menus.

| Address | Name | Description |
|---|---|---|
| 0x408030 | `InitControllers` | Creates Controller[0..1] with device type 2, else 0. `InitControllersEnd` (0x4080A0) marks its end for return-address checks. |
| 0x4080F0 | `ControllerUpdate` | Per frame: device → Controller (+0x10 held, +0x14 changed). Bits 0x80, 0x20, 0x40, 0x10, 0x100, 0x200 and 0x4000 come through the PlayerConfig masks +0x24..+0x3C. |
| 0x4083E0 | `ControllersSetPaused` | (paused) called by `SetGameState`: on resume, buttons pressed during the pause are ignored until released. |
| 0x408590 | `ControllerCheck` | *(inferred)* (controller, mask, mode) → pressed. |
| 0x4086F0 | *GetController* | (index) → Controller*; asserts index ≤ 1. |
| 0x408740 | *ActionCheck* | (controller, mask, mode), called with the rider action table's entries. |
| 0x437200 | `MenuControllerInit` | Creates the front end's controller (type 2). `MenuControllerInitEnd` (0x437268) marks its end. |
| 0x4372B0 | `MenuControllerUpdate` | Per front-end frame: device → `kMenuController`. |
| 0x480370 | `CreateController` | (type) 0 DirectInput joystick, 1 keyboard, 2 both; type 2 always takes the first joystick. |
| 0x480400 | `InputDevice` | (n) → the n-th input device made; +8 its raw buttons. |
| 0x493910 | *DevicePoll* | Reads every device (pcinput.c): the keyboard state into `kKeyboardState`, then each device's key table into its input slots. |
| 0x553EE0 | *RiderActionTable* | {mask, mode} pairs per control scheme and action, checked with 0x408740. *(inferred)* Spin left checks L1 or L2 (0x500), spin right R1 or R2 (0xA00). |
| 0x561E68 | `kKeyboardLayoutP1` | Player 1's keys: {DirectInput key, input index} pairs ending with 0xFF. |
| 0x561E88 | *KeyboardLayoutP2* | Player 2's keys, in the same form. |
| 0x561EA8 | *JoystickLayout* | The DirectInput joystick's buttons, in the same form. |
| 0x56B620 | `kControllersPaused` | uint8 set by `ControllersSetPaused`. |
| 0x56B628 | `kControllerArray` | Controller[2], 0x3C bytes each (`kControllerStride`). |
| 0x56D298 | `kMenuController` | The front end's controller record: +0 device*, +4 held, +8 changed. |
| 0x674054 | `kKeyboardState` | uint8[256] DirectInput keyboard state, bit 7 down. |
| 0x6952A0 | `kPlayerConfigArray` | PlayerConfig[2], 0x4C bytes each (`kPlayerConfigStride`). |

**Controller**

| Offset | Description |
|---|---|
| +0x00 | device* |
| +0x04 | Buttons ignored until released. |
| +0x08 | Held at pause. |
| +0x0C | Flags. |
| +0x10 | Held. |
| +0x14 | Changed this frame. |
| +0x18 | Analog x, y, x2, y2 (float). |
| +0x28 | Previous analog. |

**PlayerConfig**

| Offset | Name | Description |
|---|---|---|
| +0x00 | `kPlayerCfgFlags` | uint8 flags; `kPlayerCfgHints` (0x02) tutorial hints on. |
| +0x01 | *Controller* | uint8 controller index. |
| +0x03 | `kPlayerCfgCharacter` | uint8 character, which also picks the career. |
| +0x04 / +0x05 | | uint8 outfit and bike. |
| +0x06 | `kPlayerCfgCamera` | uint8 chase camera type. |
| +0x07 | *Name* | *(inferred)* name string ("player"). |
| +0x14..+0x3C | | uint32[11] joystick masks, one per controller options row. |
| +0x37 | | uint8 keyboard layout (0..4). |

## Cameras

| Address | Name | Description |
|---|---|---|
| 0x4056C0 | `CameraInitAll` | Inits each rider's CameraCtl and registers a draw callback. |
| 0x405780 | `CameraUpdateAll` | Per frame: full update of the active camera controller only. |
| 0x407470 | *ApplyGameCamera* | Pushes the field of view and matrix into the game camera. |
| 0x407530 | *SetActiveCameraCtl* | (index) asserts index ≤ 1. |
| 0x407620 | `CameraCtlInit` | (player, type) inits the controller with that chase camera type. |
| 0x407760 | `CameraCtlSetMode` | (player, mode); `GameStart` uses (0, 4). |
| 0x4078C0 | *SetCameraMode* | (player, mode). |
| 0x56B408 | `kActiveCameraCtl` | uint8 index of the active controller. |
| 0x56B448 | `kCameraCtlArray` | CameraCtl[2], 0xB8 bytes each (`kCameraCtlStride`). |
| 0x68FF98 | `kGameCameraObject` | The zobjCamera the game renders with. |

## Engine and rendering

| Address | Name | Description |
|---|---|---|
| 0x480F30 | `FovScale` | Field-of-view units → scale, returned in ST0. |
| 0x481640 | `TransformPoint` | (out*, matrix*, in*). |
| 0x4820C0 | `ZobjCameraUpdate` | (zobj) pushes the object's transform into its zrwCamera. |
| 0x4821C0 | `ZobjCameraCreate` | () → zobjCamera*, its zrwCamera at +0x2C. |
| 0x482310 | `ZobjCameraMatrix` | (zobjCamera, 0) → its cached view matrix. |
| 0x487400 | `ZobjDestroy` | (zobj) virtual destroy and free. |
| 0x4896D0 | `MatCreateFromImage` | (data*, size, name, type) → material*. |
| 0x48B690 | `RenderFrame` | Renders every enabled camera, then flips. |
| 0x48BC60 | `ZglSetRenderFlags` | (flags, raster) zgl flags → RenderWare render states, called through 0x561490; bit 0 clear turns depth test and depth write on. |
| 0x4925F0 | `ZrwCameraCreate` | RwCamera with full-screen sub-rasters. |
| 0x492970 | `ZrwCameraDestroy` | Also the camera pool's destructor. |
| 0x492A60 | *ZrwCameraSetFov* | (cam, fov) → view window, assuming 4:3; the widescreen fix hooks +0x21 and +0x31. |
| 0x492AC0 | `ZrwCameraSetMatrix` | (cam, float[16] matrix, float[3] pos). |
| 0x492CC0 | `ZrwCameraSetViewport` | Re-subrasters a camera to a rectangle; assumes 4:3 and is not patched by the widescreen fix. |
| 0x492F00 | `ZrwCameraBeginUpdate` | (zrwCamera) before each camera's draw. |
| 0x492FE0 | *ZrwCameraEnd* | |
| 0x4DB820 | `RwRenderStateSet` | (state, value) in this RenderWare's numbering: 4 depth test (`D3DRENDERSTATE_ZENABLE`), 6 depth write (`kRwStateZWrite`). |
| 0x4DCB10 / 0x4DCC80 / 0x4DCFD0 | *RwRasterCreate*, *RwRasterSubRaster*, *RwRasterShowRaster* | *(inferred)* |
| 0x4DE5A0 / 0x4DE5D0 | *RwCameraGetRaster*, *RwCameraGetZRaster* | Read RwCamera +0x60 / +0x64. |
| 0x4DE5E0 | `RwCameraSetNearClipPlane` | Writes +0x80, rebuilds the frustum. |
| 0x4DE620 | `RwCameraSetFarClipPlane` | Writes +0x84, rebuilds the frustum. |
| 0x4DE660 | `RwCameraSetFogDistance` | Writes +0x88. |
| 0x4DE9B0 | `RwCameraSetViewWindow` | (cam, RwV2d*) writes +0x68. |
| 0x561470 / 0x561474 | *RenderCallbackAdd*, *RenderCallbackRemove* | Function pointers: (type, fn, param, flag) → node; (node). |
| 0x561D88 | *CamApiTable* | The zrwCam interface: Create +8 (0x4925F0), Destroy +0xC (0x492970), SetFov +0x10, SetMatrix +0x18 (0x492AC0), SetViewport +0x24 (0x492CC0). |
| 0x561D98 | `kCamApiSetFov` | The SetFov entry (0x492A60), which the widescreen fix replaces. |
| 0x56BFFA | `kButtonTextureSlot` | uint16 texture slot of the button font (texture 0x33). |
| 0x67401C | `kCameraPool` | The pool of every zrwCamera: first node at +4, each node's next at +0. |
| 0x68C3E0 | *TextureTable* | Texture slots, 512 × 20 bytes. |
| 0x68C3F0 | `kTextureTableMaterial` | Slot n's material pointer is at this address + n × 20 (`kTextureTableStride`). |
| 0x6976C0 / 0x6976C4 | *FrameBufferRaster*, *ZBufferRaster* | RwRaster*; every camera is a sub-raster of the frame buffer. |
| 0x697770 / 0x697774 | `kScreenWidth`, `kScreenHeight` | int screen size in pixels. |
| 0x697778 | `kZglCurrentFlags` | uint32 flags `ZglSetRenderFlags` last applied. |

**zrwCamera** (200 bytes, `game::zrw`): +0x08 flags (bit 1 fog, bit 2
rendered), +0x0C RwCamera*, +0x10 owning zobjCamera, +0x54 matrix (position at
+0x84), +0xA4 / +0xA8 viewport width and height, +0xBC int16 field of view
(0x271C default), +0xC0 fog colour. **RwCamera** (`game::rwcam`): +0x60
raster, +0x64 z raster, +0x68 view window, +0x80 near plane, +0x84 far plane,
+0x88 fog distance.

## Shadow

One shadow descriptor, overwritten by each rider created and drawn by one
render callback: +0 model count (2), +4 models (rider, bike), +8 bike model
(centres the shadow), +0xC light direction; 0x20 bytes.

| Address | Name | Description |
|---|---|---|
| 0x49B3D0 | `ShadowGridDraw` | () list-2 callback: draws the grid if 0x6751FC is set, then clears it. |
| 0x49B4B2 | `kShadowDrawFlags` | Low byte of `push 0x4103`, the shadow's zgl flags (bit 0 no depth test). |
| 0x49B520 | `ShadowDraw` | (desc*) list-5 callback: builds the shared shadow grid around the descriptor's rider and sets 0x6751FC. |
| 0x678440 | `kShadowDesc` | The shadow descriptor. |

## Sound

| Address | Name | Description |
|---|---|---|
| 0x44E300 | `GameEvent` | (event, a, b, c, d): announcer, sounds and HUD pop-ups. Menu sounds: 2000 move, 0x7D1 toggle, 0x7D2 confirm, 0x7D3 back, 0x7D4 refused. Event 0xFA0 updates a rider slot's bike loops (b >> 28 the slot, c tyre, d pedal); 0x3E8 sets the sound mode. |
| 0x45A340 | `SoundListenerSpace` | (world pos*, out*) into `kGameCameraObject`'s space. |
| 0x4704A0 | `SfxBankSample` | (bank, sample index) → sample*. |
| 0x47E520 | `SndSrcCreate` | (sample*, pos*, flags) → u16 handle (0xFFFF on failure). |
| 0x47E650 | `SndSrcDestroy` | (handle). |
| 0x47E910 | `SndSrcSetVolume` | (handle, volume). |
| 0x55B538 | `kSfxVolume` | uint8 master effects volume. |
| 0x55B540 | `kGameSfxBank` | uint16 in-game effects bank (0xFFFF none). |
| 0x57466C | `kPedalLoopHandles` | uint16[2] pedal and chain loop per rider slot. |
| 0x574670 | `kTireLoopHandles` | uint16[2] tyre and grind loop per rider slot. |
| 0x64FCD0 | `kSndSrcPool` | The 24 3-D sound sources (zsndsrc.c), 0x28 bytes each (`kSndSrcStride`). |
| 0x650094 | `kSndSrcMaxDistSq` | uint32 hearing distance squared (2500²). |

## HUD

The HUD (bmx_zov.c) is a port of the PlayStation HUD: text and sprites are
PlayStation GPU primitives in double-buffered pools of 0xA0-byte entries,
linked into an ordering table and drawn in screen pixels (x × width / 640,
y × height / 480). An object handle is `index | size << 12 | type << 14`.

| Address | Name | Description |
|---|---|---|
| 0x4147E0 | `HudLevelInit` | Creates the HUD elements and registers the HUD tick (0x414F60). |
| 0x4148B0 | `HudLevelShutdown` | Destroys them. |
| 0x414970 | `HudTexturesInit` | () allocates and loads the HUD textures. |
| 0x415300 | `HudEnable` | (element, on) shows or hides an element. |
| 0x415370 | `HudSet` | (id, data*) sets an element of the one shared HUD; id 1 score, 2 timer, 7/8 trick names, 0xA tutorial hint, 0xC message line (`kHudMessage`), 0x10 screen fade. |
| 0x4153A0 | *HudGet* | (id, data*) reads an element's value. |
| 0x457B00 | *OverlayFlip* | End of the HUD tick: swaps the double-buffered pools. |
| 0x457F70 | `OverlayPool` | (type, size, bank, &base, &count, &kind). |
| 0x4578F0 | `OverlayInit` | Links each pool's entries (28-bit links) and marks them free. |
| 0x4580E0 | `OverlayAlloc` | (type, size) → handle of a free pool entry. |
| 0x4581A0 | `OverlayDestroy` | (handle). |
| 0x458400 | `OverlayShow` | (handle, on) shows or hides an object; hiding clears the length of its primitives' tags. |
| 0x458B00 | `OverlayMove` | Changes an object by handle; like the three below, it does not check for handle 0xFFFF. |
| 0x458CC0 / 0x459940 / 0x459BE0 | `OverlaySetA`, `OverlaySetB`, `OverlaySetC` | Change an object by handle. |
| 0x458EA0 | `OverlayImmediateBox` | (x, y, w, h, rgb) filled box for one frame. |
| 0x4594C0 | `OverlayTextCreate` | (0, font*, text, x, y, rgb, 0) → u16 handle. |
| 0x459DF0 | `OverlayImmediateText` | (font*, text, x, y, rgb, flags) text for one frame. |
| 0x459F30 | `OverlayTextWidth` | (text, font*) → width in 640×480 units. |
| 0x462050 | `OverlayDrawPrim` | (prim) one primitive → 2-D quads and lines. |
| 0x462350 | *DrawOTag* | Draws an ordering table. |
| 0x462940 | *OverlayImmediate* | (x, y, glyph) builds per-frame primitives at `kOverlayPrimCursor`. |
| 0x463820 | `OverlayBeginA` | A new HUD frame: clears the ordering table, rewinds `kOverlayPrimCursor` and sets the pending flag. |
| 0x463860 | `OverlayDrawA` | Per-camera draw callback: draws the ordering table if `kOverlayPendingA` is set, then clears it, so the HUD shows in the first camera drawn. |
| 0x4639A0 | `OverlayDrawB` | The same for `kOverlayPendingB` (the GUI). |
| 0x463AB0 | `OverlayBeginB` | The same as `OverlayBeginA` for the GUI. |
| 0x48CCF0 | `Zgl2DLines` | (flags, items {tex, v0, v1}, n, m); flags & 0x2000000 screen space. |
| 0x48D120 | `Zgl2DTris` | (flags, items {tex, v0, v1, v2}, n, m). |
| 0x48D660 | `Zgl2DQuads` | (flags, items {tex, v0..v3}, n, m); a vertex is x, y, z, ?, u, v, rgba. |
| 0x54D0DC | `kMeterScale` | float, the height meter's scale. |
| 0x556F00 | `kHudElements` | 17 × 0x18-byte elements: flags (bit 0 hidden), show, update, enable, set, get. |
| 0x596A38 | `kGlyphPool` | Text glyph pool (0, 1): 2 banks of 128. |
| 0x5A0A38 | `kBoxPool` | Box pool (0, 0): 2 banks of 16. |
| 0x5A22BC | *OverlayBank* | uint8 pool bank being built (flips each frame). |
| 0x56BF38 | `kHudFont` | The HUD's font, also the score's. |
| 0x5D24FC | `kOverlayPendingA` | int: `OverlayDrawA` draws on the next camera. |
| 0x5D2504 | `kOverlayPendingB` | int: `OverlayDrawB` draws on the next camera. |
| 0x68EBE0 | `kOverlayPrimCursor` | Next free per-frame primitive (0xFFFFFFFF none). |

**HUD state** 0x56BD48..0x56C080 (`addr::hud`); the two fonts inside are
shared.

| Address | Name | Description |
|---|---|---|
| 0x56BD48 | `kStateBegin`, `kMeterBase` | Start of the state; float, the height meter's zero. |
| 0x56BD50 | `kScoreShown` | int, the score as it counts up. |
| 0x56BD58 | | int, the score's target. |
| 0x56BE2A | `kResetByte` | uint8 the HUD setup sets to 0xFF. |
| 0x56BE2C | `kScoreText` | u16 handle of the score text (bottom right). |
| 0x56BE90 / 0x56BE94 | `kHandleA`, `kHandleB` | u16 object handles. |
| 0x56BE98 | `kFontA` | Shared font, 0xC bytes (`kFontSize`). |
| 0x56BEF8 | `kFadeBox` | u16 handle of the full-screen fade box. |
| 0x56BF2E | `kMeterMarker` | u16 handle of the height meter's marker. |
| 0x56BF38 | `kFontB` | Shared font (`kHudFont`). |
| 0x56BF58 | `kTimerValue` | The timer element's value, set by the session. |
| 0x56BFA8 | `kHandleC` | u16 object handle. |
| 0x56BFC0 | `kMeterHeight` | float, 0 while the meter is hidden. |
| 0x56BFC4 | `kMeterValue` | float, the height shown. |
| 0x56BFFC | `kHandleD` | u16 object handle. |
| 0x56C000 | `kTimerText` | u16 handle of the run timer text (bottom left). |
| 0x56C054 | `kFadeFrames` | u16 frames left of the screen fade (element 16), 0 none. |
| 0x56C080 | `kStateEnd` | End of the state. |

## GUI primitives

The GUI's primitives of a frame come from one buffer, rewound by
`OverlayBeginB`, and each takes an ordering-table entry, counting down from
the top. Both hold 1024 primitives without a check.

| Address | Name | Description |
|---|---|---|
| 0x4639F0 | `GuiPrimSetup` | () sets `kGuiPrimBase` to the static buffer if 0. |
| 0x463826 | `kOtCountA` | imm32 of `push 0x400` in `OverlayBeginA`. |
| 0x463874 | `kOtTopDrawA` | imm32 of `add eax, 0xFFC` in `OverlayDrawA`. |
| 0x4639B4 | `kOtTopDrawB` | imm32 of `add eax, 0xFFC` in `OverlayDrawB`. |
| 0x463AB6 | `kOtCountB` | imm32 of `push 0x400` in `OverlayBeginB`. |
| 0x463AC7 | `kOtTopB` | disp32 of `lea ecx, [eax + 0xFFC]`: the GUI's first entry. |
| 0x55C690 | `kScreenSize` | float width, height in pixels (GUI clip areas). |
| 0x55C698 | `kOrderingTable` | uint32* the ordering table (static 0x68EC00, 0x400 entries, `kOrderingTableEntries`), drawn top down. |
| 0x5AB4FC | | The static GUI primitive buffer, 0x27000 bytes (`kGuiPrimBufferSize`). |
| 0x5D250C | `kGuiPrimBase` | The GUI primitive buffer (0 until `GuiPrimSetup`). |
| 0x5D2510 | `kGuiPrimCursor` | Next GUI primitive; x 0..512 across, y 0..480. |
| 0x68C3D0 | `kGuiOtCursor` | uint32* entry of the next GUI primitive. |

## Runs and scoring

| Address | Name | Description |
|---|---|---|
| 0x409D20 | `RunUpdate` | Per frame: trick tracking for the turn rider, the run timer and the run end. |
| 0x40A8C0 | *SessionStart* | Zeroes the riders' session scores. |
| 0x40A9F0 | `GetSessionMode` | Returns `kSessionMode` zero-extended in EAX. |
| 0x40AA50 | `TurnSystem` | (event) run start and end, the turns; hides waiting riders. Event 5 (`kTurnRunEnd`) is the run end. |
| 0x40AA6D | `TurnEndModeCheck` | Return address of the `GetSessionMode` call at the turn end: anything but mode 11 passes the turn on. |
| 0x40ADA7 | `TurnStartModeCheck` | Return address of the call at the turn start: mode 11 places and shows the other riders instead of hiding them. |
| 0x40D010 | `TrickTracker` | (slot) combo tracking; banks with `BankCombo(turn player)`. |
| 0x40DAE0 | `BankCombo` | (player, landed) combo → run score, totals, trick book. |
| 0x40E030 | `RecordCheck` | (player, category, value, ...) level records and goals; category 1 is the run score. |
| 0x40E380 | `TutorialTips` | (player) tutorial hints (HUD element 0xA) when `kPlayerCfgHints` is set. |

## Career and goals

| Address | Name | Description |
|---|---|---|
| 0x407FC0 | `GoalBlockFor` | (player, level) → the goal block of the player's character. |
| 0x40A400 | `GoalHighlights` | Once per frame (draw callback list 0x5FB6C8): the turn player's goal items in state 2 make the goal's objects glow. |
| 0x40C300 | `GoalCheckLanding` | (player, flag) goal checks on landings and grinds. |
| 0x40CA60 | `GoalCheckObject` | (player, object) prop hit: type 0xC goals. |
| 0x40CBE0 | `GoalCheckProp` | (player, object) prop event: type 0x15 goals. |
| 0x40CD40 | `GoalsRunStart` | (player) at the run start: resets the player's goal items and opens the props of goals done in his career. |
| 0x40EC40 | `PositionGoals` | (player) ProQuest goals tied to places and gaps; ends at 0x40F855 (`PositionGoalsEnd`). |
| 0x4102C0 | *GoalGet* | (player, goal, item) → the item's state (0..7). |
| 0x410350 | `GoalSet` | (player, goal, item, state) writes the career block; state 5 also unlocks the rewards. |
| 0x555118 | *GoalDefs* | Per (level × 3 + difficulty): 4 goals of 0x54 bytes; +0 item count, items at +4 (0x14 each: +0 type, +2 record, +4 over). |
| 0x56BA28 | `kGoalObjectLists` | 2 × 64 object* (zero-terminated, 0x200 bytes) that glow for the shown tip's goal. |
| 0x56BC2C | `kGoalGlowPulse` | uint32 frame counter of the goal glow. |
| 0x6955E7 | *RidersUnlocked* | uint8 riders the rider select offers. |
| 0x6955E8 | `kCareerArray` | One 0x104-byte career block per character (`kCareerStride`): trick book, goals per level; +0x05 styles unlocked, +0x06 bikes unlocked. |
| 0x6955EF | `kGoalBlocks` | ProQuest goal progress: a 0x27-byte block per level 0..5 (`kGoalLevels`) in each career. |

## In-game screens

The in-game menus are opened by `ScreenOpen(id)` from a table of entries
{create(show), tick() → acted, draw(), u8 item count (+0xC), u8 selected item
(+0xD)}. Ids: 1 PAUSE, 2 OPTIONS, 3 AUDIO OPTIONS, 4-6 TRICK LIST, 7 END RUN?
/ EXIT?, 8/9 next rider's turn, 0xA RUN FINISHED (`kRunFinishedScreen`), 0xB
SAVE, 0xC level intro, 0xD-0xF CHALLENGES, 0x10 RESULTS, 0x11 new record name,
0x12-0x15 RECORDS.

| Address | Name | Description |
|---|---|---|
| 0x415140 | *ScreenTick* | Runs the tick once the items show; moves the selection when there are 2 or more items. |
| 0x415400 | `ScreenOpen` | (id) closes the screen showing and opens `id` (0 none). |
| 0x415B70 | `OptionsCreate` | OPTIONS (entry 0x5570A8): (show) title, labels, then the values (0x416060). |
| 0x415CF0 | `OptionsTick` | () → acted; left/right change camera type or tutorial hints on the turn player's pad. |
| 0x41A480 | `RunFinishedCreate` | RUN FINISHED (entry 0x557108): (show) title and items. |
| 0x41A830 | `RunFinishedDraw` | () one score line per rider, then the items. |
| 0x41AD00 | `RunFinishedTick` | () → acted, by item action 0..8 (6 trick list). |
| 0x41E9C0 | `SelectedItemColour` | () → pulsing colour of the selected item. |
| 0x5571B8 | `kGameScreenTable` | entry*[0x1A] by id (0x1A reopens the last one). |
| 0x56BE14 | `kScreenFrames` | uint32 frames since the screen opened. |
| 0x56BEBA | `kGameScreen` | uint8 id of the screen showing (0 none). |
| 0x56BEBC | `kMenuTexts` | u16[24] the screen's text handles, all freed when a screen opens. |
| 0x56BEEA | `kScreenTitleText` | u16 title text handle (the last `kMenuTexts` slot). |
| 0x56BF20 | `kGameScreenPtr` | The showing screen's table entry. |
| 0x56BFA4 | `kScreenItemsDelay` | uint32 frames before the items show and the tick runs. |
| 0x56C028 | `kScreenItems` | {u16 action, u16 string}[10] (`kScreenItemsMax`) of the screen showing. |

## Front end

| Address | Name | Description |
|---|---|---|
| 0x437870 | *GuiClear* | Destroys the current page's elements. |
| 0x437930 | *GuiChangeScreen* | (page, handler) schedules a page change through 0x573080. |
| 0x437980 | *GuiLoadScreen* | Loads SCRN resource 0xE000 \| page from intart.zal. |
| 0x4375A0 | | *(inferred)* (handler) installs the page's event handler. |
| 0x4383A0 | `GuiFindElement` | (id) → element* of the current page, or 0. |
| 0x438D10 | | (element*, value) writes element +0xD4. |
| 0x438DB0 | `GuiSetText` | (element*, text) copies the text. |
| 0x4391C0 | *GuiSetScreenBackdrop* | (page, backdrop) loads a backdrop from that page's group and shows it. |
| 0x4392A0 | *GuiSetBackdrop* | (backdrop) loads and sets the current page's backdrop. |
| 0x439460 | `SendElement` | (event, element, a, b) to one element (0xC draws it). |
| 0x4395E0 | `GuiSendEvent` | (event, a, b) to the elements and the page's handler. |
| 0x4396B0 | *GuiDefaultHandler* | Sends 0xC to every element on 0x15 and 0xB on 0x16. |
| 0x4397C0 / 0x4397E0 | *GuiShowElement*, *GuiHideElement* | (element*) clear or set the hidden flag (+2 bit 0). |
| 0x439A00 | *GuiParseScreen* | (page) parses the SCRN header (+0x0C backdrop) and creates the EDEF elements. |
| 0x44D990 | *FrontEndReturn* | Back to the front end after a level: the rider select on step `kSelectReturnStep` 1..3, else 0x43F730 when 0x56B618 is set, else `MainMenuEnter(0)`. |
| 0x44DD10 | `FrontEndExit` | (what) leaves the front end: 0 starts the game, 1 quits. |
| 0x46B030 | `MemAlloc` | (size) → handle. |
| 0x46B140 | `MemFree` | (handle). |
| 0x46B470 | `MemLock` | (handle) → pointer. |
| 0x46B4F0 | `MemUnlock` | (handle). |
| 0x477BA0 | *ZPrimInit* | (archive) initialises ZPRIM with intart.zal. |
| 0x477DA0 | `GuiSetClip` | (x0, y0, x1, y1); (0, 0, 639, 479) is none. |
| 0x478050 / 0x478370 | *GuiSetProp*, *GuiGetProp* | GUI renderer properties 0x0F..0x18. |
| 0x478A50 | *GuiSetFont* | (font) the font text draws use. |
| 0x478B60 | *GuiSetAsset* | (asset) the sprite asset `GuiDrawSprite` draws from. |
| 0x478D80 | *GuiSetColor* | (r, g, b, prop) packs RGB and calls `GuiSetProp`. |
| 0x478DF0 | | *(inferred)* Draws one side of the frame around the row being captured; the controller options call it 4 times. |
| 0x479660 | *ZPrimLoadSprite* | (sprite) finds it in the group slots and creates a DirectDraw texture surface. |
| 0x479CD0 | `GuiFreeImage` | (image) frees an image a page made. |
| 0x479CF0 | *GuiDrawSprite* | (frame, x, y) a frame of the current asset. |
| 0x47A620 / 0x47AA80 | *GuiDrawTextFixed*, *GuiDrawTextProp* | (text, x, y) fixed or proportional text in 640×480 space. |
| 0x47AF10 / 0x47AFD0 | *GuiFontHeight*, *GuiTextWidth* | In 640×480 units. |
| 0x47B3D0 | *GuiGroupLoaded* | (group) → non-zero if loaded. |
| 0x47B400 | *ZPrimLoadGroup* | (slot, group) slot 0 = 0xE000, slot 1 = 0xE000 \| page. |
| 0x47B5C0 | `ZPrimUnloadGroup` | (slot) frees the group and each of its assets. |
| 0x47B830 | `GuiAssetAlloc` | () → a free asset handle linked into the list, or 0. |
| 0x47B900 / 0x47B930 | *GuiAssetLock*, *GuiAssetUnlock* | Data: u16 frames, frame n at +20 + 16n (w, h, .., x, y). |
| 0x47B950 | *GuiLoadBackdrop* | (backdrop) loads a sprite or backdrop from intart.zal. |
| 0x47BAC0 | *GuiAssetFind* | (id) → loaded asset, or 0 (does not load). |
| 0x47BAF0 | *ZPrimFindSprite* | (sprite) searches the loaded group slots. |
| 0x47C030 | *DirectDrawCreateSurface* | (w, h, bpp, pixels, palette) builds a BMP and texture from an RGB555 palette. |
| 0x4824F0 | `GetString` | (u16 id) → localised text; set 1 holds ids 0x1000..0x11AE. |
| 0x48A9B0 | *QuitMessageBox* | "Do you want to quit the game?" message box. |
| 0x54D23C | *RiderPortraits* | uint16[12] 128×192 portrait per rider (GUI group 3). |
| 0x54D254 | *RiderNames* | uint16[12] string id per rider. |
| 0x54D35C | *LevelLocationPanels* | uint16[8] group 3 textured panel sprites (240×184). |
| 0x54D3BC | *LevelBanners* | uint16[60] level select 640×192 banner sprites (5 per level). |
| 0x54DB58 | *LevelLocationCards* | uint16[9] group 10 card sprites (166×128). |
| 0x55AB18 | *BikeNames* | uint16[12] string id of each rider's first bike (+ bike), read at 0x4464C0. |
| 0x55AB30 | *LevelBadges* | uint16[12] competition and level badge sprites. |
| 0x573080 | *GuiNextScreen* | uint16 target page (e.g. 0xA005, 0xA009). |
| 0x573088 | *GuiCurrentBackdrop* | Loaded backdrop sprite descriptor. |
| 0x5730A4 | `kGuiElements` | element*[]: +2 u16 flags (bit 0 hidden), +4 id. |
| 0x57323C | `kGuiElementCount` | int elements on the current page. |
| 0x57458C | `kScreenStep` | fn* the page runs each frame (0 idle). |
| 0x5745A4 | `kScreenPending` | fn* main menu action waiting to run. |
| 0x678400 | *ZPrimAssetHandle* | intart.zal asset handle. |
| 0x678408 | `kGuiAssetFree` | GUI asset handles (128 of 16 bytes) not in use, linked at +4. |
| 0x67840C | `kGuiAssetList` | Loaded assets and groups, linked at +4: +0 id, +2 flags, +8 memory, +0xC offset in it. |
| 0x678418 | `kZPrimSlots` | void*[4] loaded asset groups. |
| 0x691EB8 | `kScreenPendingTicks` | int ticks since `kScreenPending` was set (it runs after 2). |

**GUI element** (0xF0 bytes, `kGuiElementSize`): +0 type, +2 flags (bit 0
hidden), +4 id, +0x34 image, +0xD8 text memory handle, +0xDC its size.

**Main menu** (page 0xA005): a 7-slot wheel over 6 items.

| Address | Name | Description |
|---|---|---|
| 0x43FA20 | `MainMenuEnter` | (fromTitle) handler and wheel; 0 also loads SAVE.DAT and puts the wheel on its first item. |
| 0x43FB10 | `MainMenuInstall` | () installs the main menu's handler and wheel labels; `MainMenuEnter` and the post-demo page 0x43F0C0 call it. |
| 0x43FB80 | `MainMenuHandler` | Main menu events; confirm acts on `kMainMenuSelected`: 0..2 game types, 3 0x44CA00, 4 0x4433E0 (OPTIONS), 5 quit. |
| 0x4404E0 | `MainMenuLabels` | Lays out and labels the 7 wheel slots. |
| 0x55A35C | `kMainMenuStrings` | uint16[6] string ids of the items (`kMainMenuItems`). |
| 0x5745A0 | `kMainMenuSelected` | uint16 item in the centre slot, 0..5. |
| 0xA005000D | `kMainMenuSlotMid` | Element of the centre slot; the slots are 0xA005000A..10. |

## Title screen and attract demo

| Address | Name | Description |
|---|---|---|
| 0x401FAA | `TitleEnterThunk` | `jmp TitleEnter`, as stored in `kScreenPending`. |
| 0x40477B | | In the attract demo: a fresh press on Controller[0] or [1] (flags +0xC bit 0 clear, held & changed) ends it. |
| 0x43F0C0 | *PostDemoPage* | Page 0xA005 handler after the attract demo: *(inferred)* checks the save, then after 0x78 ticks installs the main menu (`MainMenuInstall`). |
| 0x43F730 | *PostDemoEnter* | Opens page 0xA005 with handler 0x43F0C0. |
| 0x43F7A0 | `TitleHandler` | (event, a, b, c) "PRESS START BUTTON": Start runs `MainMenuEnter(1)` 2 ticks later; after 0x708 idle ticks it starts the attract demo. |
| 0x43F9B0 | `TitleEnter` | () installs `TitleHandler` on page 0xA005; the PC game never calls it. |
| 0x440D30 | `AttractDemoStart` | () starts the attract demo (rider count 0), run as the pending action of the title screen or the main menu. |
| 0x5745AE | `kTitleTextSet` | uint16 set once the title's tick has set its text. |
| 0x56B618 | | int tested by `FrontEndReturn`; the code only clears it. |
| 0x691EB4 | | uint16 idle ticks of the title screen. |
| 0xA0050001 | | The title text element. |

## Rider select

Page 0xA009, kept from the PlayStation 2-player mode: with `kRiderCount` = 2
each step is done once per player, player 2 driven by GUI events 0x2B..0x37
(player 1's 0x1E..0x2A plus 13).

| Address | Name | Description |
|---|---|---|
| 0x402491 | `SelectFirstStep` | Thunk to 0x449A80, the step that sets the page up. |
| 0x4020EA / 0x4025A9 | *SelectLevelStepEntry*, *SelectRiderStepEntry* | Thunks to the level step and the rider step (0x444D20). |
| 0x445760 | *SelectLevelStep* | Enters the level step; with 2 riders it offers the levels of any career. |
| 0x447540 | `SelectEnter` | Main menu → rider select. |
| 0x447600 | `SelectLeave` | Tears the select down (back or start). |
| 0x4476C0 | *SelectCommit* | Writes both players' picks to PlayerConfig[0/1]. |
| 0x4479D0 | `SelectMove` | (dir) next or previous chooser; the step changes when it wraps. Sets "PLAYER n" (string 0x3B) in element 0xA0090002. |
| 0x447BF0 | `SelectHandler` | Rider select events; tests `kRiderCount` == 2. |
| 0x55A354 | `kGlowStep` | int +1/-1: direction of the yellow frames' glow. |
| 0x56B617 | `kSelectReturnStep` | uint8 step the select opens on next: 0 rider, 3 level (LEVEL SELECT after a run). |
| 0x574584 | `kSelectCallbacks` | +0 the yellow frames' glow (0..0x40), +4..+0xC fn*: change handlers and the next step (`kScreenStep`). |
| 0x5745EC | `kSelectImages` | The images of the outfit and bike steps (0x44 bytes, `kSelectImagesSize`). |
| 0x691700 | `kSelectState` | The select's globals up to 0x691DD4 (0x6D4 bytes, `kSelectStateSize`): timer, steps, picks, cards, images. |
| 0x691704 | `kSelectTimer` | uint frame counter. |
| 0x69182C | `kSelectWaitUntil` | uint: the steps wait for `kSelectTimer` to pass it. |
| 0x691CA0 | `kSelectRider` | int[2] → PlayerConfig +3. |
| 0x691CA8 | *SelectPlayer* | int the player choosing (0/1). |
| 0x691CB0 | `kSelectStep` | int: -1 back, 0 rider, 1 bike, 2 style, 3 level, 4 go. |
| 0x691CF8, 0x691CFC, 0x691D00 | `kSelectCardImages` | Images the first step makes. |
| 0x691D58 | `kSelectOutfit` | int[2] style → PlayerConfig +4. |
| 0x691D98 | `kSelectBike` | int[2] → PlayerConfig +5. |
| 3 | `kSelectPortraitGroup` | GUI asset group of the rider step (bank 0xE004). |
| 0xDB | *StyleString* | "STYLE"; the style step shows "%s %d" (0x445B90). |

## Controller options

Page 0xA00C (main menu → OPTIONS): a row per action (UP, DOWN, LEFT, RIGHT,
JUMP, TRICK, MODIFIER, GRIND/STALL, SPIN LEFT, SPIN RIGHT, PAUSE), then the
keyboard layout, OK and CANCEL rows. The JOYPAD text of a row is element
0xA00C0037 + row.

| Address | Name | Description |
|---|---|---|
| 0x441CB0 | `ControlsShow` | Makes the rows from the player's PlayerConfig masks and installs `ControlsHandler`. |
| 0x441F80 | *ControlsLeave* | Hides the rows; *(inferred)* returns to OPTIONS. |
| 0x4420A0 | `ControlsHandler` | (event, a, b, c). Confirm or Start on an action row starts the capture (sound 0x7D1); on OK copies the masks and the keyboard layout into PlayerConfig (0x7D2); back or CANCEL leave without it (0x7D3). During the capture, every tick and back draws the frame around the row and takes the lowest raw button of `InputDevice(player)`: its mask, the row's text and sound 0x7D2. |
| 0x55A3F4 | | char*[18] JOYPAD names by mask bit: "Up" .. "Fire 6". |
| 0x55A43C | | char*[5] "< KEYBOARD LAYOUT n >". |
| 0x55A450 | | char*[5][18] KEYBOARD names per layout. |
| 0x5745B4 | `kControlsMasks` | u32[11] the screen's copy of the rows' masks. |
| 0x5745E0 | `kControlsRow` | u8 the selected row. |
| 0x5745E1 | `kControlsCapture` | u8 the capture: 0 off, 1 waiting until the device's buttons are all released, 2 waiting for a button. |
| 0x5745E2 | | u8 the keyboard layout shown. |
| 0x691E08 | `kControlsPlayer` | int the player whose controls the screen edits. |
