// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// These globals are initialized below
let app = null;
let virt_fs = null;
let virt_data = null;

let goupile = new function() {
    let self = this;

    let tablet_mq = window.matchMedia('(pointer: coarse)');
    let standalone_mq = window.matchMedia('(display-mode: standalone)');

    let settings;
    let settings_rnd;

    let allow_go = true;

    let sse_src;
    let sse_timer;
    let sse_listeners = [];
    let sse_online = false;

    let current_asset;
    let current_url;
    let executor = new FormExecutor;

    let left_panel;
    let show_overview = true;

    let editor_el;
    let style_el;

    let popup_el;
    let popup_state;
    let popup_builder;

    document.addEventListener('readystatechange', e => {
        if (document.readyState === 'complete')
            initGoupile();
    });

    async function initGoupile() {
        log.pushHandler(log.notifyHandler);

        initNavigation();

        let db = await openDatabase();
        virt_fs = new VirtualFS(db);
        virt_data = new VirtualData(db);

        if (navigator.serviceWorker) {
            navigator.serviceWorker.register(`${env.base_url}sw.pk.js`);

            if (env.use_offline && self.isStandalone()) {
                enablePersistence();
                await updateApplication();
            }
        }
        if (window.EventSource)
            initEvents();

        await self.initApplication();
    }

    function enablePersistence() {
        let storage_warning = 'Impossible d\'activer le stockage local persistant';

        if (navigator.storage && navigator.storage.persist) {
            navigator.storage.persist().then(granted => {
                // NOTE: For some reason this does not seem to work correctly on my Firefox profile,
                // where granted is always true. Might come from an extension or some obscure privacy
                // setting. Investigate.
                if (granted) {
                    console.log('Persistent storage has been granted');
                } else {
                    log.error(storage_warning);
                }
            });
        } else {
            log.error(storage_warning);
        }
    }

    async function updateApplication() {
        let entry = new log.Entry;

        entry.progress('Mise à jour de l\'application');
        try {
            let files = await virt_fs.status();

            if (files.some(file => file.action === 'pull' || file.action === 'conflict')) {
                if (files.some(file => file.action !== 'pull' && file.action !== 'noop'))
                    throw new Error('Impossible de mettre à jour (modifications locales)');

                await virt_fs.sync(files);
                entry.success('Mise à jour terminée !');
            } else {
                entry.close();
            }
        } catch (err) {
            entry.error(err.message);
        }
    }

    async function openDatabase() {
        let db_name = `goupile.${env.app_key}`;
        let db = await idb.open(db_name, 2, (db, old_version) => {
            switch (old_version) {
                case null: {
                    db.createStore('files', {keyPath: 'path'});
                    db.createStore('files_data');
                    db.createStore('files_cache', {keyPath: 'path'});

                    db.createStore('form_records', {keyPath: 'tkey'});
                    db.createStore('form_variables', {keyPath: 'tkey'});
                } // fallthrough

                case 1: {
                    db.deleteStore('form_records');
                    db.deleteStore('form_variables');

                    db.createStore('records', {keyPath: 'tkey'});
                    db.createStore('records_data', {keyPath: 'tkey'});
                    db.createStore('records_variables', {keyPath: 'tkey'});
                } // fallthrough
            }
        });

        return db;
    }

    function initNavigation() {
        window.addEventListener('popstate', e => app.go(window.location.href, false));

        util.interceptLocalAnchors((e, href) => {
            app.go(href);
            e.preventDefault();
        });
    }

    function initEvents() {
        if (sse_src) {
            sse_src.close();
        } else {
            // Helps a bit with Firefox issue, see goupile.cc for more information
            window.addEventListener('beforeunload', () => sse_src.close());
        }

        sse_src = new EventSource(`${env.base_url}api/events.json`);

        sse_src.onopen = e => {
            resetEventTimer();
            sse_online = true;
        };
        sse_src.onerror = e => {
            sse_src.close();
            sse_online = false;

            // Browsers are supposed to retry automatically, but Firefox does weird stuff
            resetEventTimer();
        };
        sse_src.addEventListener('keepalive', e => resetEventTimer());

        for (let listener of sse_listeners)
            sse_src.addEventListener(listener.event, listener.func);
    }

    function resetEventTimer() {
        clearInterval(sse_timer);
        sse_timer = setInterval(initEvents, 30000);
    }

    // Can be launched multiple times (e.g. when main.js is edited)
    this.initApplication = async function(code = null) {
        await fetchSettings();

        let files = await virt_fs.listAll();
        let files_map = util.mapArray(files, file => file.path);

        try {
            let new_app = new Application;
            let app_builder = new ApplicationBuilder(new_app);

            if (code == null) {
                let file = await virt_fs.load('/files/main.js');
                code = file ? await file.data.text() : '';
            }

            let func = Function('app', 'data', 'route', code);
            func(app_builder, new_app.data, new_app.route);

            let known_paths = new Set(new_app.assets.map(asset => asset.path));

            // Make unused files available
            for (let file of files) {
                if (!known_paths.has(file.path))
                    app_builder.file(file);
            }

            app = new_app;
        } catch (err) {
            if (app) {
                throw err;
            } else {
                // Empty application, so that the user can still fix main.js or reset everything
                app = new Application;
                console.log(err);
            }
        }

        app.urls_map = util.mapArray(app.assets, asset => asset.url);
        app.paths_map = util.mapArray(app.assets, asset => asset.path);
        app.go = handleGo;
        app.makeURL = makeURL;

        // XXX: Hack for secondary asset thingy that we'll get rid of eventually
        for (let i = 0; i < app.assets.length; i++)
            app.assets[i].idx = i;

        // Select default page
        if (app.home) {
            app.urls_map[env.base_url] = app.urls_map[app.home];
        } else {
            app.urls_map[env.base_url] =
                app.assets.find(asset => asset.type !== 'main' && asset.type !== 'blob') || app.assets[0];
        }

        // Update custom CSS (if any)
        {
            let file = await virt_fs.load('/files/main.css');

            if (file) {
                let css = await file.data.text();
                updateApplicationCSS(css);
            }
        }

        util.deepFreeze(app, 'route');
        app.go(current_url || window.location.href, false);
    };

    async function fetchSettings() {
        let session_rnd = util.getCookie('session_rnd');

        if (session_rnd !== settings_rnd) {
            settings = {};

            if (session_rnd != null) {
                let response = await fetch(`${env.base_url}api/settings.json?rnd=${session_rnd}`);
                if (response.ok) {
                    settings = await response.json();
                } else {
                    // The request has failed and could have deleted the session_rnd cookie
                    session_rnd = util.getCookie('session_rnd');
                }
            }

            settings_rnd = session_rnd;
            return true;
        } else {
            return false;
        }
    }

    function updateApplicationCSS(css) {
        if (!style_el) {
            style_el = document.createElement('style');
            document.head.appendChild(style_el);
        }

        style_el.textContent = css;
    }

    this.isOnline = function() { return sse_online; };
    this.isConnected = function() { return !!settings_rnd; };
    this.isTablet = function() { return tablet_mq.matches; };
    this.isStandalone = function() { return standalone_mq.matches; };

    this.listenToServerEvent = function(event, func) {
        let listener = {
            event: event,
            func: func
        };
        sse_listeners.push(listener);

        sse_src.addEventListener(event, func);
    };

    // Avoid async here, because it may fail (see allow_go) and the called may need
    // to catch that synchronously.
    function handleGo(url = null, push_history = true) {
        if (!allow_go) {
            throw new Error(`A navigation function (e.g. go()) has been interrupted.
Navigation functions should only be called in reaction to user events, such as button clicks.`);
        }

        if (url) {
            if (url.match(/(http|ftp|https):\/\//g) || url.startsWith('/')) {
                url = new URL(url, window.location.href);
            } else {
                url = new URL(`${env.base_url}app/${url}`, window.location.href);
            }

            // Update route application global
            for (let [key, value] of url.searchParams) {
                let num = Number(value);
                app.route[key] = Number.isNaN(num) ? value : num;
            }

            self.run(url.pathname).then(() => {
                if (push_history) {
                    window.history.pushState(null, null, makeURL());
                } else {
                    window.history.replaceState(null, null, makeURL());
                }
            });
        } else {
            self.run();
        }
    }

    function makeURL() {
        return util.pasteURL(current_url, app.route);
    }

    this.run = async function(url = null, args = {}) {
        if (await fetchSettings())
            await self.initApplication();

        // Find relevant asset and controller
        if (url) {
            current_asset = app.urls_map[url];
            if (!current_asset && !url.endsWith('/'))
                current_asset = app.urls_map[url + '/'];
            current_url = current_asset ? current_asset.url : url;

            if (!current_asset)
                log.error(`URL inconnue '${url}'`);
        }

        if (current_asset && current_asset.form) {
            await executor.route(current_asset.form, args);

            // The user asked for this record, make sure it is visible
            if (args.id != null && !show_overview) {
                if (goupile.isTablet())
                    left_panel = null;
                show_overview = true;
            }
        }

        // Render menu and page layout
        renderMainUI();

        // Run left panel
        switch (left_panel) {
            case 'files': { await dev_files.runFiles(); } break;
            case 'editor': { await dev_files.syncEditor(current_asset.path); } break;
            case 'status': { await executor.runStatus(); } break;
            case 'data': { await executor.runData(); } break;
        }

        // Run appropriate module
        if (current_asset) {
            document.title = `${current_asset.label} — ${env.app_name}`;
            await runAssetSafe(current_asset);
        } else {
            document.title = env.app_name;
        }
    };

    function renderMainUI() {
        let show_editor = current_asset && current_asset.edit;
        let show_data = current_asset && current_asset.form;

        let correct_mode = (left_panel == null ||
                            left_panel === 'files' ||
                            (left_panel === 'editor' && show_editor) ||
                            (left_panel === 'status' && show_data) ||
                            (left_panel === 'data' && show_data));
        if (!correct_mode)
            left_panel = show_editor ? 'editor' : null;

        if (!current_asset || !current_asset.overview) {
            show_overview = false;
        } else if (!left_panel) {
            show_overview = true;
        }

        let show_assets = [];
        let select_asset;
        if (current_asset) {
            let idx = current_asset.idx;
            while (app.assets[idx].secondary)
                idx--;

            // Main asset
            show_assets.push(app.assets[idx]);
            select_asset = app.assets[idx];

            // Related secondary assets
            while (++idx < app.assets.length && app.assets[idx].secondary)
                show_assets.push(app.assets[idx]);
        }

        render(html`
            ${show_data ?
                html`<button class=${left_panel === 'status' ? 'active' : ''}
                             @click=${e => toggleLeftPanel('status')}>Suivi</button>` : ''}
            ${show_editor ?
                html`<button class=${left_panel === 'editor' ? 'active' : ''}
                             @click=${e => toggleLeftPanel('editor')}>Éditeur</button>` : ''}
            ${show_data ?
                html`<button class=${left_panel === 'data' ? 'active' : ''}
                             @click=${e => toggleLeftPanel('data')}>Données</button>` : ''}

            ${show_assets.map(asset => {
                if (asset === current_asset) {
                    return html`<button class=${show_overview ? 'active': ''}
                                        @click=${e => toggleAssetView(asset)}>${asset.overview}</button>`;
                } else {
                    return html`<button @click=${e => toggleAssetView(asset)}>${asset.overview}</button>`;
                }
            })}

            <select id="gp_assets" @change=${e => app.go(e.target.value)}>
                ${!current_asset ? html`<option>-- Sélectionnez une page --</option>` : ''}
                ${util.mapRLE(app.assets, asset => asset.category, (category, offset, len) => {
                    if (len === 1) {
                        let asset = app.assets[offset];
                        return html`<option value=${asset.url}
                                            .selected=${asset === select_asset}>${asset.category} (${asset.label})</option>`;
                    } else {
                        return html`<optgroup label=${category}>${util.mapRange(offset, offset + len, idx => {
                            let asset = app.assets[idx];
                            if (!asset.secondary) {
                                return html`<option value=${asset.url}
                                                   .selected=${asset === select_asset}>${asset.label}</option>`;
                            } else {
                                return '';
                            }
                        })}</optgroup>`;
                    }
                })}
            </select>

            <div class="gp_dropdown right">
                <button>Administration</button>
                <div>
                    <button class=${left_panel === 'files' ? 'active' : ''} @click=${e => toggleLeftPanel('files')}>Ressources</button>
                    <button @click=${e => log.error('Fonctionnalité non disponible')}>Utilisateurs</button>
                </div>
            </div>
            ${!self.isConnected() ? html`<button @click=${showLoginDialog}>Connexion</button>` : ''}
            ${self.isConnected() ? html`
                <div class="gp_dropdown right">
                    <button>${settings.username}</button>
                    <div>
                        <button @click=${showLoginDialog}>Changer d'utilisateur</button>
                        <button @click=${logout}>Déconnexion</button>
                    </div>
                </div>
            ` : ''}
        `, document.querySelector('#gp_menu'));

        if (left_panel === 'editor' && !editor_el) {
            editor_el = document.createElement('div');
            editor_el.style.height = '100%';
        }

        render(html`
            ${left_panel === 'files' ?
                html`<div id="dev_files" class=${show_overview ? 'gp_panel left' : 'gp_panel fixed'}></div>` : ''}
            ${left_panel === 'editor' ?
                html`<div id="dev_editor" class=${show_overview ? 'gp_panel left' : 'gp_panel fixed'}>${editor_el}</div>` : ''}
            ${left_panel === 'status' ?
                html`<div id="dev_status" class=${show_overview ? 'gp_panel left' : 'gp_panel fixed'}></div>` : ''}
            ${left_panel === 'data' ?
                html`<div id="dev_data" class=${show_overview ? 'gp_panel left' : 'gp_panel fixed'}></div>` : ''}
            <div id="gp_overview" class=${left_panel ? 'gp_panel right' : 'gp_panel overview'}
                 style=${show_overview ? '' : 'display: none;'}></div>

            <div id="gp_overview_log" style="display: none;"></div>
        `, document.querySelector('main'));
    }

    function showLoginDialog(e) {
        goupile.popup(e, page => {
            let username = page.text('username', 'Nom d\'utilisateur');
            let password = page.password('password', 'Mot de passe');

            page.submitHandler = async () => {
                page.close();

                let entry = new log.Entry;

                entry.progress('Connexion en cours');
                try {
                    let body = new URLSearchParams({
                        username: username.value,
                        password: password.value}
                    );

                    let response = await fetch(`${env.base_url}api/login.json`, {method: 'POST', body: body});

                    if (response.ok) {
                        entry.success('Connexion réussie');
                        await self.initApplication();
                    } else {
                        let msg = await response.text();
                        entry.error(msg);
                    }
                } catch (err) {
                    entry.error(err.message);
                }
            };
            page.buttons(page.buttons.std.ok_cancel('Connexion'));
        });
    }

    async function logout() {
        let entry = new log.Entry;

        entry.progress('Déconnexion en cours');
        try {
            let response = await fetch(`${env.base_url}api/logout.json`, {method: 'POST'});

            if (response.ok) {
                entry.success('Déconnexion réussie');
                await self.initApplication();
            } else {
                let msg = await response.text();
                entry.error(msg);
            }
        } catch (err) {
            entry.error(err.message);
        }
    }

    function toggleLeftPanel(mode) {
        if (goupile.isTablet()) {
            left_panel = mode;
            show_overview = false;
        } else if (left_panel !== mode) {
            left_panel = mode;
        } else {
            left_panel = null;
            show_overview = true;
        }

        app.go();
    }

    function toggleAssetView(asset) {
        if (goupile.isTablet() || asset !== current_asset) {
            left_panel = null;
            show_overview = true;
        } else if (!show_overview) {
            show_overview = true;
        } else {
            left_panel = left_panel || 'editor';
            show_overview = false;
        }

        app.go(asset.url);
    }

    this.validateCode = function(path, code) {
        let asset = app.paths_map[path];
        return asset ? runAssetSafe(asset, code) : true;
    };

    async function runAssetSafe(asset, code = null) {
        let overview_el;
        let log_el;
        if (asset === current_asset) {
            overview_el = document.querySelector('#gp_overview');
            log_el = document.querySelector('#gp_overview_log');
        } else {
            overview_el = document.createElement('div');
            log_el = document.createElement('div');
        }

        try {
            switch (asset.type) {
                case 'main': {
                    if (code != null) {
                        if (asset.path === '/files/main.js') {
                            await self.initApplication(code);

                            // If initApplication() succeeds it runs the page, so no need to redo it
                            return true;
                        } else if (asset.path === '/files/main.css') {
                            updateApplicationCSS(code);
                        }
                    }

                    render(html`<div class="gp_wip">Aperçu non disponible pour le moment</div>`, overview_el);
                } break;

                case 'page': {
                    // We don't want go() to be fired when a script is opened or changed in the editor,
                    // because then we wouldn't be able to come back to the script to fix the code.
                    allow_go = false;

                    if (code == null) {
                        let file = await virt_fs.load(asset.path);
                        code = file ? await file.data.text() : '';
                    }

                    executor.runForm(asset.page, code, overview_el);
                } break;

                case 'schedule': { await sched_executor.runMeetings(asset.schedule, overview_el); } break;
                case 'schedule_settings': { await sched_executor.runSettings(asset.schedule, overview_el); } break;

                default: {
                    render(html`<div class="gp_wip">Aperçu non disponible pour le moment</div>`, overview_el);
                } break;
            }

            // Things are OK!
            log_el.innerHTML = '';
            log_el.style.display = 'none';
            overview_el.classList.remove('broken');

            return true;
        } catch (err) {
            let err_line = util.parseEvalErrorLine(err);

            log_el.textContent = `⚠\uFE0E Line ${err_line || '?'}: ${err.message}`;
            log_el.style.display = 'block';
            overview_el.classList.add('broken');

            // Make it easier for complex screw ups (which are mine, most of the time)
            console.log(err);

            return false;
        } finally {
            allow_go = true;
        }
    }

    this.popup = function(e, func) {
        closePopup();
        openPopup(e, func);
    };

    function openPopup(e, func) {
        if (!popup_el)
            initPopup();

        let page = new Page('@popup');

        popup_builder = new PageBuilder(popup_state, page);
        popup_builder.changeHandler = () => openPopup(e, func);
        popup_builder.close = closePopup;

        popup_builder.pushOptions({
            missingMode: 'disable',
            wide: true
        });

        func(popup_builder);
        render(page.render(), popup_el);

        // We need to know popup width and height
        let give_focus = !popup_el.classList.contains('active');
        popup_el.style.visibility = 'hidden';
        popup_el.classList.add('active');

        // Try different positions
        {
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
            if (pos.x > window.innerWidth - popup_el.offsetWidth - 10) {
                pos.x = origin.x - popup_el.offsetWidth;
                if (pos.x < 10) {
                    pos.x = Math.min(origin.x, window.innerWidth - popup_el.offsetWidth - 10);
                    pos.x = Math.max(pos.x, 10);
                }
            }
            if (pos.y > window.innerHeight - popup_el.offsetHeight - 10) {
                pos.y = origin.y - popup_el.offsetHeight;
                if (pos.y < 10) {
                    pos.y = Math.min(origin.y, window.innerHeight - popup_el.offsetHeight - 10);
                    pos.y = Math.max(pos.y, 10);
                }
            }

            popup_el.style.left = pos.x + 'px';
            popup_el.style.top = pos.y + 'px';
        }

        if (e.stopPropagation)
            e.stopPropagation();

        // Reveal!
        popup_el.style.visibility = 'visible';

        if (give_focus) {
            // Avoid shrinking popups
            popup_el.style.minWidth = popup_el.offsetWidth + 'px';

            // Give focus to first input
            let first_widget = popup_el.querySelector(`.af_widget input, .af_widget select,
                                                       .af_widget button, .af_widget textarea`);
            if (first_widget)
                first_widget.focus();
        }
    }

    function initPopup() {
        popup_el = document.createElement('div');
        popup_el.setAttribute('id', 'gp_popup');
        document.body.appendChild(popup_el);

        popup_el.addEventListener('keydown', e => {
            switch (e.keyCode) {
                case 13: {
                    if (e.target.tagName !== 'BUTTON' && e.target.tagName !== 'A' &&
                            popup_builder.submit)
                        popup_builder.submit();
                } break;
                case 27: { closePopup(); } break;
            }
        });

        popup_el.addEventListener('click', e => e.stopPropagation());
        document.addEventListener('click', closePopup);
    }

    function closePopup() {
        popup_state = new PageState;
        popup_builder = null;

        if (popup_el) {
            popup_el.classList.remove('active');
            popup_el.style.minWidth = '';
            render('', popup_el);
        }
    }
};
