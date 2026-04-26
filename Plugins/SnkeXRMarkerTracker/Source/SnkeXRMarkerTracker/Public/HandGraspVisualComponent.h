#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HandGraspVisualComponent.generated.h"

class UMaterialInstanceDynamic;
class UMeshComponent;
class UGraspGrabComponent;

/**
 * Drop this on a hand actor (same actor that has a UGraspGrabComponent and a
 * rendered hand mesh, e.g. BP_RiggedHandRenderModel).
 *
 * It does NOT change the hand's material -- it wraps whatever material is
 * already assigned in a UMaterialInstanceDynamic and drives three parameters
 * on it every tick:
 *
 *   - GraspValue  (scalar, 0..1) -- 0 hand open, 1 fist
 *   - ColorA      (vector)       -- color when grasp = 0
 *   - ColorB      (vector)       -- color when grasp = 1
 *
 * Wire these into your existing hand material with a simple Lerp(ColorA, ColorB, GraspValue)
 * and plug the output into BaseColor / Emissive / whatever you like.
 */
UCLASS(ClassGroup = (CobraSDK), meta = (BlueprintSpawnableComponent, DisplayName = "Hand Grasp Visual Component"))
class SNKEXRMARKERTRACKER_API UHandGraspVisualComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHandGraspVisualComponent();

	/**
	 * Mesh to drive. If null, the first UMeshComponent found on this actor
	 * (or any ChildActor) is used.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Visual")
	TObjectPtr<UMeshComponent> TargetMesh = nullptr;

	/** Material slot on the mesh to wrap as a dynamic instance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Visual", meta = (ClampMin = "0"))
	int32 MaterialSlot = 0;

	/**
	 * Grasp source. If null, the first UGraspGrabComponent on this actor is used.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Visual")
	TObjectPtr<UGraspGrabComponent> GraspSource = nullptr;

	/** Scalar parameter name on the material that receives grasp strength (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Visual|Params")
	FName GraspParamName = TEXT("GraspValue");

	/** Vector parameter name for the "open hand" color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Visual|Params")
	FName ColorAParamName = TEXT("ColorA");

	/** Vector parameter name for the "closed hand" color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Visual|Params")
	FName ColorBParamName = TEXT("ColorB");

	/** Color when the hand is fully open (grasp = 0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Visual|Params", meta = (HideAlphaChannel = "false"))
	FLinearColor ColorA = FLinearColor(0.10f, 0.20f, 1.00f, 1.0f);

	/** Color when the hand is fully closed (grasp = 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Visual|Params", meta = (HideAlphaChannel = "false"))
	FLinearColor ColorB = FLinearColor(1.00f, 0.15f, 0.10f, 1.0f);

	/**
	 * Exponential smoothing factor in [0,1] applied to the grasp value each tick.
	 * 1 = no smoothing, 0 = frozen. Lower values reduce jitter but add lag.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Visual|Params", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SmoothingAlpha = 1.0f;

	/** Optional manual override. If >= 0, used instead of the grasp source. Useful for testing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Visual|Debug")
	float ManualGraspOverride = -1.0f;

	UFUNCTION(BlueprintCallable, Category = "Hand Visual")
	void SetColorA(FLinearColor NewColor);

	UFUNCTION(BlueprintCallable, Category = "Hand Visual")
	void SetColorB(FLinearColor NewColor);

	/** Force-refresh the MID (e.g. after the hand mesh is swapped at runtime). */
	UFUNCTION(BlueprintCallable, Category = "Hand Visual")
	void RebuildMaterialInstance();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynamicMID = nullptr;

	float SmoothedGrasp = 0.0f;

	void ResolveReferences();
	void PushStaticParams();
};
