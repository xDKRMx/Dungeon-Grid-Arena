// =============================================================================
// systems/GameStateMachine.cpp
//
// Purpose:
//   Definitions for the GameStateMachine class declared in
//   systems/GameStateMachine.h. Implements the legal-transition table and the
//   query helpers (R27, R30).
//
// Layer: systems (depends on core/Enums.h, systems/EventLog).
// =============================================================================
#include "systems/GameStateMachine.h"

#include "systems/EventLog.h" // EventLog - log illegal transition attempts.

namespace dga {

// Build the machine starting in the MainMenu state. The application begins at
// the title screen before any run starts (R30.1).
GameStateMachine::GameStateMachine(EventLog* log)
    : current_(GameStateId::MainMenu), log_(log) {}

// =============================================================================
// transition — validate and apply a state change
// =============================================================================

// Check the transition against the legal table first. Any→MainMenu is always
// legal (hard quit). All other transitions must appear in the table below.
void GameStateMachine::transition(GameStateId nextState) {
    // Any→MainMenu is always allowed (hard quit / reset, R30.1).
    if (nextState == GameStateId::MainMenu) {
        current_ = nextState;
        return;
    }

    // All other transitions are validated against the legal table.
    if (isLegalTransition(nextState)) {
        current_ = nextState;
    } else {
        // Illegal transition: log it (if an EventLog is available) and ignore.
        if (log_ != nullptr) {
            log_->append("GameStateMachine: illegal transition attempted — ignored.");
        }
    }
}

// =============================================================================
// isLegalTransition — the allowed-transitions table
// =============================================================================

// The table is encoded as a simple switch-in-switch. Each case in the outer
// switch covers the FROM state; the inner switch covers the TO state. Using an
// explicit table (rather than a bitmask or adjacency-list structure) keeps the
// code readable and trivially auditable against the design spec (R30).
bool GameStateMachine::isLegalTransition(GameStateId next) const {
    switch (current_) {
        // MainMenu → Playing (start a new run).
        case GameStateId::MainMenu:
            return (next == GameStateId::Playing);

        // Playing → Paused, UpgradeDraft, or GameOver.
        case GameStateId::Playing:
            return (next == GameStateId::Paused      ||
                    next == GameStateId::UpgradeDraft ||
                    next == GameStateId::GameOver);

        // Paused → Playing (resume from the pause menu, R27.3).
        case GameStateId::Paused:
            return (next == GameStateId::Playing);

        // UpgradeDraft → Playing (upgrade card chosen, R23.3).
        case GameStateId::UpgradeDraft:
            return (next == GameStateId::Playing);

        // GameOver → MainMenu (return to title screen after run ends).
        case GameStateId::GameOver:
            return (next == GameStateId::MainMenu);

        default:
            return false;
    }
}

// =============================================================================
// Query helpers
// =============================================================================

GameStateId GameStateMachine::currentState() const {
    return current_;
}

bool GameStateMachine::isPlaying() const {
    return current_ == GameStateId::Playing;
}

bool GameStateMachine::isPaused() const {
    return current_ == GameStateId::Paused;
}

bool GameStateMachine::isUpgradeDraft() const {
    return current_ == GameStateId::UpgradeDraft;
}

bool GameStateMachine::isGameOver() const {
    return current_ == GameStateId::GameOver;
}

bool GameStateMachine::isMainMenu() const {
    return current_ == GameStateId::MainMenu;
}

} // namespace dga
