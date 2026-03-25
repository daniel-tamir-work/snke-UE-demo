#include "SnkeXRMarkerTrackerComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/RunnableThread.h"
#include "HAL/Runnable.h"
#include "HAL/PlatformProcess.h"
#include "Misc/DateTime.h"

// ═══════════════════════════════════════════════════════════════════════
// Static member definitions
// ═══════════════════════════════════════════════════════════════════════

TArray<USnkeXRMarkerTrackerComponent*> USnkeXRMarkerTrackerComponent::RegisteredComponents;
FCriticalSection USnkeXRMarkerTrackerComponent::ComponentsMutex;
bool USnkeXRMarkerTrackerComponent::bSharedClientInitialized = false;
FString USnkeXRMarkerTrackerComponent::SharedServerAddress;
bool USnkeXRMarkerTrackerComponent::bSharedTrackingActive = false;
FRunnableThread* USnkeXRMarkerTrackerComponent::StreamingThread = nullptr;
FCobraStreamingRunnable* USnkeXRMarkerTrackerComponent::StreamingRunnable = nullptr;

// ═══════════════════════════════════════════════════════════════════════
// Background streaming thread (FRunnable)
// ═══════════════════════════════════════════════════════════════════════

class FCobraStreamingRunnable : public FRunnable
{
public:
	FThreadSafeBool bShouldRun = true;

	virtual uint32 Run() override
	{
		int32 ConsecutiveFailures = 0;
		double LastReconnectTime = FPlatformTime::Seconds();
		bool bIsDisconnected = false;
		FString SavedAddress = USnkeXRMarkerTrackerComponent::SharedServerAddress;

		MarkerStructuresInfo MarkersInfo;
		FMemory::Memzero(&MarkersInfo, sizeof(MarkersInfo));

		while (bShouldRun)
		{
			// Reconnect loop
			if (!USnkeXRMarkerTrackerComponent::bSharedClientInitialized || bIsDisconnected)
			{
				double Now = FPlatformTime::Seconds();
				if (Now - LastReconnectTime < 1.0)
				{
					FPlatformProcess::Sleep(0.1f);
					continue;
				}
				LastReconnectTime = Now;

				if (SavedAddress.IsEmpty() || !USnkeXRMarkerTrackerComponent::bSharedClientInitialized)
				{
					SavedAddress = USnkeXRMarkerTrackerComponent::SharedServerAddress;
				}

				{
					FScopeLock Lock(&USnkeXRMarkerTrackerComponent::ComponentsMutex);
					for (auto* Comp : USnkeXRMarkerTrackerComponent::RegisteredComponents)
					{
						Comp->ConnectionStatus = FString::Printf(TEXT("Connecting... (attempt %d)"), ConsecutiveFailures + 1);
					}
				}

				auto AddrUtf8 = StringCast<ANSICHAR>(*SavedAddress);
				int Result = generalStart(AddrUtf8.Get());

				if (Result == 1)
				{
					bIsDisconnected = false;
					ConsecutiveFailures = 0;
					USnkeXRMarkerTrackerComponent::bSharedClientInitialized = true;
					USnkeXRMarkerTrackerComponent::bSharedTrackingActive = true;

					FScopeLock Lock(&USnkeXRMarkerTrackerComponent::ComponentsMutex);
					for (auto* Comp : USnkeXRMarkerTrackerComponent::RegisteredComponents)
					{
						Comp->ConnectionStatus = TEXT("Tracking markers on ") + SavedAddress;
					}
				}
				else
				{
					ConsecutiveFailures++;
				}
				continue;
			}

			if (!USnkeXRMarkerTrackerComponent::bSharedTrackingActive)
			{
				FPlatformProcess::Sleep(0.1f);
				continue;
			}

			// Stream one frame of marker data
			int StreamResult = streamMarkersDirect(&MarkersInfo);

			if (StreamResult == 0 || MarkersInfo.marker_count < 0)
			{
				ConsecutiveFailures++;
				if (ConsecutiveFailures >= 3)
				{
					bIsDisconnected = true;
					SavedAddress = USnkeXRMarkerTrackerComponent::SharedServerAddress;
					USnkeXRMarkerTrackerComponent::bSharedClientInitialized = false;
					USnkeXRMarkerTrackerComponent::bSharedTrackingActive = false;

					FScopeLock Lock(&USnkeXRMarkerTrackerComponent::ComponentsMutex);
					for (auto* Comp : USnkeXRMarkerTrackerComponent::RegisteredComponents)
					{
						Comp->ConnectionStatus = TEXT("Disconnected - attempting reconnection");
					}
				}
				continue;
			}

			if (MarkersInfo.marker_count >= 0)
			{
				ConsecutiveFailures = 0;
			}

			if (MarkersInfo.marker_count > 0)
			{
				FScopeLock Lock(&USnkeXRMarkerTrackerComponent::ComponentsMutex);
				USnkeXRMarkerTrackerComponent::DistributeMarkersToComponents(MarkersInfo);
			}
		}

		return 0;
	}

