#!/usr/bin/env python3
"""Exercise the upstream Timeline, effects and Renderer with real QSV media."""
import copy,json,os,subprocess,tempfile,xml.etree.ElementTree as E
import array,math
from pathlib import Path
root=Path(__file__).resolve().parents[2]
work=root/'.deps/native-tests';work.mkdir(parents=True,exist_ok=True)
out=root/'artifacts/native-tests';out.mkdir(parents=True,exist_ok=True)
env={**os.environ,'HOME':str(work/'home'),'OME_AUDIO_BACKEND':'portaudio','FLTK_BACKEND':'x11','OME_HW_DECODE':'required'}
Path(env['HOME']).mkdir(exist_ok=True)
checks=[]
def run(args,**kw):return subprocess.run(list(map(str,args)),env=env,check=True,capture_output=True,**kw)
def project(name,tracks,**attrs):
 p=E.Element('open_movie_editor_project',stereo_output='1',**{k:str(v) for k,v in attrs.items()});E.SubElement(p,'name').text=name
 video=E.SubElement(p,'video_tracks');E.SubElement(p,'audio_tracks')
 for clips in tracks:
  t=E.SubElement(video,'track',name='Video',height='45')
  for clip in clips:t.append(copy.deepcopy(clip))
 path=work/(name+'.vproj');E.ElementTree(p).write(path);return path
T=35280000
source=work/'source_FSBS.mkv'
run(['ffmpeg','-v','error','-y','-f','lavfi','-i','color=red:s=320x180:r=24:d=1','-f','lavfi','-i','color=blue:s=320x180:r=24:d=1','-f','lavfi','-i','sine=frequency=440:sample_rate=48000:duration=1','-filter_complex','[0:v][1:v]hstack[v]','-map','[v]','-map','2:a','-c:v','libx264','-pix_fmt','yuv420p','-c:a','pcm_s16le',source])
base=E.Element('clip',filename=str(source),position='0',length=str(T),trimA='0',trimB='0')
proj=project('base',[[base]])
def render(name,project=proj,encoder='av1_qsv',layout='full',fps='24',extra=(),expect=True):
 path=out/(name+'.mkv');args=[root/'scripts/run-local','--render',project,path,'--encoder',encoder,'--size','320x180','--sbs',layout,'--fps',fps,*extra]
 result=subprocess.run(list(map(str,args)),env=env,capture_output=True,text=True)
 (out/(name+'.log')).write_text(result.stdout+result.stderr)
 assert (result.returncode==0)==expect,(name,result.stdout[-1000:],result.stderr[-3000:])
 if not expect:return path
 info=json.loads(run(['ffprobe','-v','error','-show_streams','-show_format','-of','json',path],text=True).stdout)
 v=next(s for s in info['streams'] if s['codec_type']=='video');assert v['width']==(640 if layout=='full' else 320) and v['height']==180
 checks.append({'case':name,'codec':v['codec_name'],'size':[v['width'],v['height']]})
 return path

def rgb(path,t=0):
 data=run(['ffmpeg','-v','error','-ss',str(t),'-i',path,'-frames:v','1','-f','rawvideo','-pix_fmt','rgb24','-']).stdout
 info=json.loads(run(['ffprobe','-v','error','-select_streams','v:0','-show_entries','stream=width,height','-of','json',path],text=True).stdout)['streams'][0]
 def sample(x,y):return tuple(data[(y*info['width']+x)*3:(y*info['width']+x)*3+3])
 return sample,info,data

