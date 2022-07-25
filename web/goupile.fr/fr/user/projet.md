# Développement d'un projet

Pour créer un eCRF, il faut se connecter sur l'interface administrateur, afficher le panneau de configuration « Projets » [1] puis cliquer sur le menu « Accès » du projet d'intérêt [2]. Vous pouvez mettre le lien ouvert en favoris pour accéder directement au projet par la suite.

:::{figure-md}
<img src="img/dev_access.webp" alt="Accès à un projet" class="bg-primary mb-1" width="400px">

Accéder à un projet depuis le panneau d'administration
:::

## Vue par défaut

Un nouvel onglet s'ouvre dans votre navigateur. Le bandeau noir en haut de page permet de configurer l'affichage de la page. Vous pouvez afficher (ou non) un ou deux panneaux en sélectionnant (ou déselectionnant) le(s) panneau(x) d'intérêt. Par défaut, le panneau « Liste » [2] est sélectionné. Les différents panneaux de configuration sont : « Code » [1], « Liste » [2] et « Saisie » [3]. Le menu déroulant central [4] vous permet de naviguer entre les différentes pages de votre eCRF (ici la première page se nomme « Introduction »). Le menu déroulant intermédiaire [5] vous permet de naviguer entre les différents sous-projets si votre projet a été subdivisé. Enfin le menu déroulant tout à droite [6] vous permet de changer votre mot de passe ou déconnecter votre session.

:::{figure-md}
<img src="img/dev_panels.webp" alt="Menu de Goupile" class="bg-primary mb-1" width="400px">

Naviguer dans Goupile
:::

### Code

Le panneau de configuration « Code » vous permet d'éditer votre eCRF. Il contient deux onglets : « Application » et « Formulaire ». Par défaut, l'onglet « Formulaire » est affiché.

L'onglet « Formulaire » vous permet l'édition du contenu de votre eCRF pour une page donnée (ici « Introduction ». Pour rappel la navigation entre les différentes pages de votre formulaire s'effectue via le menu déroulant [1]).
Le contenu est édité en ligne de codes via un éditeur de script. Le langage de programmation est le JavaScript. La connaissance du langage n'est pas nécessaire pour la création et l'édition de scripts simples. L'édition de l'eCRF et les différents modules de code seront abordés plus en détails ultérieurement.

:::{figure-md}
<img src="img/dev_code1.webp" alt="Interface d'édition de formulaire" class="bg-primary mb-1" width="400px">

Modifier le code d'un formulaire dans Goupile
:::

L'onglet « Application » vous permet d'éditer la structure générale de votre eCRF. Elle permet ainsi de définir les différentes pages et ensemble de pages. La structure est également éditée en ligne de code via un éditeur de script (également Javascript). L'édition de la structure de l'eCRF et les différents modules de code seront abordés plus en détail ultérieurement.

:::{figure-md}
<img src="img/dev_code2.webp" alt="Interface d'édition de l'application" class="bg-primary mb-1" width="400px">

Modifier l'architecture de l'application dans Goupile
:::

### Liste

Le panneau « Liste » vous permet d'ajouter des nouvelles observations (« Ajouter un suivi ») et de monitorer le recueil de données. La variable « ID » correspond à l'identifiant d'un formulaire de recueil. Il s'agit d'un numéro séquentiel par défaut mais cela peut être paramétré. Les variables « Introduction », « Avancé » et « Mise en page » correspondent aux trois pages de l'eCRF d'exemple.

:::{figure-md}
<img src="img/dev_list.webp" alt="Liste des saisies réalisées" class="bg-primary mb-1" width="400px">

Naviguer dans les dossiers saisis
:::

### Saisie

Le panneau de configuration « Saisie » vous permet de réaliser le recueil d'une nouvelle observation (nouveau patient) ou de modifier une observation donnée (après sélection de l'observation dans le panneau de configuration « Liste »). La navigation entre les différentes pages de l'eCRF peut s'effectuer avec le menu déroulant [1] ou le menu de navigation [2]. Après avoir réalisé la saisie d'une observation, cliquer sur « Enregistrer » [3].

:::{figure-md}
<img src="img/dev_entry.webp" alt="Saisie de données dans Goupile" class="bg-primary mb-1" width="400px">

Saisir des données sur un formulaire Goupile
:::

## Widgets standards

Les widgets sont créés en appelant des fonctions prédéfinies avec la syntaxe suivante :

```
<span style="color: #24579d;">function</span> ( <span style="color: #c09500;">"nom_variable"</span>, <span style="color: #842d3d;">"Libellé présenté à l'utilisateur"</span> )</p>
function ( "nom_variable", "Libellé présenté à l'utilisateur" )
```

Les noms de variables doivent commencer par une lettre ou \_, suivie de zéro ou plusieurs caractères alphanumériques ou \_. Ils ne doivent pas contenir d'espaces, de caractères accentués ou tout autre caractère spécial.

:::{figure-md}
<img src="img/instant.png" alt="Affiche instanté" class="bg-primary mb-1" width="300px">

Exemples de widgets prédéfinis, avec le code à gauche et le résultat à droite
:::

La plupart des widgets acceptent des paramètres optionnels de cette manière :

```
<span style="color: #24579d;">function</span> ( <span style="color: #c09500;">"nom_variable"</span>, <span style="color: #842d3d;">"Libellé présenté à l'utilisateur"</span>, {
    <span style="color: #054e2d;">option1</span> : <span style="color: #431180;">valeur</span>,
    <span style="color: #054e2d;">option2</span> : <span style="color: #431180;">valeur</span>
} )
```

Attention à la syntaxe du code. Lorsque les parenthèses ou les guillemets ne correspondent pas, une erreur se produit et la page affichée ne peut pas être mise à jour tant que l'erreur persiste. La section sur les erreurs contient plus d'informations à ce sujet.

### Saisie d'information

XXX

### Autres widgets

XXX

## Erreurs de programmation

Les langages de programmation (comme Javascript utilisé ici) sont très sensibles aux erreurs de syntaxe. Si vous faites une erreur, le code ne peut pas être exécuté et un message d'erreur sera affiché.

:::{figure-md}
<img src="img/error.png" alt="Erreur de code" class="bg-primary mb-1" width="300px">

Voyez-vous l'erreur ? Il y a une parenthèse fermante en trop à droite.
:::

Si cela se produit, ne paniquez pas ! La plupart des erreurs sont faciles à corriger. Si vous ne trouvez pas l'erreur, revenez en arrière à l'aide de Ctrl+Z.

## Publication du formulaire

Publiez votre formulaire simplement lorsqu'il est prêt.

Une fois votre formulaire prêt, vous devez le publier pour le rendre accessible aux autres utilisateurs. Après publication, les utilisateurs pourront saisir des données sur ces formulaires.

Pour ce faire, cliquez sur le bouton Publier en haut à droite du panneau d'édition de code. Ceci affichera le panneau de publication (visible dans la capture à gauche).

:::{figure-md}
<img src="img/publish.webp" alt="Publication d'un projet" class="bg-primary mb-1" width="300px">

Publier un projet rend les modifications disponibles à tout le monde
:::

Ce panneau récapitule les modifications apportées et les actions qu'engendrera la publication. Dans la capture à droite, on voit qu'une page a été modifiée localement (nommée « accueils ») et sera rendue publique après acceptation des modifications.
