// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

let form_exec = new function() {
    let self = this;

    let route_page;
    let context_records = new BTree;
    let page_states = {};

    let show_complete = true;

    let multi_mode = false;
    let multi_columns = new Set;

    this.route = async function(page, url) {
        let what = url.pathname.substr(page.url.length) || null;

        if (what && !what.match(/^(new|multi|[A-Z0-9]{26}(@[0-9]+)?)$/))
            throw new Error(`Adresse incorrecte '${url.pathname}'`);
        route_page = page;

        // Clear inappropriate records (wrong form)
        if (context_records.size) {
            let record0 = context_records.first();

            if (record0.table !== route_page.form.key)
                context_records.clear();
        }

        // Sync context records
        if (what === 'multi') {
            multi_mode = true;
        } else if (what === 'new') {
            let record = context_records.first();

            if (!record || record.mtime != null)
                record = vrec.create(route_page.form.key);

            context_records.clear();
            context_records.set(record.id, record);

            multi_mode = false;
        } else if (what == null) {
            let record = vrec.create(route_page.form.key);

            context_records.clear();
            context_records.set(record.id, record);

            multi_mode = false;
        } else {
            context_records.clear();

            let [id, version] = what.split('@');
            if (version != null)
                version = parseInt(version, 10);

            let record = context_records.get(id);
            if (record) {
                if (record.table !== route_page.form.key) {
                    record = null;
                } else if (version != null) {
                    if (version !== record.version)
                        record = null;
                } else {
                    if (record.version !== record.versions.length - 1)
                        record = null;
                }
            }
            if (!record) {
                record = await vrec.load(route_page.form.key, id, version);

                if (record) {
                    if (version != null && record.version !== version)
                        log.error(`La fiche version @${version} n'existe pas\n`+
                                  `Version chargée : @${record.version}`);
                } else {
                    log.error(`La fiche ${id} n'existe pas`);
                    record = vrec.create(route_page.form.key);
                }

                delete page_states[id];
            }

            context_records.set(record.id, record);

            multi_mode = false;
        }

        // Clean up unused page states
        let new_states = {};
        for (let id of context_records.keys())
            new_states[id] = page_states[id];
        page_states = new_states;
    };

    this.runPage = function(code, panel_el) {
        let func = Function('util', 'shared', 'go', 'form', 'page',
                            'values', 'variables', 'route', 'scratch', code);

        if (multi_mode) {
            if (context_records.size && multi_columns.size) {
                render(html`
                    <div class="fm_page">${util.map(context_records.values(), record => {
                        // Each entry needs to update itself without doing a full render
                        let el = document.createElement('div');
                        el.className = 'fm_entry';

                        runPageMulti(func, record, multi_columns, el);

                        return el;
                    })}</div>
                `, panel_el);
            } else if (!context_records.size) {
                render(html`<div class="fm_page">Aucun enregistrement sélectionné</div>`, panel_el);
            } else {
                render(html`<div class="fm_page">Aucune colonne sélectionnée</div>`, panel_el);
            }
        } else {
            if (!context_records.size) {
                let record = vrec.create(route_page.form.key);
                context_records.set(record.id, record);
            }

            let record = context_records.first();
            runPage(func, record, panel_el);
        }
    };

    function runPageMulti(func, record, columns, el) {
        let state = page_states[record.id];
        if (!state) {
            state = new PageState;
            page_states[record.id] = state;
        }

        let page = new Page(route_page.key);
        let builder = new PageBuilder(state, page);

        builder.decodeKey = decodeKey;
        builder.setValue = (key, value) => setValue(record, key, value);
        builder.getValue = (key, default_value) => getValue(record, key, default_value);
        builder.submitHandler = async () => {
            await saveRecord(record, page);
            state.changed = false;

            await goupile.go();
        };
        builder.changeHandler = () => runPageMulti(...arguments);

        // Build it!
        builder.pushOptions({compact: true});
        func(util, app.shared, nav.go, builder, builder,
             state.values, page.variables, {}, state.scratch);

        render(html`
            <button type="button" class="af_button" style="float: right;"
                    ?disabled=${builder.hasErrors() || !state.changed}
                    @click=${builder.submit}>Enregistrer</button>

            ${page.widgets.map(intf => {
                let visible = intf.key && columns.has(intf.key.toString());
                return visible ? intf.render() : '';
            })}
        `, el);

        window.history.replaceState(null, null, makeCurrentURL());
    }

    function runPage(func, record, el) {
        let state = page_states[record.id];
        if (!state) {
            state = new PageState;
            page_states[record.id] = state;
        }

        let page = new Page(route_page.key);
        let readonly = (record.mtime != null && record.version !== record.versions.length - 1);
        let builder = new PageBuilder(state, page, readonly);

        builder.decodeKey = decodeKey;
        builder.setValue = (key, value) => setValue(record, key, value);
        builder.getValue = (key, default_value) => getValue(record, key, default_value);
        builder.submitHandler = async () => {
            await saveRecord(record, page);
            state.changed = false;

            await goupile.go();
        };
        builder.changeHandler = () => runPage(...arguments);

        // Build it!
        func(util, app.shared, nav.go, builder, builder,
             state.values, page.variables, nav.route, state.scratch);
        builder.errorList();

        let show_actions = route_page.options.actions && page.variables.length;
        let enable_save = !builder.hasErrors() && state.changed;
        let enable_validate = !builder.hasErrors() && !state.changed &&
                              record.complete[page.key] === false;

        render(html`
            <div class="fm_form">
                ${show_actions ? html`
                    <div class="fm_id">
                        ${record.mtime == null ? html`Nouvel enregistrement` : ''}
                        ${record.mtime != null && record.sequence == null ? html`Enregistrement local` : ''}
                        ${record.mtime != null && record.sequence != null ? html`Enregistrement n°${record.sequence}` : ''}

                        ${record.mtime != null ? html`(<a @click=${e => showTrailDialog(e, record)}>trail</a>)` : ''}
                    </div>
                ` : ''}

                <div class="fm_path">${route_page.form.pages.map(page2 => {
                    let complete = record.complete[page2.key];

                    let cls = '';
                    if (page2.key === page.key)
                        cls += ' active';
                    if (complete == null) {
                        // Leave as is
                    } else if (complete) {
                        cls += ' complete';
                    } else {
                        cls += ' partial';
                    }

                    return html`<a class=${cls} href=${makeURL(route_page.form.key, page2.key, record)}>${page2.label}</a>`;
                })}</div>

                <div class="fm_page">${page.render()}</div>

                ${show_actions ? html`
                    <div class="af_actions">
                        <button type="button" class="af_button" ?disabled=${!enable_save}
                                @click=${builder.submit}>Enregistrer</button>
                        ${route_page.options.validate ?
                            html`<button type="button" class="af_button" ?disabled=${!enable_validate}
                                         @click=${e => showValidateDialog(e, builder.submit)}>Valider</button>`: ''}
                        <hr/>
                        <button type="button" class="af_button" ?disabled=${!state.changed && record.mtime == null}
                                @click=${e => handleNewClick(e, state.changed)}>Fermer</button>
                    </div>
                `: ''}
            </div>
        `, el);

        window.history.replaceState(null, null, makeCurrentURL());
    }

    function showTrailDialog(e, record) {
        goupile.popup(e, null, (page, close) => {
            page.output(html`
                <table class="tr_table">
                    <thead>
                        <tr>
                            <th></th>
                            <th>Modification</th>
                            <th>Utilisateur</th>
                        </tr>
                    </thead>
                    <tbody>
                        ${util.mapRange(0, record.versions.length, idx => {
                            let version = record.versions[record.versions.length - idx - 1];
                            let url = makeURL(route_page.form.key, route_page.key, record, version.version);

                            return html`
                                <tr>
                                    <td><a href=${url}>🔍\uFE0E</a></td>
                                    <td>${version.mtime.toLocaleString()}</td>
                                    <td>${version.username || '(local)'}</td>
                                </tr>
                            `;
                        })}
                    </tbody>
                </table>
            `);
        });
    }

    function decodeKey(key) {
        if (!key)
            throw new Error('Empty keys are not allowed');
        if (!key.match(/^[a-zA-Z_][a-zA-Z0-9_]*$/))
            throw new Error('Allowed key characters: a-z, _ and 0-9 (not as first character)');

        key = {
            variable: key,
            toString: () => key.variable
        };

        return key;
    };

    function setValue(record, key, value) {
        record.values[key] = value;
    }

    function getValue(record, key, default_value) {
        if (!record.values.hasOwnProperty(key)) {
            record.values[key] = default_value;
            return default_value;
        }

        return record.values[key];
    }

    async function saveRecord(record, page) {
        let entry = new log.Entry();

        entry.progress('Enregistrement en cours');
        try {
            let record2 = await vrec.save(record, page.key, page.variables);
            entry.success('Données enregistrées');

            if (context_records.has(record2.id))
                context_records.set(record2.id, record2);
        } catch (err) {
            entry.error(`Échec de l\'enregistrement : ${err.message}`);
        }
    }

    function handleNewClick(e, confirm) {
        if (confirm) {
            goupile.popup(e, 'Fermer l\'enregistrement', (page, close) => {
                page.output('Cette action entraînera la perte des modifications en cours, êtes-vous sûr(e) ?');

                page.submitHandler = () => {
                    close();
                    goupile.go(makeURL(route_page.form.key, route_page.key, null));
                };
            })
        } else {
            goupile.go(makeURL(route_page.form.key, route_page.key, null));
        }
    }

    function showValidateDialog(e, submit_func) {
        goupile.popup(e, 'Valider', (page, close) => {
            page.output('Confirmez-vous la validation de cette page ?');

            page.submitHandler = () => {
                close();
                submit_func(true);
            };
        });
    }

    this.runStatus = async function() {
        let records = await vrec.loadAll(route_page.form.key);
        renderStatus(records);
    };

    function renderStatus(records) {
        let pages = route_page.form.pages;

        let complete_set = new Set;
        for (let record of records) {
            // We can't compute this at save time because the set of pages may change anytime
            if (pages.every(page => record.complete[page.key]))
                complete_set.add(record.id);
        }

        render(html`
            <div class="gp_toolbar">
                <p>&nbsp;&nbsp;${records.length} ${records.length > 1 ? 'enregistrements' : 'enregistrement'}
                   (${complete_set.size} ${complete_set.size > 1 ? 'complets' : 'complet'})</p>
                <div style="flex: 1;"></div>
                <div class="gp_dropdown right">
                    <button type="button">Options</button>
                    <div>
                        <button type="button" class=${!show_complete ? 'active' : ''}
                                @click=${toggleShowComplete}>Cacher complets</button>
                    </div>
                </div>
            </div>

            <table class="st_table">
                <colgroup>
                    <col style="width: 3em;"/>
                    <col style="width: 60px;"/>
                    ${pages.map(col => html`<col/>`)}
                </colgroup>

                <thead>
                    <tr>
                        <th class="actions"></th>
                        <th class="id">ID</th>
                        ${pages.map(page => html`<th>${page.label}</th>`)}
                    </tr>
                </thead>

                <tbody>
                    ${!records.length ?
                        html`<tr><td style="text-align: left;"
                                     colspan=${2 + Math.max(1, pages.length)}>Aucune donnée à afficher</td></tr>` : ''}
                    ${records.map(record => {
                        if (show_complete || !complete_set.has(record.id)) {
                            return html`
                                <tr class=${context_records.has(record.id) ? 'selected' : ''}>
                                    <th>
                                        <a @click=${e => handleEditClick(record)}>🔍\uFE0E</a>
                                        <a @click=${e => showDeleteDialog(e, record)}>✕</a>
                                    </th>
                                    <td class="id">${record.sequence || 'local'}</td>

                                    ${pages.map(page => {
                                        let complete = record.complete[page.key];

                                        if (complete == null) {
                                            return html`<td class="none"><a href=${makeURL(route_page.form.key, page.key, record)}>Non rempli</a></td>`;
                                        } else if (complete) {
                                            return html`<td class="complete"><a href=${makeURL(route_page.form.key, page.key, record)}>Validé</a></td>`;
                                        } else {
                                            return html`<td class="partial"><a href=${makeURL(route_page.form.key, page.key, record)}>Enregistré</a></td>`;
                                        }
                                    })}
                                </tr>
                            `;
                        } else {
                            return '';
                        }
                    })}
                </tbody>
            </table>
        `, document.querySelector('#dev_status'));
    }

    function toggleShowComplete() {
        show_complete = !show_complete;
        goupile.go();
    }

    this.runData = async function() {
        let records = await vrec.loadAll(route_page.form.key);
        let variables = await vrec.listVariables(route_page.form.key);
        let columns = orderColumns(route_page.form.pages, variables);

        renderRecords(records, columns);
    };

    function renderRecords(records, columns) {
        let empty_msg;
        if (!records.length) {
            empty_msg = 'Aucune donnée à afficher';
        } else if (!columns.length) {
            empty_msg = 'Impossible d\'afficher les données (colonnes inconnues)';
            records = [];
        }

        let count1 = 0;
        let count0 = 0;
        if (multi_mode) {
            for (let record of records) {
                if (context_records.has(record.id)) {
                    count1++;
                } else {
                    count0++;
                }
            }
        }

        render(html`
            <div class="gp_toolbar">
                <p>&nbsp;&nbsp;${records.length} ${records.length > 1 ? 'enregistrements' : 'enregistrement'}</p>
                <div style="flex: 1;"></div>
                <div class="gp_dropdown right">
                    <button type="button">Export</button>
                    <div>
                        <button type="button" @click=${e => exportSheets(route_page.form, 'xlsx')}>Excel</button>
                        <button type="button" @click=${e => exportSheets(route_page.form, 'csv')}>CSV</button>
                    </div>
                </div>
                <div class="gp_dropdown right">
                    <button type="button">Options</button>
                    <div>
                        <button type="button" class=${multi_mode ? 'active' : ''}
                                @click=${e => toggleSelectionMode()}>Sélection multiple</button>
                    </div>
                </div>
            </div>

            <table class="rec_table" style=${`min-width: ${30 + 60 * columns.length}px`}>
                <colgroup>
                    <col style="width: 3em;"/>
                    <col style="width: 60px;"/>
                    ${!columns.length ? html`<col/>` : ''}
                    ${columns.map(col => html`<col/>`)}
                </colgroup>

                <thead>
                    ${columns.length ? html`
                        <tr>
                            <th colspan="2"></th>
                            ${util.mapRLE(columns, col => col.page, (page, offset, len) =>
                                html`<th class="rec_page" colspan=${len}>${page}</th>`)}
                        </tr>
                    ` : ''}
                    <tr>
                        <th class="actions">
                            ${multi_mode ?
                                html`<input type="checkbox" .checked=${count1 && !count0}
                                            .indeterminate=${count1 && count0}
                                            @change=${e => toggleAllRecords(records, e.target.checked)} />` : ''}
                        </th>
                        <th class="id">ID</th>

                        ${!columns.length ? html`<th>&nbsp;</th>` : ''}
                        ${!multi_mode ? columns.map(col => html`<th title=${col.key}>${col.key}</th>`) : ''}
                        ${multi_mode ? columns.map(col =>
                            html`<th title=${col.key}><input type="checkbox" .checked=${multi_columns.has(col.key)}
                                                             @change=${e => toggleColumn(col.key)} />${col.key}</th>`) : ''}
                    </tr>
                </thead>

                <tbody>
                    ${empty_msg ?
                        html`<tr><td colspan=${2 + Math.max(1, columns.length)}>${empty_msg}</td></tr>` : ''}
                    ${records.map(record => html`
                        <tr class=${context_records.has(record.id) ? 'selected' : ''}>
                            ${!multi_mode ? html`<th><a @click=${e => handleEditClick(record)}>🔍\uFE0E</a>
                                                      <a @click=${e => showDeleteDialog(e, record)}>✕</a></th>` : ''}
                            ${multi_mode ? html`<th><input type="checkbox" .checked=${context_records.has(record.id)}
                                                            @click=${e => handleEditClick(record)} /></th>` : ''}
                            <td class="id">${record.sequence || 'local'}</td>

                            ${columns.map(col => {
                                let value = record.values[col.key];

                                if (value == null) {
                                    if (record.values.hasOwnProperty(col.key)) {
                                        return html`<td class="missing" title="Donnée manquante">MD</td>`;
                                    } else {
                                        return html`<td class="missing" title="Non applicable">NA</td>`;
                                    }
                                } else if (Array.isArray(value)) {
                                    let text = value.join('|');
                                    return html`<td title=${text}>${text}</td>`;
                                } else if (typeof value === 'number') {
                                    return html`<td class="number" title=${value}>${value}</td>`;
                                } else {
                                    return html`<td title=${value}>${value}</td>`;
                                }
                            })}
                        </tr>
                    `)}
                </tbody>
            </table>
        `, document.querySelector('#dev_data'));
    }

    function toggleSelectionMode() {
        multi_mode = !multi_mode;

        let record0 = context_records.size ? context_records.values().next().value : null;

        if (multi_mode) {
            multi_columns.clear();
            if (record0 && record0.mtime == null)
                context_records.clear();
        } else if (record0) {
            context_records.clear();
            context_records.set(record0.id, record0);
        }

        goupile.go();
    }

    function toggleAllRecords(records, enable) {
        context_records.clear();

        if (enable) {
            for (let record of records)
                context_records.set(record.id, record);
        }

        goupile.go();
    }

    function toggleColumn(key) {
        if (multi_columns.has(key)) {
            multi_columns.delete(key);
        } else {
            multi_columns.add(key);
        }

        goupile.go();
    }

    async function exportSheets(form, format = 'xlsx') {
        if (typeof XSLX === 'undefined')
            await net.loadScript(`${env.base_url}static/xlsx.core.min.js`);

        let records = await vrec.loadAll(form.key);
        let variables = await vrec.listVariables(form.key);
        let columns = orderColumns(form.pages, variables);

        if (!columns.length) {
            log.error('Impossible d\'exporter pour le moment (colonnes inconnues)');
            return;
        }

        // Worksheet
        let ws = XLSX.utils.aoa_to_sheet([columns.map(col => col.key)]);
        for (let record of records) {
            let values = columns.map(col => record.values[col.key]);
            XLSX.utils.sheet_add_aoa(ws, [values], {origin: -1});
        }

        // Workbook
        let wb = XLSX.utils.book_new();
        let ws_name = `${env.app_key}_${dates.today()}`;
        XLSX.utils.book_append_sheet(wb, ws, ws_name);

        let filename = `export_${ws_name}.${format}`;
        switch (format) {
            case 'xlsx': { XLSX.writeFile(wb, filename); } break;
            case 'csv': { XLSX.writeFile(wb, filename, {FS: ';'}); } break;
        }
    }

    function orderColumns(pages, variables) {
        variables = variables.slice();
        variables.sort((variable1, variable2) => util.compareValues(variable1.key, variable2.key));

        let frags_map = {};
        for (let variable of variables) {
            let frag_variables = frags_map[variable.page];
            if (!frag_variables) {
                frag_variables = [];
                frags_map[variable.page] = frag_variables;
            }

            frag_variables.push(variable);
        }

        let columns = [];
        for (let page of pages) {
            let frag_variables = frags_map[page.key] || [];
            delete frags_map[page.key];

            let variables_map = util.mapArray(frag_variables, variable => variable.key);

            let first_set = new Set;
            let sets_map = {};
            for (let variable of frag_variables) {
                if (variable.before == null) {
                    first_set.add(variable.key);
                } else {
                    let set_ptr = sets_map[variable.before];
                    if (!set_ptr) {
                        set_ptr = new Set;
                        sets_map[variable.before] = set_ptr;
                    }

                    set_ptr.add(variable.key);
                }
            }

            let next_sets = [first_set];
            let next_set_idx = 0;
            while (next_set_idx < next_sets.length) {
                let set_ptr = next_sets[next_set_idx++];
                let set_start_idx = columns.length;

                while (set_ptr.size) {
                    let frag_start_idx = columns.length;

                    for (let key of set_ptr) {
                        let variable = variables_map[key];

                        if (!set_ptr.has(variable.after)) {
                            let col = {
                                page: page.label,
                                key: key,
                                type: variable.type
                            };
                            columns.push(col);
                        }
                    }

                    reverseLastColumns(columns, frag_start_idx);

                    // Avoid infinite loop that may happen in rare cases
                    if (columns.length === frag_start_idx) {
                        let use_key = set_ptr.values().next().value;

                        let col = {
                            page: page.label,
                            key: use_key,
                            type: variables_map[use_key].type
                        };
                        columns.push(col);
                    }

                    for (let i = frag_start_idx; i < columns.length; i++) {
                        let key = columns[i].key;

                        let next_set = sets_map[key];
                        if (next_set) {
                            next_sets.push(next_set);
                            delete sets_map[key];
                        }

                        delete variables_map[key];
                        set_ptr.delete(key);
                    }
                }

                reverseLastColumns(columns, set_start_idx);
            }

            // Remaining page variables
            for (let key in variables_map) {
                let col = {
                    page: page.label,
                    key: key,
                    type: variables_map[key].type
                }
                columns.push(col);
            }
        }

        return columns;
    }

    function reverseLastColumns(columns, start_idx) {
        for (let i = 0; i < (columns.length - start_idx) / 2; i++) {
            let tmp = columns[start_idx + i];
            columns[start_idx + i] = columns[columns.length - i - 1];
            columns[columns.length - i - 1] = tmp;
        }
    }

    function handleEditClick(record) {
        let enable_overview = false;

        if (!context_records.has(record.id)) {
            if (!multi_mode)
                context_records.clear();
            context_records.set(record.id, record);

            enable_overview = !multi_mode;
        } else {
            context_records.delete(record.id);

            if (!multi_mode && !context_records.size) {
                let record = vrec.create(route_page.form.key);
                context_records.set(record.id, record);
            }
        }

        if (enable_overview) {
            goupile.toggleOverview(true);
        } else {
            goupile.go();
        }
    }

    function showDeleteDialog(e, record) {
        goupile.popup(e, 'Supprimer', (page, close) => {
            page.output('Voulez-vous vraiment supprimer cet enregistrement ?');

            page.submitHandler = async () => {
                close();

                await vrec.delete(record.table, record.id);
                context_records.delete(record.id, record);

                goupile.go();
            };
        });
    }

    function makeCurrentURL() {
        let url = `${env.base_url}app/${route_page.form.key}/${route_page.key}/`;

        if (multi_mode) {
            url += 'multi';
        } else if (context_records.size) {
            let record = context_records.first();

            if (record.mtime != null) {
                url += record.id;
                if (record.version !== record.versions.length - 1)
                    url += `@${record.version}`;
            } else {
                let state = page_states[record.id];
                if (state && state.changed)
                    url += 'new';
            }
        }

        return util.pasteURL(url, nav.route);
    }

    function makeURL(form_key, page_key, record = null, version = undefined) {
        let url = `${env.base_url}app/${form_key}/${page_key}/`;

        if (record) {
            if (record.mtime != null) {
                url += record.id;

                if (version != null) {
                    url += `@${version}`;
                } else if (record.version !== record.versions.length - 1) {
                    url += `@${record.version}`;
                }
            } else {
                let state = page_states[record.id];
                if (state && state.changed)
                    url += 'new';
            }
        }

        return util.pasteURL(url, nav.route);
    }
};
