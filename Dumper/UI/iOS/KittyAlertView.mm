#import "KittyAlertView.h"

#define kColorHex(RGBA) [UIColor colorWithRed:((RGBA >> 24) & 0xFF) / 255.0 green:((RGBA >> 16) & 0xFF) / 255.0 blue:((RGBA >> 8) & 0xFF) / 255.0 alpha:((RGBA) & 0xFF) / 255.0]

#define kPaddingSmallScreen        12.0
#define kPaddingLargeScreen        14.0
#define kElementSpacing            4.0
#define kIconSizeMax               72.0
#define kButtonHeightPhone         32.0
#define kButtonHeightTablet        52.0
#define kAlertMaxAbsoluteWidth     480.0
#define kAlertMinAbsoluteWidth     250.0
#define kAlertHorizontalMargin      20.0
#define kAlertVerticalMargin        20.0
#define kAlertMaxHeightRatio        0.85
#define kMinFontScale               0.8
#define kActionBarInsetPhone        12.0
#define kActionBarInsetTablet       16.0
#define kActionButtonGap            10.0
#define kTitleMaxHeightThreshold   100.0
#define kAlertCornerRadius         20.0
#define kButtonCornerRadiusDivisor 2.0
#define kAlertShadowOffsetX        0.0
#define kAlertShadowOffsetY        12.0
#define kAlertShadowRadius         24.0
#define kAlertShadowOpacity        0.25f

#define kFontSizeTitle              15.0
#define kFontSizeSubtitle           11.0
#define kFontSizeButton             12.0
#define kFontSizeIcon               ([UIFont systemFontSize] + ([UIFont systemFontSize] * 0.25))
#define kIconLineWidthRatio         0.1
#define kIconInsetRatio             0.25
#define kSubtitleTextContainerInset 8.0
#define kSubtitleBorderWidth        1.0
#define kSubtitleBorderAlpha        0.7

#define kAnimationShowHideDuration         0.3
#define kAnimationBounceDuration           0.5
#define kAnimationPulseDuration            0.8
#define kAnimationButtonHoverDuration      0.1
#define kAnimationGradientRotationDuration 1.1
#define kAnimationPulseScaleFactor         1.1
#define kAnimationSpringDamping            0.6
#define kAnimationSpringVelocity           0.5

#define kPulseAnimationKey   @"kitty.alert.pulse"
#define kGradientRotationKey @"kitty.alert.gradientRotation"
#define kShimmerAnimationKey @"kitty.alert.shimmer"

#define kPickerRowH          34.0
#define kPickerOptionH       38.0
#define kPickerHorzPad       12.0
#define kPickerSeparatorH    0.5
#define kPickerChevronFontSz 14.0
#define kPickerCheckFontSz   13.0
#define kPickerCornerRadius  8.0
#define kPickerBottomGap     4.0

#define kPickerExpandDuration  0.22
#define kPickerChevronRotation M_PI // 180° flip on expand

#define kWaitingTintColor     [UIColor colorWithRed:0.27f green:0.51f blue:0.97f alpha:1.0f]
#define kWaitingGradientStop0 [UIColor colorWithRed:0.55f green:0.20f blue:0.95f alpha:1.0f].CGColor
#define kWaitingGradientStop1 [UIColor colorWithRed:0.27f green:0.51f blue:0.97f alpha:1.0f].CGColor
#define kWaitingGradientStop2 [UIColor colorWithRed:0.40f green:0.75f blue:1.00f alpha:0.4f].CGColor

#define kBlurViewAlpha            0.8f
#define kShimmerHighlightAlpha    0.45f
#define kAnimationShimmerDuration 2.0
#define kIconShadowRadius         10.0
#define kIconShadowOpacity        0.45f
#define kActionSeparatorAlpha     0.12f
#define kSubtitleHeightCapRatio   0.40
#define kAccentBarHeight           4.0
#define kSwitchContainerAlpha     0.06f
#define kSwitchCornerRadius       8.0
#define kSwitchBottomGap          2.0

#define kHeaderIconDiameter       56.0
#define kHeaderMinHeight          28.0
#define kHeaderVPad                8.0

#define kAlertBorderWidth          1.0
#define kAlertBorderAlpha          0.35f

#define kFontSizeTitleBump         0.0
#define kTitleGlowRadius           3.0
#define kTitleGlowOpacity          0.35f

#define kSubtitleFontAlpha         1.0f

#define kScrollFadeHeight         52.0

#define kSeparatorH                0.5

#define kInfoTintColor  [UIColor colorWithRed:0.0f green:0.40f blue:1.0f alpha:1.0f]

@implementation KittyAlertButton
@end

@implementation KittyAlertSwitch
@end

@interface KittyAlertPicker ()

@property(nonatomic, strong) NSArray<NSString*>* pickerOptions;
@property(nonatomic, strong) NSArray<NSString*>* pickerValues;
@property(nonatomic, assign) NSUInteger selectedIndex;
@property(nonatomic, assign) BOOL isExpanded;

@property(nonatomic, strong) UIControl* headerControl;
@property(nonatomic, strong) UILabel* titleLabel;
@property(nonatomic, strong) UILabel* selectedValueLabel;
@property(nonatomic, strong) UILabel* chevronLabel;
@property(nonatomic, strong) UIView* separatorView;
@property(nonatomic, strong) NSMutableArray<UIControl*>* optionControls;

@property(nonatomic, weak) KittyAlertView* alertView;
@property(nonatomic, strong) UIColor* pickerTintColor;
@property(nonatomic, strong) UIColor* pickerFontColor;

@end

@implementation KittyAlertPicker

@synthesize selectedIndex = _selectedIndex;
@synthesize isExpanded    = _isExpanded;

- (NSString*)selectedTitle
{
	if (_selectedIndex < self.pickerOptions.count)
		return self.pickerOptions[_selectedIndex];
	return @"";
}

- (NSString*)selectedValue
{
	if (_selectedIndex < self.pickerValues.count)
		return self.pickerValues[_selectedIndex];
	return @"";
}

- (CGFloat)intrinsicContentHeight
{
	CGFloat H = kPickerRowH;
	if (self.isExpanded)
		H += (CGFloat)self.pickerOptions.count * kPickerOptionH;
	return H;
}

- (instancetype)initWithOptions:(NSArray<NSString*>*)options
                         values:(NSArray<NSString*>*)values
                  selectedIndex:(NSUInteger)index
                      tintColor:(UIColor*)tintColor
                      fontColor:(UIColor*)fontColor
{
	self = [super initWithFrame:CGRectZero];
	if (!self)
		return nil;

	self.pickerOptions   = options;
	self.pickerValues    = values;
	_selectedIndex       = MIN(index, options.count > 0 ? options.count - 1 : 0);
	_isExpanded          = NO;
	self.pickerTintColor = tintColor ?: UIColor.systemPurpleColor;
	self.pickerFontColor = fontColor ?: UIColor.labelColor;
	self.optionControls  = [NSMutableArray array];

	self.backgroundColor     = [UIColor colorWithWhite:1.0 alpha:0.06];
	self.layer.cornerRadius  = kPickerCornerRadius;
	self.layer.masksToBounds = YES;
	self.clipsToBounds       = YES;

	[self buildHeader];
	[self buildOptions];
	return self;
}

- (void)buildHeader
{
	self.headerControl                 = [[UIControl alloc] init];
	self.headerControl.backgroundColor = UIColor.clearColor;
	[self.headerControl addTarget:self action:@selector(headerTapped) forControlEvents:UIControlEventTouchUpInside];

	self.titleLabel               = [[UILabel alloc] init];
	self.titleLabel.font          = [UIFont systemFontOfSize:kFontSizeButton];
	self.titleLabel.textColor     = self.pickerFontColor;
	self.titleLabel.numberOfLines = 1;
	self.titleLabel.lineBreakMode = NSLineBreakByTruncatingTail;

	self.selectedValueLabel               = [[UILabel alloc] init];
	self.selectedValueLabel.font          = [UIFont systemFontOfSize:kFontSizeButton];
	self.selectedValueLabel.textColor     = self.pickerTintColor;
	self.selectedValueLabel.numberOfLines = 1;
	self.selectedValueLabel.textAlignment = NSTextAlignmentRight;
	self.selectedValueLabel.lineBreakMode = NSLineBreakByTruncatingMiddle;

	self.chevronLabel               = [[UILabel alloc] init];
	self.chevronLabel.font          = [UIFont systemFontOfSize:kPickerChevronFontSz];
	self.chevronLabel.textColor     = self.pickerTintColor;
	self.chevronLabel.text          = @"▾";
	self.chevronLabel.textAlignment = NSTextAlignmentCenter;
	[self.chevronLabel sizeToFit];

	self.separatorView                 = [[UIView alloc] init];
	self.separatorView.backgroundColor = [UIColor.separatorColor colorWithAlphaComponent:kSubtitleBorderAlpha];
	self.separatorView.hidden          = YES;

	[self.headerControl addSubview:self.titleLabel];
	[self.headerControl addSubview:self.selectedValueLabel];
	[self.headerControl addSubview:self.chevronLabel];
	[self addSubview:self.headerControl];
	[self addSubview:self.separatorView];

	[self refreshHeaderText];
}

