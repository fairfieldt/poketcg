# Conversation camera trials

The four angle-only views use the same room, actors, positions, headings, target, camera X/Z and 50-degree FOV. Camera height changes from the prior roughly 42-degree view to 30, 20 and 12 degrees; its distance to the target consequently changes. Two closer 20-degree views additionally change actor spacing and headings for a two-person composition. See camera-settings.json for exact values.

The dialogue panel is the production compositor, with existing Ronald and Mason text excerpts. These scenes are staged camera comparisons, not changed story cutscenes. No production camera values changed.

Ronald’s repair unions the head, hair cap, spikes and fringe into a closed owning exterior. The upper scalp takes the intended hair material, closing real skin gaps. A numerical sliver is collapsed rather than deleted to avoid a pinhole. Face paint, all four animated limbs and the lower torso are preserved. All other 48 runtime records are unchanged. Native winding, skin/exterior checks and front/side/high renders pass. See hair-validation.json and the canonical character report.

DIAGNOSTIC=12 / HIGH_RES=1 builds poketcg-camera-trials.z64. The same ROM runs at 480x360 / 8 MiB and 320x240 / 4 MiB. All 18 original post-VI PNGs are checked against exact callback RGB bytes and gated after their matching native framebuffer headers, six valid callbacks and six stable callbacks. Original PNG bytes are preserved. The source framebuffer is separately available at its native resolution.

Run build_gallery.py after both capture cases exist. The capture command uses ../expansion-pak/capture.py with --output-dir pointing at cases/480 or cases/320. Raw logs/settings and callback buffers remain under the ignored adventure build/expansion-pak/conversation-camera-* directories. The installed ares application and user settings are unchanged.
