// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.


#include "PersonaPrivatePCH.h"
#include "SRigWindow.h"
#include "ObjectTools.h"
#include "ScopedTransaction.h"
#include "AssetRegistryModule.h"
#include "Editor/PropertyEditor/Public/PropertyEditorModule.h"
#include "Editor/ContentBrowser/Public/ContentBrowserModule.h"
#include "WorkflowOrientedApp/SContentReference.h"
#include "AssetNotifications.h"
#include "Animation/Rig.h"
#include "BoneSelectionWidget.h"
#include "SSearchBox.h"
#include "SInlineEditableTextBlock.h"
#include "SRigPicker.h"

#define LOCTEXT_NAMESPACE "SRigWindow"

static const FName ColumnId_NodeNameLabel( "Node Name" );
static const FName ColumnID_BoneNameLabel( "Bone" );

DECLARE_DELEGATE_TwoParams(FOnBoneMappingChanged, FName /** NodeName */, FName /** BoneName **/);
DECLARE_DELEGATE_RetVal_OneParam(FName, FOnGetBoneMapping, FName /** Node Name **/);

//////////////////////////////////////////////////////////////////////////
// SBoneMappingListRow

typedef TSharedPtr< FDisplayedBoneMappingInfo > FDisplayedBoneMappingInfoPtr;

class SBoneMappingListRow
	: public SMultiColumnTableRow< FDisplayedBoneMappingInfoPtr >
{
public:

	SLATE_BEGIN_ARGS( SBoneMappingListRow ) {}

		/** The item for this row **/
		SLATE_ARGUMENT( FDisplayedBoneMappingInfoPtr, Item )

		/* The SRigWindow that handles all retarget sources */
		SLATE_ARGUMENT( class SRigWindow*, RigWindow )

		/* Widget used to display the list of retarget sources*/
		SLATE_ARGUMENT( TSharedPtr<SBoneMappingListType>, BoneMappingListView )

		/* Persona used to update the viewport when a weight slider is dragged */
		SLATE_ARGUMENT( TWeakPtr<FPersona>, Persona )

		SLATE_EVENT( FOnBoneMappingChanged, OnBoneMappingChanged)

		SLATE_EVENT( FOnGetBoneMapping, OnGetBoneMapping)

	SLATE_END_ARGS()

	void Construct( const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTableView );

	/** Overridden from SMultiColumnTableRow.  Generates a widget for this column of the tree row. */
	virtual TSharedRef<SWidget> GenerateWidgetForColumn( const FName& ColumnName ) override;

private:

	/* The SRigWindow that handles all retarget sources*/
	// @todo remove
	SRigWindow* RigWindow;

	/** Widget used to display the list of retarget sources*/
	TSharedPtr<SBoneMappingListType> BoneMappingListView;

	/** The name and weight of the retarget source*/
	FDisplayedBoneMappingInfoPtr	Item;

	/** Pointer back to the Persona that owns us */
	TWeakPtr<FPersona> PersonaPtr;

	// Bone tree widget delegates
	void OnBoneSelectionChanged(FName Name);
	FReply OnClearButtonClicked();
	FName GetSelectedBone() const;

	FOnBoneMappingChanged OnBoneMappingChanged;
	FOnGetBoneMapping OnGetBoneMapping;
};

void SBoneMappingListRow::Construct( const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView )
{
	Item = InArgs._Item;
	RigWindow = InArgs._RigWindow;
	BoneMappingListView = InArgs._BoneMappingListView;
	OnBoneMappingChanged = InArgs._OnBoneMappingChanged;
	OnGetBoneMapping = InArgs._OnGetBoneMapping;

	PersonaPtr = InArgs._Persona;

	check( Item.IsValid() );

	SMultiColumnTableRow< FDisplayedBoneMappingInfoPtr >::Construct( FSuperRowType::FArguments(), InOwnerTableView );
}

TSharedRef< SWidget > SBoneMappingListRow::GenerateWidgetForColumn( const FName& ColumnName )
{
	if ( ColumnName == ColumnId_NodeNameLabel )
	{
		TSharedPtr< SInlineEditableTextBlock > InlineWidget;
		TSharedRef< SWidget > NewWidget = 
			SNew( SVerticalBox )

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding( 0.0f, 4.0f )
			.VAlign( VAlign_Center )
			[
				SAssignNew(InlineWidget, SInlineEditableTextBlock)
				.Text( FText::FromString(Item->GetDisplayName()) )
				.HighlightText( RigWindow->GetFilterText() )
				.IsReadOnly(true)
				.IsSelected(this, &SMultiColumnTableRow< FDisplayedBoneMappingInfoPtr >::IsSelectedExclusively)
			];

		return NewWidget;
	}
	else
	{
		check (Item->Skeleton);

		// show bone list
		// Encase the SSpinbox in an SVertical box so we can apply padding. Setting ItemHeight on the containing SListView has no effect :-(
		return
			SNew( SVerticalBox )

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding( 0.0f, 1.0f )
			.VAlign( VAlign_Center )
			[
				SNew(SHorizontalBox)

				+SHorizontalBox::Slot()
				[
					SNew(SBoneSelectionWidget)
					.Skeleton(Item->Skeleton)
					.Tooltip(FText::Format(LOCTEXT("BoneSelectinWidget", "Select Bone for node {0}"), FText::FromString(Item->GetDisplayName())))
					.OnBoneSelectionChanged(this, &SBoneMappingListRow::OnBoneSelectionChanged)
					.OnGetSelectedBone(this, &SBoneMappingListRow::GetSelectedBone)
				]

				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.OnClicked(FOnClicked::CreateSP(this, &SBoneMappingListRow::OnClearButtonClicked))
					.Text(FText::FromString(TEXT("x")))
				]
			];
	}
}

