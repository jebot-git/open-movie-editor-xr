#!/usr/bin/env python3
"""Exercise the actual preset editor, then independently inspect exported streams."""
import json, os, subprocess, sys, tempfile
with tempfile.TemporaryDirectory(prefix='ome-presets-') as root:
    env=dict(os.environ, HOME=root, XDG_CONFIG_HOME=root+'/.config', FLTK_BACKEND='x11')
    subprocess.run(['xvfb-run','-a',sys.argv[1],root],env=env,check=True)
    for name in ['none','libopus']:
        info=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',root+'/'+name+'.webm']))
        assert 'webm' in info['format']['format_name']
        streams=info['streams'];assert streams[0]['codec_name']=='vp9'
        assert len(streams)==(1 if name=='none' else 2), streams
        if name!='none':assert streams[1]['codec_name']=='opus'
        subprocess.run(['ffmpeg','-v','error','-i',root+'/'+name+'.webm','-f','null','-'],check=True)
print('PASS independently decoded WebM with VP9 only, and VP9 plus Opus')
