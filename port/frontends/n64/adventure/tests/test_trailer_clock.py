#!/usr/bin/env python3
"""Bounded host regressions for the trailer VI/host/PTS clock helper."""
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from trailer_v2_clock import load_clock

class ClockTests(unittest.TestCase):
    def fixture(self,events):
        directory=Path(self.tmp.name)
        (directory/'capture-metadata.json').write_text(json.dumps({'capture_epoch':1000.0}))
        (directory/'ffmpeg-proof.log').write_text("Input #0, x11grab, from ':2':\n  Duration: N/A, start: 1000.050000\nInput #1, pulse, from 'sink':\n  Duration: N/A, start: 1000.080000\nStream mapping:\n")
        (directory/'marker-times-complete.json').write_text(json.dumps(events))
        return directory
    def setUp(self):self.tmp=tempfile.TemporaryDirectory()
    def tearDown(self):self.tmp.cleanup()
    def event(self,vi,epoch):return {'line':f'TRAILER SHOT test vi={vi} us=123','epoch':epoch,'partial_line_repaired':False}
    def test_points_duplicates_roundtrip_and_extrapolation(self):
        p=self.fixture([self.event(0,1000.1),self.event(60,1001.1),self.event(60,1001.12),self.event(180,1003.2)])
        c=load_clock(p,presentation_latency=.04)
        self.assertEqual(len(c.events),4);self.assertEqual(len(c.points),3)
        self.assertEqual(c.metadata['collapsed_duplicate_vi_observations'],1)
        self.assertAlmostEqual(c.metadata['recorder_startup_seconds'],.05)
        self.assertAlmostEqual(c.raw_at(1),1.09)
        self.assertAlmostEqual(c.raw_at(4),4.24)
        self.assertEqual(c.events[1]['vi'],60);self.assertEqual(c.events[1]['native_seconds'],1)
        for i in range(-20,101):
            t=i/20;self.assertAlmostEqual(c.native_at(c.raw_at(t)),t,places=11)
    def test_reject_backward_vi(self):
        with self.assertRaises(ValueError):load_clock(self.fixture([self.event(10,1000),self.event(9,1001)]))
    def test_reject_nonmonotone_host(self):
        with self.assertRaises(ValueError):load_clock(self.fixture([self.event(0,1001),self.event(60,1000)]))
    def test_ffmpeg_expression(self):
        c=load_clock(self.fixture([self.event(0,1000.1),self.event(60,1001.1),self.event(180,1003.2)]),presentation_latency=.04)
        cmd=['ffmpeg','-hide_banner','-loglevel','info','-f','lavfi','-i','testsrc2=size=16x16:rate=10:duration=5',
             '-vf','settb=1/1000000,setpts='+c.setpts_expression()+',showinfo','-an','-fps_mode','passthrough','-f','null','-']
        p=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        self.assertEqual(p.returncode,0,p.stderr[-3000:])
        observed=[float(m[1]) for m in re.finditer(r'\bn:\s*\d+\s+pts:\s*-?\d+\s+pts_time:([-.0-9]+)',p.stderr)]
        self.assertEqual(len(observed),50)
        for i,t in enumerate(observed):self.assertAlmostEqual(t,c.native_at(i/10),places=5)

if __name__=='__main__':unittest.main()
