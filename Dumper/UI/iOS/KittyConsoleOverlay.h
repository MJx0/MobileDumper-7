#pragma once
#import <UIKit/UIKit.h>

typedef NS_ENUM(NSInteger, ConsoleLogLevel) {
	ConsoleLogLevelDebug,
	ConsoleLogLevelInfo,
	ConsoleLogLevelWarning,
	ConsoleLogLevelError
};

@interface KittyConsoleOverlay : UIView

+ (instancetype)sharedConsole;
- (void)showInView:(UIView*)ParentView;
- (void)hide;
- (void)addLog:(NSString*)message level:(ConsoleLogLevel)level;
- (void)clearLogs;

@end
