#ifdef __APPLE__

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <memory>
#include <optional>
#include <pthread.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#include "DumperMain.h"
#include "Settings.h"

#include "Architecture/IArchDecoder.h"
#include "Memory/IMemory.h"
#include "Memory/MemoryiOS.h"
#include "Profile/IProfile.h"

#include "UI/iOS/DraggableButton.h"
#include "UI/iOS/Icons.h"
#include "UI/iOS/KittyConsoleOverlay.h"
#include "UI/iOS/KittyFileBrowser.h"
#include "UI/iOS/UIUtils.h"

#define kDumperAlertTitle @"MobileDumper-7"

#include "Profile/CustomProfiles/Shared/PUBG.h"
#include "Profile/CustomProfiles/Shared/DeltaForce.h"

inline std::vector<std::shared_ptr<IProfile>> UECustomProfiles;

std::vector<std::shared_ptr<IProfile>>& GetUECustomProfiles()
{
	if (UECustomProfiles.empty())
	{
		UECustomProfiles.push_back(std::make_shared<PUBGProfile>());
		UECustomProfiles.push_back(std::make_shared<DeltaForceProfile>());
	}

	return UECustomProfiles;
}

DraggableButton* CreateMenuButton();
void EnsureSDKGenerationPath();
void InstallLogBridge();
void ShowMainMenu();
void ShowDumpAlert();
void RunDump(KittyAlertView* WaitingAlert, bool bSuspend = false);
void ShowDumpResult(KittyAlertView* WaitingAlert,
                    bool bSuccess,
                    const std::string& ErrorStr,
                    const std::string& ZipPath,
                    const std::string& Elapsed);
void PresentShareZIP(const std::string& ZipPath);

NSArray<NSString*>* GetModuleCandidatePaths();
bool IsUnrealGame();

static thread_t GMainMachThread = MACH_PORT_NULL;
static bool bSuspendThreads     = false;

void DumperInit()
{
	GMainMachThread = mach_thread_self();
	EnsureSDKGenerationPath();
	InstallLogBridge();
	UIWindow* KeyWindow         = UIUtils::GetMainKeyWindow();
	DraggableButton* MenuButton = CreateMenuButton();
	[KeyWindow addSubview:MenuButton];
	MenuButton.enablePulse = YES;
}

__attribute__((constructor)) static void onLoad()
{
	if (!IsUnrealGame())
		return;

	GLogger.FmtWrite(ELogLevel::Info, "======= GENERIC UE GAME DETECTED ========\n");

	__block id Token = [[NSNotificationCenter defaultCenter]
	    addObserverForName:UIApplicationDidFinishLaunchingNotification
	                object:nil
	                 queue:[NSOperationQueue mainQueue]
	            usingBlock:^(NSNotification*) {
		          if (Token)
		          {
			          [[NSNotificationCenter defaultCenter] removeObserver:Token];
			          Token = nil;
		          }

		          dispatch_after(
		              dispatch_time(DISPATCH_TIME_NOW, (int64_t)(5 * NSEC_PER_SEC)),
		              dispatch_get_main_queue(),
		              ^{
			            DumperInit();
		              });
	            }];
}

NSArray<NSString*>* GetModuleCandidatePaths()
{
	NSMutableArray<NSString*>* Paths = [NSMutableArray array];

	NSString* ExePath = [NSBundle mainBundle].executablePath;
	if (ExePath)
		[Paths addObject:ExePath];

	NSString* FwDir   = [[NSBundle mainBundle].bundlePath stringByAppendingPathComponent:@"Frameworks"];
	NSFileManager* Fm = [NSFileManager defaultManager];

	NSMutableArray<NSDictionary*>* Sized = [NSMutableArray array];
	for (NSString* Entry in [Fm contentsOfDirectoryAtPath:FwDir error:nil])
	{
		NSString* Full = [FwDir stringByAppendingPathComponent:Entry];
		NSString* Bin  = Full;
		if ([Entry hasSuffix:@".framework"])
			Bin = [Full stringByAppendingPathComponent:[Entry stringByDeletingPathExtension]];
		NSDictionary* Attrs = [Fm attributesOfItemAtPath:Bin error:nil];
		if (Attrs)
			[Sized addObject:@{@"path" : Bin,
				               @"size" : Attrs[NSFileSize] ?: @0}];
	}

	[Sized sortUsingComparator:^NSComparisonResult(NSDictionary* A, NSDictionary* B) {
	  return [B[@"size"] compare:A[@"size"]];
	}];

	for (NSUInteger I = 0; I < MIN((NSUInteger)2, Sized.count); I++)
		[Paths addObject:Sized[I][@"path"]];

	return [Paths copy];
}

