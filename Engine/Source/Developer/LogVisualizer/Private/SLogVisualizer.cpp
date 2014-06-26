// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "LogVisualizerPCH.h"
#include "SLogBar.h"
#include "Debug/LogVisualizerCameraController.h"
#include "Debug/ReporterGraph.h"
#include "MainFrame.h"
#include "DesktopPlatformModule.h"
#include "Json.h"
#include "Editor/UnrealEd/Classes/Editor/EditorEngine.h"

#if WITH_EDITOR
#	include "Editor/UnrealEd/Public/EditorComponents.h"
#	include "Editor/UnrealEd/Public/EditorReimportHandler.h"
#	include "Editor/UnrealEd/Public/TexAlignTools.h"
#	include "Editor/UnrealEd/Public/TickableEditorObject.h"
#	include "UnrealEdClasses.h"
#	include "Editor/UnrealEd/Public/Editor.h"
#	include "Editor/UnrealEd/Public/EditorViewportClient.h"
#endif

#include "GameplayDebuggingComponent.h"

#include "SFilterList.h"

#if ENABLE_VISUAL_LOG

#define LOCTEXT_NAMESPACE "SLogVisualizer"

const FName SLogVisualizer::NAME_LogName = TEXT("LogName");
const FName SLogVisualizer::NAME_StartTime = TEXT("StartTime");
const FName SLogVisualizer::NAME_EndTime = TEXT("EndTime");
const FName SLogVisualizer::NAME_LogTimeSpan = TEXT("LogTimeSpan");

namespace LogVisualizer
{
	static const FString LogFileExtensionPure = TEXT("vlog");
	static const FString LogFileDescription = LOCTEXT("FileTypeDescription", "Visual Log File").ToString();
	static const FString LogFileExtension = FString::Printf(TEXT("*.%s"), *LogFileExtensionPure);
	static const FString FileTypes = FString::Printf( TEXT("%s (%s)|%s"), *LogFileDescription, *LogFileExtension, *LogFileExtension );
}

FColor SLogVisualizer::ColorPalette[] = {
	FColor(0xff00A480),
	FColorList::Aquamarine,
	FColorList::Cyan,
	FColorList::Brown,
	FColorList::Green,
	FColorList::Orange,
	FColorList::Magenta,
	FColorList::BrightGold,
	FColorList::NeonBlue,
	FColorList::MediumSlateBlue,
	FColorList::SpicyPink,
	FColor(0xff62E200),
	FColor(0xff1F7B67),
	FColor(0xff62AA2A),
	FColor(0xff70227E),
	FColor(0xff006B53),
	FColor(0xff409300),
	FColor(0xff5D016D),
	FColor(0xff34D2AF),
	FColor(0xff8BF13C),
	FColor(0xffBC38D3),
	FColor(0xff5ED2B8),
	FColor(0xffA6F16C),
	FColor(0xffC262D3),
	FColor(0xff0F4FA8),
	FColor(0xff00AE68),
	FColor(0xffDC0055),
	FColor(0xff284C7E),
	FColor(0xff21825B),
	FColor(0xffA52959),
	FColor(0xff05316D),
	FColor(0xff007143),
	FColor(0xff8F0037),
	FColor(0xff4380D3),
	FColor(0xff36D695),
	FColor(0xffEE3B80),
	FColor(0xff6996D3),
	FColor(0xff60D6A7),
	FColor(0xffEE6B9E)
};

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SLogVisualizer::Construct(const FArguments& InArgs, FLogVisualizer* InLogVisualizer)
{
	LogVisualizer = InLogVisualizer;
	SortBy = ELogsSortMode::ByName;
	LogEntryIndex = INDEX_NONE;
	SelectedLogIndex = INDEX_NONE;
	LogsStartTime = FLT_MAX;
	LogsEndTime = -FLT_MAX;
	ScrollbarOffset = 0.f;
	ZoomSliderValue = 0.f;
	LastBarsOffset = 0.f;
	MinZoom = 1.0f;
	MaxZoom = 20.0f;
	CurrentViewedTime = 0.f;
	bDrawLogEntriesPath = true;
	bIgnoreTrivialLogs = true;

	UsedCategories.Empty();

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)
		
			// Toolbar
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew( SOverlay )
				+ SOverlay::Slot()
				[
					SNew(SHorizontalBox)
					// Record button
					+SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(1)
					.AspectRatio()
					[
						SNew(SButton)
						.OnClicked(this, &SLogVisualizer::OnRecordButtonClicked)
						.Content()
						[
							SNew(SImage)
							.Image(this, &SLogVisualizer::GetRecordButtonBrush)
						]
					]
					// 'Pause' toggle button
					+SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(1)
					.AspectRatio()
					[
						SNew(SCheckBox)
						.Style(FEditorStyle::Get(), "ToggleButtonCheckbox")
						.OnCheckStateChanged(this, &SLogVisualizer::OnPauseChanged)
						.IsChecked(this, &SLogVisualizer::GetPauseState)
						.Content()
						[
							SNew(SImage)
							.Image(FEditorStyle::GetBrush("LogVisualizer.Pause"))
						]
					]
					// 'Camera' toggle button
					+SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(1)
					.AspectRatio()
					[
						SNew(SCheckBox)
						.Style(FEditorStyle::Get(), "ToggleButtonCheckbox")
						.OnCheckStateChanged(this, &SLogVisualizer::OnToggleCamera)
						.IsChecked(this, &SLogVisualizer::GetToggleCameraState)
						.Content()
						[
							SNew(SImage)
							.Image(FEditorStyle::GetBrush("LogVisualizer.Camera"))
						]
					]
					+SHorizontalBox::Slot()
					.MaxWidth(3)
					.Padding(1)
					.AspectRatio()
					[
						SNew(SSeparator)
						.Orientation(Orient_Vertical)
					]
					// 'Save' function
					+SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(1)
					.AspectRatio()
					[
						SNew(SButton)
						.OnClicked(this, &SLogVisualizer::OnSave)
						.Content()
						[
							SNew(SImage)
							.Image(FEditorStyle::GetBrush("LogVisualizer.Save"))
						]
					]
					// 'Load' function
					+SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(1)
					.AspectRatio()
					[
						SNew(SButton)
						.OnClicked(this, &SLogVisualizer::OnLoad)
						.Content()
						[
							SNew(SImage)
							.Image(FEditorStyle::GetBrush("LogVisualizer.Load"))
						]
					]
					// 'Remove' function
					+SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(1)
					.AspectRatio()
					[
						SNew(SButton)
						.OnClicked(this, &SLogVisualizer::OnRemove)
						.Content()
						[
							SNew(SImage)
							.Image(FEditorStyle::GetBrush("LogVisualizer.Remove"))
						]
					]
					+SHorizontalBox::Slot()
					.MaxWidth(3)
					.Padding(1)
					.AspectRatio()
					[
						SNew(SSeparator)
						.Orientation(Orient_Vertical)
					]
				]
				+SOverlay::Slot()
				.HAlign(HAlign_Right)
				.Padding(4)
				[
					SNew(SVerticalBox)
					+SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SCheckBox)
						.OnCheckStateChanged(this, &SLogVisualizer::OnDrawLogEntriesPathChanged)
						.IsChecked(this, &SLogVisualizer::GetDrawLogEntriesPathState)
						.Content()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("VisLogDrawLogsPath", "Draw Log\'s path"))
							.ToolTipText(LOCTEXT("VisLogDrawLogsPathTooltip", "Toggle whether path of composed of log entries\' locations"))
						]
					]
					+SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SCheckBox)
						.OnCheckStateChanged(this, &SLogVisualizer::OnIgnoreTrivialLogs)
						.IsChecked(this, &SLogVisualizer::GetIgnoreTrivialLogs)
						.Content()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("VisLogIgnoreTrivialLogs", "Ignore trivial logs"))
							.ToolTipText(LOCTEXT("VisLogIgnoreTrivialLogsTooltip", "Whether to show trivial logs, i.e. the ones with only one entry."))
						]
					]
				]
			]

			// Filters
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STutorialWrapper, TEXT("CategoryFilters"))
				[
					SAssignNew(FilterListPtr, SLogFilterList)
					.OnFilterChanged(this, &SLogVisualizer::OnLogCategoryFiltersChanged)
					/*.OnGetContextMenu(this, &SLogVisualizer::GetFilterContextMenu)*/
					/*.FrontendFilters(FrontendFilters)*/
				]
			]

			+SVerticalBox::Slot()
			.FillHeight(5)
			[
				SNew(SSplitter)
				.Orientation(Orient_Vertical)

				+SSplitter::Slot()
				[
					SNew(SBorder)
					.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
					.Padding(1.0)
					[
						SAssignNew(LogsListWidget, SListView< TSharedPtr<FLogsListItem> >)
						.ItemHeight(20)
						// Called when the user double-clicks with LMB on an item in the list
						.OnMouseButtonDoubleClick(this, &SLogVisualizer::OnListDoubleClick)
						.ListItemsSource(&LogsList)
						.SelectionMode(ESelectionMode::Multi)
						.OnGenerateRow(this, &SLogVisualizer::LogsListGenerateRow)
						.OnSelectionChanged(this, &SLogVisualizer::LogsListSelectionChanged)
						.HeaderRow(
							SNew(SHeaderRow)
							// ID
							+SHeaderRow::Column(NAME_LogName)
							.SortMode(this, &SLogVisualizer::GetLogsSortMode)
							.OnSort(this, &SLogVisualizer::OnSortByChanged)
							.HAlignCell(HAlign_Left)
							.FillWidth(0.25f)
							[
								SNew(SHorizontalBox)
								+SHorizontalBox::Slot()
								.AutoWidth()
								.HAlign(HAlign_Left)
								.Padding(0.0, 2.0)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("VisLogName", "Log Subject"))
								]
								+SHorizontalBox::Slot()
								.AutoWidth()
								.Padding(5,0)
								[
									SAssignNew(LogNameFilterBox, SEditableTextBox)
									.SelectAllTextWhenFocused(true)
									.OnTextCommitted(this, &SLogVisualizer::FilterTextCommitted)
									.MinDesiredWidth(170.f)
									.RevertTextOnEscape(true)
								]
							]
							+SHeaderRow::Column(NAME_LogTimeSpan)
							/*.OnSort(this, &SLogVisualizer::OnSortByChanged)*/
							.VAlignCell(VAlign_Center)
							[
								SNew(SVerticalBox)
								+SVerticalBox::Slot()
								.AutoHeight()
								[
									SNew(STextBlock)
									.Text(LOCTEXT("VisLogTimeSpan", "Overview"))
									.ToolTipText(LOCTEXT("VisLogTimeSpanTooltip", "Mouse-over to see timestamp, click to show log entry"))
								]
							]
						)
					]
				]
				+SSplitter::Slot()		
				[					
					SNew(SBorder)
					.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
					.Padding(1.0)
					[
						SNew(SVerticalBox)

						+SVerticalBox::Slot()
						.AutoHeight()
						.MaxHeight(60)
						[
							SAssignNew( Timeline, STimeline )
							.MinValue(0.0f)
							.MaxValue(100.0f)
							.FixedLabelSpacing(100.f)
						]

						+SVerticalBox::Slot()
						.AutoHeight() 
						.Padding( 2 )
						.VAlign( VAlign_Fill )
						[
							SAssignNew( ScrollBar, SScrollBar )
							.Orientation( Orient_Horizontal )
							.OnUserScrolled( this, &SLogVisualizer::OnZoomScrolled )
						]

						+SVerticalBox::Slot()
						.AutoHeight() 
						.Padding( 2 )
						[
							SAssignNew(ZoomSlider, SSlider)
								.Value( this, &SLogVisualizer::GetZoomValue )
								.OnValueChanged( this, &SLogVisualizer::OnSetZoomValue )
						]
			
						+SVerticalBox::Slot()
						.Padding(2)
						.FillHeight(3)
						//.VAlign(VAlign_Fill)
						[
							SNew(SSplitter)

							+SSplitter::Slot()
							.Value(1)
							[
								SNew( SBorder )
								.Padding(1)
								.BorderImage( FEditorStyle::GetBrush( "ToolBar.Background" ) )
								[
									SAssignNew(StatusItemsView, STreeView<TSharedPtr<FLogStatusItem> >)
									.ItemHeight(40.0f)
									.TreeItemsSource(&StatusItems)
									.OnGenerateRow(this, &SLogVisualizer::HandleGenerateLogStatus)
									.OnGetChildren(this, &SLogVisualizer::OnLogStatusGetChildren)
									.SelectionMode(ESelectionMode::None)
								]
							]
							+SSplitter::Slot()
							.Value(3)
							[
								SNew( SBorder )
								.Padding(1)
								.BorderImage( FEditorStyle::GetBrush( "ToolBar.Background" ) )	
								[
									SAssignNew(LogsLinesWidget, SListView<TSharedPtr<FLogEntryItem> >)
									.ItemHeight(20)
									.ListItemsSource(&LogEntryLines)
									.SelectionMode(ESelectionMode::Multi)
									.OnGenerateRow(this, &SLogVisualizer::LogEntryLinesGenerateRow)
									//.OnSelectionChanged(this, &SLogVisualizer::LogEntryLineSelectionChanged)*/
								]
							]
						]
					]
				]
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				// Status area
				SNew(STextBlock)
				.Text(this, &SLogVisualizer::GetStatusText)
			]
		]
	];

	LogVisualizer->OnLogAdded().AddSP(this, &SLogVisualizer::OnLogAdded);

	TArray<TSharedPtr<FActorsVisLog> >& Logs = LogVisualizer->Logs;
	TSharedPtr<FActorsVisLog>* SharedLog = Logs.GetTypedData();
	for (int32 LogIndex = 0; LogIndex < Logs.Num(); ++LogIndex, ++SharedLog)
	{
		if (SharedLog->IsValid())
		{
			AddLog(LogIndex, SharedLog->Get());
		}
	}

	if (LogsList.Num() == 0)
	{
		Timeline->SetVisibility(EVisibility::Hidden);
		ScrollBar->SetVisibility(EVisibility::Hidden);
		ZoomSlider->SetVisibility(EVisibility::Hidden);
	}

	DoFullUpdate();
	
	LastBrowsePath = FPaths::GameLogDir();

	DrawingOnCanvasDelegate = FDebugDrawDelegate::CreateSP(this, &SLogVisualizer::DrawOnCanvas);
	UDebugDrawService::Register(TEXT("VisLog"), DrawingOnCanvasDelegate);
	UGameplayDebuggingComponent::OnDebuggingTargetChangedDelegate.AddSP(this, &SLogVisualizer::SelectionChanged);
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

