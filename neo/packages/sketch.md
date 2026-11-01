# Sketch foreground app

`neo.apps.sketch` 0.1.0 exports sketch and pins canvas0.1.0. It is an actual
workspace app using the browser's explicit foreground protocol, not a special
kernel window or randomly embedded OS-seed scene.

Instantiate it, switch to workspace, select the instance and press F5. Left mouse
drag records/stamps orange brush points below the header; Escape returns to the
browser. Relaunch redraws the same model. H opens its inspectable handlers.

The model stores at most512 point pairs in an image integer array. Identical
consecutive points are skipped. This initial brush stamps points without joining
them into interpolated lines; sparse pointer delivery can leave gaps. The input
handler uses capture while drawing and cancels on release/reset/capture loss.
Each frame redraws at most eight pending5×5 stamps. Private font/rectangle
behavior is copied from canvas; no external source is loaded while running.

resume binds dimensions and restarts display from the retained point model;
pause clears held drawing state. Buffer/window grants come only from the explicit
launcher and remain scoped resources. There are no file/network/native-process
operations. GUI launch currently has no save button: retained model means within
the same live image session, not automatic durable storage after exit.

Universal tests verify launch, input dispatch, exact orange RGBA, reset/release,
return/relaunch and model preservation. Native Windows launch/drawing was also
observed. Full editing, brush interpolation, unbounded documents, undo and storage
are subsequent application work.
