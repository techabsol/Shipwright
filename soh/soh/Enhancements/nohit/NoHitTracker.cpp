//
// Created by techabsol on 5/15/2025.
//
#include <libultraship/bridge.h>

#include "StringHelper.h"
#include "soh/SaveManager.h"
#include "soh/ShipInit.hpp"
#include "soh/Notification/Notification.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

extern "C" {
    extern PlayState* gPlayState;
    extern SaveContext gSaveContext;
#include "functions.h"
#include "variables.h"
#include "macros.h"
}

#define CVAR_NOHITTRACKER_NAME CVAR_ENHANCEMENT("NoHitTracker")

/***
 * NoHit Tracker
 **/

u16 hitCounter = 0;
bool playerBeingHeld = false;
bool playerBeingCaught_HC = false;
bool playerBeingCaught_Gerudo = false;
bool playerReceivedDamage = false;
const std::string gerudoCaptureMessage = "\bHalt! Stay where you are!";
const std::string castleCaptureMessage = "\b\x6" "7Hey you! Stop!\x1\x6(You, kid, over there!";

void NoHitTracker_IncrementCounter() {
    hitCounter++;
    Notification::Emit({
        .message = StringHelper::Sprintf("Now at %d hits", hitCounter)
    });
}

void NoHitTracker_DecrementCounter() {
    hitCounter--;
    Notification::Emit({
        .message = StringHelper::Sprintf("Now at %d hits", hitCounter)
    });
}

void NoHitTracker_HealthUpdate(int16_t amount) {
    Notification::Emit({
        .message = StringHelper::Sprintf("Received %d damage", amount)
    });
    if (amount < 0) {
        NoHitTracker_IncrementCounter();
        playerReceivedDamage = true;
    }
}

void NoHitTracker_Load() {
    SaveManager::Instance->LoadData("counter", hitCounter, static_cast<uint16_t>(0));
}

void NoHitTracker_Save(SaveContext* saveContext, int sectionId, bool fullSave) {
    SaveManager::Instance->SaveData("counter", hitCounter);
}

void NoHitTracker_Init() {

}

void NoHitTracker_Draw() {

}

bool NoHitTracker_CheckPlayerGrabbed(Player* player) {
    // Oddly, Wallmaster doesn't trigger this flag
    if (player->stateFlags2 & PLAYER_STATE2_GRABBED_BY_ENEMY && !playerBeingHeld) {
        playerBeingHeld = true;
        NoHitTracker_IncrementCounter();
        return true;
    }

    if (!(player->stateFlags2 & PLAYER_STATE2_GRABBED_BY_ENEMY) && playerBeingHeld) {
        playerBeingHeld = false;
    }
    return false;
}

bool NoHitTracker_CapturedByGuards(Player* player) {
    if (!playerBeingCaught_HC) {
        if (gPlayState->sceneNum == SCENE_HYRULE_CASTLE ||
            gPlayState->sceneNum == SCENE_CASTLE_COURTYARD_GUARDS_DAY ||
            gPlayState->sceneNum == SCENE_GERUDOS_FORTRESS ||
            gPlayState->sceneNum == SCENE_THIEVES_HIDEOUT) {
            if (gPlayState->msgCtx.msgMode == MSGMODE_TEXT_DISPLAYING) {
                auto mCtx = gPlayState->msgCtx;
                auto textStr = std::string(reinterpret_cast<char*>(mCtx.msgBufDecoded));

                if (textStr.starts_with(gerudoCaptureMessage) || textStr.starts_with(castleCaptureMessage)) {
                    playerBeingCaught_HC = true;
                    NoHitTracker_IncrementCounter();
                    return true;
                }
            }
        }
    }

    if (playerBeingCaught_HC && player->stateFlags1 & PLAYER_STATE1_DAMAGED) {
        playerBeingCaught_HC = false;
    }

    return false;
}

bool NoHitTracker_CheckShieldHits(Player* player) {
    // Doesn't really work. Fires when damage is received by the
    // player, but does not fire if the player is not included in
    // collision calcs (i.e. wolfos at range but hitting shield)
    if (player->shieldQuad.base.actor->colChkInfo.damage > 0) {
        Notification::Emit({
            .message = "Shield hit (simple)"
        });
    }
    return false;
}

void NoHitTracker_PlayerUpdate() {
    if (GameInteractor::IsSaveLoaded(false)) {
        Player* p = GET_PLAYER(gPlayState);
        auto colChkCtx = gPlayState->colChkCtx;
        if (p == nullptr) {
            return;
        }

        // Check order:
        // 1. Was the player grabbed
        // 2. Is the player being captured
        // 3. Did player take damage
        if (NoHitTracker_CheckPlayerGrabbed(p) ||
            NoHitTracker_CapturedByGuards(p) ||
            NoHitTracker_CheckShieldHits(p)) {
            return;
        }


        for (int i = 0; i < colChkCtx.colOCCount; i++) {
            auto colOc = colChkCtx.colOC[i];
            if ((colOc->ocFlags2 & OC2_HIT_PLAYER) > 0) {
                Notification::Emit({
                    .message = "Actor hit player"
                });
            }
        }

        // Only place I've reproduced this so far is skulltulas
        if (p->knockbackType > 0 && !playerReceivedDamage) {
            Notification::Emit({
                .message = "Player received knockback without damage"
            });
        }

        // Collision detection
        if (p->actor.colChkInfo.damage > 0) {
            NoHitTracker_IncrementCounter();
        }

        if (playerReceivedDamage) {
            playerReceivedDamage = false;
        }
    }
}

