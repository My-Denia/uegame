// DungeonEnemy.cpp - see header.

#include "DungeonEnemy.h"

#include "AIController.h"
#include "CombatConfig.h"
#include "EncounterConfig.h"
#include "HealthComponent.h"
#include "../DungeonSpawner.h"
#include "../Presentation/PresentationFeedbackComponent.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/HitResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Navigation/PathFollowingComponent.h"   // full EPathFollowingRequestResult (AIController.h only forward-declares it)
#include "NavigationSystem.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#include "m8_room_contract.hpp"
#include "m8_enemy_behavior.hpp"
#include "m8_finale.hpp"

namespace
{
	const FLinearColor kEnemyBaseColor(0.35f, 0.04f, 0.04f);
	const FLinearColor kGruntBaseColor(0.68f, 0.08f, 0.035f);
	const FLinearColor kRunnerBaseColor(0.035f, 0.42f, 0.78f);
	const FLinearColor kBruteBaseColor(0.46f, 0.07f, 0.56f);
	const FLinearColor kEnemyFlashColor(1.0f, 1.0f, 1.0f);
	const FLinearColor kWindupColor(1.0f, 0.48f, 0.02f);
	const FLinearColor kRunnerCircleColor(0.05f, 0.55f, 0.95f);
	const FLinearColor kRunnerDashColor(0.85f, 0.08f, 0.85f);
	const FLinearColor kRecoveryColor(0.22f, 0.22f, 0.26f);
	const FLinearColor kWardenGuardColor(0.12f, 0.30f, 0.85f);
	const FLinearColor kWardenBrokenColor(1.0f, 0.78f, 0.05f);
	const FLinearColor kWardenExposedColor(0.72f, 0.02f, 0.02f);
	constexpr float kHitFlashSeconds = 0.12f;
	constexpr float kBehaviorTickSeconds = 0.05f;
	constexpr float kBehaviorPulseSeconds = 0.18f;
	constexpr float kRunnerDashSpeedMultiplier = 2.0f;

	m8enemy::Archetype BehaviorArchetype(int32 TypeId)
	{
		return m8enemy::clamp_archetype(TypeId);
	}

	EUegameWardenPhase ToUeWardenPhase(m8finale::Phase Phase)
	{
		switch (Phase)
		{
		case m8finale::Phase::Guarded: return EUegameWardenPhase::Guarded;
		case m8finale::Phase::Staggered: return EUegameWardenPhase::Staggered;
		case m8finale::Phase::Exposed: return EUegameWardenPhase::Exposed;
		case m8finale::Phase::Dead: return EUegameWardenPhase::Dead;
		case m8finale::Phase::Inactive:
		default: return EUegameWardenPhase::Inactive;
		}
	}

	const TCHAR* BehaviorLabel(m8enemy::Phase Phase, m8enemy::Archetype /*Archetype*/)
	{
		switch (Phase)
		{
		case m8enemy::Phase::Dormant: return TEXT("");
		case m8enemy::Phase::Approach: return TEXT("");
		case m8enemy::Phase::GruntWindup: return TEXT("GRUNT STRIKE 0.45");
		case m8enemy::Phase::RunnerCircle: return TEXT("RUNNER CIRCLE");
		case m8enemy::Phase::RunnerTell: return TEXT("RUNNER DASH 0.35");
		case m8enemy::Phase::RunnerDash: return TEXT("RUNNER DASH");
		case m8enemy::Phase::RunnerDisengage: return TEXT("RUNNER DISENGAGE");
		case m8enemy::Phase::BruteTell: return TEXT("BRUTE SLAM 0.90");
		case m8enemy::Phase::Recovery: return TEXT("RECOVERY");
		case m8enemy::Phase::Dead:
		default: return TEXT("");
		}
	}

	FLinearColor BehaviorColor(m8enemy::Phase Phase)
	{
		switch (Phase)
		{
		case m8enemy::Phase::GruntWindup:
		case m8enemy::Phase::RunnerTell:
		case m8enemy::Phase::BruteTell: return kWindupColor;
		case m8enemy::Phase::RunnerCircle: return kRunnerCircleColor;
		case m8enemy::Phase::RunnerDash: return kRunnerDashColor;
		case m8enemy::Phase::RunnerDisengage:
		case m8enemy::Phase::Recovery: return kRecoveryColor;
		default: return kEnemyBaseColor;
		}
	}

	FLinearColor ArchetypeRestingColor(int32 ArchetypeTypeId)
	{
		switch (ArchetypeTypeId)
		{
		case 1: return kRunnerBaseColor;
		case 2: return kBruteBaseColor;
		case 0:
		default: return kGruntBaseColor;
		}
	}
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
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	GruntVisualMesh = Cube.Succeeded() ? Cube.Object : (Sphere.Succeeded() ? Sphere.Object : nullptr);
	RunnerVisualMesh = Cone.Succeeded() ? Cone.Object : (Sphere.Succeeded() ? Sphere.Object : nullptr);
	BruteVisualMesh = Sphere.Succeeded() ? Sphere.Object : nullptr;
	WardenVisualMesh = Cylinder.Succeeded() ? Cylinder.Object : (Sphere.Succeeded() ? Sphere.Object : nullptr);
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

	WardenAuraMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WardenAuraMesh"));
	WardenAuraMesh->SetupAttachment(GetCapsuleComponent());
	WardenAuraMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WardenAuraMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -86.0f));
	WardenAuraMesh->SetRelativeScale3D(FVector(1.8f, 1.8f, 0.035f));
	WardenAuraMesh->SetVisibility(false);
	if (Cylinder.Succeeded())
	{
		WardenAuraMesh->SetStaticMesh(Cylinder.Object);
	}
	if (ShapeMat.Succeeded())
	{
		WardenAuraMesh->SetMaterial(0, ShapeMat.Object);
	}

	WardenLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("WardenLight"));
	WardenLight->SetupAttachment(GetCapsuleComponent());
	WardenLight->SetRelativeLocation(FVector(0.0f, 0.0f, 90.0f));
	WardenLight->SetAttenuationRadius(750.0f);
	WardenLight->SetIntensity(0.0f);
	WardenLight->SetCastShadows(false);

	BehaviorText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("BehaviorText"));
	BehaviorText->SetupAttachment(GetCapsuleComponent());
	BehaviorText->SetRelativeLocation(FVector(0.0f, 0.0f, 140.0f));
	BehaviorText->SetWorldSize(24.0f);
	BehaviorText->SetTextRenderColor(FColor::White);
	BehaviorText->SetCollisionEnabled(ECollisionEnabled::NoCollision);

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
	ResetWardenState();
	RoomIndex = InRoomIndex;
	SpawnOrdinal = INDEX_NONE;
	SpawnerRef = InSpawner;
	// M7A.2: explicit reset - identity is "unassigned" until ApplyArchetype runs, so the
	// static-spawner / encounter-unavailable path can never inherit a stale id.
	ArchetypeTypeId = INDEX_NONE;
	bRoomChallengeModified = false;
	PreChallengeContactDamage = 0.0f;
	PreChallengeBaseMoveSpeed = 0.0f;
	PreChallengeDurationNumerator = 1;
	PreChallengeDurationDenominator = 1;
	BehaviorDurationNumerator = 1;
	BehaviorDurationDenominator = 1;
	AssignedContactDamage = Row.EnemyContactDamage;
	ContactDamage = Row.EnemyContactDamage;
	DamageInterval = Row.EnemyDamageInterval;
	AggroRange = Row.AggroRange;
	LeashRange = Row.LeashRange;
	BaseMoveSpeed = Row.EnemyMoveSpeed;
	GetCharacterMovement()->MaxWalkSpeed = BaseMoveSpeed;
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
	AssignedContactDamage = Stats.ContactDamage;
	ContactDamage = static_cast<float>(m8enemy::scale_contact_damage(
		FMath::RoundToInt(Stats.ContactDamage)));
	DamageInterval = Stats.DamageInterval;
	AggroRange = Stats.AggroRange;
	LeashRange = Stats.LeashRange;
	BaseMoveSpeed = Stats.MoveSpeed;
	GetCharacterMovement()->MaxWalkSpeed = BaseMoveSpeed;
	Health->Init(EffHP);

	// Visual-only size cue: scale the BodyMesh relative to its constructor baseline and drop
	// its bottom back onto the capsule bottom (base mesh half-height == capsule half-height
	// == 88, so offset = 88*(s-1)). The capsule, nav agent, and ContactRange are deliberately
	// NOT touched - collision and contact behaviour must not vary by archetype.
	const float S = Stats.VisualScale;
	if (BodyMesh)
	{
		UStaticMesh* VisualMesh = GruntVisualMesh;
		FVector BaseScale(0.72f, 0.72f, 1.76f);
		if (InTypeId == 1)
		{
			VisualMesh = RunnerVisualMesh;
			BaseScale = FVector(0.58f, 0.58f, 1.76f);
		}
		else if (InTypeId == 2)
		{
			VisualMesh = BruteVisualMesh;
			BaseScale = FVector(0.78f, 0.78f, 1.76f);
		}
		if (VisualMesh)
		{
			BodyMesh->SetStaticMesh(VisualMesh);
		}
		BodyMesh->SetRelativeScale3D(BaseScale * S);
		BodyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 88.0f * (S - 1.0f)));
	}

	// Evidence anchor: resolved stats + the proof that scale stayed visual-only (capsule
	// radius and ContactRange on the same line, unchanged across archetypes). The name is
	// derived from the id (TypeName is bounds-safe: out-of-range prints "?", matching the
	// old null-name fallback), so this line stays byte-identical to the pre-M7A.2 format.
	UE_LOG(LogTemp, Display,
		TEXT("[EncounterApply] room=%d type=%s hp=%.0f (arch=%.0f x mult=%.2f) speed=%.0f dmg=%.0f interval=%.2f aggro=%.0f leash=%.0f meshScale=%.2f capsuleR=%.0f contactRange=%.0f m8CommittedDmg=%.0f"),
		RoomIndex, FUegameEncounterConfig::TypeName(InTypeId), EffHP, Stats.MaxHP, InHpMult,
		Stats.MoveSpeed, Stats.ContactDamage, Stats.DamageInterval, Stats.AggroRange, Stats.LeashRange,
		S, GetCapsuleComponent()->GetScaledCapsuleRadius(), ContactRange, ContactDamage);
}

