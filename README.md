# MKPSP

Réécriture expérimentale du moteur de Mario Kart DS pour PSP. La ROM originale
n'est jamais incluse au projet : les outils lisent la copie fournie localement.

## État actuel

- lecture et extraction du NitroFS de la ROM DS ;
- prototype PSP natif : contrôles analogiques, accélération, freinage, direction,
  caméra suiveuse et piste 3D de test ;
- base prévue pour convertir ensuite les formats de circuits de MKDS.

Ce n'est pas une recompilation directe : le programme DS est en ARM et le
programme PSP doit être réécrit pour le processeur MIPS de la console.

## Extraire les données de la ROM

```sh
python3 tools/nds_extract.py "Mario Kart DS (Europe) (En,Fr,De,Es,It).nds" assets/extracted
```

Les circuits sont encore contenus dans des archives CARC. Exemple pour
extraire le circuit Mario et ses textures :

```sh
python3 tools/carc_extract.py assets/extracted/data/Course/mario_course.carc assets/courses/mario
python3 tools/carc_extract.py assets/extracted/data/Course/mario_courseTex.carc assets/courses/mario_tex
```

## Compiler pour PSP

Installer PSPSDK, puis lancer :

```sh
make
```

Copier ensuite `EBOOT.PBP` dans `PSP/GAME/MKPSP/` sur la Memory Stick.

## Donnees de jeu fournies par l'utilisateur

Le programme et les donnees Nintendo restent separes. Le depot ne contient
jamais les modeles, textures, circuits ou sons extraits de la ROM. Chaque
utilisateur doit creer son propre dossier `data/` a partir de sa copie de MKDS.

Disposition sur la Memory Stick :

```text
PSP/GAME/MarioKartPSP/
|-- EBOOT.PBP
`-- data/
    |-- characters/
    |-- karts/
    `-- courses/
```

Le dossier `data/` est ignore par Git. Les futurs chargeurs PSP liront les
fichiers relativement au dossier de l'EBOOT afin que le nom du lecteur
(`ms0:`, `ef0:` ou autre) n'ait pas besoin d'etre code en dur.

Commandes du prototype : stick ou croix pour tourner, X pour accélérer, carré
pour freiner/reculer et Start pour quitter.
