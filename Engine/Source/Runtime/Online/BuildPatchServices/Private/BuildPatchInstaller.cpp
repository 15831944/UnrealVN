// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	BuildPatchInstaller.cpp: Implements the FBuildPatchInstaller class which
	controls the process of installing a build described by a build manifest.
=============================================================================*/

#include "BuildPatchServicesPrivatePCH.h"

// Required for setting file compression flag
#if PLATFORM_WINDOWS
// Start of region that uses windows types
#include "AllowWindowsPlatformTypes.h"

#include <wtypes.h>
#include <ioapiset.h>
#include <winbase.h>
#include <fileapi.h>
#include <winioctl.h>

bool SetFileCompressionFlag(const FString& Filepath, bool bIsCompressed)
{
	// Get the file handle
	HANDLE FileHandle = ::CreateFile(
		*Filepath,								// Path to file
		GENERIC_READ | GENERIC_WRITE,			// Read and write access
		FILE_SHARE_READ | FILE_SHARE_WRITE,		// Share access for DeviceIoControl
		NULL,									// Default security
		OPEN_EXISTING,							// Open existing file
		FILE_ATTRIBUTE_NORMAL,					// No specific attributes
		NULL									// No template file handle
		);
	uint32 Error = ::GetLastError();
	if (FileHandle == NULL || FileHandle == INVALID_HANDLE_VALUE)
	{
		GLog->Logf(TEXT("BuildPatchServices: WARNING: Could not open file to set compression flag %d Error:%d File:%s"), bIsCompressed, Error, *Filepath);
		return false;
	}

	// Send the compression control code to the device
	uint16 Message = bIsCompressed ? COMPRESSION_FORMAT_DEFAULT : COMPRESSION_FORMAT_NONE;
	uint16* pMessage = &Message;
	DWORD Dummy = 0;
	LPDWORD pDummy = &Dummy;
	BOOL bSuccess = ::DeviceIoControl(
		FileHandle,								// The file handle
		FSCTL_SET_COMPRESSION,					// Control code
		pMessage,								// Our message
		sizeof(uint16),							//
		NULL,									// Not used
		0,										// Not used
		pDummy,									// The value returned will be meaningless, but is required
		NULL									// No overlap structure, we a running this synchronously
		);
	Error = ::GetLastError();
	const bool bFileSystemUnsupported = Error == ERROR_INVALID_FUNCTION;
	if (bSuccess == FALSE && bFileSystemUnsupported == false)
	{
		GLog->Logf(TEXT("BuildPatchServices: WARNING: Could not set compression flag %d Error:%d File:%s"), bIsCompressed, Error, *Filepath);
	}

	// Close the open file handle
	::CloseHandle(FileHandle);

	// We treat unsupported as not being a failure
	return bSuccess == TRUE || bFileSystemUnsupported;
}

bool SetExecutableFlag(const FString& Filepath)
{
	// Not implemented
	return true;
}
// End of region that uses windows types
#include "HideWindowsPlatformTypes.h"

#elif PLATFORM_MAC
bool SetFileCompressionFlag(const FString& Filepath, bool bIsCompressed) 
{
	// Not implemented
	return true;
}

bool SetExecutableFlag(const FString& Filepath)
{
	bool bSuccess = false;
	// Enable executable permission bit
	struct stat FileInfo;
	if (stat(TCHAR_TO_UTF8(*Filepath), &FileInfo) == 0)
	{
		bSuccess = chmod(TCHAR_TO_UTF8(*Filepath), FileInfo.st_mode | S_IXUSR | S_IXGRP | S_IXOTH) == 0;
	}
	return bSuccess;
}
#elif PLATFORM_LINUX
bool SetFileCompressionFlag(const FString& Filepath, bool bIsCompressed)
{
	// Not implemented
	return true;
}

bool SetExecutableFlag(const FString& Filepath)
{
	// Not implemented
	return true;
}
#endif

#define LOCTEXT_NAMESPACE "BuildPatchInstaller"

#define NUM_DOWNLOAD_READINGS	5

#define TIME_PER_READING		0.5

#define MEGABYTE_VALUE			1048576

#define KILOBYTE_VALUE			1024

#define NUM_FILE_MOVE_RETRIES	5

/* FBuildPatchInstaller implementation
*****************************************************************************/
FBuildPatchInstaller::FBuildPatchInstaller( FBuildPatchBoolManifestDelegate InOnCompleteDelegate, FBuildPatchAppManifestPtr CurrentManifest, FBuildPatchAppManifestRef InstallManifest, const FString& InInstallDirectory, const FString& InStagingDirectory, FBuildPatchInstallationInfo& InstallationInfoRef )
	: Thread( NULL )
	, OnCompleteDelegate( InOnCompleteDelegate )
	, CurrentBuildManifest( CurrentManifest )
	, NewBuildManifest( InstallManifest )
	, InstallDirectory( InInstallDirectory )
	, StagingDirectory( InStagingDirectory )
	, DataStagingDir( InStagingDirectory / TEXT( "PatchData" ) )
	, InstallStagingDir( InStagingDirectory / TEXT( "Install" ) )
	, PreviousMoveMarker( InstallDirectory / TEXT( "$movedMarker" ) )
	, ThreadLock()
	, bIsFileData( InstallManifest->IsFileDataManifest() )
	, bIsChunkData( !bIsFileData )
	, bSuccess( true )
	, bIsRunning( false )
	, bIsInited( false )
	, DownloadSpeedValue( 0.0 )
	, DownloadBytesLeft( 0 )
	, BuildStats()
	, BuildProgress()
	, InitialNumChunkDownloads( 0.0f )
	, InitialNumChunkConstructions( 0.0f )
	, TotalInitialDownloadSize( 0 )
	, TimePausedAt( 0.0 )
	, InstallationInfo( InstallationInfoRef )
{
	bIsRepairing = CurrentManifest.IsValid() && CurrentManifest->IsSameAs(InstallManifest);
	// Start thread!
	const TCHAR* ThreadName = TEXT( "BuildPatchInstallerThread" );
	Thread = FRunnableThread::Create(this, ThreadName);
}

FBuildPatchInstaller::~FBuildPatchInstaller()
{
	if( Thread != NULL )
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = NULL;
	}
}

bool FBuildPatchInstaller::Init()
{
	// Make sure the installation directory exists
	IFileManager::Get().MakeDirectory( *InstallDirectory, true );

	// Init build stats that count
	BuildStats.ProcessPausedTime = 0.0f;

	// We are ready to go if our delegates are bound and directories successfully created
	bool bInstallerInitSuccess = OnCompleteDelegate.IsBound();
	bInstallerInitSuccess &= IFileManager::Get().DirectoryExists( *InstallDirectory );

	// Currently we don't handle init failures, so make sure we are not missing them
	check( bInstallerInitSuccess );
	return bInstallerInitSuccess;
}

