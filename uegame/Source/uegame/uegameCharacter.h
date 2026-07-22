// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "uegameCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
class UHealthComponent;
class UCombatComponent;
class ULoadoutComponent;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/**
 *  A simple player-controllable third person character
 *  Implements a controllable orbiting camera
 */
UCLASS(abstract)
class AuegameCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Camera boom positioning the camera behind the character */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** Follow camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;
	
protected:

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MouseLookAction;

	// --- M3 combat (data-driven; numbers from Content/Data/CombatConfig.csv) ---

	/** Shared HP component (same class the enemy uses). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	UHealthComponent* Health;

	/** The single M3 melee attack (cooldown + range from the DataTable row). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	UCombatComponent* Combat;

	/** M5 build-diversity: the run's affix picks + resolved stats (reward earned on floor clear). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	ULoadoutComponent* Loadout;

public:

	/** Constructor */
	AuegameCharacter();	

protected:

	/** Initialize input action bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

protected:

	/** Called for movement input */
	void Move(const FInputActionValue& Value);

	/** Called for looking input */
	void Look(const FInputActionValue& Value);

public:

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles look inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoLook(float Yaw, float Pitch);

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

	/** M3: perform the melee attack (also driven by the Dungeon.Attack evidence verb). */
	UFUNCTION(BlueprintCallable, Category="Combat")
	virtual void DoAttack();

	/** M5: apply the player's loadout reward pick (0..2). Routed through the FloorManager so the choice
	 *  and the stairs re-poke live in one place; also driven by Dungeon.ChooseLoadout and keys 1/2/3. */
	UFUNCTION(BlueprintCallable, Category="Combat")
	virtual void DoChooseLoadout(int32 Index);

	/** Player-facing run controls. These remain active while the world is paused. */
	UFUNCTION(BlueprintCallable, Category="Run")
	virtual void DoTogglePause();
	UFUNCTION(BlueprintCallable, Category="Run")
	virtual void DoManualRestart();
	UFUNCTION(BlueprintCallable, Category="Run")
	virtual void DoQuit();

protected:

	/** Init HP from the combat DataTable row; bind death handling. */
	virtual void BeginPlay() override;

	/** M3 fail state: log + restart the level shortly after death. */
	UFUNCTION()
	void HandlePlayerDeath(AActor* DeadActor);

	/** Run 2.5 feedback: brief red screen pulse (camera fade) when the player takes damage. */
	UFUNCTION()
	void HandlePlayerDamaged(float Amount, AActor* DamageInstigator);

private:

	FTimerHandle RestartTimerHandle;

	// Legacy key forwarders (1/2/3) for the loadout offer - thin wrappers so UInputComponent::BindKey
	// (which binds a parameterless handler) can drive DoChooseLoadout(index). Console verb
	// Dungeon.ChooseLoadout is the mandated forensic interface; these are the playable convenience.
	void ChooseLoadoutKey0() { DoChooseLoadout(0); }
	void ChooseLoadoutKey1() { DoChooseLoadout(1); }
	void ChooseLoadoutKey2() { DoChooseLoadout(2); }

public:

	/** Returns CameraBoom subobject **/
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	/** Returns FollowCamera subobject **/
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }
};

