// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "AutomationWindowPrivatePCH.h"


#define LOCTEXT_NAMESPACE "AutomationTestItem"


/**
* Implements a Cell widget for the history objects of an automation report.
*/
class SAutomationHistoryCell
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SAutomationHistoryCell) { }
	SLATE_END_ARGS()

public:

	/**
	* Constructs the widget.
	*
	* @param InArgs The construction arguments.
	* @param InHistoryItem The history item we are providing the cell for.
	*/
	void Construct(const FArguments& InArgs, TSharedPtr<IAutomationReport> InHistoryItem);

	/** Rebuild the content of the history cell into our Content Area. */
	void RebuildContentArea();

public:

	// SWidget Interface

	virtual void Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime ) override;

private:

	/** A copy of the history items for us to note differences between this and the controller. */
	TArray<TSharedPtr<FAutomationHistoryItem>> HistoryCopy;

	/** The automation reports history information. */
	TSharedPtr<IAutomationReport> HistoryItem;

	/** The widget which holds the content for the History Cell. */
	TSharedPtr<SHorizontalBox> ContentArea;
};


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SAutomationHistoryCell::Construct(const FArguments& InArgs, TSharedPtr<IAutomationReport> InHistoryItem)
{
	HistoryItem = InHistoryItem;

	ContentArea = SNew(SHorizontalBox);
	RebuildContentArea();

	ChildSlot
	[
		ContentArea.ToSharedRef()
	];
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SAutomationHistoryCell::RebuildContentArea()
{
	// Clear the previous results before we update the cell again
	ContentArea->ClearChildren();

	// Create an overview of the previous results in icon form.
	const TArray<TSharedPtr<FAutomationHistoryItem>> History = HistoryItem->GetHistory();
	for (const TSharedPtr<FAutomationHistoryItem> HistoryItem : History)
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("Date"), FText::AsDateTime(HistoryItem->RunDate));

		const FSlateBrush* ResultIcon = nullptr;
		if (HistoryItem->RunResult == FAutomationHistoryItem::EAutomationHistoryResult::Errors)
		{
			Args.Add(TEXT("Result"), LOCTEXT("HasErrors", "had errors"));
			ResultIcon = FEditorStyle::GetBrush("Automation.Fail");
		}
		else if (HistoryItem->RunResult == FAutomationHistoryItem::EAutomationHistoryResult::Warnings)
		{
			Args.Add(TEXT("Result"), LOCTEXT("HasWarnings", "had warnings"));
			ResultIcon = FEditorStyle::GetBrush("Automation.Warning");
		}
		else
		{
			Args.Add(TEXT("Result"), LOCTEXT("WasSuccessful", "was successful"));
			ResultIcon = FEditorStyle::GetBrush("Automation.Success");
		}

		// Add the previous result as an icon representation to the cell
		ContentArea->AddSlot()
		[
			SNew(SImage)
			.Image(ResultIcon)
			.ToolTipText(FText::Format(LOCTEXT("ItemTooltip", "{Date} - This run {Result}!"), Args))
		];
	}
}

void SAutomationHistoryCell::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	const TArray<TSharedPtr<FAutomationHistoryItem>> TestHistory = HistoryItem->GetHistory();

	// if the test history has changed, reflect it in this cell
	if (TestHistory != HistoryCopy)
	{
		RebuildContentArea();
		HistoryCopy = TestHistory;
	}
}


/* SAutomationTestItem interface
 *****************************************************************************/