uint32 FBuildPatchInstaller::Run()
{
	// Make sure this function can never be parallelized
	static FCriticalSection SingletonFunctionLockCS;
	FScopeLock SingletonFunctionLock(&SingletonFunctionLockCS);
	FBuildPatchInstallError::Reset();

	SetRunning(true);
	SetInited(true);
	SetDownloadSpeed(-1);
	UpdateDownloadProgressInfo(true);

	// Register the current manifest with the installation info, to make sure we pull from it
	if (CurrentBuildManifest.IsValid())
	{
		InstallationInfo.RegisterAppInstallation(CurrentBuildManifest.ToSharedRef(), InstallDirectory);
	}

	// Keep track of files that failed verify
	TArray<FString> CorruptFiles;

	// Init prereqs progress value
	const bool bInstallPrereqs = !CurrentBuildManifest.IsValid() && !NewBuildManifest->GetPrereqPath().IsEmpty();

	// Get the start time
	double StartTime = FPlatformTime::Seconds();
	double CleanUpTime = 0;

	// Keep retrying the install while it is not canceled, or caused by download error
	bool bProcessSuccess = false;
	bool bCanRetry = true;
	int32 InstallRetries = 5;
	while (!bProcessSuccess && bCanRetry)
	{
		// Run the install
		bool bInstallSuccess = RunInstallation(CorruptFiles);
		BuildProgress.SetStateProgress(EBuildPatchProgress::PrerequisitesInstall, bInstallPrereqs ? 0.0f : 1.0f);
		if (bInstallSuccess)
		{
			BuildProgress.SetStateProgress(EBuildPatchProgress::Downloading, 1.0f);
			BuildProgress.SetStateProgress(EBuildPatchProgress::Installing, 1.0f);
		}

		// Backup local changes then move generated files
		bInstallSuccess = bInstallSuccess && RunBackupAndMove();

		// Setup file attributes
		bInstallSuccess = bInstallSuccess && RunFileAttributes(bIsRepairing);

		// Run Verification
		CorruptFiles.Empty();
		BuildProgress.SetStateProgress(EBuildPatchProgress::Initializing, 1.0f);
		bProcessSuccess = bInstallSuccess && RunVerification(CorruptFiles);

		// Clean staging if INSTALL success
		if (bInstallSuccess)
		{
			GLog->Logf(TEXT("BuildPatchServices: Deleting staging area"));
			CleanUpTime = FPlatformTime::Seconds();
			IFileManager::Get().DeleteDirectory(*StagingDirectory, false, true);
			CleanUpTime = FPlatformTime::Seconds() - CleanUpTime;
		}
		BuildProgress.SetStateProgress(EBuildPatchProgress::CleanUp, 1.0f);

		// Set if we can retry
		--InstallRetries;
		bCanRetry = InstallRetries > 0 && !FBuildPatchInstallError::IsInstallationCancelled() && !FBuildPatchInstallError::IsNoRetryError();

		// If successful or we will retry, remove the moved files marker
		if (bProcessSuccess || bCanRetry)
		{
			GLog->Logf(TEXT("BuildPatchServices: Reset MM"));
			IFileManager::Get().Delete(*PreviousMoveMarker, false, true);
		}
	}

	if (bProcessSuccess)
	{
		// Run the prerequisites installer if this is our first install and the manifest has prerequisites info
		if (bInstallPrereqs)
		{
			// @TODO: We also want to trigger prereq install if this is an update and the prereq installer differs in the update
			bProcessSuccess &= RunPrereqInstaller();
		}
	}

	// Set final stat values and log out results
	{
		FScopeLock Lock(&ThreadLock);
		bSuccess = bProcessSuccess;
		BuildStats.ProcessSuccess = bProcessSuccess;
		BuildStats.ProcessExecuteTime = (FPlatformTime::Seconds() - StartTime) - BuildStats.ProcessPausedTime;
		BuildStats.FailureReason = FBuildPatchInstallError::GetErrorString();
		BuildStats.FailureReasonText = FBuildPatchInstallError::GetErrorText();
		BuildStats.CleanUpTime = CleanUpTime;

		// Log stats
		GLog->Logf(TEXT("BuildPatchServices: Build Stat: AppName: %s"), *BuildStats.AppName);
		GLog->Logf(TEXT("BuildPatchServices: Build Stat: AppInstalledVersion: %s"), *BuildStats.AppInstalledVersion);
		GLog->Logf(TEXT("BuildPatchServices: Build Stat: AppPatchVersion: %s"), *BuildStats.AppPatchVersion);
		GLog->Logf(TEXT("BuildPatchServices: Build Stat: CloudDirectory: %s"), *BuildStats.CloudDirectory);
		GLog->Logf(TEXT("BuildPatchServices: Build Stat: NumFilesInBuild: %u"), BuildStats.NumFilesInBuild);
		GLog->Logf(TEXT("BuildPatchServices: Build Stat: NumFilesOutdated: %u"), BuildStats.NumFilesOutdated);
		GLog->Logf(TEXT("BuildPatchServices: Build Stat: NumFilesToRemove: %u"), BuildStats.NumFilesToRemove);
		GLog->Logf(TEXT("BuildPatchServices: Build Stat: NumChunksRequired: %u"), BuildStats.NumChunksRequired);
		GLog->Logf(TEXT("BuildPatchServices: Build Stat: ChunksQueuedForDownload: %u"), BuildStats.ChunksQueuedForDownload);
		GLog->Logf(TEXT("BuildPatchServices: Build Stat: ChunksLocallyAvailable: %u"), BuildStats.ChunksLocallyAvailable);
		GLog->Logf(TEXT("BuildPatchServices: Build Stat: NumChunksDownloaded: %u"), BuildStats.NumChunksDownloaded);
		GLog->Logf(TEXT("BuildPatchServices: Build Stat: NumChunksRecycled: %u"), BuildStats.NumChunksRecycled);
		GLog->Logf(TEXT("BuildPatchServices: Build Stat: NumChunksCacheBooted: %u"), BuildStats.NumChunksCacheBooted);
		GLog->Logf(TEXT("BuildPatchServices: Build Stat: NumDriveCacheChunkLoads: %u"), BuildStats.NumDriveCacheChunkLoads);
		GLog->Logf(TEXT("BuildPatchServices: Build Stat: NumRecycleFailures: %u"), BuildStats.NumRecycleFailures);
		GLog->Logf(TEXT("BuildPatchServices: Build Stat: NumDriveCacheLoadFailures: %u"), BuildStats.NumDriveCacheLoadFailures);
		GLog->Logf(TEXT("BuildPatchServices: Build Stat: TotalDownloadedData: %lld"), BuildStats.TotalDownloadedData);
		GLog->Logf(TEXT("BuildPatchServices: Build Stat: AverageDownloadSpeed: %.3f MB/sec"), BuildStats.AverageDownloadSpeed / 1024.0 / 1024.0);
		GLog->Logf(TEXT("BuildPatchServices: Build Stat: TheoreticalDownloadTime: %s"), *FPlatformTime::PrettyTime(BuildStats.TheoreticalDownloadTime));
		GLog->Logf(TEXT("BuildPatchServices: Build Stat: VerifyTime: %s"), *FPlatformTime::PrettyTime(BuildStats.VerifyTime));
		GLog->Logf(TEXT("BuildPatchServices: Build Stat: CleanUpTime: %s"), *FPlatformTime::PrettyTime(BuildStats.CleanUpTime));
		GLog->Logf(TEXT("BuildPatchServices: Build Stat: ProcessExecuteTime: %s"), *FPlatformTime::PrettyTime(BuildStats.ProcessExecuteTime));
		GLog->Logf(TEXT("BuildPatchServices: Build Stat: ProcessPausedTime: %.1f sec"), BuildStats.ProcessPausedTime);
		GLog->Logf(TEXT("BuildPatchServices: Build Stat: ProcessSuccess: %s"), BuildStats.ProcessSuccess ? TEXT("TRUE") : TEXT("FALSE"));
		GLog->Logf(TEXT("BuildPatchServices: Build Stat: FailureReason: %s"), *BuildStats.FailureReason);
		GLog->Logf(TEXT("BuildPatchServices: Build Stat: FailureReasonText: %s"), *BuildStats.FailureReasonText.BuildSourceString());
	}

	// Mark that we are done
	SetRunning(false);

	return bSuccess ? 0 : 1;
}