def eyes(path,reverse=False):
 sample,info,_=rgb(path);left=sample(info['width']//4,info['height']//2);right=sample(info['width']*3//4,info['height']//2)
 if reverse:left,right=right,left
 assert left[0]>200 and max(left[1:])<35,(path,left)
 assert right[2]>200 and max(right[:2])<35,(path,right)

# Encode and decode with all requested hardware codecs, then reimport into OME.
for encoder in ['av1_qsv','h264_qsv','hevc_qsv','vp9_qsv']:
 path=render(encoder,encoder=encoder);eyes(path)
 clip=copy.deepcopy(base);clip.set('filename',str(path))
 reimport=project('reimport-'+encoder,[[clip]])
 eyes(render('decode-'+encoder,project=reimport,encoder='h264_qsv'))
half=render('half',layout='half');eyes(half)
clip=copy.deepcopy(base);clip.set('filename',str(half))
eyes(render('half-reimport',project=project('half-reimport',[[clip]])))
clip=copy.deepcopy(base);clip.set('stereo_swap','1')
eyes(render('reverse',project=project('reverse',[[clip]])),True)
clip=copy.deepcopy(base);clip.set('trimA',str(T//4));clip.set('trimB',str(T//4));clip.set('length',str(T//2))
short=render('trim-subsecond',project=project('short',[[clip]]),fps='30000/1001')
probe=json.loads(run(['ffprobe','-v','error','-count_frames','-select_streams','v:0','-show_entries','stream=nb_read_frames','-of','json',short],text=True).stdout)
assert int(probe['streams'][0]['nb_read_frames'])==15
# More layers than the upstream fixed-size stack could hold.
eyes(render('twelve-layers',project=project('twelve-layers',[[base] for _ in range(12)]),encoder='h264_qsv'))
# Original Frei0r filter must execute independently for each eye.
clip=copy.deepcopy(base);E.SubElement(clip,'filter',identifier='effect:frei0r:Invert0r',name='Invert0r')
filtered=project('invert',[[clip]])
path=render('frei0r-invert',project=filtered,encoder='h264_qsv');sample,_,_=rgb(path)
a,b=sample(160,90),sample(480,90);assert a[0]<35 and a[1]>200 and a[2]>200,(a,b)
assert b[0]>200 and b[1]>200 and b[2]<35,(a,b)
# Native built-in grade + node graph are serialized and execute.
clip=copy.deepcopy(base)
f=E.SubElement(clip,'filter',identifier='effect:builtin:LiftGammaGain',bypass='0')
for channel in 'rgbv':
 f.set('lift_'+channel,'0.5' if channel=='v' else '1');f.set('gamma_'+channel,'0.5' if channel=='v' else '1');f.set('gain_'+channel,'0.25' if channel=='v' else '1')
grade=project('grade',[[clip]]);path=render('grade',project=grade,encoder='h264_qsv');sample,_,_=rgb(path);assert 50<sample(160,90)[0]<190
clip=copy.deepcopy(base);E.SubElement(clip,'filter',identifier='effect:builtin:NodeFilter',bypass='0')
node=project('node',[[clip]]);eyes(render('node',project=node,encoder='h264_qsv'))
# Titles rendered by the original FLTK title source over the original video track.
title=E.Element('clip',filename='TitleClip',position='0',length=str(T),trimA='0',trimB='0',text='NATIVE OME',x='.5',y='.5',size='60',font='1',color='255')
path=render('title',project=project('title',[[title],[base]]),encoder='h264_qsv');_,_,data=rgb(path)
assert sum(1 for i in range(0,len(data),3) if min(data[i:i+3])>150)>40,'Title did not render'
# Gmerlin native filters, original color curves and connected node graph.
clip=copy.deepcopy(base);f=E.SubElement(clip,'filter',identifier='effect:gmerlin:fv_invert',bypass='0')
for channel in 'rgba':E.SubElement(f,'parameter',name=channel,value='0' if channel=='a' else '1')
gmerlin=project('gmerlin',[[clip]]);path=render('gmerlin',project=gmerlin,encoder='h264_qsv');sample,_,_=rgb(path)
assert sample(160,90)[1]>200 and sample(480,90)[0]>200
clip=copy.deepcopy(base);f=E.SubElement(clip,'filter',identifier='effect:builtin:ColorCurves',bypass='0')
for channel in 'rgbm':
 for key,value in [('p1_x',0),('p1_y',255),('p2_x',255),('p2_y',0)]:f.set(channel+'_'+key,str(value))
curves=project('curves',[[clip]]);eyes(render('curves',project=curves,encoder='h264_qsv'))
clip=copy.deepcopy(base);f=E.SubElement(clip,'filter',identifier='effect:builtin:NodeFilter',bypass='0')
n=E.SubElement(f,'filter',id='0',identifier='effect:builtin:SinkNode');E.SubElement(n,'input',nr='0',input_id='2')
n=E.SubElement(f,'filter',id='1',identifier='effect:builtin:SrcNode')
n=E.SubElement(f,'filter',id='2',identifier='effect:frei0r:Invert0r');E.SubElement(n,'input',nr='0',input_id='1')
node_effect=project('node-invert',[[clip]]);path=render('node-invert',project=node_effect,encoder='h264_qsv');sample,_,_=rgb(path)
assert sample(160,90)[1]>200 and sample(480,90)[0]>200
# Same-track overlap uses the upstream crossfade rather than a replacement sequencer.
second=copy.deepcopy(base);second.set('position',str(T//2));E.SubElement(second,'filter',identifier='effect:frei0r:Invert0r',bypass='0')
path=render('overlap',project=project('overlap',[[base,second]]),encoder='h264_qsv');sample,_,_=rgb(path,.75)
assert min(sample(160,90))>35,sample(160,90)
# Nested original projects keep independent frame stacks and original subsecond duration.
nested=copy.deepcopy(base);nested.set('filename',str(proj));eyes(render('nested',project=project('nested',[[nested]]),encoder='h264_qsv'))
# Padded GAVL rows must not corrupt original effects or the node graph.
odd=work/'padded_FSBS.mkv'
run(['ffmpeg','-v','error','-y','-i',source,'-vf','scale=644:180','-c:v','libx264','-c:a','copy',odd])
clip=copy.deepcopy(base);clip.set('filename',str(odd));E.SubElement(clip,'filter',identifier='effect:frei0r:Invert0r',bypass='0')
path=render('padded-frei0r',project=project('padded-frei0r',[[clip]]),encoder='h264_qsv');sample,_,_=rgb(path)
for y in [20,90,160]:assert sample(160,y)[1]>200 and sample(480,y)[0]>200
# Original audio automation, mute and exact source-free leading silence.
def rms(path,start=0,duration=1):
 data=run(['ffmpeg','-v','error','-ss',str(start),'-i',path,'-t',str(duration),'-vn','-f','f32le','-ar','48000','-ac','2','-']).stdout
 samples=array.array('f',data);return math.sqrt(sum(x*x for x in samples)/len(samples))
assert rms(out/'h264_qsv.mkv',.1,.7)>.03
clip=copy.deepcopy(base);f=E.SubElement(clip,'filter',identifier='filter:builtin:VolumeAutomations')
E.SubElement(f,'automation',x='0',y='0');E.SubElement(f,'automation',x='48000',y='0')
path=render('silent-automation',project=project('silent-automation',[[clip]]),encoder='h264_qsv');assert rms(path)<.00001
clip=copy.deepcopy(base);clip.set('mute','1')
path=render('mute',project=project('mute',[[clip]]),encoder='h264_qsv');assert rms(path)<.00001
# Source-free tail remains silent rather than truncating the export.
clip=copy.deepcopy(base);clip.set('position',str(T//2))
path=render('leading-gap',project=project('gap',[[clip]]),encoder='h264_qsv');sample,_,_=rgb(path);assert max(sample(160,90))<10
assert rms(path,0,.4)<.00001 and rms(path,.6,.7)>.03
# Current Inkscape CLI and the original still-image clip path.
svg=work/'title with spaces.svg';svg.write_text('<svg xmlns="http://www.w3.org/2000/svg" width="320" height="180"><rect width="320" height="180" fill="#00ff00"/></svg>')
clip=copy.deepcopy(base);clip.set('filename',str(svg));path=render('svg',project=project('svg',[[clip]]),encoder='h264_qsv')
sample,_,_=rgb(path);assert sample(160,90)[1]>200 and sample(480,90)[1]>200
# Original libquicktime writer remains operational with atomic finalization.
legacy=out/'legacy.mov'
run([root/'scripts/run-local','--render',proj,legacy,'--encoder','jpeg','--audio','twos','--size','320x180','--sbs','mono'])
info=json.loads(run(['ffprobe','-v','error','-show_streams','-of','json',legacy],text=True).stdout)
assert info['streams'][0]['codec_name']=='mjpeg'
checks.append({'case':'original-libquicktime-export','ok':True})
# Original subtitle and SMIL actions use correct duration and format.
subtitles=project('subtitles',[[title]])
srt=out/'title.srt';run([root/'scripts/run-local','--export-srt',subtitles,srt]);assert '00:00:00,000 --> 00:00:01,000' in srt.read_text(),srt.read_text()
smil=out/'source.smil';run([root/'scripts/run-local','--export-smil',proj,smil]);assert E.parse(smil).find('.//{*}video').get('clipEnd')=='00:00:01.000'
checks.append({'case':'original-subtitle-smil-export','ok':True})
# Missing plugins retain project data and block silent incomplete renders.
clip=copy.deepcopy(base);f=E.SubElement(clip,'filter',identifier='effect:missing:test',custom='preserve');E.SubElement(f,'parameter',name='test',value='42')
missing=project('missing-effect',[[clip]]);preserved=out/'missing-effect.vproj'
run([root/'scripts/run-local','--roundtrip',missing,preserved]);assert E.parse(preserved).find('.//clip/filter/parameter').get('value')=='42'
render('missing-effect',project=missing,expect=False)
clip=copy.deepcopy(base);clip.set('filename',str(work/'missing-file.mkv'));render('missing-file',project=project('missing-file',[[clip]]),expect=False)
assert not (out/'missing-effect.mkv').exists() and not (out/'missing-file.mkv').exists()
checks.append({'case':'missing-plugin-and-media-handling','ok':True})
cancel=out/'cancel.mkv';cancel.write_bytes(b'KEEP EXISTING')
render('cancel',extra=['--cancel-after','2'],expect=False);assert cancel.read_bytes()==b'KEEP EXISTING'
assert not list(out.glob('*.ome-*'))
# Roundtrip original project effects, stereo overrides and node data.
for original in [filtered,grade,node,gmerlin,curves,node_effect]:
 target=out/(original.stem+'-roundtrip.vproj')
 run([root/'scripts/run-local','--roundtrip',original,target])
 a=E.parse(original).findall('.//clip/filter');b=E.parse(target).findall('.//clip/filter')
 assert [n.get('identifier') for n in a]==[n.get('identifier') for n in b][:len(a)]
checks.append({'case':'roundtrip-and-cancel','ok':True})
(out/'results.json').write_text(json.dumps(checks,indent=2));print(json.dumps(checks,indent=2))