- (void)buildOptions
{
	[self.optionControls makeObjectsPerformSelector:@selector(removeFromSuperview)];
	[self.optionControls removeAllObjects];

	for (NSUInteger I = 0; I < self.pickerOptions.count; I++)
	{
		UIControl* Row      = [[UIControl alloc] init];
		Row.backgroundColor = UIColor.clearColor;
		Row.tag             = (NSInteger)I;
		[Row addTarget:self action:@selector(optionTapped:) forControlEvents:UIControlEventTouchUpInside];

		UILabel* Check      = [[UILabel alloc] init];
		Check.tag           = 100;
		Check.font          = [UIFont systemFontOfSize:kPickerCheckFontSz];
		Check.textColor     = self.pickerTintColor;
		Check.text          = (I == _selectedIndex) ? @"✓" : @"";
		Check.textAlignment = NSTextAlignmentCenter;

		UILabel* OptionLbl      = [[UILabel alloc] init];
		OptionLbl.tag           = 101;
		OptionLbl.font          = [UIFont systemFontOfSize:kFontSizeButton];
		OptionLbl.textColor     = self.pickerFontColor;
		OptionLbl.text          = self.pickerOptions[I];
		OptionLbl.numberOfLines = 1;
		OptionLbl.lineBreakMode = NSLineBreakByTruncatingMiddle;

		[Row addSubview:Check];
		[Row addSubview:OptionLbl];
		[self addSubview:Row];
		[self.optionControls addObject:Row];

		UIView* Sep         = [[UIView alloc] init];
		Sep.backgroundColor = [UIColor.separatorColor colorWithAlphaComponent:kSubtitleBorderAlpha];
		Sep.tag             = (NSInteger)(200 + I);
		[self addSubview:Sep];
	}
}

- (void)refreshHeaderText
{
	if (self.pickerOptions.count > 0 && _selectedIndex < self.pickerOptions.count)
		self.selectedValueLabel.text = self.pickerOptions[_selectedIndex];
}

- (void)layoutSubviews
{
	[super layoutSubviews];

	CGFloat W = self.bounds.size.width;

	self.headerControl.frame = CGRectMake(0, 0, W, kPickerRowH);
	CGFloat ChevW            = MAX(self.chevronLabel.bounds.size.width, kPickerChevronFontSz) + kPickerHorzPad;
	CGFloat TitleNatW        = self.titleLabel.text.length
	    ? ceil([self.titleLabel.text sizeWithAttributes:@{NSFontAttributeName : self.titleLabel.font}].width) + 4.0
	    : W * 0.30;
	CGFloat TitleMaxW             = MIN(TitleNatW, W * 0.55);
	CGFloat ValMaxW               = MAX(W - ChevW - TitleMaxW - kPickerHorzPad * 2, 0.0);
	self.titleLabel.frame         = CGRectMake(kPickerHorzPad, 0, TitleMaxW, kPickerRowH);
	self.selectedValueLabel.frame = CGRectMake(kPickerHorzPad + TitleMaxW, 0, ValMaxW, kPickerRowH);
	self.chevronLabel.frame       = CGRectMake(W - ChevW, (kPickerRowH - self.chevronLabel.bounds.size.height) / 2.0,
	                                           self.chevronLabel.bounds.size.width, self.chevronLabel.bounds.size.height);

	self.separatorView.frame  = CGRectMake(0, kPickerRowH, W, kPickerSeparatorH);
	self.separatorView.hidden = !self.isExpanded;

	CGFloat OptionsY = kPickerRowH + kPickerSeparatorH;
	for (NSUInteger I = 0; I < self.optionControls.count; I++)
	{
		UIControl* Row = self.optionControls[I];
		Row.frame      = CGRectMake(0, OptionsY, W, kPickerOptionH);
		Row.hidden     = !self.isExpanded;
		Row.alpha      = self.isExpanded ? 1.0 : 0.0;

		UILabel* Check = (UILabel*)[Row viewWithTag:100];
		CGFloat CheckW = kPickerChevronFontSz + kPickerHorzPad;
		Check.frame    = CGRectMake(kPickerHorzPad, 0, CheckW, kPickerOptionH);

		UILabel* Opt = (UILabel*)[Row viewWithTag:101];
		Opt.frame    = CGRectMake(kPickerHorzPad + CheckW, 0, W - CheckW - kPickerHorzPad * 2, kPickerOptionH);

		UIView* Sep = (UIView*)[self viewWithTag:(NSInteger)(200 + I)];
		if (Sep)
		{
			Sep.frame  = CGRectMake(0, OptionsY, W, kPickerSeparatorH);
			Sep.hidden = !self.isExpanded || I == 0;
		}

		OptionsY += kPickerOptionH;
	}
}

- (void)headerTapped
{
	self.isExpanded = !self.isExpanded;

	__weak KittyAlertPicker* weakSelf = self;
	CGFloat Angle = self.isExpanded ? (CGFloat)kPickerChevronRotation : 0.0f;
	[UIView animateWithDuration:kPickerExpandDuration
	                      delay:0
	     usingSpringWithDamping:kAnimationSpringDamping
	      initialSpringVelocity:kAnimationSpringVelocity
	                    options:UIViewAnimationOptionCurveEaseInOut
	                 animations:^{
		               weakSelf.chevronLabel.transform = CGAffineTransformMakeRotation(Angle);
		               [weakSelf setNeedsLayout];
		               [weakSelf layoutIfNeeded];
	                 }
	                 completion:nil];

	[self.alertView setNeedsLayout];
	[UIView animateWithDuration:kPickerExpandDuration
	                      delay:0
	     usingSpringWithDamping:kAnimationSpringDamping
	      initialSpringVelocity:kAnimationSpringVelocity
	                    options:UIViewAnimationOptionCurveEaseInOut
	                 animations:^{ [weakSelf.alertView layoutIfNeeded]; }
	                 completion:nil];
}

- (void)optionTapped:(UIControl*)Sender
{
	NSUInteger NewIndex = (NSUInteger)Sender.tag;
	if (NewIndex >= self.pickerOptions.count)
		return;

	for (NSUInteger I = 0; I < self.optionControls.count; I++)
	{
		UILabel* Check = (UILabel*)[self.optionControls[I] viewWithTag:100];
		Check.text     = (I == NewIndex) ? @"✓" : @"";
	}

	_selectedIndex = NewIndex;
	[self refreshHeaderText];

	self.isExpanded = NO;
	__weak KittyAlertPicker* weakSelf = self;
	[UIView animateWithDuration:kPickerExpandDuration
	                      delay:0
	     usingSpringWithDamping:kAnimationSpringDamping
	      initialSpringVelocity:kAnimationSpringVelocity
	                    options:UIViewAnimationOptionCurveEaseInOut
	                 animations:^{
		               weakSelf.chevronLabel.transform = CGAffineTransformIdentity;
		               [weakSelf setNeedsLayout];
		               [weakSelf layoutIfNeeded];
		               [weakSelf.alertView setNeedsLayout];
		               [weakSelf.alertView layoutIfNeeded];
	                 }
	                 completion:nil];

	if (self.action)
		self.action(self.selectedTitle, self.selectedValue, _selectedIndex);
}

@end

@interface KittyAlertView () <UIScrollViewDelegate>

@property(nonatomic, weak) UIView* alertParentView;
@property(nonatomic, strong) UIView* alertDimmingView;
@property(nonatomic, strong) UIVisualEffectView* alertBlurView;
@property(nonatomic, strong) UIView* alertShadowView;
@property(nonatomic, strong) UIView* alertAccentBar;
@property(nonatomic, strong) CAGradientLayer* alertAccentShimmerLayer;
@property(nonatomic, strong) UIView* alertContentView;
@property(nonatomic, strong) UIImageView* alertIconView;
@property(nonatomic, strong) UIActivityIndicatorView* waitingActivityIndicator;
@property(nonatomic, strong) UILabel* alertTitleLabel;
@property(nonatomic, strong) UITextView* alertSubtitleTextView;
@property(nonatomic, strong) UIScrollView* elementsScrollView;
@property(nonatomic, strong) CAGradientLayer* scrollFadeLayer;
@property(nonatomic, strong) UIView* alertActionBar;
@property(nonatomic, strong) UIView* alertHeaderSeparator;
@property(nonatomic, strong) UIView* alertSubtitleSeparator;
@property(nonatomic, strong) UIView* alertActionSeparator;
@property(nonatomic, strong) KittyAlertButton* alertCancelButton;
@property(nonatomic, strong) KittyAlertButton* alertConfirmButton;
@property(nonatomic, strong) NSMutableArray<UITextField*>* alertTextFields;
@property(nonatomic, strong) NSMutableArray<KittyAlertButton*>* alertButtons;
@property(nonatomic, strong) NSMutableArray<KittyAlertSwitch*>* alertSwitches;
@property(nonatomic, strong) NSMutableArray<UIView*>* alertElements;
@property(nonatomic, strong, nullable) NSTimer* dismissTimer;
@property(nonatomic, assign) CGFloat waitingMaskSize;

@property(nonatomic, copy)   NSString* cachedTitleText;
@property(nonatomic, assign) CGFloat   cachedTitleWidth;
@property(nonatomic, assign) CGFloat   cachedTitleHeight;

@property(nonatomic, copy)   NSString* cachedSubtitleText;
@property(nonatomic, assign) CGFloat   cachedSubtitleWidth;
@property(nonatomic, assign) CGFloat   cachedSubtitleH;

@property(nonatomic, strong) NSArray*  cachedScrollColors;
@property(nonatomic, strong) UIColor*  cachedScrollBgColor;
@property(nonatomic, strong) NSLayoutConstraint* contentHeightConstraint;
@property(nonatomic, strong) UIView*              alertCardWrapper;
@property(nonatomic, strong) UITapGestureRecognizer* dismissTapGesture;

@property(nonatomic, assign) KittyAlertViewStyle alertStyle;

@property(nonatomic, strong, nullable) UIColor* tintColor;
@property(nonatomic, strong, nullable) UIColor* backgroundColor;
@property(nonatomic, strong, nullable) UIColor* fontColor;
@property(nonatomic, strong, nullable) UIColor* buttonFontColor;

@property(nonatomic, assign) BOOL isShowing;

@end

@implementation KittyAlertView

