#pragma once

#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

/// Lightweight browser for the dumper's output directory.
///
/// Presented as a centred card over the game window. Navigation is sandboxed to
/// @c GSettings.Generator.SDKGenerationPath — the browser can descend into dump
/// folders but never above the root. Entries may be shared or deleted; there is
/// deliberately no preview, rename or move.
@interface KittyFileBrowser : UIView

+ (instancetype)sharedBrowser;

/// Deflate level used when a shared folder is zipped, 0-9. Values outside that
/// range are clamped. Set by the caller so this view needs no build settings of
/// its own; defaults to 6.
- (void)setZipCompressionLevel:(NSInteger)Level;

- (void)showInView:(UIView*)ParentView forPath:(NSString*)RootPath;
- (void)hide;

@end

NS_ASSUME_NONNULL_END
