# Round 3 — selected final models

Reviewed `native/sheet.png` and all seven underlying actual 320×240 N64 framebuffer captures. The diagnostic uses the production Tiny3D renderer in ares with 4 MiB; portrait and four-angle views are isolated model views, while both conversations and the room use the real encounter renderer.

The larger pale irises remain legible in the portrait and both conversation views. The tuxedo's wider lower cheeks, grey upper muzzle, white chin, chest bib, and white paws form a distinct identity. The tabby's tapered lower face, forehead M, cheek stripes, muted grey-brown coat, and ringed limbs distinguish it without relying on its name. Both have rounded rear skulls, forward muzzle volume, large cat ears, and a visible tail in profile. The five-part gait retains overlap at the shoulders and hips in the side turnaround poses.

Selected round 3 for production. Both models are 728 triangles, below the 900-triangle study limit, with five rigid animation parts and two opaque 32×32 RGBA16 textures. Raw and packed-coordinate audits pass. Skull shells are closed and consistently wound. Native staged bytes agree with the authored export after the production importer's content-addressed texture renaming. There is no authored ground-shadow mesh.

At the distant room camera the two silhouettes and contrasting bib/coat are visible, but individual eye and whisker pixels cannot be judged. These ares captures verify appearance and asset validity; they do not establish physical-console performance. The larger Blender preview is retained only for authoring inspection.