bool IsUnrealGame()
{
	NSBundle* CurrentBundle = [NSBundle mainBundle];
	if (!CurrentBundle || !CurrentBundle.bundleIdentifier.length || !CurrentBundle.bundlePath.length)
		return false;

	NSString* BundlePath = CurrentBundle.bundlePath.stringByResolvingSymlinksInPath;
	if (!BundlePath.length)
		return false;

	// allow user apps only.
	if (![BundlePath containsString:@"/containers/Bundle/Application/"] ||
	    ![BundlePath hasSuffix:@".app"])
		return false;

	NSFileManager* Fm = [NSFileManager defaultManager];
	for (const auto& F : GSettings.General.iOSUnrealMarkFiles)
	{
		NSString* BundleEntryPath = [BundlePath stringByAppendingPathComponent:[NSString stringWithUTF8String:F.c_str()]];
		if ([Fm fileExistsAtPath:BundleEntryPath])
		{
			return true;
		}
	}

	return false;
}

void EnsureSDKGenerationPath()
{
	if (!GSettings.Generator.SDKGenerationPath.empty())
		return;

	NSString* AppDocDir = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES)[0];
	NSString* DumpPath  = [NSString stringWithFormat:@"%@/MobileDumper-7", AppDocDir];

	GSettings.Generator.SDKGenerationPath = DumpPath.UTF8String;
}

void InstallLogBridge()
{
	GLogger.SetOnDidLogMessage([](ELogLevel LogLevel, const std::string& Msg)
	{
		std::string CleanMsg = Utils::String::Trim(Utils::String::Trim(Msg, '\n'), '\t');
		if (CleanMsg.empty())
			return;

		NSString* Line = [NSString stringWithUTF8String:CleanMsg.c_str()];
		if (!Line)
			return;

		ConsoleLogLevel Level = ConsoleLogLevelInfo;
		switch (LogLevel)
		{
		case ELogLevel::Debug:
			Level = ConsoleLogLevelDebug;
			break;
		case ELogLevel::Info:
			Level = ConsoleLogLevelInfo;
			break;
		case ELogLevel::Warning:
			Level = ConsoleLogLevelWarning;
			break;
		case ELogLevel::Error:
			Level = ConsoleLogLevelError;
			break;
		default:
			break;
		}

		UIUtils::DispatchAsyncMain(^(void) {
		  [[KittyConsoleOverlay sharedConsole] addLog:Line level:Level];
		});
	});
}

DraggableButton* CreateMenuButton()
{
	static const CGFloat kButtonSize   = 56.0;
	static const CGFloat kButtonMargin = 20.0;
	CGFloat ScreenW                    = UIScreen.mainScreen.bounds.size.width;
	DraggableButton* Button            = UIUtils::CreateFloatingButton(
        CGRectMake(ScreenW - kButtonSize - kButtonMargin, 120.0, kButtonSize, kButtonSize),
        UIColor.systemBlueColor,
        Icons::Paw());

	Button.onClick = [](DraggableButton*)
	{
		ShowMainMenu();
	};

	return Button;
}

void ShowMainMenu()
{
	UIUtils::DispatchSyncMain(^{
	  KittyAlertView* Menu = [KittyAlertView createWithParentView:UIUtils::GetMainKeyWindow()
		                                                 forStyle:KittyAlertViewStyleInfo];

	  Menu.shouldDismissOnTapOutside = YES;
	  Menu.showAnimation             = KittyAlertViewAnimationBounce;
	  Menu.hideAnimation             = KittyAlertViewAnimationFade;

	  [Menu addButtonWithTitle:@"Dumper" action:^{ ShowDumpAlert(); }];

	  [Menu addButtonWithTitle:@"Logs"
		                action:^{
			              [[KittyConsoleOverlay sharedConsole] showInView:UIUtils::GetMainKeyWindow()];
		                }];

	  [Menu addButtonWithTitle:@"Files"
		                action:^{
			              KittyFileBrowser* Browser = [KittyFileBrowser sharedBrowser];

			              [Browser setZipCompressionLevel:GSettings.Generator.ZipCompressionLevel];

			              [Browser showInView:UIUtils::GetMainKeyWindow()
			                          forPath:[NSString stringWithUTF8String:GSettings.Generator.SDKGenerationPath.c_str()]];
		                }];

	  [Menu showWithTitle:@"MobileDumper-7"
		          subtitle:@""
		  closeButtonTitle:@"Close"
		          duration:0];
	});
}

