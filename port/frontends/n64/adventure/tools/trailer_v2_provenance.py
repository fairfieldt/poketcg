#!/usr/bin/env python3
"""Collect the private capture provenance without changing live settings."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
from trailer_edit_v2 import ADV, CAPTURES, HOST, LOCAL, PROFILES, ROOT
from trailer_v2_clock import load_clock


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def collect():
    remote_script = '''from pathlib import Path
import hashlib,json,subprocess
base=Path('/home/deck/.cache/poketcg-trailer-20261031')
names=NAMES
def digest(path):
 h=hashlib.sha256()
 with path.open('rb') as f:
  for chunk in iter(lambda:f.read(8*1024*1024),b''):h.update(chunk)
 return h.hexdigest()
preset=Path.home()/'.var/app/dev.ares.ares/data/ares/Shaders/crt/crt-guest-advanced.slangp'
records={}
for name in names:
 w=base/name
 records[name]={'rom_sha256':digest(w/'capture.z64'),'raw_master_sha256':digest(w/'proof.mkv'),'raw_master_bytes':(w/'proof.mkv').stat().st_size,'settings':(w/'settings.bml').read_text()}
result={'profiles':records,'preset_path':str(preset),'preset_text':preset.read_text(),'preset_sha256':digest(preset),'ares_flatpak_info':subprocess.check_output(['flatpak','info','dev.ares.ares'],text=True),'remaining_private_sinks':[s for s in subprocess.check_output(['pactl','list','short','sinks'],text=True).splitlines() if 'poketcg_trailer' in s]}
print(json.dumps(result,indent=2))
'''.replace('NAMES', repr([name for profiles in PROFILES.values() for name in profiles.values()]))
    remote = json.loads(subprocess.check_output(['ssh','-o','BatchMode=yes',HOST,'python3 -'], input=remote_script.encode()))
    records = []
    for source in ('intro','ronald','clubs','amy'):
        pair = []
        for variant in ('crt','none'):
            name = PROFILES[variant][source]
            directory = CAPTURES/name
            clock = load_clock(directory, presentation_latency=.04)
            markers = [s for s in (directory/'ares-proof.log').read_text().splitlines() if s.startswith('TRAILER ')]
            record = {'source': source, 'variant': variant, 'profile': name,
                **remote['profiles'][name], 'clock': clock.metadata,
                'semantic_markers': markers,
                'semantic_vi_trace': [re.sub(r' us=\d+','',line) for line in markers],
                'timed_marker_observations':len(clock.events),
                'evidence_sha256': {n:sha(directory/n) for n in ('ares-proof.log','marker-times-complete.json','capture-metadata.json')}}
            records.append(record);pair.append(record)
        assert pair[0]['rom_sha256']==pair[1]['rom_sha256'], source+' pair used different ROMs'
        # Source timeline and controller events must be the same in each take.
        assert pair[0]['semantic_vi_trace']==pair[1]['semantic_vi_trace'], source+' native VI/event trace changed'
    assert not remote['remaining_private_sinks']
    result = {'production_release_with_coin_fix':'6961e817',
        'format':{'internal':[480,360],'ram_mib':8,'recorded_output':[1440,1080],'recorded_fps':60,'fps_overlays':False},
        'actual_output':'Private Ares post-shader GL window captured directly. None is a separate shader-disabled emulator run, not inverse/post-processing.',
        'paired_roms_and_native_events_identical':True,'common_audio_encoded_once':True,
        'canonical_player_name':'Mark','profiles':records,
        'preset':{k:v for k,v in remote.items() if k!='profiles'},
        'fixtures':{'intro':'Real new-game script; automatic fresh ACKs; stops at first table before Text1511 is drawn.',
          'ronald':'Untouched legal mid-duel checkpoint; actual Slap/KO/Prize/Zapdos continuation.',
          'clubs':'Post-tutorial room warps, then collision-checked legal walking; Lightning, Fire, Water.',
          'amy':'Untouched legal final-Pokemon checkpoint; actual12 controller requests and natural player win, followed directly by source Water Medal reward.',
          'hall':'Progression warp for montage; source NPC flags/positions retained with current authored card/room models.',
          'title':'Actual animated title screen with the current logo; original closing music phrase edited independently.'},
        'safety':'Private profiles/ROMs/saves, unique verified audio streams and temporary sinks; live Steam session and user diaries were not used.'}
    out = LOCAL/'provenance';out.mkdir(exist_ok=True)
    (out/'capture.json').write_text(json.dumps(result,indent=2)+'\n')
    builds={'intro':'trailer-v2-intro','clubs':'trailer-v2-clubs','amy':'trailer-v2-fixed-amy','ronald':'trailer-ronald'}
    for source, folder in builds.items():
        path = ADV/'build'/folder/'build.json'
        (out/(source+'-build.json')).write_bytes(path.read_bytes())
    print(out)


if __name__ == '__main__':
    collect()