void SAutomationTestItem::Construct( const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView )
{
	TestStatus = InArgs._TestStatus;
	ColumnWidth = InArgs._ColumnWidth;
	HighlightText = InArgs._HighlightText;
	OnCheckedStateChangedDelegate = InArgs._OnCheckedStateChanged;

	SMultiColumnTableRow< TSharedPtr< FString > >::Construct( SMultiColumnTableRow< TSharedPtr< FString > >::FArguments(), InOwnerTableView );
}


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SWidget> SAutomationTestItem::GenerateWidgetForColumn( const FName& ColumnName )
{
	FSlateFontInfo ItemEditorFont = FEditorStyle::GetFontStyle(TEXT("NormalFont"));

	if( ColumnName == AutomationTestWindowConstants::Title)
	{
		return SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				//enabled/disabled check box
				SNew( SCheckBox )
				.IsChecked(this, &SAutomationTestItem::IsTestEnabled)
				.OnCheckStateChanged(this, &SAutomationTestItem::HandleTestingCheckbox_Click)
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				//this is where the tree is marked as expandable or not.
				SNew( SExpanderArrow, SharedThis( this ) )
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Center)
			[
				//name of the test
				SNew( STextBlock )
				.HighlightText( HighlightText )
				.Text( FText::FromString(TestStatus->GetDisplayNameWithDecoration()) )
			];
	}
	else if( ColumnName == AutomationTestWindowConstants::SmokeTest )
	{
		//icon to show if the test is considered fast or is the parent of a fast test
		return SNew( SImage) 
			.Image( this, &SAutomationTestItem::GetSmokeTestImage );
	}
	else if( ColumnName == AutomationTestWindowConstants::RequiredDeviceCount )
	{
		// Should we display an icon to indicate that this test "Requires" more than one participant?
		if( TestStatus->GetNumParticipantsRequired() > 1 )
		{
			TSharedPtr< SHorizontalBox > HBox = SNew( SHorizontalBox );
			if( TestStatus->GetTotalNumChildren() == 0 )
			{
				FFormatNamedArguments Args;
				Args.Add(TEXT("NumParticipantsRequired"), TestStatus->GetNumParticipantsRequired());

				// Display a network PC and the number which are required for this test.
				HBox->AddSlot()
					[
						SNew( SImage )
						.Image( FEditorStyle::GetBrush("Automation.Participant") )
					];
				HBox->AddSlot()
					[
						SNew( STextBlock )
						.Text( FText::Format( LOCTEXT( "NumParticipantsRequiredWrapper", "x{NumParticipantsRequired}" ), Args ) )
					];

				HBox->SetToolTipText(FText::Format(LOCTEXT("NumParticipantsRequiredMessage", "This test requires {NumParticipantsRequired} participants to be run."), Args));

			}
			else
			{
				HBox->AddSlot()
					.HAlign(HAlign_Center)
					[
						SNew( SImage )
						.Image( FEditorStyle::GetBrush("Automation.ParticipantsWarning") )
						.ToolTipText( LOCTEXT("ParticipantsWarningToolTip", "Some tests require multiple participants") )
					];
			}
			return HBox.ToSharedRef();
		}
	}
	else if( ColumnName == AutomationTestWindowConstants::Status )
	{
		TSharedRef<SHorizontalBox> HBox = SNew (SHorizontalBox);
		int32 NumClusters = FModuleManager::GetModuleChecked<IAutomationControllerModule>("AutomationController").GetAutomationController()->GetNumDeviceClusters();

		//for each cluster, display a status icon
		for (int32 ClusterIndex = 0; ClusterIndex < NumClusters; ++ClusterIndex)
		{
			//if this is a leaf test
			if (TestStatus->GetTotalNumChildren() == 0)
			{
				//for leaf tests
				HBox->AddSlot()
				.MaxWidth(ColumnWidth)
				.FillWidth(1.0)
				[			
					SNew(SBorder)
					.BorderImage( FEditorStyle::GetBrush("ErrorReporting.Box") )
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Padding( FMargin(3,0) )
					.BorderBackgroundColor( FSlateColor( FLinearColor( 1.0f, 0.0f, 1.0f, 0.0f ) ) )
					.ToolTipText( this, &SAutomationTestItem::GetTestToolTip, ClusterIndex )
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.AutoWidth()
						[
							//image when complete or un-run
							SNew( SImage )
							.Image( this, &SAutomationTestItem::ItemStatus_StatusImage, ClusterIndex )
							.Visibility( this, &SAutomationTestItem::ItemStatus_GetStatusVisibility, ClusterIndex, false )
						]
						+SHorizontalBox::Slot()
						.AutoWidth()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(16.0f)
							.HeightOverride(16.0f)
							[
								// Spinning Image while in process
								SNew(SSpinningImage)
								.Image( this, &SAutomationTestItem::ItemStatus_StatusImage, ClusterIndex )
								.Visibility(this, &SAutomationTestItem::ItemStatus_GetStatusVisibility, ClusterIndex, true)
							]
						]
					]
				];
			}
			else
			{
				//for internal tree nodes
				HBox->AddSlot()
				.MaxWidth(ColumnWidth)
				.FillWidth(1.0)
				[
					SNew(SBorder)
					.BorderImage( FEditorStyle::GetBrush("ErrorReporting.Box") )
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Center)
					.Padding( FMargin(3,0) )
					.BorderBackgroundColor( FSlateColor( FLinearColor( 1.0f, 0.0f, 1.0f, 0.0f ) ) )
					[
						//progress bar for percent of enabled children completed
						SNew(SProgressBar)
						.Percent( this, &SAutomationTestItem::ItemStatus_ProgressFraction, ClusterIndex )
						.FillColorAndOpacity(this, &SAutomationTestItem::ItemStatus_ProgressColor, ClusterIndex )
					]
				];
			}
		}
		return HBox;
	}
	else if (ColumnName == AutomationTestWindowConstants::History)
	{
		return SNew(SAutomationHistoryCell, TestStatus);
	}
	else if( ColumnName == AutomationTestWindowConstants::Timing )
	{
		return SNew( STextBlock )
		.Text( this, &SAutomationTestItem::ItemStatus_DurationText);
	}


	return SNullWidget::NullWidget;
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION


