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

import { render, html } from '../../../node_modules/lit/html.js';
import { unsafeHTML } from '../../../node_modules/lit/directives/unsafe-html.js';
import { Util, Log, Net } from '../../../../web/libjs/common.js';
import { start, zoom, makeField, makeEdit, updateEntry, deleteEntry, renderMarkdown, isConnected } from '../map.js';

function ProvenanceProvider() {
    let entries;

    this.loadMap = async function() {
        let data = await Net.get('api/entries');

        entries = [
            ...data.demheter.rows.map(row => ({ type: 'demheter', ...row }))
        ];

        console.log(entries);
    };

    this.renderFilters = function() {
        return html`
            <div id="filters">
                <div id="count"></div>

                <div class="group">
                    <label>
                        <input type="checkbox" data-filter="this.type == 'demheter'"
                               checked/> DEMHETER
                        <img src="static/icons/demheter.png" width="24" height="24" alt="" />
                    </label>
                </div>
            </div>
        `;
    };

    this.fillMap = function(filters) {
        let markers = [];
        let total = 0;

        for (let entry of entries) {
            if (entry.address.latitude != null) {
                total++;

                if (filters.every(filtre => filtre(entry))) {
                    let marker = {
                        latitude: entry.address.latitude,
                        longitude: entry.address.longitude,
                        cluster: '#24579d',
                        tooltip: entry.name,
                        priority: 1,
                        circle: '#24579d',
                        size: 32
                    };

                    marker.entry = entry;

                    markers.push(marker);
                }
            }
        }

        if (markers.length < total) {
            let text = `${markers.length} / ${total}`;
            render(text, document.querySelector('#count'));
        } else if (markers.length) {
            let text = `${markers.length} entrée${markers.length > 1 ? 's' : ''}`;
            render(text, document.querySelector('#count'));
        } else {
            let text = 'Aucun résultat';
            render(text, document.querySelector('#count'));
        }

        return markers;
    };

    this.styleCluster = function(element) {
        element.priority = element.markers.length;
    };

    function field(entry, key, view = null) {
        if (key == 'address' && entry[key] != null) {
            return makeField(entry, key, 'address');
        } else {
            return makeField(entry, key, fields[key], view);
        }
    }
}

export async function main() {
    let provider = new ProvenanceProvider;

    let latitude = 50.44;
    let longitude = 2.5;
    let zoom = (window.innerWidth <= 580) ? 7 : 8;

    await start(provider, {
        latitude: latitude,
        longitude: longitude,
        zoom: zoom
    });
}

export { zoom }