void ShowDumpAlert()
{
	UIUtils::DispatchSyncMain(^{
	  NSArray<NSString*>* ModPaths        = GetModuleCandidatePaths();
	  NSMutableArray<NSString*>* ModNames = [NSMutableArray array];
	  for (NSUInteger I = 0; I < ModPaths.count; I++)
	  {
		  NSString* Base = [ModPaths[I] lastPathComponent];
		  [ModNames addObject:Base];
	  }

	  KittyAlertView* alertView = [KittyAlertView createWithParentView:UIUtils::GetMainKeyWindow() forStyle:KittyAlertViewStyleEdit];

	  alertView.shouldDismissOnTapOutside = NO;
	  alertView.enablePulseEffect         = YES;
	  alertView.showAnimation             = KittyAlertViewAnimationBounce;
	  alertView.hideAnimation             = KittyAlertViewAnimationFade;

	  [alertView addPickerWithTitle:@"Unreal Module"
		                    options:ModNames
		                     values:ModPaths
		              selectedIndex:0
		                     action:^(NSString*, NSString* Value, NSUInteger Idx) {
			                   GSettings.General.UnrealModuleName = (Idx == 0) ? "" : Value.UTF8String;
		                     }];

	  auto AddToggle = [alertView](NSString* Title, bool* Setting)
	  {
		  [alertView addSwitchWithTitle:Title
			               initialState:*Setting ? YES : NO
			                     action:^(BOOL State) { *Setting = State; }];
	  };

	  AddToggle(@"Suspend Threads", &bSuspendThreads);
	  AddToggle(@"GObjects", &GSettings.Generator.bGenerateGObjects);
	  AddToggle(@"GObjectsWithProps", &GSettings.Generator.bGenerateGObjectsWithProps);
	  AddToggle(@"EditorOnlyMetadata", &GSettings.Generator.bGenerateEditorOnlyMetadata);
	  AddToggle(@"CppSDK", &GSettings.Generator.bGenerateCppSDK);
	  AddToggle(@"Mappings", &GSettings.Generator.bGenerateMapping);
	  AddToggle(@"IDAMappings", &GSettings.Generator.bGenerateIDAMapping);
	  AddToggle(@"Dumpspace", &GSettings.Generator.bGenerateDumpspace);

	  [alertView setCancelButtonTitle:@"Cancel" action:^{}];
	  [alertView setConfirmButtonTitle:@"Confirm"
		                        action:^{
			                      KittyAlertView* WaitingAlert =
			                          Alert::showWaiting(kDumperAlertTitle, @"This may take a few minutes.");

			                      FDumperMain::SetOnProgressCallback(
			                          [WaitingAlert](const std::string& D)
			                      {
				                      NSString* Ds = [NSString stringWithUTF8String:Utils::String::Trim(D, '\n').c_str()];

				                      UIUtils::DispatchAsyncMain(^{
					                    if (Ds.length > 0)
						                    [WaitingAlert setSubtitle:Ds needsLayout:YES];
				                      });
			                      });

			                      std::thread([WaitingAlert]()
			                      { RunDump(WaitingAlert, bSuspendThreads); })
			                          .detach();
		                        }];

	  [alertView showWithTitle:kDumperAlertTitle
		              subtitle:@"Generate GObjects, C++ SDK, Mappings and Dumpspace dumps"
		      closeButtonTitle:nil
		              duration:0];
	});
}

/// Suspends all game threads for the lifetime of this object.
/// Skips the calling thread and the main (UI) thread — suspending either
class FThreadSuspension
{
	std::vector<thread_t> SuspendedThreads;

public:
	FThreadSuspension(const FThreadSuspension&)            = delete;
	FThreadSuspension& operator=(const FThreadSuspension&) = delete;

	FThreadSuspension()
	{
		thread_t SelfThread = mach_thread_self();
		thread_t MainThread = GMainMachThread;

		thread_act_array_t ThreadList      = nullptr;
		mach_msg_type_number_t ThreadCount = 0;

		if (task_threads(mach_task_self(), &ThreadList, &ThreadCount) != KERN_SUCCESS)
		{
			GLogger.FmtWrite(ELogLevel::Warning, "FThreadSuspension: task_threads failed.\n");
			mach_port_deallocate(mach_task_self(), SelfThread);
			return;
		}

		for (mach_msg_type_number_t I = 0; I < ThreadCount; I++)
		{
			thread_t T = ThreadList[I];
			if (T == SelfThread || T == MainThread)
			{
				mach_port_deallocate(mach_task_self(), T);
				continue;
			}
			if (thread_suspend(T) == KERN_SUCCESS)
				SuspendedThreads.push_back(T);
			else
				mach_port_deallocate(mach_task_self(), T);
		}

		vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(ThreadList), ThreadCount * sizeof(thread_act_t));
		mach_port_deallocate(mach_task_self(), SelfThread);

