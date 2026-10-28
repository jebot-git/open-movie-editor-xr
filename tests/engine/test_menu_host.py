#!/usr/bin/env python3
"""Use an isolated registrar to verify real widget collapse, callbacks and fallback."""
import os,subprocess,sys,tempfile,time
from pathlib import Path
if not os.environ.get('OME_MENU_PRIVATE_BUS'):
    os.execvpe('dbus-run-session',['dbus-run-session','--',sys.executable,*sys.argv],dict(os.environ,OME_MENU_PRIVATE_BUS='1'))
from gi.repository import Gio,GLib
bus=Gio.bus_get_sync(Gio.BusType.SESSION,None)
def rpc(service,path,interface,method,sig='()',args=()):
    return bus.call_sync(service,path,interface,method,GLib.Variant(sig,args),None,Gio.DBusCallFlags.NONE,3000,None).unpack()
def registrar(method):
    return rpc('org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus',method,'(su)' if method=='RequestName' else '(s)',('com.canonical.AppMenu.Registrar',4) if method=='RequestName' else ('com.canonical.AppMenu.Registrar',))
with tempfile.TemporaryDirectory(prefix='ome-menu-') as root:
    metrics=Path(root)/'state'
    env=dict(os.environ,HOME=root,XDG_CONFIG_HOME=root+'/.config',XDG_CURRENT_DESKTOP='Dome',FLTK_BACKEND='x11')
    env.pop('WAYLAND_DISPLAY',None)
    child=subprocess.Popen(['xvfb-run','-a',sys.argv[1],str(metrics)],env=env,start_new_session=True)
    def wait(height,clicks=None):
        end=time.monotonic()+8
        while time.monotonic()<end:
            assert child.poll() is None
            if metrics.exists():
                parts=metrics.read_text().split()
                if len(parts)==3 and int(parts[0])==height and (clicks is None or int(parts[1])==clicks):return parts[2]
            time.sleep(.05)
        raise AssertionError('Menu host transition timed out')
    try:
        service=wait(25)
        assert registrar('RequestName')==(1,)
        wait(0)
        layout=rpc(service,'/org/de_xr/FLTK/Menu1','com.canonical.dbusmenu','GetLayout','(iias)',(0,-1,[]))[1]
        action=layout[2][0][2][0][0]
        rpc(service,'/org/de_xr/FLTK/Menu1','com.canonical.dbusmenu','Event','(isvu)',(action,'clicked',GLib.Variant('i',0),0))
        wait(0,1)
        registrar('ReleaseName');wait(25,1)
        print('PASS local fallback, registrar-driven collapse, exported callback, and host-loss restoration')
    finally:
        import signal
        os.killpg(child.pid,signal.SIGTERM);child.wait(timeout=4)
