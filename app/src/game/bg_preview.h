#ifndef BG_PREVIEW_H
#define BG_PREVIEW_H

#include <thermite.h>

/*
 * Background bot preview.
 *
 * While the player is on the Settings screen or the Controls screen,
 * this module quietly connects a bot-controlled snake in the
 * background so something is always moving behind the UI. It:
 *
 *   - connects using the player's current server/nickname/skin settings,
 *   - forces bot mode on for the duration (restoring the player's real
 *     bot hotkey state afterward),
 *   - feeds the bot's own steering output instead of reading real mouse/
 *     keyboard input (so interacting with Settings/Controls widgets
 *     never leaks into the hidden snake's movement),
 *   - respawns automatically every time the preview snake dies,
 *   - never touches the player's persisted score/kills/play time (see
 *     the preview_active guard in network/callback.c),
 *   - and steps out of the way cleanly if the player hits "Play" for a
 *     real session while the preview is still running.
 *
 * Call bg_preview_update() once per frame, unconditionally, regardless
 * of which screen is showing — it no-ops when there's nothing to do.
 */

/* Drives the whole background-preview lifecycle for the current frame:
   starting it, keeping it alive (incl. auto-respawn), or winding it
   down. Safe to call every frame no matter what curr_screen is. */
void bg_preview_update(tenv* env);

/* True only while a preview snake is actually connecting/alive right
   now. Used by the renderer to decide whether to draw the blurred
   backdrop behind the current screen. */
bool bg_preview_visible(tenv* env);

#endif
