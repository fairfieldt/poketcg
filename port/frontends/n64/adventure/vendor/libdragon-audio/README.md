# Project-local WAV64 VADPCM decoder fix

This directory copies the VADPCM decoder and its four private support headers from [libdragon preview revision 12bba066](https://github.com/DragonMinded/libdragon/tree/12bba06615ce9e89185947bbbeba41e290ec0c23). `UPSTREAM.json` records the original SHA256 of each file. The upstream public-domain license is preserved in `LICENSE`. The installed SDK is unchanged.

The adventure Makefile links this object before libdragon. It provides all three VADPCM codec entry points, so the archive decoder object is not extracted. The SDK mixer, RSP microcode, WAV64 loader and assets are unchanged. Public structure sizes and relevant samplebuffer offsets have compile-time guards. The opaque private state layouts are pinned to this SDK revision and must be reviewed when updating libdragon.

The upstream Huffman scratch estimate depends on the destination block size and can be too small for short reads at loop boundaries. An actual 90-second music09 regression reaches the upstream assertion. This copy instead uses independent 16-byte-aligned scratch, sized to the worst case of two 8-bit Huffman symbols per decoded VADPCM byte, plus an unaligned first byte and 32-bit refill lookahead. Maximum source data is 846 bytes, so the allocation is 1712 bytes. The per-call read uses the corresponding smaller bound. It never overlaps the in-place VADPCM input or RSP output.

EOF lookahead bytes are zeroed. A new guard verifies that bits actually consumed came from bytes actually read; truncated streams still assert. RSP scheduling and destination cache invalidation remain as upstream.

Only `wav64_vadpcm.c` differs from upstream. Compare it directly with the pinned source. The four local support headers remain byte-identical. Diagnostic 17 exercises the real mixer and RSP decoder, exact reported pause position 652536, repeated fanfares/SFX, complete music09 loops, the original tutorial reward VM, all 34 loaded map populations in 4/8 MiB, and every one of the 127 shipped assets. Run `tools/check_audio_resume.py` against a `DIAGNOSTIC=17` ROM. Its output uses private ares settings and saves.
