# Task Manager app

neo.apps.task-manager exports task-manager and depends on neo.ui.canvas. It is an
ordinary app with its own window, input, selection and rendering. Apps dropdown
creates a new instance; its top-bar button visits that existing instance.

Select an app row with the mouse or arrow keys. Enter / GO TO WINDOW requests
navigation. Delete / CLOSE WINDOW terminates the selected app and releases its
transient state and resources. Task Manager's own X terminates that instance too.
It reports actual open windows, not invented CPU/memory statistics.

The desktop binds desktop-model READ and desktop-request READ|WRITE, plus private
buffer/window READ|WRITE. The app cannot mutate the window model directly. The
mailbox is serviced by desktop image policy after callbacks return. Multiple Task
Managers share read-only metadata and retain independent selection/view state.