bool ADungeonEnemy::ConfigureAsWarden(const FCombatConfigRow& Row, int32 ResolveTokens)
{
	if (bWarden || bRoomChallengeModified || !Health || Health->IsDead()
		|| !GetCharacterMovement() || !BodyMesh || !FUegameCombatConfig::IsFromDataTable())
	{
		return false;
	}
	m8finale::Profile Profile;
	Profile.base_guard = Row.WardenBaseGuard;
	Profile.guard_reduction_per_resolve = Row.WardenGuardReductionPerResolve;
	Profile.minimum_guard = Row.WardenMinimumGuard;
	Profile.stagger_ms = FMath::RoundToInt64(Row.WardenStaggerSeconds * 1000.0f);
	Profile.stagger_damage_pct = FMath::RoundToInt(Row.WardenStaggerDamageMultiplier * 100.0f);
	m8finale::State NewState;
	if (!m8finale::configure(NewState, ResolveTokens, Profile)
		|| !FMath::IsFinite(Row.WardenMaxHP) || Row.WardenMaxHP <= 0.0f
		|| !FMath::IsFinite(Row.WardenMoveSpeed) || Row.WardenMoveSpeed <= 0.0f
		|| !FMath::IsFinite(Row.WardenContactDamage) || Row.WardenContactDamage <= 0.0f
		|| !FMath::IsFinite(Row.WardenDamageInterval) || Row.WardenDamageInterval <= 0.0f
		|| !FMath::IsFinite(Row.WardenAggroRange) || Row.WardenAggroRange <= 0.0f
		|| !FMath::IsFinite(Row.WardenLeashRange) || Row.WardenLeashRange <= Row.WardenAggroRange
		|| !FMath::IsFinite(Row.WardenMeshScale)
		|| Row.WardenMeshScale < 1.0f || Row.WardenMeshScale > 3.0f)
	{
		return false;
	}

	const float OldMaxHP = Health->GetMaxHP();
	const float OldHP = Health->GetHP();
	const float OldAssignedDamage = AssignedContactDamage;
	const float OldContactDamage = ContactDamage;
	const float OldDamageInterval = DamageInterval;
	const float OldAggroRange = AggroRange;
	const float OldLeashRange = LeashRange;
	const float OldBaseMoveSpeed = BaseMoveSpeed;
	const FVector OldMeshScale = BodyMesh->GetRelativeScale3D();
	const FVector OldMeshLocation = BodyMesh->GetRelativeLocation();
	UStaticMesh* OldVisualMesh = BodyMesh->GetStaticMesh();

	AssignedContactDamage = Row.WardenContactDamage;
	ContactDamage = static_cast<float>(m8enemy::scale_contact_damage(
		FMath::RoundToInt(Row.WardenContactDamage)));
	DamageInterval = Row.WardenDamageInterval;
	AggroRange = Row.WardenAggroRange;
	LeashRange = Row.WardenLeashRange;
	BaseMoveSpeed = Row.WardenMoveSpeed;
	GetCharacterMovement()->MaxWalkSpeed = BaseMoveSpeed;
	Health->Init(Row.WardenMaxHP);
	if (WardenVisualMesh)
	{
		BodyMesh->SetStaticMesh(WardenVisualMesh);
	}
	BodyMesh->SetRelativeScale3D(FVector(
		0.82f * Row.WardenMeshScale,
		0.82f * Row.WardenMeshScale,
		1.76f * Row.WardenMeshScale));
	BodyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 88.0f * (Row.WardenMeshScale - 1.0f)));

	bool bForcedFailure = false;
#if !UE_BUILD_SHIPPING
	bForcedFailure = WardenConfigFaultModeForTests == 1;
	WardenConfigFaultModeForTests = 0;
#endif
	if (bForcedFailure)
	{
		AssignedContactDamage = OldAssignedDamage;
		ContactDamage = OldContactDamage;
		DamageInterval = OldDamageInterval;
		AggroRange = OldAggroRange;
		LeashRange = OldLeashRange;
		BaseMoveSpeed = OldBaseMoveSpeed;
		GetCharacterMovement()->MaxWalkSpeed = BaseMoveSpeed;
		Health->Init(OldMaxHP);
		Health->SetHP(OldHP);
		BodyMesh->SetRelativeScale3D(OldMeshScale);
		BodyMesh->SetRelativeLocation(OldMeshLocation);
		BodyMesh->SetStaticMesh(OldVisualMesh);
		ResetWardenState();
		UE_LOG(LogTemp, Warning,
			TEXT("[WardenInit] rollback=true room=%d ordinal=%d sourceType=%d"),
			RoomIndex, SpawnOrdinal, ArchetypeTypeId);
		return false;
	}

	bWarden = true;
	WardenAuraMesh->SetVisibility(true);
	WardenMaxGuard = NewState.max_guard;
	WardenGuard = NewState.guard;
	WardenGuardBrokenAtMs = NewState.guard_broken_at_ms;
	WardenStaggerMs = NewState.stagger_ms;
	WardenStaggerDamagePct = NewState.stagger_damage_pct;
	bWardenExposureInitialized = false;
	bPendingCommittedHit = false;
	PendingCommittedHitRange = 0.0f;
	bChasing = false;
	LastContactDamageTime = -1000.0;
	ResetBehaviorState(false);
	ApplyBehaviorPresentation();
	UE_LOG(LogTemp, Display,
		TEXT("[WardenInit] committed=true room=%d ordinal=%d sourceType=%d resolve=%d hp=%.0f speed=%.0f assignedDamage=%.0f committedDamage=%.0f interval=%.2f aggro=%.0f leash=%.0f meshScale=%.2f guard=%d staggerMs=%lld damagePct=%d"),
		RoomIndex, SpawnOrdinal, ArchetypeTypeId, FMath::Clamp(ResolveTokens, 0, 2),
		Health->GetMaxHP(), BaseMoveSpeed, AssignedContactDamage, ContactDamage,
		DamageInterval, AggroRange, LeashRange, Row.WardenMeshScale,
		WardenGuard, static_cast<long long>(WardenStaggerMs), WardenStaggerDamagePct);
	return true;
}

const TCHAR* ADungeonEnemy::GetArchetypeDisplayName() const
{
	if (bWarden)
	{
		return TEXT("WARDEN");
	}
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
	WardenAuraMID = WardenAuraMesh->CreateDynamicMaterialInstance(0);

	ResetBehaviorState();
	GetWorldTimerManager().SetTimer(PursueTimer, this, &ADungeonEnemy::PursueTick, 0.5f, true, 0.5f);
	GetWorldTimerManager().SetTimer(
		BehaviorTimer, this, &ADungeonEnemy::BehaviorTick,
		kBehaviorTickSeconds, true, kBehaviorTickSeconds);
}

void ADungeonEnemy::ResetWardenState()
{
	bWarden = false;
	if (WardenAuraMesh)
	{
		WardenAuraMesh->SetVisibility(false);
	}
	if (WardenLight)
	{
		WardenLight->SetIntensity(0.0f);
	}
	WardenMaxGuard = 0;
	WardenGuard = 0;
	WardenGuardBrokenAtMs = -1;
	WardenStaggerMs = 0;
	WardenStaggerDamagePct = 100;
	bWardenExposureInitialized = false;
#if !UE_BUILD_SHIPPING
	WardenConfigFaultModeForTests = 0;
#endif
}

EUegameWardenPhase ADungeonEnemy::GetWardenPhase() const
{
	if (!bWarden)
	{
		return EUegameWardenPhase::Inactive;
	}
	m8finale::State State;
	State.configured = true;
	State.max_guard = WardenMaxGuard;
	State.guard = WardenGuard;
	State.guard_broken_at_ms = WardenGuardBrokenAtMs;
	State.stagger_ms = WardenStaggerMs;
	State.stagger_damage_pct = WardenStaggerDamagePct;
	const int32 CurrentHP = Health ? FMath::RoundToInt(Health->GetHP()) : 0;
	return ToUeWardenPhase(m8finale::phase(State, CurrentHP, GetBehaviorNowMs()));
}

