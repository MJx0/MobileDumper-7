#import "UIUtils.h"
#import "DraggableButton.h"

namespace UIUtils
{
	UIWindow* GetMainKeyWindow()
	{
		UIApplication* sharedApplication = [UIApplication sharedApplication];
		UIWindow* firstWindow            = nil;

		for (UIScene* scene in sharedApplication.connectedScenes)
		{
			if (scene && [scene isKindOfClass:[UIWindowScene class]])
			{
				UIWindowScene* windowScene = (UIWindowScene*)scene;
				for (UIWindow* window in windowScene.windows)
				{
					if (!window)
						continue;

					if (window.isKeyWindow)
						return window;

					if (!firstWindow)
						firstWindow = window;
				}
			}
		}

		return firstWindow;
	}

	UIViewController* GetTopViewController()
	{
		UIWindow* window = GetMainKeyWindow();
		if (!window)
			return nil;

		UIViewController* topVC = window.rootViewController;

		while (topVC)
		{
			if (topVC.presentedViewController)
			{
				topVC = topVC.presentedViewController;
			}
			else if ([topVC isKindOfClass:[UINavigationController class]])
			{
				UINavigationController* nav = (UINavigationController*)topVC;
				topVC                       = nav.visibleViewController;
			}
			else if ([topVC isKindOfClass:[UITabBarController class]])
			{
				UITabBarController* tab = (UITabBarController*)topVC;
				topVC                   = tab.selectedViewController;
			}
			else if ([topVC isKindOfClass:[UISplitViewController class]])
			{
				UISplitViewController* split = (UISplitViewController*)topVC;
				topVC                        = split.viewControllers.lastObject;
			}
			else
			{
				break;
			}
		}

		return topVC;
	}

	void DispatchSyncMain(ui_action_block_t block)
	{
		if (block)
		{
			if ([NSThread isMainThread])
			{
				block();
			}
			else
			{
				dispatch_sync(dispatch_get_main_queue(), ^{
				  block();
				});
			}
		}
	}

	void DispatchAsyncMain(ui_action_block_t block)
	{
		if (block)
		{
			dispatch_async(dispatch_get_main_queue(), ^{
			  block();
			});
		}
	}

	DraggableButton* CreateFloatingButton(CGRect Frame, UIColor* BackgroundColor, UIImage* Icon)
	{
		DraggableButton* Button   = [[DraggableButton alloc] initWithFrame:Frame];
		Button.snapToEdge         = YES;
		Button.layer.cornerRadius = Frame.size.width / 2.0;
		Button.clipsToBounds      = NO;
		Button.backgroundColor    = BackgroundColor;

		Button.layer.shadowColor   = UIColor.blackColor.CGColor;
		Button.layer.shadowOpacity = 0.3f;
		Button.layer.shadowOffset  = CGSizeMake(0, 4);
		Button.layer.shadowRadius  = 10.0;
		Button.layer.shadowPath    = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(0, 0, Frame.size.width, Frame.size.height)].CGPath;

		[Button setImage:Icon forState:UIControlStateNormal];
		Button.tintColor             = UIColor.whiteColor;
		Button.imageView.contentMode = UIViewContentModeScaleAspectFit;
		Button.imageEdgeInsets       = UIEdgeInsetsMake(14, 14, 14, 14);
		return Button;
	}

}


namespace Alert
{
	void dismiss(KittyAlertView* alert)
	{
		UIUtils::DispatchSyncMain(^() {
		  if (alert)
			  [alert dismiss];
		});
	}

	KittyAlertView* showWaiting(NSString* title, NSString* msg)
	{
		__block KittyAlertView* waitingAlert = nil;
		UIUtils::DispatchSyncMain(^{
		  waitingAlert = [KittyAlertView createWithParentView:UIUtils::GetMainKeyWindow() forStyle:KittyAlertViewStyleWaiting];

		  waitingAlert.shouldDismissOnTapOutside = NO;
		  waitingAlert.enablePulseEffect         = YES;
		  waitingAlert.showAnimation             = KittyAlertViewAnimationBounce;
		  waitingAlert.hideAnimation             = KittyAlertViewAnimationFade;

		  [waitingAlert showWithTitle:title
			                 subtitle:msg
			         closeButtonTitle:nil
			                 duration:0];
		});
		return waitingAlert;
	}

	void showSuccess(NSString* title, NSString* msg, ui_action_block_t actionBlock)
	{
		UIUtils::DispatchSyncMain(^{
		  KittyAlertView* alertView = [KittyAlertView createWithParentView:UIUtils::GetMainKeyWindow() forStyle:KittyAlertViewStyleSuccess];

		  alertView.shouldDismissOnTapOutside = NO;
		  alertView.enablePulseEffect         = YES;
		  alertView.showAnimation             = KittyAlertViewAnimationBounce;
		  alertView.hideAnimation             = KittyAlertViewAnimationFade;
		  alertView.dismissAction             = actionBlock;

		  [alertView showWithTitle:title
			              subtitle:msg
			      closeButtonTitle:@"OK"
			              duration:0];
		});
	}

	void showInfo(NSString* title, NSString* msg, float durationSeconds, ui_action_block_t actionBlock)
	{
		UIUtils::DispatchSyncMain(^{
		  KittyAlertView* alertView = [KittyAlertView createWithParentView:UIUtils::GetMainKeyWindow() forStyle:KittyAlertViewStyleInfo];

		  alertView.shouldDismissOnTapOutside = NO;
		  alertView.enablePulseEffect         = YES;
		  alertView.showAnimation             = KittyAlertViewAnimationBounce;
		  alertView.hideAnimation             = KittyAlertViewAnimationFade;
		  alertView.dismissAction             = actionBlock;

		  [alertView showWithTitle:title
			              subtitle:msg
			      closeButtonTitle:@"OK"
			              duration:durationSeconds];
		});
	}

	void showError(NSString* title, NSString* msg, ui_action_block_t actionBlock)
	{
		UIUtils::DispatchSyncMain(^{
		  KittyAlertView* alertView = [KittyAlertView createWithParentView:UIUtils::GetMainKeyWindow() forStyle:KittyAlertViewStyleError];

		  alertView.shouldDismissOnTapOutside = NO;
		  alertView.enablePulseEffect         = YES;
		  alertView.showAnimation             = KittyAlertViewAnimationBounce;
		  alertView.hideAnimation             = KittyAlertViewAnimationFade;
		  alertView.dismissAction             = actionBlock;

		  [alertView showWithTitle:title
			              subtitle:msg
			      closeButtonTitle:@"OK"
			              duration:0];
		});
	}

	void showNoOrYes(NSString* title, NSString* msg, ui_action_block_t noBlock, ui_action_block_t yesBlock)
	{
		UIUtils::DispatchSyncMain(^{
		  KittyAlertView* alertView = [KittyAlertView createWithParentView:UIUtils::GetMainKeyWindow() forStyle:KittyAlertViewStyleEdit];

		  alertView.shouldDismissOnTapOutside = NO;
		  alertView.enablePulseEffect         = YES;
		  alertView.showAnimation             = KittyAlertViewAnimationBounce;
		  alertView.hideAnimation             = KittyAlertViewAnimationFade;

		  [alertView showWithTitle:title
			              subtitle:msg
			     cancelButtonTitle:@"No"
			    cancelButtonAction:noBlock
			    confirmButtonTitle:@"Yes"
			   confirmButtonAction:yesBlock
			              duration:0];
		});
	}
} // namespace Alert
