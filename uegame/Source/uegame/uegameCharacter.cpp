// Copyright Epic Games, Inc. All Rights Reserved.

#include "uegameCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Combat/CombatComponent.h"
#include "Combat/CombatConfig.h"
#include "Combat/FloorManager.h"
#include "Combat/HealthComponent.h"
#include "Combat/LoadoutComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "uegame.h"

AuegameCharacter::AuegameCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
		
	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	GetCharacterMovement()->JumpZVelocity = 500.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character)
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)

	// M3 combat: shared health + the single melee attack (numbers from the DataTable row).
	Health = CreateDefaultSubobject<UHealthComponent>(TEXT("Health"));
	Combat = CreateDefaultSubobject<UCombatComponent>(TEXT("Combat"));

	// M5 build-diversity: the loadout state holder (reward-on-clear); base stats built in BeginPlay.
	Loadout = CreateDefaultSubobject<ULoadoutComponent>(TEXT("Loadout"));
}

void AuegameCharacter::BeginPlay()
{
	Super::BeginPlay();

	const FCombatConfigRow& Cfg = FUegameCombatConfig::Get();
	Health->Init(Cfg.PlayerMaxHP);
	Health->OnDeath.AddDynamic(this, &AuegameCharacter::HandlePlayerDeath);
	Health->OnDamaged.AddDynamic(this, &AuegameCharacter::HandlePlayerDamaged);

	// M5: build the loadout base stats from the same config row (Health is already Init'd, so the base
	// resolve here deliberately does not re-touch HP).
	if (Loadout)
	{
		Loadout->InitBaseFromConfig();
	}
}

void AuegameCharacter::DoChooseLoadout(int32 Index)
{
	// Route through the FloorManager: it applies the pick on the loadout component AND re-pokes the stairs
	// so a player already standing on the pad descends once the reward is taken (single choice code path
	// shared by Dungeon.ChooseLoadout and keys 1/2/3). No-op outside an active run.
	if (UUegameFloorManager* FM = UUegameFloorManager::Get(GetWorld()))
	{
		FM->TryChooseLoadout(Index);
	}
}

void AuegameCharacter::HandlePlayerDamaged(float /*Amount*/, AActor* /*DamageInstigator*/)
{
	// Brief red screen pulse via a camera fade (0.5 -> 0 alpha over 0.25s). No UMG asset; the
	// [Feedback] anchor makes it grep-testable that the pulse fired on the contact-damage event.
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->StartCameraFade(0.5f, 0.0f, 0.25f, FLinearColor::Red,
				/*bShouldFadeAudio=*/false, /*bHoldWhenFinished=*/false);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[Feedback] playerPulse hp=%.0f"), Health ? Health->GetHP() : -1.0f);
}

void AuegameCharacter::DoAttack()
{
	if (Combat)
	{
		Combat->TryAttack();
	}
}

void AuegameCharacter::HandlePlayerDeath(AActor* /*DeadActor*/)
{
	// M4: during an active run the FloorManager owns the fail state - [RunFailed] +
	// in-place restart from floor 1 with a fresh chained seed. The pawn is revived,
	// never destroyed, and the world is never reloaded (keeps the navmesh alive).
	if (UUegameFloorManager* FM = UUegameFloorManager::Get(GetWorld()); FM && FM->IsRunActive())
	{
		UE_LOG(LogTemp, Display, TEXT("[PlayerDeath] player HP reached 0 during a run"));
		FM->NotifyRunFailed();
		return;
	}

	// Single-floor mode (no run): original M3 fail state - death log + level restart.
	UE_LOG(LogTemp, Display, TEXT("[PlayerDeath] player HP reached 0 - restarting level in 1s"));
	GetWorldTimerManager().SetTimer(RestartTimerHandle,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)));
		}),
		1.0f, false);
}

void AuegameCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AuegameCharacter::Move);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AuegameCharacter::Look);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AuegameCharacter::Look);

		// M3 melee attack: legacy key bind (F) - coexists with EnhancedInput; the
		// Dungeon.Attack evidence verb drives the same DoAttack path.
		PlayerInputComponent->BindKey(EKeys::F, IE_Pressed, this, &AuegameCharacter::DoAttack);

		// M5 loadout choice: legacy number keys 1/2/3 pick offer option 0/1/2. Console verb
		// Dungeon.ChooseLoadout is the mandated forensic interface; these are the playable path.
		PlayerInputComponent->BindKey(EKeys::One,   IE_Pressed, this, &AuegameCharacter::ChooseLoadoutKey0);
		PlayerInputComponent->BindKey(EKeys::Two,   IE_Pressed, this, &AuegameCharacter::ChooseLoadoutKey1);
		PlayerInputComponent->BindKey(EKeys::Three, IE_Pressed, this, &AuegameCharacter::ChooseLoadoutKey2);
	}
	else
	{
		UE_LOG(Loguegame, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AuegameCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	// route the input
	DoMove(MovementVector.X, MovementVector.Y);
}

void AuegameCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// route the input
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void AuegameCharacter::DoMove(float Right, float Forward)
{
	if (GetController() != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		// get right vector 
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement 
		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection, Right);
	}
}

void AuegameCharacter::DoLook(float Yaw, float Pitch)
{
	if (GetController() != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AuegameCharacter::DoJumpStart()
{
	// signal the character to jump
	Jump();
}

void AuegameCharacter::DoJumpEnd()
{
	// signal the character to stop jumping
	StopJumping();
}
