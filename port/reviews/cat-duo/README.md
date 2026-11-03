# Pepper and Poppy

Poppy is the grey tuxedo; Pepper is the grey tabby. Either starts their shared
six-prize battle at Ishihara's House after the first medal.

The [model study](../../characters/experiments/cat-concepts/README.md) retains the
image-generated concept and three sequential model/native-review rounds.
[Editable models](cats.blend) and [Blender preview](models.png) show authored size.
The final game renders both at 50% size and adjusts their conversation framing.
[Room](native/room.png), [Poppy conversation](native/tuxedo.png), and
[Pepper conversation](native/tabby.png) are actual 320×240 ares captures using the
production N64 renderer with 4 MiB RAM.

The final deck keeps Kangaskhan and replaces Farfetch’d and Chansey with Mew Lv8
and Mew Lv23. It uses eight Lightning, four Psychic and four Double Colorless
Energy. Meowth, Electabuzz Lv35 and a Lightning Energy are guaranteed in the
opener; Electabuzz starts Active and Meowth on the Bench.

First victory awards Flying Pikachu. Subsequent victories award one Lightning
Colosseum pack and one Lightning/Colorless Mystery pack. The one-time reward is
shared by both cats and persists across saves.

Run `make -C port catduotest` for encounter/AI/save tests and
`python3 port/frontends/n64/adventure/tools/review_cats.py` to rebuild native captures.