bool FBuildPatchInstaller::RunInstallation(TArray<FString>& CorruptFiles)
{
	GLog->Logf(TEXT("BuildPatchServices: Starting Installation"));
	// Save the staging directories
	FPaths::NormalizeDirectoryName(DataStagingDir);
	FPaths::NormalizeDirectoryName(InstallStagingDir);

	// Make sure staging directories exist
	IFileManager::Get().MakeDirectory(*DataStagingDir, true);
	IFileManager::Get().MakeDirectory(*InstallStagingDir, true);

	// Reset any error from a previous install
	FBuildPatchInstallError::Reset();
	FBuildPatchAnalytics::ResetCounters();
	BuildProgress.Reset();
	BuildProgress.SetStateProgress(EBuildPatchProgress::Initializing, 0.01f);
	BuildProgress.SetStateProgress(EBuildPatchProgress::CleanUp, 0.0f);

	// Remove any inventory
	FBuildPatchFileConstructor::PurgeFileDataInventory();

	// Check if we should skip out of this process
	bool bPreviousStagingCompleted = FPaths::FileExists(PreviousMoveMarker);
	if (bPreviousStagingCompleted)
	{
		GLog->Logf(TEXT("BuildPatchServices: Detected previous staging completed"));
		// Set weights for verify only
		BuildProgress.SetStateWeight(EBuildPatchProgress::Downloading, 0.0f);
		BuildProgress.SetStateWeight(EBuildPatchProgress::Installing, 0.0f);
		BuildProgress.SetStateWeight(EBuildPatchProgress::MovingToInstall, 0.0f);
		BuildProgress.SetStateWeight(EBuildPatchProgress::BuildVerification, 1.0f);
		// Mark all installation steps complete
		BuildProgress.SetStateProgress(EBuildPatchProgress::Initializing, 1.0f);
		BuildProgress.SetStateProgress(EBuildPatchProgress::Resuming, 1.0f);
		BuildProgress.SetStateProgress(EBuildPatchProgress::Downloading, 1.0f);
		BuildProgress.SetStateProgress(EBuildPatchProgress::Installing, 1.0f);
		BuildProgress.SetStateProgress(EBuildPatchProgress::MovingToInstall, 1.0f);
		return true;
	}

	// Get the list of files needing construction
	TArray< FString > FilesToConstruct;
	if (CorruptFiles.Num() > 0)
	{
		FilesToConstruct.Append(CorruptFiles);
	}
	else
	{
		FBuildPatchAppManifest::GetOutdatedFiles(CurrentBuildManifest, NewBuildManifest, InstallDirectory, FilesToConstruct);
	}
	GLog->Logf(TEXT("BuildPatchServices: Requiring %d files"), FilesToConstruct.Num());

	// Create the downloader
	FBuildPatchDownloader::Create(DataStagingDir, NewBuildManifest, &BuildProgress);

	// Create chunk cache
	if (bIsChunkData)
	{
		FBuildPatchChunkCache::Init(NewBuildManifest, CurrentBuildManifest, DataStagingDir, InstallDirectory, &BuildProgress, FilesToConstruct, InstallationInfo);
	}

	// Hold the file constructor thread
	FBuildPatchFileConstructor* FileConstructor = NULL;

	// Store some totals
	const uint32 NumFilesInBuild = NewBuildManifest->GetNumFiles();

	// Stats for build
	const uint32 NumFilesToConstruct = bIsFileData ? NumFilesInBuild : FBuildPatchChunkCache::Get().GetStatNumFilesToConstruct();
	const uint32 NumRequiredChunks = bIsFileData ? NumFilesInBuild : FBuildPatchChunkCache::Get().GetStatNumRequiredChunks();
	const uint32 NumChunksToDownload = bIsFileData ? NumFilesInBuild : FBuildPatchChunkCache::Get().GetStatNumChunksToDownload();
	const uint32 NumChunksToConstruct = bIsFileData ? 0 : FBuildPatchChunkCache::Get().GetStatNumChunksToRecycle();

	// Save stats
	{
		FScopeLock Lock(&ThreadLock);
		BuildStats.AppName = NewBuildManifest->GetAppName();
		BuildStats.AppPatchVersion = NewBuildManifest->GetVersionString();
		BuildStats.AppInstalledVersion = CurrentBuildManifest.IsValid() ? CurrentBuildManifest->GetVersionString() : TEXT("NONE");
		BuildStats.CloudDirectory = FBuildPatchServicesModule::GetCloudDirectory();
		BuildStats.NumFilesInBuild = NumFilesInBuild;
		BuildStats.NumFilesOutdated = NumFilesToConstruct;
		BuildStats.NumChunksRequired = NumRequiredChunks;
		BuildStats.ChunksQueuedForDownload = NumChunksToDownload;
		BuildStats.ChunksLocallyAvailable = NumChunksToConstruct;
	}

	// Save initial counts as float for use with progress updates
	InitialNumChunkDownloads = NumChunksToDownload;
	InitialNumChunkConstructions = NumChunksToConstruct;

	// Setup some weightings for the progress tracking
	const float NumRequiredChunksFloat = NumRequiredChunks;
	BuildProgress.SetStateWeight(EBuildPatchProgress::Downloading, NumRequiredChunksFloat > 0.0f ? InitialNumChunkDownloads / NumRequiredChunksFloat : 0.0f);
	BuildProgress.SetStateWeight(EBuildPatchProgress::Installing, NumRequiredChunksFloat > 0.0f ? 0.1f + (InitialNumChunkConstructions / NumRequiredChunksFloat) : 0.0f);
	BuildProgress.SetStateWeight(EBuildPatchProgress::MovingToInstall, NumFilesToConstruct > 0 ? 0.05f : 0.0f);
	// A verify weight of 1 / 9 will make it 10% of the total progress
	BuildProgress.SetStateWeight(EBuildPatchProgress::BuildVerification, 1.1f / 9.0f);

	// If this is a repair operation, start off with install and download complete
	if (bIsRepairing)
	{
		GLog->Logf(TEXT("BuildPatchServices: Performing a repair operation"));
		BuildProgress.SetStateProgress(EBuildPatchProgress::Downloading, 1.0f);
		BuildProgress.SetStateProgress(EBuildPatchProgress::Installing, 1.0f);
		BuildProgress.SetStateProgress(EBuildPatchProgress::MovingToInstall, 1.0f);
	}

	// Start the file constructor
	GLog->Logf(TEXT("BuildPatchServices: Starting file contruction worker"));
	FileConstructor = new FBuildPatchFileConstructor(CurrentBuildManifest, NewBuildManifest, InstallDirectory, InstallStagingDir, FilesToConstruct, &BuildProgress);

	// Initializing is now complete if we are constructing files
	BuildProgress.SetStateProgress(EBuildPatchProgress::Initializing, NumFilesToConstruct > 0 ? 1.0f : 0.0f);

	// If this is file data, queue the download list
	if (bIsFileData)
	{
		TArray< FGuid > RequiredFileData;
		NewBuildManifest->GetChunksRequiredForFiles(FilesToConstruct, RequiredFileData);
		FBuildPatchDownloader::Get().AddChunksToDownload(RequiredFileData);
		TotalInitialDownloadSize = NewBuildManifest->GetDataSize(RequiredFileData);
	}
	else
	{
		TotalInitialDownloadSize = FBuildPatchChunkCache::Get().GetStatTotalChunkDownloadSize();
	}

	// Wait for the file constructor to complete
	while (FileConstructor->IsComplete() == false)
	{
		UpdateDownloadProgressInfo();
		FPlatformProcess::Sleep(0.1f);
	}
	FileConstructor->Wait();
	delete FileConstructor;
	FileConstructor = NULL;
	GLog->Logf(TEXT("BuildPatchServices: File construction complete"));

	// Wait for downloader to complete
	FBuildPatchDownloader::Get().NotifyNoMoreChunksToAdd();
	while (FBuildPatchDownloader::Get().IsComplete() == false)
	{
		UpdateDownloadProgressInfo();
		FPlatformProcess::Sleep(0.0f);
	}
	TArray< FBuildPatchDownloadRecord > AllChunkDownloads = FBuildPatchDownloader::Get().GetDownloadRecordings();
	SetDownloadSpeed(-1);

	// Calculate the average download speed from the recordings
	// NB: Because we are threading several downloads at once this is not simply averaging every download. We have to know about
	//     how much data is being received simultaneously too. We also need to ignore download pauses.
	int64 TotalDownloadedBytes = 0;
	double TotalTimeDownloading = 0;
	double RecoredEndTime = 0;
	if (AllChunkDownloads.Num() > 0)
	{
		// Sort by start time
		AllChunkDownloads.Sort();
		// Start with first record
		TotalTimeDownloading = AllChunkDownloads[0].EndTime - AllChunkDownloads[0].StartTime;
		TotalDownloadedBytes = AllChunkDownloads[0].DownloadSize;
		RecoredEndTime = AllChunkDownloads[0].EndTime;
		// For every other record..
		for (int32 RecordIdx = 1; RecordIdx < AllChunkDownloads.Num(); ++RecordIdx)
		{
			// Do we have some time to count
			if (RecoredEndTime < AllChunkDownloads[RecordIdx].EndTime)
			{
				// Was there a break in downloading
				if (AllChunkDownloads[RecordIdx].StartTime > RecoredEndTime)
				{
					TotalTimeDownloading += AllChunkDownloads[RecordIdx].EndTime - AllChunkDownloads[RecordIdx].StartTime;
				}
				// Otherwise don't count time overlap
				else
				{
					TotalTimeDownloading += AllChunkDownloads[RecordIdx].EndTime - RecoredEndTime;
				}
				RecoredEndTime = AllChunkDownloads[RecordIdx].EndTime;
			}
			// Count all bytes
			TotalDownloadedBytes += AllChunkDownloads[RecordIdx].DownloadSize;
		}
	}

	// Set final stats
	{
		FScopeLock Lock(&ThreadLock);
		BuildStats.TotalDownloadedData = TotalDownloadedBytes;
		BuildStats.NumChunksDownloaded = AllChunkDownloads.Num();
		const double TotalDownloadedBytesDouble = TotalDownloadedBytes;
		BuildStats.AverageDownloadSpeed = TotalTimeDownloading > 0 ? TotalDownloadedBytesDouble / TotalTimeDownloading : 0;
		BuildStats.TheoreticalDownloadTime = TotalTimeDownloading;
		BuildStats.NumChunksRecycled = bIsFileData ? 0 : FBuildPatchChunkCache::Get().GetCounterChunksRecycled();
		BuildStats.NumChunksCacheBooted = bIsFileData ? 0 : FBuildPatchChunkCache::Get().GetCounterChunksCacheBooted();
		BuildStats.NumDriveCacheChunkLoads = bIsFileData ? 0 : FBuildPatchChunkCache::Get().GetCounterDriveCacheChunkLoads();
		BuildStats.NumRecycleFailures = bIsFileData ? 0 : FBuildPatchChunkCache::Get().GetCounterRecycleFailures();
		BuildStats.NumDriveCacheLoadFailures = bIsFileData ? 0 : FBuildPatchChunkCache::Get().GetCounterDriveCacheLoadFailures();
	}

	// Perform static cleanup
	if (bIsChunkData)
	{
		FBuildPatchChunkCache::Shutdown();
	}
	FBuildPatchDownloader::Shutdown();
	FBuildPatchFileConstructor::PurgeFileDataInventory();

	GLog->Logf(TEXT("BuildPatchServices: Staged install complete"));

	return !FBuildPatchInstallError::HasFatalError();
}

