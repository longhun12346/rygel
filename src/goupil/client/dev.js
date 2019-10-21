// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

let dev = new function() {
    let self = this;

    let assets;
    let assets_map;
    let current_asset;

    let current_record;

    let left_panel;
    let show_overview;

    let editor_el;
    let editor;
    let editor_history_cache = new LruMap(32);
    let editor_path;

    this.init = async function() {
        try {
            await loadApplication();
        } catch (err) {
            // The user can still fix main.js or reset the project
        }
    };

    this.go = async function(url = null, args = {}) {
        // Find relevant asset
        if (url) {
            url = new URL(url, window.location.href);

            let path = url.pathname.substr(env.base_url.length);
            current_asset = path ? assets_map[path] : assets[0];
        }

        // Load record (if needed)
        if (current_asset && current_asset.form) {
            if (!args.hasOwnProperty('id') && current_asset.form.key !== current_record.table)
                current_record = {};
            if (args.hasOwnProperty('id') || current_record.id == null) {
                if (args.id == null) {
                    current_record = g_records.create(current_asset.form.key);
                } else if (args.id !== current_record.id) {
                    current_record = await g_records.load(current_asset.form.key, args.id);
                    if (!current_record)
                        current_record = g_records.create(current_asset.form.key);
                }
            }
        }

        // Render menu and page layout
        renderDev();

        // Run appropriate module
        if (current_asset) {
            if (current_asset.category) {
                document.title = `${current_asset.category} :: ${current_asset.label} — ${env.project_key}`;
            } else {
                document.title = `${current_asset.label} — ${env.project_key}`;
            }

            switch (left_panel) {
                case 'editor': { syncEditor(); } break;
                case 'data': { await dev_data.runTable(current_asset.form.key, current_record.id); } break;
            }

            // TODO: Deal with unknown type / renderEmpty()
            await wrapWithLog(runAsset);
        } else {
            document.title = env.project_key;
            log.error('Asset not available');
        }
    };

    async function loadApplication() {
        let prev_assets = assets;

        // Main script, it must always be there
        assets = [{
            type: 'main',
            key: 'main',
            label: 'Script CRF',

            path: 'main.js'
        }];

        try {
            let main_script = await loadScript('main.js');

            await listMainAssets(main_script, assets);
            await listBlobAssets(assets);
        } catch (err) {
            if (prev_assets.length)
                assets = prev_assets;

            throw err;
        } finally {
            assets_map = {};
            for (let asset of assets)
                assets_map[asset.key] = asset;
            current_asset = assets[0];

            current_record = {};

            left_panel = 'editor';
            show_overview = true;
        }
    }

    async function listMainAssets(script, assets) {
        let forms = [];
        let schedules = [];

        let app_builder = new ApplicationBuilder(forms, schedules);

        let func = Function('app', script);
        func(app_builder);

        for (let form of forms) {
            for (let page of form.pages) {
                assets.push({
                    type: 'page',
                    key: `${form.key}/${page.key}`,
                    category: `Formulaire '${form.key}'`,
                    label: `Page '${page.key}'`,

                    form: form,
                    page: page,
                    path: goupil.makePagePath(page.key)
                });
            }
        }

        for (let schedule of schedules) {
            assets.push({
                type: 'schedule',
                key: schedule.key,
                category: 'Agendas',
                label: `Agenda '${schedule.key}'`,

                schedule: schedule
            });
        }
    }

    async function listBlobAssets(assets) {
        let paths = await g_files.list();
        let known_paths = new Set(assets.map(asset => asset.path));

        for (let path of paths) {
            if (!known_paths.has(path)) {
                assets.push({
                    type: 'blob',
                    key: path,
                    category: 'Fichiers',
                    label: path,

                    path: path
                });
            }
        }
    }

    function renderDev() {
        let modes = [];
        if (current_asset) {
            if (current_asset.path)
                modes.push(['editor', 'Editeur']);
            if (current_asset.form)
                modes.push(['data', 'Données']);
        }

        if (left_panel && modes.length && !modes.find(mode => mode[0] === left_panel))
            left_panel = modes[0][0] || null;
        if (!left_panel)
            show_overview = true;

        render(html`
            <nav id="gp_menu" class="gp_toolbar">
                ${modes.map(mode =>
                    html`<button class=${left_panel === mode[0] ? 'active' : ''} @click=${e => toggleLeftPanel(mode[0])}>${mode[1]}</button>`)}
                ${modes.length ?
                    html`<button class=${show_overview ? 'active': ''} @click=${e => toggleOverview()}>Aperçu</button>` : ''}

                <select id="dev_assets" @change=${e => self.go(e.target.value)}>
                    ${!current_asset ? html`<option>-- Select an asset --</option>` : ''}
                    ${util.mapRLE(assets, asset => asset.category, (category, offset, len) => {
                        if (category) {
                            return html`<optgroup label=${category}>${util.mapRange(offset, offset + len, idx => {
                                let asset = assets[idx];
                                return html`<option value=${'/' + asset.key}
                                                    .selected=${asset === current_asset}>${asset.label}</option>`;
                            })}</optgroup>`;
                        } else {
                            return util.mapRange(offset, offset + len, idx => {
                                let asset = assets[idx];
                                return html`<option value=${asset.key}
                                                    .selected=${asset === current_asset}>${asset.label}</option>`;
                            });
                        }
                    })}
                </select>

                <button @click=${showCreateDialog}>Ajouter</button>
                <button ?disabled=${!current_asset || current_asset.type !== 'blob'}
                        @click=${e => showDeleteDialog(e, current_asset)}>Supprimer</button>
                <button @click=${showResetDialog}>Réinitialiser</button>
            </nav>

            <main>
                ${left_panel === 'editor' ?
                    makeEditorElement(show_overview ? 'dev_panel_left' : 'dev_panel_fixed') : ''}
                ${left_panel === 'data' ?
                    html`<div id="dev_data" class=${show_overview ? 'dev_panel_left' : 'dev_panel_fixed'}></div>` : ''}
                ${show_overview ?
                    html`<div id="dev_overview" class=${left_panel ? 'dev_panel_right' : 'dev_panel_page'}></div>` : ''}

                <div id="dev_log" style="display: none;"></div>
            </main>
        `, document.body);
    }

    function toggleLeftPanel(mode) {
        if (left_panel !== mode) {
            left_panel = mode;
        } else {
            left_panel = null;
            show_overview = true;
        }

        self.go();
    }

    function toggleOverview() {
        if (!left_panel)
            left_panel = 'editor';
        show_overview = !show_overview;

        self.go();
    }

    function showCreateDialog(e) {
        popup.form(e, page => {
            let type = page.choice('type', 'Type :', [['blob', 'Fichier'], ['page', 'Page']],
                                   {mandatory: true, untoggle: false, value: 'blob'});

            let blob;
            let key;
            let path;
            switch (type.value) {
                case 'blob': {
                    blob = page.file('file', 'Fichier :', {mandatory: true});
                    key = page.text('key', 'Clé :', {placeholder: blob.value ? blob.value.name : null});
                    if (!key.value && blob.value)
                        key.value = blob.value.name;
                    if (key.value)
                        path = goupil.makeBlobPath(key.value);
                } break;

                case 'page': {
                    key = page.text('key', 'Clé :', {mandatory: true});
                    if (key.value)
                        path = goupil.makePagePath(key.value);
                } break;
            }

            if (path) {
                if (assets.some(asset => asset.path === path))
                    key.error('Cette ressource existe déjà');
                if (!key.value.match(/^[a-zA-Z_\.][a-zA-Z0-9_\.]*$/))
                    key.error('Autorisé : a-z, _, . et 0-9 (sauf initiale)');
            }

            page.submitHandler = async () => {
                switch (type.value) {
                    case 'blob': { g_files.save(g_files.create(path, blob.value)); } break;
                    case 'page': { g_files.save(g_files.create(path, '')); } break;
                }

                let asset = {
                    path: path,
                    type: type.value
                };
                assets.push(asset);
                assets.sort();
                assets_map[path] = asset;

                page.close();
                self.go(path);
            };
            page.buttons(page.buttons.std.ok_cancel('Créer'));
        });
    }

    function showDeleteDialog(e, asset) {
        popup.form(e, page => {
            page.output(`Voulez-vous vraiment supprimer la ressource '${asset.label}' ?`);

            page.submitHandler = async () => {
                if (asset.path)
                    await g_files.delete(asset.path);

                // Remove from assets array and map
                let asset_idx = assets.findIndex(it => it.key === asset.key);
                assets.splice(asset_idx, 1);
                delete assets_map[asset.key];

                if (asset === current_asset)
                    current_asset = assets[0];

                page.close();
                self.go();
            };
            page.buttons(page.buttons.std.ok_cancel('Supprimer'));
        });
    }

    function showResetDialog(e) {
        popup.form(e, page => {
            page.output('Voulez-vous vraiment réinitialiser toutes les ressources ?');

            page.submitHandler = async () => {
                await g_files.transaction(m => {
                    m.clear();

                    for (let path in help_demo) {
                        let data = help_demo[path];
                        let file = g_files.create(path, data);

                        m.save(file);
                    }
                });
                editor_history_cache.clear();

                await self.init();
                editor_path = null;

                page.close();
                self.go();
            };
            page.buttons(page.buttons.std.ok_cancel('Réinitialiser'));
        });
    }

    function makeEditorElement(cls) {
        if (!editor_el) {
            editor_el = document.createElement('div');
            editor_el.id = 'dev_editor';
        }

        for (let cls of editor_el.classList) {
            if (!cls.startsWith('ace_') && !cls.startsWith('ace-'))
                editor_el.classList.remove(cls);
        }
        editor_el.classList.add(cls);

        return editor_el;
    }

    async function syncEditor() {
        // FIXME: Make sure we don't run loadScript more than once
        if (!window.ace)
            await util.loadScript(`${env.base_url}static/ace.js`);

        if (!editor) {
            editor = ace.edit(editor_el);

            editor.setTheme('ace/theme/monokai');
            editor.setShowPrintMargin(false);
            editor.setFontSize(12);
            editor.session.setOption('useWorker', false);
            editor.session.setMode('ace/mode/javascript');

            editor.on('change', handleEditorChange);
        }

        if (current_asset) {
            let history = editor_history_cache.get(current_asset.path);

            if (history !== editor.session.getUndoManager()) {
                if (editor_path !== current_asset.path) {
                    let script = await loadScript(current_asset.path);
                    editor.setValue(script);
                }
                editor.setReadOnly(false);
                editor.clearSelection();

                if (!history) {
                    history = new ace.UndoManager();
                    editor_history_cache.set(current_asset.path, history);
                }
                editor.session.setUndoManager(history);
            }

            editor_path = current_asset.path;
        } else {
            editor.setValue('');
            editor.setReadOnly(true);

            editor.session.setUndoManager(new ace.UndoManager());

            editor_path = null;
        }
    }

    async function handleEditorChange() {
        // This test saves us from events after setValue() calls
        if (editor_path === current_asset.path) {
            let success;
            if (current_asset.path === 'main.js') {
                success = await wrapWithLog(async () => {
                    await loadApplication();
                    await self.go();
                });
            } else {
                success = await wrapWithLog(runAsset);
            }

            if (success) {
                let file = g_files.create(current_asset.path, editor.getValue());
                await g_files.save(file);
            }
        }
    }

    async function loadScript(path) {
        if (path === editor_path) {
            return editor.getValue();
        } else {
            let file = await g_files.load(path);
            return file ? file.data : '';
        }
    }

    async function wrapWithLog(func) {
        let log_el = document.querySelector('#dev_log');
        // We still need to render the form to test it, so create a dummy element!
        let page_el = document.querySelector('#dev_overview') || document.createElement('div');

        try {
            await func();

            // Things are OK!
            log_el.innerHTML = '';
            log_el.style.display = 'none';
            page_el.classList.remove('dev_broken');

            return true;
        } catch (err) {
            let err_line = util.parseEvalErrorLine(err);

            log_el.textContent = `⚠\uFE0E Line ${err_line || '?'}: ${err.message}`;
            log_el.style.display = 'block';
            page_el.classList.add('dev_broken');

            return false;
        }
    }

    async function runAsset() {
        switch (current_asset.type) {
            case 'page': {
                let script = await loadScript(current_asset.path);
                await dev_form.runPageScript(script, current_record);
            } break;

            case 'schedule': { await dev_schedule.run(current_asset.schedule); } break;
        }
    }
};
