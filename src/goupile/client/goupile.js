// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

const goupile = new function() {
    let self = this;

    let db;

    let profile = {};
    let session_rnd;
    let passport;

    let controller;
    let current_url;

    this.start = async function() {
        ui.init();

        await registerSW();
        await initDB();

        if (ENV.base_url === '/admin/') {
            controller = new AdminController;
        } else {
            controller = new InstanceController;
        }

        await controller.start();
    };

    async function registerSW() {
        if (navigator.serviceWorker != null) {
            if (ENV.cache_offline) {
                let registration = await navigator.serviceWorker.register(`${ENV.base_url}sw.pk.js`);
                let entry = new log.Entry;

                if (registration.waiting) {
                    entry.error('Fermez tous les onglets pour terminer la mise à jour');
                } else {
                    registration.addEventListener('updatefound', () => {
                        if (registration.active) {
                            entry.progress('Mise à jour en cours, veuillez patienter');

                            registration.installing.addEventListener('statechange', e => {
                                if (e.target.state === 'installed') {
                                    entry.success('Mise à jour effectuée, l\'application va redémarrer');
                                    setTimeout(() => document.location.reload(), 3000);
                                }
                            });
                        }
                    });
                }
            } else {
                let registration = await navigator.serviceWorker.getRegistration();
                let entry = new log.Entry;

                if (registration != null) {
                    entry.progress('Mise à jour en cours, veuillez patienter');

                    await registration.unregister();

                    entry.success('Mise à jour effectuée, l\'application va redémarrer');
                    setTimeout(() => document.location.reload(), 3000);
                }
            }
        }
    }

    async function initDB() {
        let db_name = `goupile:${ENV.base_url}`;

        db = await indexeddb.open(db_name, 1, (db, old_version) => {
            switch (old_version) {
                case null: {
                    db.createStore('usr_profiles');
                } // fallthrough
            }
        });
    }

    this.runLogin = function() {
        return ui.runScreen((d, resolve, reject) => {
            d.output(html`
                <img id="gp_logo" src=${ENV.base_url + 'favicon.png'} alt="" />
                <br/>
            `);

            let username = d.text('*username', 'Nom d\'utilisateur');
            let password = d.password('*password', 'Mot de passe');

            d.action('Se connecter', {disabled: !d.isValid()}, async () => {
                try {
                    await self.login(username.value, password.value);
                    resolve(username.value);
                } catch (err) {
                    // Never reject because we want to keep the screen open
                    log.error(err);
                }
            });
        });
    };

    this.login = async function(username, password) {
        let progress = log.progress('Connexion en cours');

        try {
            let query = new URLSearchParams;
            query.set('username', username.toLowerCase());
            query.set('password', password);

            let response = await net.fetch(`${ENV.base_url}api/user/login`, {
                method: 'POST',
                body: query
            });

            if (response.ok) {
                profile = await response.json();
                session_rnd = util.getCookie('session_rnd');
                passport = (profile.passport != null) ? util.base64ToBytes(profile.passport) : null;

                // Save for offline login
                {
                    let salt = nacl.randomBytes(24);
                    let key = await deriveKey(password, salt);

                    let enc = await encrypt(profile, key);

                    await db.saveWithKey('usr_profiles', username, {
                        salt: util.bytesToBase64(salt),
                        profile: enc
                    });
                }

                progress.success('Connexion réussie');
            } else {
                let err = (await response.text()).trim();
                throw new Error(err);
            }
        } catch (err) {
            progress.close();
            throw err;
        }
    }

    function deriveKey(password, salt) {
        return new Promise((resolve, reject) => {
            scrypt(password, salt, {
                N: 16384,
                r: 8,
                p: 1,
                dkLen: 32,
                encoding: 'binary'
            }, resolve);
        });
    }

    this.logout = async function() {
        let progress = log.progress('Déconnexion en cours');

        try {
            let response = await net.fetch(`${ENV.base_url}api/user/logout`, {method: 'POST'})

            if (response.ok) {
                profile = {};
                session_rnd = undefined;
                passport = undefined;

                // Clear state and start from fresh as a precaution
                document.location.reload();
            } else {
                let err = (await response.text()).trim();
                throw new Error(err);
            }
        } catch (err) {
            progress.close();
            throw err;
        }
    }

    this.syncProfile = async function() {
        let new_rnd = util.getCookie('session_rnd');

        if (new_rnd !== session_rnd) {
            let response = await net.fetch(`${ENV.base_url}api/user/profile`);

            profile = await response.json();
            session_rnd = util.getCookie('session_rnd');
            passport = (profile.passport != null) ? util.base64ToBytes(profile.passport) : null;
        }
    };

    this.isAuthorized = function() { return !!profile.username; };
    this.getUserName = function() { return profile.username; };

    this.syncHistory = function(url, push = true) {
        if (push && current_url != null && url !== current_url) {
            window.history.pushState(null, null, url);
        } else {
            window.history.replaceState(null, null, url);
        }

        current_url = url;
    };

    this.encryptWithPassport = function(obj) {
        if (passport == null)
            throw new Error('Cannot encrypt without passport');

        return encrypt(obj, passport);
    };

    this.decryptWithPassport = function(enc) {
        if (passport == null)
            throw new Error('Cannot decrypt without passport');

        return decrypt(enc, passport);
    };

    async function encrypt(obj, key) {
        let nonce = new Uint8Array(24);
        crypto.getRandomValues(nonce);

        let json = JSON.stringify(obj);
        let message = util.stringToBytes(json);
        let box = nacl.secretbox(message, nonce, key);

        let enc = {
            nonce: util.bytesToBase64(nonce),
            box: util.bytesToBase64(box)
        };
        return enc;
    }

    async function decrypt(enc, key) {
        let nonce = util.base64ToBytes(enc.nonce);
        let box = util.base64ToBytes(enc.box);

        let message = nacl.secretbox.open(box, nonce, key);
        if (message == null)
            throw new Error('Failed to decrypt message: wrong key?');

        let json = util.bytesToString(message);
        let obj = JSON.parse(json);

        return obj;
    }
};
