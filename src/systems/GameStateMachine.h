// =============================================================================
// systems/GameStateMachine.h
//
// Purpose:
//   GameStateMachine manages the high-level game states (R27, R30). Exactly
//   one GameStateId is active at a time; the machine enforces which transitions
//   are legal and silently logs (or ignores) illegal ones.
//
//   States (from core/Enums.h):
//     * MainMenu     — title screen: start, load, leaderboard, quit.
//     * Playing      — active turn-based gameplay.
//     * Paused       — turn loop halted; pause menu shown (R27.1).
//     * UpgradeDraft — between-wave card selection (R23).
//     * GameOver     — run ended; record score, show leaderboard (R25).
//
//   Legal transitions (R30.1-R30.4, R27.1-R27.4):
//     MainMenu     → Playing      (start a new run)
//     Playing      → Paused       (open pause menu, R27.1)
//     Paused       → Playing      (resume from pause)
//     Playing      → UpgradeDraft (wave cleared, R23.1)
//     UpgradeDraft → Playing      (upgrade chosen, R23.3)
//     Playing      → GameOver     (player died, R10.6)
//     GameOver     → MainMenu     (return to title screen)
//     Any          → MainMenu     (hard quit from anywhere)
//
//   Illegal transitions: an attempt to move to a state that does not appear in
//   the table above for the current state is silently ignored (or optionally
//   logged). The machine's state is left unchanged.
//
// Why a .h/.cpp split:
//   GameStateMachine contains real logic (the transition table); declarations
//   live here and definitions in GameStateMachine.cpp (R2.1).
//
// Layer: systems (depends on core/Enums.h and systems/EventLog).
// =============================================================================
#pragma once

#include "core/Enums.h" // GameStateId - the state enum.

namespace dga {

class EventLog; // Forward decl: used to log illegal transitions optionally.

/// Manages legal transitions among the five high-level game states (R27, R30).
///
/// The machine holds a single GameStateId and validates every requested
/// transition against a fixed table of legal moves before applying it.
class GameStateMachine {
public:
    /// Construct the machine in the MainMenu state (the application starts there).
    /// @param log the EventLog used to record illegal transition attempts.
    ///            A null pointer disables transition logging (transitions are
    ///            silently ignored when illegal and log is null).
    explicit GameStateMachine(EventLog* log = nullptr);

    // ---- State transitions -------------------------------------------------

    /// Attempt to transition to `nextState`.
    ///
    /// The transition is applied if and only if it appears in the legal-
    /// transitions table (see the file header). When the transition is legal
    /// the stored state is updated to `nextState`. When the transition is
    /// illegal the state is unchanged; if an EventLog was supplied at
    /// construction time the illegal attempt is logged for debugging.
    ///
    /// Any→MainMenu is always legal (hard quit / reset), so calling
    /// transition(GameStateId::MainMenu) from any state is always accepted.
    ///
    /// @param nextState the state to try to move to.
    void transition(GameStateId nextState);

    // ---- Queries -----------------------------------------------------------

    /// @return the current high-level game state.
    GameStateId currentState() const;

    /// @return true when the machine is in the Playing state.
    bool isPlaying() const;

    /// @return true when the machine is in the Paused state.
    bool isPaused() const;

    /// @return true when the machine is in the UpgradeDraft state.
    bool isUpgradeDraft() const;

    /// @return true when the machine is in the GameOver state.
    bool isGameOver() const;

    /// @return true when the machine is in the MainMenu state.
    bool isMainMenu() const;

private:
    /// Check whether the transition from the current state to `next` is legal.
    /// @param next the target state.
    /// @return true when the transition is in the allowed-transitions table.
    bool isLegalTransition(GameStateId next) const;

    GameStateId current_; ///< The active game state.
    EventLog*   log_;     ///< Optional event log for illegal-transition messages.
};

} // namespace dga
