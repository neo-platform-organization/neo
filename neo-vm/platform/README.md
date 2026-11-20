# Platform adapter boundary

Generic runtime code includes platform.h and services.h. The root
Makefile selects adapters. Host SDK headers and handles remain under
this directory. See documentation/reference/platform-services.md for
implemented behavior and limits.

windows.c owns native JIT worker execution, executable pages, runtime
allocation, and the legacy bounded text-file bridge. services_windows.c
implements clock/wakeup, machine and entropy queries, standard streams,
input, opaque window surfaces, managed storage, and loopback TCP/UDP.
The common console reader, requests, grants, frame leases, and headless
adapter live outside the Windows backend and use the same contracts.

Presentation takes RGBA bytes. It has no widgets, drawing policy, app
names, or filesystem schema. The canonical image owns the surface
exercise's drawing and response to input. The host surface launcher
routes normalized events through a documented graph protocol.

Storage is a managed opaque key/byte store, not a general host filesystem
sandbox. Networking is loopback only. These constraints are explicit in
the API and reference. Linux and standalone x86_64 now have implementations;
hosted OpenGL/software, audio, and clipboard mechanisms are implemented. Native JIT calling convention, isolation,
request lifetime, and cancellation rules must be honored by new backends.

Standalone code lives in x86_64/, using the architecture name just like arm64.
Its trusted integer-only boot profile and device limits are documented in the build guide.