bool FBuildPatchInstaller::RunBackupAndMove()
{
	GLog->Logf(TEXT("BuildPatchServices: Running backup and stage relocation"));
	// If there's no error, move all complete files
	bool bMoveSuccess = FBuildPatchInstallError::HasFatalError() == false;
	if (bMoveSuccess)
	{
		// First handle files that should be removed for patching
		TArray< FString > FilesToRemove;
		if (CurrentBuildManifest.IsValid())
		{
			FBuildPatchAppManifest::GetRemovableFiles(CurrentBuildManifest.ToSharedRef(), NewBuildManifest, FilesToRemove);
		}
		// Add to build stats
		ThreadLock.Lock();
		BuildStats.NumFilesToRemove = FilesToRemove.Num();
		ThreadLock.Unlock();
		for (const FString& OldFilename : FilesToRemove)
		{
			BackupFileIfNecessary(OldFilename);
			const bool bDeleteSuccess = IFileManager::Get().Delete(*(InstallDirectory / OldFilename), false, true, true);
			const uint32 LastError = FPlatformMisc::GetLastError();
			GLog->Logf(TEXT("BuildPatchServices: Removed (%u,%u) %s"), bDeleteSuccess ? 1 : 0, LastError, *OldFilename);
		}

		// Now handle files that have been constructed
		bool bSavedMoveMarkerFile = false;
		TArray< FString > ConstructionFiles;
		NewBuildManifest->GetFileList(ConstructionFiles);
		BuildProgress.SetStateProgress(EBuildPatchProgress::MovingToInstall, 0.0f);
		const float NumConstructionFilesFloat = ConstructionFiles.Num();
		for (auto ConstructionFilesIt = ConstructionFiles.CreateConstIterator(); ConstructionFilesIt && bMoveSuccess && !FBuildPatchInstallError::HasFatalError(); ++ConstructionFilesIt)
		{
			const FString& ConstructionFile = *ConstructionFilesIt;
			const FString SrcFilename = InstallStagingDir / ConstructionFile;
			const FString DestFilename = InstallDirectory / ConstructionFile;
			const float FileIndexFloat = ConstructionFilesIt.GetIndex();
			// Skip files not constructed
			if (!FPaths::FileExists(SrcFilename))
			{
				BuildProgress.SetStateProgress(EBuildPatchProgress::MovingToInstall, FileIndexFloat / NumConstructionFilesFloat);
				continue;
			}
			// Create the move marker file
			if (!bSavedMoveMarkerFile)
			{
				bSavedMoveMarkerFile = true;
				GLog->Logf(TEXT("BuildPatchServices: Create MM"));
				FArchive* MoveMarkerFile = IFileManager::Get().CreateFileWriter(*PreviousMoveMarker, FILEWRITE_EvenIfReadOnly);
				if (MoveMarkerFile != NULL)
				{
					MoveMarkerFile->Close();
					delete MoveMarkerFile;
				}
				// Make sure we have some progress if we do some work
				if (BuildProgress.GetStateWeight(EBuildPatchProgress::MovingToInstall) <= 0.0f)
				{
					BuildProgress.SetStateWeight(EBuildPatchProgress::MovingToInstall, 0.1f);
				}
			}
			// Backup file if need be
			BackupFileIfNecessary(ConstructionFile);
			// Move the file to the installation directory
			int32 MoveRetries = NUM_FILE_MOVE_RETRIES;
			bMoveSuccess = IFileManager::Get().Move(*DestFilename, *SrcFilename, true, true);
			uint32 ErrorCode = FPlatformMisc::GetLastError();
			while (!bMoveSuccess && MoveRetries > 0)
			{
				--MoveRetries;
				FBuildPatchAnalytics::RecordConstructionError(ConstructionFile, ErrorCode, TEXT("Failed To Move"));
				GWarn->Logf(TEXT("BuildPatchServices: ERROR: Failed to move file %s (%d), trying copy"), *ConstructionFile, ErrorCode);
				bMoveSuccess = IFileManager::Get().Copy(*DestFilename, *SrcFilename, true, true) == COPY_OK;
				ErrorCode = FPlatformMisc::GetLastError();
				if (!bMoveSuccess)
				{
					GWarn->Logf(TEXT("BuildPatchServices: ERROR: Failed to copy file %s (%d), retying after 0.5 sec"), *ConstructionFile, ErrorCode);
					FPlatformProcess::Sleep(0.5f);
					--MoveRetries;
					bMoveSuccess = IFileManager::Get().Move(*DestFilename, *SrcFilename, true, true);
					ErrorCode = FPlatformMisc::GetLastError();
				}
				else
				{
					IFileManager::Get().Delete(*SrcFilename, false, true, false);
				}
			}
			if (!bMoveSuccess)
			{
				GWarn->Logf(TEXT("BuildPatchServices: ERROR: Failed to move file %s"), *FPaths::GetCleanFilename(ConstructionFile));
				FBuildPatchInstallError::SetFatalError(EBuildPatchInstallError::MoveFileToInstall);
			}
			else
			{
				FilesInstalled.Add(ConstructionFile);
				BuildProgress.SetStateProgress(EBuildPatchProgress::MovingToInstall, FileIndexFloat / NumConstructionFilesFloat);
			}
		}

		bMoveSuccess = bMoveSuccess && (FBuildPatchInstallError::HasFatalError() == false);
		if (bMoveSuccess)
		{
			BuildProgress.SetStateProgress(EBuildPatchProgress::MovingToInstall, 1.0f);
		}
	}
	GLog->Logf(TEXT("BuildPatchServices: Relocation complete %d"), bMoveSuccess?1:0);
	return bMoveSuccess;
}

