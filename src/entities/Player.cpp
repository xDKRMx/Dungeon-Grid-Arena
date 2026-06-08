// =============================================================================
// entities/Player.cpp
//
// Purpose:
//   Definitions for the Player class declared in entities/Player.h. This is also
//   the translation unit that needs the FULL definitions that the header only
//   forward-declared: Config (to read starting stats), Weapon (to equip), and
//   Ability (so unique_ptr<Ability> can be stored and destroyed). Including them
//   here - and only here - is what resolves the Item/Player/Inventory include
//   knot while keeping the header light.
//
// Layer: entities (depends on core/Config, items/Weapon, abilities/Ability).
// =============================================================================
#include "entities/Player.h"

#include "abilities/Ability.h" // full type so unique_ptr<Ability> can be managed.
#include "core/Config.h"       // full type so the ctor can read starting stats.
#include "items/Weapon.h"      // full type so equip(Weapon&) can store the pointer.

namespace dga {

// Build the hero from configuration. The base Entity is constructed with the
// Player kind tag and the starting/max Health, attack, and armor read from
// Config - no literals appear here (R8.5, R15.1). Ammo starts at the configured
// amount; the Charge_Meter starts empty; the shield starts inactive; no weapon
// is equipped yet.
Player::Player(const Config& config, const Vec2& startPosition)
    : Entity(EntityKind::Player, startPosition,
             config.playerStartingHealth(), config.playerMaxHealth(),
             config.playerStartingAttack(), config.playerStartingArmor()),
      ammo_(config.playerStartingAmmo()),
      fireRange_(config.playerBaseFireRange()),
      chargeMeter_(0),
      shieldRemainingTurns_(0),
      fireCooldown_(0),
      fireCooldownDuration_(config.playerFireCooldown()),
      wallPierceShotsRemaining_(0),
      doubleMoveTurnsRemaining_(0),
      blinkChainUsesRemaining_(0),
      twinStrikeChargesRemaining_(0),
      equippedWeapon_(nullptr),
      inventory_(),
      abilities_() {}

// ---- Ammo -------------------------------------------------------------------

int Player::ammo() const {
    return ammo_;
}

// Reject negative additions so a pickup can only ever increase ammo (R19.3).
void Player::addAmmo(int amount) {
    if (amount < 0) {
        return;
    }
    ammo_ += amount;
}

// Spend ammo only when the request is valid AND affordable. We refuse negative
// requests (nonsensical) and any request larger than the current reserve, in
// which case ammo is left untouched and we return false. This is the hook the
// combat system uses to reject a shot when the player is out of ammo (R16.3).
bool Player::spendAmmo(int amount) {
    if (amount < 0 || amount > ammo_) {
        return false;
    }
    ammo_ -= amount;
    return true;
}

// ---- Fire range (Feature 1) -------------------------------------------------

// Report the hero's current fire range in cells. CombatSystem uses this as the
// stopping distance for a fired projectile instead of a fixed magic constant.
int Player::fireRange() const {
    return fireRange_;
}

// Extend the fire range by a positive bonus, rejecting non-positive deltas so a
// ranged Weapon pickup can only ever lengthen reach (mirrors addAmmo/addArmor).
void Player::addFireRange(int bonus) {
    if (bonus <= 0) {
        return;
    }
    fireRange_ += bonus;
}

// ---- Charge meter -----------------------------------------------------------

int Player::chargeMeter() const {
    return chargeMeter_;
}

// Store the value as-is; clamping against the configured maximum is done by the
// AbilitySystem, which is the component that knows the cap (R22.1).
void Player::setChargeMeter(int newCharge) {
    chargeMeter_ = newCharge;
}

// ---- Equipped weapon --------------------------------------------------------

// Record a non-owning pointer to the chosen weapon. We take the weapon by
// reference (so callers cannot pass null) and store its address; ownership stays
// with the GameState's item list (R19.2).
void Player::equip(Weapon& weapon) {
    equippedWeapon_ = &weapon;
}

Weapon* Player::equippedWeapon() const {
    return equippedWeapon_;
}

// A spread shot is granted whenever the player holds a ranged weapon. This
// makes the ranged weapon pickup feel POWERFUL and visually distinct: instead
// of a single beam, the player fires in the chosen direction AND the two
// adjacent diagonal directions simultaneously.
bool Player::hasSpreadShot() const {
    return equippedWeapon_ != nullptr && equippedWeapon_->isRanged();
}

// ---- Combat-stat changes from item pickups ----------------------------------

// Overwrite the hero's attack with the equipped weapon's value (R19.2). attack_
// is inherited protected state from Entity, so Player - a subclass - may set it
// directly here while outside code still cannot.
void Player::setAttack(int newAttack) {
    attack_ = newAttack;
}

// Add to the hero's armor, rejecting negative deltas so an Armor pickup can only
// strengthen defense (R19.4). armor_ is inherited protected Entity state.
void Player::addArmor(int amount) {
    if (amount < 0) {
        return;
    }
    armor_ += amount;
}

// ---- Inventory --------------------------------------------------------------

Inventory& Player::inventory() {
    return inventory_;
}

const Inventory& Player::inventory() const {
    return inventory_;
}

// ---- Shield state (R21.6) ---------------------------------------------------

// Set the shield duration to the given number of Turns. Any existing remaining
// time is overwritten so re-activating the shield refreshes it to the full new
// duration (R21.6).
void Player::applyShield(int turns) {
    shieldRemainingTurns_ = (turns > 0) ? turns : 0;
}

// Report whether the hero currently enjoys damage immunity from an active shield.
bool Player::isShielded() const {
    return shieldRemainingTurns_ > 0;
}

// Count down the shield by one Turn, clamping at zero so a no-longer-active
// shield never goes negative (R21.6).
void Player::tickShield() {
    if (shieldRemainingTurns_ > 0) {
        --shieldRemainingTurns_;
    }
}

int Player::shieldRemainingTurns() const {
    return shieldRemainingTurns_;
}

// ---- Fire cooldown (UX polish: throttles consecutive Fire actions) ----------

// Report the current Fire cooldown in Turns. The CombatSystem reads this before
// spending ammo so a shot during cooldown is rejected without altering state.
int Player::fireCooldown() const {
    return fireCooldown_;
}

// Set the cooldown to an exact value, clamping negative inputs to zero so the
// field can never represent an impossible "negative remaining time" state.
// CombatSystem calls this after a successful shot with Config::playerFireCooldown.
void Player::setFireCooldown(int turns) {
    fireCooldown_ = (turns > 0) ? turns : 0;
}

// Count the Fire cooldown down by one Turn, stopping at zero so an idle hero
// never goes into negative territory. AbilitySystem::tickCooldowns invokes this
// at end-of-turn alongside tickShield so every per-turn timer ticks together.
void Player::tickFireCooldown() {
    if (fireCooldown_ > 0) {
        --fireCooldown_;
    }
}

// ---- Fire cooldown duration (how many turns the cooldown LASTS) -------------

// Report how many Turns the fire cooldown lasts when reset after a shot. The
// CombatSystem reads this to decide what value to write into setFireCooldown().
// A value of 0 means "no cooldown" — the hero can fire every turn.
int Player::fireCooldownDuration() const {
    return fireCooldownDuration_;
}

// Override the fire cooldown duration. The "Rapid Fire" upgrade card sets this
// to 0 to grant the hero unlimited fire speed. Negative inputs are clamped.
void Player::setFireCooldownDuration(int turns) {
    fireCooldownDuration_ = (turns >= 0) ? turns : 0;
}

// ---- Shop-purchased temporary buffs (Feature 2) -----------------------------
//
// Each buff is a small charge counter consumed by the system that owns the
// matching mechanic. The accessors are mirror-image quartets (read / add /
// consume); negative deltas in addX are silently ignored so the Shop can only
// ever grant charges, and consumeX clamps at 0 so a stray double-call from a
// system never drives the counter negative.

int Player::wallPierceShotsRemaining() const { return wallPierceShotsRemaining_; }

void Player::addWallPierceShots(int charges) {
    if (charges <= 0) { return; }
    wallPierceShotsRemaining_ += charges;
}

void Player::consumeWallPierceShot() {
    if (wallPierceShotsRemaining_ > 0) {
        --wallPierceShotsRemaining_;
    }
}

int Player::doubleMoveTurnsRemaining() const { return doubleMoveTurnsRemaining_; }

void Player::addDoubleMoveTurns(int charges) {
    if (charges <= 0) { return; }
    doubleMoveTurnsRemaining_ += charges;
}

void Player::consumeDoubleMoveTurn() {
    if (doubleMoveTurnsRemaining_ > 0) {
        --doubleMoveTurnsRemaining_;
    }
}

int Player::blinkChainUsesRemaining() const { return blinkChainUsesRemaining_; }

void Player::addBlinkChainUses(int charges) {
    if (charges <= 0) { return; }
    blinkChainUsesRemaining_ += charges;
}

void Player::consumeBlinkChainUse() {
    if (blinkChainUsesRemaining_ > 0) {
        --blinkChainUsesRemaining_;
    }
}

int Player::twinStrikeChargesRemaining() const { return twinStrikeChargesRemaining_; }

void Player::addTwinStrikeCharges(int charges) {
    if (charges <= 0) { return; }
    twinStrikeChargesRemaining_ += charges;
}

void Player::consumeTwinStrikeCharge() {
    if (twinStrikeChargesRemaining_ > 0) {
        --twinStrikeChargesRemaining_;
    }
}

// ---- Abilities --------------------------------------------------------------

// Take ownership of the ability. Null pointers are ignored so the list never
// holds an empty slot the ability system would have to special-case. std::move
// transfers the unique_ptr's ownership into the vector.
void Player::addAbility(std::unique_ptr<Ability> ability) {    if (ability == nullptr) {
        return;
    }
    abilities_.push_back(std::move(ability));
}

const std::vector<std::unique_ptr<Ability>>& Player::abilities() const {
    return abilities_;
}

std::vector<std::unique_ptr<Ability>>& Player::abilities() {
    return abilities_;
}

// ---- Polymorphic rendering --------------------------------------------------

// The hero is drawn as '@', the conventional roguelike player symbol. Returning
// it here overrides Entity's pure-virtual glyph(), which is what makes Player a
// concrete (constructible) type (R4).
char Player::glyph() const {
    return '@';
}

} // namespace dga
