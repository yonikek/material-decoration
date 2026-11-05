# oxygen-decoration-lim

A KWin window decoration (KDecoration3) that looks like **Oxygen** and keeps the
advanced features of the *material-decoration* it is derived from:

- locally integrated application menu (LIM) in the title bar, with overflow / hamburger mode
- menu search
- tiling popup on the maximize button, long-press actions on window buttons
- drag-from-buttons, per-window exceptions

Appearance (gradient title bar, glossy round buttons, 1px outline, Oxygen
shadows, caption with light contrast text) follows the system colour scheme and
border size; the old Material colour, opacity, corner-radius, button-size and
shadow options were removed.

## Build

    cmake -B build -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    sudo cmake --install build

Then select **oxygen-decoration-lim** in System Settings > Colors & Themes >
Window Decorations. Settings are stored in `oxygen-decoration-limrc`.

## Notes

`OxygenHelper.{h,cc}` re-implements the parts of Oxygen's style helper
(colour maths, window gradient, glossy button slab) that the decoration needs.

Licensed GPL-2.0-or-later (see LICENSE).