bool FBuildPatchInstaller::RunFileAttributes(bool bForce)
{
	// We need to set attributes for all files in the new build that require it
	for (const FFileManifestData& FileManifest : NewBuildManifest->Data->FileManifestList)
	{
		// Break if quitting
		if (FBuildPatchInstallError::HasFatalError())
		{
			break;
		}
		// Apply
		const bool bHasAttrib = FileManifest.bIsReadOnly || FileManifest.bIsCompressed || FileManifest.bIsUnixExecutable;
		if (bHasAttrib || bForce)
		{
			FString DestFilename = InstallDirectory / FileManifest.Filename;
			SetupFileAttributes(DestFilename, FileManifest);
		}
	}

	// We also need to check if any attributes have been removed, unless we forced anyway
	if (CurrentBuildManifest.IsValid() && !bForce)
	{
		for (const FFileManifestData& OldFileManifest : CurrentBuildManifest->Data->FileManifestList)
		{
			// Break if quitting
			if (FBuildPatchInstallError::HasFatalError())
			{
				break;
			}
			const FFileManifestData* const* NewFileManifestPtr = NewBuildManifest->FileManifestLookup.Find(OldFileManifest.Filename);
			if (NewFileManifestPtr != nullptr)
			{
				const FFileManifestData& NewFileManifest = **NewFileManifestPtr;
				const bool bAttribChanged = (OldFileManifest.bIsReadOnly && !NewFileManifest.bIsReadOnly) || (OldFileManifest.bIsCompressed && !NewFileManifest.bIsCompressed);
				if (bAttribChanged)
				{
					FString DestFilename = InstallDirectory / OldFileManifest.Filename;
					SetupFileAttributes(DestFilename, NewFileManifest);
				}
			}
		}
	}

	// We don't fail on this step currently
	return true;
}

