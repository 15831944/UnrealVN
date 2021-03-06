// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "FoliagePrivate.h"
#include "ProceduralFoliageComponent.h"
#include "ProceduralFoliageTile.h"
#include "InstancedFoliage.h"
#include "InstancedFoliageActor.h"
#include "ProceduralFoliage.h"

#include "Async/Async.h"

#define LOCTEXT_NAMESPACE "ProceduralFoliage"

UProceduralFoliageComponent::UProceduralFoliageComponent(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
	Overlap = 0.f;
	ProceduralGuid = FGuid::NewGuid();
#if WITH_EDITORONLY_DATA
	bHideDebugTiles = true;
#endif
}

void CopyTileInstances(const UProceduralFoliageTile* FromTile, UProceduralFoliageTile* ToTile, const FBox2D& InnerLocalAABB, const FTransform& ToLocalTM, const float Overlap)
{
	const FBox2D OuterLocalAABB(InnerLocalAABB.Min, InnerLocalAABB.Max + FVector2D(Overlap, Overlap));
	TArray<FProceduralFoliageInstance*> ToInstances;
	FromTile->GetInstancesInAABB(OuterLocalAABB, ToInstances, false);
	ToTile->AddInstances(ToInstances, ToLocalTM, InnerLocalAABB);
}

FBox2D GetTileRegion(const int32 X, const int32 Y, const int32 CountX, const int32 CountY, const float InnerSize, const float Overlap)
{
	const FVector2D Left(-Overlap, Overlap);
	const FVector2D Bottom(Overlap, -Overlap);

	FBox2D Region(FVector2D(Overlap,Overlap), FVector2D(InnerSize + Overlap, InnerSize + Overlap));
	if (X == 0)
	{
		Region += Left;
	}

	if (Y == 0)
	{
		Region += Bottom;
	}

	return Region;
}
void UProceduralFoliageComponent::GetTilesLayout(int32& MinXIdx, int32& MinYIdx, int32& NumX, int32& NumY, float& HalfHeight) const
{
	if(UBrushComponent* Brush = SpawningVolume->GetBrushComponent())
	{
		const FVector MinPosition = Brush->Bounds.GetBox().Min + Overlap;
		const FVector MaxPosition = Brush->Bounds.GetBox().Max - Overlap;

		//we want to find the bottom left tile that contains this MinPosition
		MinXIdx = FMath::FloorToInt(MinPosition.X / ProceduralFoliage->TileSize);
		MinYIdx = FMath::FloorToInt(MinPosition.Y / ProceduralFoliage->TileSize);

		const int32 MaxXIdx = FMath::FloorToInt(MaxPosition.X / ProceduralFoliage->TileSize);
		const int32 MaxYIdx = FMath::FloorToInt(MaxPosition.Y / ProceduralFoliage->TileSize);

		NumX = (MaxXIdx - MinXIdx) + 1;
		NumY = (MaxYIdx - MinYIdx) + 1;

		HalfHeight = Brush->Bounds.GetBox().GetExtent().Z;
	}
}

FVector UProceduralFoliageComponent::GetWorldPosition() const
{
	UBrushComponent* Brush = SpawningVolume->GetBrushComponent();
	if (Brush == nullptr || ProceduralFoliage == nullptr)
	{
		return FVector::ZeroVector;
	}
	
	int32 X1 = 0;
	int32 Y1 = 0;
	int32 NumX, NumY;
	float HalfHeight;
	GetTilesLayout(X1, Y1, NumX, NumY, HalfHeight);

	return FVector(X1 * ProceduralFoliage->TileSize, Y1 * ProceduralFoliage->TileSize, Brush->Bounds.Origin.Z);
}