	virtual void Stop() override
	{
		bShouldRun = false;
	}
};

// ═══════════════════════════════════════════════════════════════════════
// Component lifecycle
// ═══════════════════════════════════════════════════════════════════════

USnkeXRMarkerTrackerComponent::USnkeXRMarkerTrackerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void USnkeXRMarkerTrackerComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* Owner = GetOwner())
	{
		OriginalScale = Owner->GetActorScale3D();

		if (USceneComponent* Root = Owner->GetRootComponent())
		{
			if (Root->Mobility != EComponentMobility::Movable)
			{
				Root->SetMobility(EComponentMobility::Movable);
				UE_LOG(LogTemp, Warning, TEXT("SnkeXRMarkerTracker: Forced root component to Movable"));
			}
		}
	}

	if (bDebugEnabled)
	{
		UE_LOG(LogTemp, Log, TEXT("SnkeXRMarkerTracker: BeginPlay - MarkerID=%d, Server=%s"), MarkerID, *ServerAddress);
	}

	RegisterComponent();
}

void USnkeXRMarkerTrackerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterComponent();
	Super::EndPlay(EndPlayReason);
}

void USnkeXRMarkerTrackerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (MarkerID == -1)
	{
		if (AActor* Owner = GetOwner())
		{
			Owner->SetActorHiddenInGame(true);
		}
		return;
	}

	// Drain buffer, keep newest entry only (same pattern as Godot)
	FMarkerTransformData LatestData;
	bool bHasData = false;
	{
		FScopeLock Lock(&BufferMutex);
		if (MarkerBuffer.Num() > 0)
		{
			LatestData = MarkerBuffer.Last();
			MarkerBuffer.Reset();
			bHasData = true;
		}
	}

	if (bHasData)
	{
		AActor* Owner = GetOwner();
		if (Owner)
		{
			Owner->SetActorHiddenInGame(false);
		}
		bBufferWasEmptyLastFrame = false;
		TimeBufferEmpty = 0.0f;

		if (bUpdateOwnerTransform && Owner)
		{
			FTransform MarkerLocal(LatestData.Rotation, LatestData.Position, FVector::OneVector);
			FTransform FinalTransform = MarkerLocal;

			// Marker pose is relative to camera — compose with HMD world transform
			UWorld* World = GetWorld();
			if (World)
			{
				APawn* Pawn = World->GetFirstPlayerController() ? World->GetFirstPlayerController()->GetPawn() : nullptr;
				UCameraComponent* Cam = Pawn ? Pawn->FindComponentByClass<UCameraComponent>() : nullptr;
				if (Cam)
				{
					FTransform CameraWorld = Cam->GetComponentTransform();
					CameraWorld.SetScale3D(FVector::OneVector);
					FinalTransform = MarkerLocal * CameraWorld;
				}
			}

			FinalTransform.SetScale3D(OriginalScale);

			if (bDebugEnabled)
			{
				FVector WP = FinalTransform.GetLocation();
				FQuat WR = FinalTransform.GetRotation();
				UE_LOG(LogTemp, Log, TEXT("SnkeXRMarkerTracker: World Pos(%.2f, %.2f, %.2f) Rot(%.3f, %.3f, %.3f, %.3f)"),
					WP.X, WP.Y, WP.Z, WR.X, WR.Y, WR.Z, WR.W);
			}

			Owner->SetActorTransform(FinalTransform, false, nullptr, ETeleportType::TeleportPhysics);
		}

		// Build public data struct and broadcast
		FCobraMarkerData MarkerData;
		MarkerData.MarkerID = LatestData.MarkerID;
		MarkerData.Position = LatestData.Position;
		MarkerData.Rotation = LatestData.Rotation;
		MarkerData.RMSError = LatestData.RMSError;
		MarkerData.MarkerError = LatestData.MarkerError;
		MarkerData.GeneralError = LatestData.GeneralError;
		MarkerData.MarkerTimestampMs = static_cast<double>(LatestData.TimestampSeconds) * 1000.0
			+ static_cast<double>(LatestData.TimestampNanos) / 1e6;
		MarkerData.CurrentTimestampMs = FPlatformTime::Seconds() * 1000.0;

		OnMarkerReceived.Broadcast(MarkerData);

		// FPS counter
		MarkerFrameCount++;
		FPSTimer += DeltaTime;
		if (FPSTimer >= 1.0f)
		{
			MarkerFPS = MarkerFrameCount / FPSTimer;
			if (bDebugEnabled)
			{
				UE_LOG(LogTemp, Log, TEXT("Marker ID %d FPS: %.1f | Pos(%.3f, %.3f, %.3f) RMS: %.3f"),
					MarkerID, MarkerFPS,
					LatestData.Position.X, LatestData.Position.Y, LatestData.Position.Z,
					LatestData.RMSError);
			}
			MarkerFrameCount = 0;
			FPSTimer = 0.0f;
		}
	}
	else
	{
		// Buffer empty — hide actor after 100ms
		if (!bBufferWasEmptyLastFrame)
		{
			TimeBufferEmpty = 0.0f;
			bBufferWasEmptyLastFrame = true;
		}
		else
		{
			TimeBufferEmpty += DeltaTime;
		}

		if (TimeBufferEmpty >= 0.1f)
		{
			if (AActor* Owner = GetOwner())
			{
				Owner->SetActorHiddenInGame(true);
			}
		}
	}
}

