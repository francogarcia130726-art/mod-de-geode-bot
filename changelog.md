# 1.0.5
- Made Safe Mode mandatory for attempts using Live Bot or Playback; it remains active after automation is switched off and resets on a full restart or level exit.
- Removed the Safe Mode toggle and added a persistent in-level `SAFE MODE ACTIVE` indicator.
- Kept the normal end screen while forcing the vanilla completion flow into test mode; blocked `PlayLayer::showNewBest` and `GJGameLevel::savePercentage` during protected attempts.

# 1.0.4
- Added an optional, persistent Safe Mode with conservative lookahead and a 240 Hz virtual simulation cap.
- Safe Mode limits Live Bot automation to standard cube and robot forms; other movement modes are left to manual control.
- Added a visible SAFE HUD prefix and an emergency stop that releases bot inputs and switches the bot off.

# 1.0.3
- Added per-frame predictive planning with separate jump and no-input trajectory rollouts.
- Searches delayed jump timings and keeps the safest latest viable timing instead of always jumping immediately.
- Adds adaptive virtual collision-sampling resolution (up to 960 samples per rendered frame) near difficult hazards; this does not change the game's actual FPS.
- Clears pending plans when the player leaves the ground, enters cooldown, or the threat is no longer relevant.

# 1.0.2
- Added per-level adaptive jump timing based on recent failed attempts.
- Learning is saved separately for each level and player, with bounded adjustments to avoid unstable behavior.

# 1.0.1
- Show a restart notice after the mod update has been downloaded.

# 1.0.0
- Initial release.