int32 ADungeonEnemy::GetPlayerDamageMultiplierPercent() const
{
	return GetWardenPhase() == EUegameWardenPhase::Staggered
		? WardenStaggerDamagePct : 100;
}

void ADungeonEnemy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UE_LOG(LogTemp, Display,
		TEXT("[EnemyBehavior] room=%d ordinal=%d endPlay pendingCancelled=%s reason=%d"),
		RoomIndex, SpawnOrdinal, bPendingCommittedHit ? TEXT("true") : TEXT("false"),
		static_cast<int32>(EndPlayReason));
	bPendingCommittedHit = false;
	GetWorldTimerManager().ClearTimer(PursueTimer);
	GetWorldTimerManager().ClearTimer(BehaviorTimer);
	GetWorldTimerManager().ClearTimer(BehaviorPulseTimer);
	GetWorldTimerManager().ClearTimer(FlashTimer);
	Super::EndPlay(EndPlayReason);
}

int64 ADungeonEnemy::GetBehaviorNowMs() const
{
	const UWorld* World = GetWorld();
	return World ? FMath::RoundToInt64(World->GetTimeSeconds() * 1000.0) : 0;
}

void ADungeonEnemy::ResetBehaviorState(bool bDead)
{
	m8enemy::State State;
	m8enemy::initialize(State, BehaviorArchetype(bWarden ? 2 : ArchetypeTypeId),
		static_cast<uint32>(FMath::Max(0, SpawnOrdinal)), GetBehaviorNowMs());
	if (bDead)
	{
		m8enemy::reset(State, GetBehaviorNowMs(), true);
	}
	BehaviorPhase = static_cast<int32>(State.phase);
	BehaviorPhaseStartedMs = State.phase_started_ms;
	BehaviorCircleDirection = State.circle_direction;
	BehaviorDashTarget = FVector::ZeroVector;
	bBehaviorDashTargetValid = false;
	BehaviorCommitSerial = 0;
	bPendingCommittedHit = false;
	PendingCommittedHitRange = 0.0f;
	if (GetCharacterMovement())
	{
		GetCharacterMovement()->MaxWalkSpeed = BaseMoveSpeed;
	}
	ApplyBehaviorPresentation();
}

void ADungeonEnemy::ApplyBehaviorPresentation()
{
	const m8enemy::Phase Phase = static_cast<m8enemy::Phase>(BehaviorPhase);
	FLinearColor RestingColor = BehaviorColor(Phase);
	if (!bWarden && (Phase == m8enemy::Phase::Dormant || Phase == m8enemy::Phase::Approach))
	{
		RestingColor = ArchetypeRestingColor(ArchetypeTypeId);
	}
	if (BehaviorText)
	{
		FString Label(BehaviorLabel(Phase, BehaviorArchetype(bWarden ? 2 : ArchetypeTypeId)));
		if (bWarden)
		{
			switch (GetWardenPhase())
			{
			case EUegameWardenPhase::Guarded:
				Label = Label.IsEmpty()
					? FString::Printf(TEXT("WARDEN GUARD %d"), WardenGuard)
					: FString::Printf(TEXT("WARDEN GUARD %d | SLAM"), WardenGuard);
				RestingColor = kWardenGuardColor;
				break;
			case EUegameWardenPhase::Staggered:
				Label = TEXT("WARDEN BROKEN - STRIKE NOW");
				RestingColor = kWardenBrokenColor;
				break;
			case EUegameWardenPhase::Exposed:
				Label = Label.IsEmpty() ? TEXT("WARDEN EXPOSED") : TEXT("WARDEN SLAM 0.90");
				RestingColor = kWardenExposedColor;
				break;
			case EUegameWardenPhase::Dead:
				Label.Reset();
				break;
			case EUegameWardenPhase::Inactive:
			default:
				break;
			}
		}
		else if (bRoomChallengeModified && Phase != m8enemy::Phase::Dead)
		{
			Label = Label.IsEmpty() ? TEXT("CHALLENGE") : Label + TEXT(" | CHALLENGE");
		}
		BehaviorText->SetText(FText::FromString(Label));
		BehaviorText->SetTextRenderColor(RestingColor.ToFColor(true));
	}
	if (BodyMID)
	{
		BodyMID->SetVectorParameterValue(TEXT("Color"), RestingColor);
	}
	if (bWarden)
	{
		if (WardenAuraMID)
		{
			WardenAuraMID->SetVectorParameterValue(TEXT("Color"), RestingColor);
		}
		if (WardenLight)
		{
			WardenLight->SetLightColor(RestingColor);
			WardenLight->SetIntensity(
				GetWardenPhase() == EUegameWardenPhase::Staggered ? 5200.0f : 2800.0f);
		}
	}
}

void ADungeonEnemy::ClearBehaviorPulse()
{
	ApplyBehaviorPresentation();
}

void ADungeonEnemy::BehaviorTick()
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
#if !UE_BUILD_SHIPPING
	if (BehaviorFaultModeForTests == 5)
	{
		bPendingCommittedHit = false;
		return;
	}
