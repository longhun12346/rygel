// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

let user = (function() {
    let self = this;

    this.route = {};

    this.run = async function() {
        if (!env.has_users)
            throw new Error('Le module utilisateur est désactivé');

        switch (self.route.mode) {
            case 'login': { runLogin(); } break;

            default: {
                throw new Error(`Mode inconnu '${self.route.mode}'`);
            } break;
        }
    };

    this.parseURL = function(path, query) {
        let parts = path.split('/');

        let args = {
            mode: parts[0] || 'login'
        };

        return args;
    };

    this.makeURL = function(args = {}) {
        args = {...self.route, ...args};
        return `${env.base_url}user/${args.mode}/`;
    };

    // ------------------------------------------------------------------------
    // Login
    // ------------------------------------------------------------------------

    function runLogin() {
        // Options
        render(html``, document.querySelector('#th_options'));

        // View
        render(html`
            <form @submit=${e => { handleLoginSubmit(); e.preventDefault(); }}>
                <fieldset id="usr_fieldset" style="margin: 0; padding: 0; border: 0;">
                    <label>Utilisateur : <input id="usr_username" type="text"/></label>
                    <label>Mot de passe : <input id="usr_password" type="password"/></label>

                    <button>Se connecter</button>
                </fieldset>
            </form>
        `, document.querySelector('#th_view'));

        document.querySelector('#usr_username').focus();
    }

    async function handleLoginSubmit() {
        let fieldset_el = document.querySelector('#usr_fieldset');
        let username_el = document.querySelector('#usr_username');
        let password_el = document.querySelector('#usr_password');

        let success;
        try {
            fieldset_el.disabled = true;
            success = await self.login(username_el.value, password_el.value);
        } finally {
            fieldset_el.disabled = false;
        }

        if (success) {
            username_el.value = '';
            password_el.value = '';

            await thop.go();
        } else {
            password_el.focus();
        }
    }

    this.login = async function(username, password) {
        let response = await fetch(`${env.base_url}api/login.json`, {
            method: 'POST',
            body: new URLSearchParams({
                username: username,
                password: password
            })
        });

        if (response.ok) {
            log.success('Vous êtes connecté(e) !');
            self.readSessionCookies(false);
        } else {
            log.error('Connexion refusée : utilisateur ou mot de passe incorrect');
        }

        return response.ok;
    };

    this.logout = async function() {
        let response = await fetch(`${env.base_url}api/logout.json`, {method: 'POST'});

        if (response.ok) {
            log.success('Vous êtes déconnecté(e)');
            self.readSessionCookies(false);
        } else {
            // Should never happen, but just in case...
            log.error('Déconnexion refusée');
        }

        return response.ok;
    };

    // ------------------------------------------------------------------------
    // Session
    // ------------------------------------------------------------------------

    let username;
    let url_key = 0;

    this.readSessionCookies = function(warn = true) {
        let prev_url_key = url_key;

        username = util.getCookie('username') || null;
        url_key = util.getCookie('url_key') || 0;

        if (url_key !== prev_url_key && !username && warn)
            log.info('Votre session a expiré');
    };

    this.isConnected = function() { return !!url_key; }
    this.getUrlKey = function() { return url_key; }
    this.getUserName = function() { return username; }

    return this;
}).call({});