+ (instancetype)createWithParentView:(UIView*)view forStyle:(KittyAlertViewStyle)style
{
	if (!view)
	{
		NSLog(@"KittyAlertView: parentView cannot be nil");
		return nil;
	}

	KittyAlertView* av = [[KittyAlertView alloc] initWithFrame:view.bounds];
	if (av)
	{
		av.alertParentView           = view;
		av.alertStyle                = style;
		av.alertTextFields           = [NSMutableArray array];
		av.alertButtons              = [NSMutableArray array];
		av.alertSwitches             = [NSMutableArray array];
		av.alertElements             = [NSMutableArray array];
		av.shouldDismissOnTapOutside = YES;
		av.showAnimation             = KittyAlertViewAnimationFade;
		av.hideAnimation             = KittyAlertViewAnimationFade;
		av.duration                  = 0;
		av.isShowing                 = NO;
		av.enablePulseEffect         = YES;
		[av setupUI];
		av.cornerRadius = kAlertCornerRadius;
		av.dimmingAlpha = 0.5;
		[av setupRotationHandling];
	}
	return av;
}

- (void)setupUI
{
	self.tintColor       = [self tintColorForStyle:self.alertStyle];
	self.backgroundColor = [self backgroundColorForStyle:self.alertStyle];
	self.fontColor       = [self fontColorForStyle:self.alertStyle];
	self.buttonFontColor = [self buttonFontColorForStyle:self.alertStyle];

	self.alertDimmingView                  = [[UIView alloc] initWithFrame:self.bounds];
	self.alertDimmingView.backgroundColor  = [UIColor.blackColor colorWithAlphaComponent:0.5];
	self.alertDimmingView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
	[self addSubview:self.alertDimmingView];

	self.alertCardWrapper               = [[UIView alloc] init];
	self.alertCardWrapper.clipsToBounds = NO;
	self.alertCardWrapper.backgroundColor = UIColor.clearColor;
	[self addSubview:self.alertCardWrapper];

	UIBlurEffect* blurEffect              = [UIBlurEffect effectWithStyle:UIBlurEffectStyleSystemMaterialDark];
	self.alertBlurView                    = [[UIVisualEffectView alloc] initWithEffect:blurEffect];
	self.alertBlurView.alpha              = kBlurViewAlpha;
	self.alertBlurView.layer.cornerRadius = kAlertCornerRadius;
	self.alertBlurView.clipsToBounds      = YES;
	[self.alertCardWrapper addSubview:self.alertBlurView];

	self.alertShadowView                     = [[UIView alloc] init];
	self.alertShadowView.backgroundColor     = UIColor.clearColor;
	self.alertShadowView.layer.shadowColor   = UIColor.blackColor.CGColor;
	self.alertShadowView.layer.shadowOpacity = kAlertShadowOpacity;
	self.alertShadowView.layer.shadowOffset  = CGSizeMake(kAlertShadowOffsetX, kAlertShadowOffsetY);
	self.alertShadowView.layer.shadowRadius  = kAlertShadowRadius;
	[self.alertCardWrapper addSubview:self.alertShadowView];

	self.alertContentView                     = [[UIView alloc] init];
	self.alertContentView.backgroundColor     = [self backgroundColorForStyle:self.alertStyle];
	self.alertContentView.layer.cornerRadius  = kAlertCornerRadius;
	self.alertContentView.layer.masksToBounds = YES;
	self.alertContentView.layer.borderWidth   = kAlertBorderWidth;
	self.alertContentView.layer.borderColor   = [self.tintColor colorWithAlphaComponent:kAlertBorderAlpha].CGColor;
	[self.alertCardWrapper addSubview:self.alertContentView];

	self.alertAccentBar                 = [[UIView alloc] init];
	self.alertAccentBar.backgroundColor = self.tintColor;
	[self.alertContentView insertSubview:self.alertAccentBar atIndex:0];

	CAGradientLayer* shimmer = [CAGradientLayer layer];
	shimmer.colors           = @[ (id)[UIColor clearColor].CGColor,
	                              (id)[[UIColor whiteColor] colorWithAlphaComponent:kShimmerHighlightAlpha].CGColor,
	                              (id)[UIColor clearColor].CGColor ];
	shimmer.startPoint       = CGPointMake(0.0, 0.5);
	shimmer.endPoint         = CGPointMake(1.0, 0.5);
	shimmer.locations        = @[ @-0.5, @-0.25, @0.0 ];
	[self.alertAccentBar.layer addSublayer:shimmer];
	self.alertAccentShimmerLayer = shimmer;

	self.alertIconView                 = [[UIImageView alloc] init];
	self.alertIconView.contentMode     = UIViewContentModeCenter;
	self.alertIconView.image           = [self iconForStyle:self.alertStyle];
	self.alertIconView.tintColor       = UIColor.whiteColor;
	self.alertIconView.backgroundColor = self.tintColor ?: [self tintColorForStyle:self.alertStyle];
	[self.alertCardWrapper addSubview:self.alertIconView];

	// CAShapeLayer mask gives pixel-perfect D-shape clipping regardless of render pass order.
	// Path is constant (always 72×36, top corners radius 36) so we create it once here.
	{
		CAShapeLayer* BadgeMask = [CAShapeLayer layer];
		CGFloat IconR           = kHeaderIconDiameter / 2.0;
		BadgeMask.path          = [UIBezierPath bezierPathWithRoundedRect:CGRectMake(0, 0, kHeaderIconDiameter, IconR)
		                                                byRoundingCorners:UIRectCornerTopLeft | UIRectCornerTopRight
		                                                      cornerRadii:CGSizeMake(IconR, IconR)].CGPath;
		self.alertIconView.layer.mask = BadgeMask;
	}

	self.waitingActivityIndicator = [[UIActivityIndicatorView alloc] initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleMedium];
	self.waitingActivityIndicator.color            = UIColor.whiteColor;
	self.waitingActivityIndicator.hidesWhenStopped = YES;
	[self.alertIconView addSubview:self.waitingActivityIndicator];

	self.alertTitleLabel                 = [[UILabel alloc] init];
	self.alertTitleLabel.font            = [UIFont systemFontOfSize:kFontSizeTitle + kFontSizeTitleBump weight:UIFontWeightBold];
	self.alertTitleLabel.textAlignment   = NSTextAlignmentCenter;
	self.alertTitleLabel.numberOfLines   = 0;
	self.alertTitleLabel.textColor       = self.tintColor;
	self.alertTitleLabel.backgroundColor = [UIColor clearColor];
	self.alertTitleLabel.layer.shadowColor   = self.tintColor.CGColor;
	self.alertTitleLabel.layer.shadowOpacity = kTitleGlowOpacity;
	self.alertTitleLabel.layer.shadowRadius  = kTitleGlowRadius;
	self.alertTitleLabel.layer.shadowOffset  = CGSizeMake(0, 0);
	[self.alertContentView addSubview:self.alertTitleLabel];

	self.alertSubtitleTextView                                   = [[UITextView alloc] init];
	self.alertSubtitleTextView.font                              = [UIFont systemFontOfSize:kFontSizeSubtitle + kFontSizeTitleBump weight:UIFontWeightMedium];
	self.alertSubtitleTextView.textAlignment                     = NSTextAlignmentCenter;
	self.alertSubtitleTextView.textColor                         = [_fontColor colorWithAlphaComponent:kSubtitleFontAlpha];
	self.alertSubtitleTextView.backgroundColor                   = [UIColor clearColor];
	self.alertSubtitleTextView.editable                          = NO;
	self.alertSubtitleTextView.selectable                        = YES;
	self.alertSubtitleTextView.scrollEnabled                     = NO;
	self.alertSubtitleTextView.showsVerticalScrollIndicator      = YES;
	self.alertSubtitleTextView.showsHorizontalScrollIndicator    = NO;
	self.alertSubtitleTextView.textContainerInset                = UIEdgeInsetsMake(kSubtitleTextContainerInset, kSubtitleTextContainerInset, kSubtitleTextContainerInset, kSubtitleTextContainerInset);
	self.alertSubtitleTextView.textContainer.lineFragmentPadding = 0.0;
	self.alertSubtitleTextView.textContainer.lineBreakMode       = NSLineBreakByWordWrapping;
	self.alertSubtitleTextView.layer.borderWidth                 = 0.0;
	[self.alertContentView addSubview:self.alertSubtitleTextView];

	self.elementsScrollView                              = [[UIScrollView alloc] init];
	self.elementsScrollView.showsVerticalScrollIndicator = YES;
	self.elementsScrollView.delegate                     = self;
	[self.alertContentView addSubview:self.elementsScrollView];

	self.scrollFadeLayer            = [CAGradientLayer layer];
	self.scrollFadeLayer.colors     = @[
	    (id)[UIColor clearColor].CGColor,
	    (id)[UIColor clearColor].CGColor,
	    (id)[UIColor clearColor].CGColor,
	];
	self.scrollFadeLayer.locations  = @[@0.0, @0.55, @1.0];
	self.scrollFadeLayer.opacity    = 0.0;
	[self.alertContentView.layer addSublayer:self.scrollFadeLayer];

	self.alertHeaderSeparator                 = [[UIView alloc] init];
	self.alertHeaderSeparator.backgroundColor = [UIColor separatorColor];
	self.alertHeaderSeparator.hidden          = YES;
	[self.alertContentView addSubview:self.alertHeaderSeparator];

	self.alertSubtitleSeparator                 = [[UIView alloc] init];
	self.alertSubtitleSeparator.backgroundColor = [UIColor separatorColor];
	self.alertSubtitleSeparator.hidden          = YES;
	[self.alertContentView addSubview:self.alertSubtitleSeparator];

	self.alertActionBar        = [[UIView alloc] init];
	self.alertActionBar.hidden = YES;
	[self.alertContentView addSubview:self.alertActionBar];

	self.alertActionSeparator                 = [[UIView alloc] init];
	self.alertActionSeparator.backgroundColor = [UIColor separatorColor];
	[self.alertActionBar addSubview:self.alertActionSeparator];

	self.dismissTapGesture = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(handleTap:)];
	[self.alertDimmingView addGestureRecognizer:self.dismissTapGesture];

	[self setupCardConstraints];
}

