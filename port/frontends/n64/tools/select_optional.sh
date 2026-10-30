#!/bin/sh
# Chooses which optional engine objects (port/src/effects/*.c, port/src/ai/*.c,
# still being ported by other agents) go into the link.
#
#   select_optional.sh OUT STAMP... -- LINK-PREFIX... -- LINK-SUFFIX...
#
# Each STAMP (build/.../foo.ok) says whether foo.c compiled ("ok") into foo.o
# next to it. The script then links LINK-PREFIX <objects> LINK-SUFFIX and,
# while that fails, drops every object the linker blames (undefined references
# to helpers nobody has ported yet, duplicate definitions; for a duplicate only
# the redefining object is dropped) and tries again. If the linker names none
# of them, all are dropped. The weak stubs in the engine stand in for whatever
# is excluded.
#
# Writes OUT.lst (objects to link, one per line), OUT.log (report and the
# linker errors) and OUT.info (short summary for the title screen, plus
# "ai=0|1": whether any AI object is linked).
set -u
out=$1
shift
stamps=""
while [ $# -gt 0 ] && [ "$1" != "--" ]; do
	stamps="$stamps $1"
	shift
done
[ $# -gt 0 ] && shift
pre=""
while [ $# -gt 0 ] && [ "$1" != "--" ]; do
	pre="$pre $1"
	shift
done
[ $# -gt 0 ] && shift
post="$*"

: > "$out.log"
cands=""
excluded=""
for s in $stamps; do
	o=${s%.ok}.o
	if [ "$(cat "$s" 2>/dev/null)" = ok ] && [ -f "$o" ]; then
		cands="$cands $o"
	else
		excluded="$excluded $o:compile"
		echo "excluded ${o}: does not compile (${s%.ok}.err)" >> "$out.log"
	fi
done

blame() {
	# objects of $cands named in the linker errors (for "multiple definition"
	# lines only the part before it, i.e. the redefining object)
	awk -v cands="$cands" '
		BEGIN { n = split(cands, c, " ") }
		{
			line = $0
			i = index(line, "multiple definition")
			if (i) line = substr(line, 1, i)
			for (k = 1; k <= n; k++) if (index(line, c[k])) blamed[c[k]] = 1
		}
		END { for (k in blamed) print k }' "$1"
}

status=1
while :; do
	# shellcheck disable=SC2086
	if $pre $cands $post > "$out.link" 2>&1; then
		status=0
		break
	fi
	if [ -z "$cands" ]; then
		echo "the engine does not link even without the optional objects:" >> "$out.log"
		cat "$out.link" >> "$out.log"
		cat "$out.link" >&2
		break
	fi
	bad=$(blame "$out.link")
	if [ -z "$bad" ]; then
		bad=$cands
		echo "link failed without naming an optional object: excluding all of them" >> "$out.log"
	fi
	keep=""
	for o in $cands; do
		drop=0
		for b in $bad; do [ "$o" = "$b" ] && drop=1; done
		if [ $drop = 1 ]; then
			excluded="$excluded $o:link"
			echo "excluded ${o}: does not link:" >> "$out.log"
			grep -F "$o" "$out.link" | head -n 20 >> "$out.log"
		else
			keep="$keep $o"
		fi
	done
	cands=$keep
done
rm -f "$out.link"

: > "$out.lst"
for o in $cands; do echo "$o" >> "$out.lst"; done

# summary
count() { echo "$1" | tr ' ' '\n' | grep -c "/$2/" ; }
ne=$(count "$cands" effects)
na=$(count "$cands" ai)
xe=$(count "$excluded" effects)
xa=$(count "$excluded" ai)
{
	echo "effects: $ne/$((ne + xe)) linked  ai: $na/$((na + xa)) linked"
	for x in $excluded; do
		o=${x%:*}
		why=${x##*:}
		f=$(echo "$o" | sed 's|.*/engine/||; s|\.o$|.c|')
		echo "excluded: $f ($why)"
	done
	if [ "$na" -gt 0 ]; then echo "ai=1"; else echo "ai=0"; fi
} > "$out.info"
for o in $cands; do echo "linked ${o}" >> "$out.log"; done
sed 's/^/    [OPT] /' "$out.info"
exit $status