/* SAutomationTestItem Implementation
 *****************************************************************************/
const FSlateBrush* SAutomationTestItem::GetSmokeTestImage() const
{
	const FSlateBrush* ImageToUse = nullptr;
	if ( TestStatus->GetTestType() & EAutomationTestType::ATT_SmokeTest )
	{
		if ( TestStatus->IsParent() )
		{
			ImageToUse = FEditorStyle::GetBrush("Automation.SmokeTest");
		}
		else
		{
			ImageToUse = FEditorStyle::GetBrush("Automation.SmokeTestParent");
		}
	}

	return ImageToUse;
}


FText SAutomationTestItem::GetTestToolTip( int32 ClusterIndex ) const
{
	FText ToolTip;
	const int32 PassIndex = TestStatus->GetCurrentPassIndex(ClusterIndex);
	EAutomationState::Type TestState = TestStatus->GetState( ClusterIndex, PassIndex );
	if ( TestState == EAutomationState::NotRun )
	{
		ToolTip = LOCTEXT("TestToolTipNotRun", "Not Run");
	}
	else if( TestState == EAutomationState::NotEnoughParticipants )
	{
		ToolTip = LOCTEXT("ToolTipNotEnoughParticipants", "This test could not be completed as there were not enough participants.");
	}
	else
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("GameName"), FText::FromString(TestStatus->GetGameInstanceName(ClusterIndex)));

		if (TestState == EAutomationState::InProcess)
		{
			ToolTip = FText::Format(LOCTEXT("TestToolTipInProgress", "In progress on: {GameName}"), Args);
		}
		else if (TestState == EAutomationState::Success)
		{
			ToolTip = FText::Format(LOCTEXT("TestToolTipComplete", "Completed on: {GameName}"), Args);
		}
		else
		{
			ToolTip = FText::Format(LOCTEXT("TestToolTipFailed", "Failed on: {GameName}"), Args);
		}
	}
	return ToolTip;
}


ECheckBoxState SAutomationTestItem::IsTestEnabled() const
{
	return TestStatus->IsEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}


FSlateColor SAutomationTestItem::ItemStatus_BackgroundColor(const int32 ClusterIndex) const
{
	if (TestStatus->GetTotalNumChildren()==0)
	{
		const int32 PassIndex = TestStatus->GetCurrentPassIndex(ClusterIndex);
		EAutomationState::Type TestState = TestStatus->GetState(ClusterIndex,PassIndex);
		if (TestState == EAutomationState::Fail)
		{
			// Failure is marked by a red background.
			return FSlateColor( FLinearColor( 0.5f, 0.0f, 0.0f ) );
		}
		else if (TestState == EAutomationState::InProcess)
		{
			// In Process, yellow.
			return FSlateColor( FLinearColor( 0.5f, 0.5f, 0.0f ) );
		}
		else if (TestState == EAutomationState::Success)
		{
			// Success is marked by a green background.
			return FSlateColor( FLinearColor( 0.0f, 0.5f, 0.0f ) );
		}

		// Not Scheduled will receive this color which is to say no color since alpha is 0.
		return FSlateColor( FLinearColor( 1.0f, 0.0f, 1.0f, 0.0f ) );
	}
	else
	{
		// Not Scheduled will receive this color which is to say no color since alpha is 0.
		return FSlateColor( FLinearColor( 1.0f, 0.0f, 1.0f, 0.0f ) );
	}
}


FText SAutomationTestItem::ItemStatus_DurationText() const
{
	FText DurationText;
	float MinDuration;
	float MaxDuration;
	if (TestStatus->GetDurationRange(MinDuration, MaxDuration))
	{
		FNumberFormattingOptions Options;
		Options.MaximumFractionalDigits = 4;
		Options.MaximumIntegralDigits = 4;

		FFormatNamedArguments Args;
		Args.Add(TEXT("MinDuration"), MinDuration);
		Args.Add(TEXT("MaxDuration"), MaxDuration);

		//if there is a duration range
		if (MinDuration != MaxDuration)
		{
			DurationText = FText::Format(LOCTEXT("ItemStatusDurationRange", "{MinDuration}s - {MaxDuration}s"), Args);
		}
		else
		{
			DurationText = FText::Format(LOCTEXT("ItemStatusDuration", "{MinDuration}s"), Args);
		}
	}
	return DurationText;
}


