// DungeonEnemy.cpp - see header.

#include "DungeonEnemy.h"

#include "AIController.h"
#include "CombatConfig.h"
#include "EncounterConfig.h"
#include "HealthComponent.h"
#include "../DungeonSpawner.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/HitResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Navigation/PathFollowingComponent.h"   // full EPathFollowingRequestResult (AIController.h only forward-declares it)
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Enemy body colors for the Run 2.5 hit flash (drive BasicShapeMaterial's "Color" param).
	const FLinearColor kEnemyBaseColor(0.35f, 0.04f, 0.04f);   // dark crimson: reads as an enemy
	const FLinearColor kEnemyFlashColor(1.0f, 1.0f, 1.0f);     // white pop on taking damage
	constexpr float kHitFlashSeconds = 0.12f;
}

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
	// Colorable base material so BeginPlay can spin up a dynamic instance and drive "Color"
	// for the hit flash. BasicShapeMaterial exposes a "Color" vector parameter.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMat(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (ShapeMat.Succeeded())
	{
		BodyMesh->SetMaterial(0, ShapeMat.Object);
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
	// M7A.2: explicit reset - identity is "unassigned" until ApplyArchetype runs, so the
	// static-spawner / encounter-unavailable path can never inherit a stale id.
	ArchetypeTypeId = INDEX_NONE;
	ContactDamage = Row.EnemyContactDamage;
	DamageInterval = Row.EnemyDamageInterval;
	AggroRange = Row.AggroRange;
	LeashRange = Row.LeashRange;
	GetCharacterMovement()->MaxWalkSpeed = Row.EnemyMoveSpeed;
	Health->Init(Row.EnemyMaxHP);
	// Contact reach = my capsule + a typical player capsule (42) + slack.
	ContactRange = GetCapsuleComponent()->GetScaledCapsuleRadius() + 42.0f + 40.0f;
}

void ADungeonEnemy::ApplyArchetype(const FEncounterArchetypeStats& Stats, float InHpMult, int32 InTypeId)
{
	// M7A.2: the numeric id IS the identity; the display/log name below is derived from it.
	ArchetypeTypeId = InTypeId;

	// Stat-only overlay of the 6 fields InitEnemy set from the Default row. The archetype's
	// base HP re-applies the M4 per-floor multiplier so floor scaling semantics are preserved
	// (Grunt: arch == Default, so hp == the pre-M6 EffHP exactly).
	const float EffHP = Stats.MaxHP * InHpMult;
	ContactDamage = Stats.ContactDamage;
	DamageInterval = Stats.DamageInterval;
	AggroRange = Stats.AggroRange;
	LeashRange = Stats.LeashRange;
	GetCharacterMovement()->MaxWalkSpeed = Stats.MoveSpeed;
	Health->Init(EffHP);

	// Visual-only size cue: scale the BodyMesh relative to its constructor baseline and drop
	// its bottom back onto the capsule bottom (base mesh half-height == capsule half-height
	// == 88, so offset = 88*(s-1)). The capsule, nav agent, and ContactRange are deliberately
	// NOT touched - collision and contact behaviour must not vary by archetype.
	const float S = Stats.VisualScale;
	if (BodyMesh && !FMath::IsNearlyEqual(S, 1.0f))
	{
		BodyMesh->SetRelativeScale3D(FVector(0.68f * S, 0.68f * S, 1.76f * S));
		BodyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 88.0f * (S - 1.0f)));
	}

	// Evidence anchor: resolved stats + the proof that scale stayed visual-only (capsule
	// radius and ContactRange on the same line, unchanged across archetypes). The name is
	// derived from the id (TypeName is bounds-safe: out-of-range prints "?", matching the
	// old null-name fallback), so this line stays byte-identical to the pre-M7A.2 format.
	UE_LOG(LogTemp, Display,
		TEXT("[EncounterApply] room=%d type=%s hp=%.0f (arch=%.0f x mult=%.2f) speed=%.0f dmg=%.0f interval=%.2f aggro=%.0f leash=%.0f meshScale=%.2f capsuleR=%.0f contactRange=%.0f"),
		RoomIndex, FUegameEncounterConfig::TypeName(InTypeId), EffHP, Stats.MaxHP, InHpMult,
		Stats.MoveSpeed, Stats.ContactDamage, Stats.DamageInterval, Stats.AggroRange, Stats.LeashRange,
		S, GetCapsuleComponent()->GetScaledCapsuleRadius(), ContactRange);
}

const TCHAR* ADungeonEnemy::GetArchetypeDisplayName() const
{
	// Owner ruling (M7A.2): unassigned reads as neutral "Enemy" - never "Grunt" (a lie about
	// the no-assignment path) and not "Default" (implementation vocabulary, not player-facing).
	return HasArchetypeAssignment()
		? FUegameEncounterConfig::TypeName(ArchetypeTypeId)
		: TEXT("Enemy");
}

