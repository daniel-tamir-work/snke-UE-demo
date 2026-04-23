#include "RecenterSysPropComponent.h"

#include "HeadMountedDisplayFunctionLibrary.h"
#include "HeadMountedDisplayTypes.h"
#include "Engine/Engine.h"

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
	}

	bLastValueWasOne = bCurrentIsOne;
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
