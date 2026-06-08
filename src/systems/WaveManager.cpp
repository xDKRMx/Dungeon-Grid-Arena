// =============================================================================
// systems/WaveManager.cpp
//
// Purpose:
//   Definitions for the WaveManager class declared in systems/WaveManager.h.
//   Implements wave start (map generation + enemy spawning with boss cadence),
//   wave clearance detection, and wave advancement (R17, R18).
//
// Layer: systems (depends on entities, world, core/Config).
// =============================================================================
#include "systems/WaveManager.h"

#include <algorithm> // std::min - cap minion/spawn count to available tiles.
#include <memory>    // std::make_unique - factory for enemy concrete types.
#include <vector>    // std::vector - spawn tile list.

#include "core/Config.h"        // Config - enemy stats, map dimensions.
#include "core/Rng.h"           // Rng - deterministic randomness for items.
#include "entities/Enemy.h"     // BossEnemy, MeleeEnemy, RookEnemy, BishopEnemy,
                                //   QueenEnemy, FastEnemy - concrete enemy types.
#include "items/AmmoItem.h"     // AmmoItem - concrete ammo pickup.
#include "items/Armor.h"        // Armor - concrete armor pickup.
#include "items/HealthPotion.h" // HealthPotion - concrete potion pickup.
#include "items/Item.h"         // Item - polymorphic base for floor items.
#include "items/Treasure.h"     // Treasure - concrete score pickup.
#include "items/Weapon.h"       // Weapon - concrete weapon pickup.
#include "systems/GameState.h"  // GameState - map, enemies, waveNumber.
#include "world/GridMap.h"      // GridMap - cleared and repopulated each wave.
#include "world/MapGenerator.h" // MapGenerator::generate, pickSpawns.

