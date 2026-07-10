// Copyright Epic Games, Inc. All Rights Reserved.

#include "uegameGameMode.h"

#include "UI/UegameHUD.h"

AuegameGameMode::AuegameGameMode()
{
	// M7A.1: use the truthful readability overlay as the game HUD. Set on the abstract C++ base so the
	// map's BP game mode (BP_ThirdPersonGameMode, reparented from TP_ThirdPersonGameMode) inherits it;
	// no .uasset edit needed. Presentation-only - does not affect input, camera, or gameplay.
	HUDClass = AUegameHUD::StaticClass();
}
