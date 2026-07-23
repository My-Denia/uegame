// DungeonStairs.cpp - see header.

#include "DungeonStairs.h"

#include "FloorManager.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
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
	ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (Cube.Succeeded())
	{
		PadMesh->SetStaticMesh(Cube.Object);
		PadMesh->SetRelativeScale3D(FVector(1.8f, 1.8f, 0.25f));
		PadMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -85.0f));
	}
	if (ShapeMaterial.Succeeded())
	{
		PadMesh->SetMaterial(0, ShapeMaterial.Object);
	}

	BeaconMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BeaconMesh"));
	BeaconMesh->SetupAttachment(Trigger);
	BeaconMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BeaconMesh->SetRelativeScale3D(FVector(0.42f, 0.42f, 1.35f));
	BeaconMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 18.0f));
	if (Cylinder.Succeeded())
	{
		BeaconMesh->SetStaticMesh(Cylinder.Object);
	}
	if (ShapeMaterial.Succeeded())
	{
		BeaconMesh->SetMaterial(0, ShapeMaterial.Object);
	}

	BeaconLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("BeaconLight"));
	BeaconLight->SetupAttachment(Trigger);
	BeaconLight->SetRelativeLocation(FVector(0.0f, 0.0f, 80.0f));
	BeaconLight->SetAttenuationRadius(650.0f);
	BeaconLight->SetIntensity(1800.0f);
	BeaconLight->SetCastShadows(false);

	ExitLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("ExitLabel"));
	ExitLabel->SetupAttachment(Trigger);
	ExitLabel->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
	ExitLabel->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
	ExitLabel->SetWorldSize(34.0f);
	ExitLabel->SetRelativeLocation(FVector(0.0f, 0.0f, 180.0f));
	ExitLabel->SetText(FText::FromString(TEXT("EXIT SEALED")));
}

void ADungeonStairs::BeginPlay()
{
	Super::BeginPlay();
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ADungeonStairs::OnTriggerBegin);
	Trigger->OnComponentEndOverlap.AddDynamic(this, &ADungeonStairs::OnTriggerEnd);
	ExitMaterial = PadMesh ? PadMesh->CreateDynamicMaterialInstance(0) : nullptr;
	if (BeaconMesh && ExitMaterial)
	{
		BeaconMesh->SetMaterial(0, ExitMaterial);
	}
	SetExitReadyVisual(false);
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
	bPawnInside = true;
	UE_LOG(LogTemp, Display, TEXT("[Stairs] player stepped on the stairs"));
	RequestDescendNow();
}

void ADungeonStairs::OnTriggerEnd(UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (PC && OtherActor == PC->GetPawn())
	{
		bPawnInside = false;
	}
}

void ADungeonStairs::OnFloorCleared()
{
	SetExitReadyVisual(true);
	if (!bPawnInside)
	{
		return;   // player not on the pad; the normal overlap handles it when they arrive
	}
	// require-floor-clear: the overlap already fired and was gated; the gate is now open, so
	// descend without making the player step off and back on.
	UE_LOG(LogTemp, Display, TEXT("[Stairs] floor cleared while player on pad - retrying descend"));
	RequestDescendNow();
}

void ADungeonStairs::SetExitReadyVisual(bool bReady)
{
	const FLinearColor Color = bReady
		? FLinearColor(1.0f, 0.52f, 0.08f)
		: FLinearColor(0.08f, 0.38f, 0.48f);
	if (ExitMaterial)
	{
		ExitMaterial->SetVectorParameterValue(TEXT("Color"), Color);
	}
	if (BeaconLight)
	{
		BeaconLight->SetLightColor(Color);
		BeaconLight->SetIntensity(bReady ? 4200.0f : 1300.0f);
	}
	if (ExitLabel)
	{
		ExitLabel->SetText(FText::FromString(bReady ? TEXT("EXIT READY") : TEXT("EXIT SEALED")));
		ExitLabel->SetTextRenderColor(bReady ? FColor(255, 178, 55) : FColor(70, 205, 230));
	}
}

void ADungeonStairs::RequestDescendNow()
{
	if (UUegameFloorManager* FM = UUegameFloorManager::Get(GetWorld()))
	{
		FM->RequestDescend(/*bForce=*/false);   // gate policy enforced in the FloorManager
	}
}