SLogVisualizer::~SLogVisualizer()
{
	UGameplayDebuggingComponent::OnDebuggingTargetChangedDelegate.RemoveAll(this);
	LogVisualizer->OnLogAdded().RemoveAll(this);
	UDebugDrawService::Unregister(DrawingOnCanvasDelegate);
}

void SLogVisualizer::OnListDoubleClick(TSharedPtr<FLogsListItem> LogListItem)
{
#if WITH_EDITOR
	FVector Orgin, Extent;

	bool bFoundActor = false;
	if (LogVisualizer->Logs.IsValidIndex(LogListItem->LogIndex))
	{
		TSharedPtr<FActorsVisLog>& Log = LogVisualizer->Logs[LogListItem->LogIndex];
		for (FActorIterator It(GetWorld()); It; ++It)
		{
			AActor* Actor = Cast<AActor>(*It);
			if (Actor->GetFName() == Log->Name)
			{
				Actor->GetActorBounds(false, Orgin, Extent);
				bFoundActor = true;
				break;
			}
		}
	}

	if (!bFoundActor)
	{
		Extent = FVector(10, 10, 10);
	}

	//if (LogVisualizer->Logs.IsValidIndex(SelectedLogIndex))
	if (LogVisualizer->Logs.IsValidIndex(LogListItem->LogIndex))
	{
		TSharedPtr<FActorsVisLog>& Log = LogVisualizer->Logs[LogListItem->LogIndex];
		//TSharedPtr<FActorsVisLog> Log = LogVisualizer->Logs[SelectedLogIndex];
		if (Log.IsValid() && Log->Entries.IsValidIndex(LogEntryIndex))
		{
			Orgin = Log->Entries[LogEntryIndex]->Location;
		}
	}


	UEditorEngine *EEngine = Cast<UEditorEngine>(GEngine);
	if (GIsEditor && EEngine != NULL)
	{
		for (auto ViewportClient : EEngine->AllViewportClients)
		{
			//GEditor->MoveViewportCamerasToActor(SelectedActors, bActiveViewportOnly);
			ViewportClient->FocusViewportOnBox(FBox::BuildAABB(Orgin, Extent));
		}
	}
#endif
}

int32 SLogVisualizer::GetCurrentVisibleLogEntryIndex(const TArray<TSharedPtr<FVisLogEntry> >& InVisibleEntries)
{
	if (LogVisualizer->Logs.IsValidIndex(SelectedLogIndex))
	{
		TSharedPtr<FActorsVisLog> Log = LogVisualizer->Logs[SelectedLogIndex];
		if (Log.IsValid() && Log->Entries.IsValidIndex(LogEntryIndex))
		{
			for (int32 Index = 0; Index < InVisibleEntries.Num(); ++Index)
			{
				if (InVisibleEntries[Index] == Log->Entries[LogEntryIndex])
				{
					return Index;
				}
			}
		}
	}

	return INDEX_NONE;
}