FReply SBoneMappingListRow::OnClearButtonClicked()
{
	if(OnBoneMappingChanged.IsBound())
	{
		OnBoneMappingChanged.Execute(Item->GetNodeName(), NAME_None);
	}

	return FReply::Handled();
}

void SBoneMappingListRow::OnBoneSelectionChanged(FName Name)
{
	if (OnBoneMappingChanged.IsBound())
	{
		OnBoneMappingChanged.Execute(Item->GetNodeName(), Name);
	}
}

FName SBoneMappingListRow::GetSelectedBone() const
{
	if (OnGetBoneMapping.IsBound())
	{
		return OnGetBoneMapping.Execute(Item->GetNodeName());
	}

	// @todo delete?
//	return Item->BoneName
	
	return NAME_None;
}

//////////////////////////////////////////////////////////////////////////
// SRigWindow

void SRigWindow::Construct(const FArguments& InArgs)
{
	PersonaPtr = InArgs._Persona;
	Skeleton = NULL;
	bDisplayAdvanced = false;

	if ( PersonaPtr.IsValid() )
	{
		Skeleton = PersonaPtr.Pin()->GetSkeleton();
		PersonaPtr.Pin()->RegisterOnPostUndo(FPersona::FOnPostUndo::CreateSP( this, &SRigWindow::PostUndo ) );
	}
	
	// @todo it will crash right nwo without Skeleton
	check (Skeleton);

	Skeleton->RefreshRigConfig();

	// show list of skeletalmeshes that they can choose from
	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	ChildSlot
	[
		SNew( SVerticalBox )

		// first add rig asset picker
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)

			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RigNameLabel", "Select Rig "))
				.Font(FEditorStyle::GetFontStyle("Persona.RetargetManager.BoldFont"))
			]

			+SHorizontalBox::Slot()
			[
				SAssignNew( AssetComboButton, SComboButton )
				//.ToolTipText( this, &SPropertyEditorAsset::OnGetToolTip )
				.ButtonStyle( FEditorStyle::Get(), "PropertyEditor.AssetComboStyle" )
				.ForegroundColor(FEditorStyle::GetColor("PropertyEditor.AssetName.ColorAndOpacity"))
				.OnGetMenuContent( this, &SRigWindow::MakeRigPickerWithMenu )
				.ContentPadding(2.0f)
				.ButtonContent()
				[
					// Show the name of the asset or actor
					SNew(STextBlock)
					.TextStyle( FEditorStyle::Get(), "PropertyEditor.AssetClass" )
					.Font( FEditorStyle::GetFontStyle( "PropertyWindow.NormalFont" ) )
					.Text(this,&SRigWindow::GetAssetName)
				]
			]

			+SHorizontalBox::Slot()
			.HAlign(HAlign_Right)
			[
				SNew(SButton)
				.OnClicked(FOnClicked::CreateSP(this, &SRigWindow::OnToggleAdvanced))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Text(this, &SRigWindow::GetAdvancedButtonText)
				.ToolTipText(LOCTEXT("ToggleAdvanced_Tooltip", "Toggle Base/Advanced configuration"))
			]
		]

		// now show bone mapping
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0,2)
		[
			SNew(SHorizontalBox)
			// Filter entry
			+SHorizontalBox::Slot()
			.FillWidth( 1 )
			[
				SAssignNew( NameFilterBox, SSearchBox )
				.SelectAllTextWhenFocused( true )
				.OnTextChanged( this, &SRigWindow::OnFilterTextChanged )
				.OnTextCommitted( this, &SRigWindow::OnFilterTextCommitted )
			]
		]

		+ SVerticalBox::Slot()
		.FillHeight( 1.0f )		// This is required to make the scrollbar work, as content overflows Slate containers by default
		[
			SAssignNew( BoneMappingListView, SBoneMappingListType )
			.ListItemsSource( &BoneMappingList )
			.OnGenerateRow( this, &SRigWindow::GenerateBoneMappingRow )
			.ItemHeight( 22.0f )
			.HeaderRow
			(
				SNew( SHeaderRow )
				+ SHeaderRow::Column( ColumnId_NodeNameLabel )
				.DefaultLabel( LOCTEXT( "RigWindow_NodeNameLabel", "Node (Rig)" ) )
				.FixedWidth(150.f)

				+ SHeaderRow::Column( ColumnID_BoneNameLabel )
				.DefaultLabel( LOCTEXT( "RigWindow_BoneNameLabel", "Bone (Skeleton)" ) )
			)
		]
	];

	CreateBoneMappingList();
}