bool USnkeXRMarkerTrackerComponent::IsConnected() const
{
	return bSharedClientInitialized;
}

// ═══════════════════════════════════════════════════════════════════════
// Shared client management
// ═══════════════════════════════════════════════════════════════════════

void USnkeXRMarkerTrackerComponent::RegisterComponent()
{
	FScopeLock Lock(&ComponentsMutex);
	RegisteredComponents.Add(this);
	bool bIsFirst = (RegisteredComponents.Num() == 1);

	if (bDebugEnabled)
	{
		UE_LOG(LogTemp, Log, TEXT("SnkeXRMarkerTracker: Registered component (total: %d)"), RegisteredComponents.Num());
	}

	if (bIsFirst)
	{
		InitializeSharedClient(ServerAddress, AlgorithmMode);
	}
	else if (bSharedClientInitialized)
	{
		ConnectionStatus = TEXT("Connected (shared) to ") + SharedServerAddress;
	}
	else
	{
		ConnectionStatus = TEXT("Waiting for connection");
	}
}

void USnkeXRMarkerTrackerComponent::UnregisterComponent()
{
	bool bShouldCleanup = false;
	{
		FScopeLock Lock(&ComponentsMutex);
		RegisteredComponents.Remove(this);
		bShouldCleanup = (RegisteredComponents.Num() == 0);

		if (bDebugEnabled)
		{
			UE_LOG(LogTemp, Log, TEXT("SnkeXRMarkerTracker: Unregistered component (remaining: %d)"), RegisteredComponents.Num());
		}
	}
	// Cleanup OUTSIDE the lock — the streaming thread also needs the lock
	if (bShouldCleanup)
	{
		CleanupSharedClient();
	}
	ConnectionStatus = TEXT("Disconnected");
}

void USnkeXRMarkerTrackerComponent::InitializeSharedClient(const FString& ServerAddr, int32 AlgoMode)
{
	SharedServerAddress = ServerAddr;

	auto AddrUtf8 = StringCast<ANSICHAR>(*ServerAddr);
	int Result = generalStart(AddrUtf8.Get(), AlgoMode);

	if (Result == 1)
	{
		bSharedClientInitialized = true;
		bSharedTrackingActive = true;
		UE_LOG(LogTemp, Log, TEXT("SnkeXRMarkerTracker: Connected and tracking on %s"), *ServerAddr);
	}
	else
	{
		bSharedClientInitialized = false;
		bSharedTrackingActive = false;
		UE_LOG(LogTemp, Warning, TEXT("SnkeXRMarkerTracker: Failed to connect — will retry in background"));
	}

	// Start streaming thread (handles reconnection either way)
	if (!StreamingRunnable)
	{
		StreamingRunnable = new FCobraStreamingRunnable();
		StreamingThread = FRunnableThread::Create(StreamingRunnable, TEXT("CobraMarkerStreaming"), 0, TPri_Normal);
		UE_LOG(LogTemp, Log, TEXT("SnkeXRMarkerTracker: Streaming thread started"));
	}

	for (auto* Comp : RegisteredComponents)
	{
		Comp->ConnectionStatus = Result == 1
			? (TEXT("Tracking markers on ") + ServerAddr)
			: TEXT("Connection failed — retrying...");
	}
}