#endif
	const UHealthComponent* PlayerHealth = Player
		? Player->FindComponentByClass<UHealthComponent>() : nullptr;
	if (!Player || !PlayerHealth || PlayerHealth->IsDead() || !IsActiveThreat())
	{
		bPendingCommittedHit = false;
		return;
	}
	if (bWarden)
	{
		const EUegameWardenPhase WardenPhase = GetWardenPhase();
		if (WardenPhase == EUegameWardenPhase::Staggered)
		{
			bPendingCommittedHit = false;
			PendingCommittedHitRange = 0.0f;
			if (AAIController* AI = Cast<AAIController>(GetController()))
			{
				AI->StopMovement();
			}
			GetCharacterMovement()->MaxWalkSpeed = 0.0f;
			ApplyBehaviorPresentation();
			return;
		}
		if (WardenPhase == EUegameWardenPhase::Exposed && !bWardenExposureInitialized)
		{
			bWardenExposureInitialized = true;
			ResetBehaviorState(false);
			UE_LOG(LogTemp, Display,
				TEXT("[WardenPhase] room=%d ordinal=%d phase=Exposed behavior=BruteApproach"),
				RoomIndex, SpawnOrdinal);
		}
	}

	m8enemy::State State;
	State.archetype = BehaviorArchetype(bWarden ? 2 : ArchetypeTypeId);
	State.phase = static_cast<m8enemy::Phase>(BehaviorPhase);
	State.phase_started_ms = BehaviorPhaseStartedMs;
	State.spawn_ordinal = static_cast<uint32>(FMath::Max(0, SpawnOrdinal));
	State.circle_direction = BehaviorCircleDirection;
	State.dash_target = { BehaviorDashTarget.X, BehaviorDashTarget.Y };
	State.dash_target_valid = bBehaviorDashTargetValid;
	State.commit_serial = BehaviorCommitSerial;

	const FVector SelfLocation = GetActorLocation();
	const FVector PlayerLocation = Player->GetActorLocation();
	const float Dist = FVector::Dist2D(SelfLocation, PlayerLocation);
	m8enemy::Input In;
	In.now_ms = GetBehaviorNowMs();
	In.alive = Health && !Health->IsDead();
	In.active = bChasing;
	In.within_leash = Dist <= LeashRange;
	In.has_los = ComputeLOSTo(Player);
	In.in_attack_range = Dist <= ContactRange;
	In.in_danger_radius = Dist <= static_cast<float>(m8enemy::kBruteDangerRadius);
	In.self = { SelfLocation.X, SelfLocation.Y };
	In.target = { PlayerLocation.X, PlayerLocation.Y };
	In.dash_complete = bBehaviorDashTargetValid
		&& FVector::Dist2D(SelfLocation, BehaviorDashTarget) <= 60.0f;
	In.duration_numerator = BehaviorDurationNumerator;
	In.duration_denominator = BehaviorDurationDenominator;
#if !UE_BUILD_SHIPPING
	if (BehaviorFaultModeForTests == 1)
	{
		In.in_attack_range = false;
		In.in_danger_radius = false;
	}
	else if (BehaviorFaultModeForTests == 2)
	{
		In.has_los = false;
	}
	else if (BehaviorFaultModeForTests == 4)
	{
		In.within_leash = false;
	}
#endif

	FVector ProjectedDashTarget = PlayerLocation;
	bool bProjectedDashTarget = true;
	if (State.phase == m8enemy::Phase::RunnerTell)
	{
		const m8enemy::Vec2 Candidate = m8enemy::cap_dash_target(In.self, In.target);
		const FVector Candidate3D(Candidate.x, Candidate.y, SelfLocation.Z);
		FNavLocation Projected;
		UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld());
		bProjectedDashTarget = Nav && Nav->ProjectPointToNavigation(Candidate3D, Projected);
		if (bProjectedDashTarget)
		{
			ProjectedDashTarget = Projected.Location;
		}
		In.nav_valid = bProjectedDashTarget;
	}
	else if (State.phase == m8enemy::Phase::RunnerDash)
	{
		In.nav_valid = bBehaviorDashTargetValid;
	}
#if !UE_BUILD_SHIPPING
	if (BehaviorFaultModeForTests == 3)
	{
		In.nav_valid = false;
	}
#endif

	const m8enemy::Output Out = m8enemy::update(State, In);
	if (Out.dash_started && bProjectedDashTarget)
	{
		State.dash_target = { ProjectedDashTarget.X, ProjectedDashTarget.Y };
		State.dash_target_valid = true;
	}

	BehaviorPhase = static_cast<int32>(State.phase);
	BehaviorPhaseStartedMs = State.phase_started_ms;
	BehaviorCircleDirection = State.circle_direction;
	BehaviorDashTarget = FVector(
		static_cast<float>(State.dash_target.x),
		static_cast<float>(State.dash_target.y), SelfLocation.Z);
	bBehaviorDashTargetValid = State.dash_target_valid;
	BehaviorCommitSerial = State.commit_serial;

	if (Out.reset)
	{
		bPendingCommittedHit = false;
		PendingCommittedHitRange = 0.0f;
	}
	if (Out.phase_changed)
	{
		ApplyBehaviorPresentation();
		UE_LOG(LogTemp, Display, TEXT("[EnemyBehavior] room=%d ordinal=%d type=%s phase=%s"),
			RoomIndex, SpawnOrdinal, GetArchetypeDisplayName(),
			ANSI_TO_TCHAR(m8enemy::phase_name(State.phase)));
	}
	if (Out.strike_committed)
	{
		bPendingCommittedHit = Out.request_hit;
		PendingCommittedHitRange = State.archetype == m8enemy::Archetype::Brute
			? static_cast<float>(m8enemy::kBruteDangerRadius) : ContactRange;
		if (BehaviorText)
		{
			BehaviorText->SetText(FText::FromString(Out.request_hit ? TEXT("STRIKE!") : TEXT("MISS")));
			BehaviorText->SetTextRenderColor(FColor::White);
		}
		if (BodyMID)
		{
			BodyMID->SetVectorParameterValue(TEXT("Color"), kEnemyFlashColor);
		}
		GetWorldTimerManager().SetTimer(
			BehaviorPulseTimer, this, &ADungeonEnemy::ClearBehaviorPulse,
			kBehaviorPulseSeconds, false);
		UE_LOG(LogTemp, Display,
			TEXT("[EnemyBehavior] room=%d ordinal=%d type=%s commit=%u hitIntent=%s"),
			RoomIndex, SpawnOrdinal, GetArchetypeDisplayName(), Out.commit_serial,
			Out.request_hit ? TEXT("yes") : TEXT("no"));
	}

	if (BehaviorText)
	{
		FVector ViewLocation = PlayerLocation;
		FRotator ViewRotation = FRotator::ZeroRotator;
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
		}
		FRotator Facing = (ViewLocation - BehaviorText->GetComponentLocation()).Rotation();
		Facing.Pitch = 0.0f;
		Facing.Roll = 0.0f;
		BehaviorText->SetWorldRotation(Facing);
	}
}