- (void)setupCardConstraints
{
	self.alertCardWrapper.translatesAutoresizingMaskIntoConstraints = NO;
	for (UIView* V in @[ self.alertContentView, self.alertShadowView, self.alertBlurView ])
		V.translatesAutoresizingMaskIntoConstraints = NO;

	self.contentHeightConstraint          = [self.alertCardWrapper.heightAnchor constraintEqualToConstant:0];
	self.contentHeightConstraint.priority = UILayoutPriorityDefaultHigh;

	UILayoutGuide* Safe = self.safeAreaLayoutGuide;

	NSLayoutConstraint* PreferredWidth = [self.alertCardWrapper.widthAnchor
	    constraintEqualToAnchor:Safe.widthAnchor
	                multiplier:0.78];
	PreferredWidth.priority = 900;

	// CenterY is best-effort: the required top/bottom and top-badge inequality constraints
	// take priority in tight spaces (landscape, tall content) so the action bar is never clipped.
	NSLayoutConstraint* CenterY = [self.alertCardWrapper.centerYAnchor constraintEqualToAnchor:Safe.centerYAnchor];
	CenterY.priority = UILayoutPriorityDefaultHigh;

	[NSLayoutConstraint activateConstraints:@[
		[self.alertCardWrapper.centerXAnchor constraintEqualToAnchor:Safe.centerXAnchor],
		[self.alertCardWrapper.leadingAnchor constraintGreaterThanOrEqualToAnchor:Safe.leadingAnchor
		                                                                   constant:kAlertHorizontalMargin],
		[self.alertCardWrapper.trailingAnchor constraintLessThanOrEqualToAnchor:Safe.trailingAnchor
		                                                                  constant:-kAlertHorizontalMargin],
		[self.alertCardWrapper.widthAnchor constraintLessThanOrEqualToConstant:kAlertMaxAbsoluteWidth],
		[self.alertCardWrapper.widthAnchor constraintGreaterThanOrEqualToConstant:kAlertMinAbsoluteWidth],
		PreferredWidth,
		CenterY,
		// Extra kHeaderIconDiameter/2 ensures the badge (which protrudes above the wrapper) stays on-screen.
		[self.alertCardWrapper.topAnchor constraintGreaterThanOrEqualToAnchor:Safe.topAnchor
		                                                               constant:kAlertVerticalMargin + kHeaderIconDiameter / 2.0],
		[self.alertCardWrapper.bottomAnchor constraintLessThanOrEqualToAnchor:Safe.bottomAnchor
		                                                               constant:-kAlertVerticalMargin],
		[self.alertCardWrapper.heightAnchor constraintLessThanOrEqualToAnchor:Safe.heightAnchor
		                                                            multiplier:kAlertMaxHeightRatio],
		self.contentHeightConstraint,

		[self.alertContentView.leadingAnchor constraintEqualToAnchor:self.alertCardWrapper.leadingAnchor],
		[self.alertContentView.trailingAnchor constraintEqualToAnchor:self.alertCardWrapper.trailingAnchor],
		[self.alertContentView.topAnchor constraintEqualToAnchor:self.alertCardWrapper.topAnchor],
		[self.alertContentView.bottomAnchor constraintEqualToAnchor:self.alertCardWrapper.bottomAnchor],

		[self.alertShadowView.leadingAnchor constraintEqualToAnchor:self.alertCardWrapper.leadingAnchor],
		[self.alertShadowView.trailingAnchor constraintEqualToAnchor:self.alertCardWrapper.trailingAnchor],
		[self.alertShadowView.topAnchor constraintEqualToAnchor:self.alertCardWrapper.topAnchor],
		[self.alertShadowView.bottomAnchor constraintEqualToAnchor:self.alertCardWrapper.bottomAnchor],

		[self.alertBlurView.leadingAnchor constraintEqualToAnchor:self.alertCardWrapper.leadingAnchor],
		[self.alertBlurView.trailingAnchor constraintEqualToAnchor:self.alertCardWrapper.trailingAnchor],
		[self.alertBlurView.topAnchor constraintEqualToAnchor:self.alertCardWrapper.topAnchor],
		[self.alertBlurView.bottomAnchor constraintEqualToAnchor:self.alertCardWrapper.bottomAnchor],
	]];
}

- (void)setupRotationHandling
{
	// UIDevice only posts orientation notifications while generation is on.
	[[UIDevice currentDevice] beginGeneratingDeviceOrientationNotifications];
	[[NSNotificationCenter defaultCenter] addObserver:self
	                                         selector:@selector(deviceOrientationDidChange:)
	                                             name:UIDeviceOrientationDidChangeNotification
	                                           object:nil];
}

- (void)deviceOrientationDidChange:(NSNotification*)notification
{
	if (!self.alertParentView)
		return;
	self.frame = self.alertParentView.bounds;
}

