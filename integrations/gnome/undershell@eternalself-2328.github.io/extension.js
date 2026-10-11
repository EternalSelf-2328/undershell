// SPDX-License-Identifier: GPL-3.0-or-later
// GNOME has no layer-shell, so undershell's widgets are plain windows there.
// This extension starts undershell as its own Wayland client and turns those
// windows into part of the desktop: type DESKTOP (below every window, on all
// workspaces, out of Alt+Tab and the overview), at the place each window's
// title names ("undershell|widget|<connector>|x|y|w|h|<id>", logical px on
// that monitor). The editor ("undershell|editor|<connector>") is laid over its
// whole monitor, above the rest, while it is open (not fullscreen: mutter
// paints black under a fullscreen window, hiding the desktop being edited).
//
// Mutter's API moved: GNOME 49+ has Meta.WaylandClient.new_subprocess() and
// Meta.Window.set_type(); 46–48 Meta.WaylandClient.new().spawnv() and
// make_desktop(); 45 has neither make_desktop nor set_type, so a desktop
// window is emulated there (lowered, sticky, out of the window list).

import GLib from 'gi://GLib';
import Gio from 'gi://Gio';
import Meta from 'gi://Meta';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const TITLE = /^undershell\|(widget|editor)\|([^|]*)(?:\|(-?\d+)\|(-?\d+)\|(\d+)\|(\d+))?/;

export default class UndershellExtension extends Extension {
    enable() {
        this._windows = new Map(); // MetaWindow -> [signal ids]
        this._restarts = [];
        this._stopping = false;
        this._spawn();
        this._mapId = global.window_manager.connect('map', (_wm, actor) => this._adopt(actor.meta_window));
        this._focusId = global.display.connect('notify::focus-window', () => this._guardFocus());
        this._monitorsId = Main.layoutManager.connect('monitors-changed', () => this._placeAll());
        for (const actor of global.get_window_actors())
            this._adopt(actor.meta_window);
    }

    disable() {
        this._stopping = true;
        global.window_manager.disconnect(this._mapId);
        global.display.disconnect(this._focusId);
        Main.layoutManager.disconnect(this._monitorsId);
        if (this._retryId)
            GLib.source_remove(this._retryId);
        this._retryId = 0;
        for (const [win, ids] of this._windows)
            ids.forEach(id => win.disconnect(id));
        this._windows.clear();
        this._proc?.send_signal(15); // SIGTERM: undershell exits cleanly
        this._proc = null;
        this._client = null;
    }

    _binary() {
        // gnome-shell's PATH may lack ~/.local/bin (a hand install)
        for (const path of [GLib.find_program_in_path('undershell'),
            GLib.build_filenamev([GLib.get_home_dir(), '.local', 'bin', 'undershell'])]) {
            if (path && GLib.file_test(path, GLib.FileTest.IS_EXECUTABLE))
                return path;
        }
        return null;
    }

    _spawn() {
        const bin = this._binary();
        if (!bin) {
            console.warn('undershell: the undershell binary was not found');
            return;
        }
        const launcher = new Gio.SubprocessLauncher({flags: Gio.SubprocessFlags.NONE});
        launcher.setenv('UNDERSHELL_HOST', 'gnome-shell', true);
        try {
            if (typeof Meta.WaylandClient.new_subprocess === 'function') { // 49+
                this._client = Meta.WaylandClient.new_subprocess(global.context, launcher, [bin]);
                this._proc = this._client.get_subprocess();
            } else { // 44–48
                this._client = Meta.WaylandClient.new(global.context, launcher);
                this._proc = this._client.spawnv(global.display, [bin]);
            }
        } catch (e) {
            console.error(`undershell: could not start ${bin}: ${e.message}`);
            return;
        }
        const proc = this._proc;
        proc.wait_async(null, (p, res) => {
            try {
                p.wait_finish(res);
            } catch {}
            if (this._stopping || proc !== this._proc)
                return;
            // a crash: start it again, unless it keeps crashing
            const now = GLib.get_monotonic_time();
            this._restarts = this._restarts.filter(t => now - t < 60e6);
            if (!p.get_successful() && this._restarts.length < 5) {
                this._restarts.push(now);
                this._retryId = GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, 2, () => {
                    this._retryId = 0;
                    this._spawn();
                    return GLib.SOURCE_REMOVE;
                });
            }
        });
    }

    _ours(win) {
        if (!win || !this._client)
            return false;
        try {
            return this._client.owns_window(win);
        } catch {
            return false;
        }
    }

    _adopt(win) {
        if (!this._ours(win) || this._windows.has(win))
            return;
        const ids = [];
        this._windows.set(win, ids);
        ids.push(win.connect('notify::title', () => this._place(win)));
        ids.push(win.connect('unmanaged', () => this._windows.delete(win)));
        const m = TITLE.exec(win.get_title() ?? '');
        if (m && m[1] === 'editor') {
            this._hide(win);
            win.make_above();
            win.activate(global.get_current_time());
        } else {
            this._makeDesktop(win, ids);
        }
        this._place(win);
        // a new window is placed by mutter right after mapping: place it again
        GLib.idle_add(GLib.PRIORITY_DEFAULT_IDLE, () => {
            if (this._windows.has(win))
                this._place(win);
            return GLib.SOURCE_REMOVE;
        });
    }

    _hide(win) {
        if (typeof win.hide_from_window_list === 'function')
            win.hide_from_window_list(); // 49+
        else if (typeof this._client.hide_from_window_list === 'function')
            this._client.hide_from_window_list(win); // 44–48
    }

    _makeDesktop(win, ids) {
        if (typeof win.set_type === 'function') { // 49+
            win.set_type(Meta.WindowType.DESKTOP);
        } else if (typeof this._client.make_desktop === 'function') { // 46–48
            this._client.make_desktop(win);
        } else { // 45: emulated, as Desktop Icons NG did
            this._hide(win);
            win.stick();
            win.lower();
            ids.push(win.connect('raised', () => win.lower()));
        }
    }

    _place(win) {
        const m = TITLE.exec(win.get_title() ?? '');
        if (!m)
            return;
        const monitors = global.backend?.get_monitor_manager?.() ?? Meta.MonitorManager.get();
        const index = monitors.get_monitor_for_connector(m[2]);
        const geo = global.display.get_monitor_geometry(index >= 0 ? index : global.display.get_primary_monitor());
        if (m[1] === 'editor')
            win.move_resize_frame(true, geo.x, geo.y, geo.width, geo.height);
        else
            win.move_frame(true, geo.x + Number(m[3]), geo.y + Number(m[4]));
    }

    _placeAll() {
        for (const win of this._windows.keys())
            this._place(win);
    }

    // a widget never keeps the keyboard (a DESKTOP window can take it on map)
    _guardFocus() {
        const win = global.display.focus_window;
        if (!win || !this._windows.has(win))
            return;
        const m = TITLE.exec(win.get_title() ?? '');
        if (m && m[1] === 'widget')
            global.display.unset_input_focus(global.get_current_time());
    }
}
