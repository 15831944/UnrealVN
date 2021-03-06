// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "UnrealEd.h"

#include "FoliageEditModule.h"

#include "FoliageEditActions.h"

void FFoliageEditCommands::RegisterCommands()
{
	UI_COMMAND( SetPaint, "Paint", "Paint", EUserInterfaceActionType::ToggleButton, FInputGesture() );
	UI_COMMAND( SetReapplySettings, "Reapply", "Reapply settings to instances", EUserInterfaceActionType::ToggleButton, FInputGesture() );
	UI_COMMAND( SetSelect, "Select", "Select", EUserInterfaceActionType::ToggleButton, FInputGesture() );
	UI_COMMAND( SetLassoSelect, "Lasso", "Lasso Select", EUserInterfaceActionType::ToggleButton, FInputGesture() );
	UI_COMMAND( SetPaintBucket, "Fill", "Paint Bucket", EUserInterfaceActionType::ToggleButton, FInputGesture() );

	UI_COMMAND( SetNoSettings, "Hide Details", "Hide details.", EUserInterfaceActionType::ToggleButton, FInputGesture() );
	UI_COMMAND( SetPaintSettings, "Show Painting settings", "Show painting settings.", EUserInterfaceActionType::ToggleButton, FInputGesture() );
	UI_COMMAND( SetClusterSettings, "Show Instance settings", "Show settings for placed instances.", EUserInterfaceActionType::ToggleButton, FInputGesture() );

	UI_COMMAND( RemoveFoliageType, "Remove", "Remove all associated foliage instances", EUserInterfaceActionType::Button, FInputGesture() );
	UI_COMMAND( ShowFoliageTypeInCB, "Show in Content Browser", "Show asset in Content Browser.", EUserInterfaceActionType::Button, FInputGesture() );
	UI_COMMAND( SelectAllInstances, "Select All Instances", "Select all instances of foliage type.", EUserInterfaceActionType::Button, FInputGesture());
	UI_COMMAND( DeselectAllInstances, "Deselect All Instances", "Deselect all instances of foliage type.", EUserInterfaceActionType::Button, FInputGesture());
}

