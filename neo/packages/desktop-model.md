# Desktop window and source-position model

neo.desktop.model0.1.0 exports desktop, pure image policy with no native resources.
The desktop session embeds it as model. windows records have title, x/y, width/
height, z, nD coordinates and an explicit app connection. Open windows always stay
on the canvas: there is no minimized flag or minimize/restore handler.

## Ownership

A view references its independent app actor; it does not own/copy its program or
model. The model close operation removes the view record. The enclosing desktop session
then deletes the app actor and resources; the model never owns their lifecycle. Open requires an explicit
candidate grant127; SEND is not inferred. Publication stages metadata before
copying into windows. Sequential allocation/budget failure is not a transaction.

## Operations

Set request fields, then invoke sequentially:

| Handler | Inputs/result |
| --- | --- |
| open | name/title/x/y/width/height and candidate; publish and focus view |
| focus | name; raise z and select |
| close | name; delete record and reconcile highest remaining z |
| move | name, x/y; checked extents; update selected source coordinates |
| resize | name, positive width/height; validate before publication |
| pan | pan-x/y; validate both new viewport coordinates first |
| screen | name; x/y minus viewport, without changing source |
| hit | pointer-x/y plus viewport; highest-z half-open rectangle |
| set-position | name and position array; validate full source and project |
| project | refresh all derived x/y using view-axes |

coordinates use the spatial scale1,000,000. view-axes selects two distinct
nonnegative axes. display-dimension controls initial storage; higher selected axes
grow the stored prefix with zeros. Hidden coordinates survive moves/projection.
There is no fixed dimensional ceiling beyond host memory/work/integer bounds.

Screen/hit above describe the unscaled model adapter. The session applies zoom
and uses the same rounded projected rectangles for paint and picking. Its visit
operation pans the camera to a window; it does not move that window.

Tests cover authority, focus/z, geometry, negative/half-open hits, pan/overflow,
close retaining app identity, nD edits and graph save/reopen. See desktop.md for
session controls, resource ownership and raster/projection limits.