namespace dga {

namespace {

// ---- Wave-scaling constants ------------------------------------------------
// These control how many enemies spawn and which types appear as the run
// progresses. Centralizing them here avoids magic numbers in the function bodies
// (R8.5) while keeping them private to this translation unit.

/// Base enemy count on wave 1 (before the scaling factor is applied).
constexpr int kBaseEnemyCount = 3;

/// Additional enemies added per wave number (rounded down).
/// Wave 1 → 3+0=3; Wave 2 → 3+1=4; Wave 5 → 3+2=5; Wave 10 → 3+5=8, etc.
constexpr int kEnemiesPerWaveGrowth = 1;

/// Wave index at which Rook enemies start to appear in the mix. Rooks are the
/// most basic ranged attacker (fire along rows/columns), so they show up from
/// the very first wave to guarantee the player meets a ranged threat early and
/// the ranged-attack beam effect is visible in normal play.
constexpr int kRookAppearWave = 1;

/// Wave index at which Bishop enemies (diagonal fire) join the mix.
constexpr int kBishopAppearWave = 2;

/// Wave index at which Queen enemies (row/column/diagonal fire) join the mix.
constexpr int kQueenAppearWave = 3;

/// Wave index at which Fast enemies start to appear in the mix.
constexpr int kFastAppearWave = 2;

/// From this wave onward at least one ranged enemy is GUARANTEED to spawn: the
/// first spawn slot is forced to a Rook so the player always faces a distance
/// attacker (and can see the ranged beam cue) from this wave on. Deterministic
/// because it is a fixed slot, not a random draw.
constexpr int kGuaranteedRangedWave = 2;

/// Relative weight given to each MELEE-family enemy entry in the spawn pool.
/// Melee stays common early but, paired with the ranged weights below, never
/// dominates the pool so heavily that ranged enemies rarely appear (the old
/// table effectively spawned ~90% melee on early waves).
constexpr int kMeleePoolWeight = 2;

/// Relative weight given to each RANGED enemy entry (Rook/Bishop/Queen) once it
/// is eligible. Two entries per eligible ranged type ensures a healthy mix of
/// distance attackers from wave 2 onward rather than the occasional straggler.
constexpr int kRangedPoolWeight = 2;

/// Modulus used to determine boss waves (R18.1).
constexpr int kBossWaveModulus = 5;

// ---- Item spawning constants ----------------------------------------------
// Each wave drops a small randomised batch of pickups onto FREE floor tiles so
// the dungeon does not feel empty between fights (R19, R20). The numbers below
// are the only tunables; everything else (which kinds appear, the weighted
// distribution, the per-pickup magnitudes) is derived from them and from
// Config so balancing stays in one place (R8.5).

/// Minimum number of items dropped per wave.
constexpr int kMinItemsPerWave = 4;

/// Maximum number of items dropped per wave.
constexpr int kMaxItemsPerWave = 8;

/// Weighted probability table used to pick which kind of item to drop. The
/// weights are summed and a uniformly distributed value in [0, total) selects
/// a kind. Higher weights => more frequent pickups; weapons are deliberately
/// rare so they feel like a real upgrade when they appear.
constexpr int kWeightHealthPotion = 30;
constexpr int kWeightAmmo         = 30;
constexpr int kWeightArmor        = 15;
constexpr int kWeightTreasure     = 20;
constexpr int kWeightWeapon       = 5;

/// Total of every weight above; precomputed so the rangeInt call below stays
/// a single named expression rather than a literal sum.
constexpr int kItemWeightTotal = kWeightHealthPotion + kWeightAmmo +
                                 kWeightArmor + kWeightTreasure +
                                 kWeightWeapon;

/// Rounds granted by one AmmoItem pickup (R19.3).
constexpr int kAmmoPickupAmount     = 5;

/// Damage reduction added by one Armor pickup (R19.4). Raised to 3 so each
/// armor pickup gives meaningful protection as a shield-buffer (3 points of
/// damage absorption). Finding 2-3 armor items gives 6-9 buffer HP of shield.
constexpr int kArmorPickupBonus     = 3;

/// Lower bound on a Treasure pickup's score value (inclusive, R19.5).
constexpr int kTreasureMinValue     = 5;

/// Upper bound on a Treasure pickup's score value (inclusive, R19.5).
constexpr int kTreasureMaxValue     = 25;

/// Lower bound on a Weapon pickup's attack value (inclusive, R19.2).
/// For MELEE weapons; ranged weapons use a higher range to feel powerful.
///
/// Tuning history (Bug 2 rebalance): originally 2, which was BELOW the hero's
/// starting attack of 14 (Config::playerStartingAttack), so a freshly rolled
/// melee weapon was a downgrade. Raised to 16 so every melee weapon roll is a
/// strict upgrade over base attack and the pickup feels rewarding. Weapon's
/// applyTo also carries a max-guard as defence-in-depth.
constexpr int kWeaponMinAttack      = 16;

/// Upper bound on a Weapon pickup's attack value (inclusive, R19.2).
/// For MELEE weapons; ranged weapons use a higher range to feel powerful.
///
/// Tuning history (Bug 2 rebalance): raised from 5 to 22 to align with the new
/// kWeaponMinAttack = 16; together they keep melee weapon rolls in a clear
/// "real upgrade over base attack" band without overshooting ranged weapons.
constexpr int kWeaponMaxAttack      = 22;

/// Lower bound on a RANGED Weapon's attack value. Set higher than base melee
/// weapons so equipping a ranged weapon (with spread shot) feels like a clear
/// power spike that justifies the ammo cost.
constexpr int kRangedWeaponMinAttack = 16;

/// Upper bound on a RANGED Weapon's attack value.
///
/// Tuning history (Bug 2 rebalance): raised from 20 to 24 so ranged weapons
/// keep their "best-in-class damage" identity now that melee weapons roll up
/// to 22 (see kWeaponMaxAttack above). At 24 the main beam still reliably
/// one-shots most standard enemies and the spread shot remains devastating.
constexpr int kRangedWeaponMaxAttack = 24;

/// Probability (out of 100) that a freshly spawned weapon is RANGED. Melee
/// weapons are the default; ranged weapons consume ammo to fire (R16, OD-3).
constexpr int kWeaponRangedPercent  = 50;

/// Compute the total number of non-boss enemies to spawn on a given wave.
/// @param waveNumber the current 1-based wave index.
/// @return the number of enemies to spawn (always >= kBaseEnemyCount).
int enemyCountForWave(int waveNumber) {
    // Divide by 2 so growth is gentler; floor((waveNumber-1)/2) extra enemies.
    const int extra = (waveNumber - 1) / 2;
    return kBaseEnemyCount + extra + (waveNumber - 1) * kEnemiesPerWaveGrowth / 4;
}

// ---- Per-wave enemy HP scaling (Feature: balance vs upgrade draft) ---------
//
// Every spawned enemy gets a flat HP bonus added to both its maximum and its
// current Health by Entity::boostMaxHealth. The bonus grows in steps every
// ~3 waves so the early-game stays gentle and the mid/late game keeps up with
// the player's stacking damage upgrades (Sharpened Blade, Extra Damage, etc.).

/// Number of waves between successive HP-bonus tiers. Every kHpBonusStep
/// waves the per-enemy HP bonus grows by one step. Set to 3 so the player
/// feels the difficulty bump roughly once every other upgrade draft.
constexpr int kHpBonusStep = 3;

/// HP added to every normal enemy per tier reached. Wave 1-2 gets +0,
/// 3-5 gets +6, 6-8 gets +12, 9-11 gets +18, ... and so on. A small step
/// keeps the early game readable while still scaling visibly into late waves.
constexpr int kHpBonusPerTierNormal = 6;

/// HP added to a boss per tier reached. Bosses scale faster than normal
/// enemies because they are alone on their wave and the player has been
/// accumulating upgrades for ~5 waves by the time they appear.
constexpr int kHpBonusPerTierBoss = 24;

/// Compute the per-enemy HP bonus for a given wave number. Tiers up every
/// kHpBonusStep waves; bosses use a steeper per-tier increment than normal
/// enemies because they have to survive the player's full kit.
/// @param waveNumber the current 1-based wave index.
/// @param isBoss     true when computing the bonus for the wave's boss
///                   enemy, false for a regular per-spawn enemy.
/// @return the additive HP bonus (always >= 0).
int computeEnemyHpBonus(int waveNumber, bool isBoss) {
    if (waveNumber <= 0) { return 0; }
    // Floor((wave - 1) / step) tiers — wave 1 is tier 0 (no bonus), wave 4
    // is tier 1 (+stepBonus), and so on.
    const int tier = (waveNumber - 1) / kHpBonusStep;
    const int perTier = isBoss ? kHpBonusPerTierBoss
                                : kHpBonusPerTierNormal;
    return tier * perTier;
}

/// Choose a concrete enemy kind for the given spawn index and wave number.
/// The mix shifts toward stronger types as the wave number grows (R17.3).
///
/// Two guarantees make ranged enemies actually visible in play (Fix 4):
///   1. From kGuaranteedRangedWave onward the FIRST spawn slot (index 0) is
///      forced to a Rook, so every such wave has at least one distance attacker
///      regardless of the random draw.
///   2. For all other slots the pool is built with balanced weights — melee
///      stays common but ranged types each contribute multiple entries — so the
///      mix is no longer overwhelmingly melee once ranged types are eligible.
///
/// @param index      which spawn slot this is (0-based).
/// @param waveNumber the current wave number driving the mix table.
/// @param rng        deterministic randomness for the weighted pick.
/// @return the EntityKind to spawn for this slot.
EntityKind pickEnemyKind(int index, int waveNumber, Rng& rng) {
    // Guarantee #1: force the first spawn slot to a Rook from the configured
    // wave on. This is deterministic (a fixed slot, no rng draw) so a given
    // seed still reproduces the same wave composition (R26.4).
    if (index == 0 && waveNumber >= kGuaranteedRangedWave) {
        return EntityKind::RookEnemy;
    }

    // Build a weighted candidate pool. Melee is always available; ranged and
    // fast types are added once the wave reaches their appearance threshold.
    // Each push_back is one "ticket"; pushing a kind multiple times raises its
    // probability without a separate weight table (R8.5: weights are named).
    std::vector<EntityKind> pool;

    for (int w = 0; w < kMeleePoolWeight; ++w) {
        pool.push_back(EntityKind::MeleeEnemy);
    }

    if (waveNumber >= kFastAppearWave) {
        // Fast enemies are melee-family pressure; one entry keeps them present
        // without crowding out the ranged types this fix is meant to surface.
        pool.push_back(EntityKind::FastEnemy);
    }
    if (waveNumber >= kRookAppearWave) {
        for (int w = 0; w < kRangedPoolWeight; ++w) {
            pool.push_back(EntityKind::RookEnemy);
        }
    }
    if (waveNumber >= kBishopAppearWave) {
        for (int w = 0; w < kRangedPoolWeight; ++w) {
            pool.push_back(EntityKind::BishopEnemy);
        }
    }
    if (waveNumber >= kQueenAppearWave) {
        for (int w = 0; w < kRangedPoolWeight; ++w) {
            pool.push_back(EntityKind::QueenEnemy);
        }
    }

    // Pick uniformly from the weighted pool using the shared deterministic Rng
    // (R26.4). Ticket multiplicity above provides the per-type weighting.
    return rng.choice(pool);
}

/// Factory: construct the appropriate concrete enemy at the given position.
/// @param kind     the enemy type to create.
/// @param position the floor tile to spawn it on.
/// @param config   balancing source for the enemy's stats.
/// @return an owning pointer to the new enemy.
std::unique_ptr<Enemy> makeEnemy(EntityKind kind,
                                  const Vec2& position,
                                  const Config& config) {
    switch (kind) {
        case EntityKind::MeleeEnemy:
            return std::make_unique<MeleeEnemy>(position, config);
        case EntityKind::RookEnemy:
            return std::make_unique<RookEnemy>(position, config);
        case EntityKind::BishopEnemy:
            return std::make_unique<BishopEnemy>(position, config);
        case EntityKind::QueenEnemy:
            return std::make_unique<QueenEnemy>(position, config);
        case EntityKind::FastEnemy:
            return std::make_unique<FastEnemy>(position, config);
        case EntityKind::BossEnemy:
            return std::make_unique<BossEnemy>(position, config);
        default:
            return std::make_unique<MeleeEnemy>(position, config);
    }
}

// ---- Item spawn helpers ----------------------------------------------------

/// Pick one ItemKind via a weighted draw from the table at the top of this
/// file. Centralising the weights keeps the distribution readable (one column)
/// and lets the dispatch in pickRandomItemKind be a flat if/else cascade.
/// @param rng deterministic randomness source (the run's shared Rng, R26.4).
/// @return a randomly chosen ItemKind.
ItemKind pickRandomItemKind(Rng& rng) {
    // Roll a value in [0, kItemWeightTotal) and walk the weights in order; the
    // first weight whose cumulative sum exceeds the roll wins. This is the
    // textbook weighted-pick by cumulative thresholds.
    int roll = rng.rangeInt(0, kItemWeightTotal - 1);

    if (roll < kWeightHealthPotion)                          { return ItemKind::HealthPotion; }
    roll -= kWeightHealthPotion;
    if (roll < kWeightAmmo)                                  { return ItemKind::AmmoItem; }
    roll -= kWeightAmmo;
    if (roll < kWeightArmor)                                 { return ItemKind::Armor; }
    roll -= kWeightArmor;
    if (roll < kWeightTreasure)                              { return ItemKind::Treasure; }
    // Fallthrough bucket is the rarest pick: a Weapon.
    return ItemKind::Weapon;
}

/// Build one concrete Item of the requested kind on the given tile, drawing any
/// per-kind randomness (treasure value, weapon attack, ranged-or-not) from the
/// shared deterministic Rng so a fixed seed always reproduces the same drops.
/// @param kind       which concrete pickup to construct (R19.1-R19.5).
/// @param position   the floor tile the item lies on until collected.
/// @param rng        randomness source for per-kind value rolls.
/// @param config     balancing source; supplies the ranged-weapon fire-range
///                   bonus so a ranged Weapon pickup also extends reach
///                   (Feature 1) without hardcoding the bonus here (R8.5).
/// @return an owning pointer to the new Item, ready to be added to GameState.
std::unique_ptr<Item> makeItem(ItemKind kind, const Vec2& position, Rng& rng,
                               const Config& config) {
    switch (kind) {
        case ItemKind::HealthPotion:
            // Potions always heal "to full" via Player::heal clamping (R19.1),
            // so they carry no per-pickup magnitude of their own.
            return std::make_unique<HealthPotion>(position);

        case ItemKind::AmmoItem:
            // Fixed per-pickup amount keeps ranged play predictable; the value
            // is one named constant rather than scattered literals (R8.5).
            return std::make_unique<AmmoItem>(position, kAmmoPickupAmount);

        case ItemKind::Armor:
            // Small steady bonus per pickup; layered armor over a long run is
            // rewarded by a steady damage-reduction climb (R19.4).
            return std::make_unique<Armor>(position, kArmorPickupBonus);

        case ItemKind::Treasure: {
            // Random gold value in the configured range (R19.5).
            const int value = rng.rangeInt(kTreasureMinValue, kTreasureMaxValue);
            return std::make_unique<Treasure>(position, value);
        }

        case ItemKind::Weapon: {
            // Random attack value plus a coin flip for ranged-or-not so the
            // player encounters both melee and ranged upgrades over a run. A
            // ranged weapon gets a HIGHER attack range (kRangedWeaponMinAttack
            // to kRangedWeaponMaxAttack) so it feels like a real power spike
            // with spread shot; a melee weapon uses the base range. A ranged
            // weapon also carries a fire-range bonus (Feature 1) so picking it
            // up extends the hero's reach; a melee weapon carries a 0 bonus.
            const bool ranged = rng.rangeInt(0, 99) < kWeaponRangedPercent;
            const int  attack = ranged
                ? rng.rangeInt(kRangedWeaponMinAttack, kRangedWeaponMaxAttack)
                : rng.rangeInt(kWeaponMinAttack, kWeaponMaxAttack);
            const int  rangeBonus =
                ranged ? config.fireRangeUpgradeBonus() : 0;
            return std::make_unique<Weapon>(position, attack, ranged, rangeBonus);
        }
    }
    // Defensive: unknown kind falls back to a potion so the pickup is at least
    // useful and the code is never silently broken by a future enum addition.
    return std::make_unique<HealthPotion>(position);
}

/// Drop a randomised batch of items on FREE floor tiles for the current wave.
///
/// The procedure is:
///   1. Compute a target item count in [kMinItemsPerWave, kMaxItemsPerWave].
///   2. Gather every Floor tile from the GridMap (R9 / R20.2) and drop the ones
///      currently occupied by the player or any spawned enemy so we never stack
///      a pickup under a creature.
///   3. Fisher-Yates partial shuffle the front of the candidate list to obtain
///      that many DISTINCT random tiles in O(n) without pulling extras.
///   4. For each chosen tile, weighted-pick an ItemKind, build the concrete
///      Item, and hand ownership to the GameState via addItem.
///
/// Determinism: every random draw uses the supplied Rng so a fixed seed always
/// produces the same map + enemy + item layout, preserving the save/load
/// reproducibility guarantee (R26.4).
///
/// @param state  the authoritative game state to drop items into.
/// @param rng    the shared deterministic Rng (R26.4).
/// @param config balancing source forwarded to makeItem (ranged-weapon fire
///               range bonus, Feature 1).
void spawnWaveItems(GameState& state, Rng& rng, const Config& config) {
    // ---- Target count ------------------------------------------------------
    const int targetCount = rng.rangeInt(kMinItemsPerWave, kMaxItemsPerWave);
    if (targetCount <= 0) { return; }

    // ---- Build the candidate tile list ------------------------------------
    // floorTiles() returns every walkable cell (R9.4). We then drop any tile
    // currently occupied by the player or an enemy so two creatures never
    // share a cell with a pickup.
    std::vector<Vec2> candidates = state.map().floorTiles();
    if (candidates.empty()) { return; }

    const Vec2 playerPos = state.player().position();

    // Collect enemy positions once instead of scanning the enemies vector
    // for every candidate; cheaper and keeps the filter loop tight.
    std::vector<Vec2> enemyTiles;
    enemyTiles.reserve(state.enemies().size());
    for (const auto& enemyPtr : state.enemies()) {
        if (enemyPtr) { enemyTiles.push_back(enemyPtr->position()); }
    }

    // Single in-place filter pass: keep only tiles that are NOT the player and
    // NOT any enemy. We swap-and-pop so the surviving tiles compact at the
    // front and stay in the same memory; no extra allocation needed.
    std::size_t kept = 0;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const Vec2& tile = candidates[i];
        if (tile == playerPos) { continue; }

        bool occupied = false;
        for (const Vec2& enemyTile : enemyTiles) {
            if (tile == enemyTile) { occupied = true; break; }
        }
        if (occupied) { continue; }

        if (kept != i) { candidates[kept] = tile; }
        ++kept;
    }
    candidates.resize(kept);