bool UProceduralFoliageComponent::SpawnTiles(TArray<FDesiredFoliageInstance>& OutInstances)
{
#if WITH_EDITOR
	if (ProceduralFoliage && SpawningVolume && SpawningVolume->GetBrushComponent())
	{
		/** Constants for laying out the overlapping tile grid*/
		const float InnerTileSize = ProceduralFoliage->TileSize;
		const FVector2D InnerTileV(InnerTileSize, InnerTileSize);
		const FVector2D InnerTileX(InnerTileSize, 0.f);
		const FVector2D InnerTileY(0.f, InnerTileSize);

		const float OuterTileSize = ProceduralFoliage->TileSize + Overlap;
		const FVector2D OuterTileV(OuterTileSize, OuterTileSize);
		const FVector2D OuterTileX(OuterTileSize, 0.f);
		const FVector2D OuterTileY(0.f, OuterTileSize);

		const FVector2D OverlapX(Overlap, 0.f);
		const FVector2D OverlapY(0.f, Overlap);

		TArray<TFuture< TArray<FDesiredFoliageInstance>* >> Futures;
		
		int32 XOffset, YOffset, NumTilesX, NumTilesY;
		float HalfHeight;
		GetTilesLayout(XOffset, YOffset, NumTilesX, NumTilesY, HalfHeight);

		FBodyInstance* VolumeBodyInstance = SpawningVolume->GetBrushComponent()->GetBodyInstance();

		FThreadSafeCounter LastCancel;
		const int32 LastCanelInit = LastCancel.GetValue();
		FThreadSafeCounter* LastCancelPtr = &LastCancel;

		const FVector WorldPosition = GetWorldPosition();
		ProceduralFoliage->SimulateIfNeeded();

		for (int32 X = 0; X < NumTilesX; ++X)
		{
			for (int32 Y = 0; Y < NumTilesY; ++Y)
			{
				//We have to get the tiles and create new one to build on main thread
				const UProceduralFoliageTile* Tile = ProceduralFoliage->GetRandomTile(X + XOffset, Y + YOffset);
				if (Tile == nullptr)	//simulation was cancelled or failed
				{
					return false;
				}

				const UProceduralFoliageTile* RightTile = (X + 1 < NumTilesX) ? ProceduralFoliage->GetRandomTile(X + XOffset + 1, Y + YOffset) : nullptr;
				const UProceduralFoliageTile* TopTile = (Y + 1 < NumTilesY) ? ProceduralFoliage->GetRandomTile(X + XOffset, Y + YOffset+ 1) : nullptr;
				const UProceduralFoliageTile* TopRightTile = (RightTile && TopTile) ? ProceduralFoliage->GetRandomTile(X + XOffset + 1, Y + YOffset + 1) : nullptr;

				UProceduralFoliageTile* JTile = ProceduralFoliage->CreateTempTile();

				Futures.Add(Async<TArray<FDesiredFoliageInstance>*>(EAsyncExecution::ThreadPool, [=]()
				{
					if (LastCancelPtr->GetValue() != LastCanelInit)
					{
						return new TArray<FDesiredFoliageInstance>();
					}

					const FVector OrientedOffset = FVector(X, Y, 0.f) * FVector(InnerTileSize, InnerTileSize, 0.f);
					const FTransform TileTM(OrientedOffset + WorldPosition);

					//copy the inner tile
					const FBox2D InnerBox = GetTileRegion(X, Y, NumTilesX, NumTilesY, InnerTileSize, Overlap);
					CopyTileInstances(Tile, JTile, InnerBox, FTransform::Identity, Overlap);


					if (RightTile)
					{
						//Add overlap to the right
						const FBox2D RightBox(FVector2D(-Overlap, InnerBox.Min.Y), FVector2D(Overlap, InnerBox.Max.Y));
						const FTransform RightTM(FVector(InnerTileSize, 0.f, 0.f));
						CopyTileInstances(RightTile, JTile, RightBox, RightTM, Overlap);
					}

					if (TopTile)
					{
						//Add overlap to the top
						const FBox2D TopBox(FVector2D(InnerBox.Min.X, -Overlap), FVector2D(InnerBox.Max.X, Overlap));
						const FTransform TopTM(FVector(0.f, InnerTileSize, 0.f));
						CopyTileInstances(TopTile, JTile, TopBox, TopTM, Overlap);
					}

					if (TopRightTile)
					{
						//Add overlap to the top right
						const FBox2D TopRightBox(FVector2D(-Overlap, -Overlap), FVector2D(Overlap, Overlap));
						const FTransform TopRightTM(FVector(InnerTileSize, InnerTileSize, 0.f));
						CopyTileInstances(TopRightTile, JTile, TopRightBox, TopRightTM, Overlap);
					}

					TArray<FDesiredFoliageInstance>* DesiredInstances = new TArray<FDesiredFoliageInstance>();
					JTile->InstancesToArray();
					JTile->CreateInstancesToSpawn(*DesiredInstances, TileTM, ProceduralGuid, HalfHeight, VolumeBodyInstance);
					JTile->Empty();

					return DesiredInstances;
					
				})
				);
			}
		}

		const FText StatusMessage = LOCTEXT("PlaceProceduralFoliage", "Placing ProceduralFoliage...");
		const FText CancelMessage = LOCTEXT("PlaceProceduralFoliageCancel", "Cancelling ProceduralFoliage...");
		GWarn->BeginSlowTask(StatusMessage, true, true);


		int32 FutureIdx = 0;
		bool bCancelled = false;
		for (int X = 0; X < NumTilesX; ++X)
		{
			for (int Y = 0; Y < NumTilesY; ++Y)
			{
				bool bFirstTime = true;
				while (Futures[FutureIdx].WaitFor(FTimespan(0, 0, 0, 0, 100)) == false || bFirstTime)
				{
					if (GWarn->ReceivedUserCancel() && bCancelled == false)
					{
						LastCancel.Increment();
						bCancelled = true;
					}

					if (bCancelled)
					{
						GWarn->StatusUpdate(Y + X * NumTilesY, NumTilesX * NumTilesY, CancelMessage);
					}
					else
					{
						GWarn->StatusUpdate(Y + X * NumTilesY, NumTilesX * NumTilesY, StatusMessage);
					}

					bFirstTime = false;
				}

				TArray<FDesiredFoliageInstance>* DesiredInstances = Futures[FutureIdx++].Get();
				OutInstances.Append(*DesiredInstances);
				delete DesiredInstances;
			}
		}

		GWarn->EndSlowTask();

		return !bCancelled;
	}
#endif
	return false;

}

bool UProceduralFoliageComponent::SpawnProceduralContent(TArray <FDesiredFoliageInstance>& OutInstances)
{
#if WITH_EDITOR
	
	if (SpawnTiles(OutInstances))
	{
		RemoveProceduralContent();
		return true;
	}

#endif

	return false;
}

void UProceduralFoliageComponent::RemoveProceduralContent()
{
#if WITH_EDITOR
	UWorld* World = GetWorld();

	for (ULevel* Level : World->GetLevels())
	{
		if (Level)
		{
			AInstancedFoliageActor* IFA = AInstancedFoliageActor::GetInstancedFoliageActorForLevel(Level);
			if (IFA)
			{
				IFA->DeleteInstancesForProceduralFoliageComponent(this);
			}
		}
	}
#endif
}

#undef LOCTEXT_NAMESPACE