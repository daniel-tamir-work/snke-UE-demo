#include "GraspGrabComponent.h"

#include "HeadMountedDisplayFunctionLibrary.h"
#include "HeadMountedDisplayTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h" // TActorIterator
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"

DEFINE_LOG_CATEGORY_STATIC(LogGraspGrab, Log, All);

namespace
{
	static const TCHAR* HandToStr(EControllerHand H)
	{
		switch (H)
		{
			case EControllerHand::Left:  return TEXT("Left");
			case EControllerHand::Right: return TEXT("Right");
			default:                     return TEXT("Other");
		}
	}

	static const TCHAR* TrackingStatusToStr(ETrackingStatus S)
	{
		switch (S)
		{
			case ETrackingStatus::NotTracked:       return TEXT("NotTracked");
			case ETrackingStatus::InertialOnly:     return TEXT("InertialOnly");
			case ETrackingStatus::Tracked:          return TEXT("Tracked");
			default:                                return TEXT("?");
		}
	}

	// Compute 0..1 curl for a single finger using joint positions only (rotation-convention-independent).
	// Measures how "folded" the finger is by the angle between the base segment (metacarpal->proximal)
	// and the vector from proximal to the fingertip. Straight finger ~0, tightly curled fist ~1.
	static float ComputeFingerCurl(
		const FXRMotionControllerData& Data,
		EHandKeypoint Metacarpal,
		EHandKeypoint Proximal,
		EHandKeypoint Tip)
	{
		const FVector M = Data.HandKeyPositions[(int32)Metacarpal];
		const FVector P = Data.HandKeyPositions[(int32)Proximal];
		const FVector T = Data.HandKeyPositions[(int32)Tip];

		const FVector BaseDir = (P - M).GetSafeNormal();
		const FVector TipDir  = (T - P).GetSafeNormal();
		if (BaseDir.IsNearlyZero() || TipDir.IsNearlyZero())
		{
			return 0.0f;
		}

		// Dot = 1 when finger is straight, -1 when fully folded back onto itself.
		const float Dot = FVector::DotProduct(BaseDir, TipDir);
		// Map [1, -1] -> [0, 1]
		return FMath::Clamp((1.0f - Dot) * 0.5f, 0.0f, 1.0f);
	}

	// Compute overall grasp strength (0..1) from the 4 non-thumb fingers.
	// Returns -1 if skeletal position data isn't available.
	// Validation matches the project's BP_RiggedHandRenderModel / BP_XRHands flow:
	// both HandKeyPositions and HandKeyRotations must carry all 26 joints.
	static float ComputeGraspStrength(const FXRMotionControllerData& Data)
	{
		if (Data.HandKeyPositions.Num()  != EHandKeypointCount ||
		    Data.HandKeyRotations.Num() != EHandKeypointCount)
		{
			return -1.0f;
		}

		const float IdxCurl = ComputeFingerCurl(Data,
			EHandKeypoint::IndexMetacarpal, EHandKeypoint::IndexProximal, EHandKeypoint::IndexTip);
		const float MidCurl = ComputeFingerCurl(Data,
			EHandKeypoint::MiddleMetacarpal, EHandKeypoint::MiddleProximal, EHandKeypoint::MiddleTip);
		const float RngCurl = ComputeFingerCurl(Data,
			EHandKeypoint::RingMetacarpal, EHandKeypoint::RingProximal, EHandKeypoint::RingTip);
		const float LitCurl = ComputeFingerCurl(Data,
			EHandKeypoint::LittleMetacarpal, EHandKeypoint::LittleProximal, EHandKeypoint::LittleTip);

		return (IdxCurl + MidCurl + RngCurl + LitCurl) * 0.25f;
	}
}

UGraspGrabComponent::UGraspGrabComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork; // after hand pose is updated
	bAutoActivate = true;
}

void UGraspGrabComponent::BeginPlay()
{
	Super::BeginPlay();
	bWasGrasped = false;
	HeldActor = nullptr;

	if (bAutoMakeGrabbablesMovable || bAutoFixGrabbableCollision || bAutoEnablePhysicsOnGrabbables)
	{
		AutoSetupGrabbablesInWorld();
	}
}

