// =============================================================================
// main.cpp
//
// Purpose:
//   Program entry point (R2.4). Contains ONLY wiring — no game-rule logic:
//     1. Parse the optional --seed <n> command-line flag.
//     2. Construct a Config with default balancing values.
//     3. Construct an IRenderer — RaylibRenderer when the build is configured
//        with -DDGA_WITH_RAYLIB and raylib is on the include / link path,
//        ConsoleRenderer otherwise. Either renderer is exposed only as an
//        IRenderer reference to Game (R8.2, R28.5).
//     4. Construct a Game with the Config, renderer, and seed.
//     5. Call game.run() to enter the main loop.
//     6. Clean shutdown (all destructors run normally on scope exit).
//
// Requirements: 2.4, 28.4, 28.5, 30.1, 30.3, 30.4
//
// Build notes:
//   Console-only build (no external dependencies):
//     g++ -std=c++17 -Wall -Wextra -I src <all .cpp files> -o game
//
//   Raylib build (links statically against libs/raylib/lib/libraylib.a):
//     g++ -std=c++17 -Wall -Wextra -DDGA_WITH_RAYLIB -I src
//         -I libs/raylib/include  <all .cpp files including RaylibRenderer.cpp>
//         -L libs/raylib/lib -lraylib -lopengl32 -lgdi32 -lwinmm -o game_raylib
// =============================================================================

#include <cstdlib>   // std::atoi — parse the --seed argument.
#include <ctime>     // std::time — fallback seed when --seed is not provided.
#include <iostream>  // std::cerr — only used here for startup error messages.
#include <string>    // std::string, std::to_string — CLI argument comparison.

#include "core/Config.h"              // Config  — default balancing constants.
#include "systems/Game.h"             // Game    — the top-level orchestrator.

// Renderer selection: pick the graphical RaylibRenderer when the build flag
// is set, otherwise fall back to the always-available ConsoleRenderer. Only
// one of these headers is pulled into the translation unit, so a build that
// does NOT define DGA_WITH_RAYLIB never sees a single raylib symbol.
#ifdef DGA_WITH_RAYLIB
#  include "render/RaylibRenderer.h" // RaylibRenderer — graphical window.
#else
#  include "render/ConsoleRenderer.h" // ConsoleRenderer — ASCII baseline.
#endif

// ---------------------------------------------------------------------------
// Anonymous-namespace constants — avoid magic numbers in main (R8.5).
// ---------------------------------------------------------------------------
namespace {

    /// Command-line flag that lets the player supply a reproducible RNG seed.
    /// Usage: ./game --seed 12345
    const std::string SEED_FLAG = "--seed";

    /// Seed value meaning "generate from the current time".
    constexpr unsigned int SEED_FROM_TIME = 0;

} // anonymous namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    // -------------------------------------------------------------------------
    // Step 1: Parse the --seed flag.
    //
    // The seed makes a run fully reproducible: the same seed always generates
    // the same map, spawns, and upgrade draws (R26.4). This is also invaluable
    // for testing. If the flag is absent, SEED_FROM_TIME tells Game to use
    // std::time() instead, giving a different run every session.
    // -------------------------------------------------------------------------
    unsigned int seed = SEED_FROM_TIME;

    for (int i = 1; i < argc - 1; ++i) {
        if (std::string(argv[i]) == SEED_FLAG) {
            // atoi returns 0 for non-numeric strings; 0 maps to "use time".
            seed = static_cast<unsigned int>(std::atoi(argv[i + 1]));
        }
    }

    // -------------------------------------------------------------------------
    // Step 2: Construct the Config with default balancing values.
    //
    // Config is the single source of truth for every tunable constant (R8.5).
    // The default constructor already populates all fields; nothing in main.cpp
    // knows the actual numbers.
    // -------------------------------------------------------------------------
    dga::Config config;

    // -------------------------------------------------------------------------
    // Step 3: Construct the renderer.
    //
    // The renderer is the ONLY place in the project that performs drawing or
    // reads keyboard input (R8.2). Game stores a reference to the chosen
    // IRenderer; the rest of the code is renderer-agnostic. Selecting between
    // graphical (raylib) and ASCII (console) is a single #ifdef here so the
    // game core never grows a branch on which renderer is active (R28.5).
    // -------------------------------------------------------------------------
#ifdef DGA_WITH_RAYLIB
    dga::RaylibRenderer renderer; // Opens a 1280x720 window.
#else
    dga::ConsoleRenderer renderer; // Uses the existing terminal.
#endif

    // -------------------------------------------------------------------------
    // Step 4 & 5: Construct Game and run.
    //
    // Game wires together all the logic systems and takes a non-owning reference
    // to the renderer. game.run() drives the full application lifecycle and only
    // returns when the player selects Quit from the main menu.
    // -------------------------------------------------------------------------
    dga::Game game(config, renderer, seed);
    game.run();

    // -------------------------------------------------------------------------
    // Step 6: Clean shutdown.
    //
    // All objects on the stack (game, renderer, config) are destroyed here by
    // their normal destructors. No explicit cleanup is required.
    // -------------------------------------------------------------------------
    return 0;
}
