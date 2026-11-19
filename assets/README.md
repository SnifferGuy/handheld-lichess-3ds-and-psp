# Lichess Handheld artwork

- The twelve Cburnett SVGs are by Colin M. L. Burnett, from [Lichess's Cburnett set](https://github.com/lichess-org/lila/tree/master/public/piece/cburnett). The upstream attribution and GPL v2 license text are included in `cburnett/`. Upstream distributes this set under GPL v2 or later.
- `NotoSans.ttf` is the variable-width Noto Sans font from [Google Fonts](https://github.com/google/fonts/tree/main/ofl/notosans), licensed under the SIL Open Font License 1.1. See `NotoSans-OFL.txt`.
- `icon-3ds.png` and `icon-psp.png` use Burnett's white knight over the game's board colors. Regenerate them with `python generate-atlas.py --icons-only`. The Cburnett attribution and GPL terms also apply to the artwork embedded in these icons.

`generate-atlas.py` rasterizes the original SVGs at both board sizes and packs them alongside printable ASCII glyphs. Install Pillow 13.0.0 and resvg-py 0.3.3, then run `python generate-atlas.py --tex3ds PATH_TO_TEX3DS` from the game directory. The checked-in `screen_atlas.hpp` allows ordinary builds without these asset tools. The 3DS uses a 256×512 GPU texture and the PSP uses the same-sized RGBA pixel data.
