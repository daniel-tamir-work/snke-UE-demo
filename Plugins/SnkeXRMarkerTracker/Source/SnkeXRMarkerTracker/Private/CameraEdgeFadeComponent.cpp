#include "CameraEdgeFadeComponent.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogCameraEdgeFade, Log, All);

namespace
{
	// Parameter names must match what the user wires in their Material graph.
	const FName PN_ClearRadius     = TEXT("ClearRadius");
	const FName PN_FadePower       = TEXT("FadePower");
	const FName PN_CornerRoundness = TEXT("CornerRoundness");
	const FName PN_FadeColor       = TEXT("FadeColor");
}

UCameraEdgeFadeComponent::UCameraEdgeFadeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Engine's default plane mesh (1m x 1m, +X normal). We rotate it to face -Y
	// so the visible face points toward +X from the camera when we offset it.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(
		TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		SetStaticMesh(PlaneMesh.Object);
	}

	// Render hygiene: the fade should never interact with the world.
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCollisionProfileName(TEXT("NoCollision"));
	SetGenerateOverlapEvents(false);
	SetCastShadow(false);
	bCastDynamicShadow      = false;
	bCastStaticShadow       = false;
	bAffectDistanceFieldLighting = false;
	bAffectDynamicIndirectLighting = false;
	bReceivesDecals         = false;
	SetRenderCustomDepth(false);

	// Render on top of everything — this is a camera overlay.
	TranslucencySortPriority = 100;

	// Default placement applied in OnRegister via ApplyPlacement().
}

void UCameraEdgeFadeComponent::OnRegister()
{
	Super::OnRegister();
	ApplyPlacement();
	SetVisibility(bEnabled);

	// In editor we also want live preview without PIE.
	EnsureDynamicMaterial();
	PushAllParameters();
}

void UCameraEdgeFadeComponent::BeginPlay()
{
	Super::BeginPlay();
	EnsureDynamicMaterial();
	PushAllParameters();
	SetVisibility(bEnabled);
}

#if WITH_EDITOR
void UCameraEdgeFadeComponent::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);

	const FName Name = Event.GetPropertyName();

	if (Name == GET_MEMBER_NAME_CHECKED(UCameraEdgeFadeComponent, QuadDistance) ||
	    Name == GET_MEMBER_NAME_CHECKED(UCameraEdgeFadeComponent, QuadSize))
	{
		ApplyPlacement();
	}

	if (Name == GET_MEMBER_NAME_CHECKED(UCameraEdgeFadeComponent, VignetteMaterial))
	{
		// Re-create MID from the new base material.
		DynamicMID = nullptr;
		EnsureDynamicMaterial();
	}

	if (Name == GET_MEMBER_NAME_CHECKED(UCameraEdgeFadeComponent, bEnabled))
	{
		SetVisibility(bEnabled);
	}

	PushAllParameters();
}
#endif

void UCameraEdgeFadeComponent::SetFadeOpacity(float Alpha01)
{
	FadeColor.A = FMath::Clamp(Alpha01, 0.0f, 1.0f);
	PushAllParameters();
}

void UCameraEdgeFadeComponent::SetClearRadius(float NewClearRadius)
{
	ClearRadius = FMath::Clamp(NewClearRadius, 0.0f, 1.0f);
	PushAllParameters();
}

void UCameraEdgeFadeComponent::SetFadePower(float NewFadePower)
{
	FadePower = FMath::Clamp(NewFadePower, 0.5f, 10.0f);
	PushAllParameters();
}

void UCameraEdgeFadeComponent::SetCornerRoundness(float NewCornerRoundness)
{
	CornerRoundness = FMath::Clamp(NewCornerRoundness, 0.0f, 1.0f);
	PushAllParameters();
}

void UCameraEdgeFadeComponent::SetFadeColor(FLinearColor NewFadeColor)
{
	FadeColor = NewFadeColor;
	PushAllParameters();
}

void UCameraEdgeFadeComponent::SetVignetteEnabled(bool bNewEnabled)
{
	bEnabled = bNewEnabled;
	SetVisibility(bEnabled);
}

void UCameraEdgeFadeComponent::RefreshParameters()
{
	EnsureDynamicMaterial();
	PushAllParameters();
}

void UCameraEdgeFadeComponent::ApplyPlacement()
{
	// The Engine plane is 100 cm square, lying in XY plane with normal +Z.
	// We want the plane to face the camera (normal pointing back at it), pinned in front.
	//
	// Camera local space in UE: +X forward, +Y right, +Z up.
	// A plane whose normal is -X (facing the camera) with size QuadSize:
	//   - Rotate the plane so its +Z normal becomes -X: pitch -90 (rotate around Y).
	//   - Place it QuadDistance along +X.
	const float Scale01 = QuadSize / 100.0f; // engine plane is 100 cm

	SetRelativeLocation(FVector(QuadDistance, 0.0f, 0.0f));
	SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));      // Pitch -90: normal from +Z to -X
	SetRelativeScale3D(FVector(Scale01, Scale01, 1.0f));
}

void UCameraEdgeFadeComponent::EnsureDynamicMaterial()
{
	if (!VignetteMaterial)
	{
		return;
	}

	if (!DynamicMID || DynamicMID->Parent != VignetteMaterial)
	{
		DynamicMID = UMaterialInstanceDynamic::Create(VignetteMaterial, this);
	}

	if (DynamicMID)
	{
		SetMaterial(0, DynamicMID);
	}
}

void UCameraEdgeFadeComponent::PushAllParameters()
{
	if (!DynamicMID)
	{
		return;
	}

	DynamicMID->SetScalarParameterValue(PN_ClearRadius,     ClearRadius);
	DynamicMID->SetScalarParameterValue(PN_FadePower,       FadePower);
	DynamicMID->SetScalarParameterValue(PN_CornerRoundness, CornerRoundness);
	DynamicMID->SetVectorParameterValue(PN_FadeColor,       FadeColor);
}