		GLogger.FmtWrite(ELogLevel::Info, "Suspended {} game thread(s).\n", SuspendedThreads.size());
	}

	~FThreadSuspension()
	{
		for (thread_t T : SuspendedThreads)
		{
			thread_resume(T);
			mach_port_deallocate(mach_task_self(), T);
		}
		if (!SuspendedThreads.empty())
			GLogger.FmtWrite(ELogLevel::Info, "Resumed {} game thread(s).\n", SuspendedThreads.size());
	}
};

void RunDump(KittyAlertView* WaitingAlert, bool bSuspend)
{
	auto StartTime = std::chrono::high_resolution_clock::now();

	EnsureSDKGenerationPath();

	{
		std::error_code Ec;
		std::filesystem::create_directories(GSettings.Generator.SDKGenerationPath, Ec);
		if (Ec)
		{
			GLogger.FmtWrite(ELogLevel::Error, "Failed to create output dir at \"{}\"\n", GSettings.Generator.SDKGenerationPath, Ec.message());
			GLogger.FmtWrite(ELogLevel::Error, "Error: \"{}\"\n", Ec.message());
			Alert::dismiss(WaitingAlert);
			Alert::showError(kDumperAlertTitle, @"Failed to create output directory.", nil);
			FDumperMain::SetOnProgressCallback(nullptr);
			GLogger.CloseFileStream();
			return;
		}
	}

	GMemory      = std::make_unique<FMemoryiOS>(mach_task_self());
	GArchDecoder = CreateArchDecoder(EArch::Arm64);

	for (const auto& it : GetUECustomProfiles())
	{
		for (const auto& Identifier : it->GetSupportedGames())
		{
			if (Identifier == std::string(GMemory->GetProcessName()))
			{
				GLogger.FmtWrite(ELogLevel::Info, "Using Custom UE Profile Override ({}).\n", Identifier);
				GProfile = it;
				goto DumpLabel;
			}
		}
	}

	GLogger.FmtWrite(ELogLevel::Info, "Using Generic UE Profile.\n");
	GProfile = std::make_shared<IProfile>();

DumpLabel:
	std::string OutDumpZip, OutErr;
	bool bSuccess = false;
	try
	{
		std::optional<FThreadSuspension> Suspension;
		if (bSuspend)
			Suspension.emplace();

		bSuccess = FDumperMain::Run(OutDumpZip, OutErr);
	}
	catch (const std::exception& E)
	{
		OutErr = E.what();
	}
	catch (...)
	{
		OutErr = "Unknown exception";
	}

	auto Elapsed           = std::chrono::high_resolution_clock::now() - StartTime;
	std::string ElapsedStr = Utils::ChronoDurationToString(Elapsed);

	GLogger.FmtWrite(ELogLevel::Info, "Duration: {}\n", ElapsedStr);

	if (bSuccess)
	{
		GLogger.FmtWrite(ELogLevel::Info, "Dump succeded.\n");
	}
	else
	{
		GLogger.FmtWrite(ELogLevel::Error, "Dump failed!\n");
		GLogger.FmtWrite(ELogLevel::Error, "Failure reason: \"{}\"\n", OutErr);
	}

	// clean up
	{
		FDumperMain::SetOnProgressCallback(nullptr);

		GProfile.reset();
		GetUECustomProfiles().clear();

		GArchDecoder.reset();
		GMemory.reset();

		GLogger.CloseFileStream();
	}

	ShowDumpResult(WaitingAlert, bSuccess, OutDumpZip, OutErr, ElapsedStr);
}

void ShowDumpResult(KittyAlertView* WaitingAlert,
                    bool bSuccess,
                    const std::string& ZipPath,
                    const std::string& ErrorStr,
                    const std::string& Elapsed)
{
	Alert::dismiss(WaitingAlert);

	if (bSuccess)
	{
		NSString* Msg = [NSString stringWithFormat:@"Duration: %s\n\nDump:\n%s\n\nCheck \'Files\' menu to share/transfer dump files.", Elapsed.c_str(), ZipPath.c_str()];
		Alert::showSuccess(kDumperAlertTitle, Msg);
	}
	else
	{
		NSString* ErrMsg = [NSString stringWithFormat:@"Error: \"%s\"\n\nDuration: %s\n\nCheck logs for more details.", ErrorStr.c_str(), Elapsed.c_str()];
		Alert::showError(kDumperAlertTitle, ErrMsg, nil);
	}
}

#endif // __APPLE__