bool ADungeonEnemy::ComputeLOSTo(const AActor* Target) const
{
	if (!Target)
	{
		return false;
	}
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	// Strictly-horizontal ray at capsule-center height (~z=100). Every dungeon ISM (floor,
	// corridor, door, wall) is BlockAll => WorldStatic, but floor/corridor/door slabs are
	// FloorScale=0.1 (top ~z+5), so a same-Z ray clears them and only tall walls (z-span
	// [0,200]) occlude. End.Z is pinned to Start.Z so this never becomes a foot-to-head
	// diagonal that would clip a slab. Pawns (player, other enemies) are not WorldStatic and
	// are ignored by the object-type query, so they never block the sightline.
	const FVector Start = GetActorLocation();
	FVector End = Target->GetActorLocation();
	End.Z = Start.Z;
	FCollisionQueryParams Params(FName(TEXT("EnemyLOS")), /*bTraceComplex=*/false, this);
	FHitResult Hit;
	const bool bBlocked = World->LineTraceSingleByObjectType(
		Hit, Start, End, FCollisionObjectQueryParams(ECC_WorldStatic), Params);
	return !bBlocked;
}

void ADungeonEnemy::BeginPlay()
{
	Super::BeginPlay();
	Health->OnDeath.AddDynamic(this, &ADungeonEnemy::HandleDeath);
	Health->OnDamaged.AddDynamic(this, &ADungeonEnemy::HandleDamaged);

	// Dynamic material instance for the hit flash; start at the resting enemy color.
	BodyMID = BodyMesh->CreateDynamicMaterialInstance(0);
	if (BodyMID)
	{
		BodyMID->SetVectorParameterValue(TEXT("Color"), kEnemyBaseColor);
	}

	GetWorldTimerManager().SetTimer(PursueTimer, this, &ADungeonEnemy::PursueTick, 0.5f, true, 0.5f);
}

void ADungeonEnemy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(PursueTimer);
	GetWorldTimerManager().ClearTimer(FlashTimer);
	Super::EndPlay(EndPlayReason);
}

void ADungeonEnemy::PursueTick()
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player)
	{
		return;
	}

	AAIController* AI = Cast<AAIController>(GetController());
	const float Dist = FVector::Dist2D(GetActorLocation(), Player->GetActorLocation());

	// --- Run 2.5 perception state machine: Idle <-> Chasing ---
	if (!bChasing)
	{
		// Acquire only within AggroRange AND with line-of-sight. LOS is required to START a
		// chase (kills global aggro and through-wall acquisition). Otherwise stay Idle: no
		// wander, no repath, no contact damage.
		if (Dist <= AggroRange && ComputeLOSTo(Player))
		{
			bChasing = true;
			UE_LOG(LogTemp, Display,
				TEXT("[Aggro] room=%d acquire dist=%.0f los=yes aggroRange=%.0f"),
				RoomIndex, Dist, AggroRange);
		}
		else
		{
			return;   // Idle: stand in place.
		}
	}
	else if (Dist > LeashRange)
	{
		// De-aggro past the leash. LOS is deliberately NOT re-checked while chasing, so
		// rounding a corner keeps the target (the "memory") instead of flickering; the enemy
		// only forgets once the player is physically beyond LeashRange (> AggroRange = hysteresis).
		bChasing = false;
		UE_LOG(LogTemp, Display,
			TEXT("[Aggro] room=%d release dist=%.0f leashRange=%.0f"),
			RoomIndex, Dist, LeashRange);
		if (AI)
		{
			AI->StopMovement();
		}
		return;
	}

	// --- Chasing: navmesh pursuit (repath every timer tick) ---
	if (AI)
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
				Dist);
		}
	}

	// Contact damage with per-target interval — only a Chasing enemy in reach deals it.
	if (Dist <= ContactRange)
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

void ADungeonEnemy::HandleDamaged(float /*Amount*/, AActor* /*DamageInstigator*/)
{
	// Hit flash: white pop for ~0.12s, then back to the resting color. Grep-testable evidence
	// that the flash fired on the damage event (plan-audit #3), independent of the screenshot.
	if (BodyMID)
	{
		BodyMID->SetVectorParameterValue(TEXT("Color"), kEnemyFlashColor);
		GetWorldTimerManager().SetTimer(FlashTimer, this, &ADungeonEnemy::ClearFlash, kHitFlashSeconds, false);
	}
	UE_LOG(LogTemp, Display, TEXT("[Feedback] hitFlash room=%d hp=%.0f"),
		RoomIndex, Health ? Health->GetHP() : -1.0f);
}

void ADungeonEnemy::ClearFlash()
{
	if (BodyMID)
	{
		BodyMID->SetVectorParameterValue(TEXT("Color"), kEnemyBaseColor);
	}
}

bool ADungeonEnemy::IsActiveThreat() const
{
	return !bNeutralizedForFloorExit
		&& !IsActorBeingDestroyed()
		&& Health
		&& !Health->IsDead();
}

bool ADungeonEnemy::NeutralizeForFloorExit(bool& bOutDestroyQueued)
{
	bOutDestroyQueued = false;
	if (!IsActiveThreat())
	{
		return false;
	}

	// Destroy is deferred. Remove every gameplay effect synchronously so a completed
	// floor cannot receive one final movement/contact-damage tick on the exit pad.
	bNeutralizedForFloorExit = true;
	ContactDamage = 0.0f;
	bChasing = false;
	SetCanBeDamaged(false);
	GetWorldTimerManager().ClearTimer(PursueTimer);
	GetWorldTimerManager().ClearTimer(FlashTimer);
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		AI->StopMovement();
	}
	SetActorEnableCollision(false);
	SetActorTickEnabled(false);
	SetActorHiddenInGame(true);
	bOutDestroyQueued = Destroy();
	return true;
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
