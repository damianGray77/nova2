#pragma once

// The viewer's dark theme, from Nova 1's. Colours are 0x00RRGGBB; virtual
// keys are Win32's VK_ codes (windows.h stays out).
#include "nova2/types.h"

namespace nova2 {

inline constexpr u32 kBg = 0x16181D, kPanel = 0x1F2229, kHead = 0x282C35, kText = 0xD8DEE9, kDim = 0x8A91A0,
                     kFaint = 0x4A505E, kAccent = 0x5EA1FF, kWarn = 0xE5A050, kBad = 0xE06060, kGood = 0x60C080,
                     kEdge = 0x3A404D, kButton = 0x30343F, kButtonHot = 0x3B4150, kRowSel = 0x2F4A70,
                     kRowHot = 0x2A2E38;
inline constexpr u32 kSeriesColor[4] = {0x5EA1FF, 0xF2A65A, 0x7BD88F, 0xE06C9F};
// Deaths by cause (DeathCause's order: exit, fault, cut-out, device failed,
// memory, cold start, heat), and gate 2's power colours.
inline constexpr u32 kCauseColor[7] = {0x5EA1FF, 0xE06C9F, 0xF2A65A, 0x8A91A0, 0xA06CD5, 0xE06060, 0xFF8C1A};
inline constexpr u32 kVoltMean = 0xE8C547, kVoltLow = 0xE06060, kCutOut = 0xE06060;

// The map (gate 3): water, land, links, bridges, and a place with nothing
// to show.
inline constexpr u32 kWater = 0x142231, kLand = 0x2A3029, kLink = 0x59627A, kBridge = 0x7FB0E0, kEmptyPlace = 0x4A505E;

// What is happening at a place now (viewer.cpp's NowKind, worst first):
// cold start, memory fading, blackout, heat trip, power low, swell, network
// off.
inline constexpr u32 kNowColor[7] = {0x40E0F0, 0xE060E0, 0xFF4040, 0xFF8C1A, 0xF0D040, 0xA080FF, 0x8FA8C8};

// Memory strips (13.4).
inline constexpr u32 kPageFree = 0x24272E, kPageReserved = 0x4A505E, kPageRegion = 0x3D4A63, kPermData = 0x4F7FC9,
                     kPermExec = 0xD9894A, kPermBoth = 0xA06CD5, kRunHeld = 0x7BD88F;

inline constexpr u32 kVkSpace = 0x20, kVkEscape = 0x1B, kVkPlus = 0xBB, kVkMinus = 0xBD, kVkAdd = 0x6B,
                     kVkSubtract = 0x6D, kVkHome = 0x24;

} // namespace nova2
