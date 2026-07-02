// DungeonStairs.cpp - see header.

#include "DungeonStairs.h"

#include "FloorManager.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ConstructorHelpers.h"

ADungeonStairs::ADungeonStairs()
{
	PrimaryActorTick.bCanEverTick = false;

	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	RootComponent = Trigger;
	Trigger->SetBoxExtent(FVector(110.0f, 110.0f, 100.0f));
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionObjectType(ECC_WorldDynamic);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	// Placeholder visual: a flat pad (engine cube squashed), no collision of its own.
	PadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PadMesh"));
	PadMesh->SetupAttachment(Trigger);
	PadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		PadMesh->SetStaticMesh(Cube.Object);
		PadMesh->SetRelativeScale3D(FVector(1.8f, 1.8f, 0.25f));
		PadMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -85.0f));
	}
}

void ADungeonStairs::BeginPlay()
{
	Super::BeginPlay();
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ADungeonStairs::OnTriggerBegin);
}

void ADungeonStairs::OnTriggerBegin(UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool /*bFromSweep*/,
	const FHitResult& /*SweepResult*/)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC || OtherActor != PC->GetPawn())
	{
		return;   // only the player descends
	}
	UE_LOG(LogTemp, Display, TEXT("[Stairs] player stepped on the stairs"));
	if (UUegameFloorManager* FM = UUegameFloorManager::Get(World))
	{
		FM->RequestDescend(/*bForce=*/false);
	}
}
