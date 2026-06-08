// =============================================================================
// items/Treasure.h
//
// Purpose:
//   Treasure is a concrete Item whose only effect is to add its value to the
//   run's Score when collected (R19.5). It is consumable: once picked up and
//   scored, the gold is gone and the pickup leaves play (R20.3).
//
//   Treasure is the one Item subtype that changes NO Player stat. Per the
//   scoring rule (R24.1), the Score is computed as
//       wave * 100 + kills * 10 + treasureValue * 1
//   so a treasure's worth flows into the *Score*, which is owned by the systems
//   layer (GameState / ScoreBoard), not into the Player. Because Item::applyTo
//   only receives a Player&, it has nothing meaningful to do here: the pickup
//   path in the systems layer reads value() and adds it to the Score itself.
//   applyTo() is therefore a deliberate, documented no-op (see the .cpp), and
//   value() is the accessor the scoring path consumes.
//
//   It is still one of the five Item subtypes demonstrating inheritance +
//   polymorphism (R4.3): it overrides applyTo()/glyph() like its siblings so the
//   collection loop can treat every pickup uniformly through an Item base
//   reference (R19.6).
//
// Layer: items (depends on items/Item and, in the .cpp, entities/Player).
// =============================================================================
#pragma once

#include "items/Item.h" // Item - the abstract base this overrides.

namespace dga {

class Player; // Forward decl: applyTo() takes a Player& (resolved in the .cpp).

/// A consumable treasure pickup whose value is added to the Score (R19.5).
class Treasure : public Item {
public:
    /// Construct a treasure at a grid position with its score value.
    /// @param position the grid cell the treasure lies on until picked up.
    /// @param value    how many points this treasure contributes to the Score
    ///        when collected (weighted by Config::scoreWeightTreasure, R24.1).
    Treasure(const Vec2& position, int value);

    /// No-op on the Player by design (R19.5): treasure adds to the Score, not to
    /// any Player stat, and the Score lives in the systems layer. The pickup
    /// path reads value() and credits the Score directly; this override exists
    /// only so Treasure satisfies the Item interface and can be applied through
    /// an Item base reference like every other pickup (R19.6).
    /// @param player the collecting hero; intentionally left unchanged.
    void applyTo(Player& player) override;

    /// @return true: treasure is removed once collected and scored (R20.3).
    bool isConsumable() const override;

    /// @return '$', the ASCII icon for treasure/gold.
    char glyph() const override;

    /// @return the score value this treasure contributes when collected (R24.1).
    int value() const;

private:
    int value_; ///< Points this treasure adds to the Score on pickup (R19.5).
};

} // namespace dga
