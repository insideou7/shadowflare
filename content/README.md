# Open content

Replacement content that lets the engine run without the original game:
two areas (a goblin camp and its cave), the player, three kinds of
monster and their behaviour, and a few items. The formats are described in
[docs/content.md](../docs/content.md).

## Licence

Everything in this folder is licensed under the Creative Commons
Attribution-ShareAlike 3.0 licence ([LICENSE-CC-BY-SA-3.0.txt](LICENSE-CC-BY-SA-3.0.txt)),
separately from the engine's code, which is MIT.

The art and maps come from
[Flare: Empyrean Campaign](https://github.com/flareteam/flare-game),
Copyright © 2010-2013 Clint Bellanger, with contributors keeping the
copyright to their own contributions; its full credits are in
[CREDITS-flare.txt](CREDITS-flare.txt). They were converted by
`tools/content/import_flare.py` from flare-game commit `af6eee6d`:
character layers composited into single sprite sheets, tiles regrouped
into one sheet per map, and maps and monster placements converted to this
engine's formats. The item pictures (inventory and ground) and the
inventory panel are our own, rendered by `tools/art/items.py` and
`tools/art/ui.py`. Monster stats and behaviour (`ai.txt`) and the item
definitions (`items.txt`) are our own too.

Changes to these files must stay under CC-BY-SA 3.0 (or a later version)
and keep this attribution.