void UGraspGrabComponent::AutoSetupGrabbablesInWorld()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	int32 PromotedMobility = 0;
	int32 PromotedCollision = 0;
	int32 EnabledPhysics    = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A || !A->ActorHasTag(GrabTag))
		{
			continue;
		}

		USceneComponent* Root = A->GetRootComponent();
		if (!Root)
		{
			continue;
		}

		if (bAutoMakeGrabbablesMovable && Root->Mobility != EComponentMobility::Movable)
		{
			Root->SetMobility(EComponentMobility::Movable);
			++PromotedMobility;
		}

		UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Root);
		if (!Prim)
		{
			continue;
		}

		if (bAutoFixGrabbableCollision)
		{
			const FName Profile = Prim->GetCollisionProfileName();
			if (Profile == TEXT("NoCollision") || Profile == TEXT("Default") ||
			    Prim->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
			{
				Prim->SetCollisionProfileName(TEXT("PhysicsActor"));
				++PromotedCollision;
			}
		}

		if (bAutoEnablePhysicsOnGrabbables && !Prim->IsSimulatingPhysics())
		{
			Prim->SetSimulatePhysics(true);
			++EnabledPhysics;
		}
	}

	UE_LOG(LogGraspGrab, Log,
		TEXT("Grape [%s] AutoSetup: tag='%s', promoted Mobility=%d, fixed Collision=%d, enabled Physics=%d"),
		HandToStr(Hand), *GrabTag.ToString(), PromotedMobility, PromotedCollision, EnabledPhysics);
}

void UGraspGrabComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseHeldActor();
	Super::EndPlay(EndPlayReason);
}

void UGraspGrabComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	FXRMotionControllerData Data;
	UHeadMountedDisplayFunctionLibrary::GetMotionControllerData(World, Hand, Data);

	// Compute grasp strength from skeletal fingertip-to-palm distances.
	const float GraspStrength = ComputeGraspStrength(Data);
	const bool bHasSkeleton = GraspStrength >= 0.0f;

	// Publish to BlueprintReadOnly UPROPERTY so sibling components (e.g. the
	// hand visual shader) can read it without recomputing.
	CurrentGraspStrength = bHasSkeleton ? GraspStrength : 0.0f;

	// Decide new grasp state with hysteresis.
	bool bIsGrasped = bWasGrasped;
	if (Data.bValid && bHasSkeleton)
	{
		if (!bWasGrasped && GraspStrength >= GraspThreshold)
		{
			bIsGrasped = true;
		}
		else if (bWasGrasped && GraspStrength <= ReleaseThreshold)
		{
			bIsGrasped = false;
		}
	}
	else if (!Data.bValid)
	{
		// Lost tracking — treat as released so we don't hold forever.
		bIsGrasped = false;
	}

	// ---- Debug heartbeat (on-screen + log) ----
	if (bDebugLogGraspState)
	{
		DebugLogAccumulator += DeltaTime;
		if (DebugLogAccumulator >= DebugLogInterval)
		{
			DebugLogAccumulator = 0.0f;
			// Per-finger breakdown (helps calibrate thresholds).
			float IdxC = 0.f, MidC = 0.f, RngC = 0.f, LitC = 0.f;
			FVector PalmPos = FVector::ZeroVector;
			if (Data.HandKeyPositions.Num()  == EHandKeypointCount &&
			    Data.HandKeyRotations.Num() == EHandKeypointCount)
			{
				IdxC = ComputeFingerCurl(Data,
					EHandKeypoint::IndexMetacarpal, EHandKeypoint::IndexProximal, EHandKeypoint::IndexTip);
				MidC = ComputeFingerCurl(Data,
					EHandKeypoint::MiddleMetacarpal, EHandKeypoint::MiddleProximal, EHandKeypoint::MiddleTip);
				RngC = ComputeFingerCurl(Data,
					EHandKeypoint::RingMetacarpal, EHandKeypoint::RingProximal, EHandKeypoint::RingTip);
				LitC = ComputeFingerCurl(Data,
					EHandKeypoint::LittleMetacarpal, EHandKeypoint::LittleProximal, EHandKeypoint::LittleTip);

				PalmPos = Data.HandKeyPositions[(int32)EHandKeypoint::Palm];
			}

			UE_LOG(LogGraspGrab, Log,
				TEXT("Grape [%s] bValid=%d keys=%d grasp=%.2f (I=%.2f M=%.2f R=%.2f L=%.2f) thr=%.2f state=%s palm=(%.1f, %.1f, %.1f)"),
				HandToStr(Hand),
				Data.bValid ? 1 : 0,
				Data.HandKeyPositions.Num(),
				GraspStrength,
				IdxC, MidC, RngC, LitC,
				GraspThreshold,
				bIsGrasped ? TEXT("HELD") : TEXT("open"),
				PalmPos.X, PalmPos.Y, PalmPos.Z);

			if (GEngine)
			{
				const FColor C = bIsGrasped ? FColor::Green : (Data.bValid ? FColor::Yellow : FColor::Red);
				const int32 Key = (Hand == EControllerHand::Left) ? 12001 : 12002;
				GEngine->AddOnScreenDebugMessage(Key, DebugLogInterval + 0.25f, C,
					FString::Printf(TEXT("Grape [%s] valid=%d grasp=%.2f %s"),
						HandToStr(Hand), Data.bValid ? 1 : 0, GraspStrength,
						bIsGrasped ? TEXT("HELD") : TEXT("open")));
			}

			// Held-object pose heartbeat (fires on the same 1Hz cadence as above).
			if (AActor* Held = HeldActor.Get())
			{
				const FVector    Loc = Held->GetActorLocation();
				const FRotator   Rot = Held->GetActorRotation();
				UE_LOG(LogGraspGrab, Log,
					TEXT("Grape [%s] held='%s' loc=(%.1f, %.1f, %.1f) rot=(P=%.1f Y=%.1f R=%.1f)"),
					HandToStr(Hand), *Held->GetName(),
					Loc.X, Loc.Y, Loc.Z,
					Rot.Pitch, Rot.Yaw, Rot.Roll);

				if (GEngine)
				{
					const int32 HeldKey = (Hand == EControllerHand::Left) ? 12005 : 12006;
					GEngine->AddOnScreenDebugMessage(HeldKey, DebugLogInterval + 0.25f, FColor::Cyan,
						FString::Printf(TEXT("[%s] %s @ (%.0f, %.0f, %.0f)"),
							HandToStr(Hand), *Held->GetName(), Loc.X, Loc.Y, Loc.Z));
				}
			}
		}
	}

	// ---- State-change logs ----
	if (bIsGrasped != bWasGrasped)
	{
		UE_LOG(LogGraspGrab, Log, TEXT("Grape [%s] grasp %s (strength=%.2f)"),
			HandToStr(Hand),
			bIsGrasped ? TEXT("PRESSED") : TEXT("RELEASED"),
			GraspStrength);
		if (GEngine)
		{
			const int32 Key = (Hand == EControllerHand::Left) ? 12003 : 12004;
			GEngine->AddOnScreenDebugMessage(Key, 1.5f,
				bIsGrasped ? FColor::Green : FColor::Orange,
				FString::Printf(TEXT("Grape [%s] %s"), HandToStr(Hand),
					bIsGrasped ? TEXT("GRASP") : TEXT("release")));
		}
	}

	// Rising edge: try to grab
	if (bIsGrasped && !bWasGrasped && !HeldActor.IsValid())
	{
		TryGrab(Data);
	}
	// Falling edge: release
	else if (!bIsGrasped && bWasGrasped && HeldActor.IsValid())
	{
		ReleaseHeldActor();
	}

	bWasGrasped = bIsGrasped;
}

