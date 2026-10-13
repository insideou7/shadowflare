#!/usr/bin/env python3
"""Sets the open content's monster stats for the original's combat rules.

The engine fights like ShadowFlare (hit chance = hit - evasion, 20-98%;
damage = attack x 90-110% - defence, at least 1; see src/logic/progression.h).
Our campaign is far shorter than the original's, so its monsters cannot use
the original's numbers. Instead each map has a level range the player should
be in, and every monster is sized against a hero of that level wearing that
map's typical gear. Its old health and attack (the first fields of its enemy
line) only say how strong it is next to the others on the map.

    python3 tools/content/balance.py content     # rewrites the enemy lines
"""
import pathlib
import re
import sys

# Experience per level, Table.Tbd table 13 (as in progression.cpp).
NEEDS = [25, 80, 150, 200, 300, 400, 600, 1200, 2000, 2200, 2400, 3000]

# Map: (first level, last level, weapon attack, armour defence).
MAPS = {
    "00000001": (1, 3, 20, 20),   # Goblin Meadow: Short Sword, Cloth Armour
    "00000002": (3, 4, 20, 28),   # Old Waterworks: + Buckler
    "00000003": (4, 6, 32, 39),   # Caves and Coal Mine: Long Sword, hood, boots
    "00000004": (6, 7, 32, 39),   # Fort of Thieves
    "00000005": (7, 8, 45, 59),   # The Wilds: Great Sword, Plate Mail
    "00000006": (8, 9, 45, 59),   # The Deep
    "00000007": (9, 10, 45, 59),  # The Castle
}


def total_experience(level):
    return sum(NEEDS[: level - 1])


def hero(level):
    """Attack and defence of the male warrior at a level (heroStats)."""
    classless = min(level, 5) - 1
    warrior = max(0, level - 5)
    attack = 10 + 4 * classless + sum(9 if k < 10 else 7 if k < 20 else 6 for k in range(warrior))
    defense = 10 + 4 * classless + sum(6 if k < 10 else 5 if k < 20 else 4 for k in range(warrior))
    return attack, defense


def main(root):
    for directory, (first, last, weapon, armour) in MAPS.items():
        path = pathlib.Path(root) / "Scenario" / directory / "scenario.txt"
        lines = path.read_text().splitlines()
        enemies = [i for i, line in enumerate(lines) if line.startswith("enemy=")]
        fields = {i: lines[i][len("enemy="):].split(",") for i in enemies}
        # The originals: kind,x,y,direction,hp,attack,radius,ai (kept in a
        # comment so the script can be run again).
        base = {}
        extra = {}  # magic defence and element (enemy_elements.py), kept
        for i in enemies:
            full = lines[i].split("#")[0].strip()[len("enemy="):].split(",")
            extra[i] = full[16:18]
            m = re.search(r"# base (\d+),(\d+)", lines[i])
            f = fields[i]
            base[i] = (int(m.group(1)), int(m.group(2))) if m else (int(f[4]), int(f[5]))
            fields[i] = f[:8] if not m else lines[i].split("#")[0].strip()[len("enemy="):].split(",")[:8]
        level = (first + last) // 2
        hero_attack, hero_defense = hero(level)
        player_attack, player_defense = hero_attack + weapon, hero_defense + armour
        mean_attack = sum(a for _, a in base.values()) / len(base)
        total_hp = sum(h for h, _ in base.values())
        experience = total_experience(last) - total_experience(first)
        for i in enemies:
            hp, attack = base[i]
            strength = attack / mean_attack
            defense = min(round(0.4 * player_attack * strength), round(0.7 * player_attack))
            new_attack = round(player_defense + 2.5 * attack)
            # About as many blows to fell it as before (the old rules did
            # about 20 a blow at the start).
            new_hp = max(1, round(hp * (player_attack - defense) / (20 + 2 * (level - 1))))
            exp = max(1, round(experience * hp / total_hp))
            f = fields[i]
            out = f[:4] + [str(new_hp), str(new_attack), f[6], f[7], str(level), "0", "0", "0", str(exp), str(defense), "50", "0"] + extra[i]
            lines[i] = "enemy=" + ",".join(out) + f"  # base {hp},{attack}"
        path.write_text("\n".join(lines) + "\n")
        print(directory, f"level {level}: hero attack {player_attack}, defence {player_defense}, {len(enemies)} monsters, {experience} experience")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "content")
