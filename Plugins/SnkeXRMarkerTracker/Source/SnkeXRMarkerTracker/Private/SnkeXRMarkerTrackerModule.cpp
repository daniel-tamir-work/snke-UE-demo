#include "SnkeXRMarkerTrackerModule.h"

#define LOCTEXT_NAMESPACE "FSnkeXRMarkerTrackerModule"

void FSnkeXRMarkerTrackerModule::StartupModule()
{
	UE_LOG(LogTemp, Log, TEXT("SnkeXRMarkerTracker: Module started"));
}

void FSnkeXRMarkerTrackerModule::ShutdownModule()
{
	UE_LOG(LogTemp, Log, TEXT("SnkeXRMarkerTracker: Module shut down"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSnkeXRMarkerTrackerModule, SnkeXRMarkerTracker)