void SRigWindow::OnFilterTextChanged( const FText& SearchText )
{
	FilterText = SearchText;

	CreateBoneMappingList( SearchText.ToString() );
}

void SRigWindow::OnFilterTextCommitted( const FText& SearchText, ETextCommit::Type CommitInfo )
{
	// Just do the same as if the user typed in the box
	OnFilterTextChanged( SearchText );
}

TSharedRef<ITableRow> SRigWindow::GenerateBoneMappingRow(TSharedPtr<FDisplayedBoneMappingInfo> InInfo, const TSharedRef<STableViewBase>& OwnerTable)
{
	check( InInfo.IsValid() );

	return
		SNew( SBoneMappingListRow, OwnerTable )
		.Persona( PersonaPtr )
		.Item( InInfo )
		.RigWindow( this )
		.BoneMappingListView( BoneMappingListView )
		.OnBoneMappingChanged(this, &SRigWindow::OnBoneMappingChanged)
		.OnGetBoneMapping(this, &SRigWindow::GetBoneMapping);
}

void SRigWindow::CreateBoneMappingList( const FString& SearchText)
{
	BoneMappingList.Empty();

	const URig* Rig = Skeleton->GetRig();

	if ( Rig )
	{
		bool bDoFiltering = !SearchText.IsEmpty();
		const TArray<FNode>& Nodes = Rig->GetNodes();

		for ( const auto Node : Nodes )
		{
			const FName& Name = Node.Name;
			const FString& DisplayName = Node.DisplayName;
			const FName& BoneName = Skeleton->GetRigBoneMapping(Name);

			if (Node.bAdvanced == bDisplayAdvanced)
			{
				if(bDoFiltering)
				{
					if(!Name.ToString().Contains(SearchText))
					{
						continue; // Skip items that don't match our filter
					}
					if(!DisplayName.Contains(SearchText))
					{
						continue;
					}
					if(!BoneName.ToString().Contains(SearchText))
					{
						continue;
					}
				}

				TSharedRef<FDisplayedBoneMappingInfo> Info = FDisplayedBoneMappingInfo::Make(Name, DisplayName, Skeleton);

				BoneMappingList.Add(Info);
			}
		}
	}

	BoneMappingListView->RequestListRefresh();
}


void SRigWindow::OnAssetSelected(UObject* Object)
{
	if (Skeleton)
	{
		AssetComboButton->SetIsOpen(false);

		const FScopedTransaction Transaction(LOCTEXT("RigAssetChanged", "Select Rig"));
		Skeleton->Modify();
		Skeleton->SetRigConfig(Cast<URig>(Object));
		CreateBoneMappingList(FilterText.ToString());

		FAssetNotifications::SkeletonNeedsToBeSaved(Skeleton);
	}
}

/** Returns true if the asset shouldn't show  */
bool SRigWindow::ShouldFilterAsset(const class FAssetData& AssetData)
{
	return (AssetData.GetAsset() == GetRigObject());
}

UObject* SRigWindow::GetRigObject() const
{
	return (Skeleton)? Skeleton->GetRig() : NULL;
}

SRigWindow::~SRigWindow()
{
	if (PersonaPtr.IsValid())
	{
		PersonaPtr.Pin()->UnregisterOnPostUndo(this);
	}
}

void SRigWindow::PostUndo()
{
	CreateBoneMappingList(FilterText.ToString());
}

void SRigWindow::OnBoneMappingChanged(FName NodeName, FName BoneName)
{
	const FScopedTransaction Transaction(LOCTEXT("BoneMappingChanged", "Change Bone Mapping"));
	Skeleton->Modify();

	Skeleton->SetRigBoneMapping(NodeName, BoneName);
}

FName SRigWindow::GetBoneMapping(FName NodeName)
{
	return Skeleton->GetRigBoneMapping(NodeName);
}

FReply SRigWindow::OnToggleAdvanced()
{
	bDisplayAdvanced = !bDisplayAdvanced;

	CreateBoneMappingList(FilterText.ToString());

	return FReply::Handled();
}

FText SRigWindow::GetAdvancedButtonText() const
{
	if (bDisplayAdvanced)
	{
		return LOCTEXT("ShowBase", "Show Base");
	}

	return LOCTEXT("ShowAdvanced", "Show Advanced");
}

TSharedRef<SWidget> SRigWindow::MakeRigPickerWithMenu()
{
	// rig asset picker
	return	
		SNew(SRigPicker)
		.InitialObject(Skeleton->GetRig())
		.OnShouldFilterAsset(this, &SRigWindow::ShouldFilterAsset)
		.OnSetReference(this, &SRigWindow::OnAssetSelected)
		.OnClose(this, &SRigWindow::CloseComboButton );
}

void SRigWindow::CloseComboButton()
{
	AssetComboButton->SetIsOpen(false);
}

FText SRigWindow::GetAssetName() const
{
	UObject* Rig = GetRigObject();
	if (Rig)
	{
		return FText::FromString(Rig->GetName());
	}

	return LOCTEXT("None", "None");
}

#undef LOCTEXT_NAMESPACE

