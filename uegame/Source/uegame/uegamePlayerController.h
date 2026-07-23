// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "uegamePlayerController.generated.h"

class UInputMappingContext;
class UUserWidget;

enum class EUegameProductModal : uint8
{
	None = 0,
	Restart,
	Quit
};

/**
 *  PlayerController for the shipping product shell.
 *  Gameplay truth remains in FloorManager; this class owns only local presentation modals
 *  (welcome and confirmation) and routes ordinary keyboard input to that authority.
 */
UCLASS(abstract)
class AuegamePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	bool IsWelcomeVisible() const { return bWelcomeVisible; }
	bool IsConfirmationVisible() const { return ProductModal != EUegameProductModal::None; }
	bool IsBlockingGameplayInput() const { return bWelcomeVisible || IsConfirmationVisible(); }
	FString GetConfirmationTitle() const;
	FString GetConfirmationAction() const;
	
protected:

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category ="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** Mobile controls widget to spawn */
	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	/** Pointer to the mobile controls widget */
	UPROPERTY()
	TObjectPtr<UUserWidget> MobileControlsWidget;

	/** If true, the player will use UMG touch controls even if not playing on mobile platforms */
	UPROPERTY(EditAnywhere, Config, Category = "Input|Touch Controls")
	bool bForceTouchControls = false;

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

	/** Returns true if the player should use UMG touch controls */
	bool ShouldUseTouchControls() const;

private:
	void HandleAccept();
	void HandlePauseOrCancel();
	void HandleRestart();
	void HandleQuit();
	void OpenConfirmation(EUegameProductModal Modal);
	void CancelConfirmation();
	void ConfirmCurrentModal();

	bool bWelcomeVisible = true;
	bool bPausedForConfirmation = false;
	EUegameProductModal ProductModal = EUegameProductModal::None;
};
