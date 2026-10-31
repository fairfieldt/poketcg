These twelve PNGs are original ares v148 post-VI exports from the isolated face-options ROM. Each raw 640x240 image should be displayed at 4:3 (for example 640x480 by exact row duplication). No sharpening or synthetic VI filter was applied.

`capture-provenance.json` verifies every PNG against its captured post-VI callback buffer: 153,600 exact RGB pixels per image, 1,843,200 total. The same run completed all twelve native framebuffer dumps and heap measurements before the emulator exited. The production ROM, world manifest and facial texture pools retained their original hashes.

`framing.json` verifies all fifteen camera/model cases fit the exact trial viewports and places every face below the review label. `comparison-control.json` confirms all changed pixels are confined to the projected facial regions (with a three-native-pixel filter margin) and the label. Geometry, poses, lighting, cameras and nonfacial pixels are identical across options.

The review-only debugger hook and framing checker are included. The installed emulator and host settings were unchanged; the capture used the previously verified isolated debug copy and the original ares PNG encoder.
