#include "HandGraspVisualComponent.h"

#include "GraspGrabComponent.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Actor.h"
#include "Components/ChildActorComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogHandGraspVisual, Log, All);

UHandGraspVisualComponent::UHandGraspVisualComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork; // after hand pose tick
	bAutoActivate = true;
}

void UHandGraspVisualComponent::BeginPlay()
{
	Super::BeginPlay();
	SmoothedGrasp = 0.0f;
	ResolveReferences();
	RebuildMaterialInstance();
}

void UHandGraspVisualComponent::ResolveReferences()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	if (!GraspSource)
	{
		GraspSource = Owner->FindComponentByClass<UGraspGrabComponent>();
	}

	if (!TargetMesh)
	{
		TargetMesh = Owner->FindComponentByClass<UMeshComponent>();

		// Fallback: look into ChildActor components (ChildActor-style hand rigs).
		if (!TargetMesh)
		{
			TArray<UChildActorComponent*> ChildActorComps;
			Owner->GetComponents<UChildActorComponent>(ChildActorComps);
			for (UChildActorComponent* CAC : ChildActorComps)
			{
				if (CAC && CAC->GetChildActor())
				{
					TargetMesh = CAC->GetChildActor()->FindComponentByClass<UMeshComponent>();
					if (TargetMesh) break;
				}
			}
		}
	}

	if (!GraspSource)
	{
		UE_LOG(LogHandGraspVisual, Warning,
			TEXT("HandGraspVisual on '%s': no UGraspGrabComponent found -- hand color will stay at ColorA."),
			*Owner->GetName());
	}
	if (!TargetMesh)
	{
		UE_LOG(LogHandGraspVisual, Warning,
			TEXT("HandGraspVisual on '%s': no UMeshComponent found on owner or child actors."),
			*Owner->GetName());
	}
}

void UHandGraspVisualComponent::RebuildMaterialInstance()
{
	DynamicMID = nullptr;

	if (!TargetMesh)
	{
		return;
	}

	// Wrap whatever material is currently on the slot in a MID. Passing nullptr as
	// the source material tells the engine to use the slot's existing assignment.
	DynamicMID = TargetMesh->CreateDynamicMaterialInstance(MaterialSlot, nullptr);

	PushStaticParams();
}

void UHandGraspVisualComponent::PushStaticParams()
{
	if (!DynamicMID)
	{
		return;
	}
	DynamicMID->SetVectorParameterValue(ColorAParamName, ColorA);
	DynamicMID->SetVectorParameterValue(ColorBParamName, ColorB);
	DynamicMID->SetScalarParameterValue(GraspParamName,  SmoothedGrasp);
}

void UHandGraspVisualComponent::SetColorA(FLinearColor NewColor)
{
	ColorA = NewColor;
	if (DynamicMID)
	{
		DynamicMID->SetVectorParameterValue(ColorAParamName, ColorA);
	}
}

void UHandGraspVisualComponent::SetColorB(FLinearColor NewColor)
{
	ColorB = NewColor;
	if (DynamicMID)
	{
		DynamicMID->SetVectorParameterValue(ColorBParamName, ColorB);
	}
}

void UHandGraspVisualComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!DynamicMID)
	{
		// Retry once per tick until the hand rig is set up (Child Actors sometimes
		// construct a frame late).
		ResolveReferences();
		if (TargetMesh)
		{
			RebuildMaterialInstance();
		}
		if (!DynamicMID)
		{
			return;
		}
	}

	float TargetGrasp = 0.0f;
	if (ManualGraspOverride >= 0.0f)
	{
		TargetGrasp = FMath::Clamp(ManualGraspOverride, 0.0f, 1.0f);
	}
	else if (GraspSource)
	{
		TargetGrasp = FMath::Clamp(GraspSource->CurrentGraspStrength, 0.0f, 1.0f);
	}

	const float Alpha = FMath::Clamp(SmoothingAlpha, 0.0f, 1.0f);
	SmoothedGrasp = FMath::Lerp(SmoothedGrasp, TargetGrasp, Alpha);

	DynamicMID->SetScalarParameterValue(GraspParamName, SmoothedGrasp);
}
