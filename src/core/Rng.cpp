// =============================================================================
// core/Rng.cpp
//
// Purpose:
//   Implements the non-template members of Rng declared in Rng.h: construction,
//   re-seeding, reading the seed back, and producing an integer in a range.
//   (The `choice` template lives in the header because it must be instantiated
//   per element type at each call site.)
//
//   Every method keeps all randomness flowing through the single stored
//   std::mt19937 engine so the generator stays deterministic: the same seed
//   always reproduces the same sequence, which is what makes maps, spawns, and
//   drafts repeatable for tests and faithful for save/load (R26.4).
// =============================================================================
#include "core/Rng.h"

#include <algorithm> // std::swap - used to normalize an inverted range safely.

namespace dga {

// -----------------------------------------------------------------------------
// Construction.
//
// Store the seed and seed the engine with the same value. Delegating to
// reseed() keeps the "store seed then seed engine" logic in one place, so the
// constructor and reseed() can never drift apart.
// -----------------------------------------------------------------------------
Rng::Rng(unsigned int seedValue) : seed_(seedValue), engine_(seedValue) {}

// -----------------------------------------------------------------------------
// Re-seeding.
//
// Remember the new seed and reset the engine to it. After this call the engine
// produces exactly the sequence a freshly constructed Rng(newSeed) would, which
// is precisely what save/load needs to restore randomness (R26.4).
// -----------------------------------------------------------------------------
void Rng::reseed(unsigned int newSeed) {
    seed_ = newSeed;
    engine_.seed(newSeed);
}

// -----------------------------------------------------------------------------
// Seed accessor.
//
// A simple const getter so callers (notably the save system) can persist the
// exact seed the run is using.
// -----------------------------------------------------------------------------
unsigned int Rng::seed() const { return seed_; }

// -----------------------------------------------------------------------------
// Uniform integer in an inclusive range.
//
// std::uniform_int_distribution<int> already yields a uniform value across the
// closed interval [low, high], including the degenerate case low == high (which
// can only return that single value). We construct the distribution per call so
// each call can use its own bounds; this is the idiomatic, allocation-free use
// of the standard distributions.
//
// Defensive normalization: the documented precondition is
// minInclusive <= maxInclusive. If a caller accidentally passes them inverted,
// constructing the distribution with low > high is undefined behavior, so we
// swap them first. This turns a potential crash into a safe, well-defined
// result rather than silently corrupting the sequence.
// -----------------------------------------------------------------------------
int Rng::rangeInt(int minInclusive, int maxInclusive) {
    int low = minInclusive;
    int high = maxInclusive;
    if (low > high) {
        int temp = low;
        low = high;
        high = temp;
    }

    std::uniform_int_distribution<int> distribution(low, high);
    return distribution(engine_);
}

} // namespace dga