void UGraspGrabComponent::TryGrab(const FXRMotionControllerData& Data)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// Prefer the palm keypoint position (world space) as the grab origin.
	// Data.PalmPosition is a controller-only field and is zero on hand-tracking runtimes,
	// but HandKeyPositions[Palm] is populated. Fall back to owner location if keypoints aren't available.
	FVector Origin = Owner->GetActorLocation();
	if (Data.HandKeyPositions.Num() == EHandKeypointCount)
	{
		const FVector PalmKey = Data.HandKeyPositions[(int32)EHandKeypoint::Palm];
		if (!PalmKey.IsNearlyZero())
		{
			Origin = PalmKey;
		}
	}

	AActor* Target = FindNearestGrabbable(Origin);
	if (!Target)
	{
		if (bVerboseLogging)
		{
			UE_LOG(LogGraspGrab, Verbose, TEXT("Grape [Hand %d] Grasp: no '%s' actor within %.1f cm of palm"),
				(int32)Hand, *GrabTag.ToString(), GrabRadius);
		}
		return;
	}

	USceneComponent* OwnerRoot = Owner->GetRootComponent();
	USceneComponent* TargetRoot = Target->GetRootComponent();
	if (!OwnerRoot || !TargetRoot)
	{
		return;
	}

	if (TargetRoot->Mobility != EComponentMobility::Movable)
	{
		UE_LOG(LogGraspGrab, Warning, TEXT("Grape Target %s root is not Movable — cannot grab. Set mobility to Movable."),
			*Target->GetName());
		return;
	}

	// Disable physics while held so KeepWorld attach can drive the transform.
	// Remember whether it was on so we can restore (object falls back) on release.
	bHeldWasSimulatingPhysics = false;
	if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(TargetRoot))
	{
		if (Prim->IsSimulatingPhysics())
		{
			bHeldWasSimulatingPhysics = true;
			Prim->SetSimulatePhysics(false);
		}
	}

	const FAttachmentTransformRules Rules(
		EAttachmentRule::KeepWorld,   // location -> preserves offset at grab moment
		EAttachmentRule::KeepWorld,   // rotation
		EAttachmentRule::KeepWorld,   // scale
		/*bWeldSimulatedBodies=*/ false);

	TargetRoot->AttachToComponent(OwnerRoot, Rules);
	HeldActor = Target;

	UE_LOG(LogGraspGrab, Log, TEXT("Grape Grabbed '%s' with hand %d"), *Target->GetName(), (int32)Hand);
}

