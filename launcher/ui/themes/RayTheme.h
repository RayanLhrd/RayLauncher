// SPDX-License-Identifier: GPL-3.0-only
/*
 *  RayLauncher - Minecraft Launcher
 *  Copyright (C) 2026 RayLauncher Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

/**
 * @brief The one and only RayLauncher visual theme.
 *
 * RayLauncher intentionally ships with a single, locked-in modern dark look — no user-facing
 * theme customization. This class centralizes everything visual:
 *
 *   * The global QPalette (window/text/button/highlight colors).
 *   * The global QStyle (Fusion, the cross-platform predictable base).
 *   * The global QApplication stylesheet (one string covering every standard Qt widget plus
 *     our custom ones — RayModpackCard, RaySimpleProgressDialog, etc.).
 *
 * Call RayTheme::applyGlobalStyle() once from Application's startup sequence, after QApplication
 * exists but before the MainWindow is constructed. Everything painted from then on picks up
 * the palette / QSS on first draw. No runtime re-selection; no per-widget theme lookups.
 *
 * The legacy ThemeManager / ITheme infrastructure stays around in Commit A so the (soon-to-be
 * deleted) Appearance settings page doesn't crash. Commit B will remove both.
 */
class RayTheme {
   public:
    /// Apply palette + Fusion style + QSS to the global QApplication. Idempotent — safe to call
    /// multiple times, although it's expected to be called exactly once at startup.
    static void applyGlobalStyle();

   private:
    RayTheme() = delete;
};