bool incrementHeld = false;
bool decrementHeld = false;
void NoHitTracker_ManualHitUpdate() {
    if (GameInteractor::IsSaveLoaded(false)) {
        auto p = GET_PLAYER(gPlayState);
        if (p == nullptr) return;

        if (!incrementHeld && CHECK_BTN_ALL(gPlayState->state.input[0].cur.button, BTN_L | BTN_DUP)) {
            NoHitTracker_IncrementCounter();
            incrementHeld = true;
        } else if (!decrementHeld && CHECK_BTN_ALL(gPlayState->state.input[0].cur.button, BTN_L | BTN_DDOWN)) {
            NoHitTracker_DecrementCounter();
            decrementHeld = true;
        } else if (incrementHeld && !CHECK_BTN_ALL(gPlayState->state.input[0].cur.button, BTN_DUP)) {
            incrementHeld = false;
        } else if (decrementHeld && !CHECK_BTN_ALL(gPlayState->state.input[0].cur.button, BTN_DDOWN)) {
            decrementHeld = false;
        }
    }
}

void RegisterNoHitTracker() {
    osSyncPrintf("Registering NoHitTracker");
    COND_HOOK(GameInteractor::OnInterfaceUpdate, 1, NoHitTracker_Draw)
    //COND_HOOK(GameInteractor::OnLoadGame, 1, [](int32_t fileNum) { NoHitTracker_Init(); })
    COND_HOOK(GameInteractor::OnPlayerHealthChange, 1, NoHitTracker_HealthUpdate)
    COND_HOOK(GameInteractor::OnPlayerUpdate, 1, NoHitTracker_PlayerUpdate)
    COND_HOOK(GameInteractor::OnGameFrameUpdate, 1, NoHitTracker_ManualHitUpdate)
    SaveManager::Instance->AddLoadFunction("nohit", 1, NoHitTracker_Load);
    SaveManager::Instance->AddSaveFunction("nohit", 1, NoHitTracker_Save, true, SECTION_PARENT_NONE);
}

static RegisterShipInitFunc initFunc(RegisterNoHitTracker, { CVAR_NOHITTRACKER_NAME });

/*************************************************************************************
 *                                 No-Hit Rules                                      *
 *************************************************************************************
 *            What is a hit              |            What is safe                   *
 ****************************************|********************************************
 * [x] Damage                            | [ ] Bonking                               *
 * [ ] Damageless hits                   | [ ] Shielding 2, 3, 1 scrubs before Gohma *
 * [ ] Shielding                         | [ ] Scrub protecting scrub code           *
 * [ ] Save & quit as escape             | [ ] Mirror shield on Twinrova             *
 * [ ] Voiding                           | [ ] Save & Quit in first room of dungeon  *
 * [x] Gibdo/ReDead grab                 | [ ] Save & Quit in Kokiri Forest as child *
 * [x] Dead Hand grab                    | [ ] Save & Quit in Temple of Time as      *
 * [ ] Wall Master grab                  |     adult                                 *
 * [ ] Hits during i-frames              | [ ] Scripted cutscenes                    *
 * [x] Getting caught by castle guards   | [ ] Hits while not in control of Link     *
 * [x] Getting caught in Gerudo Fortress | [x] Failed songs                          *
 *                                       | [ ] Rolling through blade traps           *
 *************************************************************************************/

/**
 * Non-ruleset todos
 * TODO: Figure out how to make configurable/toggleable. Hard coded on
 * TODO: Part of configuring should allow switching between NoHit and NoDamage
 * Let's be honest, NoDamage is the easier ruleset to implement.
 * TODO: Persistent counter instead of using SoH notifications
 **/

// One thing I considered is just listening for audio cues. But:
// 1. There isn't a hook for that
// 2. I haven't checked to see if those are consistent when the sounds are randomized

// Another thing I thought of is hooking into the animations. Haven't investigated
// to see if this is reliable enough.

// Once shield hits are working, these rooms need to be ignored
// 2, 3, 1 scrub room is in room 9
// Scrub guard w/ code is in room 4

// The trick with damageless hits/iframe hits is that the game skips calling damage
// if invincible. So OnPlayerHealthChange doesn't get called.
// We have access to player invincibility, but I'm not sure how to detect hits

// Voidout can be read from play state:
// play->transitionTrigger == TRANS_TRIGGER_START
// play->nextEntranceIndex == gSaveContext.respawn[RESPAWN_MODE_DOWN].entranceIndex
// play->transitionType = TRANS_TYPE_FADE_BLACK
// gSaveContext.respawnFlag == 1
// OR Player.state2 has FORCED_VOID_OUT flag?

// Bonking should be fine, but I think we're going to have to start snooping
// on the collision data, so we'll have to exclude bonks

// Possible S&Q implementation: Track which file is getting loaded
// When game is closing, monitor which scene+room is being left
// On game load, if same file is being loaded, but scene+room is not the same, count as hit
