// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

//=============================================================================
// TextBufferExporterTXT
//=============================================================================

#pragma once
#include "TextBufferExporterTXT.generated.h"

UCLASS()
class UTextBufferExporterTXT : public UExporter
{
	GENERATED_UCLASS_BODY()


	// Begin UExporter Interface
	virtual bool ExportText( const FExportObjectInnerContext* Context, UObject* Object, const TCHAR* Type, FOutputDevice& Ar, FFeedbackContext* Warn, uint32 PortFlags=0 ) override;
	// End UExporter Interface
};



