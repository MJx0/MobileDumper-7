#pragma once

#import <UIKit/UIKit.h>

typedef NS_ENUM(NSInteger, KittyAlertViewStyle) {
	KittyAlertViewStyleSuccess,
	KittyAlertViewStyleError,
	KittyAlertViewStyleWarning,
	KittyAlertViewStyleInfo,
	KittyAlertViewStyleEdit,
	KittyAlertViewStyleWaiting,
	KittyAlertViewStyleCustom
};

typedef NS_ENUM(NSInteger, KittyAlertViewAnimation) {
	KittyAlertViewAnimationFade,
	KittyAlertViewAnimationSlideFromTop,
	KittyAlertViewAnimationSlideFromBottom,
	KittyAlertViewAnimationScale,
	KittyAlertViewAnimationBounce,
	KittyAlertViewAnimationRotate
};

NS_ASSUME_NONNULL_BEGIN

@interface KittyAlertButton : UIButton

@property(nonatomic, copy, nullable) void (^action)(void);

@end

@interface KittyAlertSwitch : UIView

@property(nonatomic, strong) UILabel* switchLabel;
@property(nonatomic, strong) UISwitch* switchControl;
@property(nonatomic, copy, nullable) void (^action)(bool);

@end

/// Collapsible drop-down picker element for KittyAlertView.
///
/// Shows a header row (title + selected value + chevron) that expands inline
/// to reveal option rows when tapped.  After the user picks an option the
/// picker collapses and fires the action block.
@interface KittyAlertPicker : UIView

/// Called when the selection changes.  Receives the display title, the
/// corresponding value string, and the zero-based index.
@property(nonatomic, copy, nullable) void (^action)(NSString* title, NSString* value, NSUInteger index);

/// Display title of the currently selected option.
@property(nonatomic, readonly) NSString* selectedTitle;

/// Value string of the currently selected option.
@property(nonatomic, readonly) NSString* selectedValue;

/// Zero-based index of the currently selected option.
@property(nonatomic, readonly) NSUInteger selectedIndex;

/// YES while the option list is visible.
@property(nonatomic, readonly) BOOL isExpanded;

/// Total height the picker requires in the current state (collapsed or expanded).
/// The parent alert view reads this during layout to size the picker's frame.
@property(nonatomic, readonly) CGFloat intrinsicContentHeight;

@end

@interface KittyAlertView : UIView

@property(nonatomic, assign) KittyAlertViewAnimation showAnimation;
@property(nonatomic, assign) KittyAlertViewAnimation hideAnimation;
@property(nonatomic, assign) BOOL shouldDismissOnTapOutside;
@property(nonatomic, assign) NSTimeInterval duration;
@property(nonatomic, assign) BOOL enablePulseEffect;
@property(nonatomic, copy, nullable) void (^dismissAction)(void);
@property(nonatomic, readonly, assign) BOOL isShowing;
@property(nonatomic, assign) CGFloat cornerRadius;
@property(nonatomic, assign) CGFloat dimmingAlpha;
@property(nonatomic, strong, nullable) UIImage* iconImage;

- (instancetype)init NS_UNAVAILABLE;
- (instancetype)initWithFrame:(CGRect)frame NS_UNAVAILABLE;
- (instancetype)initWithCoder:(NSCoder*)coder NS_UNAVAILABLE;
+ (instancetype)alloc NS_UNAVAILABLE;
+ (instancetype)allocWithZone:(struct _NSZone*)zone NS_UNAVAILABLE;

+ (instancetype)createWithParentView:(UIView*)view forStyle:(KittyAlertViewStyle)style;

- (void)showWithTitle:(nullable NSString*)title
             subtitle:(nullable NSString*)subtitle
     closeButtonTitle:(nullable NSString*)closeButtonTitle
             duration:(NSTimeInterval)duration;

- (void)showWithTitle:(nullable NSString*)title
             subtitle:(nullable NSString*)subtitle
     closeButtonTitle:(nullable NSString*)closeButtonTitle
    closeButtonAction:(void (^_Nullable)(void))closeButtonAction
             duration:(NSTimeInterval)duration;

- (void)showWithTitle:(nullable NSString*)title
               subtitle:(nullable NSString*)subtitle
      cancelButtonTitle:(nullable NSString*)cancelButtonTitle
     cancelButtonAction:(void (^_Nullable)(void))cancelButtonAction
     confirmButtonTitle:(nullable NSString*)confirmButtonTitle
    confirmButtonAction:(void (^_Nullable)(void))confirmButtonAction
               duration:(NSTimeInterval)duration;

- (KittyAlertButton*)addButtonWithTitle:(NSString*)title action:(void (^_Nullable)(void))action;

- (KittyAlertSwitch*)addSwitchWithTitle:(NSString*)title initialState:(BOOL)initialState action:(void (^_Nullable)(BOOL))action;

/// Add a collapsible drop-down picker as the next element in the alert.
///
/// @param title       Label shown on the left of the collapsed header row.
/// @param options     Display strings shown in the expanded option list.
/// @param values      Value strings passed to @p action (parallel to @p options).
/// @param selectedIndex  Zero-based index of the initially selected option.
/// @param action      Block called when the selection changes.
- (KittyAlertPicker*)addPickerWithTitle:(NSString*)title
                                options:(NSArray<NSString*>*)options
                                 values:(NSArray<NSString*>*)values
                          selectedIndex:(NSUInteger)selectedIndex
                                 action:(void (^_Nullable)(NSString* title, NSString* value, NSUInteger index))action;

- (void)setCancelButtonTitle:(nullable NSString*)title action:(void (^_Nullable)(void))action;
- (void)setConfirmButtonTitle:(nullable NSString*)title action:(void (^_Nullable)(void))action;

- (void)dismiss;

- (void)setTitle:(nullable NSString*)title needsLayout:(BOOL)needsLayout;
- (void)setSubtitle:(nullable NSString*)subtitle needsLayout:(BOOL)needsLayout;

- (void)setTintColor:(nullable UIColor*)tintColor;
- (void)setBackgroundColor:(nullable UIColor*)backgroundColor;
- (void)setFontColor:(nullable UIColor*)fontColor;
- (void)setButtonFontColor:(nullable UIColor*)fontColor;

@end

NS_ASSUME_NONNULL_END