void SLogVisualizer::GetVisibleEntries(const TSharedPtr<FActorsVisLog>& Log, TArray<TSharedPtr<FVisLogEntry> >& OutEntries)
{
	OutEntries.Empty();

	if (FilterListPtr.IsValid())
	{
		for (int32 EntryIndex = 0; EntryIndex < Log->Entries.Num(); ++EntryIndex)
		{
			// if any log line is visible - add this entry
			bool bAddedEntry = false;

			if (!bAddedEntry)
			{
				for (int32 LogLineIndex = 0; LogLineIndex < Log->Entries[EntryIndex]->LogLines.Num(); ++LogLineIndex)
				{
					if (FilterListPtr->IsFilterEnabled(Log->Entries[EntryIndex]->LogLines[LogLineIndex].Category.ToString(), Log->Entries[EntryIndex]->LogLines[LogLineIndex].Verbosity))
					{
						OutEntries.AddUnique(Log->Entries[EntryIndex]);
						bAddedEntry = true;
						break;
					}
				}
			}

			if (bAddedEntry)
			{
				continue;
			}

			for (int32 ElementIndex = 0; ElementIndex < Log->Entries[EntryIndex]->ElementsToDraw.Num(); ++ElementIndex)
			{
				if (Log->Entries[EntryIndex]->ElementsToDraw[ElementIndex].Category == NAME_None || FilterListPtr->IsFilterEnabled(Log->Entries[EntryIndex]->ElementsToDraw[ElementIndex].Category.ToString(), Log->Entries[EntryIndex]->ElementsToDraw[ElementIndex].Verbosity))
				{
					OutEntries.AddUnique(Log->Entries[EntryIndex]);
					bAddedEntry = true;
					break;
				}
			}
			if (bAddedEntry)
			{
				continue;
			}

			for (int32 SampleIndex = 0; SampleIndex < Log->Entries[EntryIndex]->HistogramSamples.Num(); ++SampleIndex)
			{
				const FName CurrentCategory = Log->Entries[EntryIndex]->HistogramSamples[SampleIndex].Category;
				const FName CurrentGraphName = Log->Entries[EntryIndex]->HistogramSamples[SampleIndex].GraphName;
				const FName CurrentDataName = Log->Entries[EntryIndex]->HistogramSamples[SampleIndex].DataName;

				if (CurrentCategory == NAME_None || 
					(FilterListPtr->IsFilterEnabled(CurrentCategory.ToString(), ELogVerbosity::All) &&
					FilterListPtr->IsFilterEnabled(CurrentGraphName.ToString(), CurrentDataName.ToString(), ELogVerbosity::All)))
				{
					OutEntries.AddUnique(Log->Entries[EntryIndex]);
					bAddedEntry = true;
					break;
				}
			}
		}

		return;
	}

	// if there is no LogFilter widget - show all
	OutEntries = Log->Entries;
}

void SLogVisualizer::OnLogCategoryFiltersChanged()
{
	RebuildFilteredList();

	if (LogVisualizer && LogVisualizer->Logs.IsValidIndex(SelectedLogIndex))
	{
		TSharedPtr<FActorsVisLog> Log = LogVisualizer->Logs[SelectedLogIndex];
		if (Log.IsValid() && Log->Entries.IsValidIndex(LogEntryIndex))
		{
			ShowEntry(Log->Entries[LogEntryIndex].Get());
		}
	}
}

UWorld* SLogVisualizer::GetWorld() const
{
	// TODO: This needs to be an internalized reference
	UEditorEngine *EEngine = Cast<UEditorEngine>(GEngine);
	if (GIsEditor && EEngine != NULL)
	{
		// lets use PlayWorld during PIE/Simulate and regular world from editor otherwise, to draw debug information
		return EEngine->PlayWorld != NULL ? EEngine->PlayWorld : EEngine->GetEditorWorldContext().World();
		
	}
	else if (!GIsEditor)
	{
		return LogVisualizer->GetWorld();
	}

	return NULL;
}

void SLogVisualizer::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	TimeTillNextUpdate -= InDeltaTime;

	UWorld* World = GetWorld();
	if (World && !World->bPlayersOnly && TimeTillNextUpdate < 0 && LogVisualizer->IsRecording())
	{
		DoFullUpdate();
	}
}

FReply SLogVisualizer::OnMouseWheel( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	if (MouseEvent.IsLeftControlDown())
	{
		OnSetZoomValue(FMath::Clamp(ZoomSliderValue + MouseEvent.GetWheelDelta() * 0.05f, 0.f, 1.f));
		return FReply::Handled();
	}
	return SCompoundWidget::OnMouseWheel(MyGeometry, MouseEvent);
}

FReply SLogVisualizer::OnKeyDown( const FGeometry& MyGeometry, const FKeyboardEvent& InKeyboardEvent )
{
	const FKey Key = InKeyboardEvent.GetKey();
	if (Key == EKeys::Left || Key == EKeys::Right)
	{
		int32 MoveBy = Key == EKeys::Left ? -1 : 1;
		if (InKeyboardEvent.IsLeftControlDown())
		{
			MoveBy *= 10;
		}

		IncrementCurrentLogIndex(MoveBy);

		return FReply::Handled();
	}

	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyboardEvent);
}

TSharedRef< SWidget > SLogVisualizer::MakeMainMenu()
{
	FMenuBarBuilder MenuBuilder( NULL );
	{
		// File
		MenuBuilder.AddPullDownMenu( 
			NSLOCTEXT("LogVisualizer", "FileMenu", "File"),
			NSLOCTEXT("LogVisualizer", "FileMenu_ToolTip", "Open the file menu"),
			FNewMenuDelegate::CreateSP( this, &SLogVisualizer::OpenSavedSession ) );

		// Help
		MenuBuilder.AddPullDownMenu( 
			NSLOCTEXT("LogVisualizer", "HelpMenu", "Help"),
			NSLOCTEXT("LogVisualizer", "HelpMenu_ToolTip", "Open the help menu"),
			FNewMenuDelegate::CreateSP( this, &SLogVisualizer::FillHelpMenu ) );
	}

	// Create the menu bar
	TSharedRef< SWidget > MenuBarWidget = MenuBuilder.MakeWidget();

	return MenuBarWidget;
}

void SLogVisualizer::FillHelpMenu(FMenuBuilder& MenuBuilder)
{
}

void SLogVisualizer::OpenSavedSession(FMenuBuilder& MenuBuilder)
{
}

//----------------------------------------------------------------------//
// non-slate
//----------------------------------------------------------------------//

void SLogVisualizer::SelectionChanged(AActor* DebuggedActor, bool bIsBeingDebuggedNow)
{
	if (DebuggedActor != NULL && bIsBeingDebuggedNow)
	{
		SelectActor(DebuggedActor);
	}
}

void SLogVisualizer::IncrementCurrentLogIndex(int32 IncrementBy)
{
	if (!LogVisualizer->Logs.IsValidIndex(SelectedLogIndex))
	{
		return;
	}

	TSharedPtr<FActorsVisLog> Log = LogVisualizer->Logs[SelectedLogIndex];
	check(Log.IsValid());

	int32 NewEntryIndex = FMath::Clamp(LogEntryIndex + IncrementBy, 0, Log->Entries.Num() - 1);

	if (FilterListPtr.IsValid())
	{
		while (NewEntryIndex >= 0 && NewEntryIndex < Log->Entries.Num())
		{
			bool bShouldShow = false;
			for (int32 LineIndex = 0; LineIndex < Log->Entries[NewEntryIndex]->LogLines.Num(); ++LineIndex)
			{
				if (FilterListPtr->IsFilterEnabled(Log->Entries[NewEntryIndex]->LogLines[LineIndex].Category.ToString(), Log->Entries[NewEntryIndex]->LogLines[LineIndex].Verbosity))
				{
					bShouldShow = true;
					break;
				}
			}

			if (!bShouldShow)
			{
				for (int32 LineIndex = 0; LineIndex < Log->Entries[NewEntryIndex]->ElementsToDraw.Num(); ++LineIndex)
				{
					if (Log->Entries[NewEntryIndex]->ElementsToDraw[LineIndex].Category == NAME_None || FilterListPtr->IsFilterEnabled(Log->Entries[NewEntryIndex]->ElementsToDraw[LineIndex].Category.ToString(), Log->Entries[NewEntryIndex]->ElementsToDraw[LineIndex].Verbosity))
					{
						bShouldShow = true;
						break;
					}
				}
			}

			if (!bShouldShow)
			{
				for (int32 SampleIndex = 0; SampleIndex < Log->Entries[NewEntryIndex]->HistogramSamples.Num(); ++SampleIndex)
				{
					const FName CurrentCategory = Log->Entries[NewEntryIndex]->HistogramSamples[SampleIndex].Category;
					const FName CurrentGraphName = Log->Entries[NewEntryIndex]->HistogramSamples[SampleIndex].GraphName;
					const FName CurrentDataName = Log->Entries[NewEntryIndex]->HistogramSamples[SampleIndex].DataName;
					if (CurrentCategory == NAME_None ||
						(FilterListPtr->IsFilterEnabled(CurrentCategory.ToString(), ELogVerbosity::All) && FilterListPtr->IsFilterEnabled(CurrentGraphName.ToString(), CurrentDataName.ToString(), ELogVerbosity::All)))
					{
						bShouldShow = true;
						break;
					}
				}
			}

			if (bShouldShow)
			{
				break;
			}

			NewEntryIndex += (IncrementBy > 0 ? 1 : -1);
		}
	}

	if (NewEntryIndex != LogEntryIndex && Log->Entries.IsValidIndex(NewEntryIndex))
	{
		LogEntryIndex = NewEntryIndex;
		ShowEntry(Log->Entries[NewEntryIndex].Get());
	}
}

