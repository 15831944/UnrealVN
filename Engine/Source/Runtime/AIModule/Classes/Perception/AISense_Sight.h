// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Perception/AIPerceptionSystem.h"
#include "Perception/AISense.h"
#include "AISense_Sight.generated.h"

class IAISightTargetInterface;
class UAISenseConfig_Sight;
class UAISense_Sight;

namespace ESightPerceptionEventName
{
	enum Type
	{
		Undefined,
		GainedSight,
		LostSight
	};
}

USTRUCT()
struct AIMODULE_API FAISightEvent
{
	GENERATED_USTRUCT_BODY()

	typedef UAISense_Sight FSenseClass;

	float Age;
	ESightPerceptionEventName::Type EventType;	

	UPROPERTY()
	AActor* SeenActor;

	UPROPERTY()
	AActor* Observer;

	FAISightEvent(){}

	FAISightEvent(AActor* InSeenActor, AActor* InObserver, ESightPerceptionEventName::Type InEventType)
		: Age(0.f), EventType(InEventType), SeenActor(InSeenActor), Observer(InObserver)
	{
	}
};

struct FAISightTarget
{
	typedef FName FTargetId;
	static const FTargetId InvalidTargetId;

	TWeakObjectPtr<AActor> Target;
	IAISightTargetInterface* SightTargetInterface;
	FGenericTeamId TeamId;
	FTargetId TargetId;

	FAISightTarget(AActor* InTarget = NULL, FGenericTeamId InTeamId = FGenericTeamId::NoTeam);

	FORCEINLINE FVector GetLocationSimple() const
	{
		return Target.IsValid() ? Target->GetActorLocation() : FVector::ZeroVector;
	}

	FORCEINLINE const AActor* GetTargetActor() const { return Target.Get(); }
};

struct FAISightQuery
{
	FPerceptionListenerID ObserverId;
	FAISightTarget::FTargetId TargetId;

	float Age;
	float Score;
	float Importance;

	uint32 bLastResult : 1;

	FAISightQuery(FPerceptionListenerID ListenerId = FPerceptionListenerID::InvalidID(), FAISightTarget::FTargetId Target = FAISightTarget::InvalidTargetId)
		: ObserverId(ListenerId), TargetId(Target), Age(0), Score(0), Importance(0), bLastResult(false)
	{
	}

	void RecalcScore()
	{
		Score = Age + Importance;
	}

	class FSortPredicate
	{
	public:
		FSortPredicate()
		{}

		bool operator()(const FAISightQuery& A, const FAISightQuery& B) const
		{
			return A.Score > B.Score;
		}
	};
};

UCLASS(ClassGroup=AI, config=Game)
class AIMODULE_API UAISense_Sight : public UAISense
{
	GENERATED_UCLASS_BODY()

public:
	struct FDigestedSightProperties
	{
		float PeripheralVisionAngleCos;
		float SightRadiusSq;
		float LoseSightRadiusSq;
		uint8 AffiliationFlags;

		FDigestedSightProperties();
		FDigestedSightProperties(const UAISenseConfig_Sight& SenseConfig);
	};	
	
	//TChunkedArray<FDigestedSightProperties> DigestedProps

	TMap<FAISightTarget::FTargetId, FAISightTarget> ObservedTargets;
	TMap<FPerceptionListenerID, FDigestedSightProperties> DigestedProperties;

	TArray<FAISightQuery> SightQueryQueue;

protected:
	UPROPERTY(config)
	int32 MaxTracesPerTick;

	UPROPERTY(config)
	float HighImportanceQueryDistanceThreshold;

	float HighImportanceDistanceSquare;

	UPROPERTY(config)
	float MaxQueryImportance;

	UPROPERTY(config)
	float SightLimitQueryImportance;

public:

	virtual void PostInitProperties() override;
	
	void RegisterEvent(const FAISightEvent& Event);	

	virtual void RegisterSource(AActor& SourceActors) override;
	
protected:
	virtual float Update() override;

	void OnNewListenerImpl(const FPerceptionListener& NewListener);
	void OnListenerUpdateImpl(const FPerceptionListener& UpdatedListener);
	void OnListenerRemovedImpl(const FPerceptionListener& UpdatedListener);	

	void GenerateQueriesForListener(const FPerceptionListener& Listener, const FDigestedSightProperties& PropertyDigest);

	enum FQueriesOperationPostProcess
	{
		DontSort,
		Sort
	};
	void RemoveAllQueriesByListener(const FPerceptionListener& Listener, FQueriesOperationPostProcess PostProcess);
	void RemoveAllQueriesToTarget(const FName& TargetId, FQueriesOperationPostProcess PostProcess);

	/** returns information whether new LoS queries have been added */
	bool RegisterTarget(AActor& TargetActor, FQueriesOperationPostProcess PostProcess);

	FORCEINLINE void SortQueries() { SightQueryQueue.Sort(FAISightQuery::FSortPredicate()); }

	float CalcQueryImportance(const FPerceptionListener& Listener, const FVector& TargetLocation, const float SightRadiusSq) const;

public:
#if !UE_BUILD_SHIPPING
	//----------------------------------------------------------------------//
	// DEBUG
	//----------------------------------------------------------------------//
	FString GetDebugLegend() const;
	static FColor GetDebugSightRangeColor() { return FColor::Green; }
	static FColor GetDebugLoseSightColor() { return FColorList::NeonPink; }
#endif // !UE_BUILD_SHIPPING
};
