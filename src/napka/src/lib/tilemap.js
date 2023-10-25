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

import { Util, Net, LruMap } from '../../../web/libjs/common.js';

function SpatialMap() {
    let self = this;

    let factor = 8;
    let map = new Map;

    Object.defineProperties(this, {
        clusters: { get: getClusters, enumerable: true }
    })

    this.add = function(x, y, element) {
        let xp = Math.floor(x / factor);
        let yp = Math.floor(y / factor);
        let sizep = Math.ceil(element.size / factor / 3);

        let item = {
            x: x,
            y: y,
            xp: xp,
            yp: yp,
            element: element
        };

        let matched = false;

        if (element.cluster != null) {
            outer: for (let i = -sizep; i <= sizep; i++) {
                for (let j = -sizep; j <= sizep; j++) {
                    let maybe = element.cluster + ':' + (xp + i) + ':' + (yp + j);
                    let match = map.get(maybe);

                    if (match == null)
                        continue;

                    match.items.push(item);
                    matched = true;

                    map.delete(key(match));

                    match.xp = match.items.reduce((acc, item) => acc + item.xp, 0) / match.items.length;
                    match.yp = match.items.reduce((acc, item) => acc + item.yp, 0) / match.items.length;

                    map.set(key(match), match);

                    break outer;
                }
            }
        }

        if (!matched) {
            let match = {
                cluster: element.cluster,
                xp: xp,
                yp: yp,
                items: [item]
            };

            map.set(key(match), match);
        }
    };

    function key(match) {
        let key = match.cluster + ':' + match.xp + ':' + match.yp;
        return key;
    }

    function getClusters() {
        let clusters = Array.from(map.values()).map(match => {
            let pos = {
                x: match.items.reduce((acc, item) => acc + item.x, 0) / match.items.length,
                y: match.items.reduce((acc, item) => acc + item.y, 0) / match.items.length
            };

            let radius = Math.max(...match.items.map(item => {
                let dist = distance(pos, { x: item.x, y: item.y });
                return dist + item.element.size;
            }));

            let cluster = {
                x: pos.x,
                y: pos.y,
                size: radius,
                color: match.cluster,
                items: match.items.map(item => item.element)
            };

            return cluster;
        });

        return clusters;
    }
}

