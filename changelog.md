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