bool FBuildPatchInstaller::RunVerification(TArray< FString >& CorruptFiles)
{
	// Make sure this function can never be parallelized
	static FCriticalSection SingletonFunctionLockCS;
	FScopeLock SingletonFunctionLock(&SingletonFunctionLockCS);

	BuildProgress.SetStateProgress(EBuildPatchProgress::BuildVerification, 0.0f);

	// Verify the installation
	double VerifyTime = 0;
	double VerifyPauseTime;
	GLog->Logf(TEXT("BuildPatchServices: Verifying install"));
	CorruptFiles.Empty();
	VerifyTime = FPlatformTime::Seconds();
	bool bVerifySuccess = NewBuildManifest->VerifyAgainstDirectory(InstallDirectory, CorruptFiles, FBuildPatchFloatDelegate::CreateRaw(this, &FBuildPatchInstaller::UpdateVerificationProgress), FBuildPatchBoolRetDelegate::CreateRaw(this, &FBuildPatchInstaller::IsPaused), VerifyPauseTime);
	VerifyTime = FPlatformTime::Seconds() - VerifyTime - VerifyPauseTime;
	if (!bVerifySuccess)
	{
		FString ErrorString = TEXT("Build verification failed on ");
		ErrorString += FString::Printf(TEXT("%d"), CorruptFiles.Num());
		ErrorString += TEXT(" file(s)");
		FBuildPatchInstallError::SetFatalError(EBuildPatchInstallError::BuildVerifyFail, ErrorString);
	}

	ThreadLock.Lock();
	BuildStats.VerifyTime = VerifyTime;
	ThreadLock.Unlock();

	BuildProgress.SetStateProgress(EBuildPatchProgress::BuildVerification, 1.0f);

	// Delete/Backup any incorrect files if failure was not cancellation
	if (!FBuildPatchInstallError::IsInstallationCancelled())
	{
		for (const FString& CorruptFile : CorruptFiles)
		{
			BackupFileIfNecessary(CorruptFile, true);
			IFileManager::Get().Delete(*(InstallDirectory / CorruptFile), false, true);
			IFileManager::Get().Delete(*(InstallStagingDir / CorruptFile), false, true);
		}
	}

	GLog->Logf(TEXT("BuildPatchServices: Verify stage complete %d"), bVerifySuccess ? 1 : 0);

	return bVerifySuccess;
}

bool FBuildPatchInstaller::BackupFileIfNecessary(const FString& Filename, bool bDiscoveredByVerification /*= false */)
{
	const FString BackupDirectory = FBuildPatchServicesModule::GetBackupDirectory();
	const FString InstalledFilename = InstallDirectory / Filename;
	const FString BackupFilename = BackupDirectory / Filename;
	const bool bBackupOriginals = !BackupDirectory.IsEmpty();
	// Skip if not doing backups
	if (!bBackupOriginals)
	{
		return true;
	}
	// Skip if no file to backup
	const bool bInstalledFileExists = FPaths::FileExists(InstalledFilename);
	if (!bInstalledFileExists)
	{
		return true;
	}
	// Skip if already backed up
	const bool bAlreadyBackedUp = FPaths::FileExists(BackupFilename);
	if (bAlreadyBackedUp)
	{
		return true;
	}
	// Skip if the target file was already copied to the installation
	const bool bAlreadyInstalled = FilesInstalled.Contains(Filename);
	if (bAlreadyInstalled)
	{
		return true;
	}
	// If discovered by verification, but the patching system did not touch the file, we know it must be backed up.
	// If patching system touched the file it would already have been backed up
	if (bDiscoveredByVerification && CurrentBuildManifest.IsValid() && !FBuildPatchAppManifest::IsFileOutdated(CurrentBuildManifest.ToSharedRef(), NewBuildManifest, Filename))
	{
		return IFileManager::Get().Move(*BackupFilename, *InstalledFilename, true, true, true);
	}
	bool bUserEditedFile = bDiscoveredByVerification;
	const bool bCheckFileChanges = !bDiscoveredByVerification;
	if (bCheckFileChanges)
	{
		const FFileManifestData* OldFileManifest = CurrentBuildManifest.IsValid() ? CurrentBuildManifest->GetFileManifest(Filename) : nullptr;
		const FFileManifestData* NewFileManifest = NewBuildManifest->GetFileManifest(Filename);
		const int64 InstalledFilesize = IFileManager::Get().FileSize(*InstalledFilename);
		const int64 OriginalFileSize = OldFileManifest ? OldFileManifest->GetFileSize() : INDEX_NONE;
		const int64 NewFileSize = NewFileManifest ? NewFileManifest->GetFileSize() : INDEX_NONE;
		const FSHAHashData HashZero;
		const FSHAHashData& HashOld = OldFileManifest ? OldFileManifest->FileHash : HashZero;
		const FSHAHashData& HashNew = NewFileManifest ? NewFileManifest->FileHash : HashZero;
		const bool bFileSizeDiffers = OriginalFileSize != InstalledFilesize && NewFileSize != InstalledFilesize;
		bUserEditedFile = bFileSizeDiffers || FBuildPatchUtils::VerifyFile(InstalledFilename, HashOld, HashNew) == 0;
	}
	// Finally, use the above logic to determine if we must do the backup
	const bool bNeedBackup = bUserEditedFile;
	bool bBackupSuccess = true;
	if (bNeedBackup)
	{
		GLog->Logf(TEXT("BuildPatchServices: Backing up %s"), *Filename);
		bBackupSuccess = IFileManager::Get().Move(*BackupFilename, *InstalledFilename, true, true, true);
	}
	return bBackupSuccess;
}