void ADungeonEnemy::DriveBehaviorMovement(AAIController* AI, APawn* Player)
{
	if (!AI || !Player || !GetCharacterMovement())
	{
		return;
	}
	const m8enemy::Phase Phase = static_cast<m8enemy::Phase>(BehaviorPhase);
	GetCharacterMovement()->MaxWalkSpeed = Phase == m8enemy::Phase::RunnerDash
		? BaseMoveSpeed * kRunnerDashSpeedMultiplier : BaseMoveSpeed;

	EPathFollowingRequestResult::Type Result = EPathFollowingRequestResult::Failed;
	bool bRequestedMove = false;
	switch (Phase)
	{
	case m8enemy::Phase::Approach:
		Result = AI->MoveToActor(Player, 60.0f);
		bRequestedMove = true;
		break;
	case m8enemy::Phase::RunnerCircle:
	{
		FVector Radial = GetActorLocation() - Player->GetActorLocation();
		Radial.Z = 0.0f;
		if (!Radial.Normalize()) { Radial = GetActorForwardVector(); }
		const FVector Target = Player->GetActorLocation()
			+ Radial.RotateAngleAxis(45.0f * static_cast<float>(BehaviorCircleDirection), FVector::UpVector)
			* static_cast<float>(m8enemy::kRunnerCircleRadius);
		Result = AI->MoveToLocation(Target, 50.0f);
		bRequestedMove = true;
		break;
	}
	case m8enemy::Phase::RunnerDash:
		if (bBehaviorDashTargetValid)
		{
			Result = AI->MoveToLocation(BehaviorDashTarget, 35.0f);
			bRequestedMove = true;
		}
		break;
	case m8enemy::Phase::RunnerDisengage:
	{
		FVector Away = GetActorLocation() - Player->GetActorLocation();
		Away.Z = 0.0f;
		if (!Away.Normalize()) { Away = -GetActorForwardVector(); }
		Result = AI->MoveToLocation(GetActorLocation()
			+ Away * static_cast<float>(m8enemy::kRunnerCircleRadius), 50.0f);
		bRequestedMove = true;
		break;
	}
	default:
		AI->StopMovement();
		break;
	}

	if (bRequestedMove && !bLoggedFirstMove)
	{
		bLoggedFirstMove = true;
		UE_LOG(LogTemp, Display, TEXT("[Enemy] room=%d first behavior move result=%s"), RoomIndex,
			Result == EPathFollowingRequestResult::RequestSuccessful ? TEXT("RequestSuccessful") :
			Result == EPathFollowingRequestResult::AlreadyAtGoal ? TEXT("AlreadyAtGoal") : TEXT("Failed"));
	}
}

void ADungeonEnemy::PursueTick()
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
#if !UE_BUILD_SHIPPING
	if (BehaviorFaultModeForTests == 5)
	{
		bPendingCommittedHit = false;
		return;
	}
#endif
	const UHealthComponent* PlayerHealth = Player
		? Player->FindComponentByClass<UHealthComponent>() : nullptr;
	if (!Player || !PlayerHealth || PlayerHealth->IsDead())
	{
		bPendingCommittedHit = false;
		if (AAIController* AI = Cast<AAIController>(GetController()))
		{
			AI->StopMovement();
		}
		return;
	}
	if (bWarden && GetWardenPhase() == EUegameWardenPhase::Staggered)
	{
		bPendingCommittedHit = false;
		PendingCommittedHitRange = 0.0f;
		if (AAIController* AI = Cast<AAIController>(GetController()))
		{
			AI->StopMovement();
		}
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
			AlertIdleRoomPeers(Player);
		}
		else
		{
			return;   // Idle: stand in place.
		}
	}
	else if (Dist > LeashRange
#if !UE_BUILD_SHIPPING
		|| BehaviorFaultModeForTests == 4
#endif
	)
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
		ResetBehaviorState();
		return;
	}

	// The phase core selects movement and creates one-shot attack commits. Proximity alone
	// can no longer damage the player.
	DriveBehaviorMovement(AI, Player);
	if (bPendingCommittedHit)
	{
		bPendingCommittedHit = false;
		bool bInCommittedRange = Dist <= PendingCommittedHitRange;
		bool bClearStrikeLOS = ComputeLOSTo(Player);
#if !UE_BUILD_SHIPPING
		if (BehaviorFaultModeForTests == 1) { bInCommittedRange = false; }
		if (BehaviorFaultModeForTests == 2) { bClearStrikeLOS = false; }
#endif
		UWorld* World = GetWorld();
		const double Now = World ? World->GetTimeSeconds() : 0.0;
		if (bInCommittedRange && bClearStrikeLOS && Now - LastContactDamageTime >= DamageInterval)
		{
			LastContactDamageTime = Now;
			if (UHealthComponent* PlayerHP = Player->FindComponentByClass<UHealthComponent>())
			{
				PlayerHP->TakeDamage(ContactDamage, this);
				UE_LOG(LogTemp, Display,
					TEXT("[EnemyBehavior] room=%d ordinal=%d commit=%u commitDamage=%.0f interval=%.2f"),
					RoomIndex, SpawnOrdinal, BehaviorCommitSerial, ContactDamage, DamageInterval);
			}
		}
	}
}

