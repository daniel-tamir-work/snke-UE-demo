#include "RecenterSysPropComponent.h"

#include "HeadMountedDisplayFunctionLibrary.h"
#include "HeadMountedDisplayTypes.h"
#include "Engine/Engine.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Pawn.h"

#if PLATFORM_ANDROID
#include <sys/system_properties.h>
#endif

DEFINE_LOG_CATEGORY_STATIC(LogRecenterSysProp, Log, All);

URecenterSysPropComponent::URecenterSysPropComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.0f;
}

void URecenterSysPropComponent::BeginPlay()
{
	Super::BeginPlay();

	const FString Initial = ReadSysProp(SysPropName);
	bLastValueWasOne = (Initial == TEXT("1"));

	if (bUseEyeLevelTracking)
	{
		// Switch OpenXR reference space to LOCAL (formerly "Eye" in UE < 5.4). This is what
		// allows ResetOrientationAndPosition to actually reset Z on Monado-style runtimes.
		UHeadMountedDisplayFunctionLibrary::SetTrackingOrigin(EHMDTrackingOrigin::Local);
		UE_LOG(LogRecenterSysProp, Log,
			TEXT("RecenterSysProp: TrackingOrigin set to Local (eye-level, so Z is recenterable)."));
	}

	UE_LOG(LogRecenterSysProp, Log,
		TEXT("RecenterSysProp: polling '%s' every %.2fs (initial='%s')"),
		*SysPropName, PollIntervalSeconds, *Initial);
}

void URecenterSysPropComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	TimeSinceLastPoll += DeltaTime;
	if (TimeSinceLastPoll < PollIntervalSeconds)
	{
		return;
	}
	TimeSinceLastPoll = 0.0f;

	const FString Current = ReadSysProp(SysPropName);
	const bool bCurrentIsOne = (Current == TEXT("1"));

	if (bCurrentIsOne && !bLastValueWasOne)
	{
		UE_LOG(LogRecenterSysProp, Log,
			TEXT("RecenterSysProp: '%s' rose to '1' — triggering HMD recenter."),
			*SysPropName);
		TriggerRecenterNow();

#if PLATFORM_ANDROID
		if (bAutoResetSysPropToZeroAfterTrigger)
		{
			if (!WriteSysProp(SysPropName, "0"))
			{
				UE_LOG(LogRecenterSysProp, Warning,
					TEXT("RecenterSysProp: could not write sysprop '%s' to 0 from the app (SELinux / permissions). Run: adb shell setprop %s 0"),
					*SysPropName,
					*SysPropName);
			}
		}
#endif
	}

	// Re-read so edge detection works after we clear the prop (or if clear failed, stays armed correctly).
	bLastValueWasOne = (ReadSysProp(SysPropName) == TEXT("1"));
}

void URecenterSysPropComponent::TriggerRecenterNow()
{
	const EOrientPositionSelector::Type Options = bResetPositionToo
		? EOrientPositionSelector::OrientationAndPosition
		: EOrientPositionSelector::Orientation;

	UHeadMountedDisplayFunctionLibrary::ResetOrientationAndPosition(RecenterYawOffset, Options);

	UE_LOG(LogRecenterSysProp, Log,
		TEXT("RecenterSysProp: ResetOrientationAndPosition(yaw=%.2f, options=%s)"),
		RecenterYawOffset,
		bResetPositionToo ? TEXT("OrientationAndPosition") : TEXT("Orientation"));

	// --- Height (Z) fallback ---
	// Many OpenXR runtimes (Monado included) preserve Z through recenter because the
	// floor is an absolute reference. Snap the pawn's Z so the camera's current world
	// Z coincides with the pawn origin. This is visually equivalent to "zero out Z".
	if (bResetPositionToo && bAlsoSnapPawnToHeadHeight)
	{
		APawn* Pawn = Cast<APawn>(GetOwner());
		if (!Pawn)
		{
			UE_LOG(LogRecenterSysProp, Verbose,
				TEXT("RecenterSysProp: owner is not a Pawn, skipping Z snap."));
			return;
		}

		UCameraComponent* Camera = Pawn->FindComponentByClass<UCameraComponent>();
		if (!Camera)
		{
			UE_LOG(LogRecenterSysProp, Warning,
				TEXT("RecenterSysProp: no UCameraComponent on pawn '%s', skipping Z snap."),
				*Pawn->GetName());
			return;
		}

		const FVector PawnLoc   = Pawn->GetActorLocation();
		const FVector CameraLoc = Camera->GetComponentLocation();
		const float   DeltaZ    = CameraLoc.Z - PawnLoc.Z; // head height above pawn origin
		if (FMath::Abs(DeltaZ) > 0.1f)
		{
			FVector NewLoc = PawnLoc;
			NewLoc.Z += DeltaZ; // move pawn up to where the head currently is
			Pawn->SetActorLocation(NewLoc, /*bSweep=*/false);

			UE_LOG(LogRecenterSysProp, Log,
				TEXT("RecenterSysProp: snapped pawn Z by %.2f cm (pawn Z %.2f -> %.2f) so head becomes the new origin."),
				DeltaZ, PawnLoc.Z, NewLoc.Z);
		}
		else
		{
			UE_LOG(LogRecenterSysProp, Verbose,
				TEXT("RecenterSysProp: camera already at pawn Z (delta=%.3f), no Z snap needed."),
				DeltaZ);
		}
	}
}

FString URecenterSysPropComponent::ReadSysProp(const FString& Name)
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

bool URecenterSysPropComponent::WriteSysProp(const FString& Name, const char* Utf8Value)
{
#if PLATFORM_ANDROID
	const FTCHARToUTF8 NameUtf8(*Name);
	const int Err = __system_property_set(NameUtf8.Get(), Utf8Value);
	if (Err != 0)
	{
		UE_LOG(LogRecenterSysProp, Warning,
			TEXT("RecenterSysProp: __system_property_set('%s', '%s') returned %d"),
			*Name,
			UTF8_TO_TCHAR(Utf8Value),
			Err);
		return false;
	}
	return true;
#else
	return false;
#endif
}
