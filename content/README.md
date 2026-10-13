# Open content

Replacement content that lets the engine run without the original game:
the original's first area, walked on like the original's with our own walls
and trees (Remote Town, Cold Svalt Town and the fields, forests and dungeons
round them), the hero and the heroine with their equipment, the Create New Character screen (`System/Select`, drawn by `tools/art/select_screen.py` with figures from `tools/art/portraits.py`), 24 kinds of monster, the
townspeople, the hero's dogs (`partners.txt`, `Character/PARTNER`, with the dash's speed streak painted by `tools/art/streak.py`), items, the game panel along the bottom of the screen (`tools/art/panel.py`) and the magic bar's icons (`tools/art/magic_icons.py`), the inventory panel, the shop window, the GATE window (`tools/art/warp.py`), the speech
balloon, the font, the spell, trap and script effects (`Character/OPTION`,
painted by `tools/art/effects.py`) and the story pages that open and close
each episode (`System/Common/Pattern/Visual01`-`06`, `Waiting` and
`WaitIcon`, painted by `tools/art/story.py`; the story is told in our own
words and set in Liberation Serif Italic, under the SIL Open Font License
1.1), and the sound effects and music (`System/Game/Voice`,
`System/Game/Music` and `System/Title/Music`, synthesized and composed by
`tools/sound`). The formats are described in
[docs/content.md](../docs/content.md).

All of it is made for this project: modelled and rendered in Blender by
the scripts in [tools/art](../tools/art) and laid out by
`tools/art/build_pack.py`, in the style of the original game but drawn
from scratch, with no original pictures traced or reused. The sounds are
made the same way: synthesized by our own scripts, with no recording of
the original's sampled or reused, and the music is our own. Monster stats
and behaviour (`ai.txt`), the item definitions (`items.txt`) and the
dogs' names and stats (`partners.txt`) are our own too.

## License

Everything in this folder is under the MIT license, the same as the
engine's code; see [LICENSE](../LICENSE) at the top of the repository.
