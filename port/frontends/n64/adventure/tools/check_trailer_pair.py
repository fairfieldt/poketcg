#!/usr/bin/env python3
"""Check matching trailer formats, completed frames and identical shared audio."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import subprocess


def run(args):
    return subprocess.check_output(args)


def boxes(path):
    found = []
    with path.open('rb') as f:
        while True:
            at = f.tell(); header = f.read(8)
            if not header: break
            size, name = struct.unpack('>I4s', header)
            if size == 1: size = struct.unpack('>Q', f.read(8))[0]
            if not size: size = path.stat().st_size-at
            found.append({'type': name.decode(), 'offset': at, 'size': size})
            f.seek(at+size)
    return found


def check(directory):
    directory = Path(directory)
    plan = json.loads((directory/'timeline.json').read_text())
    expected_frames = round(plan['duration']*60)
    reports = []
    for name in ('poketcg64-trailer.mp4', 'poketcg64-trailer-no-crt.mp4'):
        path = directory/name
        probe = json.loads(run(['ffprobe', '-v', 'error', '-show_streams', '-show_format', '-of', 'json', str(path)]))
        (directory/(name+'.ffprobe.json')).write_text(json.dumps(probe, indent=2)+'\n')
        video = next(s for s in probe['streams'] if s['codec_type']=='video')
        audio = next(s for s in probe['streams'] if s['codec_type']=='audio')
        assert (video['width'], video['height'], video['codec_name'], video['pix_fmt'], video['r_frame_rate']) == (1440,1080,'h264','yuv420p','60/1')
        assert int(video['nb_frames']) == expected_frames
        assert audio['codec_name']=='aac' and audio['channels']==2 and audio['sample_rate']=='48000'
        delta = abs(float(video['duration'])-float(audio['duration']))
        assert delta < 1/60
        layout = boxes(path)
        assert next(b['offset'] for b in layout if b['type']=='moov') < next(b['offset'] for b in layout if b['type']=='mdat')
        packet = run(['ffmpeg','-v','error','-i',str(path),'-map','0:a:0','-c:a','copy','-f','adts','-'])
        pcm = run(['ffmpeg','-v','error','-i',str(path),'-map','0:a:0','-c:a','pcm_s16le','-f','s16le','-'])
        reports.append({'file': name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
            'bytes': path.stat().st_size, 'video_frames': int(video['nb_frames']),
            'video_duration': float(video['duration']), 'audio_duration': float(audio['duration']),
            'av_duration_difference_seconds': delta, 'faststart': True,
            'compressed_audio_adts_sha256': hashlib.sha256(packet).hexdigest(),
            'decoded_stereo_pcm16_sha256': hashlib.sha256(pcm).hexdigest()})
    assert reports[0]['compressed_audio_adts_sha256']==reports[1]['compressed_audio_adts_sha256']
    assert reports[0]['decoded_stereo_pcm16_sha256']==reports[1]['decoded_stereo_pcm16_sha256']
    assert reports[0]['video_frames']==reports[1]['video_frames']
    result = {'passed': True, 'expected_duration': plan['duration'], 'expected_frames': expected_frames,
              'identical_compressed_audio': True, 'identical_decoded_audio': True, 'files': reports}
    (directory/'pair-validation.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    check(parser.parse_args().directory)