void SLogVisualizer::AddLog(int32 LogIndex, const FActorsVisLog* Log)
{
	if (Log->Entries.Num() == 0)
	{
		return;
	}

	if (LogsList.Num() == 0)
	{
		Timeline->SetVisibility(EVisibility::Visible);
		ScrollBar->SetVisibility(EVisibility::Visible);
		ZoomSlider->SetVisibility(EVisibility::Visible);
	}

	const float StartTimestamp = Log->Entries[0]->TimeStamp;
	const float EndTimestamp = Log->Entries[Log->Entries.Num() - 1]->TimeStamp;
	
	for (int32 EntryIndex = 0; EntryIndex < Log->Entries.Num(); ++EntryIndex)
	{
		for (auto Iter(Log->Entries[EntryIndex]->LogLines.CreateConstIterator()); Iter; Iter++)
		{
			int32 Index = UsedCategories.Find(Iter->Category.ToString());
			if (Index == INDEX_NONE)
			{
				Index = UsedCategories.Add(Iter->Category.ToString());
				FilterListPtr->AddFilter(Iter->Category.ToString(), GetColorForUsedCategory(Index));
			}
		}

		for (auto Iter(Log->Entries[EntryIndex]->ElementsToDraw.CreateConstIterator()); Iter; Iter++)
		{
			const FString CategoryAsString = Iter->Category != NAME_None ? Iter->Category.ToString() : TEXT("ShapeElement");

			int32 Index = UsedCategories.Find(CategoryAsString);
			if (Index == INDEX_NONE)
			{
				Index = UsedCategories.Add(CategoryAsString);
				FilterListPtr->AddFilter(CategoryAsString, GetColorForUsedCategory(Index));
			}
		}

		for (int32 SampleIndex = 0; SampleIndex < Log->Entries[EntryIndex]->HistogramSamples.Num(); ++SampleIndex)
		{
			const FString CategoryAsString = Log->Entries[EntryIndex]->HistogramSamples[SampleIndex].Category.ToString();

			int32 Index = UsedCategories.Find(CategoryAsString);
			if (Index == INDEX_NONE)
			{
				Index = UsedCategories.Add(CategoryAsString);
				FilterListPtr->AddFilter(CategoryAsString, GetColorForUsedCategory(Index));
			}

			const FString GraphNameAsString = Log->Entries[EntryIndex]->HistogramSamples[SampleIndex].GraphName.ToString();
			const FString DataNameAsString = Log->Entries[EntryIndex]->HistogramSamples[SampleIndex].DataName.ToString();
			FilterListPtr->AddGraphFilter(GraphNameAsString, DataNameAsString, FColor::White);
		}
	}

	LogsList.Add(MakeShareable(new FLogsListItem(Log->Name.ToString()
		, StartTimestamp, EndTimestamp, LogIndex)));
}

void SLogVisualizer::DoFullUpdate()
{
	TSharedPtr<FLogsListItem>* LogListItem = LogsList.GetTypedData();
	for (int32 ItemIndex = 0; ItemIndex < LogsList.Num(); ++ItemIndex, ++LogListItem)
	{
		if (LogListItem->IsValid() && LogVisualizer->Logs.IsValidIndex((*LogListItem)->LogIndex))
		{
			TSharedPtr<FActorsVisLog>& Log = LogVisualizer->Logs[(*LogListItem)->LogIndex];
			LogsStartTime = FMath::Min(Log->Entries[0]->TimeStamp, LogsStartTime);
			LogsEndTime = FMath::Max(Log->Entries[Log->Entries.Num()-1]->TimeStamp, LogsEndTime);
		}
	}

	Timeline->SetMinMaxValues(LogsStartTime, LogsEndTime);
	// set zoom max so that single even on SBarLogs has desired size on maximum zoom
	const float WidthPx = Timeline->GetDrawingGeometry().Size.X;
	if (WidthPx > 0)
	{
		const float OldMaxZoom = MaxZoom;
		const float PxPerTimeUnit = WidthPx * SLogBar::TimeUnit / (LogsEndTime - LogsStartTime);
		MaxZoom = SLogBar::MaxUnitSizePx / PxPerTimeUnit;
		if (MaxZoom < MinZoom)
		{
			MaxZoom = MinZoom;
		}

		ZoomSliderValue = MaxZoom * ZoomSliderValue / OldMaxZoom;
		// update 
	}
		
	RebuildFilteredList();

	TimeTillNextUpdate = 1.f/FullUpdateFrequency;
}

void SLogVisualizer::OnLogAdded()
{
	// take last log
	const int32 NewLogIndex = LogVisualizer->Logs.Num()-1;
	
	TSharedPtr<FLogsListItem> Item;
	for (int32 Index = 0; Index < LogsList.Num(); ++Index)
	{
		Item = LogsList[Index];
		TArray<TSharedPtr<FActorsVisLog> >& Logs = LogVisualizer->Logs;
		if (Item->Name == Logs[NewLogIndex]->Name.ToString())
		{
			break;
		}
	}
	
	if (!Item.IsValid())
	{
		AddLog(NewLogIndex, LogVisualizer->Logs[NewLogIndex].Get());
	}		

	RequestFullUpdate();
}

TSharedRef<ITableRow> SLogVisualizer::LogsListGenerateRow(TSharedPtr<FLogsListItem> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SLogsTableRow, OwnerTable)
			.Item(InItem)
			.OwnerVisualizerWidget(SharedThis(this));
}

void SLogVisualizer::LogsListSelectionChanged(TSharedPtr<FLogsListItem> SelectedItem, ESelectInfo::Type SelectInfo)
{
	//@todo find log entry closest to current time selection
	//LogEntryIndex = INDEX_NONE;
	const int32 NewLogIndex = SelectedItem.IsValid() ? SelectedItem->LogIndex : INDEX_NONE;
	if (NewLogIndex != SelectedLogIndex && NewLogIndex != INDEX_NONE)
	{
		SelectedLogIndex = NewLogIndex;
		TSharedPtr<FActorsVisLog> Log = LogVisualizer->Logs[NewLogIndex];
		LogEntryIndex = Log->Entries.Num() - 1;
	}
	
	if (LogVisualizer->Logs.IsValidIndex(SelectedLogIndex))
	{
		if (USelection* SelectedActors = GEditor->GetSelectedActors())
		{
			TSharedPtr<FActorsVisLog> Log = LogVisualizer->Logs[SelectedLogIndex];

			if (UWorld* World = GetWorld())
			{
				for (FConstPawnIterator Iterator = World->GetPawnIterator(); Iterator; ++Iterator)
				{
					if (APawn* CurrentPawn = *Iterator)
					{
						if (AController* CurrentController = CurrentPawn->GetController())
						{
							if (CurrentController->GetName() == Log->Name.ToString())
							{
								SelectedActors->Select(CurrentPawn);
							}
							else
							{
								SelectedActors->Deselect(CurrentPawn);
							}
						}
					}
				}
			}
		}
	}

	//SetCurrentViewedTime(CurrentViewedTime, /*bForce=*/true);
	
	LogsLinesWidget->RequestListRefresh();
}

TSharedRef<ITableRow> SLogVisualizer::LogEntryLinesGenerateRow(TSharedPtr<FLogEntryItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew( STableRow< TSharedPtr<FString> >, OwnerTable )
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(5.0f, 0.0f))
			[
				SNew(STextBlock) 
				.ColorAndOpacity(FSlateColor(Item->CategoryColor))
				.Text(Item->Category)
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(5.0f, 0.0f))
			[
				SNew(STextBlock) 				
				.ColorAndOpacity(FSlateColor(FLinearColor::Gray))
				.Text(FString(TEXT("(")) + FString(FOutputDevice::VerbosityToString(Item->Verbosity)) + FString(TEXT(")")))
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(5.0f, 0.0f))
			[
				SNew(STextBlock) 
				.Text(Item->Line)
			]			
		];
}
/*
void SLogVisualizer::LogEntryLineSelectionChanged(TSharedPtr<FLogsListItem> SelectedItem, ESelectInfo::Type SelectInfo)
{

}
*/
bool SLogVisualizer::ShouldListLog(const FActorsVisLog& Log)
{
	//// Check log name filter
	if ((LogNameFilterString.Len() > 0 && !Log.Name.ToString().Contains(LogNameFilterString) )
		|| (bIgnoreTrivialLogs == true && Log.Entries.Num() < 2)
		)
	{
		return false;
	}

	return true;
}

