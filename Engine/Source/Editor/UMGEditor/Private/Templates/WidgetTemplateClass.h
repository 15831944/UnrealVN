// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WidgetTemplate.h"

class UWidget;

/**
 * A template that can spawn any widget derived from the UWidget class.
 */
class FWidgetTemplateClass : public FWidgetTemplate
{
public:
	/** Constructor */
	explicit FWidgetTemplateClass(TSubclassOf<UWidget> InWidgetClass);

	/** Destructor */
	virtual ~FWidgetTemplateClass();

	/** Gets the category for the widget */
	virtual FText GetCategory() const override;

	/** Creates an instance of the widget for the tree. */
	virtual UWidget* Create(UWidgetTree* Tree) override;

	/** The icon coming from the default object of the class */
	virtual const FSlateBrush* GetIcon() const override;

	/** Gets the tooltip widget for this palette item. */
	virtual TSharedRef<IToolTip> GetToolTip() const override;

protected:
	/** Creates a widget template class without any class reference */
	FWidgetTemplateClass();

	/** Called when objects need to be swapped out for new versions, like after a blueprint recompile. */
	void OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap);

	/** The widget class that will be created by this template */
	TWeakObjectPtr<UClass> WidgetClass;
};
