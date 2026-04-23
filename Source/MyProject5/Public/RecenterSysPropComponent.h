#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RecenterSysPropComponent.generated.h"

/**
 * Polls an Android system property on a fixed interval. When the value
 * transitions from "0" (or unset) to "1", the HMD orientation and position
 * are recentered via UHeadMountedDisplayFunctionLibrary::ResetOrientationAndPosition.
 *
 * On dev machine:
 *   adb shell setprop debug.myapp.recenter 1   // trigger recenter
 *   adb shell setprop debug.myapp.recenter 0   // arm for next trigger
 *
 * On non-Android platforms this component is a no-op.
 */
UCLASS(ClassGroup = (XR), meta = (BlueprintSpawnableComponent), DisplayName = "Recenter SysProp Component")
class MYPROJECT5_API URecenterSysPropComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URecenterSysPropComponent();

	/** System property name to poll. Default: debug.myapp.recenter */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recenter")
	FString SysPropName = TEXT("debug.myapp.recenter");

	/** Seconds between polls. Avoid polling faster than necessary. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recenter", meta = (ClampMin = "0.1"))
	float PollIntervalSeconds = 0.5f;

	/** Yaw offset (degrees) applied when recentering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recenter")
	float RecenterYawOffset = 0.0f;

	/** If true, also reset position; if false, only orientation (yaw). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recenter")
	bool bResetPositionToo = true;

	/** Manually trigger a recenter from Blueprint or code. */
	UFUNCTION(BlueprintCallable, Category = "Recenter")
	void TriggerRecenterNow();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	float TimeSinceLastPoll = 0.0f;
	bool  bLastValueWasOne = false;

	/** Reads the sysprop; returns empty string if unavailable / not set. */
	static FString ReadSysProp(const FString& Name);
};