void UGraspGrabComponent::ReleaseHeldActor()
{
	AActor* Held = HeldActor.Get();
	if (!Held)
	{
		HeldActor = nullptr;
		return;
	}

	if (USceneComponent* HeldRoot = Held->GetRootComponent())
	{
		const FDetachmentTransformRules Rules(
			EDetachmentRule::KeepWorld,
			EDetachmentRule::KeepWorld,
			EDetachmentRule::KeepWorld,
			/*bCallModify=*/ true);
		HeldRoot->DetachFromComponent(Rules);

		// Restore physics simulation if it was on before grab (so the object falls).
		// Or if AutoEnablePhysicsOnGrabbables is on, force-enable on release too —
		// covers the case where AutoSetup ran but the object was held before its first fall.
		if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(HeldRoot))
		{
			const bool bShouldSimulate = bHeldWasSimulatingPhysics || bAutoEnablePhysicsOnGrabbables;
			if (bShouldSimulate && !Prim->IsSimulatingPhysics())
			{
				Prim->SetSimulatePhysics(true);
			}
		}
	}

	UE_LOG(LogGraspGrab, Log, TEXT("Grape Released '%s' from hand %d"), *Held->GetName(), (int32)Hand);
	HeldActor = nullptr;
	bHeldWasSimulatingPhysics = false;
}

AActor* UGraspGrabComponent::FindNearestGrabbable(const FVector& Origin) const
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return nullptr;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(GraspGrab), /*bTraceComplex=*/ false);
	Params.AddIgnoredActor(Owner);

	TArray<FOverlapResult> Overlaps;
	const bool bHit = World->OverlapMultiByObjectType(
		Overlaps,
		Origin,
		FQuat::Identity,
		FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllDynamicObjects),
		FCollisionShape::MakeSphere(GrabRadius),
		Params);

	// Diagnostic: log every overlap hit and whether it passed the tag filter.
	// Fires once per grab attempt; cheap enough to always leave on for now.
	UE_LOG(LogGraspGrab, Log,
		TEXT("Grape [%s] overlap@(%.1f,%.1f,%.1f) r=%.1f -> %d hit(s)"),
		HandToStr(Hand), Origin.X, Origin.Y, Origin.Z, GrabRadius, Overlaps.Num());

	for (const FOverlapResult& Hit : Overlaps)
	{
		AActor* A = Hit.GetActor();
		if (!A) continue;
		UE_LOG(LogGraspGrab, Log,
			TEXT("  - '%s' class=%s hasTag(%s)=%s mob=%d dist=%.1f"),
			*A->GetName(),
			*A->GetClass()->GetName(),
			*GrabTag.ToString(),
			A->ActorHasTag(GrabTag) ? TEXT("YES") : TEXT("no"),
			A->GetRootComponent() ? (int32)A->GetRootComponent()->Mobility.GetValue() : -1,
			FVector::Dist(Origin, A->GetActorLocation()));
	}

	if (!bHit)
	{
		return nullptr;
	}

	AActor* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();

	for (const FOverlapResult& Hit : Overlaps)
	{
		AActor* HitActor = Hit.GetActor();
		if (!HitActor || HitActor == Owner)
		{
			continue;
		}
		if (!HitActor->ActorHasTag(GrabTag))
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(Origin, HitActor->GetActorLocation());
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			Nearest = HitActor;
		}
	}

	return Nearest;
}
