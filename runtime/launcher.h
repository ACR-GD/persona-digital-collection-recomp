// Launcher window (runtime/launcher.c).
#pragma once
enum { LAUNCH_QUIT = 0, LAUNCH_PLAY = 1 };
struct launch_paths {
    const char *data_dir, *overlay_dir, *save_dir, *cfg_path, *patch;
};
// Shows the launcher; returns LAUNCH_PLAY or LAUNCH_QUIT. Settings are written to cfg_path and
// exported as PERSONA_* variables on Play.
int launcher_run(const struct launch_paths *lp);
