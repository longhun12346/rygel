// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

function RecordManager(db) {
    let self = this;

    function makeTableKey(table, id) {
        return `${table}_${id}`;
    }

    this.create = function(table) {
        let id = util.makeULID();
        let tkey = makeTableKey(table, id);

        let record = {
            tkey: tkey,

            id: id,
            table: table,
            values: {}
        };

        return record;
    };

    this.save = async function(record, variables) {
        variables = variables.map((variable, idx) => {
            let ret = {
                tkey: makeTableKey(record.table, variable.key),

                key: variable.key,
                table: record.table,
                type: variable.type,
                before: variables[idx - 1] ? variables[idx - 1].key : null,
                after: variables[idx + 1] ? variables[idx + 1].key : null
            };

            return ret;
        });

        return await db.transaction(db => {
            db.save('records', record);
            db.saveAll('variables', variables);
        });
    };

    this.delete = async function(table, id) {
        let tkey = makeTableKey(table, id);
        return await db.delete('records', tkey);
    };

    this.reset = async function() {
        await db.clear('records');
    };

    this.load = async function(table, id) {
        let tkey = makeTableKey(table, id);
        return await db.load('records', tkey);
    };

    this.loadAll = async function(table) {
        // Works for ASCII names, which we enforce
        let start_key = table + '_';
        let end_key = table + '`';

        let [records, variables] = await Promise.all([
            db.loadAll('records', start_key, end_key),
            db.loadAll('variables', start_key, end_key)
        ]);

        return [records, variables];
    };

    return this;
}
