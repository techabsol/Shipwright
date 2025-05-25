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
bool playerBeingCaught = false;
const u16 TEXT_ID_CASTLE_CAPTURE = 0x6000;
const u16 TEXT_ID_GERUDO_CAPTURE = 0x702D;

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
    if (amount < 0) {
        NoHitTracker_IncrementCounter();
    }
}

void NoHitTracker_Load() {
    SaveManager::Instance->LoadData("counter", hitCounter, static_cast<uint16_t>(0));
}

void NoHitTracker_Save(SaveContext* saveContext, int sectionId, bool fullSave) {
    SaveManager::Instance->SaveData("counter", hitCounter);
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

bool NoHitTracker_CapturedByGuards() {
    auto mCtx = gPlayState->msgCtx;

    if (!playerBeingCaught) {
        if (mCtx.textId == TEXT_ID_CASTLE_CAPTURE || mCtx.textId == TEXT_ID_GERUDO_CAPTURE) {
            playerBeingCaught = true;
            NoHitTracker_IncrementCounter();
            return true;
        }
    }

    if (playerBeingCaught && mCtx.msgMode == MSGMODE_NONE) {
        playerBeingCaught = false;
    }

    return false;
}

void NoHitTracker_PlayerUpdate() {
    if (GameInteractor::IsSaveLoaded(false)) {
        const auto p = GET_PLAYER(gPlayState);
        if (p == nullptr) {
            return;
        }

        // Check order:
        // 1. Was the player grabbed
        // 2. Is the player being captured
        if (NoHitTracker_CheckPlayerGrabbed(p) ||
            NoHitTracker_CapturedByGuards()) { }
    }
}

bool incrementHeld = false;
bool decrementHeld = false;
void NoHitTracker_ManualHitUpdate() {
    if (GameInteractor::IsSaveLoaded(false)) {
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
    COND_HOOK(GameInteractor::OnInterfaceUpdate, 1, NoHitTracker_Draw)
    //COND_HOOK(GameInteractor::OnPlayerHealthChange, 1, NoHitTracker_HealthUpdate)
    COND_HOOK(GameInteractor::OnPlayerUpdate, 1, NoHitTracker_PlayerUpdate)
    COND_HOOK(GameInteractor::OnGameFrameUpdate, 1, NoHitTracker_ManualHitUpdate)
    COND_HOOK(GameInteractor::OnPlayerDamage, 1, NoHitTracker_HealthUpdate)
    SaveManager::Instance->AddLoadFunction("nohit", 1, NoHitTracker_Load);
    SaveManager::Instance->AddSaveFunction("nohit", 1, NoHitTracker_Save, true, SECTION_PARENT_NONE);
}

static RegisterShipInitFunc initFunc(RegisterNoHitTracker, { CVAR_NOHITTRACKER_NAME });

/*************************************************************************************
 *                                 No-Hit Rules                                      *
 *************************************************************************************
 *            What is a hit              |            What is safe                   *
 ****************************************|********************************************
 * [x] Damage                            | [x] Bonking                               *
 * [x] Damageless hits                   | [ ] Shielding 2, 3, 1 scrubs before Gohma *
 * [ ] Shielding                         | [ ] Scrub protecting scrub code           *
 * [ ] Save & quit as escape             | [ ] Mirror shield on Twinrova             *
 * [x] Voiding                           | [ ] Save & Quit in first room of dungeon  *
 * [x] Gibdo/ReDead grab                 | [ ] Save & Quit in Kokiri Forest as child *
 * [x] Dead Hand grab                    | [ ] Save & Quit in Temple of Time as      *
 * [ ] Wall Master grab                  |     adult                                 *
 * [x] Hits during i-frames              | [ ] Scripted cutscenes                    *
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

// Once shield hits are working, these rooms need to be ignored
// 2, 3, 1 scrub room is in room 9
// Scrub guard w/ code is in room 4

// Possible S&Q implementation: Track which file is getting loaded
// When game is closing, monitor which scene+room is being left
// On game load, if same file is being loaded, but scene+room is not the same, count as hit
