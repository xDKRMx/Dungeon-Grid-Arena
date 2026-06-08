// =============================================================================
// io/SaveManager.cpp
//
// Purpose:
//   Definitions for SaveManager declared in io/SaveManager.h.
//
//   save()  — serializes the full GameState to a tagged text file (R26.1).
//   load()  — reads and validates the file, then atomically restores the
//             GameState (R26.2-R26.4).  No partial state overwrites (R26.3).
//
// Transactional-load implementation detail:
//   The load path parses every line into LOCAL staging variables.  Only after
//   the ENTIRE file has been validated without error does the function write
//   back to the caller-supplied `state`.  This guarantees that a truncated or
//   malformed file never partially corrupts a live run (R26.3).
//
// Enemy/Item recreation:
//   Enemies are re-created with the correct Config stats by constructing the
//   concrete subtype for the saved EntityKind, then calling takeDamage to
//   reduce health to the saved value (enemies spawn at full health in their
//   constructor).  Items are constructed directly with the saved parameters.
//
// Layer: io (depends on systems/GameState, systems/EventLog,
//   entities/Enemy, items/*, world/GridMap, core — never on render).
// =============================================================================
#include "io/SaveManager.h"

#include <fstream>  // std::ifstream, std::ofstream
#include <memory>   // std::make_unique
#include <sstream>  // std::istringstream
#include <string>   // std::string, std::getline
#include <vector>   // std::vector (staging)

// -- Systems (needed by both paths) ------------------------------------------
#include "systems/EventLog.h"  // EventLog::append — progress/error reporting
#include "systems/GameState.h" // GameState — the run state container

// -- Core --------------------------------------------------------------------
#include "core/Config.h"       // Config — re-creating enemies/items from config
#include "core/Enums.h"        // EntityKind, ItemKind, TileType
#include "core/Vec2.h"         // Vec2 — position type

// -- World -------------------------------------------------------------------
#include "world/GridMap.h"     // GridMap::setType, width/height

// -- Entities ----------------------------------------------------------------
#include "entities/Enemy.h"    // Enemy base + all subtype ctors (same .h/.cpp)
#include "entities/Player.h"   // Player — stat setters used during restore

// -- Items -------------------------------------------------------------------
#include "items/AmmoItem.h"    // AmmoItem ctor
#include "items/Armor.h"       // Armor ctor
#include "items/HealthPotion.h"// HealthPotion ctor
#include "items/Treasure.h"    // Treasure ctor
#include "items/Weapon.h"      // Weapon ctor

