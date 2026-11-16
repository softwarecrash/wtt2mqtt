# WTTC attribution and source provenance

Upstream: https://github.com/matatunos/wttc

Pinned commit: **adbbf985a46c425c476f0dd9db3e8422f0bc7ea1** (2026-11-07).

Project maintainer: matatunos / nacho (vigia). Upstream states that the code and content were generated with Claude (Anthropic). This statement is retained in the original files. License: **GNU GPL version 3 or later**, as stated in upstream README; full license is at repository root `LICENSE`.

Unmodified upstream files distributed here:

| Local file | Original path |
|---|---|
| `averias.h` | `firmware/WTTC/averias.h` |

WTT2MQTT's `lib/WBus/` and sensor/ID/counter/error decoding in `lib/Heater/` are adaptations of the protocol descriptions and routines in `firmware/WTTC/WTTC.ino` and `firmware/WTTC/logica.h`, rewritten as bounded, non-blocking C++ classes. Changes: dependency-injected port, timed break/echo/receive states, strict bounds and payload checks, owner-controlled command scheduling, separate inferred state and observed flame, optional fields, safe stop handling, no Arduino/UI/program dependencies in the core. These changes are part of WTT2MQTT 0.1.0, 2026-11-16, by WTT2MQTT contributors.

Error texts are included unchanged. WTTC identifies libwbus (Manuel Jander, https://sourceforge.net/projects/libwbus/) as its protocol/error-code reference. No additional libwbus source is copied directly into this repository. Heater compatibility and physical bus conformance require validation on real hardware.

WTT2MQTT is distributed under GPL-3.0-or-later, without warranty. When distributing a binary, also provide the matching complete corresponding source and build instructions/dependencies under the applicable licenses. The packaging script includes this project's source, embedded third-party files, license and build instructions. Third-party toolchain/framework dependencies retain their own licenses and are fetched by PlatformIO; obtain and retain the matching sources/license notices when redistributing builds. No claim of Webasto endorsement is made.
