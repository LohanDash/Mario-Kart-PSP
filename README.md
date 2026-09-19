# Mario Kart PSP

**Mario Kart PSP** est un fangame Mario Kart développé nativement pour la PlayStation Portable.

L'objectif n'est plus de porter ou de réécrire Mario Kart DS, mais de créer un **véritable opus Mario Kart original pensé pour la PSP**, avec son propre contenu, sa propre sélection de circuits et son propre gameplay.

Le projet utilise un moteur maison écrit en C avec le PSPSDK.

## Objectif

Créer un épisode Mario Kart complet adapté aux capacités de la PSP :

- gameplay arcade inspiré de la série Mario Kart ;
- drift et mini-turbos ;
- objets ;
- courses à plusieurs tours ;
- contre-la-montre ;
- Grand Prix ;
- personnages et véhicules Mario ;
- circuits originaux et circuits rétro ;
- musique et effets sonores ;
- interface pensée pour la PSP.

Le projet n'est pas une émulation de Mario Kart DS et ne cherche pas à reproduire exactement son moteur.

## État actuel

Le moteur possède déjà une base jouable comprenant notamment :

- rendu 3D natif sur PSP ;
- contrôles analogiques ;
- accélération, freinage et direction ;
- caméra de course ;
- collisions avec les circuits ;
- drift et mini-turbos ;
- système de tours ;
- contre-la-montre ;
- musique et effets sonores ;
- système de boost ;
- chargement de circuits et d'objets ;
- outils de conversion de modèles et de données vers des formats adaptés à la PSP.

Plusieurs circuits servent actuellement au développement, notamment :

- Mario Circuit ;
- Waluigi Pinball ;
- Luigi's Mansion.

Ils pourront évoluer pour correspondre à la direction du nouvel opus.

## Compilation

Le projet nécessite le **PSPSDK**.

Depuis un environnement configuré avec PSPDEV :

```sh
make
