#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sf {

// One kind of item, as System/Game/Parameter/Item.Ibn describes it
// (ShadowFlare.exe loads it at 0x462f90; see docs/formats.md). Only the
// fields the engine uses so far are kept.
struct ItemDef {
    enum class Class : std::int32_t { Weapon = 0, Armour = 1, Accessory = 2, Consumable = 3, Special = 4 };

    // Weapon subtypes. 1 and 3 are two-handed.
    static constexpr int kSword = 0, kAxe = 1, kGreatSword = 3, kBowgun = 5, kStaff = 8, kRod = 9;
    // Armour subtypes: which slot it is worn in.
    static constexpr int kHelmet = 0, kBody = 1, kShield = 2, kBoots = 3;

    // Indices into `stats`, in the order of the item tooltip.
    enum Stat { kAttack, kHit, kDefense, kEvasion, kMagicAttack, kMagicHit, kMagicDefense, kMagicEvasion,
                kAttackSpeed, kWalkSpeed, kStatCount };

    // Magic modifiers (weapons, armour and accessories): what a magic or
    // unique item may add, rolled once when the item is made (0x467310).
    // Indices in the order of the tooltip at 0x40af26; 14-21 are named
    // from the tooltip strings only.
    enum Modifier {
        kModAttack, kModDefense, kModHit, kModEvasion, kModMagicAttack, kModMagicDefense, kModMagicHit,
        kModMagicEvasion, kModAttackSpeed, kModWalkSpeed, kModMaxHp, kModMaxMp, kModStrength, kModMagicLevel,
        kModStunChance, kModStunLength, kModHighDamage, kModLifeRecovery, kModMentalRecovery, kModMentalCost,
        kModReflection, kModReflection2, kModStaminaMedicine, kModMentalMedicine, kModAbsorption,
        kModAbsorption2, kModGold, kModMineEffect, kModMineCount, kModChantSpeed,
        // What the companion gets while the hero wears the item (0x4138b0).
        kModCompanionHp, kModCompanionAttack, kModCompanionHit, kModCompanionDefense, kModCompanionEvasion,
        kModCompanionMagicAttack, kModCompanionMagicHit, kModCompanionMagicDefense, kModCompanionMagicEvasion,
        kModifierCount
    };
    // One modifier's range: with `chance` percent it is a number from min
    // to max, otherwise 0.
    struct ModifierRange {
        std::int32_t min = 0, max = 0, chance = 0;
    };

    Class cls = Class::Special;
    std::int32_t code = 0;  // the item's id within its class; Mct placements use (class, code)
    std::string name;       // as shown once identified
    std::string unidentifiedName;  // as shown before (magic and unique items drop unidentified)
    std::int32_t subtype = -1;
    std::int32_t quality = 0;  // 0 normal, 1 magic, 2 unique, 3 quest
    std::int32_t width = 1, height = 1;  // in 32-pixel inventory cells
    std::int32_t price = 0;
    // How heavy it is (0x28). The item window weighs what the hero wears
    // against their strength (0x445630), and it picks sounds (0x466110):
    // 60 or more sounds heavy.
    std::int32_t weight = 0;
    bool fixedPrice = false;  // 0x44: its modifiers add nothing to the price
    // Picture in the bag: pattern `invPattern` of System/Game/Pattern/Item<invBank as %04d>.
    std::int32_t invBank = -1, invPattern = -1;
    // Picture on the ground: Character/ITEM/<groundFolder as %08d>, either a
    // chart of its Animation.Caf or, when the folder has no animation, a
    // pattern of its Pattern.njp.
    std::int32_t groundFolder = -1, groundPicture = -1;
    std::int32_t dropWeight = 0;   // relative chance among items that may drop
    std::int32_t poolMask = 0;     // which drop lists and shops may pick it (bits)
    std::int32_t itemLevel = 0;    // how deep in the game it starts dropping
    std::int32_t requiredLevel = 0;
    // Weapons and armour: how much wear it takes before it breaks (0xb8),
    // 0 = never wears out.
    std::int32_t durability = 0;
    std::array<std::int32_t, kStatCount> stats{};  // weapons and armour
    // Weapons and armour: what wearing it adds to the hero's level in each
    // element (fire, water, earth, thunder, holy, dark, gel, metal; see
    // logic/magic.h).
    std::array<std::int32_t, 8> elements{};
    std::array<ModifierRange, kModifierCount> modifiers{};
    // Rolled the same way, then added to `elements` (0x44fe30).
    std::array<ModifierRange, 8> elementModifiers{};
    // Layers of the hero's Animation00.Caf turned on while worn (weapons,
    // body armour, shields), -1 = none.
    std::int32_t heroLayer = -1;
    std::int32_t glowLayer = -1;  // a second layer some weapons add (fire glow, aura)
    bool twoHandedFlag = false;   // weapons that are two-handed whatever their subtype
    // Consumables: word 0 = 1 if it may go in the belt, 1 = health restored,
    // 2 = mental power restored; the rest are not understood yet.
    std::array<std::int32_t, 9> effect{};

    bool twoHanded() const {
        return cls == Class::Weapon && (twoHandedFlag || subtype == kAxe || subtype == kGreatSword);
    }
    bool isGold() const { return cls == Class::Special && code == 0; }
    // Mines are counted, not carried in the bag (0x44a0e2).
    bool isMine() const { return cls == Class::Special && code == 1; }
};

struct ItemTable {
    std::vector<ItemDef> items;

    const ItemDef* find(ItemDef::Class cls, std::int32_t code) const;

    // Item.Ibn. `substitution` is the byte table the packed data goes
    // through before it is decompressed; it is only in ShadowFlare.exe (see
    // itemSubstitutionTable). Throws FormatError.
    static ItemTable parse(std::span<const std::uint8_t> data, const std::array<std::uint8_t, 256>& substitution);

    // The decompressed body: five classes, each a count and its records.
    static ItemTable parseRecords(std::span<const std::uint8_t> body);
};

// Reads Item.Ibn's substitution table out of the player's ShadowFlare.exe.
// The exe builds it on the stack with one `mov byte [esp+n], value` per
// entry (0x463031 to 0x46376e); this decodes those instructions. Throws
// FormatError when the exe is not the one expected.
std::array<std::uint8_t, 256> itemSubstitutionTable(std::span<const std::uint8_t> exe);

}  // namespace sf
