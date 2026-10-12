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

    Class cls = Class::Special;
    std::int32_t code = 0;  // the item's id within its class; Mct placements use (class, code)
    std::string name;       // as shown once identified
    std::int32_t subtype = -1;
    std::int32_t quality = 0;  // 0 normal, 1 magic, 2 unique, 3 quest
    std::int32_t width = 1, height = 1;  // in 32-pixel inventory cells
    std::int32_t price = 0;
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
    std::array<std::int32_t, kStatCount> stats{};  // weapons and armour
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