bool FBuildPatchInstaller::RunPrereqInstaller()
{
	FString PrereqPath = InstallDirectory / NewBuildManifest->GetPrereqPath();
	PrereqPath = FPaths::ConvertRelativePathToFull(PrereqPath);
	FString PrereqCommandline = NewBuildManifest->GetPrereqArgs();

	GLog->Logf(TEXT("BuildPatchServices: Running prerequisites installer %s %s"), *PrereqPath, *PrereqCommandline);

	BuildProgress.SetStateProgress(EBuildPatchProgress::PrerequisitesInstall, 0.0f);

	// @TODO: Tell our installer to run with no UI since we will have BuildPatchProgress
	// @TODO: Pass in params to the installer that tell it to fire up the portal/launcher after install if it has to perform a restart.
	FProcHandle ProcessHandle = FPlatformProcess::CreateProc(*PrereqPath, *PrereqCommandline, true, false, false, NULL, 0, *FPaths::GetPath(PrereqPath), NULL);
	bool bPrereqInstallSuccessful = true;

	if (!ProcessHandle.IsValid())
	{
		GLog->Logf(TEXT("BuildPatchServices: ERROR: Failed to start the prerequisites install process."));
		FBuildPatchAnalytics::RecordPrereqInstallnError(PrereqPath, PrereqCommandline, -1, TEXT("Failed to start installer"));
		// @TODO: Do we need to do anything special to fail?
		bPrereqInstallSuccessful = false;
	}

	int32 ReturnCode;
	if (bPrereqInstallSuccessful)
	{
		FPlatformProcess::WaitForProc(ProcessHandle);
		FPlatformProcess::GetProcReturnCode(ProcessHandle, &ReturnCode);

		ProcessHandle.Close();

		if (ReturnCode != 0)
		{
			if (ReturnCode == 3010)
			{
				GLog->Logf(TEXT("BuildPatchServices: Prerequisites executable returned restart required code %d"), ReturnCode);
				// @TODO: Inform app that system restart is required
			}
			else
			{
				GLog->Logf(TEXT("BuildPatchServices: ERROR: Prerequisites executable failed with code %d"), ReturnCode);
				FBuildPatchAnalytics::RecordPrereqInstallnError(PrereqPath, PrereqCommandline, ReturnCode, TEXT("Failed to install"));
				bPrereqInstallSuccessful = false;
			}
		}
	}

	if (bPrereqInstallSuccessful)
	{
		BuildProgress.SetStateProgress(EBuildPatchProgress::PrerequisitesInstall, 1.0f);
	}
	else
	{
		FBuildPatchInstallError::SetFatalError(EBuildPatchInstallError::PrerequisiteError);
	}

	return bPrereqInstallSuccessful;
}

void FBuildPatchInstaller::SetRunning( bool bRunning )
{
	FScopeLock Lock( &ThreadLock );
	bIsRunning = bRunning;
}

void FBuildPatchInstaller::SetInited( bool bInited )
{
	FScopeLock Lock( &ThreadLock );
	bIsInited = bInited;
}

void FBuildPatchInstaller::SetDownloadSpeed( double ByteSpeed )
{
	FScopeLock Lock( &ThreadLock );
	DownloadSpeedValue = ByteSpeed;
}

void FBuildPatchInstaller::SetDownloadBytesLeft( const int64& BytesLeft )
{
	FScopeLock Lock( &ThreadLock );
	DownloadBytesLeft = BytesLeft;
}

void FBuildPatchInstaller::UpdateDownloadProgressInfo( bool bReset )
{
	// Static variables for persistent values
	static double LastTime = FPlatformTime::Seconds();
	static double NowTime = 0;
	static double DeltaTime = 0;
	static double LastReadingTime = 0;
	static double LastDataReadings[NUM_DOWNLOAD_READINGS] = { 0 };
	static double LastTimeReadings[NUM_DOWNLOAD_READINGS] = { 0 };
	static uint32 ReadingIdx = 0;
	static bool bProgressIsDownload = true;
	static double AverageDownloadSpeed = 0;

	// Reset internals?
	if( bReset )
	{
		LastTime = FPlatformTime::Seconds();
		LastReadingTime = LastTime;
		NowTime = 0;
		DeltaTime = 0;
		for (int32 i = 0; i < NUM_DOWNLOAD_READINGS; ++i)
		{
			LastDataReadings[i] = 0;
			LastTimeReadings[i] = 0;
		}
		ReadingIdx = 0;
		bProgressIsDownload = true;
		AverageDownloadSpeed = 0;
		return;
	}

	// Return if not downloading yet
	if( !NewBuildManifest->IsFileDataManifest() && !FBuildPatchChunkCache::Get().HaveDownloadsStarted() )
	{
		return;
	}

	// Calculate percentage complete based on number of chunks
	const int64 DownloadNumBytesLeft = FBuildPatchDownloader::Get().GetNumBytesLeft();
	const float DownloadSizeFloat = TotalInitialDownloadSize;
	const float DownloadBytesLeftFloat = DownloadNumBytesLeft;
	const float DownloadProgress = 1.0f - (TotalInitialDownloadSize > 0 ? DownloadBytesLeftFloat / DownloadSizeFloat : 0.0f);
	BuildProgress.SetStateProgress( EBuildPatchProgress::Downloading, DownloadProgress );

	// Calculate the average download speed
	NowTime = FPlatformTime::Seconds();
	DeltaTime += NowTime - LastTime;
	if( DeltaTime > TIME_PER_READING )
	{
		const double BytesDownloaded = FBuildPatchDownloader::Get().GetByteDownloadCountReset();
		const double TimeSinceLastReading = NowTime - LastReadingTime;
		LastReadingTime = NowTime;
		LastDataReadings[ReadingIdx] = BytesDownloaded;
		LastTimeReadings[ReadingIdx] = TimeSinceLastReading;
		ReadingIdx = (ReadingIdx + 1) % NUM_DOWNLOAD_READINGS;
		DeltaTime = 0;
		double TotalData = 0;
		double TotalTime = 0;
		for (uint32 i = 0; i < NUM_DOWNLOAD_READINGS; ++i)
		{
			TotalData += LastDataReadings[i];
			TotalTime += LastTimeReadings[i];
		}
		AverageDownloadSpeed = TotalData / TotalTime;
	}

	// Set download values
	SetDownloadSpeed( DownloadProgress < 1.0f ? AverageDownloadSpeed : -1.0f );
	SetDownloadBytesLeft( DownloadNumBytesLeft );

	// Set last time
	LastTime = NowTime;
}

