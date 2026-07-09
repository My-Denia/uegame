// UegameHUD.h - M7A.1 truthful readability overlay.
//
// A code-only AHUD (no UMG/Slate widget, no imported asset) that renders CURRENT M5/M6 runtime state
// on the normal game camera so a viewer of ordinary play (or a showcase capture) can read what the
// systems are doing without the log. It is strictly PRESENTATION: DrawHUD() reads live from the same
// truth-sources the forensic verbs read - ULoadoutComponent (Dungeon.LoadoutStatus), the ADungeonSpawner
// encounter cache (Dungeon.RoomRoles / Dungeon.EnemyRoster), UHealthComponent, and UUegameFloorManager -
// and never copies, fakes, recomputes, or writes any gameplay/determinism state. It consumes no RNG and
// adds no gameplay tick, so it cannot perturb any M5/M6 hash.
//
// Toggle with the console vars ui.ShowReadout (1=draw, 0=nothing) and ui.ShowReadoutScale (text scale).
// Wired as the game HUD via AuegameGameMode::HUDClass, inherited by the BP game mode. Present in all
// build configs (this is player-facing UI, not a forensic verb); it references no !UE_BUILD_SHIPPING code.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"

#include "UegameHUD.generated.h"

UCLASS()
class UEGAME_API AUegameHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