- (void)layoutSubviews
{
	[super layoutSubviews];

	if (!self.superview)
		return;

	self.alertDimmingView.frame = self.bounds;

	CGFloat cardWidth = self.alertContentView.bounds.size.width;
	if (cardWidth <= 0)
		return;

	BOOL bCompactV = (self.traitCollection.verticalSizeClass == UIUserInterfaceSizeClassCompact);

	static const CGFloat kReferenceCardW = 480.0;
	CGFloat scaleF       = MIN(cardWidth / kReferenceCardW, 1.0);
	CGFloat padding      = MAX(round(kPaddingLargeScreen      * scaleF), 8.0);

	CGFloat phoneW       = self.bounds.size.width;
	CGFloat phoneScaleF  = phoneW <= 375.0 ? 0.82 : (phoneW <= 430.0 ? 0.92 : 1.0);

	CGFloat buttonHeight = bCompactV
	    ? 36.0
	    : MAX(round(kButtonHeightTablet * scaleF * phoneScaleF), kButtonHeightPhone);
	CGFloat actionInset  = MAX(round(kActionBarInsetTablet     * scaleF), 8.0);

	UIEdgeInsets SafeInsets = UIEdgeInsetsZero;
	if (@available(iOS 11.0, *))
		SafeInsets = self.safeAreaInsets;
	CGFloat safeH          = MAX(self.bounds.size.height - SafeInsets.top - SafeInsets.bottom, 1.0);
	CGFloat alertMaxHeight = safeH * kAlertMaxHeightRatio;

	CGFloat currentY = padding;

	BOOL bShowIcon  = (self.alertIconView.image != nil);
	BOOL bShowTitle = (self.alertTitleLabel.text.length > 0);
	self.alertIconView.hidden = !bShowIcon;

	if (bShowIcon)
		currentY = kAccentBarHeight + kElementSpacing;

	if (bShowIcon || bShowTitle)
	{
		UIFont*  TitleFont = [UIFont systemFontOfSize:kFontSizeTitle + kFontSizeTitleBump weight:UIFontWeightBold];
		CGFloat  TitleW    = cardWidth - 2.0 * padding;
		CGFloat  HeaderH   = kHeaderMinHeight;

		if (bShowTitle && TitleW > 0)
		{
			if (![self.cachedTitleText isEqualToString:self.alertTitleLabel.text] || self.cachedTitleWidth != TitleW)
			{
				CGRect Bounds = [self.alertTitleLabel.text
				                 boundingRectWithSize:CGSizeMake(TitleW, CGFLOAT_MAX)
				                             options:NSStringDrawingUsesLineFragmentOrigin | NSStringDrawingUsesFontLeading
				                          attributes:@{NSFontAttributeName: TitleFont}
				                             context:nil];
				self.cachedTitleText   = self.alertTitleLabel.text;
				self.cachedTitleWidth  = TitleW;
				self.cachedTitleHeight = ceil(Bounds.size.height);
			}
			HeaderH = MAX(kHeaderMinHeight, self.cachedTitleHeight + kHeaderVPad);
		}

		if (bShowTitle)
		{
			self.alertTitleLabel.font  = TitleFont;
			self.alertTitleLabel.frame = CGRectMake(padding, currentY, TitleW, HeaderH);
		}
		else
		{
			self.alertTitleLabel.frame = CGRectZero;
		}

		self.alertHeaderSeparator.hidden = NO;
		self.alertHeaderSeparator.frame  = CGRectMake(0, currentY + HeaderH, cardWidth, kSeparatorH);

		currentY += HeaderH + kSeparatorH + kElementSpacing;
	}
	else
	{
		self.alertTitleLabel.frame       = CGRectZero;
		self.alertHeaderSeparator.hidden = YES;
	}

	if (self.alertSubtitleTextView.text.length > 0)
	{
		CGFloat textViewW = cardWidth - 2.0 * padding;
		CGFloat textW     = textViewW - 2.0 * kSubtitleTextContainerInset;
		CGFloat subtitleTextH;
		if (![self.cachedSubtitleText isEqualToString:self.alertSubtitleTextView.text] || self.cachedSubtitleWidth != textW)
		{
			CGRect boundingRect      = [self.alertSubtitleTextView.text
			    boundingRectWithSize:CGSizeMake(textW, CGFLOAT_MAX)
			                 options:NSStringDrawingUsesLineFragmentOrigin | NSStringDrawingUsesFontLeading
			              attributes:@{NSFontAttributeName : self.alertSubtitleTextView.font}
			                 context:nil];
			self.cachedSubtitleText  = self.alertSubtitleTextView.text;
			self.cachedSubtitleWidth = textW;
			self.cachedSubtitleH     = ceil(CGRectGetHeight(boundingRect));
		}
		subtitleTextH = self.cachedSubtitleH;

		CGFloat subtitleCap   = alertMaxHeight * kSubtitleHeightCapRatio;
		BOOL isScrollable     = subtitleTextH > subtitleCap;
		CGFloat subtitleH     = isScrollable ? subtitleCap : subtitleTextH;

		self.alertSubtitleTextView.scrollEnabled     = isScrollable;
		self.alertSubtitleTextView.frame = CGRectMake(padding, currentY, textViewW,
		                                              subtitleH + 2.0 * kSubtitleTextContainerInset);
		currentY                        += self.alertSubtitleTextView.frame.size.height + kElementSpacing;

		self.alertSubtitleSeparator.hidden = NO;
		self.alertSubtitleSeparator.frame  = CGRectMake(0, currentY, cardWidth, kSeparatorH);
		currentY                          += kSeparatorH + kElementSpacing;
	}
	else
	{
		self.alertSubtitleTextView.frame             = CGRectZero;
		self.alertSubtitleTextView.scrollEnabled     = NO;
		self.alertSubtitleTextView.layer.borderWidth = 0.0;
		self.alertSubtitleSeparator.hidden            = YES;
	}

	BOOL hasCancelBtn  = (self.alertCancelButton  != nil);
	BOOL hasConfirmBtn = (self.alertConfirmButton != nil);
	BOOL hasActionBar  = hasCancelBtn || hasConfirmBtn;
	CGFloat actionBarH = hasActionBar ? (actionInset + buttonHeight + actionInset) : 0.0;

	CGFloat scrollW = cardWidth - 2.0 * padding;
	self.elementsScrollView.frame = CGRectMake(padding, currentY, scrollW, alertMaxHeight);
	CGFloat scrollContentY        = 0.0;

	static const CGFloat kSwitchScale      = 0.75;
	static const CGFloat kSwitchIntrinsicW = 51.0 * kSwitchScale;
	static const CGFloat kSwitchIntrinsicH = 31.0 * kSwitchScale;
	static const CGFloat kSwitchRowH       = 32.0;
	static const CGFloat kSwitchHorzPad    = 10.0;

	for (UIView* element in self.alertElements)
	{
		if ([element isKindOfClass:[KittyAlertButton class]])
		{
			KittyAlertButton* button   = (KittyAlertButton*)element;
			button.frame               = CGRectMake(0, scrollContentY, scrollW, buttonHeight);
			button.layer.cornerRadius  = buttonHeight / kButtonCornerRadiusDivisor;
			button.layer.masksToBounds = YES;
			scrollContentY            += buttonHeight + kElementSpacing;
		}
		else if ([element isKindOfClass:[KittyAlertSwitch class]])
		{
			KittyAlertSwitch* switchCon   = (KittyAlertSwitch*)element;
			switchCon.frame               = CGRectMake(0, scrollContentY, scrollW, kSwitchRowH);
			switchCon.switchControl.frame = CGRectMake(scrollW - kSwitchIntrinsicW - kSwitchHorzPad,
			                                           (kSwitchRowH - kSwitchIntrinsicH) / 2.0,
			                                           kSwitchIntrinsicW, kSwitchIntrinsicH);
			switchCon.switchLabel.frame   = CGRectMake(kSwitchHorzPad, 0,
			                                           scrollW - kSwitchIntrinsicW - kSwitchHorzPad * 3,
			                                           kSwitchRowH);
			scrollContentY               += kSwitchRowH + kSwitchBottomGap;
		}
		else if ([element isKindOfClass:[KittyAlertPicker class]])
		{
			KittyAlertPicker* picker = (KittyAlertPicker*)element;
			CGFloat pickerH          = picker.intrinsicContentHeight;
			picker.frame             = CGRectMake(0, scrollContentY, scrollW, pickerH);
			scrollContentY          += pickerH + kPickerBottomGap;
		}
	}

	self.elementsScrollView.contentSize = CGSizeMake(scrollW, scrollContentY);

	CGFloat alertHeight = MIN(currentY + scrollContentY + actionBarH + (hasActionBar ? 0.0 : padding),
	                          alertMaxHeight);

	if (fabs(self.contentHeightConstraint.constant - alertHeight) > 0.5)
		self.contentHeightConstraint.constant = alertHeight;

	CGFloat scrollH               = alertHeight - currentY - actionBarH - (hasActionBar ? 0.0 : padding);
	CGRect scrollFrame            = self.elementsScrollView.frame;
	scrollFrame.size.height       = MAX(scrollH, 0.0);
	self.elementsScrollView.frame = scrollFrame;

	static const CGFloat kFadeH = kScrollFadeHeight;
	CGRect sv                   = self.elementsScrollView.frame;
	CGFloat fadeY               = sv.origin.y + sv.size.height - kFadeH;
	self.scrollFadeLayer.frame  = CGRectMake(sv.origin.x, fadeY, sv.size.width, kFadeH);
	[self updateScrollFadeForOffset:self.elementsScrollView.contentOffset];

	self.alertAccentBar.frame          = CGRectMake(0, 0, cardWidth, kAccentBarHeight);
	self.alertAccentShimmerLayer.frame = self.alertAccentBar.bounds;

	self.alertActionBar.hidden      = !hasActionBar;
	self.alertActionBar.frame       = CGRectMake(0, alertHeight - actionBarH, cardWidth, actionBarH);
	self.alertActionSeparator.frame = CGRectMake(0, 0, cardWidth, kSeparatorH);

	if (hasActionBar)
	{
		CGFloat btnAreaW  = cardWidth - 2.0 * actionInset;
		CGFloat btnRadius = buttonHeight / kButtonCornerRadiusDivisor;

		if (hasCancelBtn && hasConfirmBtn)
		{
			CGFloat half                               = (btnAreaW - kActionButtonGap) / 2.0;
			self.alertCancelButton.frame               = CGRectMake(actionInset, actionInset, half, buttonHeight);
			self.alertConfirmButton.frame              = CGRectMake(actionInset + half + kActionButtonGap, actionInset, half, buttonHeight);
			self.alertCancelButton.layer.cornerRadius  = btnRadius;
			self.alertConfirmButton.layer.cornerRadius = btnRadius;
		}
		else
		{
			KittyAlertButton* soloBtn  = hasCancelBtn ? self.alertCancelButton : self.alertConfirmButton;
			soloBtn.frame              = CGRectMake(actionInset, actionInset, btnAreaW, buttonHeight);
			soloBtn.layer.cornerRadius = btnRadius;
		}
	}

	self.alertShadowView.layer.shadowPath =
	    [UIBezierPath bezierPathWithRoundedRect:CGRectMake(0, 0, cardWidth, alertHeight)
	                               cornerRadius:kAlertCornerRadius]
	        .CGPath;

	if (bShowIcon)
	{
		CGFloat IconR                        = kHeaderIconDiameter / 2.0;
		self.alertIconView.frame             = CGRectMake(cardWidth / 2.0 - IconR, -IconR, kHeaderIconDiameter, IconR);
		self.waitingActivityIndicator.center = CGPointMake(IconR, IconR / 2.0);
	}
}

- (void)updateScrollFadeForOffset:(CGPoint)Offset
{
	UIScrollView* Sv      = self.elementsScrollView;
	CGFloat ContentBottom = Sv.contentSize.height - Sv.bounds.size.height;
	BOOL bMoreBelow = (ContentBottom > 12.0) && (Offset.y < ContentBottom - 2.0);

	UIColor* BgColor = self.alertContentView.backgroundColor ?: UIColor.systemBackgroundColor;
	if (![BgColor isEqual:self.cachedScrollBgColor])
	{
		self.cachedScrollBgColor = BgColor;
		self.cachedScrollColors  = @[
		    (id)[BgColor colorWithAlphaComponent:0.0].CGColor,
		    (id)[BgColor colorWithAlphaComponent:0.0].CGColor,
		    (id)[BgColor colorWithAlphaComponent:1.0].CGColor,
		];
	}
	self.scrollFadeLayer.colors = self.cachedScrollColors;

	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	self.scrollFadeLayer.opacity = bMoreBelow ? 1.0f : 0.0f;
	[CATransaction commit];
}

- (void)scrollViewDidScroll:(UIScrollView*)ScrollView
{
	[self updateScrollFadeForOffset:ScrollView.contentOffset];
}

- (void)startShimmerAnimation
{
	if (!self.alertAccentShimmerLayer)
		return;

	[self stopShimmerAnimation];
	CABasicAnimation* shimmerAnim = [CABasicAnimation animationWithKeyPath:@"locations"];
	shimmerAnim.fromValue         = @[ @-0.5, @-0.25, @0.0 ];
	shimmerAnim.toValue           = @[ @1.0, @1.25, @1.5 ];
	shimmerAnim.duration          = kAnimationShimmerDuration;
	shimmerAnim.repeatCount       = HUGE_VALF;
	shimmerAnim.timingFunction    = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut];
	[self.alertAccentShimmerLayer addAnimation:shimmerAnim forKey:kShimmerAnimationKey];
}

- (void)stopShimmerAnimation
{
	if ([self.alertAccentShimmerLayer animationForKey:kShimmerAnimationKey])
		[self.alertAccentShimmerLayer removeAnimationForKey:kShimmerAnimationKey];
}

- (void)startPulseAnimation {}

- (void)stopPulseAnimation {}

- (void)startWaitingGradientAnimation
{
	[self.waitingActivityIndicator startAnimating];
}

- (void)stopWaitingGradientAnimation
{
	[self.waitingActivityIndicator stopAnimating];
}

