#!/usr/bin/env python3
"""Verify the staged/installed original app against only its private runtimes."""
import json,os,subprocess,sys,tempfile,xml.etree.ElementTree as E
from pathlib import Path
root=Path(__file__).resolve().parents[2]
prefix=Path(sys.argv[1]).resolve();private=prefix/'libexec/openmovieeditor';binary=private/'openmovieeditor'
checks=[]
with tempfile.TemporaryDirectory(prefix='ome-install-check-') as home:
 env={**os.environ,'HOME':home,'LD_LIBRARY_PATH':str(private/'lib')+':/opt/fltk-vr/toolkit/lib64',
      'GMERLIN_PLUGIN_PATH':str(private/'lib/gmerlin/plugins'),'LIBQUICKTIME_PLUGIN_DIR':str(private/'lib/libquicktime'),
      'OME_AUDIO_BACKEND':'portaudio','FLTK_BACKEND':'x11'}
 def run(args,**kw):return subprocess.run(list(map(str,args)),env=env,check=True,capture_output=True,text=True,timeout=60,**kw)
 version=run([binary,'--version']).stdout.strip();assert '0.0.20090105-qsv-sbs' in version
 libraries=[binary,*private.rglob('*.so'),*private.glob('lib/*.so.*')]
 for file in libraries:
  output=run(['ldd',file]).stdout;assert 'not found' not in output,(file,output)
 main=run(['ldd',binary]).stdout
 assert str(private/'lib/libgmerlin.so.0') in main and str(private/'lib/libquicktime.so.0') in main,main
 checks.append({'case':'private-runtime-dependencies','elf_files':len(libraries),'ok':True})
 # Hide build-tree plugins, so the native plugin test cannot accidentally use them.
 hidden=[]
 try:
  for directory in [root/'.deps/install/lib/gmerlin/plugins',root/'.deps/sysroot/usr/lib64/libquicktime']:
   if directory.exists():
    target=directory.with_name(directory.name+'-verification-hidden');directory.rename(target);hidden.append((directory,target))
  screenshot=root/'artifacts/staged-native-editor.ppm'
  run(['xvfb-run','-a',binary,'--smoke-test',screenshot]);assert screenshot.stat().st_size>100000
  output=root/'artifacts/staged-gmerlin.mkv'
  run(['xvfb-run','-a',binary,'--render',root/'.deps/native-tests/gmerlin.vproj',output,'--encoder','h264_qsv','--size','320x180','--sbs','full'])
  metadata=json.loads(run(['ffprobe','-v','error','-show_streams','-of','json',output]).stdout)
  assert metadata['streams'][0]['width']==640
  checks.append({'case':'native-ui-and-gmerlin-export-with-build-plugins-hidden','ok':True})
 finally:
  for original,temporary in reversed(hidden):temporary.rename(original)
assert (prefix/'share/doc/openmovieeditor/tutorial.html').exists()
run(['desktop-file-validate',prefix/'share/applications/org.openmovieeditor.Editor.desktop'])
checks.append({'case':'desktop-entry-and-original-help','ok':True})
print(json.dumps({'ok':True,'version':version,'prefix':str(prefix),'checks':checks},indent=2))