EVisibility SAutomationTestItem::ItemStatus_GetStatusVisibility(const int32 ClusterIndex, const bool bForInProcessThrobber) const
{
	const int32 PassIndex = TestStatus->GetCurrentPassIndex(ClusterIndex);
	EAutomationState::Type TestState = TestStatus->GetState(ClusterIndex,PassIndex);
	bool bImageVisible = TestState != EAutomationState::InProcess;

	bool bFinalVisibility =  bForInProcessThrobber ? !bImageVisible : bImageVisible;

	return bFinalVisibility ? EVisibility::Visible : EVisibility::Collapsed;
}


FText SAutomationTestItem::ItemStatus_NumParticipantsRequiredText() const
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("NumParticipantsRequired"), TestStatus->GetNumParticipantsRequired());

	return FText::Format(LOCTEXT("NumParticipantsRequiredWrapper", "x{NumParticipantsRequired}"), Args);
}


FSlateColor SAutomationTestItem::ItemStatus_ProgressColor(const int32 ClusterIndex) const
{
	FAutomationCompleteState CompleteState;
	const int32 PassIndex = TestStatus->GetCurrentPassIndex(ClusterIndex);
	TestStatus->GetCompletionStatus(ClusterIndex,PassIndex, CompleteState);

	if (CompleteState.TotalEnabled > 0)
	{
		if (CompleteState.NumEnabledTestsFailed > 0)
		{
			// Failure is marked by a red background.
			return FSlateColor( FLinearColor( 1.0f, 0.0f, 0.0f ) );
		}
		else if((CompleteState.NumEnabledTestsPassed !=  CompleteState.TotalEnabled) || 
			(CompleteState.NumEnabledTestsWarnings > 0) ||
			(CompleteState.NumEnabledTestsCouldntBeRun > 0 ))
		{
			// In Process, yellow.
			return FSlateColor( FLinearColor( 1.0f, 1.0f, 0.0f ) );
		}
		else
		{
			// Success is marked by a green background.
			return FSlateColor( FLinearColor( 0.0f, 1.0f, 0.0f ) );
		}
	}

	// Not Scheduled will receive this color which is to say no color since alpha is 0.
	return FSlateColor( FLinearColor( 1.0f, 0.0f, 1.0f, 0.0f ) );
}


TOptional<float> SAutomationTestItem::ItemStatus_ProgressFraction(const int32 ClusterIndex) const
{
	FAutomationCompleteState CompleteState;
	const int32 PassIndex = TestStatus->GetCurrentPassIndex(ClusterIndex);
	TestStatus->GetCompletionStatus(ClusterIndex,PassIndex, CompleteState);

	uint32 TotalComplete = CompleteState.NumEnabledTestsPassed + CompleteState.NumEnabledTestsFailed + CompleteState.NumEnabledTestsCouldntBeRun;
	// Only show a percentage if there is something interesting to report
	if( (TotalComplete> 0) && (CompleteState.TotalEnabled > 0) )
	{
		return (float)TotalComplete/CompleteState.TotalEnabled;
	}
	// Return incomplete state
	return 0.0f;
}


const FSlateBrush* SAutomationTestItem::ItemStatus_StatusImage(const int32 ClusterIndex) const
{
	const int32 PassIndex = TestStatus->GetCurrentPassIndex(ClusterIndex);
	EAutomationState::Type TestState = TestStatus->GetState(ClusterIndex,PassIndex);

	const FSlateBrush* ImageToUse;
	switch( TestState )
	{
	case EAutomationState::Success:
		{
			FAutomationCompleteState CompleteState;
			TestStatus->GetCompletionStatus(ClusterIndex,PassIndex, CompleteState);
			//If there were ANY warnings in the results
			if (CompleteState.NumEnabledTestsWarnings || CompleteState.NumDisabledTestsWarnings)
			{
				ImageToUse = FEditorStyle::GetBrush("Automation.Warning");
			}
			else
			{
				ImageToUse = FEditorStyle::GetBrush("Automation.Success");
			}
		}
		break;

	case EAutomationState::Fail:
		ImageToUse = FEditorStyle::GetBrush("Automation.Fail");
		break;

	case EAutomationState::NotRun:
		{
			ImageToUse = FEditorStyle::GetBrush("Automation.NotRun");
		}
		break;

	case EAutomationState::NotEnoughParticipants:
		ImageToUse = FEditorStyle::GetBrush("Automation.NotEnoughParticipants");
		break;

	default:
	case EAutomationState::InProcess:
		ImageToUse = FEditorStyle::GetBrush("Automation.InProcess");
		break;
	}

	return ImageToUse;
}


/* SAutomationTestitem event handlers
 *****************************************************************************/

void SAutomationTestItem::HandleTestingCheckbox_Click(ECheckBoxState)
{
	OnCheckedStateChangedDelegate.ExecuteIfBound(TestStatus);
}


#undef LOCTEXT_NAMESPACE
