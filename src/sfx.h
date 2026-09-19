#ifndef MKPSP_SFX_H
#define MKPSP_SFX_H

enum SfxId {
    SFX_NONE = 0,
    SFX_MENU_MOVE,
    SFX_MENU_CONFIRM,
    SFX_MENU_BACK,
    SFX_RACE_START,
    SFX_DRIFT,
    SFX_DRIFT_BOOST,
    SFX_ITEM,
    SFX_BOOST,
    SFX_CANNON,
    SFX_LANDING,
    SFX_WALL_HIT,
    SFX_HOP,
    SFX_PINBALL_LAUNCH,
    SFX_PINBALL_DASH,
    SFX_PINBALL_BUMPER,
    SFX_PINBALL_FLIPPER,
    SFX_PINBALL_BALL_HIT,
    SFX_PINBALL_EXIT,
    SFX_LAP,
    SFX_RACE_FINISH,
    SFX_MANSION_TREE,
    SFX_MANSION_CHANDELIER,
    SFX_MANSION_BOO,
    SFX_MANSION_PICTURE,
    SFX_MARIO_0,
    SFX_MARIO_1,
    SFX_MARIO_2,
    SFX_MARIO_3,
    SFX_MARIO_4,
    SFX_MARIO_5,
    SFX_MARIO_6,
    SFX_MARIO_7,
    SFX_MARIO_8,
    SFX_MARIO_9,
    SFX_MARIO_10
};

void sfx_init(void);
void sfx_play(enum SfxId id);
void sfx_set_engine(int enabled);
void sfx_set_drift(int enabled);
void sfx_shutdown(void);

#endif
