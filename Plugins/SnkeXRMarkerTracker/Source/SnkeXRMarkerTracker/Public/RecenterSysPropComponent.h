#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RecenterSysPropComponent.generated.h"

/**
 * Polls an Android system property on a fixed interval (TickComponent, throttled).
 * When attached to your pawn, this runs with the pawn's tick.
 *
 * adb shell setprop debug.myapp.recenter 1   // trigger recenter (app resets prop to 0 on Android when possible)
 *
 * Lives in SnkeXRMarkerTracker (Runtime plugin) so it appears in Add Component like USnkeXRMarkerTrackerComponent.
 */
UCLASS(ClassGroup = (CobraSDK), meta = (BlueprintSpawnableComponent))
class SNKEXRMARKERTRACKER_API URecenterSysPropComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URecenterSysPropComponent();

	/** System property name to poll. Default: debug.myapp.recenter */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recenter")
	FString SysPropName = TEXT("debug.myapp.recenter");

	/** Seconds between polls. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recenter", meta = (ClampMin = "0.1"))
	float PollIntervalSeconds = 0.5f;

	/** Yaw offset (degrees) applied when recentering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recenter")
	float RecenterYawOffset = 0.0f;

	/** If true, also reset position; if false, only orientation (yaw). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recenter")
	bool bResetPositionToo = true;

	/**
	 * If true, switch HMD tracking origin to Local (eye-level — "Eye" in UE < 5.4) on BeginPlay.
	 * With LocalFloor/Stage origins (OpenXR default on standalone), ResetOrientationAndPosition
	 * only resets yaw + XY — the blue axis (Z / height) stays anchored to the real-world floor.
	 * Local/eye-level tracking anchors the reference space to the HMD pose itself so Z resets too.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recenter|Height")
	bool bUseEyeLevelTracking = true;

	/**
	 * Belt-and-suspenders fallback for runtimes that ignore Z on reset.
	 * After calling ResetOrientationAndPosition, shift the owning pawn's world Z so the
	 * camera's current world Z equals the pawn's world Z (camera becomes the new origin in Z).
	 * Safe to combine with bUseEyeLevelTracking; if Eye tracking already zeroed Z, this is a no-op.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recenter|Height")
	bool bAlsoSnapPawnToHeadHeight = true;

	/** If true (default), after recenter the app writes the sysprop back to "0" so you only need adb setprop ... 1 each time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recenter")
	bool bAutoResetSysPropToZeroAfterTrigger = true;

	UFUNCTION(BlueprintCallable, Category = "Recenter")
	void TriggerRecenterNow();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	float TimeSinceLastPoll = 0.0f;
	bool bLastValueWasOne = false;

	static FString ReadSysProp(const FString& Name);
	static bool WriteSysProp(const FString& Name, const char* Utf8Value);
};
