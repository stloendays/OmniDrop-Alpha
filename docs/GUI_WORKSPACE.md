# Desktop workspace UI (development source)

OmniDrop's desktop window has an updated, low-noise two-pane workspace
in the current development branch. This layout **is not present in the
tagged v0.4.0 Alpha ZIP** until the next tested release.

## Layout

- The header keeps **Open files**, **Workflow**, and **Translate** accessible.
  Additional tools, rename history, settings/recovery and diagnostics remain
  available through the existing menus.
- The left **Input files** pane provides a compact drop zone and the preserved
  **Recent Files** list, when entries exist.
- The right **Available actions** pane provides file details, action search,
  count of matching/available actions, action list, and the existing Run/Stop
  progress controls. Before file selection it explains how to start; when a
  search has no matches it explains why the list is empty.
- The panes can be resized independently with the splitter. The lower
  **Recent Activity** list remains separate from the raw worker output.

All existing file processing, action availability, non-destructive defaults,
queue semantics and persistence are unchanged.

## Input and accessibility

| Interaction | Result |
| --- | --- |
| Ctrl+O | Open native multi-file chooser |
| Ctrl+F | Focus the action search field, select existing query |
| Ctrl+Return | Run the currently selected **available** action only |
| Double-click Recent | Re-inspect that local file |
| Drag local files | Select those local regular files |
| Drag folders / remote URLs | Reject unsupported entries; explain rejected mixed drops |
| Hover unavailable action | Display the backend explanation without adding noisy paragraphs |

Long operation progress remains visible in the right pane. Cooperative Stop
still takes effect only between individual files and does not imply that
an active processor can be forcefully cancelled.

The palette is neutral black/white/grey, with semantic focus borders and
non-color-only disabled statuses. Buttons are accessible from both mouse and
keyboard; explanatory captions do not conceal blockers or errors.

## Verification

The new offscreen Qt integration test creates a real MainWindow and validates
the two-pane UI, placeholder search states, native file selection path,
shortcuts and rejection of unsupported drag-and-drop inputs. Run Windows CTest
and packaged portable smoke before merging; this GUI feature is omitted from
Linux builds configured with `OMNIDROP_BUILD_GUI=OFF`.

This change deliberately does **not** add OCR, video converters, AI models,
cloud editing, background telemetry, automatic file modification or a
self-updater.
