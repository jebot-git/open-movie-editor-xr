# Open Movie Editor: FLTK, QSV and SBS extension

This builds the **original Open Movie Editor** from `src/`: its multitrack
editor, media browser, title editor, audio automation, overlaps/crossfades,
Frei0r and Gmerlin effects, node compositor, project library, XML `.vproj`
files, subtitle/SMIL export and codec/preset dialogs. The extension is part
of that application. No replacement frontend or JSON project format is shipped.

Upstream: https://github.com/oracle2025/Open-Movie-Editor, revision
`ca7f2195d257d91bf147f6a66e01e2c33e85a346` (0.0.20090105 source).

## Editing and stereo

The system installation is `/usr/local/bin/openmovieeditor`. Run `openmovieeditor`. The existing media browser, drag/drop timeline,
selection, trim, split, copy/paste and undo/redo remain the editing interface.
**File → Import media** imports at the current cursor into a video track.
**File → Open** imports a `.vproj` into the original project library under
`~/Video Projects`; the supplied source project remains intact. Save and
Save As use that library, following the upstream workflow.

The **Stereo** menu adds project defaults and per-clip input interpretation:
Auto, Mono, Full SBS or Half SBS, with automatic/LR/RL eye order. Clip settings
participate in undo/redo, split/copy/paste and project serialization. Tagged mono
wins over filename hints. Metadata and SBS/FSBS/HSBS filename markers are
recognized; width alone does not imply stereo. Choose a layout explicitly for
ambiguous older files. Half-SBS exports carry SAR 2; MKV also records stereo mode
and the full/half layout. Use MKV for reliable metadata interchange. For MP4,
use an `_FSBS`/`_HSBS` filename or select the layout explicitly in the player.

The original video pane offers auto, SBS pair, left eye, right eye, red/cyan
anaglyph and original packed source views. Each stereo eye runs through an
independent instance of the original timeline, filters and node graph; titles
and ordinary 2D sources are composited into both eyes. Scaling fits each eye
independently with borders. The native GL preview handles dynamic texture sizes
and arbitrary layer counts rather than the old fixed ten 2048px textures.

**Convert 2D to SBS (Arc)** runs the installed `arc-sbs convert` asynchronously,
then imports its result into the original timeline. It converts the selected
source before timeline edits. It uses Arc's installed OpenVINO model and
conversion settings; the editor does not download a model. Cancellation stops
only that conversion's process group.

**View edited timeline in mpvr / XR** renders the edited timeline, including
its effects, then launches the installed `mpvr` with an explicit SBS layout and
QSV-copy decoding. mpvr retains its existing desktop/DomeD/XR routing. Preview
files are retained under `$XDG_CACHE_HOME/openmovieeditor/previews` (default
`~/.cache`); remove them to reclaim space. No desktop session is restarted.

## Encoding and hardware

The original render/preset and codec option dialogs now include Intel QSV AV1,
H.264, H.265/HEVC and VP9, plus explicit software H.264. Libquicktime codecs and
presets are retained. Output size in a QSV preset describes an eye's display
size: full SBS doubles stored width; half SBS retains width and signals SAR 2.
Select the output layout in the render dialog. VP9 uses bitrate/low-power mode;
the other QSV codecs use global quality. Choose MKV for VP9.

FFmpeg/libav performs hardware decoding and encoding on Intel QSV. **Original
GAVL/Frei0r/Gmerlin effects and CPU compositing require downloaded host frames**;
the native editor is not a zero-copy GPU effects pipeline. The working image
format is 8-bit RGBA, encoded as NV12 for QSV. HDR colour management and HDR
export are not implemented. Hardware support depends on the Intel device and
driver; the four requested codecs were exercised on the local Arc GPU.

The default node is `/dev/dri/by-path/pci-0000:03:00.0-render` for this workspace.
Override with `OME_QSV_DEVICE=/dev/dri/renderD...`; codec options also expose the
node. Decoding defaults to QSV with software fallback during decoder setup.
`OME_HW_DECODE=required` requires a supported hardware decoder;
`OME_HW_DECODE=off` selects software decoding. Encoding failures are reported,
without silently selecting a software encoder. `OME_AUDIO_BACKEND=portaudio`
skips JACK; otherwise JACK is tried with `JackNoStartServer` before PortAudio.

Native FFmpeg renders publish output atomically after successful finalization.
Failure/cancel removes temporary output and preserves an existing destination.
The original render dialog asks before overwriting a destination. Audio is
mixed by the original engine at 48 kHz with sample-accurate output scheduling;
video cuts are quantized to the selected rational output frame rate.

## DomeD conventions

The same patched FLTK 1.4.5 as DomeD is the default and only built frontend.
Arc's shared appearance is reused. The native DBusMenu/Wayland appmenu bridge
is included in this repository so another project's source changes cannot
change the editor's menu contract.
The original File, Edit, Tracks, View, Stereo and Help commands are exported
with enabled states, shortcuts and radio/check state. DomeD supplies its
universal Window commands. The main menu remains usable outside DomeD.

Standard bindings: Ctrl+N/O/S/Shift+S/I/E/W/Q, Ctrl+Z, **Ctrl+Shift+Z**, Ctrl+X/C/V,
Delete and Ctrl+A. Edit commands operate on a focused text input before the
timeline. Existing bare-key timeline tools and playback function keys remain;
the editor does not register compositor Super shortcuts. Dome appearance follows
`$XDG_CONFIG_HOME/dome/appearance.json`, including live palette/font changes,
custom timeline labels, and newly opened dialogs. Legacy application skin
commands do not override the Dome theme.

## Release build and user installation

