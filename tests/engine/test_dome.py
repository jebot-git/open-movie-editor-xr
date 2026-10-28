#!/usr/bin/env python3
"""Native Wayland original OME, DBusMenu, Dome keyboard routing and live theme."""
import json,os,re,socket,subprocess,sys,tempfile,time,urllib.request,xml.etree.ElementTree as E
from pathlib import Path
import gi
from gi.repository import Gio,GLib
from Xlib import X,XK,display
from Xlib.ext import xtest
from PIL import Image
root=Path(__file__).resolve().parents[2];out=root/'artifacts/native-dome';out.mkdir(parents=True,exist_ok=True)
if os.environ.get('OME_PRIVATE_BUS')!='1':os.execvpe('dbus-run-session',['dbus-run-session','--',sys.executable,*sys.argv],{**os.environ,'OME_PRIVATE_BUS':'1'})
processes=[];logs=[];checks=[]
def check(value,label):
 assert value,label
 checks.append(label);print('PASS',label,flush=True)
def wait(fn):
 deadline=time.monotonic()+25
 while time.monotonic()<deadline:
  try:
   result=fn()
   if result:return result
  except (OSError,GLib.Error,StopIteration,E.ParseError):pass
  time.sleep(.1)
 raise AssertionError('Timed out')
def start(args,name,env,**kw):
 log=(out/(name+'.log')).open('w');logs.append(log)
 p=subprocess.Popen(list(map(str,args)),env=env,stdout=log,stderr=log,start_new_session=True,**kw);processes.append(p);return p
bus=Gio.bus_get_sync(Gio.BusType.SESSION,None)
def dbus(service,path,interface,method,signature='',body=()):
 return bus.call_sync(service,path,interface,method,GLib.Variant('('+signature+')',body) if signature else None,None,Gio.DBusCallFlags.NONE,3000,None).unpack()
def call(service,method,sig='',body=()):return dbus(service,'/org/de_xr/FLTK/Menu1','com.canonical.dbusmenu',method,sig,body)
def service_for(pid):
 for name in dbus('org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus','ListNames')[0]:
  if not name.startswith(':'):continue
  try:
   if dbus('org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus','GetConnectionUnixProcessID','s',(name,))[0]==pid:
    call(name,'GetLayout','iias',(0,-1,[]));return name
  except GLib.Error:pass
def flatten(node):
 yield node
 for child in node[2]:yield from flatten(child)
