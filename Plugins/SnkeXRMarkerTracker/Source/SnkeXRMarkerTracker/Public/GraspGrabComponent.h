#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h" // EControllerHand
#include "GraspGrabComponent.generated.h"

struct FXRMotionControllerData;

/**
 * Drop this on a hand actor (e.g. BP_RiggedHandRenderModel) and set Hand = Left/Right.
 *
 * Each tick:
 *   - Reads OpenXR motion controller grasp state (bIsGrasped) for the selected hand.
 *   - On rising edge: finds the nearest actor with tag GrabTag within GrabRadius of the owner
 *     and attaches it (KeepWorldTransform — preserves the relative offset at grab moment).
 *   - On falling edge: detaches (KeepWorldTransform).
 *
 * The "Grabbable" actor must be Movable.
 */
UCLASS(ClassGroup = (CobraSDK), meta = (BlueprintSpawnableComponent, DisplayName = "Grasp Grab Component"))
class SNKEXRMARKERTRACKER_API UGraspGrabComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGraspGrabComponent();

	/** Which hand's grasp state drives this component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grab")
	EControllerHand Hand = EControllerHand::Left;

	/** Tag an actor must carry (via Actor Tags) to be considered grabbable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grab")
	FName GrabTag = TEXT("Grabbable");

	/** Search radius (cm) around the palm when looking for a Grabbable on grasp. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grab", meta = (ClampMin = "0.1"))
	float GrabRadius = 10.0f;

	/**
	 * Grasp strength (0..1) at/above which we consider the hand "closed".
	 * Computed from skeletal fingertip-to-palm distances (index/middle/ring/little).
	 * 0 = hand fully open, 1 = hand fully closed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grab", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GraspThreshold = 0.6f;

	/** Hysteresis — once grasping, release only when grasp strength falls below this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grab", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ReleaseThreshold = 0.4f;

	/** If true, logs verbose grab/release events. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grab")
	bool bVerboseLogging = false;

	/**
	 * If true, on BeginPlay, scan the level and promote every actor tagged GrabTag
	 * to Mobility = Movable. Saves you from setting Mobility per-actor in the editor.
	 * Idempotent — running on multiple hand components is fine.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grab|Auto-Setup")
	bool bAutoMakeGrabbablesMovable = true;

	/**
	 * If true, also promote each tagged actor's collision profile to "PhysicsActor"
	 * when its current preset is "NoCollision" or "Default". Required for the overlap
	 * query to find them. Won't override custom presets you've already set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grab|Auto-Setup")
	bool bAutoFixGrabbableCollision = true;

	/**
	 * If true, also call SetSimulatePhysics(true) on each tagged actor so they fall
	 * back to the floor/shelf when released. Requires the mesh to have simple
	 * collision primitives.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grab|Auto-Setup")
	bool bAutoEnablePhysicsOnGrabbables = false;

	/** If true, logs motion controller data (bValid / bIsGrasped / etc.) every DebugLogInterval seconds and prints on-screen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grab|Debug")
	bool bDebugLogGraspState = true;

	/** Seconds between debug heartbeat logs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grab|Debug", meta = (ClampMin = "0.05"))
	float DebugLogInterval = 1.0f;

	/** The actor currently held by this hand (nullptr if none). */
	UFUNCTION(BlueprintPure, Category = "Grab")
	AActor* GetHeldActor() const { return HeldActor.Get(); }

	/** Force-release whatever is held. */
	UFUNCTION(BlueprintCallable, Category = "Grab")
	void ReleaseHeldActor();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	bool bWasGrasped = false;
	float DebugLogAccumulator = 0.0f;

	/** Whether the held actor was simulating physics before we grabbed it. */
	bool bHeldWasSimulatingPhysics = false;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> HeldActor;

	AActor* FindNearestGrabbable(const FVector& Origin) const;
	void TryGrab(const FXRMotionControllerData& Data);
	void AutoSetupGrabbablesInWorld();
};