- (void)showWithTitle:(nullable NSString*)title
             subtitle:(nullable NSString*)subtitle
     closeButtonTitle:(nullable NSString*)closeButtonTitle
             duration:(NSTimeInterval)duration
{
	if (self.isShowing)
		[self dismiss];

	self.alertIconView.hidden = self.alertIconView.image == nil;

	if (self.alertStyle == KittyAlertViewStyleWaiting)
		[self startWaitingGradientAnimation];
	else
		[self stopWaitingGradientAnimation];

	self.alertTitleLabel.text       = title;
	self.alertSubtitleTextView.text = subtitle;

	if (closeButtonTitle.length > 0)
		[self setConfirmButtonTitle:closeButtonTitle action:nil];

	self.duration = duration;
	[self show];
}

- (void)showWithTitle:(nullable NSString*)title
             subtitle:(nullable NSString*)subtitle
     closeButtonTitle:(nullable NSString*)closeButtonTitle
    closeButtonAction:(void (^_Nullable)(void))closeButtonAction
             duration:(NSTimeInterval)duration
{
	if (closeButtonTitle.length > 0)
		[self setConfirmButtonTitle:closeButtonTitle action:closeButtonAction];
	[self showWithTitle:title subtitle:subtitle closeButtonTitle:nil duration:duration];
}

- (void)showWithTitle:(nullable NSString*)title
               subtitle:(nullable NSString*)subtitle
      cancelButtonTitle:(nullable NSString*)cancelButtonTitle
     cancelButtonAction:(void (^_Nullable)(void))cancelButtonAction
     confirmButtonTitle:(nullable NSString*)confirmButtonTitle
    confirmButtonAction:(void (^_Nullable)(void))confirmButtonAction
               duration:(NSTimeInterval)duration
{
	if (cancelButtonTitle.length > 0)
		[self setCancelButtonTitle:cancelButtonTitle action:cancelButtonAction];
	if (confirmButtonTitle.length > 0)
		[self setConfirmButtonTitle:confirmButtonTitle action:confirmButtonAction];
	[self showWithTitle:title subtitle:subtitle closeButtonTitle:nil duration:duration];
}

- (KittyAlertButton*)makeActionButton
{
	KittyAlertButton* button   = [KittyAlertButton buttonWithType:UIButtonTypeCustom];
	button.titleLabel.font     = [UIFont systemFontOfSize:kFontSizeButton weight:UIFontWeightSemibold];
	button.layer.masksToBounds = YES;
	[button addTarget:self action:@selector(touchDown:) forControlEvents:UIControlEventTouchDown];
	[button addTarget:self action:@selector(touchUpInside:) forControlEvents:UIControlEventTouchUpInside];
	[button addTarget:self action:@selector(touchUpOutside:) forControlEvents:UIControlEventTouchUpOutside];
	[button addTarget:self action:@selector(buttonTapped:) forControlEvents:UIControlEventTouchUpInside];
	return button;
}

- (void)setCancelButtonTitle:(nullable NSString*)title action:(void (^_Nullable)(void))action
{
	if (!title.length)
	{
		[self.alertCancelButton removeFromSuperview];
		self.alertCancelButton = nil;
	}
	else
	{
		if (!self.alertCancelButton)
		{
			self.alertCancelButton = [self makeActionButton];
			[self.alertActionBar addSubview:self.alertCancelButton];
		}
		[self.alertCancelButton setTitle:title forState:UIControlStateNormal];
		self.alertCancelButton.action          = action ?: ^{};
		UIColor* cancelTint                    = self.tintColor ?: [self tintColorForStyle:self.alertStyle];
		self.alertCancelButton.backgroundColor = cancelTint;
		[self.alertCancelButton setTitleColor:self.buttonFontColor ?: UIColor.whiteColor
		                             forState:UIControlStateNormal];
	}
	self.alertActionBar.hidden = (self.alertCancelButton == nil && self.alertConfirmButton == nil);
	[self setNeedsLayout];
}

- (void)setConfirmButtonTitle:(nullable NSString*)title action:(void (^_Nullable)(void))action
{
	if (!title.length)
	{
		[self.alertConfirmButton removeFromSuperview];
		self.alertConfirmButton = nil;
	}
	else
	{
		if (!self.alertConfirmButton)
		{
			self.alertConfirmButton = [self makeActionButton];
			[self.alertActionBar addSubview:self.alertConfirmButton];
		}
		[self.alertConfirmButton setTitle:title forState:UIControlStateNormal];
		self.alertConfirmButton.action          = action ?: ^{};
		UIColor* tint                           = self.tintColor ?: [self tintColorForStyle:self.alertStyle];
		self.alertConfirmButton.backgroundColor = tint;
		[self.alertConfirmButton setTitleColor:self.buttonFontColor ?: UIColor.whiteColor
		                              forState:UIControlStateNormal];
	}
	self.alertActionBar.hidden = (self.alertCancelButton == nil && self.alertConfirmButton == nil);
	[self setNeedsLayout];
}

- (KittyAlertButton*)addButtonWithTitle:(NSString*)title action:(void (^_Nullable)(void))action
{
	if (!title.length)
	{
		NSLog(@"KittyAlertView: Button title cannot be empty");
		return nil;
	}
	KittyAlertButton* button = [KittyAlertButton buttonWithType:UIButtonTypeCustom];
	[button setTitle:title forState:UIControlStateNormal];
	button.titleLabel.font = [UIFont systemFontOfSize:kFontSizeButton weight:UIFontWeightMedium];
	[button setTitleColor:self.buttonFontColor ?: UIColor.whiteColor forState:UIControlStateNormal];
	button.backgroundColor = self.tintColor ?: [self tintColorForStyle:self.alertStyle];
	[button addTarget:self action:@selector(touchDown:) forControlEvents:UIControlEventTouchDown];
	[button addTarget:self action:@selector(touchUpInside:) forControlEvents:UIControlEventTouchUpInside];
	[button addTarget:self action:@selector(touchUpOutside:) forControlEvents:UIControlEventTouchUpOutside];
	[button addTarget:self action:@selector(buttonTapped:) forControlEvents:UIControlEventTouchUpInside];

	[self.elementsScrollView addSubview:button];
	[self.alertButtons addObject:button];
	[self.alertElements addObject:button];
	if (action)
	{
		button.action = [action copy];
	}
	else
	{
		button.action = (^(){});
	}
	[self setNeedsLayout];
	return button;
}

- (KittyAlertSwitch*)addSwitchWithTitle:(NSString*)title initialState:(BOOL)initialState action:(void (^_Nullable)(BOOL))action
{
	if (!title.length)
	{
		NSLog(@"KittyAlertView: Switch title cannot be empty");
		return nil;
	}

	KittyAlertSwitch* switchContainer   = [[KittyAlertSwitch alloc] init];
	switchContainer.backgroundColor     = [UIColor colorWithWhite:1.0 alpha:kSwitchContainerAlpha];
	switchContainer.layer.cornerRadius  = kSwitchCornerRadius;
	switchContainer.layer.masksToBounds = YES;

	switchContainer.switchLabel               = [[UILabel alloc] init];
	switchContainer.switchLabel.text          = title;
	switchContainer.switchLabel.font          = [UIFont systemFontOfSize:kFontSizeButton];
	switchContainer.switchLabel.textColor     = self.fontColor ?: [self fontColorForStyle:self.alertStyle];
	switchContainer.switchLabel.numberOfLines = 1;
	switchContainer.switchLabel.textAlignment = NSTextAlignmentLeft;
	switchContainer.switchLabel.lineBreakMode = NSLineBreakByTruncatingTail;

	[switchContainer addSubview:switchContainer.switchLabel];

	switchContainer.switchControl             = [[UISwitch alloc] init];
	switchContainer.switchControl.on          = initialState;
	switchContainer.switchControl.tintColor   = self.tintColor ?: [self tintColorForStyle:self.alertStyle];
	switchContainer.switchControl.onTintColor = self.tintColor ?: [self tintColorForStyle:self.alertStyle];
	switchContainer.switchControl.transform   = CGAffineTransformMakeScale(0.75, 0.75);
	[switchContainer.switchControl addTarget:self action:@selector(switchChanged:) forControlEvents:UIControlEventValueChanged];

	[switchContainer addSubview:switchContainer.switchControl];

	[self.elementsScrollView addSubview:switchContainer];
	[self.alertSwitches addObject:switchContainer];
	[self.alertElements addObject:switchContainer];
	if (action)
	{
		switchContainer.action = [action copy];
	}
	else
	{
		switchContainer.action = (^(BOOL){});
	}
	[self setNeedsLayout];
	return switchContainer;
}

- (KittyAlertPicker*)addPickerWithTitle:(NSString*)title
                                options:(NSArray<NSString*>*)options
                                 values:(NSArray<NSString*>*)values
                          selectedIndex:(NSUInteger)selectedIndex
                                 action:(void (^_Nullable)(NSString*, NSString*, NSUInteger))action
{
	if (!title.length || !options.count)
	{
		NSLog(@"KittyAlertView: Picker title and options cannot be empty");
		return nil;
	}

	UIColor* TintColor = self.tintColor ?: [self tintColorForStyle:self.alertStyle];
	UIColor* FontColor = self.fontColor ?: [self fontColorForStyle:self.alertStyle];

	KittyAlertPicker* picker = [[KittyAlertPicker alloc]
	    initWithOptions:options
	             values:values.count == options.count ? values : options
	      selectedIndex:selectedIndex
	          tintColor:TintColor
	          fontColor:FontColor];

	picker.titleLabel.text = title;
	picker.alertView       = self;
	picker.action          = action ? [action copy] : nil;

	[self.elementsScrollView addSubview:picker];
	[self.alertElements addObject:picker];
	[self setNeedsLayout];
	return picker;
}