void ADungeonEnemy::AlertIdleRoomPeers(APawn* Player)
{
	UWorld* World = GetWorld();
	ADungeonSpawner* SourceSpawner = SpawnerRef.Get();
	if (!Player || !World || !SourceSpawner)
	{
		return;
	}
	for (TActorIterator<ADungeonEnemy> It(World); It; ++It)
	{
		ADungeonEnemy* Peer = *It;
		if (!IsValid(Peer) || Peer == this || Peer->IsActorBeingDestroyed()
			|| Peer->SpawnerRef.Get() != SourceSpawner || Peer->RoomIndex != RoomIndex
			|| Peer->bChasing || !Peer->Health || Peer->Health->IsDead())
		{
			continue;
		}
		const float PeerDist = FVector::Dist2D(Peer->GetActorLocation(), Player->GetActorLocation());
		if (PeerDist > Peer->LeashRange || !Peer->ComputeLOSTo(Player))
		{
			continue;
		}
		Peer->bChasing = true;
		UE_LOG(LogTemp, Display,
			TEXT("[Aggro] peer-alert sourceOrdinal=%d peerOrdinal=%d room=%d dist=%.0f los=yes"),
			SpawnOrdinal, Peer->SpawnOrdinal, RoomIndex, PeerDist);
	}
}

#if !UE_BUILD_SHIPPING
void ADungeonEnemy::RunBehaviorContractProbeForTests(int32 Mode)
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	UHealthComponent* PlayerHealth = Player
		? Player->FindComponentByClass<UHealthComponent>() : nullptr;
	if (!Player || !PlayerHealth || !IsActiveThreat())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[EnemyBehaviorTest] mode=%d ready=false room=%d ordinal=%d"),
			Mode, RoomIndex, SpawnOrdinal);
		return;
	}

	BehaviorFaultModeForTests = FMath::Clamp(Mode, 0, 8);
	bChasing = true;
	LastContactDamageTime = -1000.0;
	bPendingCommittedHit = false;
	PendingCommittedHitRange = 0.0f;
	// Pin the synthetic strike probes to an unobstructed, in-range position. The
	// external command first teleports the player to the room centre; moving the
	// selected enemy here removes live AI movement from this exact adapter test.
	// Modes 1 and 2 then invalidate range/LOS through the dedicated fault seam.
	if (Mode >= 0 && Mode <= 2)
	{
		const FVector ProbeLocation = Player->GetActorLocation() + FVector(80.0f, 0.0f, 0.0f);
		SetActorLocation(ProbeLocation, false, nullptr, ETeleportType::TeleportPhysics);
		if (AAIController* AI = Cast<AAIController>(GetController()))
		{
			AI->StopMovement();
		}
	}
	const int64 NowMs = GetBehaviorNowMs();
	const m8enemy::Archetype Archetype = BehaviorArchetype(bWarden ? 2 : ArchetypeTypeId);
	if (Mode == 7 || Mode == 8)
	{
		bPendingCommittedHit = true;
		PendingCommittedHitRange = ContactRange;
		UE_LOG(LogTemp, Display,
			TEXT("[EnemyBehaviorTest] mode=%d armed=true room=%d ordinal=%d pending=true"),
			Mode, RoomIndex, SpawnOrdinal);
		return;
	}
	if (Mode == 4)
	{
		BehaviorPhase = static_cast<int32>(m8enemy::Phase::Approach);
		bPendingCommittedHit = true;
		PendingCommittedHitRange = ContactRange;
	}
	else if (Mode == 5 || Mode == 6)
	{
		BehaviorPhase = static_cast<int32>(m8enemy::Phase::Approach);
		bPendingCommittedHit = true;
		PendingCommittedHitRange = ContactRange;
	}
	else if (Archetype == m8enemy::Archetype::Runner)
	{
		BehaviorPhase = static_cast<int32>(m8enemy::Phase::RunnerTell);
		BehaviorPhaseStartedMs = NowMs - m8enemy::kRunnerTellMs;
	}
	else if (Archetype == m8enemy::Archetype::Brute)
	{
		BehaviorPhase = static_cast<int32>(m8enemy::Phase::BruteTell);
		BehaviorPhaseStartedMs = NowMs - m8enemy::kBruteTellMs;
	}
	else
	{
		BehaviorPhase = static_cast<int32>(m8enemy::Phase::GruntWindup);
		BehaviorPhaseStartedMs = NowMs - m8enemy::kGruntWindupMs;
	}

	const float BeforeHP = PlayerHealth->GetHP();
	BehaviorTick();
	PursueTick();
	const float AfterHP = PlayerHealth->GetHP();
	const m8enemy::Phase Phase = static_cast<m8enemy::Phase>(BehaviorPhase);
	UE_LOG(LogTemp, Display,
		TEXT("[EnemyBehaviorTest] mode=%d ready=true room=%d ordinal=%d type=%s hpDelta=%.0f pending=%s phase=%s commit=%u"),
		Mode, RoomIndex, SpawnOrdinal, GetArchetypeDisplayName(), BeforeHP - AfterHP,
		bPendingCommittedHit ? TEXT("true") : TEXT("false"),
		ANSI_TO_TCHAR(m8enemy::phase_name(Phase)), BehaviorCommitSerial);
	BehaviorFaultModeForTests = 0;
}
#endif

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
	ApplyBehaviorPresentation();
}

bool ADungeonEnemy::IsActiveThreat() const
{
	return !bNeutralizedForFloorExit
		&& !IsActorBeingDestroyed()
		&& Health
		&& !Health->IsDead();
}

int32 ADungeonEnemy::GetCombatPoolCurrent() const
{
	if (bWarden && WardenGuard > 0)
	{
		return WardenGuard;
	}
	return Health ? FMath::Max(0, FMath::RoundToInt(Health->GetHP())) : 0;
}

int32 ADungeonEnemy::GetCombatPoolMax() const
{
	if (bWarden && WardenGuard > 0)
	{
		return FMath::Max(1, WardenMaxGuard);
	}
	return Health ? FMath::Max(1, FMath::RoundToInt(Health->GetMaxHP())) : 1;
}

