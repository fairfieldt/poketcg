#!/usr/bin/env python3
"""Align private capture PTS to the recorded native VI/60 clock.

Use on the original, unshifted video timestamps before trim/STARTPTS resets:
    clock = load_clock(directory, presentation_latency=.04)
    vf = 'setpts=' + clock.setpts_expression()
    raw_cut = clock.raw_at(native_vi / 60)

The expression only changes timestamps; it does not generate/interpolate
frames. A subsequent CFR/fps filter may hold/drop recorded frames. Marker UART
observation, 60Hz sampling and display latency leave roughly one-frame phase
uncertainty: this is semantic edit alignment, not pixel-perfect synchronization.
"""
from bisect import bisect_right
from dataclasses import dataclass
import argparse
import json
import math
from pathlib import Path
import re


@dataclass(frozen=True)
class CaptureClock:
    directory: Path
    points: tuple  # (native_seconds, observed_raw_seconds)
    events: tuple
    metadata: dict
    presentation_latency: float = 0.0

    @staticmethod
    def _interpolate(points, value):
        if not math.isfinite(value):raise ValueError('Clock coordinate must be finite')
        index=max(0,min(len(points)-2,bisect_right([p[0] for p in points],value)-1))
        (x0,y0),(x1,y1)=points[index:index+2]
        return y0+(value-x0)*(y1-y0)/(x1-x0)

    def raw_at(self, native_seconds):
        """Raw source PTS seconds for native VI/60; extrapolate endpoint slopes."""
        return self._interpolate(self.points,float(native_seconds))+self.presentation_latency

    def native_at(self, raw_seconds):
        """Inverse mapping, useful for checks; source PTS must retain its origin."""
        inverse=tuple((raw,native) for native,raw in self.points)
        return self._interpolate(inverse,float(raw_seconds)-self.presentation_latency)

    def setpts_expression(self, *, escape_commas=True):
        """FFmpeg expression in timestamp units, ready after `setpts=`.

        Default commas are escaped for an unquoted filtergraph/filter-script
        value. With escape_commas=False, quote the whole expression yourself.
        Preserve input PTS: do not apply PTS-STARTPTS before this expression.
        """
        points=[(raw+self.presentation_latency,native) for native,raw in self.points]
        def linear(i):
            (r0,n0),(r1,n1)=points[i:i+2];slope=(n1-n0)/(r1-r0)
            return f'({n0:.12g}+((PTS*TB)-({r0:.12g}))*{slope:.12g})'
        expression=linear(len(points)-2)
        for i in reversed(range(len(points)-2)):
            expression=f'if(lt(PTS*TB,{points[i+1][0]:.12g}),{linear(i)},{expression})'
        expression=f'({expression})/TB'
        return expression.replace(',',r'\,') if escape_commas else expression


def load_clock(directory, *, presentation_latency=0.0):
    """Read marker observations and actual x11grab input epoch for any profile."""
    directory=Path(directory)
    if not math.isfinite(presentation_latency):raise ValueError('Presentation latency must be finite')
    capture=json.loads((directory/'capture-metadata.json').read_text())
    observations=json.loads((directory/'marker-times-complete.json').read_text())
    log=(directory/'ffmpeg-proof.log').read_text()
    # Scope strictly to video input0; Pulse audio has its own nearby epoch.
    block=re.search(r'Input #0, x11grab,[\s\S]*?(?=\nInput #\d|\nStream mapping:|\Z)',log)
    match=re.search(r'\bstart:\s*([0-9]+(?:\.[0-9]+)?)',block.group(0)) if block else None
    if not match:raise ValueError('Actual Input #0/x11grab start epoch is absent from FFmpeg log')
    start=float(match[1]);events=[];chosen=[];duplicate_spans=[];last_vi=-1
    for observation in observations:
        line=observation.get('line','');vi_match=re.search(r'\bvi=(\d+)\b',line)
        if not vi_match:continue
        vi=int(vi_match[1]);epoch=float(observation['epoch'])
        if not math.isfinite(epoch):raise ValueError('Nonfinite marker epoch')
        if vi<last_vi:raise ValueError(f'Native VI moved backward: {last_vi} -> {vi}')
        event={**observation,'line':line,'vi':vi,'native_seconds':vi/60,
               'epoch':epoch,'observed_raw_seconds':epoch-start}
        events.append(event)
        if vi==last_vi:
            # First observation best avoids later work within the same VI;
            # keep every original event for the editor's semantic boundaries.
            duplicate_spans.append(abs(epoch-chosen[-1]['epoch']))
            if epoch<chosen[-1]['epoch']:chosen[-1]=event
        else:chosen.append(event)
        last_vi=vi
    if len(chosen)<2:raise ValueError('At least two distinct native VI points are required')
    points=tuple((e['native_seconds'],e['observed_raw_seconds']) for e in chosen)
    if any(b[1]<=a[1] for a,b in zip(points,points[1:])):
        raise ValueError('Distinct VI observations do not form a strictly monotone host clock')
    slopes=[(b[1]-a[1])/(b[0]-a[0]) for a,b in zip(points,points[1:])]
    metadata={'capture':capture,'x11_input_start_epoch':start,
      'capture_process_epoch':capture.get('capture_epoch'),
      'recorder_startup_seconds':start-float(capture['capture_epoch']) if 'capture_epoch' in capture else None,
      'presentation_latency_seconds':presentation_latency,'observation_count':len(events),
      'distinct_vi_points':len(points),'collapsed_duplicate_vi_observations':len(events)-len(points),
      'largest_duplicate_observation_span_seconds':max(duplicate_spans,default=0),
      'native_range_seconds':[points[0][0],points[-1][0]],
      'observed_raw_range_seconds':[points[0][1],points[-1][1]],
      'segment_raw_seconds_per_native_second_range':[min(slopes),max(slopes)],
      'duplicate_rule':'Earliest epoch for each VI; semantic events remain intact',
      'extrapolation':'Continue the nearest endpoint segment outside the observed range',
      'uncertainty':'Approximately one60Hz frame from marker observation/display phase; latency offset is an explicit assumption',
      'timestamp_contract':'Original raw video PTS seconds, before trim or STARTPTS normalization'}
    return CaptureClock(directory,points,tuple(events),metadata,presentation_latency)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path)
    parser.add_argument('--latency',type=float,default=0)
    parser.add_argument('--expression',action='store_true')
    args=parser.parse_args();clock=load_clock(args.directory,presentation_latency=args.latency)
    if args.expression:print(clock.setpts_expression())
    else:print(json.dumps({'metadata':clock.metadata,'points':clock.points,'events':clock.events},indent=2))
