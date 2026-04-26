#include "CameraEdgeFadeComponent.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

#if PLATFORM_ANDROID
#include <sys/system_properties.h>
#endif

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
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.0f;

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
	RefreshEffectiveVisibility();

	// In editor we also want live preview without PIE.
	EnsureDynamicMaterial();
	PushAllParameters();
}

void UCameraEdgeFadeComponent::BeginPlay()
{
	Super::BeginPlay();
	EnsureDynamicMaterial();
	PushAllParameters();

	// Prime the sysprop state immediately so we don't wait a full poll interval
	// for the first read.
	const FString Initial = ReadSysProp(FadeDisableSysProp);
	bSysPropDisabled = (Initial == TEXT("1"));
	RefreshEffectiveVisibility();

	UE_LOG(LogCameraEdgeFade, Log,
		TEXT("CameraEdgeFade: polling '%s' every %.2fs (initial='%s', effective=%s)"),
		*FadeDisableSysProp, SysPropPollInterval, *Initial,
		(bEnabled && !bSysPropDisabled) ? TEXT("visible") : TEXT("hidden"));
}

void UCameraEdgeFadeComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (FadeDisableSysProp.IsEmpty())
	{
		return;
	}

	TimeSinceLastPoll += DeltaTime;
	if (TimeSinceLastPoll < SysPropPollInterval)
	{
		return;
	}
	TimeSinceLastPoll = 0.0f;

	const FString Current = ReadSysProp(FadeDisableSysProp);
	const bool bShouldDisable = (Current == TEXT("1"));

	if (bShouldDisable != bSysPropDisabled)
	{
		bSysPropDisabled = bShouldDisable;
		UE_LOG(LogCameraEdgeFade, Log,
			TEXT("CameraEdgeFade: sysprop '%s' -> '%s', fade now %s"),
			*FadeDisableSysProp, *Current,
			(bEnabled && !bSysPropDisabled) ? TEXT("VISIBLE") : TEXT("HIDDEN"));
		RefreshEffectiveVisibility();
	}
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
		RefreshEffectiveVisibility();
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
	RefreshEffectiveVisibility();
}

void UCameraEdgeFadeComponent::RefreshEffectiveVisibility()
{
	SetVisibility(bEnabled && !bSysPropDisabled);
}

FString UCameraEdgeFadeComponent::ReadSysProp(const FString& Name)
{
#if PLATFORM_ANDROID
	char Buffer[PROP_VALUE_MAX] = { 0 };
	const FTCHARToUTF8 NameUtf8(*Name);
	const int Len = __system_property_get(NameUtf8.Get(), Buffer);
	if (Len <= 0)
	{
		return FString();
	}
	return FString(UTF8_TO_TCHAR(Buffer));
#else
	return FString();
#endif
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