- (void)show
{
	if (self.isShowing || !self.alertParentView)
		return;

	[self.alertParentView addSubview:self];
	self.isShowing = YES;

	self.dismissTapGesture.enabled = self.shouldDismissOnTapOutside;

	self.alertCardWrapper.alpha     = 1.0;
	self.alertCardWrapper.transform = CGAffineTransformIdentity;

	__weak KittyAlertView* weakSelf = self;

	self.alertDimmingView.alpha = 0;
	self.alertBlurView.alpha    = 0;
	[UIView animateWithDuration:kAnimationShowHideDuration
	                 animations:^{
		               weakSelf.alertDimmingView.alpha = 1;
		               weakSelf.alertBlurView.alpha    = kBlurViewAlpha;
	                 }];

	void (^completion)(BOOL) = ^(BOOL) {
	  __strong KittyAlertView* strongSelf = weakSelf;
	  if (strongSelf)
	  {
		  [strongSelf startPulseAnimation];
		  [strongSelf startShimmerAnimation];
	  }
	};

	if (self.showAnimation == KittyAlertViewAnimationFade)
	{
		self.alertCardWrapper.alpha = 0;
		[UIView animateWithDuration:kAnimationShowHideDuration
		                 animations:^{
			               weakSelf.alertCardWrapper.alpha = 1;
		                 }
		                 completion:completion];
	}
	else if (self.showAnimation == KittyAlertViewAnimationSlideFromTop)
	{
		self.alertCardWrapper.transform = CGAffineTransformMakeTranslation(0, -self.bounds.size.height);
		[UIView animateWithDuration:kAnimationShowHideDuration
		                 animations:^{
			               weakSelf.alertCardWrapper.transform = CGAffineTransformIdentity;
		                 }
		                 completion:completion];
	}
	else if (self.showAnimation == KittyAlertViewAnimationSlideFromBottom)
	{
		self.alertCardWrapper.transform = CGAffineTransformMakeTranslation(0, self.bounds.size.height);
		[UIView animateWithDuration:kAnimationShowHideDuration
		                 animations:^{
			               weakSelf.alertCardWrapper.transform = CGAffineTransformIdentity;
		                 }
		                 completion:completion];
	}
	else if (self.showAnimation == KittyAlertViewAnimationScale)
	{
		self.alertCardWrapper.transform = CGAffineTransformMakeScale(0.1, 0.1);
		[UIView animateWithDuration:kAnimationShowHideDuration
		                 animations:^{
			               weakSelf.alertCardWrapper.transform = CGAffineTransformIdentity;
		                 }
		                 completion:completion];
	}
	else if (self.showAnimation == KittyAlertViewAnimationBounce)
	{
		self.alertCardWrapper.transform = CGAffineTransformMakeScale(0.1, 0.1);
		[UIView animateWithDuration:kAnimationBounceDuration
		                      delay:0
		     usingSpringWithDamping:kAnimationSpringDamping
		      initialSpringVelocity:kAnimationSpringVelocity
		                    options:UIViewAnimationOptionCurveEaseInOut
		                 animations:^{
			               weakSelf.alertCardWrapper.transform = CGAffineTransformIdentity;
		                 }
		                 completion:completion];
	}
	else if (self.showAnimation == KittyAlertViewAnimationRotate)
	{
		self.alertCardWrapper.transform = CGAffineTransformMakeRotation(M_PI);
		self.alertCardWrapper.alpha     = 0;
		[UIView animateWithDuration:kAnimationShowHideDuration
		                 animations:^{
			               weakSelf.alertCardWrapper.transform = CGAffineTransformIdentity;
			               weakSelf.alertCardWrapper.alpha     = 1;
		                 }
		                 completion:completion];
	}
	else
	{
		completion(YES);
	}

	if (self.duration > 0)
	{
		[self.dismissTimer invalidate];
		__weak KittyAlertView* weakTimer = self;
		self.dismissTimer = [NSTimer scheduledTimerWithTimeInterval:self.duration
		                                                    repeats:NO
		                                                      block:^(NSTimer*) {
		    [weakTimer dismiss];
		}];
	}
}

- (void)dismiss
{
	if (!self.isShowing)
		return;

	self.isShowing = NO;

	if (self.dismissTimer)
	{
		[self.dismissTimer invalidate];
		self.dismissTimer = nil;
	}

	[self stopPulseAnimation];
	[self stopWaitingGradientAnimation];
	[self stopShimmerAnimation];

	__weak KittyAlertView* weakSelf = self;

	[UIView animateWithDuration:kAnimationShowHideDuration
	                 animations:^{
		               weakSelf.alertDimmingView.alpha = 0;
		               weakSelf.alertBlurView.alpha    = 0;
	                 }];

	void (^completion)(BOOL) = ^(BOOL) {
	  __strong KittyAlertView* strongSelf = weakSelf;
	  // A show() between dismiss() and this completion re-adds the view; removing it
	  // here would silently tear down the alert that just appeared.
	  if (strongSelf && !strongSelf.isShowing)
	  {
		  [strongSelf removeFromSuperview];
		  if (strongSelf.dismissAction)
		  {
			  strongSelf.dismissAction();
		  }
	  }
	};

	if (self.hideAnimation == KittyAlertViewAnimationFade)
	{
		[UIView animateWithDuration:kAnimationShowHideDuration
		                 animations:^{
			               weakSelf.alertCardWrapper.alpha = 0;
		                 }
		                 completion:completion];
	}
	else if (self.hideAnimation == KittyAlertViewAnimationSlideFromTop)
	{
		[UIView animateWithDuration:kAnimationShowHideDuration
		                 animations:^{
			               weakSelf.alertCardWrapper.transform = CGAffineTransformMakeTranslation(0, -weakSelf.bounds.size.height);
		                 }
		                 completion:completion];
	}
	else if (self.hideAnimation == KittyAlertViewAnimationSlideFromBottom)
	{
		[UIView animateWithDuration:kAnimationShowHideDuration
		                 animations:^{
			               weakSelf.alertCardWrapper.transform = CGAffineTransformMakeTranslation(0, weakSelf.bounds.size.height);
		                 }
		                 completion:completion];
	}
	else if (self.hideAnimation == KittyAlertViewAnimationScale)
	{
		[UIView animateWithDuration:kAnimationShowHideDuration
		                 animations:^{
			               weakSelf.alertCardWrapper.transform = CGAffineTransformMakeScale(0.1, 0.1);
		                 }
		                 completion:completion];
	}
	else if (self.hideAnimation == KittyAlertViewAnimationBounce)
	{
		[UIView animateWithDuration:kAnimationShowHideDuration
		                 animations:^{
			               weakSelf.alertCardWrapper.transform = CGAffineTransformMakeScale(0.1, 0.1);
		                 }
		                 completion:completion];
	}
	else if (self.hideAnimation == KittyAlertViewAnimationRotate)
	{
		[UIView animateWithDuration:kAnimationShowHideDuration
		                 animations:^{
			               weakSelf.alertCardWrapper.transform = CGAffineTransformMakeRotation(M_PI);
			               weakSelf.alertCardWrapper.alpha     = 0;
		                 }
		                 completion:completion];
	}
	else
	{
		completion(YES);
	}
}

- (void)handleTap:(UITapGestureRecognizer*)gesture
{
	BOOL bTapInsideIcon = self.alertIconView && !self.alertIconView.hidden &&
	                      [self.alertIconView pointInside:[gesture locationInView:self.alertIconView] withEvent:nil];
	if (self.shouldDismissOnTapOutside && self.alertContentView && !bTapInsideIcon &&
	    ![self.alertContentView pointInside:[gesture locationInView:self.alertContentView] withEvent:nil])
	{
		[self dismiss];
	}
}

- (void)touchDown:(KittyAlertButton*)button
{
	if (!button)
		return;

	[UIView animateWithDuration:kAnimationButtonHoverDuration
	                 animations:^{
		               button.transform = CGAffineTransformMakeScale(kAnimationPulseScaleFactor, kAnimationPulseScaleFactor);
	                 }];
}

- (void)touchUpInside:(KittyAlertButton*)button
{
	if (!button)
		return;

	[UIView animateWithDuration:kAnimationShowHideDuration
	                      delay:0
	     usingSpringWithDamping:kAnimationSpringDamping
	      initialSpringVelocity:kAnimationSpringVelocity
	                    options:UIViewAnimationOptionCurveEaseInOut
	                 animations:^{
		               button.transform = CGAffineTransformIdentity;
	                 }
	                 completion:nil];
}

- (void)touchUpOutside:(KittyAlertButton*)button
{
	if (!button)
		return;

	[UIView animateWithDuration:kAnimationShowHideDuration
	                      delay:0
	     usingSpringWithDamping:kAnimationSpringDamping
	      initialSpringVelocity:kAnimationSpringVelocity
	                    options:UIViewAnimationOptionCurveEaseInOut
	                 animations:^{
		               button.transform = CGAffineTransformIdentity;
	                 }
	                 completion:nil];
}

- (void)buttonTapped:(KittyAlertButton*)sender
{
	if (!sender)
		return;

	BOOL bIsContentButton = [self.alertButtons indexOfObject:sender] != NSNotFound;
	BOOL bIsActionButton  = (sender == self.alertCancelButton || sender == self.alertConfirmButton);

	if (bIsContentButton || bIsActionButton)
	{
		if (sender.action)
			sender.action();
		[self dismiss];
	}
}

- (void)switchChanged:(UISwitch*)sender
{
	if (sender && sender.superview && [sender.superview isKindOfClass:[KittyAlertSwitch class]])
	{
		KittyAlertSwitch* container = (KittyAlertSwitch*)sender.superview;
		NSUInteger index            = [self.alertSwitches indexOfObject:container];
		if (index != NSNotFound)
		{
			if (container.action)
				container.action(sender.on);
		}
	}
}

- (void)setTitle:(nullable NSString*)title needsLayout:(BOOL)needsLayout
{
	self.alertTitleLabel.text = title;
	if (needsLayout && self.isShowing)
	{
		[self setNeedsLayout];
	}
}

