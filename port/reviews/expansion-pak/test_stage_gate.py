#!/usr/bin/env python3
"""Prove queued previous-stage VI frames cannot pass the capture phase gate."""
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import types

HERE=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='poketcg-vi-gate-') as temp:
    root=Path(temp);config=root/'config.json'
    config.write_text(json.dumps(dict(output=str(root),log=str(root/'log'),wanted=['overworld','lab','board'],
        require_native_frame=True,native_lead_frames=6,stable_frames=6,minimum_valid_frames=16)))
    os.environ['POKETCG_VI_CAPTURE_CONFIG']=str(config)
    sys.modules['lldb']=types.ModuleType('lldb')
    spec=importlib.util.spec_from_file_location('vi_capture_gate',HERE/'post-vi/vi_capture.py')
    hook=importlib.util.module_from_spec(spec);spec.loader.exec_module(hook)
    hook.callback_count=100;hook.valid_count=90
    hook.stage='overworld';hook.captured['overworld']='actual-overworld'
    # Reproduce the old bug's important distinction: an initial queued image
    # differs from the phase image that was actually captured later.
    hook.last_hash='initial-queued-image';hook.stable=40
    hook.consume_line('VIDEO REVIEW lab BEGIN')
    assert hook.prior_stage_hash=='actual-overworld'
    assert hook.stable==0 and hook.last_hash is None
    assert hook.native_ready_valid_count is None
    hook.stable=40
    assert not hook.stage_capture_ready('actual-overworld')
    assert not hook.stage_capture_ready('different-but-still-queued-image')
    hook.consume_line('FRAMEBUFFER overworld 480 360')
    assert hook.native_ready_valid_count is None,'An unrelated native header opened the gate'
    hook.callback_count=120;hook.valid_count=110
    hook.consume_line('FRAMEBUFFER lab 480 360')
    assert hook.native_frame_name=='lab' and hook.native_frame_dimensions==[480,360]
    assert hook.stable==0 and hook.last_hash is None
    hook.stable=6;hook.valid_count=115
    assert not hook.stage_capture_ready('actual-lab'),'Only five native-following callbacks'
    hook.valid_count=116;hook.stable=5
    assert not hook.stage_capture_ready('actual-lab'),'Only five stable callbacks'
    hook.stable=6
    assert not hook.stage_capture_ready('actual-overworld'),'Previous captured image reused'
    assert hook.stage_capture_ready('actual-lab')
    # Capture stores the actual accepted digest, which gates the next stage.
    hook.captured['lab']='actual-lab'
    hook.consume_line('DUEL UI board BEGIN')
    assert hook.prior_stage_hash=='actual-lab' and hook.stable==0
    assert hook.native_frame_name is None
    hook.consume_line('FRAMEBUFFER duel-ui-board 480 360')
    assert hook.native_frame_name=='duel-ui-board'
    events=[json.loads(line) for line in (root/'callback-events.jsonl').read_text().splitlines()]
    assert len([event for event in events if event['event']=='native_ready'])==2
print('STAGE GATE PASS: previous/initial hash mismatch; matching native header; six later valid callbacks; six stable callbacks; marker reset; UI prefix')