void USnkeXRMarkerTrackerComponent::CleanupSharedClient()
{
	if (StreamingRunnable)
	{
		StreamingRunnable->bShouldRun = false;
	}
	if (StreamingThread)
	{
		StreamingThread->WaitForCompletion();
		delete StreamingThread;
		StreamingThread = nullptr;
	}
	if (StreamingRunnable)
	{
		delete StreamingRunnable;
		StreamingRunnable = nullptr;
	}

	if (bSharedTrackingActive)
	{
		stopMarkerTracking();
		bSharedTrackingActive = false;
	}
	if (bSharedClientInitialized)
	{
		cleanupClient();
		bSharedClientInitialized = false;
	}
	SharedServerAddress.Empty();

	UE_LOG(LogTemp, Log, TEXT("SnkeXRMarkerTracker: Shared client cleaned up"));
}

// ═══════════════════════════════════════════════════════════════════════
// Marker distribution (called from streaming thread under ComponentsMutex)
// ═══════════════════════════════════════════════════════════════════════

void USnkeXRMarkerTrackerComponent::DistributeMarkersToComponents(const MarkerStructuresInfo& MarkersInfo)
{
	float WorldToMeters = 100.0f; // Default; overridden per-component if world is available

	for (int32 i = 0; i < MarkersInfo.marker_count; i++)
	{
		const MarkerStructureInfo& Marker = MarkersInfo.marker_structures[i];
		int32 CurrentMarkerID = static_cast<int32>(Marker.marker_id);

		bool bValidPos = FMath::IsFinite(Marker.pose.x) && FMath::IsFinite(Marker.pose.y) && FMath::IsFinite(Marker.pose.z);
		bool bValidRot = FMath::IsFinite(Marker.pose.qx) && FMath::IsFinite(Marker.pose.qy)
			&& FMath::IsFinite(Marker.pose.qz) && FMath::IsFinite(Marker.pose.qw);
		if (!bValidPos || !bValidRot)
		{
			continue;
		}

		TransformedMarkerPose TransformedPose;
		if (transformMarkerPose(&Marker.pose, &TransformedPose) == 0 || TransformedPose.valid == 0)
		{
			continue;
		}

		for (auto* Comp : RegisteredComponents)
		{
			if (CurrentMarkerID != Comp->MarkerID)
			{
				continue;
			}

			FVector Position;
			FQuat Rotation;
			ConvertToUnrealSpace(TransformedPose, Position, Rotation, WorldToMeters);

			FMarkerTransformData Data;
			Data.MarkerID = CurrentMarkerID;
			Data.Position = Position;
			Data.Rotation = Rotation;
			Data.TimestampSeconds = MarkersInfo.timestamp_seconds;
			Data.TimestampNanos = MarkersInfo.timestamp_nanos;
			Data.RMSError = Marker.rms_position_error;
			Data.MarkerError = Marker.error;
			Data.GeneralError = MarkersInfo.general_error;
			Data.EllipseCount = FMath::Min(MarkersInfo.ellipse_count, (int)MAX_DETECTED_ELLIPSES);
			if (Data.EllipseCount > 0 && MarkersInfo.detected_ellipses)
			{
				FMemory::Memcpy(Data.Ellipses, MarkersInfo.detected_ellipses, Data.EllipseCount * sizeof(EllipseInfo));
			}

			{
				FScopeLock Lock(&Comp->BufferMutex);
				if (Comp->MarkerBuffer.Num() >= MaxBufferSize)
				{
					Comp->MarkerBuffer.RemoveAt(0);
				}
				Comp->MarkerBuffer.Add(Data);
			}
		}
	}
}

// ═══════════════════════════════════════════════════════════════════════
// Coordinate conversion: Godot/OpenXR → Unreal Engine
//
// transformMarkerPose outputs right-handed Y-up in meters (OpenXR/Godot space).
// UE uses left-handed Z-up in centimeters.
// This matches the conversion UE's own OpenXR plugin applies:
//   UE.X = -OpenXR.Z * scale
//   UE.Y =  OpenXR.X * scale
//   UE.Z =  OpenXR.Y * scale
//   Quaternion: (-qz, qx, qy, -qw)
// ═══════════════════════════════════════════════════════════════════════

void USnkeXRMarkerTrackerComponent::ConvertToUnrealSpace(
	const TransformedMarkerPose& InPose,
	FVector& OutPosition,
	FQuat& OutRotation,
	float WorldToMeters)
{
	OutPosition.X = -InPose.position[2] * WorldToMeters;
	OutPosition.Y =  InPose.position[0] * WorldToMeters;
	OutPosition.Z =  InPose.position[1] * WorldToMeters;

	OutRotation.X =  InPose.rotation[2];
	OutRotation.Y =  -InPose.rotation[0];
	OutRotation.Z =  -InPose.rotation[1];
	OutRotation.W = -InPose.rotation[3];
	OutRotation.Normalize();
}
