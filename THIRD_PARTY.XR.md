# Extension provenance and dependencies

- Open Movie Editor: Richard Spindler and upstream contributors,
  GPL-2.0-or-later. Original sources and notices are retained.
- New `src/media`, `src/EditorIntegration` and native integration changes:
  GPL-2.0-or-later. The application is distributed together with GPLv3 Gmerlin;
  the combined installed build follows GPL-3.0-or-later terms, as permitted by
  upstream's later-version clause. See `COPYING` and `COPYING.Gmerlin`.
- Gmerlin 1.2.0: official SourceForge source, GPLv3, built privately with current
  GCC compatibility flags. Source archive SHA256:
  `80a217c417b197562961cfd55bf5d356d7eb5278601f6f5e05636455822e3d62`.
  The bootstrap script records the URL and build adjustments. Its source stays
  in `.deps/gmerlin-1.2.0`; core and video-filter binaries are installed privately.
- Libquicktime, libdv and Schroedinger: unmodified Fedora 44 RPM binaries,
  extracted without system installation. Versions and exact archive hashes are
  recorded in `packaging/dependency-rpms.sha256`. RPM copyright/license files
  accompany the private installation under the application documentation.
- Arc SBS shared `fltk/appearance.h`, `desktop-menu.{h,cpp}`,
  `desktop-menu.cmake` and `appmenu.xml` are compiled/included directly from the
  sibling checkout. MIT SPDX notices and the protocol's notices are retained.
- Arc SBS conversion and mpvr playback execute the existing installed commands.
  No model, external movie, proprietary conversion code or mpvr fork is bundled.
- FLTK, FFmpeg/oneVPL, GAVL, PortAudio, Intel media driver and desktop libraries
  remain dynamically linked host dependencies, with their distribution terms.