int32 ADungeonEnemy::ApplyPlayerDamage(int32 Amount, AActor* DamageInstigator)
{
	if (!Health || Health->IsDead() || Amount <= 0)
	{
		return 0;
	}
	if (!bWarden)
	{
		const int32 Before = GetCombatPoolCurrent();
		Health->TakeDamage(static_cast<float>(Amount), DamageInstigator);
		return FMath::Max(0, Before - GetCombatPoolCurrent());
	}

	m8finale::State State;
	State.configured = true;
	State.max_guard = WardenMaxGuard;
	State.guard = WardenGuard;
	State.guard_broken_at_ms = WardenGuardBrokenAtMs;
	State.stagger_ms = WardenStaggerMs;
	State.stagger_damage_pct = WardenStaggerDamagePct;
	const int32 CurrentHP = FMath::Max(0, FMath::RoundToInt(Health->GetHP()));
	const m8finale::DamageResult Result = m8finale::apply_damage(
		State, CurrentHP, Amount, GetBehaviorNowMs());
	WardenGuard = State.guard;
	WardenGuardBrokenAtMs = State.guard_broken_at_ms;

	if (Result.guard_broken)
	{
		bWardenExposureInitialized = false;
		bPendingCommittedHit = false;
		PendingCommittedHitRange = 0.0f;
		if (AAIController* AI = Cast<AAIController>(GetController()))
		{
			AI->StopMovement();
		}
		GetCharacterMovement()->MaxWalkSpeed = 0.0f;
		ApplyBehaviorPresentation();
		if (APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			if (UPresentationFeedbackComponent* Presentation =
				Player->FindComponentByClass<UPresentationFeedbackComponent>())
			{
				Presentation->EmitWardenBroken();
			}
		}
		UE_LOG(LogTemp, Display,
			TEXT("[WardenPhase] room=%d ordinal=%d phase=Staggered guard=0 durationMs=%lld damagePct=%d"),
			RoomIndex, SpawnOrdinal, static_cast<long long>(WardenStaggerMs),
			WardenStaggerDamagePct);
	}
	if (Result.guard_damage > 0)
	{
		HandleDamaged(static_cast<float>(Result.guard_damage), DamageInstigator);
	}
	if (Result.hp_damage > 0)
	{
		Health->TakeDamage(static_cast<float>(Result.hp_damage), DamageInstigator);
	}
	UE_LOG(LogTemp, Display,
		TEXT("[WardenDamage] room=%d ordinal=%d raw=%d guardApplied=%d hpApplied=%d multiplierPct=%d guard=%d/%d hp=%.0f/%.0f phase=%d"),
		RoomIndex, SpawnOrdinal, Amount, Result.guard_damage, Result.hp_damage,
		Result.multiplier_pct, WardenGuard, WardenMaxGuard,
		Health->GetHP(), Health->GetMaxHP(), static_cast<int32>(GetWardenPhase()));
	return Result.guard_damage + Result.hp_damage;
}

bool ADungeonEnemy::CanApplyRoomChallengeModifier() const
{
	return IsActiveThreat() && !bWarden && !bRoomChallengeModified
		&& GetCharacterMovement() != nullptr;
}

bool ADungeonEnemy::ApplyRoomChallengeModifier()
{
	if (!CanApplyRoomChallengeModifier())
	{
		return false;
	}
	PreChallengeContactDamage = ContactDamage;
	PreChallengeBaseMoveSpeed = BaseMoveSpeed;
	PreChallengeDurationNumerator = BehaviorDurationNumerator;
	PreChallengeDurationDenominator = BehaviorDurationDenominator;
	ContactDamage = static_cast<float>(m8contract::challenge_damage(FMath::RoundToInt(ContactDamage)));
	BaseMoveSpeed = PreChallengeBaseMoveSpeed * 1.20f;
	BehaviorDurationNumerator = 5;
	BehaviorDurationDenominator = 6;
	GetCharacterMovement()->MaxWalkSpeed = BaseMoveSpeed;
	bRoomChallengeModified = true;
	ResetBehaviorState(false);
	UE_LOG(LogTemp, Display,
		TEXT("[RoomContractEnemy] room=%d ordinal=%d type=%s challenge=true damage=%.0f speed=%.1f phaseRatio=5/6"),
		RoomIndex, SpawnOrdinal, GetArchetypeDisplayName(), ContactDamage, BaseMoveSpeed);
	return true;
}

bool ADungeonEnemy::RollbackRoomChallengeModifier()
{
	if (!bRoomChallengeModified || !GetCharacterMovement())
	{
		return false;
	}
	ContactDamage = PreChallengeContactDamage;
	BaseMoveSpeed = PreChallengeBaseMoveSpeed;
	BehaviorDurationNumerator = PreChallengeDurationNumerator;
	BehaviorDurationDenominator = PreChallengeDurationDenominator;
	GetCharacterMovement()->MaxWalkSpeed = BaseMoveSpeed;
	bRoomChallengeModified = false;
	ResetBehaviorState(false);
	UE_LOG(LogTemp, Display,
		TEXT("[RoomContractEnemy] room=%d ordinal=%d type=%s challenge=false rollback=true damage=%.0f speed=%.1f phaseRatio=%d/%d"),
		RoomIndex, SpawnOrdinal, GetArchetypeDisplayName(), ContactDamage, BaseMoveSpeed,
		BehaviorDurationNumerator, BehaviorDurationDenominator);
	return true;
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
	bPendingCommittedHit = false;
	bChasing = false;
	SetCanBeDamaged(false);
	GetWorldTimerManager().ClearTimer(PursueTimer);
	GetWorldTimerManager().ClearTimer(BehaviorTimer);
	GetWorldTimerManager().ClearTimer(BehaviorPulseTimer);
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
	const bool bCancelledPendingCommit = bPendingCommittedHit;
	ResetBehaviorState(true);
	GetWorldTimerManager().ClearTimer(BehaviorTimer);
	GetWorldTimerManager().ClearTimer(BehaviorPulseTimer);
	UE_LOG(LogTemp, Display,
		TEXT("[EnemyBehavior] room=%d ordinal=%d death pendingCancelled=%s"),
		RoomIndex, SpawnOrdinal, bCancelledPendingCommit ? TEXT("true") : TEXT("false"));
	UE_LOG(LogTemp, Display, TEXT("[Enemy] died room=%d ordinal=%d warden=%s"),
		RoomIndex, SpawnOrdinal, bWarden ? TEXT("true") : TEXT("false"));
	if (ADungeonSpawner* Spawner = SpawnerRef.Get())
	{
		Spawner->NotifyEnemyDead(this);
	}
	Destroy();
}