Required host development libraries: current FFmpeg/libav (8.x API), the patched
FLTK 1.4.5, GAVL, libsndfile, libsamplerate, JACK, PortAudio, GL/GLU, GLib/GIO,
fontconfig, nlohmann-json and Wayland/X11. Gmerlin 1.2 and libquicktime are kept
in a private prefix to preserve upstream functionality. On Fedora/Nobara 44:

```sh
scripts/bootstrap-legacy
cmake -S . -B build-original -G Ninja \
  -DFLTK_DIR=/opt/fltk-vr/toolkit/share/fltk \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local" -DCMAKE_INSTALL_LIBEXECDIR=libexec
cmake --build build-original -j8
DESTDIR="$PWD/stage" cmake --install build-original
# Verify staged ELF dependencies and staged runtime before installing:
cmake --install build-original
update-desktop-database "$HOME/.local/share/applications"
update-mime-database "$HOME/.local/share/mime"
```

Build as the regular user. The bootstrap extracts pinned RPMs and builds
Gmerlin privately; it does not run sudo or alter system libraries. System
prerequisites must already be present. Private runtime libraries/plugins are
installed beneath `~/.local/libexec/openmovieeditor`. FLTK remains shared with
DomeD. `scripts/run-local` runs the build with private dependency paths.

## System installation

Build and stage as the regular user, then authenticate only for installation:

```sh
cmake -S . -B build-system -G Ninja \
  -DFLTK_DIR=/opt/fltk-vr/toolkit/share/fltk \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
  -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_INSTALL_LIBEXECDIR=libexec \
  -DCMAKE_INSTALL_DOCDIR:STRING=share/doc/openmovieeditor
cmake --build build-system -j8
DESTDIR="$PWD/stage-system" cmake --install build-system
python tests/engine/test_install.py "$PWD/stage-system/usr/local"
sudo sh scripts/install-system.sh
python tests/engine/test_install.py /usr/local
```

The system launcher, application entry, help and private compatibility libraries
are beneath `/usr/local`; FLTK is shared from `/opt/fltk-vr/toolkit`. System
installation does not require building as root or restarting the desktop.

## Validation and CLI

```sh
xvfb-run -a python tests/engine/test_native.py
python tests/engine/test_dome.py
scripts/run-local --render film.vproj output.mkv \
  --encoder av1_qsv --size 1920x1080 --fps 30000/1001 --sbs full
scripts/run-local --roundtrip film.vproj copy.vproj
```

CLI rendering uses the same original Timeline and Renderer as the UI; its title
renderer requires a display (use Xvfb when headless). The native engine tests
exercise hardware encode/reimport for all four QSV codecs, stereo eyes, half-SBS
metadata, effects, titles, layers, trims, gaps, round trips and cancellation.
The isolated nested-DomeD test exercises the actual native Wayland client,
DBusMenu actions, keyboard edits, modal dialogs and appearance updates.
Results and logs are under `artifacts/native-tests` and `artifacts/native-dome`.
Headset presentation still needs a physical headset check.

## Command reference

The [openmovieeditor(1) source manual](doc/man/openmovieeditor.1) documents
render options, environment variables, output semantics and project locations.
Read it from the checkout with `man -l doc/man/openmovieeditor.1`.
The current CMake install rules do not install that new source manual.

`openmovieeditor --help` and `--version` exit before starting the GUI.
The remaining commands use the original editor engine and require a display;
use `xvfb-run -a` for batch work without a desktop. Examples:

```sh
openmovieeditor --roundtrip film.vproj copy.vproj
xvfb-run -a openmovieeditor --render film.vproj film.mkv \
  --encoder av1_qsv --size 1920x1080 --fps 30000/1001 --sbs full
xvfb-run -a openmovieeditor --export-srt film.vproj titles.srt --track 0
xvfb-run -a openmovieeditor --export-smil film.vproj film.smil --track 0
```

Render defaults are AV1 QSV, AAC audio, 1280×720 per eye, 24 fps, quality 23
and bitrate 8000000 bits/s. Without `--sbs`, the project output layout is used.
`--device` overrides `OME_QSV_DEVICE` for that invocation. Codec availability
still depends on the configured FFmpeg build and hardware. The source manual
also distinguishes the original project library from temporary XR preview files.

## Presets and menu corrections (2026-10-28)

The preset editor includes **WebM (.webm)** with compatible VP9/AV1 video and
Opus/Vorbis audio choices. The explicit container survives preset copying and
XML save/reopen, and export uses the WebM muxer and `.webm` suffix. Existing
presets without a container attribute retain their earlier filename-based
FFmpeg container selection. Unsupported codec/container combinations report an
export failure and retain any existing destination.

Choose **No audio** in the Audio Codec list to export video without an audio
stream. Audio options/bitrate controls become inactive; the choice survives
preset save/reopen. The native FFmpeg and retained libquicktime paths both honor
it. WebM Opus finalization also handles an empty resampler flush correctly.

The original main menu now uses `DesktopMenuBar`, retaining shortcuts while
DomeD draws the commands. It collapses only after the registrar and the window
association exist, and returns if the menu host disappears. The project chooser
remains a separate control in the editor. The system launcher retains `dome-link`
routing when available.

`ctest --test-dir build-fixes-release --output-on-failure` exercises the actual
preset editor, copy/XML/reopen and independently decoded VP9-only/VP9+Opus
WebM exports, plus real menu collapse, command dispatch and host-loss fallback
on a private bus/display. `OME_BINARY=/path/to/openmovieeditor python3
tests/engine/test_dome.py` checks the native Wayland editor under an isolated
DomeD, including exported commands, radio states, editing shortcuts and dialogs.