void SLogVisualizer::UpdateFilterInfo()
{
	// get filters
	LogNameFilterString = LogNameFilterBox->GetText().ToString();
}

void SLogVisualizer::SetCurrentViewedTime(float NewTime, const bool bForce)
{
	if (CurrentViewedTime == NewTime && bForce == false)
	{
		return;
	}

	CurrentViewedTime = NewTime;
}

void SLogVisualizer::RequestShowLogEntry(TSharedPtr<FLogsListItem> Item, TSharedPtr<FVisLogEntry> LogEntry)
{
	ShowLogEntry(Item, LogEntry);
}

void SLogVisualizer::ShowLogEntry(TSharedPtr<FLogsListItem> Item, TSharedPtr<FVisLogEntry> LogEntry)
{
	if(LogsListWidget->GetSelectedItems().Find(Item) == INDEX_NONE)
	{
		LogsListWidget->ClearSelection();
		LogsListWidget->SetItemSelection(Item, true);
	}

	if (LogVisualizer->Logs.IsValidIndex(SelectedLogIndex))
	{
		TSharedPtr<FActorsVisLog> Log = LogVisualizer->Logs[SelectedLogIndex];
		LogEntryIndex = Log->Entries.Find(LogEntry);
	}
	else
	{
		LogEntryIndex = INDEX_NONE;
	}

	ShowEntry(LogEntry.Get());
}

FLinearColor SLogVisualizer::GetColorForUsedCategory(int32 Index)
{
	if (Index >= 0 && Index < sizeof(ColorPalette) / sizeof(ColorPalette[0]))
	{
		return ColorPalette[Index];
	}

	return FLinearColor::White;
}

TSharedRef<ITableRow> SLogVisualizer::HandleGenerateLogStatus(TSharedPtr<FLogStatusItem> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (InItem->Children.Num() > 0)
	{
		return SNew(STableRow<TSharedPtr<FLogStatusItem> >, OwnerTable)
			[
				SNew(STextBlock).Text(InItem->ItemText)
			];
	}

	FString TooltipText = FString::Printf(TEXT("%s: %s"), *InItem->ItemText, *InItem->ValueText);
	return SNew(STableRow<TSharedPtr<FLogStatusItem> >, OwnerTable)
		[
			SNew(SBorder)
			.BorderImage( FEditorStyle::GetBrush("NoBorder") )
			.ToolTipText(TooltipText)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock).Text(InItem->ItemText).ColorAndOpacity(FColorList::Aquamarine)
				]
				+SHorizontalBox::Slot()
				.Padding(4.0f, 0, 0, 0)
				.AutoWidth()
				[
					SNew(STextBlock).Text(InItem->ValueText)
				]
			]
		];
}

void SLogVisualizer::OnLogStatusGetChildren(TSharedPtr<FLogStatusItem> InItem, TArray< TSharedPtr<FLogStatusItem> >& OutItems)
{
	OutItems = InItem->Children;
}

void SLogVisualizer::UpdateStatusItems(const FVisLogEntry* LogEntry)
{
	TArray<FString> ExpandedCategories;
	for (int32 ItemIndex = 0; ItemIndex < StatusItems.Num(); ItemIndex++)
	{
		const bool bIsExpanded = StatusItemsView->IsItemExpanded(StatusItems[ItemIndex]);
		if (bIsExpanded)
		{
			ExpandedCategories.Add(StatusItems[ItemIndex]->ItemText);
		}
	}

	StatusItems.Empty();

	if (LogEntry)
	{
		FString TimestampDesc = FString::Printf(TEXT("%.2fs"), LogEntry->TimeStamp);
		StatusItems.Add(MakeShareable(new FLogStatusItem(LOCTEXT("VisLogTimestamp","Time").ToString(), TimestampDesc)));

		for (int32 CategoryIndex = 0; CategoryIndex < LogEntry->Status.Num(); CategoryIndex++)
		{
			if (LogEntry->Status[CategoryIndex].Data.Num() <= 0)
			{
				continue;
			}

			TSharedRef<FLogStatusItem> StatusItem = MakeShareable(new FLogStatusItem(LogEntry->Status[CategoryIndex].Category));
			for (int32 LineIndex = 0; LineIndex < LogEntry->Status[CategoryIndex].Data.Num(); LineIndex++)
			{
				FString KeyDesc, ValueDesc;
				const bool bHasValue = LogEntry->Status[CategoryIndex].GetDesc(LineIndex, KeyDesc, ValueDesc);
				if (bHasValue)
				{
					StatusItem->Children.Add(MakeShareable(new FLogStatusItem(KeyDesc, ValueDesc)));
				}
			}

			StatusItems.Add(StatusItem);
		}
	}

	StatusItemsView->RequestTreeRefresh();

	for (int32 ItemIndex = 0; ItemIndex < StatusItems.Num(); ItemIndex++)
	{
		for (const FString& Category : ExpandedCategories)
		{
			if (StatusItems[ItemIndex]->ItemText == Category)
			{
				StatusItemsView->SetItemExpansion(StatusItems[ItemIndex], true);
				break;
			}
		}
	}
}

void SLogVisualizer::ShowEntry(const FVisLogEntry* LogEntry)
{
	UpdateStatusItems(LogEntry);
	LogEntryLines.Reset();
	
	const FVisLogEntry::FLogLine* LogLine = LogEntry->LogLines.GetTypedData();
	for (int LineIndex = 0; LineIndex < LogEntry->LogLines.Num(); ++LineIndex, ++LogLine)
	{
		bool bShowLine = true;

		if (FilterListPtr.IsValid())
		{
			bShowLine = FilterListPtr->IsFilterEnabled(LogLine->Category.ToString(), LogLine->Verbosity);
		}

		if (bShowLine)
		{
			FLogEntryItem EntryItem;
			EntryItem.Category = LogLine->Category.ToString();

			int32 Index = UsedCategories.Find(EntryItem.Category);
			if (Index == INDEX_NONE)
			{
				Index = UsedCategories.Add(EntryItem.Category);
			}
			EntryItem.CategoryColor = GetColorForUsedCategory(Index);

			EntryItem.Verbosity = LogLine->Verbosity;
			EntryItem.Line = LogLine->Line;

			LogEntryLines.Add(MakeShareable(new FLogEntryItem(EntryItem)));
		}
	}

	SetCurrentViewedTime(LogEntry->TimeStamp);

	LogsLinesWidget->RequestListRefresh();
}