//@todo this is deprecated and shouldn't be used anymore [6/4/2014 justin.sargent]
FText FBuildPatchInstaller::GetDownloadSpeedText()
{
	static const FText DownloadSpeedFormat = LOCTEXT("BuildPatchInstaller_DownloadSpeedFormat", "{Current} / {Total} ({Speed}/sec)");

	FScopeLock Lock(&ThreadLock);
	FText SpeedDisplayedText;
	if (DownloadSpeedValue >= 0)
	{
		FNumberFormattingOptions SpeedFormattingOptions;
		SpeedFormattingOptions.MaximumFractionalDigits = 1;
		SpeedFormattingOptions.MinimumFractionalDigits = 0;

		FNumberFormattingOptions SizeFormattingOptions;
		SizeFormattingOptions.MaximumFractionalDigits = 1;
		SizeFormattingOptions.MinimumFractionalDigits = 1;

		FFormatNamedArguments Args;
		Args.Add(TEXT("Speed"), FText::AsMemory(DownloadSpeedValue, &SpeedFormattingOptions));
		Args.Add(TEXT("Total"), FText::AsMemory(TotalInitialDownloadSize, &SizeFormattingOptions));
		Args.Add(TEXT("Current"), FText::AsMemory(TotalInitialDownloadSize - DownloadBytesLeft, &SizeFormattingOptions));
		return FText::Format(DownloadSpeedFormat, Args);
	}

	return FText();
}

double FBuildPatchInstaller::GetDownloadSpeed() const
{
	FScopeLock Lock(&ThreadLock);
	return DownloadSpeedValue;
}

int64 FBuildPatchInstaller::GetInitialDownloadSize() const
{
	FScopeLock Lock(&ThreadLock);
	return TotalInitialDownloadSize;
}

int64 FBuildPatchInstaller::GetTotalDownloaded() const
{
	FScopeLock Lock(&ThreadLock);
	return TotalInitialDownloadSize - DownloadBytesLeft;
}

bool FBuildPatchInstaller::IsComplete()
{
	FScopeLock Lock( &ThreadLock );
	return !bIsRunning && bIsInited;
}

bool FBuildPatchInstaller::IsCanceled()
{
	FScopeLock Lock( &ThreadLock );
	return FBuildPatchInstallError::GetErrorState() == EBuildPatchInstallError::UserCanceled;
}

bool FBuildPatchInstaller::IsPaused()
{
	return BuildProgress.GetPauseState();
}

bool FBuildPatchInstaller::HasError()
{
	FScopeLock Lock( &ThreadLock );
	if( FBuildPatchInstallError::GetErrorState() == EBuildPatchInstallError::UserCanceled )
	{
		return false;
	}
	return !BuildStats.ProcessSuccess;
}

//@todo this is deprecated and shouldn't be used anymore [6/4/2014 justin.sargent]
FText FBuildPatchInstaller::GetPercentageText()
{
	static const FText PleaseWait = LOCTEXT( "BuildPatchInstaller_GenericProgress", "Please Wait" );

	FScopeLock Lock( &ThreadLock );

	float Progress = GetUpdateProgress() * 100.0f;
	if( Progress <= 0.0f )
	{
		return PleaseWait;
	}

	FNumberFormattingOptions PercentFormattingOptions;
	PercentFormattingOptions.MaximumFractionalDigits = 0;
	PercentFormattingOptions.MinimumFractionalDigits = 0;

	return FText::AsPercent(GetUpdateProgress(), &PercentFormattingOptions);
}

FText FBuildPatchInstaller::GetStatusText()
{
	return BuildProgress.GetStateText();
}

float FBuildPatchInstaller::GetUpdateProgress()
{
	return BuildProgress.GetProgress();
}

FBuildInstallStats FBuildPatchInstaller::GetBuildStatistics()
{
	FScopeLock Lock( &ThreadLock );
	return BuildStats;
}

FText FBuildPatchInstaller::GetErrorText()
{
	FScopeLock Lock( &ThreadLock );
	return BuildStats.FailureReasonText;
}

void FBuildPatchInstaller::CancelInstall()
{
	FBuildPatchInstallError::SetFatalError( EBuildPatchInstallError::UserCanceled );
	FBuildPatchHTTP::CancelAllHttpRequests();
	// Make sure we are not paused
	if( IsPaused() )
	{
		TogglePauseInstall();
	}
}

bool FBuildPatchInstaller::TogglePauseInstall()
{
	if( IsPaused() )
	{
		// We are now resuming, so record time paused for
		FScopeLock Lock( &ThreadLock );
		const double PausedForSec = FPlatformTime::Seconds() - TimePausedAt;
		BuildStats.ProcessPausedTime += PausedForSec;
	}
	else
	{
		// If there is an error, we don't allow the pause
		if( FBuildPatchInstallError::HasFatalError() )
		{
			return false;
		}
		// Set the time we pause at
		TimePausedAt = FPlatformTime::Seconds();
	}
	return BuildProgress.TogglePauseState();
}

void FBuildPatchInstaller::UpdateVerificationProgress( float Percent )
{
	BuildProgress.SetStateProgress( EBuildPatchProgress::BuildVerification, Percent );
}

void FBuildPatchInstaller::SetupFileAttributes( const FString& FilePath, const FFileManifestData& FileManifest )
{
	// File must not be readonly to be able to set attributes
	IPlatformFile::GetPlatformPhysical().SetReadOnly(*FilePath, false);
	// Set correct attributes
	SetFileCompressionFlag(FilePath, FileManifest.bIsCompressed);
	if (!IPlatformFile::GetPlatformPhysical().SetReadOnly(*FilePath, FileManifest.bIsReadOnly))
	{
		GLog->Logf(TEXT("BuildPatchServices: WARNING: Could not set readonly flag %s"), *FilePath);
	}
	if (FileManifest.bIsUnixExecutable && !SetExecutableFlag(FilePath))
	{
		GLog->Logf(TEXT("BuildPatchServices: WARNING: Could not set executable flag %s"), *FilePath);
	}
}

void FBuildPatchInstaller::ExecuteCompleteDelegate()
{
	// Should be executed in main thread, and already be complete
	check( IsInGameThread() );
	check( IsComplete() );
	// Call the complete delegate
	OnCompleteDelegate.Execute( bSuccess, NewBuildManifest );
}

void FBuildPatchInstaller::WaitForThread() const
{
	if (Thread != NULL)
	{
		Thread->WaitForCompletion();
	}
}

#undef LOCTEXT_NAMESPACE