    if (candidates.empty()) { return; }

    // ---- Pick distinct tiles via partial Fisher-Yates ---------------------
    // Classic partial shuffle: at each step swap the i-th element with a
    // random later element, then keep the i-th element. After `picks` swaps
    // the front `picks` slots hold a uniformly chosen sample without repeats.
    const int picks = std::min(targetCount, static_cast<int>(candidates.size()));
    for (int i = 0; i < picks; ++i) {
        const int last = static_cast<int>(candidates.size()) - 1;
        const int swapWith = rng.rangeInt(i, last);
        if (swapWith != i) {
            std::swap(candidates[static_cast<std::size_t>(i)],
                      candidates[static_cast<std::size_t>(swapWith)]);
        }
    }

    // ---- Materialise the items -------------------------------------------
    for (int i = 0; i < picks; ++i) {
        const Vec2& tile = candidates[static_cast<std::size_t>(i)];
        const ItemKind kind = pickRandomItemKind(rng);
        state.addItem(makeItem(kind, tile, rng, config));
    }
}

} // anonymous namespace

// =============================================================================
// startWave — generate map and spawn enemies (R17.1, R17.3, R18.1)
// =============================================================================

void WaveManager::startWave(GameState& state,
                            const Config& config,
                            int enemySpawnCount) const {
    const int waveNumber = state.waveNumber();

    // ---- Clear the existing enemy and item lists before the new wave. ------
    state.enemies().clear();
    state.items().clear();

    // ---- Generate a fresh map for this wave (R9). --------------------------
    MapGenerator generator;
    generator.generate(state.map(), state.rng(), enemySpawnCount);

    // ---- Choose spawn tiles (R9.4, R9.6). ----------------------------------
    SpawnPlan plan =
        generator.pickSpawns(state.map(), state.rng(), enemySpawnCount);

    // Place the player on the chosen start tile.
    state.player().setPosition(plan.playerStart);

    // ---- Boss wave (R18.1): one BossEnemy, rest minions or none. -----------
    if (waveNumber % kBossWaveModulus == 0) {
        // Spawn the boss on the first enemy spawn tile.
        if (!plan.enemySpawns.empty()) {
            auto bossPtr = std::make_unique<BossEnemy>(plan.enemySpawns[0], config);
            // Boss waves get a bigger HP scaling than normal enemies because
            // the player is also stronger by then (mid-run upgrades stack).
            const int bossBonus = computeEnemyHpBonus(waveNumber, /*isBoss=*/true);
            if (bossBonus > 0) {
                bossPtr->boostMaxHealth(bossBonus);
            }
            state.addEnemy(std::move(bossPtr));
            // Remaining spawn tiles can be empty (boss wave is just the boss).
        }
        // Drop a randomised batch of pickups onto free floor tiles so the boss
        // arena still rewards exploration (R19, R20).
        spawnWaveItems(state, state.rng(), config);
        return;
    }

    // ---- Normal wave: mixed enemy types scaled to wave number (R17.3). -----
    const int spawnCount =
        std::min(static_cast<int>(plan.enemySpawns.size()), enemySpawnCount);

    // Wave-scaled HP bonus applied to every spawned enemy (Feature: balance
    // the player's mid-run damage upgrades). Computed once per wave because
    // every enemy on a given wave gets the same boost.
    const int normalHpBonus =
        computeEnemyHpBonus(waveNumber, /*isBoss=*/false);

    for (int idx = 0; idx < spawnCount; ++idx) {
        const EntityKind kind =
            pickEnemyKind(idx, waveNumber, state.rng());
        auto enemyPtr =
            makeEnemy(kind, plan.enemySpawns[static_cast<std::size_t>(idx)], config);
        if (normalHpBonus > 0) {
            enemyPtr->boostMaxHealth(normalHpBonus);
        }
        state.addEnemy(std::move(enemyPtr));
    }

    // ---- Floor pickups (R19, R20) -----------------------------------------
    // After enemies are placed we sprinkle a randomised batch of items onto
    // the remaining free floor tiles. spawnWaveItems excludes the player tile
    // and every enemy tile so a pickup never starts the wave underneath a
    // creature (R20.2). Determinism comes from the shared run Rng (R26.4).
    spawnWaveItems(state, state.rng(), config);
}

// =============================================================================
// isWaveCleared — all enemies defeated (R17.2, R18.5)
// =============================================================================

bool WaveManager::isWaveCleared(const GameState& state) const {
    return state.enemies().empty();
}

// =============================================================================
// advance — increment wave number and start the next wave (R17.2, R17.4)
// =============================================================================

void WaveManager::advance(GameState& state, const Config& config) const {
    // Increment the wave number first so startWave reads the new value (R17.4).
    const int nextWave = state.waveNumber() + 1;
    state.setWaveNumber(nextWave);

    // Compute the spawn count for the next wave and launch it.
    const int spawnCount = enemyCountForWave(nextWave);
    startWave(state, config, spawnCount);
}

} // namespace dga
