#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "lib_marker_tracker_client.h"
#include "SnkeXRMarkerTrackerComponent.generated.h"

USTRUCT(BlueprintType)
struct FCobraMarkerData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Marker Tracker")
	int32 MarkerID = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Marker Tracker")
	FVector Position = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Marker Tracker")
	FQuat Rotation = FQuat::Identity;

	UPROPERTY(BlueprintReadOnly, Category = "Marker Tracker")
	float RMSError = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Marker Tracker")
	int32 MarkerError = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Marker Tracker")
	int32 GeneralError = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Marker Tracker")
	double MarkerTimestampMs = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Marker Tracker")
	double CurrentTimestampMs = 0.0;
};

struct FMarkerTransformData
{
	int32 MarkerID = -1;
	FVector Position = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;
	int64 TimestampSeconds = 0;
	int32 TimestampNanos = 0;
	float RMSError = 0.0f;
	int32 MarkerError = 0;
	int32 GeneralError = 0;
	int32 EllipseCount = 0;
	EllipseInfo Ellipses[MAX_DETECTED_ELLIPSES];
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCobraMarkerReceived, const FCobraMarkerData&, MarkerData);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCobraConnectionStatusChanged, const FString&, Status);

/**
 * Streams marker pose data from the Cobra SDK gRPC service and updates
 * the owning actor's transform in real-time. Mirrors the architecture of
 * the Godot SnkeXRMarkerTracker node: a single shared gRPC client with a
 * background streaming thread distributes marker data to per-component buffers.
 *
 * Usage: add this component to any actor, set MarkerID in the Details panel,
 * and the actor's transform will track that marker at runtime on Android.
 */
UCLASS(ClassGroup = (CobraSDK), meta = (BlueprintSpawnableComponent))
class SNKEXRMARKERTRACKER_API USnkeXRMarkerTrackerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USnkeXRMarkerTrackerComponent();

	/** Marker ID to track. -1 disables tracking (actor stays hidden). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marker Tracker")
	int32 MarkerID = -1;

	/** gRPC server address. Default uses Android unix domain socket. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marker Tracker")
	FString ServerAddress = TEXT("unix-abstract:cobra_grpc");

	/** Algorithm mode: 0 = pose only, 1 = 2D ellipses only, 2 = both */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marker Tracker", meta = (ClampMin = "0", ClampMax = "2"))
	int32 AlgorithmMode = 0;

	/** If true, the component sets the owning actor's world transform each frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marker Tracker")
	bool bUpdateOwnerTransform = true;

	/** Print debug info to the output log. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marker Tracker|Debug")
	bool bDebugEnabled = false;

	/** Current connection status (read-only). */
	UPROPERTY(BlueprintReadOnly, Category = "Marker Tracker|Status")
	FString ConnectionStatus = TEXT("Not connected");

	/** Marker update rate in Hz (read-only). */
	UPROPERTY(BlueprintReadOnly, Category = "Marker Tracker|Status")
	float MarkerFPS = 0.0f;

	UPROPERTY(BlueprintAssignable, Category = "Marker Tracker|Events")
	FOnCobraMarkerReceived OnMarkerReceived;

	UPROPERTY(BlueprintAssignable, Category = "Marker Tracker|Events")
	FOnCobraConnectionStatusChanged OnConnectionStatusChanged;

	UFUNCTION(BlueprintCallable, Category = "Marker Tracker")
	bool IsConnected() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	// ── Per-component state ──────────────────────────────────────────────
	TArray<FMarkerTransformData> MarkerBuffer;
	FCriticalSection BufferMutex;
	static constexpr int32 MaxBufferSize = 3;

	FVector OriginalScale = FVector::OneVector;

	float TimeBufferEmpty = 0.0f;
	bool bBufferWasEmptyLastFrame = true;
	int32 MarkerFrameCount = 0;
	float FPSTimer = 0.0f;

	void RegisterComponent();
	void UnregisterComponent();

	// ── Shared client (static across all component instances) ────────────
	static TArray<USnkeXRMarkerTrackerComponent*> RegisteredComponents;
	static FCriticalSection ComponentsMutex;
	static bool bSharedClientInitialized;
	static FString SharedServerAddress;
	static bool bSharedTrackingActive;

	static FRunnableThread* StreamingThread;
	static class FCobraStreamingRunnable* StreamingRunnable;

	static void InitializeSharedClient(const FString& ServerAddr, int32 AlgoMode);
	static void CleanupSharedClient();
	static void DistributeMarkersToComponents(const MarkerStructuresInfo& MarkersInfo);

	/** Convert transformMarkerPose output (Godot/OpenXR space) to UE world space. */
	static void ConvertToUnrealSpace(const TransformedMarkerPose& InPose, FVector& OutPosition, FQuat& OutRotation, float WorldToMeters);

	friend class FCobraStreamingRunnable;
};