try:
 with tempfile.TemporaryDirectory(prefix='ome-native-dome-') as tmp:
  tmp=Path(tmp);home=tmp/'home';home.mkdir();runtime=tmp/'runtime';runtime.mkdir(mode=0o700)
  config=home/'.config';(config/'dome').mkdir(parents=True)
  appearance=config/'dome/appearance.json';appearance.write_text(json.dumps({'light':False,'accent':'#65abff','font':'Unifont / Sans-Serif','font_size':16}))
  with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
  rd,wr=os.pipe();start(['Xvfb','-displayfd',str(wr),'-screen','0','1600x1000x24','-nolisten','tcp'],'xvfb',os.environ.copy(),pass_fds=(wr,));os.close(wr)
  with os.fdopen(rd) as pipe:screen=':'+pipe.readline().strip()
  env={**os.environ,'HOME':str(home),'DISPLAY':screen,'XDG_RUNTIME_DIR':str(runtime),'XDG_CONFIG_HOME':str(config),'XDG_STATE_HOME':str(home/'.local/state'),'XDG_DATA_HOME':str(home/'.local/share'),'DOME_PREVIEW_PORT':str(port),'DOME_START_XR':'0','DOME_LOGIN_SESSION':'0','DOME_RENDER_CAPTURE':str(out/'render.ppm'),'VK_DRIVER_FILES':'/usr/share/vulkan/icd.d/lvp_icd.x86_64.json','LIBGL_ALWAYS_SOFTWARE':'1','LIBGL_KOPPER_DISABLE':'true','DOME_RENDERER':'vulkan'}
  for key in ['WAYLAND_DISPLAY','WAYLAND_SOCKET','DOME_BACKEND','FLTK_BACKEND','LD_LIBRARY_PATH']:env.pop(key,None)
  dome=start(['/usr/local/bin/dome-compositor','--backend','nested'],'compositor',env)
  http=urllib.request.build_opener(urllib.request.ProxyHandler({}));base=f'http://127.0.0.1:{port}'
  def state():return json.load(http.open(base+'/api/state',timeout=2))['scene']
  def action(data):http.open(urllib.request.Request(base+'/api/action',data=json.dumps(data).encode(),headers={'Content-Type':'application/json'}),timeout=3).close();time.sleep(.3)
  wait(lambda:state()['source']=='wayland')
  wayland=wait(lambda:re.search(r'Dome Wayland socket: (\S+)',(out/'compositor.log').read_text()))[1]
  client={**env,'WAYLAND_DISPLAY':wayland,'FLTK_BACKEND':'wayland','XDG_CURRENT_DESKTOP':'Dome','OME_AUDIO_BACKEND':'portaudio','DE_XR_APPEARANCE_TRACE':'1'};client.pop('DISPLAY',None)
  args=[os.environ.get('OME_BINARY',str(root/'scripts/run-local')),root/'.deps/native-tests/base.vproj']
  if os.environ.get('OME_BINARY'):
   client['LD_LIBRARY_PATH']=str(root/'.deps/install/lib')+':'+str(root/'.deps/sysroot/usr/lib64')+':/opt/fltk-vr/toolkit/lib'
   client['GMERLIN_PLUGIN_PATH']=str(root/'.deps/install/lib/gmerlin/plugins');client['LIBQUICKTIME_PLUGIN_DIR']=str(root/'.deps/sysroot/usr/lib64/libquicktime')
  if os.getenv('OME_DEBUGGER'):
   client['LD_LIBRARY_PATH']=str(root/'.deps/install/lib')+':'+str(root/'.deps/sysroot/usr/lib64')+':/opt/fltk-vr/toolkit/lib64'
   client['GMERLIN_PLUGIN_PATH']=str(root/'.deps/install/lib/gmerlin/plugins');client['LIBQUICKTIME_PLUGIN_DIR']=str(root/'.deps/sysroot/usr/lib64/libquicktime')
   args=[root/'.deps/sysroot/usr/bin/gdb','-batch','-ex','set debuginfod enabled off','-ex','run','-ex','bt','--args',root/'build-original/openmovieeditor',root/'.deps/native-tests/base.vproj']
  app=start(args,'editor',client)
  try:wid=wait(lambda:next((w['id'] for w in state()['windows'] if w['title']=='Open Movie Editor'),None))
  except AssertionError:
   (out/'failed-state.json').write_text(json.dumps(state(),indent=2))
   result=subprocess.run([root/'.deps/sysroot/usr/bin/gdb','-batch','-ex','set debuginfod enabled off','-ex','bt','-p',str(app.pid)],capture_output=True,text=True,timeout=10)
   (out/'stack.log').write_text(result.stdout+result.stderr);raise
  service=wait(lambda:service_for(app.pid));action({'op':'focus','id':wid});time.sleep(1)
  Image.open(out/'render.ppm').transpose(Image.Transpose.FLIP_TOP_BOTTOM).save(out/'native-editor.png')
  def menu():return list(flatten(call(service,'GetLayout','iias',(0,-1,[]))[1]))
  def item(label):return next(n for n in menu() if n[1].get('label','').replace('_','')==label)
  labels=[n[1].get('label','') for n in menu()];(out/'menu.json').write_text(json.dumps(menu(),indent=2))
  check(all(any(label in x for x in labels) for label in ['File','Edit','Stereo','Tracks','Help']),'Original menus exported by native Wayland client')
  check(item('Redo')[1]['shortcut']==[['Control','Shift','Z']],'Dome Redo convention is Ctrl+Shift+Z')
  check(item('Selected clip stereo...')[1]['enabled']==False,'Selection-dependent stereo menu disabled with no clip selected')
  def clickmenu(label):call(service,'Event','isvu',(item(label)[0],'clicked',GLib.Variant('i',0),0));time.sleep(.4)
  xd=display.Display(screen);host=wait(lambda:next((w for w in xd.screen().root.query_tree().children if w.get_wm_name() and 'Dome' in w.get_wm_name()),None));host.set_input_focus(X.RevertToParent,X.CurrentTime);xd.sync()
  def chord(*names):
   codes=[xd.keysym_to_keycode(XK.string_to_keysym(name)) for name in names]
   for c in codes:xtest.fake_input(xd,X.KeyPress,c)
   for c in reversed(codes):xtest.fake_input(xd,X.KeyRelease,c)
   xd.sync();time.sleep(.35)
  def saved():
   return next((p for p in home.rglob('*.vproj') if E.parse(p).findtext('name')=='base'),None)
  project=wait(saved)
  clickmenu('SBS pair');chord('Control_L','s')
  wait(lambda:E.parse(project).getroot().get('stereo_preview')=='1');check(True,'Exported radio menu action updates original project and Ctrl+S saves it')
  check(item('SBS pair')[1]['toggle-state']==1,'Stereo radio state roundtrips over DBusMenu')
  chord('Control_L','a');wait(lambda:item('Cut')[1]['enabled']);chord('Control_L','x');chord('Control_L','s')
  wait(lambda:len(E.parse(project).findall('.//clip'))==0);check(True,'Ctrl+A and Ctrl+X edit the original timeline through Dome')
  chord('Control_L','z');chord('Control_L','s');wait(lambda:len(E.parse(project).findall('.//clip'))==1)
  chord('Control_L','Shift_L','z');chord('Control_L','s');wait(lambda:len(E.parse(project).findall('.//clip'))==0);check(True,'Undo and Ctrl+Shift+Z Redo restore and remove the original clip')
  chord('Control_L','z');chord('Control_L','s')
  appearance.write_text(json.dumps({'light':True,'accent':'#e07040','font':'Unifont / Sans-Serif','font_size':16}));time.sleep(1)
  clickmenu('Project stereo settings...');wait(lambda:any(w['title']=='Project stereo settings' for w in state()['windows']))
  check('DX_STYLE Project stereo settings' in (out/'editor.log').read_text(),'New modal dialog inherits Dome appearance after live theme update')
  time.sleep(.3)
  Image.open(out/'render.ppm').transpose(Image.Transpose.FLIP_TOP_BOTTOM).save(out/'editor.png')
  chord('Control_L','w');wait(lambda:not any(w['title']=='Project stereo settings' for w in state()['windows']));check(app.poll() is None,'Ctrl+W closes the active modal without closing the editor')
  chord('Control_L','e');wait(lambda:any(w['title']=='Export' for w in state()['windows']));check(True,'Ctrl+E opens the original codec and render dialog')
  check(app.poll() is None,'Original native editor remains alive after editing and modal actions')
  (out/'results.json').write_text(json.dumps({'ok':True,'checks':checks,'headset_tested':False},indent=2))
finally:
 import signal
 for p in reversed(processes):
  try:os.killpg(p.pid,signal.SIGTERM);p.wait(timeout=4)
  except ProcessLookupError:pass
  except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait()
 for f in logs:f.close()
