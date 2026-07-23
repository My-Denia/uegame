// Copyright Epic Games, Inc. All Rights Reserved.


#include "uegamePlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "Combat/FloorManager.h"
#include "Presentation/PresentationFeedbackComponent.h"
#include "uegame.h"
#include "Widgets/Input/SVirtualJoystick.h"

void AuegamePlayerController::BeginPlay()
{
	Super::BeginPlay();

	// only spawn touch controls on local player controllers
	if (IsLocalPlayerController() && ShouldUseTouchControls())
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(Loguegame, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}
}

void AuegamePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}
	}

	InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &AuegamePlayerController::HandleAccept)
		.bExecuteWhenPaused = true;
	InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AuegamePlayerController::HandleAccept)
		.bExecuteWhenPaused = true;
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AuegamePlayerController::HandlePauseOrCancel)
		.bExecuteWhenPaused = true;
	InputComponent->BindKey(EKeys::R, IE_Pressed, this, &AuegamePlayerController::HandleRestart)
		.bExecuteWhenPaused = true;
	InputComponent->BindKey(EKeys::Q, IE_Pressed, this, &AuegamePlayerController::HandleQuit)
		.bExecuteWhenPaused = true;
}

bool AuegamePlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}

FString AuegamePlayerController::GetConfirmationTitle() const
{
	switch (ProductModal)
	{
	case EUegameProductModal::Restart: return TEXT("RESTART THIS RUN?");
	case EUegameProductModal::Quit: return TEXT("QUIT TO DESKTOP?");
	case EUegameProductModal::None:
	default: return FString();
	}
}

FString AuegamePlayerController::GetConfirmationAction() const
{
	switch (ProductModal)
	{
	case EUegameProductModal::Restart: return TEXT("ENTER / R  RESTART");
	case EUegameProductModal::Quit: return TEXT("ENTER / Q  QUIT");
	case EUegameProductModal::None:
	default: return FString();
	}
}

void AuegamePlayerController::HandleAccept()
{
	if (IsConfirmationVisible())
	{
		ConfirmCurrentModal();
		return;
	}
	if (bWelcomeVisible)
	{
		bWelcomeVisible = false;
		if (APawn* PlayerPawn = GetPawn())
		{
			if (UPresentationFeedbackComponent* Presentation =
				PlayerPawn->FindComponentByClass<UPresentationFeedbackComponent>())
			{
				Presentation->EmitFloorStarted(1, false);
			}
		}
		UE_LOG(Loguegame, Display, TEXT("[ProductShell] welcome dismissed"));
		return;
	}
}

void AuegamePlayerController::HandlePauseOrCancel()
{
	if (IsConfirmationVisible())
	{
		CancelConfirmation();
		return;
	}
	if (bWelcomeVisible)
	{
		return;
	}
	if (UUegameFloorManager* FM = UUegameFloorManager::Get(GetWorld()))
	{
		FM->TogglePause();
	}
}

void AuegamePlayerController::HandleRestart()
{
	if (bWelcomeVisible)
	{
		return;
	}
	if (ProductModal == EUegameProductModal::Restart)
	{
		ConfirmCurrentModal();
		return;
	}
	if (UUegameFloorManager* FM = UUegameFloorManager::Get(GetWorld()))
	{
		const m8authority::RunState State = FM->GetRunState();
		if (State == m8authority::RunState::Won
			|| State == m8authority::RunState::Failed
			|| State == m8authority::RunState::Error)
		{
			FM->RequestManualRestart();
			return;
		}
	}
	OpenConfirmation(EUegameProductModal::Restart);
}

void AuegamePlayerController::HandleQuit()
{
	if (ProductModal == EUegameProductModal::Quit)
	{
		ConfirmCurrentModal();
		return;
	}
	OpenConfirmation(EUegameProductModal::Quit);
}

void AuegamePlayerController::OpenConfirmation(EUegameProductModal Modal)
{
	if (Modal == EUegameProductModal::None)
	{
		return;
	}
	const bool bSwitchingConfirmation = IsConfirmationVisible();
	ProductModal = Modal;
	if (!bSwitchingConfirmation)
	{
		bPausedForConfirmation = false;
		if (UUegameFloorManager* FM = UUegameFloorManager::Get(GetWorld());
			FM && FM->GetRunState() == m8authority::RunState::Playing)
		{
			FM->TogglePause();
			bPausedForConfirmation = FM->GetRunState() == m8authority::RunState::Paused;
		}
	}
	UE_LOG(Loguegame, Display, TEXT("[ProductShell] confirmation opened action=%s pausedForModal=%s"),
		Modal == EUegameProductModal::Quit ? TEXT("quit") : TEXT("restart"),
		bPausedForConfirmation ? TEXT("true") : TEXT("false"));
}

void AuegamePlayerController::CancelConfirmation()
{
	ProductModal = EUegameProductModal::None;
	if (bPausedForConfirmation)
	{
		if (UUegameFloorManager* FM = UUegameFloorManager::Get(GetWorld());
			FM && FM->GetRunState() == m8authority::RunState::Paused)
		{
			FM->TogglePause();
		}
	}
	bPausedForConfirmation = false;
	UE_LOG(Loguegame, Display, TEXT("[ProductShell] confirmation cancelled"));
}

void AuegamePlayerController::ConfirmCurrentModal()
{
	const EUegameProductModal Confirmed = ProductModal;
	ProductModal = EUegameProductModal::None;
	bPausedForConfirmation = false;
	if (UUegameFloorManager* FM = UUegameFloorManager::Get(GetWorld()))
	{
		if (Confirmed == EUegameProductModal::Restart)
		{
			FM->RequestManualRestart();
		}
		else if (Confirmed == EUegameProductModal::Quit)
		{
			FM->RequestQuit();
		}
	}
}
