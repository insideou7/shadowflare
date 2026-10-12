# Open content

Replacement content that lets the engine run without the original game:
seven areas (a village meadow, the old waterworks under it, caves and a
coal mine, a fort, the wilds, the deep caverns and a castle), the
original's first area laid out on its routes with our own walls and
paths (Remote Town, Cold Svalt Town and the fields, forests and dungeons
round them), the hero and the heroine with their equipment, the Create New Character screen (`System/Select`, drawn by `tools/art/select_screen.py` with figures from `tools/art/portraits.py`), 24 kinds of monster, the
townspeople, items, the game panel along the bottom of the screen (`tools/art/panel.py`), the inventory panel, the shop window, the speech
balloon, the font and the spell effects (`Character/OPTION`, painted by
`tools/art/effects.py`). The formats are described in
[docs/content.md](../docs/content.md).

All of it is made for this project: modelled and rendered in Blender by
the scripts in [tools/art](../tools/art) and laid out by
`tools/art/build_pack.py`, in the style of the original game but drawn
from scratch, with no original pictures traced or reused. Monster stats
and behaviour (`ai.txt`) and the item definitions (`items.txt`) are our
own too.

## License

Everything in this folder is under the MIT license, the same as the
engine's code; see [LICENSE](../LICENSE) at the top of the repository.
