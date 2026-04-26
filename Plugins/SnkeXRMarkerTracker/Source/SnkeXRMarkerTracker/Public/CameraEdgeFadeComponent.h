#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "CameraEdgeFadeComponent.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;

/**
 * Screen-space edge fade mesh parented to a camera. Port of the Godot VignetteXR
 * setup used in our cobra-demo XR project: a plane pinned in front of the camera
 * driving an unlit translucent material that fades the outer pixels to FadeColor.
 *
 * Usage:
 *   1. Create a Material (see plugin README / notes below) with parameters:
 *        Scalar  "ClearRadius"      (0..1)
 *        Scalar  "FadePower"        (0.5..10)
 *        Scalar  "CornerRoundness"  (0..1)
 *        Vector  "FadeColor"        (RGB + Alpha)
 *      Material settings: Domain=Surface, Blend=Translucent, Shading=Unlit,
 *      TwoSided=true, DisableDepthTest=true, DisableDepthWrite=true.
 *   2. Attach this component as a *child* of your camera (drag it under the
 *      CameraComponent in the pawn's component tree).
 *   3. Assign your material to VignetteMaterial.
 *   4. Tweak parameters live in the Details panel.
 */
UCLASS(ClassGroup = (CobraSDK),
       meta = (BlueprintSpawnableComponent, DisplayName = "Camera Edge Fade"))
class SNKEXRMARKERTRACKER_API UCameraEdgeFadeComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UCameraEdgeFadeComponent();

	/** The material that implements the vignette shader (see class comment). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vignette")
	TObjectPtr<UMaterialInterface> VignetteMaterial = nullptr;

	/** How much of the screen center stays fully clear. 0 = fade from center, 1 = no fade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vignette",
	          meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ClearRadius = 0.0f;

	/** Color and alpha the edges fade to. Alpha=1 is fully opaque at the outer rim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vignette", meta = (HideAlphaChannel = "false"))
	FLinearColor FadeColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);

	/** Fade curve. Higher = more gradual; only the extreme edge is fully opaque. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vignette",
	          meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float FadePower = 3.3f;

	/** 0 = rectangular edges, 1 = circular vignette. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vignette",
	          meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CornerRoundness = 0.0f;

	/**
	 * How far in front of the parent (camera) the quad sits, in cm.
	 * Small positive number — close enough to cover the full view, far enough to avoid near-plane clipping.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vignette|Placement",
	          meta = (ClampMin = "0.5"))
	float QuadDistance = 10.0f;

	/** The edge of the quad in cm at QuadDistance. Should be >= ~2x QuadDistance to cover any reasonable FOV. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vignette|Placement",
	          meta = (ClampMin = "1.0"))
	float QuadSize = 40.0f;

	/** Globally disable the fade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vignette")
	bool bEnabled = true;

	/** Set fade opacity (writes to FadeColor.A). Useful for animating fades. */
	UFUNCTION(BlueprintCallable, Category = "Vignette")
	void SetFadeOpacity(float Alpha01);

	/** Set clear radius (0..1). 0 = fade from center, 1 = no fade. */
	UFUNCTION(BlueprintCallable, Category = "Vignette")
	void SetClearRadius(float NewClearRadius);

	/** Set fade curve power (0.5..10). Higher = more gradual. */
	UFUNCTION(BlueprintCallable, Category = "Vignette")
	void SetFadePower(float NewFadePower);

	/** Set corner roundness (0..1). 0 = rectangular edges, 1 = circular vignette. */
	UFUNCTION(BlueprintCallable, Category = "Vignette")
	void SetCornerRoundness(float NewCornerRoundness);

	/** Set edge fade color (RGB + alpha). Alpha scales overall opacity. */
	UFUNCTION(BlueprintCallable, Category = "Vignette")
	void SetFadeColor(FLinearColor NewFadeColor);

	/** Toggle the whole effect on/off (mesh visibility). */
	UFUNCTION(BlueprintCallable, Category = "Vignette")
	void SetVignetteEnabled(bool bNewEnabled);

	/** Force-push all current UPROPERTY values to the material instance. */
	UFUNCTION(BlueprintCallable, Category = "Vignette")
	void RefreshParameters();

protected:
	virtual void OnRegister() override;
	virtual void BeginPlay() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynamicMID = nullptr;

	void ApplyPlacement();
	void EnsureDynamicMaterial();
	void PushAllParameters();
};