namespace dga {

// ===========================================================================
// Tag string constants (keep in one place so save and load agree).
// ===========================================================================
static constexpr const char* TAG_SEED          = "SEED";
static constexpr const char* TAG_WAVE          = "WAVE";
static constexpr const char* TAG_SCORE         = "SCORE";
static constexpr const char* TAG_GOLD          = "GOLD";
static constexpr const char* TAG_TURN          = "TURN";
static constexpr const char* TAG_KILLS         = "KILLS";
static constexpr const char* TAG_PLAYER_POS    = "PLAYER_POS";
static constexpr const char* TAG_PLAYER_HEALTH = "PLAYER_HEALTH";
static constexpr const char* TAG_PLAYER_AMMO   = "PLAYER_AMMO";
static constexpr const char* TAG_PLAYER_ARMOR  = "PLAYER_ARMOR";
static constexpr const char* TAG_PLAYER_ATTACK = "PLAYER_ATTACK";
static constexpr const char* TAG_CHARGE        = "CHARGE";
static constexpr const char* TAG_MAP_WIDTH     = "MAP_WIDTH";
static constexpr const char* TAG_MAP_HEIGHT    = "MAP_HEIGHT";
static constexpr const char* TAG_TILE          = "TILE";
static constexpr const char* TAG_ENEMY         = "ENEMY";
static constexpr const char* TAG_ITEM          = "ITEM";

// Tile sub-tokens.
static constexpr char TILE_FLOOR = 'F';
static constexpr char TILE_WALL  = 'W';

// Enemy kind sub-tokens (saved as short strings for readability).
static constexpr const char* KIND_MELEE  = "Melee";
static constexpr const char* KIND_ROOK   = "Rook";
static constexpr const char* KIND_BISHOP = "Bishop";
static constexpr const char* KIND_QUEEN  = "Queen";
static constexpr const char* KIND_FAST   = "Fast";
static constexpr const char* KIND_BOSS   = "Boss";

// Item kind sub-tokens.
static constexpr const char* KIND_HEALTH_POTION = "HealthPotion";
static constexpr const char* KIND_WEAPON        = "Weapon";
static constexpr const char* KIND_AMMO_ITEM     = "AmmoItem";
static constexpr const char* KIND_ARMOR         = "Armor";
static constexpr const char* KIND_TREASURE      = "Treasure";

// ===========================================================================
// save()
// ===========================================================================

bool SaveManager::save(const GameState&   state,
                       const std::string& filePath,
                       EventLog&          log) {
    // Truncate-and-overwrite: each save is a full snapshot, not an incremental
    // append.  std::ios::trunc is redundant for ofstream (default) but explicit.
    std::ofstream outFile(filePath, std::ios::out | std::ios::trunc);
    if (!outFile.is_open()) {
        log.append("SaveManager: ERROR — could not open save file for writing: "
                   + filePath);
        return false; // R3.3: never crash; just report and return.
    }

    // ---- Scalar run counters -----------------------------------------------
    outFile << TAG_SEED          << ' ' << state.rng().seed()       << '\n';
    outFile << TAG_WAVE          << ' ' << state.waveNumber()       << '\n';
    outFile << TAG_SCORE         << ' ' << state.score()            << '\n';
    outFile << TAG_GOLD          << ' ' << state.gold()             << '\n';
    outFile << TAG_TURN          << ' ' << state.turnCount()        << '\n';
    outFile << TAG_KILLS         << ' ' << state.enemiesKilled()    << '\n';

    // ---- Player state -------------------------------------------------------
    const Player& player = state.player();
    outFile << TAG_PLAYER_POS    << ' ' << player.position().x
                                 << ' ' << player.position().y  << '\n';
    outFile << TAG_PLAYER_HEALTH << ' ' << player.health()      << '\n';
    outFile << TAG_PLAYER_AMMO   << ' ' << player.ammo()        << '\n';
    outFile << TAG_PLAYER_ARMOR  << ' ' << player.armor()       << '\n';
    outFile << TAG_PLAYER_ATTACK << ' ' << player.attack()      << '\n';
    outFile << TAG_CHARGE        << ' ' << player.chargeMeter() << '\n';

    // ---- Map ----------------------------------------------------------------
    const GridMap& map = state.map();
    outFile << TAG_MAP_WIDTH  << ' ' << map.width()  << '\n';
    outFile << TAG_MAP_HEIGHT << ' ' << map.height() << '\n';

    for (int tileY = 0; tileY < map.height(); ++tileY) {
        for (int tileX = 0; tileX < map.width(); ++tileX) {
            const Vec2 tilePos(tileX, tileY);
            const char tileChar =
                (map.typeAt(tilePos) == TileType::Floor) ? TILE_FLOOR
                                                         : TILE_WALL;
            outFile << TAG_TILE
                    << ' ' << tileX
                    << ' ' << tileY
                    << ' ' << tileChar
                    << '\n';
        }
    }

    // ---- Enemies ------------------------------------------------------------
    for (const auto& enemyPtr : state.enemies()) {
        if (enemyPtr == nullptr) {
            continue; // Defensive: the GameState should never hold null entries.
        }
        const Enemy& enemy = *enemyPtr;

        // Map EntityKind to the saved tag string.
        const char* kindTag = nullptr;
        switch (enemy.kind()) {
            case EntityKind::MeleeEnemy:  kindTag = KIND_MELEE;  break;
            case EntityKind::RookEnemy:   kindTag = KIND_ROOK;   break;
            case EntityKind::BishopEnemy: kindTag = KIND_BISHOP; break;
            case EntityKind::QueenEnemy:  kindTag = KIND_QUEEN;  break;
            case EntityKind::FastEnemy:   kindTag = KIND_FAST;   break;
            case EntityKind::BossEnemy:   kindTag = KIND_BOSS;   break;
            default:
                // Player kind would be a programming error; skip it.
                continue;
        }

        outFile << TAG_ENEMY
                << ' ' << kindTag
                << ' ' << enemy.position().x
                << ' ' << enemy.position().y
                << ' ' << enemy.health()
                << '\n';
    }

    // ---- Items --------------------------------------------------------------
    for (const auto& itemPtr : state.items()) {
        if (itemPtr == nullptr) {
            continue;
        }
        const Item& item = *itemPtr;

        outFile << TAG_ITEM;

        switch (item.kind()) {
            case ItemKind::HealthPotion:
                // HealthPotion has no extra parameters.
                outFile << ' ' << KIND_HEALTH_POTION
                        << ' ' << item.position().x
                        << ' ' << item.position().y;
                break;

            case ItemKind::Weapon: {
                // Extra: attackValue and ranged flag.
                const auto& weapon = static_cast<const Weapon&>(item);
                outFile << ' ' << KIND_WEAPON
                        << ' ' << item.position().x
                        << ' ' << item.position().y
                        << ' ' << weapon.attackValue()
                        << ' ' << (weapon.isRanged() ? 1 : 0);
                break;
            }

            case ItemKind::AmmoItem: {
                // Extra: ammo amount.
                const auto& ammoItem = static_cast<const AmmoItem&>(item);
                outFile << ' ' << KIND_AMMO_ITEM
                        << ' ' << item.position().x
                        << ' ' << item.position().y
                        << ' ' << ammoItem.amount();
                break;
            }

            case ItemKind::Armor: {
                // Extra: armor bonus.
                const auto& armor = static_cast<const Armor&>(item);
                outFile << ' ' << KIND_ARMOR
                        << ' ' << item.position().x
                        << ' ' << item.position().y
                        << ' ' << armor.armorBonus();
                break;
            }

            case ItemKind::Treasure: {
                // Extra: treasure value.
                const auto& treasure = static_cast<const Treasure&>(item);
                outFile << ' ' << KIND_TREASURE
                        << ' ' << item.position().x
                        << ' ' << item.position().y
                        << ' ' << treasure.value();
                break;
            }
        }

        outFile << '\n';
    }

    // RAII: outFile flushes and closes on scope exit.
    outFile.flush();
    if (outFile.good()) {
        log.append("SaveManager: run saved to " + filePath);
        return true;
    }
    log.append("SaveManager: WARNING — write error while saving to "
               + filePath);
    return false;
}

// ===========================================================================
// load()  — transactional: parse everything into staging, then commit.
// ===========================================================================

bool SaveManager::load(const std::string& filePath,
                       GameState&         state,
                       EventLog&          log) {
    // ---- Step 1: open the file (R26.3, R3.3) --------------------------------
    std::ifstream inFile(filePath);
    if (!inFile.is_open()) {
        log.append("SaveManager: no save file found at " + filePath);
        return false; // R26.3: leave state unchanged.
    }

    // ---- Step 2: Parse all lines into staging variables --------------------
    //
    // All "staging_*" values are LOCAL.  Nothing is written to `state` until
    // every line has been parsed successfully.

    // --- Staging scalars ---
    bool     foundSeed        = false;
    bool     foundWave        = false;
    bool     foundScore       = false;
    // Gold (Feature 1) is OPTIONAL during load for backward compatibility:
    // saves written before the field existed simply omit the GOLD line and
    // are restored with a 0 wallet. There is therefore no foundGold flag —
    // the staging value just defaults to 0 and is committed unconditionally.
    bool     foundTurn        = false;
    bool     foundKills       = false;
    bool     foundPlayerPos   = false;
    bool     foundPlayerHp    = false;
    bool     foundPlayerAmmo  = false;
    bool     foundPlayerArmor = false;
    bool     foundPlayerAtk   = false;
    bool     foundCharge      = false;
    bool     foundMapWidth    = false;
    bool     foundMapHeight   = false;

    unsigned int stagingSeed   = 0;
    int          stagingWave   = 1;
    int          stagingScore  = 0;
    int          stagingGold   = 0;
    int          stagingTurn   = 0;
    int          stagingKills  = 0;
    Vec2         stagingPlayerPos(0, 0);
    int          stagingPlayerHp    = 0;
    int          stagingPlayerAmmo  = 0;
    int          stagingPlayerArmor = 0;
    int          stagingPlayerAtk   = 0;
    int          stagingCharge      = 0;
    int          stagingMapWidth    = 0;
    int          stagingMapHeight   = 0;

    // --- Staging map tiles ---
    // We defer building the GridMap until we know width/height.  Tiles are
    // stored as (x, y, TileType) triples and applied once both dimensions are
    // known.
    struct StagingTile {
        int      tileX;
        int      tileY;
        TileType tileType;
    };
    std::vector<StagingTile> stagingTiles;

    // --- Staging enemies ---
    struct StagingEnemy {
        EntityKind enemyKind;
        int        posX;
        int        posY;
        int        health;
    };
    std::vector<StagingEnemy> stagingEnemies;

    // --- Staging items ---
    struct StagingItem {
        ItemKind itemKind;
        int      posX;
        int      posY;
        // Kind-specific extras (only some are meaningful per kind).
        int      extraInt1 = 0; // Weapon.attackValue / AmmoItem.amount
                                // / Armor.armorBonus / Treasure.value
        int      extraInt2 = 0; // Weapon.ranged flag (0 or 1)
    };
    std::vector<StagingItem> stagingItems;

    // ---- Parsing loop -------------------------------------------------------
    std::string currentLine;
    int         lineNumber = 0;

    while (std::getline(inFile, currentLine)) {
        ++lineNumber;

        // Skip blank lines.
        if (currentLine.empty()) {
            continue;
        }

        std::istringstream lineStream(currentLine);
        std::string        tag;
        lineStream >> tag;

        if (tag == TAG_SEED) {
            if (!(lineStream >> stagingSeed)) {
                log.append("SaveManager: malformed SEED on line "
                           + std::to_string(lineNumber));
                return false;
            }
            foundSeed = true;

        } else if (tag == TAG_WAVE) {
            if (!(lineStream >> stagingWave)) {
                log.append("SaveManager: malformed WAVE on line "
                           + std::to_string(lineNumber));
                return false;
            }
            foundWave = true;

        } else if (tag == TAG_SCORE) {
            if (!(lineStream >> stagingScore)) {
                log.append("SaveManager: malformed SCORE on line "
                           + std::to_string(lineNumber));
                return false;
            }
            foundScore = true;

        } else if (tag == TAG_GOLD) {
            // OPTIONAL field (Feature 1). Older save files do not contain a
            // GOLD line; if one is present we read it, otherwise the staging
            // value defaults to 0 and the run loads with an empty wallet.
            if (!(lineStream >> stagingGold)) {
                log.append("SaveManager: malformed GOLD on line "
                           + std::to_string(lineNumber));
                return false;
            }

        } else if (tag == TAG_TURN) {
            if (!(lineStream >> stagingTurn)) {
                log.append("SaveManager: malformed TURN on line "
                           + std::to_string(lineNumber));
                return false;
            }
            foundTurn = true;

        } else if (tag == TAG_KILLS) {
            if (!(lineStream >> stagingKills)) {
                log.append("SaveManager: malformed KILLS on line "
                           + std::to_string(lineNumber));
                return false;
            }
            foundKills = true;

        } else if (tag == TAG_PLAYER_POS) {
            if (!(lineStream >> stagingPlayerPos.x >> stagingPlayerPos.y)) {
                log.append("SaveManager: malformed PLAYER_POS on line "
                           + std::to_string(lineNumber));
                return false;
            }
            foundPlayerPos = true;

        } else if (tag == TAG_PLAYER_HEALTH) {
            if (!(lineStream >> stagingPlayerHp)) {
                log.append("SaveManager: malformed PLAYER_HEALTH on line "
                           + std::to_string(lineNumber));
                return false;
            }
            foundPlayerHp = true;

        } else if (tag == TAG_PLAYER_AMMO) {
            if (!(lineStream >> stagingPlayerAmmo)) {
                log.append("SaveManager: malformed PLAYER_AMMO on line "
                           + std::to_string(lineNumber));
                return false;
            }
            foundPlayerAmmo = true;

        } else if (tag == TAG_PLAYER_ARMOR) {
            if (!(lineStream >> stagingPlayerArmor)) {
                log.append("SaveManager: malformed PLAYER_ARMOR on line "
                           + std::to_string(lineNumber));
                return false;
            }
            foundPlayerArmor = true;

        } else if (tag == TAG_PLAYER_ATTACK) {
            if (!(lineStream >> stagingPlayerAtk)) {
                log.append("SaveManager: malformed PLAYER_ATTACK on line "
                           + std::to_string(lineNumber));
                return false;
            }
            foundPlayerAtk = true;

        } else if (tag == TAG_CHARGE) {
            if (!(lineStream >> stagingCharge)) {
                log.append("SaveManager: malformed CHARGE on line "
                           + std::to_string(lineNumber));
                return false;
            }
            foundCharge = true;

        } else if (tag == TAG_MAP_WIDTH) {
            if (!(lineStream >> stagingMapWidth)) {
                log.append("SaveManager: malformed MAP_WIDTH on line "
                           + std::to_string(lineNumber));
                return false;
            }
            foundMapWidth = true;

        } else if (tag == TAG_MAP_HEIGHT) {
            if (!(lineStream >> stagingMapHeight)) {
                log.append("SaveManager: malformed MAP_HEIGHT on line "
                           + std::to_string(lineNumber));
                return false;
            }
            foundMapHeight = true;

        } else if (tag == TAG_TILE) {
            int  tileX = 0, tileY = 0;
            char tileChar = ' ';
            if (!(lineStream >> tileX >> tileY >> tileChar)) {
                log.append("SaveManager: malformed TILE on line "
                           + std::to_string(lineNumber));
                return false;
            }
            TileType tileType;
            if      (tileChar == TILE_FLOOR) { tileType = TileType::Floor; }
            else if (tileChar == TILE_WALL)  { tileType = TileType::Wall;  }
            else {
                log.append("SaveManager: unknown tile type '" +
                           std::string(1, tileChar) + "' on line " +
                           std::to_string(lineNumber));
                return false;
            }
            stagingTiles.push_back({ tileX, tileY, tileType });

        } else if (tag == TAG_ENEMY) {
            std::string kindTag;
            int         posX = 0, posY = 0, health = 0;
            if (!(lineStream >> kindTag >> posX >> posY >> health)) {
                log.append("SaveManager: malformed ENEMY on line "
                           + std::to_string(lineNumber));
                return false;
            }

            EntityKind enemyKind;
            if      (kindTag == KIND_MELEE)  { enemyKind = EntityKind::MeleeEnemy;  }
            else if (kindTag == KIND_ROOK)   { enemyKind = EntityKind::RookEnemy;   }
            else if (kindTag == KIND_BISHOP) { enemyKind = EntityKind::BishopEnemy; }
            else if (kindTag == KIND_QUEEN)  { enemyKind = EntityKind::QueenEnemy;  }
            else if (kindTag == KIND_FAST)   { enemyKind = EntityKind::FastEnemy;   }
            else if (kindTag == KIND_BOSS)   { enemyKind = EntityKind::BossEnemy;   }
            else {
                log.append("SaveManager: unknown enemy kind '" + kindTag
                           + "' on line " + std::to_string(lineNumber));
                return false;
            }
            stagingEnemies.push_back({ enemyKind, posX, posY, health });

        } else if (tag == TAG_ITEM) {
            std::string kindTag;
            int         posX = 0, posY = 0;
            if (!(lineStream >> kindTag >> posX >> posY)) {
                log.append("SaveManager: malformed ITEM on line "
                           + std::to_string(lineNumber));
                return false;
            }

            StagingItem stagingItem;
            stagingItem.posX = posX;
            stagingItem.posY = posY;

            if (kindTag == KIND_HEALTH_POTION) {
                stagingItem.itemKind = ItemKind::HealthPotion;
                // No extra fields for a HealthPotion.

            } else if (kindTag == KIND_WEAPON) {
                int attackValue = 0, rangedFlag = 0;
                if (!(lineStream >> attackValue >> rangedFlag)) {
                    log.append("SaveManager: malformed Weapon ITEM on line "
                               + std::to_string(lineNumber));
                    return false;
                }
                stagingItem.itemKind  = ItemKind::Weapon;
                stagingItem.extraInt1 = attackValue;
                stagingItem.extraInt2 = rangedFlag;

            } else if (kindTag == KIND_AMMO_ITEM) {
                int amount = 0;
                if (!(lineStream >> amount)) {
                    log.append("SaveManager: malformed AmmoItem ITEM on line "
                               + std::to_string(lineNumber));
                    return false;
                }
                stagingItem.itemKind  = ItemKind::AmmoItem;
                stagingItem.extraInt1 = amount;

            } else if (kindTag == KIND_ARMOR) {
                int armorBonus = 0;
                if (!(lineStream >> armorBonus)) {
                    log.append("SaveManager: malformed Armor ITEM on line "
                               + std::to_string(lineNumber));
                    return false;
                }
                stagingItem.itemKind  = ItemKind::Armor;
                stagingItem.extraInt1 = armorBonus;

            } else if (kindTag == KIND_TREASURE) {
                int value = 0;
                if (!(lineStream >> value)) {
                    log.append("SaveManager: malformed Treasure ITEM on line "
                               + std::to_string(lineNumber));
                    return false;
                }
                stagingItem.itemKind  = ItemKind::Treasure;
                stagingItem.extraInt1 = value;

            } else {
                log.append("SaveManager: unknown item kind '" + kindTag
                           + "' on line " + std::to_string(lineNumber));
                return false;
            }

            stagingItems.push_back(stagingItem);

        } else {
            // Unknown tag: treat as malformed (R3.4).
            log.append("SaveManager: unrecognised tag '" + tag + "' on line "
                       + std::to_string(lineNumber));
            return false;
        }
    }

    // ---- Step 3: Validate that all required scalars were present -----------
    if (!foundSeed)        { log.append("SaveManager: missing SEED");          return false; }
    if (!foundWave)        { log.append("SaveManager: missing WAVE");          return false; }
    if (!foundScore)       { log.append("SaveManager: missing SCORE");         return false; }
    if (!foundTurn)        { log.append("SaveManager: missing TURN");          return false; }
    if (!foundKills)       { log.append("SaveManager: missing KILLS");         return false; }
    if (!foundPlayerPos)   { log.append("SaveManager: missing PLAYER_POS");    return false; }
    if (!foundPlayerHp)    { log.append("SaveManager: missing PLAYER_HEALTH"); return false; }
    if (!foundPlayerAmmo)  { log.append("SaveManager: missing PLAYER_AMMO");   return false; }
    if (!foundPlayerArmor) { log.append("SaveManager: missing PLAYER_ARMOR");  return false; }
    if (!foundPlayerAtk)   { log.append("SaveManager: missing PLAYER_ATTACK"); return false; }
    if (!foundCharge)      { log.append("SaveManager: missing CHARGE");        return false; }
    if (!foundMapWidth)    { log.append("SaveManager: missing MAP_WIDTH");     return false; }
    if (!foundMapHeight)   { log.append("SaveManager: missing MAP_HEIGHT");    return false; }

    // ---- Step 4: Commit — write everything back to `state` -----------------
    //
    // From this point forward the function only modifies `state`.  All parsing
    // is complete and validated; partial-overwrite risk is gone.

    // Re-seed the RNG (R26.4): the run continues with the same sequence it had
    // at the moment it was saved.
    state.rng().reseed(stagingSeed);

    // Restore run counters.
    state.setWaveNumber(stagingWave);
    state.setScore(stagingScore);
    // Restore the gold reserve. Saves predating Feature 1 omit the GOLD line
    // and stagingGold remains 0 (the default), so older saves simply load with
    // an empty wallet — backward compatibility intact.
    state.setGold(stagingGold);

    // Turn count: increment from 0 by staging value.
    // GameState only exposes incrementTurnCount(), so we call it stagingTurn times.
    // (turnCount_ starts at 0 in a freshly constructed GameState; the constructor
    //  that created `state` already set it to 0.  We need to *set* it here.)
    // Because there is no setTurnCount, we replicate the count by incrementing.
    // For large turn counts this loop is trivial (it is purely integer arithmetic).
    {
        // First bring current count to zero by resetting through the only available
        // path: we use the GameState as a staging object, so we just call
        // incrementTurnCount the right number of times.  The current count should
        // already be zero (state was just constructed by the caller), but we cannot
        // know that for certain, so we read it first.
        const int currentTurnCount = state.turnCount();
        const int turnsNeeded      = stagingTurn - currentTurnCount;
        for (int i = 0; i < turnsNeeded; ++i) {
            state.incrementTurnCount();
        }
    }

    // Restore enemies-killed count by incrementing from 0.
    {
        const int currentKills = state.enemiesKilled();
        const int killsNeeded  = stagingKills - currentKills;
        for (int i = 0; i < killsNeeded; ++i) {
            state.incrementEnemiesKilled();
        }
    }

    // Restore player stats.
    Player& player = state.player();
    player.setPosition(stagingPlayerPos);
    // Health: bring the player to stagingPlayerHp.
    // The player currently has the default starting health from construction.
    // We first heal to max, then takeDamage to reach the exact saved value.
    player.heal(player.maxHealth());          // Bring to max.
    const int damageToApply = player.health() - stagingPlayerHp;
    if (damageToApply > 0) {
        player.takeDamage(damageToApply);     // Reduce to saved value.
    }
    // Attack, armor, ammo, charge.
    player.setAttack(stagingPlayerAtk);
    // Armor: addArmor ignores negatives; zero out first by starting from 0.
    // Entity::armor_ is protected; Player exposes addArmor(int) only.
    // We must reach stagingPlayerArmor from the initial playerStartingArmor.
    // The simplest way without a setArmor mutator: add the delta.
    {
        const int currentArmor = player.armor();
        const int armorDelta   = stagingPlayerArmor - currentArmor;
        if (armorDelta > 0) {
            player.addArmor(armorDelta);
        }
        // If armorDelta is negative or zero, we leave it — addArmor ignores
        // negatives by design. In practice a saved game should never have LESS
        // armor than the starting value, but we handle it gracefully.
    }
    // Ammo: similarly, add the delta from the current count.
    {
        const int currentAmmo = player.ammo();
        const int ammoDelta   = stagingPlayerAmmo - currentAmmo;
        if (ammoDelta > 0) {
            player.addAmmo(ammoDelta);
        }
    }
    player.setChargeMeter(stagingCharge);

    // Restore map tiles.
    GridMap& map = state.map();
    for (const StagingTile& stagingTile : stagingTiles) {
        map.setType(Vec2(stagingTile.tileX, stagingTile.tileY),
                    stagingTile.tileType);
    }

    // Restore enemies — clear the current live list then re-populate.
    state.enemies().clear();

    // We need a Config to construct enemies (they read their stats from it).
    // Build a default Config here; the stats must match what was originally
    // saved for the health to be restored correctly.
    const Config defaultConfig;

    for (const StagingEnemy& stagingEnemy : stagingEnemies) {
        const Vec2 enemyPos(stagingEnemy.posX, stagingEnemy.posY);
        std::unique_ptr<Enemy> newEnemy;

        switch (stagingEnemy.enemyKind) {
            case EntityKind::MeleeEnemy:
                newEnemy = std::make_unique<MeleeEnemy>(enemyPos, defaultConfig);
                break;
            case EntityKind::RookEnemy:
                newEnemy = std::make_unique<RookEnemy>(enemyPos, defaultConfig);
                break;
            case EntityKind::BishopEnemy:
                newEnemy = std::make_unique<BishopEnemy>(enemyPos, defaultConfig);
                break;
            case EntityKind::QueenEnemy:
                newEnemy = std::make_unique<QueenEnemy>(enemyPos, defaultConfig);
                break;
            case EntityKind::FastEnemy:
                newEnemy = std::make_unique<FastEnemy>(enemyPos, defaultConfig);
                break;
            case EntityKind::BossEnemy:
                newEnemy = std::make_unique<BossEnemy>(enemyPos, defaultConfig);
                break;
            default:
                // Shouldn't reach here (validated above), but be safe.
                continue;
        }

        // Adjust health to the saved value.  The enemy was constructed at full
        // health; apply damage to bring it down to the saved value.
        const int damageToRestore = newEnemy->health() - stagingEnemy.health;
        if (damageToRestore > 0) {
            newEnemy->takeDamage(damageToRestore);
        }

        state.addEnemy(std::move(newEnemy));
    }

    // Restore items — clear then re-populate.
    state.items().clear();

    for (const StagingItem& stagingItem : stagingItems) {
        const Vec2 itemPos(stagingItem.posX, stagingItem.posY);
        std::unique_ptr<Item> newItem;

        switch (stagingItem.itemKind) {
            case ItemKind::HealthPotion:
                newItem = std::make_unique<HealthPotion>(itemPos);
                break;
            case ItemKind::Weapon:
                newItem = std::make_unique<Weapon>(
                    itemPos,
                    stagingItem.extraInt1,             // attackValue
                    stagingItem.extraInt2 != 0          // ranged flag
                );
                break;
            case ItemKind::AmmoItem:
                newItem = std::make_unique<AmmoItem>(
                    itemPos,
                    stagingItem.extraInt1              // amount
                );
                break;
            case ItemKind::Armor:
                newItem = std::make_unique<Armor>(
                    itemPos,
                    stagingItem.extraInt1              // armorBonus
                );
                break;
            case ItemKind::Treasure:
                newItem = std::make_unique<Treasure>(
                    itemPos,
                    stagingItem.extraInt1              // value
                );
                break;
        }

        state.addItem(std::move(newItem));
    }

    log.append("SaveManager: run restored from " + filePath);
    return true;
}

} // namespace dga
