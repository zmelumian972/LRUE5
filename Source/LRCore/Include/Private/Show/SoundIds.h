#pragma once

// The race's sound ids: the effects bank's (GENERAL.SBK, by index), then the hazards' in the
// track's ambient bank (3000 + its index).

#include <cstdint>

namespace lr {

// The countdown's, a player's powerslide begun, the landings (a rise of one wheel, two or three, four).
constexpr int32_t CountdownBeepSound = 0, CountdownGoSound = 15, DriftSound = 57, LandingSounds[3] = {63, 64, 4};

// The engines: a player's layers (idle, drive, coast) and its start, everyone else's; the brake's squeal and loop,
// the skid, the spin, the scrapes and the bumps (drawn).
constexpr int32_t EngineSounds[3] = {32, 10, 61}, EngineStartSound = 62, OthersEngineSound = 79, BrakeSquealSound = 2, BrakeLoopSound = 3;
constexpr int32_t SkidSound = 58, SpinSound = 60, ScrapeSounds[2] = {56, 25}, BumpSounds[2] = {24, 55};

// The power-ups'.
constexpr int32_t TurboStartSounds = 36; // + the turbo's level
constexpr int32_t TurboLoopSounds = 42; // + the turbo's level (a warp's is the last)
constexpr int32_t TurboEndSound = 40, StrongTurboEndSound = 75;
constexpr int32_t WarpSound = 45, WarpOutSound = 39, WarpInSound = 41; // the warp's loop; the player's own warp
constexpr int32_t WhooshSound = 65; // a turbo's start, a warp's
constexpr int32_t BrickSounds = 11; // + the white bricks held: a brick picked up
constexpr int32_t BrickBackSound = 14; // a taken brick grows back
constexpr int32_t ColourSwapSound = 72, ColourTakenSound = 49; // a colour brick taken with a colour held, or not
constexpr int32_t ShieldSounds[4] = {76, 53, 77, 78}, ShieldEndSound = 59, BlockSounds[3] = {1, 70, 71}; // the loops by level
constexpr int32_t OilSplashSound = 46, OilLoopSound = 48, TrapSpinSound = 47;
constexpr int32_t MagnetDropSound = 33, MagnetLoopSound = 34, MagnetCatchSound = 35, MagnetFadeSound = 74;
constexpr int32_t CurseSkullSound = 8, CurseLoopSound = 9; // a cursed racer's skull's loop; the curse's
constexpr int32_t BlastSound = 5; // a cannonball's, the dynamite's
constexpr int32_t FuseSound = 17; // the dynamite's
constexpr int32_t CannonFireSound = 6, MissileLaunchSound = 51, MissileBlastSound = 50;
constexpr int32_t HookFireSound = 21, HookGrabSound = 18, HookReleaseSound = 20, HookSnapSound = 22, HookReelSound = 73;
constexpr int32_t BoltHumSound = 68, CrackleSound = 69, BoltStrikeSound = 67, ThunderSound = 66; // the lightning's
// The projectiles' flights: cannonball, hook flying, hook attached, missile.
constexpr int32_t FlightSounds[4] = {7, 23, 19, 52};

// The hazards' (the track's ambient bank).
constexpr int32_t LavaBubblingSound = 3002, CraneHumSound = 3003, GhostWailSound = 3012, GhostNearSound = 3013, GhostHitSound = 3015;

} // namespace lr
