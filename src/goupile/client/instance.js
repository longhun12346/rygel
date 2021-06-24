// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

function InstanceController() {
    let self = this;

    let app;

    let route = {
        page: null,
        ulid: null,
        version: null
    };

    let code_cache = new LruMap(4);
    let page_code;

    let form_meta;
    let form_state;
    let data_rows;

    let editor_el;
    let editor_ace;
    let editor_filename;
    let editor_buffers = new LruMap(32);
    let editor_ignore_change = false;

    let develop = false;
    let error_entries = {};

    this.start = async function() {
        initUI();
        await initApp();

        self.go(null, window.location.href);
    };

    this.hasUnsavedData = function() {
        return form_state != null && form_state.hasChanged();
    };

    async function initApp() {
        let code = await fetchCode('main.js');

        try {
            let new_app = new ApplicationInfo;
            let builder = new ApplicationBuilder(new_app);

            runUserCode('Application', code, {
                app: builder
            });
            if (!new_app.pages.size)
                throw new Error('Main script does not define any page');

            new_app.home = new_app.pages.values().next().value;
            app = Object.freeze(new_app);
        } catch (err) {
            if (app == null) {
                let new_app = new ApplicationInfo;
                let builder = new ApplicationBuilder(new_app);

                // For simplicity, a lot of code assumes at least one page exists
                builder.page("default", "Page par défaut");

                new_app.home = new_app.pages.values().next().value;
                app = Object.freeze(new_app);
            }
        }
    }

    function initUI() {
        document.documentElement.className = 'instance';

        ui.setMenu(() => html`
            <button class="icon" style="background-position-y: calc(-538px + 1.2em);"
                    @click=${e => self.go(e, ENV.base_url)}>${ENV.title}</button>
            ${goupile.hasPermission('develop') ? html`
                <button class=${'icon' + (ui.isPanelEnabled('editor') ? ' active' : '')}
                        style="background-position-y: calc(-230px + 1.2em);"
                        @click=${ui.wrapAction(e => togglePanel(e, 'editor'))}>Code</button>
            ` : ''}
            <button class=${'icon' + (ui.isPanelEnabled('data') ? ' active' : '')}
                    style="background-position-y: calc(-274px + 1.2em);"
                    @click=${ui.wrapAction(e => togglePanel(e, 'data'))}>Suivi</button>
            <button class=${'icon' + (ui.isPanelEnabled('page') ? ' active' : '')}
                    style="background-position-y: calc(-318px + 1.2em);"
                    @click=${ui.wrapAction(e => togglePanel(e, 'page'))}>Formulaire</button>

            <div style="flex: 1; min-width: 20px;"></div>
            ${ui.isPanelEnabled('editor') || ui.isPanelEnabled('page') ? html`
                ${util.map(app.pages.values(), page => {
                    let missing = page.dependencies.some(dep => !form_meta.status.has(dep));
                    return html`<button class=${page === route.page ? 'active' : ''} ?disabled=${missing}
                                        @click=${ui.wrapAction(e => self.go(e, page.url))}>${page.title}</button>`;
                })}
                <div style="flex: 1; min-width: 20px;"></div>
            ` : ''}

            <div class="drop right">
                <button class="icon" style="background-position-y: calc(-494px + 1.2em)">${profile.username}</button>
                <div>
                    <button @click=${ui.wrapAction(goupile.logout)}>Se déconnecter</button>
                </div>
            </div>
        `);

        ui.createPanel('editor', 0, false, () => {
            let tabs = [];
            tabs.push({
                title: 'Application',
                filename: 'main.js'
            });
            tabs.push({
                title: 'Formulaire',
                filename: route.page.filename
            });

            // Ask ACE to adjust if needed, it needs to happen after the render
            setTimeout(() => editor_ace.resize(false), 0);

            return html`
                <div style="--menu_color: #383936;">
                    <div class="ui_toolbar">
                        ${tabs.map(tab => html`<button class=${editor_filename === tab.filename ? 'active' : ''}
                                                       @click=${ui.wrapAction(e => toggleEditorFile(tab.filename))}>${tab.title}</button>`)}
                        <div style="flex: 1;"></div>
                        <button @click=${ui.wrapAction(runDeployDialog)}>Déployer</button>
                    </div>

                    ${editor_el}
                </div>
            `;
        });

        ui.createPanel('data', 0, true, () => {
            return html`
                <div class="padded">
                    <div class="ui_quick">
                        ${data_rows.length || 'Aucune'} ${data_rows.length > 1 ? 'lignes' : 'ligne'}
                        <div style="flex: 1;"></div>
                        <a @click=${ui.wrapAction(e => { data_rows = null; return self.go(e); })}>Rafraichir</a>
                    </div>

                    <table class="ui_table" id="ins_data">
                        <colgroup>
                            <col style="width: 2em;"/>
                            <col style="width: 160px;"/>
                            ${util.mapRange(0, app.pages.size, () => html`<col/>`)}
                        </colgroup>

                        <thead>
                            <tr>
                                <th></th>
                                <th>Identifiant</th>
                                ${util.map(app.pages.values(), page => html`<th>${page.title}</th>`)}
                            </tr>
                        </thead>

                        <tbody>
                            ${data_rows.map(row => html`
                                <tr class=${row.ulid === route.ulid ? 'active' : ''}>
                                    <td>
                                        <a @click=${ui.wrapAction(e => runDeleteRecordDialog(e, row.ulid))}>✕</a>
                                    </td>
                                    <td class=${row.hid == null ? 'missing' : ''}>${row.hid != null ? row.hid : 'NA'}</td>
                                    ${util.map(app.pages.values(), page => {
                                        let url = page.url + `/${row.ulid}`;

                                        if (row.status.has(page.key)) {
                                            return html`<td class="saved"><a href=${url}
                                                                             @click=${e => ui.setPanelState('page', true, false)}>Enregistré</a></td>`;
                                        } else {
                                            return html`<td class="missing"><a href=${url}
                                                                            @click=${e => ui.setPanelState('page', true, false)}>Non rempli</a></td>`;
                                        }
                                    })}
                                </tr>
                            `)}
                            ${!data_rows.length ? html`<tr><td colspan=${2 + app.pages.size}>Aucune ligne à afficher</td></tr>` : ''}
                        </tbody>
                    </table>

                    <div class="ui_actions">
                        <button @click=${ui.wrapAction(goNewRecord)}>Créer un nouvel enregistrement</button>
                    </div>
                </div>
            `;
        });

        ui.createPanel('page', 1, false, () => {
            let readonly = (route.version < form_meta.fragments.length);

            let model = new FormModel;
            let builder = new FormBuilder(form_state, model, readonly);

            let error;
            try {
                let meta = util.assignDeep({}, form_meta);
                runUserCode('Formulaire', page_code, {
                    form: builder,
                    values: form_state.values,
                    meta: meta,
                    go: (url) => self.go(null, url)
                });
                form_meta.hid = meta.hid;

                if (model.hasErrors())
                    builder.errorList();

                if (model.variables.length) {
                    let enable_save = !model.hasErrors() && form_state.hasChanged();

                    builder.action('Enregistrer', {disabled: !enable_save}, () => {
                        if (builder.triggerErrors())
                            return saveRecord();
                    });
                    builder.action('-');
                    if (route.version > 0) {
                        builder.action('Nouveau', {}, goNewRecord);
                    } else {
                        builder.action('Réinitialiser', {disabled: !form_state.hasChanged()}, goNewRecord);
                    }
                }
            } catch (err) {
                error = err;
            }

            return html`
                <div>
                    <div id="ins_page">
                        <div class="ui_quick">
                            ${model.variables.length ? html`
                                ${!route.version ? 'Nouvel enregistrement' : ''}
                                ${route.version > 0 && form_meta.hid != null ? `Enregistrement : ${form_meta.hid}` : ''}
                                ${route.version > 0 && form_meta.hid == null ? 'Enregistrement local' : ''}
                            `  : ''}
                            <div style="flex: 1;"></div>
                            ${route.version > 0 ? html`
                                ${route.version < form_meta.fragments.length ?
                                    html`<span style="color: red;">Ancienne version du ${form_meta.mtime.toLocaleString()}</span>` : ''}
                                ${route.version === form_meta.fragments.length ? html`<span>Version actuelle</span>` : ''}

                                &nbsp;(<a @click=${ui.wrapAction(e => runTrailDialog(e, route.ulid))}>historique</a>)
                            ` : ''}
                        </div>

                        ${error == null ? html`
                            <form id="ins_form" @submit=${e => e.preventDefault()}>
                                ${model.render()}
                            </form>
                        ` : ''}
                        ${error != null ? html`<span class="ui_wip">${error.message}</span>` : ''}
                    </div>

                    ${develop ? html`
                        <div style="flex: 1;"></div>
                        <div id="ins_notice">
                            Formulaires en développement<br/>
                            Déployez les avant d'enregistrer des données
                        </div>
                    ` : ''}
                </div>
            `;
        });

        ui.setPanelState('data', true);
    };

    function togglePanel(e, key, enable = null) {
        ui.setPanelState(key, !ui.isPanelEnabled(key));
        return self.go(e);
    }

    function runUserCode(title, code, arguments) {
        let entry = error_entries[title];
        if (entry == null) {
            entry = new log.Entry;
            error_entries[title] = entry;
        }

        try {
            let func = new Function(...Object.keys(arguments), code);
            func(...Object.values(arguments));

            entry.close();
        } catch (err) {
            let line = util.parseEvalErrorLine(err);
            let msg = `Erreur sur ${title}\n${line != null ? `Ligne ${line} : ` : ''}${err.message}`;

            entry.error(msg, -1);
            throw new Error(msg);
        }
    }

    function runTrailDialog(e, ulid) {
        return ui.runDialog(e, (d, resolve, reject) => {
            if (ulid !== route.ulid)
                reject();

            d.output(html`
                <table class="ui_table">
                    <tbody>
                        ${util.mapRange(0, form_meta.fragments.length, idx => {
                            let version = form_meta.fragments.length - idx;
                            let fragment = form_meta.fragments[version - 1];

                            let page = app.pages.get(fragment.page) || route.page;
                            let url = page.url + `/${ulid}@${version}`;

                            return html`
                                <tr class=${version === route.version ? 'active' : ''}>
                                    <td><a href=${url}>🔍\uFE0E</a></td>
                                    <td>${fragment.user}</td>
                                    <td>${fragment.mtime.toLocaleString()}</td>
                                </tr>
                            `;
                        })}
                    </tbody>
                </table>
            `);
        });
    }

    function goNewRecord(e) {
        let url = route.page.url + '/new';
        return self.go(e, url);
    }

    async function saveRecord() {
        if (develop)
            throw new Error('Enregistrement refusé : formulaire non déployé');

        let progress = log.progress('Enregistrement en cours');

        try {
            enablePersistence();

            let values = Object.assign({}, form_state.values);
            for (let key in values) {
                if (values[key] === undefined)
                    values[key] = null;
            }

            let key = `${profile.username}:processus@${form_meta.ulid}`;
            let fragment = {
                user: profile.username,
                mtime: new Date,
                page: route.page.key,
                values: values
            };

            await db.transaction('rw', ['rec_records'], async t => {
                let enc = await db.load('rec_records', key);

                let record;
                if (enc != null) {
                    record = await goupile.decryptWithPassport(enc);
                    if (form_meta.hid != null)
                        record.hid = form_meta.hid;
                } else {
                    record = {
                        store: 'processus',
                        ulid: form_meta.ulid,
                        hid: form_meta.hid,
                        fragments: []
                    };
                }

                if (form_meta.version !== record.fragments.length)
                    throw new Error('Cannot overwrite old record fragment');
                record.fragments.push(fragment);

                enc = await goupile.encryptWithPassport(record);
                await db.saveWithKey('rec_records', key, enc);
            });

            progress.success('Enregistrement effectué');

            // Trigger reloads
            route.version = null;
            form_meta = null;
            data_rows = null;

            self.go();
        } catch (err) {
            progress.close();
            throw err;
        }
    }

    function runDeleteRecordDialog(e, ulid) {
        return ui.runConfirm(e, 'Voulez-vous vraiment supprimer cet enregistrement ?', 'Supprimer', async () => {
            let progress = log.progress('Suppression en cours');

            try {
                let key = `${profile.username}:processus@${ulid}`;
                let fragment = {
                    user: profile.username,
                    mtime: new Date,
                    page: null,
                    values: {}
                };

                await db.transaction('rw', ['rec_records'], async () => {
                    let enc = await db.load('rec_records', key);
                    if (enc == null)
                        throw new Error('Cet enregistrement est introuvable');

                    let record = await goupile.decryptWithPassport(enc);

                    record.fragments.push(fragment);

                    enc = await goupile.encryptWithPassport(record);
                    await db.saveWithKey('rec_records', key, enc);
                });

                progress.success('Suppression effectuée');

                if (route.ulid === ulid) {
                    route.ulid = null;
                    route.version = null;
                }
                data_rows = null;

                self.go();
            } catch (err) {
                progress.close();
                throw err;
            }
        })
    }

    function enablePersistence() {
        let storage_warning = 'Impossible d\'activer le stockage local persistant';

        if (navigator.storage && navigator.storage.persist) {
            navigator.storage.persist().then(granted => {
                if (granted) {
                    console.log('Stockage persistant activé');
                } else {
                    console.log(storage_warning);
                }
            });
        } else {
            console.log(storage_warning);
        }
    }

    async function syncEditor() {
        if (editor_el == null) {
            if (typeof ace === 'undefined')
                await net.loadScript(`${ENV.base_url}static/ace.js`);

            editor_el = document.createElement('div');
            editor_el.setAttribute('style', 'flex: 1;');
            editor_ace = ace.edit(editor_el);

            editor_ace.setTheme('ace/theme/merbivore_soft');
            editor_ace.setShowPrintMargin(false);
            editor_ace.setFontSize(13);
            editor_ace.setBehavioursEnabled(false);
        }

        let buffer = editor_buffers.get(editor_filename);

        if (buffer == null) {
            let code = await fetchCode(editor_filename);

            let session = new ace.EditSession('', 'ace/mode/javascript');
            session.setOption('useWorker', false);
            session.setUseWrapMode(true);
            session.doc.setValue(code);
            session.setUndoManager(new ace.UndoManager());
            session.on('change', e => handleFileChange(editor_filename));

            buffer = {
                session: session,
                reload: false
            };
            editor_buffers.set(editor_filename, buffer);
        } else if (buffer.reload) {
            let code = await fetchCode(editor_filename);

            editor_ignore_change = true;
            buffer.session.doc.setValue(code);
            editor_ignore_change = false;

            buffer.reload = false;
        }

        editor_ace.setSession(buffer.session);
    }

    function toggleEditorFile(filename) {
        editor_filename = filename;
        return self.go();
    }

    async function handleFileChange(filename) {
        if (editor_ignore_change)
            return;

        let buffer = editor_buffers.get(filename);

        // Should never fail, but who knows..
        if (buffer != null) {
            let key = `${profile.username}:${filename}`;
            let blob = new Blob([buffer.session.doc.getValue()]);
            let sha256 = await computeSha256(blob);

            await db.saveWithKey('fs_files', key, {
                filename: filename,
                size: blob.size,
                sha256: await computeSha256(blob),
                blob: blob
            });
        }

        self.go();
    }

    // Javascript Blob/File API sucks, plain and simple
    async function computeSha256(blob) {
        let sha256 = new Sha256;

        for (let offset = 0; offset < blob.size; offset += 65536) {
            let piece = blob.slice(offset, offset + 65536);
            let buf = await new Promise((resolve, reject) => {
                let reader = new FileReader;

                reader.onload = e => resolve(e.target.result);
                reader.onerror = e => {
                    reader.abort();
                    reject(new Error(e.target.error));
                };

                reader.readAsArrayBuffer(piece);
            });

            sha256.update(buf);
        }

        return sha256.finalize();
    }

    // XXX: Broken refresh, etc
    async function runDeployDialog(e) {
        let actions = await computeDeployActions();
        let modifications = actions.reduce((acc, action) => acc + (action.type !== 'noop'), 0);

        return ui.runDialog(null, (d, resolve, reject) => {
            d.output(html`
                <div class="ui_quick">
                    ${modifications || 'Aucune'} ${modifications.length > 1 ? 'modifications' : 'modification'} à effectuer
                    <div style="flex: 1;"></div>
                    <a @click=${ui.wrapAction(e => { actions = null; return self.go(); })}>Rafraichir</a>
                </div>

                <table class="ui_table ins_deploy">
                    <colgroup>
                        <col style="width: 2.4em;"/>
                        <col/>
                        <col style="width: 4.6em;"/>
                        <col style="width: 8em;"/>
                        <col/>
                        <col style="width: 4.6em;"/>
                    </colgroup>

                    <tbody>
                        ${actions.map(action => {
                            let local_cls = 'path';
                            if (action.type === 'pull') {
                                local_cls += action.remote_sha256 ? ' overwrite' : ' overwrite delete';
                            } else if (action.local_sha256 == null) {
                                local_cls += ' virtual';
                            }

                            let remote_cls = 'path';
                            if (action.type === 'push') {
                                remote_cls += action.local_sha256 ? ' overwrite' : ' overwrite delete';
                            } else if (action.remote_sha256 == null) {
                                remote_cls += ' none';
                            }

                            return html`
                                <tr class=${action.type === 'conflict' ? 'conflict' : ''}>
                                    <td>
                                        <a @click=${ui.wrapAction(e => runDeleteFileDialog(e, action.filename))}>x</a>
                                    </td>
                                    <td class=${local_cls}>${action.filename}</td>
                                    <td class="size">${action.local_sha256 ? util.formatDiskSize(action.local_size) : ''}</td>

                                    <td class="actions">
                                        <a class=${action.type === 'pull' ? 'active' : (action.local_sha256 == null || action.local_sha256 === action.remote_sha256 ? 'disabled' : '')}
                                           @click=${e => { action.type = 'pull'; d.restart(); }}>&lt;</a>
                                        <a class=${action.type === 'push' ? 'active' : (action.local_sha256 == null || action.local_sha256 === action.remote_sha256 ? 'disabled' : '')}
                                           @click=${e => { action.type = 'push'; d.restart(); }}>&gt;</a>
                                    </td>

                                    <td class=${remote_cls}>${action.filename}</td>
                                    <td class="size">
                                        ${action.type === 'conflict' ? html`<span class="conflict" title="Modifications en conflit">⚠\uFE0E</span>&nbsp;` : ''}
                                        ${action.remote_sha256 ? util.formatDiskSize(action.remote_size) : ''}
                                    </td>
                                </tr>
                            `;
                        })}
                    </tbody>
                </table>

                <div class="ui_quick">
                    <div style="flex: 1;"></div>
                    <a @click=${ui.wrapAction(runAddFileDialog)}>Ajouter un fichier</a>
                </div>
            `);

            d.action('Déployer', {disabled: !d.isValid()}, async () => {
                await deploy(actions);

                resolve();
                self.go();
            });
        });
    }

    async function computeDeployActions() {
        let [local_files, remote_files] = await Promise.all([
            db.loadAll('fs_files'),
            net.fetchJson(`${ENV.base_url}api/files/list`)
        ]);

        let local_map = util.arrayToObject(local_files, file => file.filename);
        // XXX: let sync_map = util.arrayToObject(sync_files, file => file.filename);
        let remote_map = util.arrayToObject(remote_files, file => file.filename);

        let actions = [];

        // Handle local files
        for (let local_file of local_files) {
            let remote_file = remote_map[local_file.filename];
            let sync_file = remote_file; // XXX: sync_map[local_file.filename];

            if (remote_file != null) {
                if (local_file.sha256 != null) {
                    if (local_file.sha256 === remote_file.sha256) {
                        actions.push(makeDeployAction(local_file.filename, local_file, remote_file, 'noop'));
                    } else if (sync_file != null && sync_file.sha256 === remote_file.sha256) {
                        actions.push(makeDeployAction(local_file.filename, local_file, remote_file, 'push'));
                    } else {
                        actions.push(makeDeployAction(local_file.filename, local_file, remote_file, 'conflict'));
                    }
                } else {
                    actions.push(makeDeployAction(local_file.filename, local_file, remote_file, 'noop'));
                }
            } else {
                if (sync_file == null) {
                    actions.push(makeDeployAction(local_file.filename, local_file, null, 'push'));
                } else if (sync_file.sha256 === local_file.sha256) {
                    actions.push(makeDeployAction(local_file.filename, local_file, null, 'pull'));
                } else {
                    actions.push(makeDeployAction(local_file.filename, local_file, null, 'conflict'));
                }
            }
        }

        // Pull remote-only files
        {
            // let new_sync_files = [];

            for (let remote_file of remote_files) {
                if (local_map[remote_file.filename] == null) {
                    actions.push(makeDeployAction(remote_file.filename, null, remote_file, 'noop'));

                    //new_sync_files.push(remote_file);
                }
            }

            // await db.saveAll('fs_sync', new_sync_files);
        }

        return actions;
    };

    function makeDeployAction(filename, local, remote, type) {
        let action = {
            filename: filename,
            type: type
        };

        if (local != null) {
            action.local_size = local.size;
            action.local_sha256 = local.sha256;
        }
        if (remote != null) {
            action.remote_size = remote.size;
            action.remote_sha256 = remote.sha256;
        }

        return action;
    }

    function runAddFileDialog(e) {
        return ui.runDialog(e, (d, resolve, reject) => {
            let file = d.file('*file', 'Fichier :');
            let filename = d.text('*filename', 'Chemin :', {value: file.value ? file.value.name : null});

            if (filename.value) {
                if (!filename.value.match(/^[A-Za-z0-9_\.]+(\/[A-Za-z0-9_\.]+)*$/))
                    filename.error('Caractères autorisés: a-z, 0-9, \'_\', \'.\' et \'/\' (pas au début ou à la fin)');
                if (filename.value.includes('/../') || filename.value.endsWith('/..'))
                    filename.error('Le chemin ne doit pas contenir de composants \'..\'');
            }

            d.action('Créer', {disabled: !d.isValid()}, async () => {
                let progress = new log.Entry;

                progress.progress('Enregistrement du fichier');
                try {
                    let key = `${profile.username}:${filename.value}`;
                    await db.saveWithKey('fs_files', key, {
                        filename: filename.value,
                        size: file.value.size,
                        sha256: await computeSha256(filename.value),
                        blob: file.value
                    })

                    let buffer = editor_buffers.get(file.filename);
                    if (buffer != null)
                        buffer.reload = true;

                    progress.success('Fichier enregistré');
                    resolve();

                    self.go();
                } catch (err) {
                    progress.close();
                    reject(err);
                }
            });
        });
    }

    function runDeleteFileDialog(e, filename) {
        log.error('NOT IMPLEMENTED');
    }

    async function deploy(actions) { // XXX: reentrancy
        let progress = log.progress('Déploiement en cours');

        try {
            if (actions.some(action => action.type == 'conflict'))
                throw new Error('Conflits non résolus');

            for (let i = 0; i < actions.length; i += 10) {
                let p = actions.slice(i, i + 10).map(async action => {
                    switch (action.type) {
                        case 'push': {
                            let url = util.pasteURL(`${ENV.base_url}files/${action.filename}`, {sha256: action.remote_sha256 || ''});

                            if (action.local_sha256 != null) {
                                let key = `${profile.username}:${action.filename}`;
                                let file = await db.load('fs_files', key);

                                let response = await net.fetch(url, {method: 'PUT', body: file.blob});
                                if (!response.ok) {
                                    let err = (await response.text()).trim();
                                    throw new Error(err);
                                }

                                await db.delete('fs_files', key);
                            } else {
                                let response = await net.fetch(url, {method: 'DELETE'});
                                if (!response.ok && response.status !== 404) {
                                    let err = (await response.text()).trim();
                                    throw new Error(err);
                                }
                            }
                        } break;

                        case 'noop':
                        case 'pull': {
                            let key = `${profile.username}:${action.filename}`;
                            await db.delete('fs_files', key);

                            let buffer = editor_buffers.get(action.filename);
                            if (buffer != null)
                                buffer.reload = true;
                        } break;
                    }
                });
                await Promise.all(p);
            }

            progress.success('Déploiement effectué');
        } catch (err) {
            progress.close();
            throw err;
        } finally {
            code_cache.clear();
        }
    };

    this.go = async function(e = null, url = null, push_history = true) {
        try {
            await goupile.syncProfile();
            if (!goupile.isAuthorized()) {
                await goupile.runLogin();

                if (ENV.backup_key != null)
                    await backupClientData();
            }

            // Update current route
            if (url != null) {
                if (!url.endsWith('/'))
                    url += '/';
                url = new URL(url, window.location.href);
                if (url.pathname === ENV.base_url)
                    url = new URL(app.home.url, window.location.href);

                if (url.pathname.startsWith(`${ENV.base_url}main/`)) {
                    let path = url.pathname.substr(ENV.base_url.length + 5);
                    let [key, what] = path.split('/').map(str => str.trim());

                    // Support shorthand URLs: /main/<ULID>(@<VERSION>)
                    if (key && key.match(/^[A-Z0-9]{26}(@[0-9]+)?$/)) {
                        what = key;
                        key = app.home.key;
                    }

                    let [ulid, version] = what ? what.split('@') : [null, null];

                    // Popping history
                    if (!ulid && !push_history)
                        ulid = 'new';

                    // Confirm dangerous action (risk of data loss)
                    if ((ulid && ulid !== route.ulid || (version && version !== route.version))) {
                        if (form_state != null && form_state.hasChanged()) {
                            try {
                                await ui.runConfirm(e, "Si vous continuez, vous perdrez les modifications en cours. Voulez-vous continuer ?",
                                                       "Continuer", () => {});
                            } catch (err) {
                                // If we're popping state, this will fuck up navigation history but we can't
                                // refuse popstate events. History mess is better than data loss.
                                self.go();
                                return;
                            }
                        }
                    }

                    // Deal with page
                    route.page = app.pages.get(key);
                    if (route.page == null) {
                        log.error(`La page '${key}' n'existe pas`);
                        route.page = app.home;
                    }

                    // Deal with ULID
                    if (ulid && ulid !== route.ulid) {
                        if (ulid === 'new')
                            ulid = null;
                        route.ulid = ulid;
                        route.version = null;

                        // Help the user fill forms
                        setTimeout(() => {
                            let el = document.querySelector('#ins_page');
                            if (el != null)
                                el.parentNode.scrollTop = 0;
                        });
                        ui.setPanelState('page', true, false);
                    }

                    // And with version!
                    if (version) {
                        version = version.trim();

                        if (version.match(/^[0-9]+$/)) {
                            route.version = parseInt(version, 10);
                        } else {
                            log.error('L\'indicateur de version n\'est pas un nombre');
                            route.version = null;
                        }
                    }
                } else {
                    window.location.href = url.href;
                }
            }

            // Is the user changing stuff?
            {
                let range = IDBKeyRange.bound(profile.username + ':', profile.username + '`', false, true);
                let count = await db.count('fs_files', range);

                develop = !!count;
            }

            // Load record
            if (route.ulid == null) {
                form_meta = null;
            } else if (form_meta == null || form_meta.ulid !== route.ulid ||
                                            form_meta.version !== route.version) {
                let key = `${profile.username}:processus@${route.ulid}`;
                let enc = await db.load('rec_records', key);

                if (enc != null) {
                    try {
                        let record = await goupile.decryptWithPassport(enc);
                        let fragments = record.fragments;

                        // Deleted record
                        if (fragments[fragments.length - 1].page == null)
                            throw new Error('L\'enregistrement demandé est supprimé');

                        if (route.version == null) {
                            route.version = fragments.length;
                        } else if (route.version > fragments.length) {
                            throw new Error(`Cet enregistrement n'a pas de version ${route.version}`);
                        }

                        let values = {};
                        for (let i = 0; i < route.version; i++) {
                            let fragment = fragments[i];
                            Object.assign(values, fragment.values);
                        }
                        for (let fragment of fragments) {
                            fragment.mtime = new Date(fragment.mtime);
                            delete fragment.values;
                        }

                        form_meta = {
                            page: route.page,
                            ulid: route.ulid,
                            hid: record.hid,
                            version: route.version,
                            mtime: fragments[fragments.length - 1].mtime,
                            fragments: fragments,
                            status: new Set(fragments.map(fragment => fragment.page))
                        };

                        form_state = new FormState(values);
                        form_state.changeHandler = () => self.go(null, null, false);
                    } catch (err) {
                        log.error(err);
                        form_meta = null;
                    }
                } else {
                    log.error('L\'enregistrement demandé n\'existe pas');
                    form_meta = null;
                }
            }
            if (form_meta == null) {
                route.ulid = util.makeULID();
                route.version = 0;

                form_meta = {
                    page: route.page,
                    ulid: route.ulid,
                    hid: null,
                    version: 0,
                    mtime: null,
                    fragments: [],
                    status: new Set()
                };

                form_state = new FormState;
                form_state.changeHandler = () => self.go(null, null, false);
            }

            // Check dependencies and redirect if needed
            {
                let missing = route.page.dependencies.some(dep => !form_meta.status.has(dep));

                if (missing) {
                    let dep0 = route.page.dependencies[0];
                    route.page = app.pages.get(dep0);
                }
            }

            // Update browser URL
            {
                let url = route.page.url;
                if (route.version > 0) {
                    url += `/${route.ulid}`;
                    if (route.version < form_meta.fragments.length)
                        url += `@${route.version}`;
                }
                goupile.syncHistory(url, push_history);
            }

            // Sync editor (if needed)
            if (ui.isPanelEnabled('editor')) {
                if (editor_filename == null || editor_filename.startsWith('pages/'))
                    editor_filename = route.page.filename;

                await syncEditor();
            }

            // Load rows for data panel
            if (data_rows == null && ui.isPanelEnabled('data')) {
                let range = IDBKeyRange.bound(profile.username + ':', profile.username + '`', false, true);
                let rows = await db.loadAll('rec_records');

                data_rows = [];
                for (let enc of rows) {
                    try {
                        let record = await goupile.decryptWithPassport(enc);
                        let fragments = record.fragments;

                        // Deleted record
                        if (fragments[fragments.length - 1].page == null)
                            continue;

                        let row = {
                            ulid: record.ulid,
                            hid: record.hid,
                            status: new Set(fragments.map(fragment => fragment.page))
                        };
                        data_rows.push(row);
                    } catch (err) {
                        console.log(err);
                    }
                }

                data_rows.sort(util.makeComparator(meta => meta.hid, navigator.language, {
                    numeric: true,
                    ignorePunctuation: true,
                    sensitivity: 'base'
                }));
            }

            // Fetch page code (for page panel)
            page_code = await fetchCode(route.page.filename);

            ui.render();
        } catch (err) {
            log.error(err);
        }
    };
    this.go = util.serializeAsync(this.go);

    async function fetchCode(filename) {
        // Anything in the editor?
        {
            let buffer = editor_buffers.get(filename);
            if (buffer != null && !buffer.reload)
                return buffer.session.doc.getValue();
        }

        // Try the cache
        {
            let code = code_cache.get(filename);
            if (code != null)
                return code;
        }

        // Try locally saved files
        if (goupile.isAuthorized()) {
            let key = `${profile.username}:${filename}`;
            let file = await db.load('fs_files', key);

            if (file != null) {
                let code = await file.blob.text();
                code_cache.set(filename, code);
                return code;
            }
        }

        // The server is our last hope
        {
            let response = await net.fetch(`${ENV.base_url}files/${filename}`);

            if (response.ok) {
                let code = await response.text();
                code_cache.set(filename, code);
                return code;
            } else {
                return '';
            }
        }
    }

    async function backupClientData() {
        let progress = log.progress('Archivage sécurisé des données');

        try {
            let range = IDBKeyRange.bound(profile.username + ':', profile.username + '`', false, true);
            let objects = await db.loadAll('rec_records', range);

            let records = [];
            for (let enc of objects) {
                try {
                    let record = await goupile.decryptWithPassport(enc);
                    records.push(record);
                } catch (err) {
                    console.log(err);
                }
            }

            let enc = await goupile.encryptBackup(records);
            let response = await fetch(`${ENV.base_url}api/files/backup`, {
                method: 'POST',
                body: JSON.stringify(enc)
            });

            if (response.ok) {
                console.log('Archive sécurisée envoyée');
                progress.close();
            } else if (response.status === 409) {
                console.log('Archivage ignoré (archive récente)');
                progress.close();
            } else {
                let err = (await response.text()).trim();
                throw new Error(err);
            }
        } catch (err) {
            progress.close();
            throw err;
        }
    }
};