int32 SLogVisualizer::FindIndexInLogsList(const int32 LogIndex) const
{
	for (int32 Index = 0; Index < LogsList.Num(); ++Index)
	{
		if (LogsList[Index]->LogIndex == LogIndex)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

void SLogVisualizer::RebuildFilteredList()
{
	// store current selection
	TArray< TSharedPtr<FLogsListItem> > ItemsToSelect = LogsListWidget->GetSelectedItems();

	LogsList.Reset();
	for (int32 LogIndex = 0; LogIndex < LogVisualizer->Logs.Num(); ++LogIndex)
	{
		const FActorsVisLog& Log = *(LogVisualizer->Logs[LogIndex]);
		//const int32 IndexInList = FindIndexInLogsList(LogIndex);

		if (ShouldListLog(Log))
		{
			// Passed filter so add to filtered results (defer sorting until end)
			AddLog(LogIndex, &Log);			
		}
	}
		
	// When underlying array changes, refresh list
	LogsListWidget->RequestListRefresh();

	// redo selection
	if (ItemsToSelect.Num() > 0)
	{
		TSharedPtr<FLogsListItem>* Item = ItemsToSelect.GetTypedData();
		for (int32 ItemIndex = 0; ItemIndex < ItemsToSelect.Num(); ++ItemIndex, ++Item)
		{
			const int32 IndexInList = FindIndexInLogsList((*Item)->LogIndex);
			if (IndexInList != INDEX_NONE)
			{
				LogsListWidget->SetItemSelection(LogsList[IndexInList], true);
			}
		}
	}
}

float SLogVisualizer::GetZoomValue() const
{
	return ZoomSliderValue;
}

void SLogVisualizer::OnSetZoomValue( float NewValue )
{
	const float PrevZoom = GetZoom();
	const float PrevVisibleRange = 1.0f / PrevZoom;

	ZoomSliderValue = NewValue;
	const float Zoom = GetZoom();

	const float MaxOffset = GetMaxScrollOffsetFraction();
	const float MaxGraphOffset = GetMaxGraphOffset();
	
	const float ViewedTimeSpan = (LogsEndTime - LogsStartTime) / Zoom;
	const float ScrollOffsetFraction = FMath::Clamp((CurrentViewedTime - LogsStartTime - ViewedTimeSpan/2) / (LogsEndTime - LogsStartTime), 0.0f, MaxOffset);

	const float WidthPx = Timeline->GetDrawingGeometry().Size.X;
	const float GraphOffset = MaxOffset > 0 ? (ScrollOffsetFraction / MaxOffset) * MaxGraphOffset : 0.f;
	
	ZoomChangedNotify.Broadcast(Zoom, -GraphOffset);

	ScrollBar->SetState( ScrollOffsetFraction, 1.0f / Zoom );

	Timeline->SetZoom( Zoom );
	Timeline->SetOffset( -GraphOffset );

	ScrollbarOffset = -GraphOffset;
}

void SLogVisualizer::OnZoomScrolled(float InScrollOffsetFraction)
{
	if( ZoomSliderValue > 0.0f )
	{
		const float MaxOffset = GetMaxScrollOffsetFraction();
		const float MaxGraphOffset = GetMaxGraphOffset();
		InScrollOffsetFraction = FMath::Clamp( InScrollOffsetFraction, 0.0f, MaxOffset );
		float GraphOffset = -( InScrollOffsetFraction / MaxOffset ) * MaxGraphOffset;

		ScrollBar->SetState( InScrollOffsetFraction, 1.0f / GetZoom() );

		ZoomChangedNotify.Broadcast(GetZoom(), GraphOffset);

		Timeline->SetOffset( GraphOffset );

		ScrollbarOffset = GraphOffset;
	}
}

void SLogVisualizer::OnDrawLogEntriesPathChanged(ESlateCheckBoxState::Type NewState)
{
	bDrawLogEntriesPath = (NewState == ESlateCheckBoxState::Checked);
}

ESlateCheckBoxState::Type SLogVisualizer::GetDrawLogEntriesPathState() const
{
	return bDrawLogEntriesPath ? ESlateCheckBoxState::Checked : ESlateCheckBoxState::Unchecked;
}

void SLogVisualizer::OnIgnoreTrivialLogs(ESlateCheckBoxState::Type NewState)
{
	bIgnoreTrivialLogs = (NewState == ESlateCheckBoxState::Checked);
	DoFullUpdate();
}

ESlateCheckBoxState::Type SLogVisualizer::GetIgnoreTrivialLogs() const
{
	return bIgnoreTrivialLogs ? ESlateCheckBoxState::Checked : ESlateCheckBoxState::Unchecked;
}

void SLogVisualizer::OnToggleCamera(ESlateCheckBoxState::Type NewState)
{
	UWorld* World = GetWorld();
	if (ALogVisualizerCameraController::IsEnabled(World))
	{
		ALogVisualizerCameraController::DisableCamera(World);
	}
	else
	{
		ALogVisualizerCameraController::EnableCamera(World);
	}
}

ESlateCheckBoxState::Type SLogVisualizer::GetToggleCameraState() const
{
	return ALogVisualizerCameraController::IsEnabled(GetWorld())
		? ESlateCheckBoxState::Checked : ESlateCheckBoxState::Unchecked;
}

//----------------------------------------------------------------------//
// Drawing
//----------------------------------------------------------------------//
void SLogVisualizer::DrawOnCanvas(UCanvas* Canvas, APlayerController*)
{
	UWorld* World = GetWorld();
	if (World != NULL && LogVisualizer->Logs.IsValidIndex(SelectedLogIndex))
	{
		TSharedPtr<FActorsVisLog> Log = LogVisualizer->Logs[SelectedLogIndex];
		const TArray<TSharedPtr<FVisLogEntry> >& Entries = Log->Entries;

		if (bDrawLogEntriesPath)
		{
			const TSharedPtr<FVisLogEntry>* Entry = Entries.GetTypedData();
			FVector Location = (*Entry)->Location;
			++Entry;

			for (int32 Index = 1; Index < Entries.Num(); ++Index, ++Entry)
			{
				const FVector CurrentLocation = (*Entry)->Location;
				DrawDebugLine(World, Location, CurrentLocation, FColor(160, 160, 240));
				Location = CurrentLocation;
			}
		}

		if (Entries.IsValidIndex(LogEntryIndex))
		{
			// draw all additional data stored in current entry
			const TSharedPtr<FVisLogEntry>& Entry = Entries[LogEntryIndex];

			// mark current location
			DrawDebugCone(World, Entry->Location, /*Direction*/FVector(0, 0, 1), /*Length*/200.f
				, PI/64, PI/64, /*NumSides*/16, FColor::Red);
			
			UFont* Font = GEngine->GetSmallFont();
			FCanvasTextItem TextItem( FVector2D::ZeroVector, FText::GetEmpty(), Font, FLinearColor::White );
			const FString TimeStampString = FString::Printf(TEXT("%.2f"), Entry->TimeStamp);
			const FVector EntryScreenLoc = Canvas->Project(Entry->Location);
			Canvas->SetDrawColor(FColor::Black);
			Canvas->DrawText(Font, TimeStampString,EntryScreenLoc.X+1, EntryScreenLoc.Y+1);
			Canvas->SetDrawColor(FColor::White);
			Canvas->DrawText(Font, TimeStampString, EntryScreenLoc.X, EntryScreenLoc.Y);

			//let's draw histogram data
			struct FGraphLineData
			{
				FName DataName;
				TArray<FVector2D> Samples;
			};
			typedef TMap<FName, FGraphLineData > FGraphLines;

			struct FGraphData
			{
				FGraphData() : Min(FVector2D(FLT_MAX, FLT_MAX)), Max(FVector2D(FLT_MIN, FLT_MIN)) {}

				FVector2D Min, Max;
				TMap<FName, FGraphLineData > GraphLines;
			};

			TMap<FName, FGraphData>	CollectedGraphs;

			for (int32 EntryIndex = 0; EntryIndex < Entries.Num(); ++EntryIndex)
			{
				const TSharedPtr<FVisLogEntry>& CurrentEntry = Entries[EntryIndex];
				if (CurrentEntry->TimeStamp > Entry->TimeStamp)
				{
					break;
				}

				const int32 SamplesNum = CurrentEntry->HistogramSamples.Num();
				for (int32 SampleIndex = 0; SampleIndex < SamplesNum; ++SampleIndex)
				{
					FVisLogEntry::FHistogramSample CurrentSample = CurrentEntry->HistogramSamples[SampleIndex];
					if (!FilterListPtr.IsValid() || (FilterListPtr->IsFilterEnabled(CurrentSample.Category.ToString(), ELogVerbosity::All) &&
						FilterListPtr->IsFilterEnabled(CurrentSample.GraphName.ToString(), CurrentSample.DataName.ToString(), ELogVerbosity::All)))
					{
						FGraphData &GraphData = CollectedGraphs.FindOrAdd(CurrentSample.GraphName);
						FGraphLineData &LineData = GraphData.GraphLines.FindOrAdd(CurrentSample.DataName);
						LineData.DataName = CurrentSample.DataName;
						LineData.Samples.Add( CurrentSample.SampleValue );

						GraphData.Min.X = FMath::Min(GraphData.Min.X, CurrentSample.SampleValue.X);
						GraphData.Min.Y = FMath::Min(GraphData.Min.Y, CurrentSample.SampleValue.Y);

						GraphData.Max.X = FMath::Max(GraphData.Max.X, CurrentSample.SampleValue.X);
						GraphData.Max.Y = FMath::Max(GraphData.Max.Y, CurrentSample.SampleValue.Y);
					}
				}
			}

			int32 GraphIndex = 0;
			if (CollectedGraphs.Num() > 0)
			{
				for (auto It(CollectedGraphs.CreateConstIterator()); It; ++It)
				{
					TWeakObjectPtr<UReporterGraph> HistogramGraph = Canvas->GetReporterGraph();
					if (!HistogramGraph.IsValid())
					{
						break;
					}
					HistogramGraph->SetNumGraphLines(It->Value.GraphLines.Num());
					int32 LineIndex = 0;
					for (auto LinesIt(It->Value.GraphLines.CreateConstIterator()); LinesIt; ++LinesIt)
					{
						int32 ColorIndex = UsedCategories.Find(LinesIt->Value.DataName.ToString());
						if (ColorIndex == INDEX_NONE)
						{
							ColorIndex = UsedCategories.AddUnique(LinesIt->Value.DataName.ToString());
						}

						HistogramGraph->GetGraphLine(LineIndex)->Color = GetColorForUsedCategory(ColorIndex);
						HistogramGraph->GetGraphLine(LineIndex)->LineName = LinesIt->Value.DataName.ToString();
						HistogramGraph->GetGraphLine(LineIndex)->Data.Append(LinesIt->Value.Samples);
						++LineIndex;
					}

					float dx = 0.8f / CollectedGraphs.Num();
					HistogramGraph->SetGraphScreenSize( 0.1f + GraphIndex * (dx+0.02), 0.1f + GraphIndex * dx + dx, (1.0f - dx) * 0.5f,   1.0f - (1 - dx) * 0.5f);
					HistogramGraph->SetAxesMinMax(It->Value.Min, It->Value.Max);

					HistogramGraph->SetNumThresholds(1);
					FGraphThreshold* GraphThreshold = HistogramGraph->GetThreshold(0);
					GraphThreshold->Threshold = 0.f;
					GraphThreshold->Color = FLinearColor::White;
					GraphThreshold->ThresholdName = TEXT("     0");
					HistogramGraph->SetStyles(EGraphAxisStyle::Grid, EGraphDataStyle::Lines);
					HistogramGraph->SetBackgroundColor( FColor(0,0,0, 90) );
					HistogramGraph->SetLegendPosition(ELegendPosition::Inside);

					HistogramGraph->bVisible = true;
					HistogramGraph->Draw(Canvas);

					++GraphIndex;
				}
			}


			const FVisLogEntry::FElementToDraw* ElementToDraw = Entry->ElementsToDraw.GetTypedData();
			const int32 ElementsCount = Entry->ElementsToDraw.Num();
			
			for (int32 ElementIndex = 0; ElementIndex < ElementsCount; ++ElementIndex, ++ElementToDraw)
			{
				if (FilterListPtr.IsValid() && !FilterListPtr->IsFilterEnabled(ElementToDraw->Category.ToString(), ElementToDraw->Verbosity))
				{
					continue;
				}

				const FColor Color = ElementToDraw->GetFColor();
				Canvas->SetDrawColor(Color);

				switch(ElementToDraw->GetType())
				{
				case FVisLogEntry::FElementToDraw::SinglePoint:
					{
						const float Radius = float(ElementToDraw->Radius);
						const bool bDrawLabel = ElementToDraw->Description.IsEmpty() == false;
						const FVector* Location = ElementToDraw->Points.GetTypedData();
						for (int32 Index = 0; Index < ElementToDraw->Points.Num(); ++Index, ++Location)
						{
							DrawDebugSphere(World, *Location, Radius, 16, Color);
							if (bDrawLabel)
							{
								const FVector ScreenLoc = Canvas->Project(*Location);
								Canvas->DrawText(Font, FString::Printf(TEXT("%s_%d"), *ElementToDraw->Description, Index), ScreenLoc.X, ScreenLoc.Y);
							}
						}
					}
					break;
				case FVisLogEntry::FElementToDraw::Segment:
					{
						const float Thickness = float(ElementToDraw->Thicknes);
						const bool bDrawLabel = ElementToDraw->Description.IsEmpty() == false && ElementToDraw->Points.Num() > 2;
						const FVector* Location = ElementToDraw->Points.GetTypedData();
						for (int32 Index = 0; Index + 1 < ElementToDraw->Points.Num(); Index += 2, Location += 2)
						{
							DrawDebugLine(World, *Location, *(Location + 1), Color
								, /*bPersistentLines*/false, /*LifeTime*/-1
								, /*DepthPriority*/0, Thickness);

							if (bDrawLabel)
							{
								const FString PrintString = FString::Printf(TEXT("%s_%d"), *ElementToDraw->Description, Index);
								float TextXL, TextYL;
								Canvas->StrLen(Font, PrintString, TextXL, TextYL);
								const FVector ScreenLoc = Canvas->Project(*Location + (*(Location+1)-*Location)/2);
								Canvas->DrawText(Font, *PrintString, ScreenLoc.X - TextXL/2.0f, ScreenLoc.Y - TextYL/2.0f);
							}
						}
						if (ElementToDraw->Description.IsEmpty() == false)
						{
							float TextXL, TextYL;
							Canvas->StrLen(Font, ElementToDraw->Description, TextXL, TextYL);
							const FVector ScreenLoc = Canvas->Project(ElementToDraw->Points[0] 
								+ (ElementToDraw->Points[1] - ElementToDraw->Points[0])/2);
							Canvas->DrawText(Font, *ElementToDraw->Description, ScreenLoc.X - TextXL/2.0f, ScreenLoc.Y - TextYL/2.0f);
						}
					}
					break;
				case FVisLogEntry::FElementToDraw::Path:
					{
						const float Thickness = float(ElementToDraw->Thicknes);
						FVector Location = ElementToDraw->Points[0];
						for (int32 Index = 1; Index < ElementToDraw->Points.Num(); ++Index)
						{
							const FVector CurrentLocation = ElementToDraw->Points[Index];
							DrawDebugLine(World, Location, CurrentLocation, Color
								, /*bPersistentLines*/false, /*LifeTime*/-1
								, /*DepthPriority*/0, Thickness);
							Location = CurrentLocation;
						}
					}
					break;
				case FVisLogEntry::FElementToDraw::Box:
					{
						const float Thickness = float(ElementToDraw->Thicknes);
						const bool bDrawLabel = ElementToDraw->Description.IsEmpty() == false && ElementToDraw->Points.Num() > 2;
						const FVector* BoxExtent = ElementToDraw->Points.GetTypedData();
						for (int32 Index = 0; Index + 1 < ElementToDraw->Points.Num(); Index += 2, BoxExtent += 2)
						{
							FBox Box(*BoxExtent, *(BoxExtent + 1));
							DrawDebugBox(World, Box.GetCenter(), Box.GetExtent(), Color
								, /*bPersistentLines*/false, /*LifeTime*/-1
								, /*DepthPriority*/0/*, Thickness*/);

							if (bDrawLabel)
							{
								const FString PrintString = FString::Printf(TEXT("%s_%d"), *ElementToDraw->Description, Index);
								float TextXL, TextYL;
								Canvas->StrLen(Font, PrintString, TextXL, TextYL);
								const FVector ScreenLoc = Canvas->Project(Box.GetCenter());
								Canvas->DrawText(Font, *PrintString, ScreenLoc.X - TextXL/2.0f, ScreenLoc.Y - TextYL/2.0f);
							}
						}
						if (ElementToDraw->Description.IsEmpty() == false)
						{
							float TextXL, TextYL;
							Canvas->StrLen(Font, ElementToDraw->Description, TextXL, TextYL);
							const FVector ScreenLoc = Canvas->Project(ElementToDraw->Points[0] 
							+ (ElementToDraw->Points[1] - ElementToDraw->Points[0])/2);
							Canvas->DrawText(Font, *ElementToDraw->Description, ScreenLoc.X - TextXL/2.0f, ScreenLoc.Y - TextYL/2.0f);
						}
					}
					break;
				}
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////

const FSlateBrush* SLogVisualizer::GetRecordButtonBrush() const
{
	if(LogVisualizer->IsRecording())
	{
		// If recording, show stop button
		return 	FEditorStyle::GetBrush("LogVisualizer.Stop");
	}
	else
	{
		// If stopped, show record button
		return 	FEditorStyle::GetBrush("LogVisualizer.Record");
	}
}

FString SLogVisualizer::GetStatusText() const
{
	return TEXT("");
}

ESlateCheckBoxState::Type SLogVisualizer::GetPauseState() const
{
	UWorld* World = GetWorld();
	return (World != NULL && (World->bPlayersOnly || World->bPlayersOnlyPending)) ? ESlateCheckBoxState::Checked : ESlateCheckBoxState::Unchecked;
}

FReply SLogVisualizer::OnRecordButtonClicked()
{
	// Toggle recording state
	LogVisualizer->SetIsRecording(!LogVisualizer->IsRecording());

	return FReply::Handled();
}

FReply SLogVisualizer::OnLoad()
{
	TArray<FString> OpenFilenames;
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	bool bOpened = false;
	if ( DesktopPlatform )
	{
		void* ParentWindowWindowHandle = NULL;

		IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
		const TSharedPtr<SWindow>& MainFrameParentWindow = MainFrameModule.GetParentWindow();
		if ( MainFrameParentWindow.IsValid() && MainFrameParentWindow->GetNativeWindow().IsValid() )
		{
			ParentWindowWindowHandle = MainFrameParentWindow->GetNativeWindow()->GetOSWindowHandle();
		}

		bOpened = DesktopPlatform->OpenFileDialog(
			ParentWindowWindowHandle,
			LOCTEXT("OpenProjectBrowseTitle", "Open Project").ToString(),
			LastBrowsePath,
			TEXT(""),
			LogVisualizer::FileTypes,
			EFileDialogFlags::None,
			OpenFilenames
			);
	}

	if ( bOpened )
	{
		if ( OpenFilenames.Num() > 0 )
		{
			LastBrowsePath = OpenFilenames[0];
			LoadFiles(OpenFilenames);
		}
	}

	DoFullUpdate();

	return FReply::Handled();
}

FReply SLogVisualizer::OnSave()
{
	// Prompt the user for the filenames
	TArray<FString> SaveFilenames;
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	bool bSaved = false;
	if ( DesktopPlatform )
	{
		void* ParentWindowWindowHandle = NULL;

		IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
		const TSharedPtr<SWindow>& MainFrameParentWindow = MainFrameModule.GetParentWindow();
		if ( MainFrameParentWindow.IsValid() && MainFrameParentWindow->GetNativeWindow().IsValid() )
		{
			ParentWindowWindowHandle = MainFrameParentWindow->GetNativeWindow()->GetOSWindowHandle();
		}

		bSaved = DesktopPlatform->SaveFileDialog(
			ParentWindowWindowHandle,
			LOCTEXT("NewProjectBrowseTitle", "Choose a project location").ToString(),
			LastBrowsePath,
			TEXT(""),
			LogVisualizer::FileTypes,
			EFileDialogFlags::None,
			SaveFilenames
			);
	}

	if ( bSaved )
	{
		if ( SaveFilenames.Num() > 0 )
		{
			LastBrowsePath = SaveFilenames[0];
			SaveSelectedLogs(SaveFilenames[0]);
			/*CurrentProjectFilePath = FPaths::GetPath(FPaths::GetPath(SaveFilenames[0]));
			CurrentProjectFileName = FPaths::GetBaseFilename(SaveFilenames[0]);*/
		}
	}

	return FReply::Handled();
}

FReply SLogVisualizer::OnRemove()
{
	TArray< TSharedPtr<FLogsListItem> > ItemsToRemove = LogsListWidget->GetSelectedItems();
	if (ItemsToRemove.Num() > 0)
	{
		TArray<int32> IndicesToRemove;
		IndicesToRemove.AddUninitialized(ItemsToRemove.Num());
	
		for (int32 ListItemIndex = 0; ListItemIndex < ItemsToRemove.Num(); ++ListItemIndex)
		{
			IndicesToRemove[ListItemIndex] = ItemsToRemove[ListItemIndex]->LogIndex;
		}

		IndicesToRemove.Sort();

		for (int32 LogToRemove = IndicesToRemove.Num() - 1; LogToRemove >= 0; --LogToRemove)
		{
			LogVisualizer->Logs.RemoveAtSwap(IndicesToRemove[LogToRemove], 1, false);

			const int32 IndexInList = FindIndexInLogsList(IndicesToRemove[LogToRemove]);
			if (IndexInList != INDEX_NONE)
			{
				LogsList.RemoveAtSwap(IndexInList);
			}
		}

		LogsListWidget->ClearSelection();

		RebuildFilteredList();
	}

	return FReply::Handled();
}

void SLogVisualizer::OnPauseChanged(ESlateCheckBoxState::Type NewState)
{
	UWorld* World = GetWorld();
	if (World != NULL)
	{
		if (NewState != ESlateCheckBoxState::Checked)
		{
			World->bPlayersOnly = false;
			World->bPlayersOnlyPending = false;

			ALogVisualizerCameraController::DisableCamera(World);
		}
		else
		{
			World->bPlayersOnlyPending = true;
			// switch debug cam on
			CameraController = ALogVisualizerCameraController::EnableCamera(World);
			if (CameraController.IsValid())
			{
				CameraController->OnActorSelected = ALogVisualizerCameraController::FActorSelectedDelegate::CreateSP(
					this, &SLogVisualizer::CameraActorSelected
				);
				CameraController->OnIterateLogEntries = ALogVisualizerCameraController::FLogEntryIterationDelegate::CreateSP(
					this, &SLogVisualizer::IncrementCurrentLogIndex
				);
			}
		}
	}
}

void SLogVisualizer::CameraActorSelected(AActor* SelectedActor)
{
	// find log corresponding to this Actor
	if (SelectedActor == NULL || LogVisualizer == NULL)
	{
		return;
	}

	SelectActor(SelectedActor);
}

void SLogVisualizer::SelectActor(AActor* SelectedActor)
{
	const AActor* LogOwner = SelectedActor->GetVisualLogRedirection();
	const int32 LogIndex = LogVisualizer->GetLogIndexForActor(LogOwner);
	if (LogVisualizer->Logs.IsValidIndex(LogIndex))
	{
		SelectedLogIndex = LogIndex;

		// find item pointing to given log index
		for (int32 ItemIndex = 0; ItemIndex < LogsList.Num(); ++ItemIndex)
		{
			if (LogsList[ItemIndex]->LogIndex == LogIndex)
			{
				TSharedPtr<FActorsVisLog> Log = LogVisualizer->Logs[SelectedLogIndex];
				ShowLogEntry(LogsList[ItemIndex], Log->Entries[Log->Entries.Num()-1]);
				break;
			}
		}
	}
}

void SLogVisualizer::FilterTextCommitted(const FText& CommentText, ETextCommit::Type CommitInfo)
{
	UpdateFilterInfo();
	DoFullUpdate();
}

FString SLogVisualizer::GetLogEntryStatusText() const
{
	return TEXT("Pause game with Pause button\nand select log entry to start viewing\nlog's content");
}

void SLogVisualizer::OnSortByChanged(const FName& ColumnName, EColumnSortMode::Type NewSortMode)
{
	SortBy = ELogsSortMode::ByName;

	if (ColumnName == NAME_StartTime)
	{
		SortBy = ELogsSortMode::ByStartTime;
	}
	else if (ColumnName == NAME_EndTime)
	{
		SortBy = ELogsSortMode::ByEndTime;
	}

	RebuildFilteredList();
}

EColumnSortMode::Type SLogVisualizer::GetLogsSortMode() const
{
	return (SortBy == ELogsSortMode::ByName) ? EColumnSortMode::Ascending : EColumnSortMode::None;
}

void SLogVisualizer::LoadFiles(TArray<FString>& OpenFilenames)
{
	for (int FilenameIndex = 0; FilenameIndex < OpenFilenames.Num(); ++FilenameIndex)
	{
		FArchive* FileAr = IFileManager::Get().CreateFileReader(*(OpenFilenames[FilenameIndex]));
		if (FileAr != NULL)
		{
			TSharedPtr<FJsonObject> Object;
			TSharedRef<TJsonReader<UCS2CHAR> > Reader = TJsonReader<UCS2CHAR>::Create(FileAr);

			if (FJsonSerializer::Deserialize(Reader, Object))
			{
				TArray< TSharedPtr<FJsonValue> > JsonLogs = Object->GetArrayField(VisualLogJson::TAG_LOGS);
				for (int32 LogIndex = 0; LogIndex < JsonLogs.Num(); ++LogIndex)
				{
					TSharedPtr<FJsonObject> JsonLogObject = JsonLogs[LogIndex]->AsObject();
					if (JsonLogObject.IsValid() != false)
					{
						if (JsonLogObject->HasTypedField<EJson::String>(VisualLogJson::TAG_NAME))
						{
							TSharedPtr<FActorsVisLog> NewLog = MakeShareable(new FActorsVisLog(JsonLogs[LogIndex]));
							LogVisualizer->AddLoadedLog(NewLog);
						}
					}
				}
				bIgnoreTrivialLogs = false;
			}

			FileAr->Close();
		}
	}

	if (OpenFilenames.Num() > 0)
	{
		RebuildFilteredList();
	}
}

void SLogVisualizer::SaveSelectedLogs(FString& Filename)
{
	TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);

	TArray< TSharedPtr<FJsonValue> > EntriesArray;
	TArray< TSharedPtr<FLogsListItem> > ItemsToSave = LogsListWidget->GetSelectedItems();
	if (ItemsToSave.Num() == 0)
	{
		// store all 
		ItemsToSave = LogsList;
	}

	EntriesArray.Reserve(ItemsToSave.Num());


	TSharedPtr<FLogsListItem>* LogListItem = ItemsToSave.GetTypedData();
	for (int32 ItemIndex = 0; ItemIndex < ItemsToSave.Num(); ++ItemIndex, ++LogListItem)
	{
		if (LogListItem->IsValid() && LogVisualizer->Logs.IsValidIndex((*LogListItem)->LogIndex))
		{
			TSharedPtr<FActorsVisLog> Log = LogVisualizer->Logs[(*LogListItem)->LogIndex];
			EntriesArray.Add(Log->ToJson());
		}
	}
	
	if (EntriesArray.Num() > 0)
	{
		Object->SetArrayField(VisualLogJson::TAG_LOGS, EntriesArray);

		FArchive* FileAr = IFileManager::Get().CreateFileWriter(*Filename);
		if (FileAr != NULL)
		{
			TSharedRef<TJsonWriter<UCS2CHAR> > Writer = TJsonWriter<UCS2CHAR>::Create(FileAr);
			FJsonSerializer::Serialize( Object.ToSharedRef(), Writer );		
			FileAr->Close();
		}
	}
}

#undef LOCTEXT_NAMESPACE

#endif //ENABLE_VISUAL_LOG
