# Utilisation de Goupile

Goupile est codé comme une application web de type SPA (Single Page Application). Un bref aperçu des différentes facettes de Goupile est donné ci-après; référez-vous au manuel utilisateur pour des informations plus détaillées.

## Conception d'un eCRF

:::{figure-md}
<img src="img/editor.webp" alt="Interface de conception" class="bg-primary mb-1" width="200px">

Programmez la page dans **l'éditeur** (gauche), et le **résultat** (droite) s'affiche immédiatement
:::

Lors de la conception d'un formulaire, l'utilisateur le programme en Javascript via un éditeur texte en ligne, en appelant des fonctions prédéfinies qui génèrent les champs voulus par l'utilisateur. L'eCRF s'affiche directement sur le même écran.

Pour créer un eCRF, l'utilisateur commence par définir l'organisation et la succession des pages de saisie et des tables de données sous-jacentes. Il est possible de créer simplement des eCRF à plusieurs tables avec des relations 1-à-1 et 1-à-N (parent-enfants) à partir de ce mode.

Le contenu des pages est également défini en Javascript. Le fait de programmer nous donne beaucoup de possibilités, notamment la réalisation de formulaires complexes (conditions, boucles, calculs dynamiques, widgets spécifiques, etc.), sans sacrifier la simplicité pour les formulaires usuels.

## Validation des données

La vérification de la validité des données par rapport aux contraintes imposées a lieu côté client (systématiquement) et côté serveur (sur les pages où cette option est activée). Celle-ci repose sur le code Javascript de chaque page, qui peut définir des conditions et des erreurs en fonction des données saisies.

Ces erreurs alimentent la base de *contrôles qualités* qui peuvent ensuite être monitorées **[WIP]**.

Pour assurer la sécurité du serveur malgré l'exécution de code Javascript potentiellement malveillant, plusieurs mesures sont prises et détaillées dans la section [Architecture du serveur](serveur.md#architecture-du-serveur).

## Support hors ligne

Les eCRF développés avec Goupile peuvent fonctionner en mode hors ligne (si cette option est activée). Dans ce cas, Goupile utilise les fonctionnalités PWA des navigateurs modernes pour pouvoir mettre en cache ses fichiers et les scripts des formulaires, et être installé en tant que pseudo-application native.

Dans ce cas, les données sont synchronisées dans un deuxième temps lorsque la connexion est disponible.

Les données hors ligne sont chiffrées symétriquement à l'aide d'une clé spécifique à chaque utilisateur et connue du serveur. Cette clé est communiquée au navigateur après une authentification réussie.

Pour que l'utilisateur puisse se connecter à son application hors ligne, une copie de son profil (dont la clé de chiffrement des données hors ligne) est stockée sur sa machine, chiffrée par une clé dérivée de son mot de passe. Lorsqu'il cherche à se connecter hors ligne, son identifiant et son mot de passe sont utilisés pour déchiffrer ce profil et pouvoir accéder aux données locales.

Si le client installable est utilisé (basé sur Electron), l'authentification hors ligne peut aussi être configurée en mode fort, avec nécessité de brancher une clé USB contenant une seconde clé de chiffrement pour pouvoir se connecter à l'eCRF.