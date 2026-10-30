# Generates the card art table of the N64 frontend from the disassembly's
# src/gfx.asm: for every card picture, the value the card data stores in its
# Gfx field (the `gfx` macro: offset from CardGraphics / 8, banks counted as
# $4000 bytes) and the picture's file name (src/gfx/cards/<name>.png).
#
#   awk -f cardart.awk src/gfx.asm > cardart_table.h
function hex(s,    i, c, v) {
	v = 0
	s = tolower(s)
	for (i = 1; i <= length(s); i++) {
		c = index("0123456789abcdef", substr(s, i, 1))
		if (c == 0)
			break
		v = v * 16 + c - 1
	}
	return v
}
BEGIN {
	sec = -1
	n = 0
	print "/* Generated from src/gfx.asm by tools/cardart.awk. Do not edit. */"
	print "static const struct CardArt { uint16_t gfx; const char *name; } kCardArt[] = {"
}
/^SECTION "Card Gfx [0-9]+"/ {
	match($0, /Card Gfx [0-9]+/)
	sec = substr($0, RSTART + 9, RLENGTH - 9) - 1
	off = 0
	next
}
/^SECTION/ { sec = -1; next }
# the virtual console build patches some palettes: count the regular build only
/^IF DEF\(_VC\)/ { vc = 1; next }
/^ELSE/ { vc = 0; next }
/^ENDC/ { vc = 0; next }
vc { next }
sec >= 0 && /INCBIN "gfx\/cards\/[a-z0-9_]+\.2bpp"/ {
	match($0, /cards\/[a-z0-9_]+\.2bpp/)
	name = substr($0, RSTART + 6, RLENGTH - 11)
	printf "\t{ 0x%04x, \"%s\" },\n", (sec * 16384 + off) / 8, name
	off += 768
	n++
	next
}
sec >= 0 && /INCBIN "gfx\/cards\/[a-z0-9_]+\.pal"/ { off += 8; next }
sec >= 0 && /^[ \t]*ds[ \t]+\$[0-9a-fA-F]+/ {
	match($0, /\$[0-9a-fA-F]+/)
	off += hex(substr($0, RSTART + 1, RLENGTH - 1))
	next
}
END {
	print "};"
	printf "#define NUM_CARD_ART %d\n", n
}
