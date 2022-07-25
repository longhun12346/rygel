# Principes généraux

## Introduction

Goupile permet de concevoir un eCRF avec une approche un peu différente des outils habituels, puisqu'il s'agit de programmer le contenu des formulaires, tout en automatisant les autres aspects communs à tous les eCRF :

- Types de champs préprogrammés et flexibles
- Publication des formulaires
- Enregistrement et synchronisation des données
- Recueil en ligne et hors ligne, sur ordinateur, tablette ou mobile
- Gestion des utilisateurs et droits

En plus des fonctionnalités habituelles, nous nous sommes efforcés de réduire au maximum le délai entre le développement d'un formulaire et la saisie des données.

Il s'agit d'un *outil en développement* et certains choix sont encore en cours. Les indications **[WIP]** désignent des fonctionnalités ou options en cours de développement ou d'amélioration.

Même si une version publique compilée n'est pas encore disponible, vous pouvez tester la [démo en ligne](https://goupile.fr/demo/).

## Domaines et projets

Chaque service Goupile dispose d'un domaine (ou sous-domaine). Par exemple, [demo.goupile.fr](https://demo.goupile.fr/) et [psy-lille.goupile.fr](https://psy-lille.goupile.fr/) sont des services distincts avec des bases de données séparées et des utilisateurs différents (même si possiblement hébergés sur un même serveur).

Lors de la création d'un domaine, un (ou plusieurs) administrateurs de confiance sont désignés pour en gérer les projets et les utilisateurs. Une paire de clé de chiffrement est générée pour réaliser les backups des bases de données du domaine. La clé publique est stockée sur le serveur pour créer les backups. La clé privée est confiée aux administrateurs désignés et n'est pas stockée; sa perte **entraîne la perte de tous les backups** de ce domaine.

*Les détails sur le chiffrement utilisé sont détaillés dans la section sur les [choix architecturaux](serveur.md).*

Ce sont les administrateurs qui peuvent créér les projets et leur affecter des utilisateurs, soit pour qu'ils conçoivent les formulaires, soit pour qu'ils y saisissent les données.

## Gestion des utilisateurs

Chaque domaine Goupile contient une liste d'utilisateurs.

:::{figure-md}
<img src="img/admin.webp" alt="Interface d'administration" class="bg-primary mb-1" width="200px">

Gérez vos projets et vos utilisateurs au sein du module d'administration
:::

Ces comptes utilisateurs sont gérés par le ou les administrateurs désignés pour ce domaine, qui peuvent les créer, les modifier et les supprimer.

Chaque utilisateur peut être affecté à un ou plusieurs projets, avec un ensemble de droits en fonction de ses préprogatives. Il existe deux ensembles de droits :

- *Droits de développement*, qui permettent de configurer un projet et ses formulaires
- *Droits d'accès*, qui permettent d'accéder aux données

Ces droits sont détaillés dans les tableaux qui suivent :

Droit       | Explication
----------- | --------------------------------------------------------
*Develop*   | Modification des formulaires
*Publish*   | Publication des formulaires modifiés
*Configure* | Configuration du projet et des centres (multi-centrique)
*Assign*    | Modification des droits des utilisateurs sur le projet

Droit       | Explication
----------- | -----------------------------------------------------------------------
*Load*      | Lecture des enregistrements
*Save*      | Modification des enregistrements
*Export*    | CExport facile des données (CSV, XLSX, etc.)
*Batch*     | Recalcul de toutes les variables calculées sur tous les enregistrements
*Message*   | Envoi de mails et SMS via les formulaires

Par défaut l'authentification des utilisateurs repose sur un couple identifiant / mot de passe. Ce mot de passe est stocké hashé en base (libsodium pwhash).

Plusieurs **modes d'authentification forte** sont disponibles ou prévus :

- Fichier clé supplémentaire stocké sur clé USB (ce mode présente l'avantage d'être compatible avec une utilisation chiffrée hors ligne)
- Support de tokens TOTP avec applications d'authentification (Authy, FreeOTP, Google Authenticator, etc.)
- Support de clés physiques avec Webauthn (Yubikey, etc.) **[WIP]**
