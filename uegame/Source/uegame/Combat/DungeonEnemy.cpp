// DungeonEnemy.cpp - see header.

#include "DungeonEnemy.h"

#include "AIController.h"
#include "CombatConfig.h"
#include "HealthComponent.h"
#include "../DungeonSpawner.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Navigation/PathFollowingComponent.h"   // full EPathFollowingRequestResult (AIController.h only forward-declares it)
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

ADungeonEnemy::ADungeonEnemy()
{
	PrimaryActorTick.bCanEverTick = false;

	GetCapsuleComponent()->InitCapsuleSize(34.0f, 88.0f);

	// Placeholder visual: engine sphere squashed into a capsule-ish blob. No collision.
	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(GetCapsuleComponent());
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded())
	{
		BodyMesh->SetStaticMesh(Sphere.Object);
		BodyMesh->SetRelativeScale3D(FVector(0.68f, 0.68f, 1.76f));
	}

	Health = CreateDefaultSubobject<UHealthComponent>(TEXT("Health"));

	// Navmesh-driven pursuit needs an AI controller for runtime-spawned pawns.
	AIControllerClass = AAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);
}

void ADungeonEnemy::InitEnemy(const FCombatConfigRow& Row, int32 InRoomIndex, ADungeonSpawner* InSpawner)
{
	RoomIndex = InRoomIndex;
	SpawnerRef = InSpawner;
	ContactDamage = Row.EnemyContactDamage;
	DamageInterval = Row.EnemyDamageInterval;
	GetCharacterMovement()->MaxWalkSpeed = Row.EnemyMoveSpeed;
	Health->Init(Row.EnemyMaxHP);
	// Contact reach = my capsule + a typical player capsule (42) + slack.
	ContactRange = GetCapsuleComponent()->GetScaledCapsuleRadius() + 42.0f + 40.0f;
}

void ADungeonEnemy::BeginPlay()
{
	Super::BeginPlay();
	Health->OnDeath.AddDynamic(this, &ADungeonEnemy::HandleDeath);
	GetWorldTimerManager().SetTimer(PursueTimer, this, &ADungeonEnemy::PursueTick, 0.5f, true, 0.5f);
}

void ADungeonEnemy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(PursueTimer);
	Super::EndPlay(EndPlayReason);
}

void ADungeonEnemy::PursueTick()
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player)
	{
		return;
	}

	// Navmesh pursuit (repath every tick of this timer).
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		const EPathFollowingRequestResult::Type Res = AI->MoveToActor(Player, 60.0f);
		if (!bLoggedFirstMove)
		{
			bLoggedFirstMove = true;
			// Evidence (acceptance C): navmesh path request outcome for at least one enemy.
			UE_LOG(LogTemp, Display,
				TEXT("[Enemy] room=%d first MoveToActor result=%s dist=%.0f"),
				RoomIndex,
				Res == EPathFollowingRequestResult::RequestSuccessful ? TEXT("RequestSuccessful") :
				Res == EPathFollowingRequestResult::AlreadyAtGoal ? TEXT("AlreadyAtGoal") : TEXT("Failed"),
				FVector::Dist2D(GetActorLocation(), Player->GetActorLocation()));
		}
	}

	// Contact damage with per-target interval (single target in M3: the player).
	if (FVector::Dist2D(GetActorLocation(), Player->GetActorLocation()) <= ContactRange)
	{
		UWorld* World = GetWorld();
		const double Now = World ? World->GetTimeSeconds() : 0.0;
		if (Now - LastContactDamageTime >= DamageInterval)
		{
			LastContactDamageTime = Now;
			if (UHealthComponent* PlayerHP = Player->FindComponentByClass<UHealthComponent>())
			{
				PlayerHP->TakeDamage(ContactDamage, this);
			}
		}
	}
}

void ADungeonEnemy::HandleDeath(AActor* /*DeadActor*/)
{
	UE_LOG(LogTemp, Display, TEXT("[Enemy] died room=%d"), RoomIndex);
	if (ADungeonSpawner* Spawner = SpawnerRef.Get())
	{
		Spawner->NotifyEnemyDead(RoomIndex);
	}
	Destroy();
}