- (void)setSubtitle:(nullable NSString*)subtitle needsLayout:(BOOL)needsLayout
{
	self.alertSubtitleTextView.text = subtitle;
	if (needsLayout && self.isShowing)
	{
		[self setNeedsLayout];
	}
}

- (void)setTintColor:(nullable UIColor*)tintColor
{
	_tintColor = tintColor ?: [self tintColorForStyle:self.alertStyle];

	self.alertIconView.tintColor = UIColor.whiteColor;

	for (KittyAlertButton* button in self.alertButtons)
	{
		if (button)
		{
			button.backgroundColor = _tintColor;
		}
	}

	for (KittyAlertSwitch* switchCon in self.alertSwitches)
	{
		if (switchCon && switchCon.switchControl)
		{
			switchCon.switchControl.tintColor   = _tintColor;
			switchCon.switchControl.onTintColor = _tintColor;
		}
	}

	self.alertIconView.image             = [self iconForStyle:self.alertStyle];
	self.alertIconView.tintColor         = UIColor.whiteColor;
	self.alertIconView.backgroundColor   = _tintColor;
	self.alertAccentBar.backgroundColor  = _tintColor;
	self.alertContentView.layer.borderColor = [_tintColor colorWithAlphaComponent:kAlertBorderAlpha].CGColor;

	if (self.alertCancelButton)
		self.alertCancelButton.backgroundColor = _tintColor;
	if (self.alertConfirmButton)
		self.alertConfirmButton.backgroundColor = _tintColor;
}

- (void)setBackgroundColor:(nullable UIColor*)backgroundColor
{
	_backgroundColor                      = backgroundColor ?: [self backgroundColorForStyle:self.alertStyle];
	self.alertContentView.backgroundColor = _backgroundColor;
}

- (void)setFontColor:(nullable UIColor*)fontColor
{
	_fontColor = fontColor ?: [self fontColorForStyle:self.alertStyle];

	self.alertTitleLabel.textColor                = _fontColor;
	self.alertTitleLabel.layer.shadowColor        = _fontColor.CGColor;
	self.alertSubtitleTextView.textColor          = [_fontColor colorWithAlphaComponent:kSubtitleFontAlpha];

	for (KittyAlertSwitch* switchCon in self.alertSwitches)
	{
		if (switchCon && switchCon.switchLabel)
		{
			switchCon.switchLabel.textColor = _fontColor;
		}
	}
}

- (void)setButtonFontColor:(nullable UIColor*)fontColor
{
	_buttonFontColor = fontColor ?: [self fontColorForStyle:self.alertStyle];

	for (KittyAlertButton* button in self.alertButtons)
	{
		if (button)
			[button setTitleColor:_buttonFontColor forState:UIControlStateNormal];
	}

	if (self.alertCancelButton)
		[self.alertCancelButton setTitleColor:_buttonFontColor forState:UIControlStateNormal];
	if (self.alertConfirmButton)
		[self.alertConfirmButton setTitleColor:_buttonFontColor forState:UIControlStateNormal];
}


- (void)setEnablePulseEffect:(BOOL)enablePulseEffect
{
	_enablePulseEffect = enablePulseEffect;
	if (self.isShowing)
	{
		if (enablePulseEffect)
			[self startPulseAnimation];
		else
			[self stopPulseAnimation];
	}
}

- (void)setShouldDismissOnTapOutside:(BOOL)shouldDismissOnTapOutside
{
	_shouldDismissOnTapOutside = shouldDismissOnTapOutside;
	if (self.isShowing)
	{
		self.dismissTapGesture.enabled = shouldDismissOnTapOutside;
	}
}

- (void)setDuration:(NSTimeInterval)duration
{
	_duration = duration;

	if (self.dismissTimer)
	{
		[self.dismissTimer invalidate];
		self.dismissTimer = nil;
	}

	if (duration > 0 && self.isShowing)
	{
		__weak KittyAlertView* weakTimer = self;
		self.dismissTimer = [NSTimer scheduledTimerWithTimeInterval:duration
		                                                    repeats:NO
		                                                      block:^(NSTimer*) {
		    [weakTimer dismiss];
		}];
	}
}

- (void)setShowAnimation:(KittyAlertViewAnimation)showAnimation
{
	_showAnimation = showAnimation;
}

- (void)setHideAnimation:(KittyAlertViewAnimation)hideAnimation
{
	_hideAnimation = hideAnimation;
}

- (void)setDismissAction:(void (^_Nullable)(void))dismissAction
{
	if (dismissAction)
		_dismissAction = [dismissAction copy];
	else
		_dismissAction = ^() {
		};
}

- (void)setCornerRadius:(CGFloat)cornerRadius
{
	_cornerRadius                            = cornerRadius;
	self.alertContentView.layer.cornerRadius = cornerRadius;
}

- (void)setDimmingAlpha:(CGFloat)dimmingAlpha
{
	_dimmingAlpha                         = dimmingAlpha;
	self.alertDimmingView.backgroundColor = [UIColor.blackColor colorWithAlphaComponent:dimmingAlpha];
}

- (void)setIconImage:(nullable UIImage*)iconImage
{
	_iconImage               = iconImage;
	self.alertIconView.image = iconImage;
	if (self.isShowing)
		[self setNeedsLayout];
}

- (UIColor*)darkerColor:(UIColor*)color
{
	CGFloat r, g, b, a;
	if ([color getRed:&r green:&g blue:&b alpha:&a])
	{
		return [UIColor colorWithRed:MAX(r * 0.6, 0.0)
		                       green:MAX(g * 0.6, 0.0)
		                        blue:MAX(b * 0.6, 0.0)
		                       alpha:a];
	}
	return color;
}

- (CALayer*)clockMaskLayerForSize:(CGFloat)size
{
	CAShapeLayer* mask = [CAShapeLayer layer];
	UIBezierPath* path = [UIBezierPath bezierPath];
	[path addArcWithCenter:CGPointMake(size / 2, size / 2)
	                radius:size / 2 - size * kIconLineWidthRatio
	            startAngle:0
	              endAngle:2 * M_PI
	             clockwise:YES];
	[path addArcWithCenter:CGPointMake(size / 2, size / 2)
	                radius:size * kIconInsetRatio
	            startAngle:0
	              endAngle:2 * M_PI
	             clockwise:NO];
	mask.path     = path.CGPath;
	mask.fillRule = kCAFillRuleEvenOdd;
	mask.frame    = CGRectMake(0, 0, size, size);
	return mask;
}

- (UIColor*)tintColorForStyle:(KittyAlertViewStyle)style
{
	switch (style)
	{
	case KittyAlertViewStyleSuccess:
		return UIColor.systemGreenColor;
	case KittyAlertViewStyleError:
		return UIColor.systemRedColor;
	case KittyAlertViewStyleWarning:
		return UIColor.systemOrangeColor;
	case KittyAlertViewStyleInfo:
		return kInfoTintColor;
	case KittyAlertViewStyleEdit:
		return UIColor.systemPurpleColor;
	case KittyAlertViewStyleWaiting:
		return kWaitingTintColor;
	case KittyAlertViewStyleCustom:
		return UIColor.systemIndigoColor;
	}
	return UIColor.labelColor;
}

- (UIColor*)backgroundColorForStyle:(KittyAlertViewStyle)style
{
	return UIColor.systemBackgroundColor;
}

- (UIColor*)fontColorForStyle:(KittyAlertViewStyle)style
{
	return UIColor.labelColor;
}

- (UIColor*)buttonFontColorForStyle:(KittyAlertViewStyle)style
{
	return UIColor.whiteColor;
}

- (UIImage*)iconForStyle:(KittyAlertViewStyle)style
{
	if (style == KittyAlertViewStyleCustom)
		return nil;

	// Waiting: UIActivityIndicatorView provides the spinner; 1×1 transparent placeholder
	// keeps alertIconView.image != nil so the badge stays visible.
	if (style == KittyAlertViewStyleWaiting)
	{
		UIGraphicsImageRendererFormat* fmt = [UIGraphicsImageRendererFormat defaultFormat];
		fmt.opaque                         = NO;
		UIGraphicsImageRenderer* r = [[UIGraphicsImageRenderer alloc] initWithSize:CGSizeMake(1, 1) format:fmt];
		return [r imageWithActions:^(UIGraphicsImageRendererContext* _Nonnull __unused ctx) {}];
	}

	NSString* symbolName;
	switch (style)
	{
	case KittyAlertViewStyleSuccess: symbolName = @"checkmark"; break;
	case KittyAlertViewStyleError:   symbolName = @"xmark"; break;
	case KittyAlertViewStyleWarning: symbolName = @"exclamationmark"; break;
	case KittyAlertViewStyleInfo:    symbolName = @"info"; break;
	case KittyAlertViewStyleEdit:    symbolName = @"pencil"; break;
	default:                         return nil;
	}

	// UIViewContentModeCenter displays the symbol at exactly this point size — no upscaling.
	UIImageSymbolConfiguration* cfg = [UIImageSymbolConfiguration
	    configurationWithPointSize:16.0 weight:UIImageSymbolWeightSemibold];
	return [UIImage systemImageNamed:symbolName withConfiguration:cfg];
}

- (void)dealloc
{
	if (self.dismissTimer)
	{
		[self.dismissTimer invalidate];
		self.dismissTimer = nil;
	}

	[[UIDevice currentDevice] endGeneratingDeviceOrientationNotifications];
	[[NSNotificationCenter defaultCenter] removeObserver:self];
	[self stopPulseAnimation];
	[self stopWaitingGradientAnimation];
	[self stopShimmerAnimation];
	[self.alertDimmingView removeGestureRecognizer:self.dismissTapGesture];
}

@end
