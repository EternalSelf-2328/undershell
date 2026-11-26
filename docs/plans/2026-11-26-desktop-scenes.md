# Desktop scenes and widgets

Build on the existing editor, audio capture, saved layouts and file watchers. Keep the new behavior opt-in and verify each stage with the strict test suite and targeted checks.

1. Extend Image with a folder slideshow. Extend Text with a watched local data source.
2. Add a focus timer with start, pause, resume and stop actions. Expose the countdown to Text and notify companions at phase changes.
3. Add desktop rules for local time, battery, media playback, focus and Umbriel workspaces. Apply saved layouts at runtime so automatic switches cannot overwrite the editing layout.
4. Add a stereo oscilloscope, needle meters and a spectrogram. Keep stereo analysis scoped to the looks that need it.
5. Add aquarium, terrarium and rainy room dioramas. Use the current performance governor and limit animation to the widget.
6. Add editable scene animation to layers and portable scene import and export. Use the existing editor and profiles for layout. Copy referenced media and validate imports before changing the desktop.

Test empty and unavailable data, rapid mode changes, silence and pause, and scenes that target different output sizes. Remove a new option or disable its rule to return to the previous behavior. Use an isolated Wayland session for end-to-end checks.
