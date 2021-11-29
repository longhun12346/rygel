// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program. If not, see https://www.gnu.org/licenses/.

const ui = new function() {
    let self = this;

    let menu_render;
    let panels = new Map;
    let active_panels = 0;

    let expanded = {};
    let new_expanded;

    let dialogs = {
        prev: null,
        next: null
    };
    dialogs.prev = dialogs;
    dialogs.next = dialogs;

    let log_el;
    let log_entries = [];

    this.init = function() {
        log_el = document.createElement('div');
        log_el.id = 'ui_log';
        document.body.appendChild(log_el);

        log.pushHandler(notifyHandler);

        window.addEventListener('resize', () => {
            adaptToViewport();
            if (!document.body.classList.contains('gp_loading'))
                self.render();
        });
        adaptToViewport();

        document.addEventListener('click', closeOldDialogs);
    }

    function adaptToViewport(keep_panel = null) {
        let small_threshold = 900;
        let mobile_threshold = 580;

        let small = window.innerWidth < small_threshold;
        let mobile = window.innerWidth < mobile_threshold;
        let max_panels = util.clamp(Math.floor(window.innerWidth / mobile_threshold), 1, 2);

        while (active_panels > max_panels) {
            let disable_priority = Number.MAX_SAFE_INTEGER;
            let disable_panel;

            for (let panel of panels.values()) {
                if (panel !== keep_panel && panel.active && panel.priority < disable_priority) {
                    disable_priority = panel.priority;
                    disable_panel = panel;
                }
            }

            disable_panel.active = false;
            active_panels--;
        }

        document.documentElement.classList.toggle('small', small);
        document.documentElement.classList.toggle('mobile', mobile);
    }

    this.render = function() {
        let render_main = true;

        // Run dialog functions
        {
            let it = dialogs.next;

            while (it !== dialogs) {
                it.render();

                if (it.type === 'screen') {
                    render_main = false;
                    break;
                }

                it = it.next;
            }
        }

        // Render main screen
        if (render_main) {
            new_expanded = {};

            render(html`
                ${menu_render != null ? html`<nav class=${goupile.isLocked() ? 'ui_toolbar locked' : 'ui_toolbar'}
                                                  id="ui_top" style="z-index: 999999;">${menu_render()}</nav>` : ''}

                <main id="ui_panels">
                    ${util.map(panels.values(), panel => panel.active ? panel.render() : '')}
                </main>
            `, document.querySelector('#ui_main'));

            expanded = new_expanded;
        } else {
            render('', document.querySelector('#ui_main'));
        }

        adjustLogLayer();
        adjustLoadingSpinner();
    };

    this.setMenu = function(func) {
        menu_render = func;
    };

    this.createPanel = function(key, priority, active, func) {
        panels.set(key, {
            render: func,
            priority: priority,
            active: active
        });
        active_panels += active;

        adaptToViewport();
    };

    this.isPanelEnabled = function(key) {
        let panel = panels.get(key);
        return panel != null && panel.active;
    };

    this.setPanelState = function(key, active) {
        let panel = panels.get(key);

        if (active !== panel.active && (active || active_panels > 1)) {
            panel.active = active;
            active_panels += active ? 1 : -1;

            if (active)
                adaptToViewport(panel);
        }
    };

    this.getEnabledPanels = function() {
        let keys = [];

        for (let [key, panel] of panels.entries()) {
            if (panel.active)
                keys.push(key);
        }

        return keys;
    };

    this.setEnabledPanels = function(keys) {
        active_panels = 0;
        for (let [key, panel] of panels.entries()) {
            panel.active = keys.includes(key);
            active_panels += panel.active;
        }

        adaptToViewport();
    };

    this.runDialog = function(e, title, options, func) {
        if (e != null) {
            e.stopPropagation();

            if (options.type == null) {
                if (document.documentElement.classList.contains('small')) {
                    options.type = 'modal';
                } else {
                    options.type = 'popup';
                }
            }
        } else if (options.type == null) {
            options.type = (title != null) ? 'modal' : 'screen';
        }

        return runDialog(e, title, options, func);
    };

    function runDialog(e, title, options, func) {
        if (e != null)
            closeOldDialogs(e);

        return new Promise((resolve, reject) => {
            let dialog = {
                prev: dialogs.prev,
                next: dialogs,

                title: title,
                type: options.type,
                fixed: !!options.fixed,
                el: document.createElement('div'),
                state: new FormState,
                render: () => buildDialog(dialog, e, func),
                closeable: options.type !== 'screen',

                resolve: value => {
                    if (dialog.el != null) {
                        resolve(value);
                        closeDialog(dialog);
                    }
                },
                reject: err => {
                    if (dialog.el != null) {
                        reject(err);
                        closeDialog(dialog);
                    }
                }
            };
            dialog.state.changeHandler = dialog.render;

            // Complete linked list insertion
            dialogs.prev.next = dialog;
            dialogs.prev = dialog;

            // Modal or popup?
            dialog.el.className = `ui_dialog ${options.type}`;
            if (options.type !== 'screen') {
                dialog.el.addEventListener('keydown', e => {
                    if (e.keyCode == 27)
                        dialog.reject(null);
                });
            }

            dialog.el.style.zIndex = 9999999;
            adjustLogLayer();

            // Show it!
            document.querySelector('#ui_root').appendChild(dialog.el);
            dialog.render();

            if (options.type === 'screen')
                render('', document.querySelector('#ui_main'));

            document.body.classList.remove('gp_loading');
        });
    };

    function closeOldDialogs(e) {
        // Close dialogs
        {
            let it = dialogs.next;

            while (it !== dialogs) {
                if (it.closeable) {
                    if (it.type === 'popup') {
                        let target = e.target.parentNode;

                        for (;;) {
                            if (target == null || target.classList == null) {
                                it.reject(null);
                                break;
                            } else if (target.classList.contains('ui_dialog') && target.classList.contains('popup')) {
                                break;
                            }
                            target = target.parentNode;
                        }
                    } else if (it.type === 'modal') {
                        let target = e.target;

                        if (target === it.el)
                            it.reject(null);
                    }
                }

                it = it.next;
            }
        }

        // Close dropdown menus
        {
            let els = document.querySelectorAll('.ui_toolbar > .drop.active');
            let target = util.findParent(e.target, el => el.classList.contains('drop'));

            for (let el of els) {
                if (el !== target)
                    el.classList.remove('active');
            }
        }

        adjustLogLayer();
    }

    function buildDialog(dialog, e, func) {
        let model = new FormModel;
        let builder = new FormBuilder(dialog.state, model);

        builder.pushOptions({
            missing_mode: 'disable',
            wide: true
        });
        func(builder, dialog.resolve, dialog.reject);
        if (dialog.closeable)
            builder.action('Annuler', {}, () => dialog.reject(null));

        render(html`
            <div>
                ${dialog.title ? html`<div class="ui_legend">${dialog.title}</div>` : ''}
                <form @submit=${e => e.preventDefault()}>
                    ${model.render()}
                </form>
            </div>
        `, dialog.el);

        // We need to know popup width and height
        let give_focus = !dialog.el.classList.contains('active');
        dialog.el.style.visibility = 'hidden';
        dialog.el.classList.add('active');
        if (dialog.fixed)
            dialog.el.classList.add('fixed');

        // Try different popup positions
        if (dialog.type === 'popup' && e != null) {
            let origin;
            if (e.clientX && e.clientY) {
                origin = {
                    x: e.clientX - 1,
                    y: e.clientY - 1
                };
            } else {
                let rect = e.target.getBoundingClientRect();
                origin = {
                    x: (rect.left + rect.right) / 2,
                    y: (rect.top + rect.bottom) / 2
                };
            }

            let pos = {
                x: origin.x,
                y: origin.y
            };
            if (pos.x > window.innerWidth - dialog.el.offsetWidth - 10) {
                pos.x = origin.x - dialog.el.offsetWidth;
                if (pos.x < 10) {
                    pos.x = Math.min(origin.x, window.innerWidth - dialog.el.offsetWidth - 10);
                    pos.x = Math.max(pos.x, 10);
                }
            }
            if (pos.y > window.innerHeight - dialog.el.offsetHeight - 10) {
                pos.y = origin.y - dialog.el.offsetHeight;
                if (pos.y < 10) {
                    pos.y = Math.min(origin.y, window.innerHeight - dialog.el.offsetHeight - 10);
                    pos.y = Math.max(pos.y, 10);
                }
            }

            dialog.el.style.left = pos.x + 'px';
            dialog.el.style.top = pos.y + 'px';
        }

        // Reveal!
        dialog.el.style.visibility = 'visible';
        if (give_focus) {
            let widget0 = dialog.el.querySelector('input, select, button, textarea');
            if (widget0 != null)
                setTimeout(() => widget0.focus(), 0);
        }
    }

    function closeDialog(dialog) {
        dialog.next.prev = dialog.prev;
        dialog.prev.next = dialog.next;

        document.querySelector('#ui_root').removeChild(dialog.el);
        dialog.el = null;

        adjustLoadingSpinner();
    }

    function adjustLoadingSpinner() {
        let empty = (dialogs.next === dialogs) && !active_panels;
        document.body.classList.toggle('gp_loading', empty);
    }

    this.runConfirm = function(e, msg, action, func) {
        let title = action + ' (confirmation)';

        return self.runDialog(e, title, {}, (d, resolve, reject) => {
            d.output(msg);

            d.action(action, {disabled: !d.isValid()}, async e => {
                try {
                    await func(e);
                    resolve();
                } catch (err) {
                    log.error(err);
                    d.refresh();
                }
            });
        });
    };

    this.wrapAction = function(func) {
        return async e => {
            let target = e.target;

            if (target.disabled || target.classList.contains('disabled') ||
                                   target.classList.contains('ui_busy'))
                return;

            try {
                target.classList.add('ui_busy');
                if (target.disabled != null)
                    target.disabled = true;

                await func(e);
            } catch (err) {
                if (err != null) {
                    log.error(err);
                    throw err;
                }
            } finally {
                target.classList.remove('ui_busy');
                if (target.disabled != null)
                    target.disabled = false;
            }
        };
    };

    this.deployMenu = function(e) {
        let el = util.findParent(e.target, el => el.classList.contains('drop'));

        if (el.classList.contains('active')) {
            if (!e.ctrlKey)
                el.classList.remove('active');
        } else {
            closeOldDialogs(e);
            el.classList.add('active');
        }

        let expands = el.querySelectorAll('.expand');
        for (let i = 0; i < expands.length; i++) {
            let exp = expands[i];
            exp.classList.toggle('active', i === expands.length - 1);
        }

        e.stopPropagation();
    };

    this.expandMenu = function(key, title, active, content) {
        if (expanded.hasOwnProperty(key))
            active = expanded[key];
        new_expanded[key] = active;

        return html`
            <div class=${active ? 'expand active' : 'expand'} data-key=${key}>
                <button @click=${handleMenuClick}>${title}</button>
                <div>${content}</div>
            <div>
        `;
    };

    function handleMenuClick(e) {
        let el = util.findParent(e.target, el => el.classList.contains('expand'));
        let key = el.dataset.key;

        expanded[key] = el.classList.toggle('active');

        e.stopPropagation();
    }

    function notifyHandler(action, entry) {
        if (typeof lithtml !== 'undefined' && entry.type !== 'debug') {
            switch (action) {
                case 'open': {
                    log_entries.unshift(entry);

                    if (entry.type === 'progress') {
                        // Wait a bit to show progress entries to prevent quick actions from showing up
                        setTimeout(renderLog, 300);
                    } else {
                        renderLog();
                    }
                } break;
                case 'edit': {
                    renderLog();
                } break;
                case 'close': {
                    log_entries = log_entries.filter(it => it !== entry);
                    renderLog();
                } break;
            }
        }

        log.defaultHandler(action, entry);
    };

    function renderLog() {
        render(log_entries.map(entry => {
            let msg = (entry.msg instanceof Error) ? entry.msg.message : entry.msg;

            if (typeof msg === 'string')
                msg = msg.split('\n').map(line => [line, html`<br/>`]);

            if (entry.type === 'progress') {
                return html`<div class="progress">
                    <div class="ui_log_spin"></div>
                    ${msg}
                </div>`;
            } else {
                return html`<div class=${entry.type} @click=${e => entry.close()}>
                    <button class="ui_log_close">X</button>
                    ${msg}
                </div>`;
            }
        }), log_el);
    }

    function adjustLogLayer() {
        // Adjust log z-index. We need to do it dynamically because we want it to show
        // above screen and modal dialogs (which show up above eveyerthing else), but
        // below menu bar dropdown menus.
        log_el.style.zIndex = (dialogs.next != dialogs) ? 999999999 : 99999;
    }
};
