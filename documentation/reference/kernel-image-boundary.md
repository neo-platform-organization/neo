# Native runtime and live image

[VM documentation](../README.md)

neo uses one C execution runtime. The live image contains programs, services,
policy and state executed by that runtime. The image does not need a second neo
interpreter to become the OS.

Native responsibilities are object storage/lifetime, capability enforcement,
safe graph mutation/copy/import, evaluator and primitive execution, execution
budgets, checkpoint encoding/validation and host/device bindings. Image services
choose scheduling order, recovery decisions, package workflows, save/restore
plans and application behavior. Maths, rendering and GUI algorithms belong in
the image. Privileged image services use explicit grants enforced by the runtime.

This is a responsibility boundary, not a goal to minimize native code at any cost.
Some primitives are appropriate basic mechanisms; a large native policy subsystem
is not image-owned merely because an image handler wraps it.

Package dependency pins, graph ordering/cycle detection, greater-version eligibility
and declared-dependent removal checks now live in the image manager. The native
duplicate resolver and version-order checks were removed after image acceptance
tests passed. Actual grants, topology, lifetime checks and atomic publication stay
enforced natively. Structural schema and baseline/dirty validation remain native;
this bounded migration does not claim every package decision has moved already.

The experimental second evaluator was removed. The normal C evaluator, image
scheduler/environment/package services, native recovery and inert decoding remain.
Future graph-resident activation/debugger work can retain this same execution
runtime; it need not introduce another interpreter or promise C-stack restoration.