function TileMap(runner) {
    let self = this;

    // Shortcuts
    let canvas = runner.canvas;
    let ctx = canvas.getContext('2d');
    let mouse_state = runner.mouseState;
    let pressed_keys = runner.pressedKeys;

    let tiles = null;

    let marker_groups = {};

    let handle_click = (markers) => {};

    const DEFAULT_ZOOM = 7;
    const MAX_FETCHERS = 8;

    let state = null;

    let last_wheel_time = 0;
    let zoom_animation = null;

    let render_elements = [];

    let missing_assets = 0;
    let active_fetchers = 0;
    let fetch_queue = [];
    let fetch_handles = new Map;

    let known_tiles = new LruMap(256);
    let marker_textures = new LruMap(32);

    Object.defineProperties(this, {
        width: { get: () => canvas.width, enumerable: true },
        height: { get: () => canvas.height, enumerable: true },

        coordinates: { get: () => {
            let center = { x: canvas.width / 2, y: canvas.height / 2 };
            return self.screenToCoord(center);
        }, enumerable: true },

        zoomLevel: { get: () => state.zoom, enumerable: true },

        onClick: { get: () => handle_click, set: func => { handle_click = func; }, enumerable: true }
    });

    this.init = async function(config) {
        tiles = Object.assign({
            min_zoom: 1
        }, config);

        ctx.imageSmoothingEnabled = true;
        ctx.imageSmoothingQuality = 'high';

        state = {
            zoom: DEFAULT_ZOOM,
            pos: latLongToXY(48.866667, 2.333333, DEFAULT_ZOOM),
            grab: null
        };

        known_tiles.clear();
        marker_textures.clear();
    };

    this.move = function(lat, lng, zoom = null) {
        if (zoom != null)
            state.zoom = zoom;
        state.pos = latLongToXY(lat, lng, state.zoom);

        zoom_animation = null;

        runner.busy();
    };

    this.setMarkers = function(key, markers) {
        if (!Array.isArray(markers))
            throw new Error('Not an array of markers');

        marker_groups[key] = markers;
    };

    this.clearMarkers = function(key) {
        delete marker_groups[key];
    };

    this.update = function() {
        if (ctx == null)
            return;

        // Animate zoom
        if (zoom_animation != null) {
            let t = easeInOutSine((runner.updateCounter - zoom_animation.start) / 60);

            if (t < 1) {
                zoom_animation.value = zoom_animation.from + t * (zoom_animation.to - zoom_animation.from);
                runner.busy();
            } else {
                zoom_animation = null;
            }
        }

        // Create render elements (markers and clusters)
        {
            let viewport = getViewport();

            let groups = Object.values(marker_groups);
            let grid = new SpatialMap;

            for (let markers of groups) {
                for (let marker of markers) {
                    let pos = latLongToXY(marker.latitude, marker.longitude, state.zoom);
                    grid.add(pos.x, pos.y, marker);
                }
            }

            let markers = [];
            let clusters = [];

            for (let cluster of grid.clusters) {
                if (cluster.items.length == 1) {
                    let element = {
                        type: 'marker',

                        x: cluster.x - viewport.x1,
                        y: cluster.y - viewport.y1,
                        size: cluster.size,
                        clickable: cluster.items[0].clickable,

                        markers: cluster.items
                    };

                    markers.push(element);
                } else {
                    let element = {
                        type: 'cluster',

                        x: cluster.x - viewport.x1,
                        y: cluster.y - viewport.y1,
                        size: cluster.size,
                        clickable: true,

                        markers: cluster.items,
                        color: cluster.color
                    };

                    clusters.push(element);
                }
            }

            // Show explicit markers above all else
            render_elements = [...clusters, ...markers];
        }

        // Detect user targets
        let targets = [];
        for (let i = render_elements.length - 1; i >= 0; i--) {
            let element = render_elements[i];

            if (element.clickable &&
                    distance(element, mouse_state) < adaptMarkerSize(element.size, state.zoom) / 2) {
                targets = element.markers;
                break;
            }
        }

        // Handle actions
        if (mouse_state.left >= 1 && (!targets.length || state.grab != null)) {
            if (state.grab != null) {
                state.pos.x += state.grab.x - mouse_state.x;
                state.pos.y += state.grab.y - mouse_state.y;
            }

            state.grab = {
                x: mouse_state.x,
                y: mouse_state.y
            };
        } else if (!mouse_state.left) {
            state.grab = null;
        }
        if (mouse_state.left == -1 && targets.length && state.grab == null)
            handle_click(targets);

        // Adjust cursor style
        if (state.grab != null) {
            runner.cursor = 'grabbing';
        } else if (targets.length) {
            runner.cursor = 'pointer';
        } else {
            runner.cursor = 'grab';
        }

        // Handle zooming
        if (mouse_state.wheel) {
            let now = performance.now();

            if (now - last_wheel_time >= 200) {
                if (mouse_state.wheel < 0) {
                    self.zoom(1, mouse_state);
                } else if (mouse_state.wheel > 0) {
                    self.zoom(-1, mouse_state);
                }

                last_wheel_time = now;
            }
        }

        // Make sure we stay in the box
        {
            let size = mapSize(state.zoom);

            if (size >= canvas.width)
                state.pos.x = Util.clamp(state.pos.x, canvas.width / 2, size - canvas.width / 2);
            if (size >= canvas.height)
                state.pos.y = Util.clamp(state.pos.y, canvas.height / 2, size - canvas.height / 2);
        }

        // Fix rounding issues
        state.pos.x = Math.floor(state.pos.x);
        state.pos.y = Math.floor(state.pos.y);
    };

    function easeInOutSine(t) {
        return -(Math.cos(Math.PI * t) - 1) / 2;
    }

    this.zoom = function(delta, at = null) {
        if (state.zoom + delta < tiles.min_zoom || state.zoom + delta > tiles.max_zoom)
            return;
        if (at == null)
            at = { x: canvas.width / 2, y: canvas.height / 2 };

        if (delta > 0) {
            for (let i = 0; i < delta; i++) {
                state.pos.x = (state.pos.x * 2) + (at.x - canvas.width / 2);
                state.pos.y = (state.pos.y * 2) + (at.y - canvas.height / 2);
            }
        } else {
            for (let i = 0; i < -delta; i++) {
                state.pos.x = (state.pos.x * 0.5) - 0.5 * (at.x - canvas.width / 2);
                state.pos.y = (state.pos.y * 0.5) - 0.5 * (at.y - canvas.height / 2);
            }
        }

        if (zoom_animation == null) {
            zoom_animation = {
                start: null,
                from: state.zoom,
                to: null,
                value: state.zoom,
                at: null
            };
        } else {
            zoom_animation.from = zoom_animation.value;
        }
        zoom_animation.start = runner.updateCounter;
        zoom_animation.to = state.zoom + delta;
        zoom_animation.at = { x: at.x, y: at.y };

        state.zoom += delta;

        stopFetchers();
    };

    function getViewport() {
        let viewport = {
            x1: Math.floor(state.pos.x - canvas.width / 2),
            y1: Math.floor(state.pos.y - canvas.height / 2),
            x2: Math.ceil(state.pos.x + canvas.width / 2),
            y2: Math.floor(state.pos.y + canvas.height / 2)
        };

        return viewport;
    }

    this.draw = function() {
        if (ctx == null)
            return;

        let viewport = getViewport();

        fetch_queue.length = 0;
        missing_assets = 0;

        let adjust = { x: 0, y: 0 };
        let anim_zoom = state.zoom;
        let anim_scale = 1;

        if (zoom_animation != null) {
            anim_zoom = zoom_animation.value;

            let delta = Math.pow(2, state.zoom - anim_zoom) - 1;

            adjust.x = delta * (zoom_animation.at.x - canvas.width / 2);
            adjust.y = delta * (zoom_animation.at.y - canvas.height / 2);

            anim_scale = Math.pow(2, anim_zoom - state.zoom);
        }

        // Draw tiles
        {
            ctx.save();

            ctx.translate(Math.floor(canvas.width / 2), Math.floor(canvas.height / 2));
            ctx.scale(anim_scale, anim_scale);
            ctx.translate(-state.pos.x + adjust.x, -state.pos.y + adjust.y);

            let i1 = Math.floor(viewport.x1 / tiles.tilesize);
            let j1 = Math.floor(viewport.y1 / tiles.tilesize);
            let i2 = Math.ceil(viewport.x2 / tiles.tilesize);
            let j2 = Math.ceil(viewport.y2 / tiles.tilesize);

            for (let i = i1; i <= i2; i++) {
                for (let j = j1; j <= j2; j++)
                    drawTile(origin, i, j);
            }

            if (zoom_animation != null) {
                for (let j = j1 - 2; j <= j2 + 2; j++) {
                    drawTile(origin, i1 - 2, j, false);
                    drawTile(origin, i1 - 1, j, false);
                    drawTile(origin, i2 + 1, j, false);
                    drawTile(origin, i2 + 2, j, false);
                }
                for (let i = i1; i <= i2; i++) {
                    drawTile(origin, i, j1 - 2, false);
                    drawTile(origin, i, j1 - 1, false);
                    drawTile(origin, i, j2 + 1, false);
                    drawTile(origin, i, j2 + 2, false);
                }
            }

            ctx.restore();
        }

        // Draw markers
        {
            ctx.save();
            ctx.translate(0.5, 0.5);

            let current_filter = null;

            ctx.filter = 'none';

            for (let element of render_elements) {
                if (zoom_animation != null) {
                    let centered = {
                        x: element.x - canvas.width / 2,
                        y: element.y - canvas.height / 2
                    };

                    element.x = Math.round(canvas.width / 2 + anim_scale * (centered.x + adjust.x));
                    element.y = Math.round(canvas.height / 2 + anim_scale * (centered.y + adjust.y));
                }

                if (element.x < -element.size || element.x > canvas.width + element.size)
                    continue;
                if (element.y < -element.size || element.y > canvas.height + element.size)
                    continue;

                switch (element.type) {
                    case 'marker': {
                        let marker = element.markers[0];

                        if (marker.filter != current_filter) {
                            ctx.filter = marker.filter ?? 'none';
                            current_filter = marker.filter;
                        }

                        if (marker.icon != null) {
                            let img = getImage(marker_textures, marker.icon);

                            let width = adaptMarkerSize(marker.size, anim_zoom);
                            let height = adaptMarkerSize(marker.size, anim_zoom);

                            if (img != null)
                                ctx.drawImage(img, element.x - width / 2, element.y - height / 2, width, height);
                        } else if (marker.circle != null) {
                            let radius = adaptMarkerSize(marker.size / 2, state.zoom);

                            ctx.beginPath();
                            ctx.arc(element.x, element.y, radius, 0, 2 * Math.PI, false);

                            ctx.fillStyle = marker.circle;
                            ctx.fill();
                        }
                    } break;

                    case 'cluster': {
                        let radius = adaptMarkerSize(element.size / 2, state.zoom);

                        if (current_filter) {
                            ctx.filter = 'none';
                            current_filter = null;
                        }

                        ctx.beginPath();
                        ctx.arc(element.x, element.y, radius, 0, 2 * Math.PI, false);

                        ctx.fillStyle = element.color;
                        ctx.fill();

                        ctx.font = Math.floor(element.size / 2) + 'px Open Sans';

                        let text = element.markers.length;
                        let width = ctx.measureText(text).width + 8;

                        ctx.fillStyle = 'white';
                        ctx.fillText(text, element.x - width / 2 + 4, element.y + element.size / 6);
                    } break;
                }
            }

            ctx.restore();
        }

        if (missing_assets) {
            let start = Math.min(fetch_queue.length, MAX_FETCHERS - active_fetchers);

            for (let i = 0; i < start; i++)
                startFetcher();
        } else {
            stopFetchers();
        }
    };

    async function startFetcher() {
        active_fetchers++;

        while (fetch_queue.length) {
            let idx = Util.getRandomInt(0, fetch_queue.length);
            let [handle] = fetch_queue.splice(idx, 1);

            fetch_handles.set(handle.url, handle);

            try {
                let img = await fetchImage(handle);

                handle.cache.set(handle.url, img);
                runner.busy();
            } catch (err) {
                if (err != null)
                    console.error(err);
            } finally {
                fetch_handles.delete(handle.url);
            }
        }

        active_fetchers--;
    }

    function adaptMarkerSize(size, zoom) {
        if (zoom >= 7) {
            return size;
        } else {
            return size * ((zoom + 3) / 10);
        }
    }

    function drawTile(origin, i, j, fetch = true) {
        let x = Math.floor(i * tiles.tilesize);
        let y = Math.floor(j * tiles.tilesize);

        // Start with appropriate tile (if any)
        {
            let tile = getTile(state.zoom, i, j, fetch);

            if (tile != null) {
                ctx.drawImage(tile, x, y, tiles.tilesize, tiles.tilesize);
                return;
            }
        }

        // Try zoomed out tiles if we have any
        for (let out = 1; state.zoom - out >= tiles.min_zoom; out++) {
            let factor = Math.pow(2, out);

            let i2 = Math.floor(i / factor);
            let j2 = Math.floor(j / factor);
            let tile = getTile(state.zoom - out, i2, j2, false);

            if (tile != null) {
                ctx.drawImage(tile, tiles.tilesize / factor * (i % factor), tiles.tilesize / factor * (j % factor),
                                    tiles.tilesize / factor, tiles.tilesize / factor,
                                    x, y, tiles.tilesize, tiles.tilesize);
                break;
            }
        }

        // Also put in zoomed in tiles (could be partial)
        for (let out = 1; out < 5; out++) {
            if (state.zoom + out > tiles.max_zoom)
                break;

            let factor = Math.pow(2, out);

            let i2 = Math.floor(i * factor);
            let j2 = Math.floor(j * factor);

            for (let di = 0; di < factor; di++) {
                for (let dj = 0; dj < factor; dj++) {
                    let tile = getTile(state.zoom + out, i2 + di, j2 + dj, false);

                    if (tile != null) {
                        ctx.drawImage(tile, 0, 0, tiles.tilesize, tiles.tilesize,
                                      x + (di * tiles.tilesize / factor),
                                      y + (dj * tiles.tilesize / factor),
                                      tiles.tilesize / factor, tiles.tilesize / factor);
                    }
                }
            }
        }
    }

    function getTile(zoom, i, j, fetch = true) {
        if (i < 0 || i >= Math.pow(2, zoom))
            return null;
        if (j < 0 || j >= Math.pow(2, zoom))
            return null;

        let url = parseURL(tiles.url, zoom, i, j);
        let tile = getImage(known_tiles, url, fetch);

        return tile;
    }

    function getImage(cache, url, fetch = true) {
        if (typeof url != 'string')
            return url;

        let img = cache.get(url);

        missing_assets += (img == null && fetch);

        if (img == null && fetch) {
            if (fetch_queue.includes(url))
                return null;
            if (fetch_handles.has(url))
                return null;

            let handle = {
                valid: true,
                cache: cache,
                url: url,
                img: null
            };

            fetch_queue.push(handle);
        }

        return img;
    }

    function parseURL(url, zoom, i, j) {
        let ret = url.replace(/{[a-z]+}/g, m => {
            switch (m) {
                case '{s}': return 'a';
                case '{z}': return zoom;
                case '{x}': return i;
                case '{y}': return j;
                case '{r}': return '';
                case '{ext}': return 'png';
            }
        });

        return ret;
    }

    async function fetchImage(handle) {
        let img = await new Promise((resolve, reject) => {
            let img = new Image();

            if (!handle.valid) {
                reject(null);
                return;
            }

            img.onload = () => resolve(img);
            img.onerror = () => {
                if (!handle.valid)
                    reject(null);

                reject(new Error(`Failed to load texture '${handle.url}'`));
            };

            img.src = handle.url;
            img.crossOrigin = 'anonymous';

            handle.img = img;
        });

        // Fix latency spikes caused by image decoding
        if (typeof createImageBitmap != 'undefined')
            img = await createImageBitmap(img);

        return img;
    }

    function stopFetchers() {
        fetch_queue.length = 0;

        for (let handle of fetch_handles.values()) {
            handle.valid = false;

            if (handle.img != null)
                handle.img.setAttribute('src', '');
        }
    }

    function latLongToXY(latitude, longitude, zoom) {
        const MinLatitude = -85.05112878;
        const MaxLatitude = 85.05112878;
        const MinLongitude = -180;
        const MaxLongitude = 180;

        latitude = Util.clamp(latitude, MinLatitude, MaxLatitude);
        longitude = Util.clamp(longitude, MinLongitude, MaxLongitude);

        let x = (longitude + 180) / 360;
        let sinLatitude = Math.sin(latitude * Math.PI / 180);
        let y = 0.5 - Math.log((1 + sinLatitude) / (1 - sinLatitude)) / (4 * Math.PI);

        let size = mapSize(zoom);
        let px = Util.clamp(Math.round(x * size + 0.5), 0, ((size - 1) >>> 0));
        let py = Util.clamp(Math.round(y * size + 0.5), 0, ((size - 1) >>> 0));

        return { x: px, y: py };
    }

    this.coordToScreen = function(latitude, longitude) {
        let viewport = getViewport();
        let pos = latLongToXY(latitude, longitude, state.zoom);

        pos.x -= viewport.x1;
        pos.y -= viewport.y1;

        return pos;
    };

    this.screenToCoord = function(pos) {
        let size = mapSize(state.zoom);
        let px = Util.clamp(pos.x + state.pos.x - canvas.width / 2, 0, size);
        let py = size - Util.clamp(pos.y + state.pos.y - canvas.height / 2, 0, size);

        let x = (Util.clamp(px, 0, ((size - 1) >>> 0)) - 0.5) / size;
        let y = (Util.clamp(py, 0, ((size - 1) >>> 0)) - 0.5) / size;

        let longitude = (x * 360) - 180;
        let latitude = Math.atan(Math.sinh(2 * (y - 0.5) * Math.PI)) * (180 / Math.PI);

        return [latitude, longitude];
    };

    function mapSize(zoom) {
        return tiles.tilesize * Math.pow(2, zoom);
    }
}

function distance(p1, p2) {
    let dx = p1.x - p2.x;
    let dy = p1.y - p2.y;

    return Math.sqrt(dx * dx + dy * dy);
}

export { TileMap }
