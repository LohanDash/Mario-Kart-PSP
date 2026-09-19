# Mario Kart PSP

**Mario Kart PSP** is a fan-made Mario Kart installment developed natively for the PlayStation Portable.

The goal of the project is to create a complete Mario Kart experience designed specifically around the PSP hardware, with its own selection of characters, vehicles, courses, gameplay systems, presentation, and technical choices.

Rather than reproducing a single existing Mario Kart game, Mario Kart PSP takes inspiration from multiple entries in the series while being developed as its own standalone installment.

The game runs on a custom engine written in C using PSPSDK and targets real PSP hardware.

## Project goals

Mario Kart PSP aims to deliver:

- fast arcade-style racing;
- drifting and mini-turbos;
- items and boost mechanics;
- Grand Prix and Time Trial gameplay;
- original course layouts;
- retro courses adapted for PSP;
- Mario characters and vehicles;
- custom menus and presentation;
- music and sound effects;
- a lightweight engine optimized for the PSP.

## Current development state

The project already includes a playable racing foundation with:

- native PSP 3D rendering;
- analog steering;
- acceleration and braking;
- race camera;
- course collision;
- lap tracking;
- drifting;
- mini-turbos;
- boost mechanics;
- music playback;
- sound effects;
- course loading;
- object loading;
- PSP-specific asset conversion tools.

Several courses are currently being used during development, including:

- Mario Circuit;
- Waluigi Pinball;
- Luigi's Mansion.

These courses are part of the current development environment and may evolve as the project takes shape.

## Technical direction

Mario Kart PSP is built as a native PSP game rather than an emulator or compatibility layer.

The engine is written primarily in C and uses PSPSDK for:

- rendering;
- controller input;
- audio;
- file access;
- memory management;
- PSP executable generation.

Development tools are also used to convert models, textures, collision data, music, and other assets into formats suitable for PSP hardware.

## Building

The project requires **PSPSDK**.

From an environment configured with PSPDEV:

```sh
make
