// =============================================================================
// core/Rng.h
//
// Purpose:
//   Rng is the game's single source of randomness. It wraps a standard
//   Mersenne Twister engine (std::mt19937) together with the unsigned seed it
//   was started from, and exposes a few small, purpose-built helpers:
//   pick an integer in a range, and pick a random element of a vector.
//
//   Doing ALL randomness through one seeded object is what makes the game
//   *deterministic*: given the same seed, the engine produces the exact same
//   sequence of numbers every time. That single guarantee is what lets us
//
//     - write reproducible tests (a fixed seed always builds the same map and
//       the same enemy spawns, so a test can assert on concrete results), and
//     - save and restore a run faithfully (R26.4): we store the seed in the
//       save file, re-seed on load, and the run continues identically.
//
//   If different parts of the game called std::rand() or created their own
//   engines, none of that would hold. Routing everything through one Rng keeps
//   the behavior predictable and easy to reason about.
//
// Why split into .h / .cpp:
//   Rng owns real logic (it builds distributions and produces values), so the
//   declarations live here and the definitions live in Rng.cpp, per the
//   project's multi-file rule. The one exception is the `choice` *template*,
//   which must stay in this header because the compiler needs to see its body
//   to instantiate it for each element type it is used with.
//
// Layer: core (depends only on the C++ standard library).
// =============================================================================
#pragma once

#include <cassert> // assert - guards choice() against an empty input.
#include <random>  // std::mt19937, std::uniform_int_distribution.
#include <vector>  // std::vector - the container choice() selects from.

namespace dga {

/// A deterministic, seedable pseudo-random number generator.
///
/// "Deterministic" means the output depends only on the seed: same seed in,
/// same sequence out. The stored seed can be read back with seed() so it can
/// be written to a save file and reapplied later to reproduce the run exactly.
class Rng {
public:
    /// Construct the generator from an explicit seed.
    /// @param seedValue the value used to seed the underlying engine; it is
    ///        also remembered so it can be read back via seed().
    /// The constructor is `explicit` so an integer is never silently converted
    /// into an Rng by accident.
    explicit Rng(unsigned int seedValue);

    /// Re-seed the generator deterministically, restarting its sequence.
    /// After this call the engine behaves exactly as a freshly constructed
    /// Rng(newSeed) would, and seed() will return newSeed. This is used on
    /// save/load to restore the saved randomness (R26.4).
    /// @param newSeed the new seed to store and apply to the engine.
    void reseed(unsigned int newSeed);

    /// Read back the seed the generator is currently running from.
    /// @return the seed last passed to the constructor or reseed().
    /// Needed by save/load so the exact randomness can be persisted (R26.4).
    unsigned int seed() const;

    /// Return a uniformly distributed integer in the inclusive range
    /// [minInclusive, maxInclusive].
    /// @param minInclusive the smallest value that may be returned.
    /// @param maxInclusive the largest value that may be returned.
    /// @return a value v with minInclusive <= v <= maxInclusive.
    /// Precondition: minInclusive <= maxInclusive. The degenerate case where
    /// the two bounds are equal returns that single value. (See Rng.cpp for how
    /// a misused range is handled safely.)
    int rangeInt(int minInclusive, int maxInclusive);

    /// Pick one element of a non-empty vector with uniform probability.
    ///
    /// Implemented as a template so it works for any element type the game
    /// stores in a vector (upgrade cards, spawn points, enemy kinds, ...). The
    /// body lives in the header because templates are instantiated per type at
    /// the call site.
    ///
    /// @tparam T the element type held by the vector.
    /// @param items the vector to choose from; MUST NOT be empty.
    /// @return a const reference to the chosen element. The reference is valid
    ///         only while `items` itself is alive and unmodified.
    ///
    /// The non-empty precondition is checked with assert(): selecting from an
    /// empty collection is a programming error, and there is no sensible value
    /// to return by reference, so we fail loudly in debug builds rather than
    /// reading out of bounds. The single random choice reuses rangeInt() so all
    /// range logic lives in exactly one place.
    template <typename type>
    const type& choice(const std::vector<type>& items) {
        assert(!items.empty() && "Rng::choice can not be empty!");

        // Valid indices run from 0 to size() - 1; pick one uniformly.
        const int lastIndex = items.size() - 1;
        const int chosenIndex = rangeInt(0, lastIndex);
        return items[chosenIndex];
    }

private:
    /// The seed the engine was started from, kept so it can be read back and
    /// persisted (R26.4). Updated whenever the engine is (re)seeded.
    unsigned int seed_;

    /// The underlying pseudo-random engine. Mersenne Twister is a standard,
    /// well-distributed generator; seeding it identically reproduces its output.
    std::mt19937 engine_;
};

} // namespace dga
