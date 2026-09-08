#import "KittyFileBrowser.h"

#include <string>

#import "Icons.h"
#import "UIUtils.h"

#include "../../Utils/Utils.h"

namespace Metrics
{
	// Cards
	constexpr CGFloat CardCornerRadius   = 22.0;
	constexpr CGFloat CardWidthFraction  = 0.92;
	constexpr CGFloat CardHeightFraction = 0.78;
	constexpr CGFloat CardMaxWidth       = 560.0;
	constexpr CGFloat CardMaxHeight      = 720.0;
	constexpr CGFloat CardMinHeight      = 280.0;

	// Bands
	constexpr CGFloat HeaderHeight     = 52.0;
	constexpr CGFloat BreadcrumbHeight = 26.0;
	constexpr CGFloat ActionBarHeight  = 60.0;
	constexpr CGFloat RowHeight        = 62.0;

	// Spacing scale
	constexpr CGFloat SpaceXS  = 3.0;
	constexpr CGFloat SpaceS   = 6.0;
	constexpr CGFloat SpaceM   = 10.0;
	constexpr CGFloat SpaceL   = 12.0;
	constexpr CGFloat SpaceXL  = 16.0;
	constexpr CGFloat SpaceXXL = 24.0;

	// Controls
	constexpr CGFloat SidePadding     = SpaceXL;
	constexpr CGFloat BackButtonWidth = 30.0;
	constexpr CGFloat BackButtonInset = 4.0;
	constexpr CGFloat CloseButtonSize = 26.0;
	constexpr CGFloat RowIconSize     = 26.0;
	constexpr CGFloat RowChevronSize  = 12.0;
	/// The chevron itself is far too small to hit; its button is padded out to this.
	constexpr CGFloat RowTapTarget     = 44.0;
	constexpr CGFloat RowTopInset      = 11.0;
	constexpr CGFloat CheckSize        = 22.0;
	constexpr CGFloat CheckRadius      = CheckSize / 2.0;
	constexpr CGFloat CheckBorderWidth = 1.5;

	constexpr CGFloat ActionButtonWidthFraction = 0.5;
	constexpr CGFloat EmptyLabelInset           = SpaceXXL;
}

namespace Typography
{
	constexpr CGFloat TitleSize      = 17.0;
	constexpr CGFloat BreadcrumbSize = 11.5;
	constexpr CGFloat RowNameSize    = 15.0;
	constexpr CGFloat RowDetailSize  = 12.0;
	constexpr CGFloat ButtonSize     = 15.0;
	constexpr CGFloat EmptySize      = 14.0;
}

namespace Motion
{
	constexpr NSTimeInterval ShowDuration = 0.34;
	constexpr NSTimeInterval HideDuration = 0.20;

	constexpr CGFloat ShowSpringDamping  = 0.86;
	constexpr CGFloat ShowSpringVelocity = 0.20;
	constexpr CGFloat ShowFromScale      = 0.92;
	constexpr CGFloat HideToScale        = 0.95;
}

namespace Palette
{
	constexpr CGFloat BackdropAlpha      = 0.45;
	constexpr CGFloat SecondaryTextAlpha = 0.55;
	constexpr CGFloat TertiaryTextAlpha  = 0.45;
	constexpr CGFloat MutedTextAlpha     = 0.40;
	constexpr CGFloat DisabledTextAlpha  = 0.25;
	constexpr CGFloat SeparatorAlpha     = 0.12;
	constexpr CGFloat HairlineAlpha      = 0.08;
	constexpr CGFloat SurfaceAlpha       = 0.06;
	constexpr CGFloat ChevronAlpha       = 0.35;
	constexpr CGFloat DocIconAlpha       = 0.65;
	constexpr CGFloat CloseIconAlpha     = 0.60;

	inline UIColor* Backdrop() { return [UIColor colorWithWhite:0.0 alpha:BackdropAlpha]; }
	inline UIColor* PrimaryText() { return UIColor.whiteColor; }
	inline UIColor* SecondaryText() { return [UIColor colorWithWhite:1.0 alpha:SecondaryTextAlpha]; }
	inline UIColor* TertiaryText() { return [UIColor colorWithWhite:1.0 alpha:TertiaryTextAlpha]; }
	inline UIColor* MutedText() { return [UIColor colorWithWhite:1.0 alpha:MutedTextAlpha]; }
	inline UIColor* DisabledText() { return [UIColor colorWithWhite:1.0 alpha:DisabledTextAlpha]; }
	inline UIColor* Separator() { return [UIColor colorWithWhite:1.0 alpha:SeparatorAlpha]; }
	inline UIColor* Hairline() { return [UIColor colorWithWhite:1.0 alpha:HairlineAlpha]; }
	inline UIColor* Surface() { return [UIColor colorWithWhite:1.0 alpha:SurfaceAlpha]; }
	inline UIColor* Chevron() { return [UIColor colorWithWhite:1.0 alpha:ChevronAlpha]; }
	inline UIColor* CloseIcon() { return [UIColor colorWithWhite:1.0 alpha:CloseIconAlpha]; }

	inline UIColor* Accent() { return [UIColor colorWithRed:0.20 green:0.60 blue:1.00 alpha:1.0]; }
	inline UIColor* Destructive() { return [UIColor colorWithRed:1.00 green:0.35 blue:0.35 alpha:1.0]; }
	inline UIColor* FolderIcon() { return [UIColor colorWithRed:0.35 green:0.66 blue:1.00 alpha:1.0]; }
	inline UIColor* ArchiveIcon() { return [UIColor colorWithRed:0.98 green:0.76 blue:0.30 alpha:1.0]; }
	inline UIColor* DocIcon() { return [UIColor colorWithWhite:1.0 alpha:DocIconAlpha]; }
}

static NSString* const kTitleRoot         = @"Dumps";
static NSString* const kTitleSelectedFmt  = @"%lu selected";
static NSString* const kButtonShare       = @"Share";
static NSString* const kButtonDelete      = @"Delete";
static NSString* const kEmptyRoot         = @"No dumps yet.\nRun a dump and it will appear here.";
static NSString* const kEmptyFolder       = @"This folder is empty.";
static NSString* const kDetailFolderFmt   = @"%lu item%@ · %@";
static NSString* const kDetailFileFmt     = @"%@ · %@";
static NSString* const kDateFormat        = @"MMM d HH:mm";
static NSString* const kNoDatePlaceholder = @"—";
static NSString* const kZipExtension      = @"zip";

static NSString* const kPreparingTitle     = @"Preparing…";
static NSString* const kPreparingBody      = @"Compressing folders for sharing.";
static NSString* const kShareFailedTitle   = @"Share Failed";
static NSString* const kShareFailedBody    = @"Could not compress the selected folders.";
static NSString* const kPartialTitle       = @"Some Items Skipped";
static NSString* const kPartialBodyFmt     = @"Could not compress: %@";
static NSString* const kDeleteTitle        = @"Delete";
static NSString* const kDeleteOneFmt       = @"\"%@\"";
static NSString* const kDeleteManyFmt      = @"%lu items";
static NSString* const kDeleteConfirmFmt   = @"Delete %@? This cannot be undone.";
static NSString* const kDeleteElsewhereFmt = @" (%lu outside this folder)";
static NSString* const kDeleteFailedTitle  = @"Delete Failed";
static NSString* const kDeleteFailedFmt    = @"Could not delete: %@";
static NSString* const kNoPathTitle        = @"No Output Path";
static NSString* const kNoPathBody         = @"The dump output path has not been set.";

static NSString* const kListSeparator = @", ";
static NSString* const kPluralSuffix  = @"s";

static NSString* const kCellReuseId = @"KittyFileCell";

@interface KittyShareItem : NSObject <UIActivityItemSource>

- (instancetype)initWithURL:(NSURL*)Url;

@property(nonatomic, strong) NSURL* url;

@end

@implementation KittyShareItem

- (instancetype)initWithURL:(NSURL*)Url
{
	self = [super init];
	if (self)
		_url = Url;
	return self;
}

- (id)activityViewControllerPlaceholderItem:(UIActivityViewController*)Controller
{
	return self.url;
}

- (id)activityViewController:(UIActivityViewController*)Controller
         itemForActivityType:(UIActivityType)ActivityType
{
	return self.url;
}

- (NSString*)activityViewController:(UIActivityViewController*)Controller
             subjectForActivityType:(UIActivityType)ActivityType
{
	return self.url.lastPathComponent;
}

- (UIImage*)activityViewController:(UIActivityViewController*)Controller
     thumbnailImageForActivityType:(UIActivityType)ActivityType
                     suggestedSize:(CGSize)Size
{
	return nil;
}

@end

// ── Entry model ───────────────────────────────────────────────────────────────

@interface KittyFileEntry : NSObject

@property(nonatomic, strong) NSURL* url;
@property(nonatomic, copy) NSString* name;
@property(nonatomic, copy) NSString* detail;
@property(nonatomic, assign) BOOL isDirectory;
@property(nonatomic, strong) NSDate* modified;

@end

@implementation KittyFileEntry
@end

// ── Row ───────────────────────────────────────────────────────────────────────

@interface KittyFileCell : UITableViewCell

@property(nonatomic, strong) UIImageView* iconView;
@property(nonatomic, strong) UILabel* nameLabel;
@property(nonatomic, strong) UILabel* detailLabel;
@property(nonatomic, strong) UIImageView* chevronView;
@property(nonatomic, strong) UIImageView* checkView;
@property(nonatomic, strong) NSLayoutConstraint* checkWidth;
@property(nonatomic, strong) UIButton* checkHitButton;
@property(nonatomic, strong) UIButton* chevronHitButton;
/// Selection is the primary action, so the row and the checkbox both toggle it.
/// Descending into a folder is secondary and belongs to the chevron alone.
@property(nonatomic, copy) void (^onToggleCheck)(void);
@property(nonatomic, copy) void (^onNavigate)(void);

- (void)applyEntry:(KittyFileEntry*)Entry checked:(BOOL)Checked;

@end

@implementation KittyFileCell

- (instancetype)initWithStyle:(UITableViewCellStyle)Style reuseIdentifier:(NSString*)Reuse
{
	self = [super initWithStyle:Style reuseIdentifier:Reuse];
	if (!self)
		return nil;

	self.backgroundColor             = UIColor.clearColor;
	self.contentView.backgroundColor = UIColor.clearColor;

	UIView* Highlight           = [[UIView alloc] init];
	Highlight.backgroundColor   = Palette::Hairline();
	self.selectedBackgroundView = Highlight;

	_checkView                                           = [[UIImageView alloc] init];
	_checkView.translatesAutoresizingMaskIntoConstraints = NO;
	_checkView.contentMode                               = UIViewContentModeCenter;
	_checkView.layer.cornerRadius                        = Metrics::CheckRadius;
	[self.contentView addSubview:_checkView];

	// The checkbox itself is too small to hit reliably, so an invisible button
	// covers it with padding on every side.
	_checkHitButton                                           = [UIButton buttonWithType:UIButtonTypeCustom];
	_checkHitButton.translatesAutoresizingMaskIntoConstraints = NO;
	[_checkHitButton addTarget:self action:@selector(checkTapped) forControlEvents:UIControlEventTouchUpInside];
	[self.contentView addSubview:_checkHitButton];

	_iconView                                           = [[UIImageView alloc] init];
	_iconView.translatesAutoresizingMaskIntoConstraints = NO;
	_iconView.contentMode                               = UIViewContentModeScaleAspectFit;
	[self.contentView addSubview:_iconView];

	_nameLabel                                           = [[UILabel alloc] init];
	_nameLabel.translatesAutoresizingMaskIntoConstraints = NO;
	_nameLabel.font                                      = [UIFont systemFontOfSize:Typography::RowNameSize weight:UIFontWeightMedium];
	_nameLabel.textColor                                 = Palette::PrimaryText();
	_nameLabel.lineBreakMode                             = NSLineBreakByTruncatingMiddle;
	[self.contentView addSubview:_nameLabel];

	_detailLabel                                           = [[UILabel alloc] init];
	_detailLabel.translatesAutoresizingMaskIntoConstraints = NO;
	_detailLabel.font                                      = [UIFont systemFontOfSize:Typography::RowDetailSize weight:UIFontWeightRegular];
	_detailLabel.textColor                                 = Palette::SecondaryText();
	[self.contentView addSubview:_detailLabel];

	_chevronView                                           = [[UIImageView alloc] initWithImage:Icons::ChevronRight()];
	_chevronView.translatesAutoresizingMaskIntoConstraints = NO;
	_chevronView.contentMode                               = UIViewContentModeScaleAspectFit;
	_chevronView.tintColor                                 = Palette::Chevron();
	[self.contentView addSubview:_chevronView];

	_chevronHitButton                                           = [UIButton buttonWithType:UIButtonTypeCustom];
	_chevronHitButton.translatesAutoresizingMaskIntoConstraints = NO;
	[_chevronHitButton addTarget:self action:@selector(chevronTapped) forControlEvents:UIControlEventTouchUpInside];
	[self.contentView addSubview:_chevronHitButton];

	UILayoutGuide* Margins = self.contentView.layoutMarginsGuide;

	_checkWidth = [_checkView.widthAnchor constraintEqualToConstant:0.0];

	[NSLayoutConstraint activateConstraints:@[
		[_checkView.leadingAnchor constraintEqualToAnchor:Margins.leadingAnchor],
		[_checkView.centerYAnchor constraintEqualToAnchor:self.contentView.centerYAnchor],
		_checkWidth,
		[_checkView.heightAnchor constraintEqualToConstant:Metrics::CheckSize],

		[_checkHitButton.centerXAnchor constraintEqualToAnchor:_checkView.centerXAnchor],
		[_checkHitButton.centerYAnchor constraintEqualToAnchor:_checkView.centerYAnchor],
		[_checkHitButton.widthAnchor constraintEqualToConstant:Metrics::CheckSize + Metrics::SpaceL],
		[_checkHitButton.heightAnchor constraintEqualToAnchor:self.contentView.heightAnchor],

		[_iconView.leadingAnchor constraintEqualToAnchor:_checkView.trailingAnchor
		                                        constant:Metrics::SpaceS + Metrics::SpaceXS],
		[_iconView.centerYAnchor constraintEqualToAnchor:self.contentView.centerYAnchor],
		[_iconView.widthAnchor constraintEqualToConstant:Metrics::RowIconSize],
		[_iconView.heightAnchor constraintEqualToConstant:Metrics::RowIconSize],

		[_chevronView.trailingAnchor constraintEqualToAnchor:Margins.trailingAnchor],
		[_chevronView.centerYAnchor constraintEqualToAnchor:self.contentView.centerYAnchor],
		[_chevronView.widthAnchor constraintEqualToConstant:Metrics::RowChevronSize],
		[_chevronView.heightAnchor constraintEqualToConstant:Metrics::RowChevronSize],

		[_chevronHitButton.centerXAnchor constraintEqualToAnchor:_chevronView.centerXAnchor],
		[_chevronHitButton.centerYAnchor constraintEqualToAnchor:_chevronView.centerYAnchor],
		[_chevronHitButton.widthAnchor constraintEqualToConstant:Metrics::RowTapTarget],
		[_chevronHitButton.heightAnchor constraintEqualToAnchor:self.contentView.heightAnchor],

		[_nameLabel.leadingAnchor constraintEqualToAnchor:_iconView.trailingAnchor
		                                         constant:Metrics::SpaceL],
		[_nameLabel.trailingAnchor constraintEqualToAnchor:_chevronView.leadingAnchor
		                                          constant:-Metrics::SpaceM],
		[_nameLabel.topAnchor constraintEqualToAnchor:self.contentView.topAnchor
		                                     constant:Metrics::RowTopInset],

		[_detailLabel.leadingAnchor constraintEqualToAnchor:_nameLabel.leadingAnchor],
		[_detailLabel.trailingAnchor constraintEqualToAnchor:_nameLabel.trailingAnchor],
		[_detailLabel.topAnchor constraintEqualToAnchor:_nameLabel.bottomAnchor
		                                       constant:Metrics::SpaceXS],
		[_detailLabel.bottomAnchor constraintLessThanOrEqualToAnchor:self.contentView.bottomAnchor
		                                                      constant:-Metrics::SpaceS],
	]];

	return self;
}

- (void)prepareForReuse
{
	[super prepareForReuse];
	self.onToggleCheck = nil;
	self.onNavigate    = nil;
}

- (void)checkTapped
{
	if (self.onToggleCheck)
		self.onToggleCheck();
}

- (void)chevronTapped
{
	if (self.onNavigate)
		self.onNavigate();
}

- (void)applyEntry:(KittyFileEntry*)Entry checked:(BOOL)Checked
{
	self.nameLabel.text   = Entry.name;
	self.detailLabel.text = Entry.detail;

	if (Entry.isDirectory)
	{
		self.iconView.image     = Icons::Folder();
		self.iconView.tintColor = Palette::FolderIcon();
	}
	else if ([Entry.name.pathExtension.lowercaseString isEqualToString:kZipExtension])
	{
		self.iconView.image     = Icons::Archive();
		self.iconView.tintColor = Palette::ArchiveIcon();
	}
	else
	{
		self.iconView.image     = Icons::Doc();
		self.iconView.tintColor = Palette::DocIcon();
	}

	// The chevron means "descend" and coexists with the checkbox. Its button is
	// disabled for files so the row underneath still receives the tap.
	self.chevronView.hidden       = !Entry.isDirectory;
	self.chevronHitButton.enabled = Entry.isDirectory;

	self.checkView.image             = Checked ? Icons::Check() : nil;
	self.checkView.tintColor         = Palette::PrimaryText();
	self.checkView.backgroundColor   = Checked ? Palette::Accent() : UIColor.clearColor;
	self.checkView.layer.borderWidth = Checked ? 0.0 : Metrics::CheckBorderWidth;
	self.checkView.layer.borderColor = Palette::MutedText().CGColor;

	self.checkWidth.constant = Metrics::CheckSize;
}

@end

// ── Browser ───────────────────────────────────────────────────────────────────

@interface KittyFileBrowser () <UITableViewDelegate, UITableViewDataSource>

@property(nonatomic, strong) UIView* backdrop;
@property(nonatomic, strong) UIVisualEffectView* card;

@property(nonatomic, strong) UIButton* backButton;
@property(nonatomic, strong) UILabel* titleLabel;
@property(nonatomic, strong) UIButton* closeButton;
@property(nonatomic, strong) UILabel* breadcrumbLabel;

@property(nonatomic, strong) UITableView* tableView;
@property(nonatomic, strong) UILabel* emptyLabel;

@property(nonatomic, strong) UIView* actionBar;
@property(nonatomic, strong) NSLayoutConstraint* actionBarHeight;
@property(nonatomic, strong) UIButton* shareButton;
@property(nonatomic, strong) UIButton* deleteButton;

@property(nonatomic, strong) NSURL* rootURL;
@property(nonatomic, strong) NSURL* currentURL;
@property(nonatomic, strong) NSMutableArray<KittyFileEntry*>* entries;
/// Keyed by standardized path rather than NSURL: two NSURLs for the same file can
/// compare unequal over a trailing slash or a /private prefix. Holds the entry so a
/// selection stays actionable after navigating away from the folder it lives in.
@property(nonatomic, strong) NSMutableDictionary<NSString*, KittyFileEntry*>* selection;
@property(nonatomic, assign) NSInteger zipLevel;

@property(nonatomic, assign) BOOL isVisible;

@property(nonatomic, strong) NSByteCountFormatter* byteFormatter;
@property(nonatomic, strong) NSDateFormatter* dateFormatter;

@end

@implementation KittyFileBrowser

+ (instancetype)sharedBrowser
{
	static KittyFileBrowser* Shared = nil;
	static dispatch_once_t Token;
	dispatch_once(&Token, ^{
	  Shared = [[self alloc] initWithFrame:UIScreen.mainScreen.bounds];
	});
	return Shared;
}

- (instancetype)initWithFrame:(CGRect)Frame
{
	self = [super initWithFrame:Frame];
	if (!self)
		return nil;

	_entries   = [NSMutableArray array];
	_selection = [NSMutableDictionary dictionary];
	_zipLevel  = 6;

	_byteFormatter            = [[NSByteCountFormatter alloc] init];
	_byteFormatter.countStyle = NSByteCountFormatterCountStyleFile;

	_dateFormatter            = [[NSDateFormatter alloc] init];
	_dateFormatter.dateFormat = kDateFormat;

	[self buildCard];
	[self buildHeader];
	[self buildList];
	[self buildActionBar];
	[self activateLayout];

	return self;
}

#pragma mark - Construction

- (void)buildCard
{
	self.backgroundColor = UIColor.clearColor;

	_backdrop                                           = [[UIView alloc] init];
	_backdrop.backgroundColor                           = Palette::Backdrop();
	_backdrop.translatesAutoresizingMaskIntoConstraints = NO;
	[self addSubview:_backdrop];

	[_backdrop addGestureRecognizer:[[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(hide)]];

	UIBlurEffect* Blur = [UIBlurEffect effectWithStyle:UIBlurEffectStyleDark];

	_card                                           = [[UIVisualEffectView alloc] initWithEffect:Blur];
	_card.translatesAutoresizingMaskIntoConstraints = NO;
	_card.layer.cornerRadius                        = Metrics::CardCornerRadius;
	_card.clipsToBounds                             = YES;
	[self addSubview:_card];
}

- (void)buildHeader
{
	UIView* Content = _card.contentView;

	_backButton                                           = [UIButton buttonWithType:UIButtonTypeCustom];
	_backButton.translatesAutoresizingMaskIntoConstraints = NO;
	[_backButton setImage:Icons::ChevronLeft() forState:UIControlStateNormal];
	_backButton.tintColor = Palette::PrimaryText();
	_backButton.hidden    = YES;
	[_backButton addTarget:self action:@selector(goBack) forControlEvents:UIControlEventTouchUpInside];
	[Content addSubview:_backButton];

	_titleLabel                                           = [[UILabel alloc] init];
	_titleLabel.translatesAutoresizingMaskIntoConstraints = NO;
	_titleLabel.font                                      = [UIFont systemFontOfSize:Typography::TitleSize weight:UIFontWeightSemibold];
	_titleLabel.textColor                                 = Palette::PrimaryText();
	_titleLabel.lineBreakMode                             = NSLineBreakByTruncatingHead;
	[Content addSubview:_titleLabel];

	_closeButton                                           = [UIButton buttonWithType:UIButtonTypeCustom];
	_closeButton.translatesAutoresizingMaskIntoConstraints = NO;
	[_closeButton setImage:Icons::Close() forState:UIControlStateNormal];
	_closeButton.tintColor = Palette::CloseIcon();
	[_closeButton addTarget:self action:@selector(hide) forControlEvents:UIControlEventTouchUpInside];
	[Content addSubview:_closeButton];

	_breadcrumbLabel                                           = [[UILabel alloc] init];
	_breadcrumbLabel.translatesAutoresizingMaskIntoConstraints = NO;
	_breadcrumbLabel.font                                      = [UIFont systemFontOfSize:Typography::BreadcrumbSize weight:UIFontWeightRegular];
	_breadcrumbLabel.textColor                                 = Palette::TertiaryText();
	_breadcrumbLabel.lineBreakMode                             = NSLineBreakByTruncatingHead;
	[Content addSubview:_breadcrumbLabel];
}

- (void)buildList
{
	UIView* Content = _card.contentView;

	_tableView                                           = [[UITableView alloc] initWithFrame:CGRectZero style:UITableViewStylePlain];
	_tableView.translatesAutoresizingMaskIntoConstraints = NO;
	_tableView.backgroundColor                           = UIColor.clearColor;
	_tableView.separatorColor                            = Palette::Hairline();
	_tableView.separatorInset                            = UIEdgeInsetsMake(0, Metrics::SidePadding, 0, Metrics::SidePadding);
	_tableView.rowHeight = Metrics::RowHeight;
	_tableView.delegate                                  = self;
	_tableView.dataSource                                = self;
	_tableView.indicatorStyle                            = UIScrollViewIndicatorStyleWhite;
	[_tableView registerClass:KittyFileCell.class forCellReuseIdentifier:kCellReuseId];
	[Content addSubview:_tableView];

	_emptyLabel                                           = [[UILabel alloc] init];
	_emptyLabel.translatesAutoresizingMaskIntoConstraints = NO;
	_emptyLabel.font                                      = [UIFont systemFontOfSize:Typography::EmptySize weight:UIFontWeightRegular];
	_emptyLabel.textColor                                 = Palette::MutedText();
	_emptyLabel.textAlignment                             = NSTextAlignmentCenter;
	_emptyLabel.numberOfLines                             = 0;
	_emptyLabel.hidden                                    = YES;
	[Content addSubview:_emptyLabel];
}

- (void)buildActionBar
{
	UIView* Content = _card.contentView;

	_actionBar                                           = [[UIView alloc] init];
	_actionBar.translatesAutoresizingMaskIntoConstraints = NO;
	_actionBar.backgroundColor                           = Palette::Surface();
	_actionBar.clipsToBounds                             = YES;
	[Content addSubview:_actionBar];

	_shareButton = [self actionButtonWithTitle:kButtonShare
	                                      icon:Icons::Share()
	                                     color:Palette::Accent()
	                                    action:@selector(shareSelection)];
	[_actionBar addSubview:_shareButton];

	_deleteButton = [self actionButtonWithTitle:kButtonDelete
	                                       icon:Icons::Trash()
	                                      color:Palette::Destructive()
	                                     action:@selector(deleteSelection)];
	[_actionBar addSubview:_deleteButton];
}

- (UIButton*)actionButtonWithTitle:(NSString*)Title
                              icon:(UIImage*)Icon
                             color:(UIColor*)Color
                            action:(SEL)Action
{
	UIButton* Button                                 = [UIButton buttonWithType:UIButtonTypeCustom];
	Button.translatesAutoresizingMaskIntoConstraints = NO;
	Button.titleLabel.font                           = [UIFont systemFontOfSize:Typography::ButtonSize weight:UIFontWeightMedium];
	[Button setTitle:Title forState:UIControlStateNormal];
	[Button setImage:Icon forState:UIControlStateNormal];
	[Button setTitleColor:Color forState:UIControlStateNormal];
	[Button setTitleColor:Palette::DisabledText() forState:UIControlStateDisabled];
	Button.tintColor       = Color;
	Button.imageEdgeInsets = UIEdgeInsetsMake(0, -Metrics::SpaceS, 0, 0);
	Button.titleEdgeInsets = UIEdgeInsetsMake(0, Metrics::SpaceS, 0, 0);
	[Button addTarget:self action:Action forControlEvents:UIControlEventTouchUpInside];
	return Button;
}

- (void)activateLayout
{
	UIView* Content     = _card.contentView;
	UILayoutGuide* Safe = self.safeAreaLayoutGuide;

	UIView* Separator                                   = [[UIView alloc] init];
	Separator.translatesAutoresizingMaskIntoConstraints = NO;
	Separator.backgroundColor                           = Palette::Separator();
	[Content addSubview:Separator];

	NSLayoutConstraint* CardWidth  = [_card.widthAnchor constraintEqualToAnchor:Safe.widthAnchor
                                                                    multiplier:Metrics::CardWidthFraction];
	NSLayoutConstraint* CardHeight = [_card.heightAnchor constraintEqualToAnchor:Safe.heightAnchor
	                                                                  multiplier:Metrics::CardHeightFraction];
	// Below required so the max/min caps below can win on large or short screens.
	CardWidth.priority  = UILayoutPriorityRequired - 1;
	CardHeight.priority = UILayoutPriorityRequired - 1;

	_actionBarHeight = [_actionBar.heightAnchor constraintEqualToConstant:Metrics::ActionBarHeight];

	[NSLayoutConstraint activateConstraints:@[
		[_backdrop.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
		[_backdrop.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
		[_backdrop.topAnchor constraintEqualToAnchor:self.topAnchor],
		[_backdrop.bottomAnchor constraintEqualToAnchor:self.bottomAnchor],

		[_card.centerXAnchor constraintEqualToAnchor:Safe.centerXAnchor],
		[_card.centerYAnchor constraintEqualToAnchor:Safe.centerYAnchor],
		CardWidth,
		CardHeight,
		[_card.widthAnchor constraintLessThanOrEqualToConstant:Metrics::CardMaxWidth],
		[_card.heightAnchor constraintLessThanOrEqualToConstant:Metrics::CardMaxHeight],
		[_card.heightAnchor constraintGreaterThanOrEqualToConstant:Metrics::CardMinHeight],

		[_backButton.leadingAnchor constraintEqualToAnchor:Content.leadingAnchor
		                                          constant:Metrics::SidePadding - Metrics::BackButtonInset],
		[_backButton.topAnchor constraintEqualToAnchor:Content.topAnchor],
		[_backButton.heightAnchor constraintEqualToConstant:Metrics::HeaderHeight],
		[_backButton.widthAnchor constraintEqualToConstant:Metrics::BackButtonWidth],

		[_titleLabel.leadingAnchor constraintEqualToAnchor:_backButton.trailingAnchor
		                                          constant:Metrics::SpaceS],
		[_titleLabel.centerYAnchor constraintEqualToAnchor:_backButton.centerYAnchor],
		[_titleLabel.trailingAnchor constraintLessThanOrEqualToAnchor:_closeButton.leadingAnchor
		                                                     constant:-Metrics::SpaceM],


		[_closeButton.trailingAnchor constraintEqualToAnchor:Content.trailingAnchor
		                                            constant:-Metrics::SidePadding],
		[_closeButton.centerYAnchor constraintEqualToAnchor:_backButton.centerYAnchor],
		[_closeButton.widthAnchor constraintEqualToConstant:Metrics::CloseButtonSize],
		[_closeButton.heightAnchor constraintEqualToConstant:Metrics::CloseButtonSize],

		[_breadcrumbLabel.leadingAnchor constraintEqualToAnchor:Content.leadingAnchor
		                                               constant:Metrics::SidePadding],
		[_breadcrumbLabel.trailingAnchor constraintEqualToAnchor:Content.trailingAnchor
		                                                constant:-Metrics::SidePadding],
		[_breadcrumbLabel.topAnchor constraintEqualToAnchor:_backButton.bottomAnchor],
		[_breadcrumbLabel.heightAnchor constraintEqualToConstant:Metrics::BreadcrumbHeight],

		[Separator.leadingAnchor constraintEqualToAnchor:Content.leadingAnchor],
		[Separator.trailingAnchor constraintEqualToAnchor:Content.trailingAnchor],
		[Separator.topAnchor constraintEqualToAnchor:_breadcrumbLabel.bottomAnchor],
		[Separator.heightAnchor constraintEqualToConstant:1.0 / UIScreen.mainScreen.scale],

		[_tableView.leadingAnchor constraintEqualToAnchor:Content.leadingAnchor],
		[_tableView.trailingAnchor constraintEqualToAnchor:Content.trailingAnchor],
		[_tableView.topAnchor constraintEqualToAnchor:Separator.bottomAnchor],
		[_tableView.bottomAnchor constraintEqualToAnchor:_actionBar.topAnchor],

		[_emptyLabel.centerXAnchor constraintEqualToAnchor:_tableView.centerXAnchor],
		[_emptyLabel.centerYAnchor constraintEqualToAnchor:_tableView.centerYAnchor],
		[_emptyLabel.leadingAnchor constraintEqualToAnchor:Content.leadingAnchor
		                                          constant:Metrics::EmptyLabelInset],
		[_emptyLabel.trailingAnchor constraintEqualToAnchor:Content.trailingAnchor
		                                           constant:-Metrics::EmptyLabelInset],

		[_actionBar.leadingAnchor constraintEqualToAnchor:Content.leadingAnchor],
		[_actionBar.trailingAnchor constraintEqualToAnchor:Content.trailingAnchor],
		[_actionBar.bottomAnchor constraintEqualToAnchor:Content.bottomAnchor],
		_actionBarHeight,

		[_shareButton.leadingAnchor constraintEqualToAnchor:_actionBar.leadingAnchor],
		[_shareButton.topAnchor constraintEqualToAnchor:_actionBar.topAnchor],
		[_shareButton.bottomAnchor constraintEqualToAnchor:_actionBar.bottomAnchor],
		[_shareButton.widthAnchor constraintEqualToAnchor:_actionBar.widthAnchor
		                                       multiplier:Metrics::ActionButtonWidthFraction],

		[_deleteButton.trailingAnchor constraintEqualToAnchor:_actionBar.trailingAnchor],
		[_deleteButton.topAnchor constraintEqualToAnchor:_actionBar.topAnchor],
		[_deleteButton.bottomAnchor constraintEqualToAnchor:_actionBar.bottomAnchor],
		[_deleteButton.widthAnchor constraintEqualToAnchor:_actionBar.widthAnchor
		                                        multiplier:Metrics::ActionButtonWidthFraction],
	]];
}

#pragma mark - Listing

- (void)setZipCompressionLevel:(NSInteger)Level
{
	// The zip library rejects anything outside 0-9 outright, so clamp rather than
	// let a stray value fail the share silently.
	self.zipLevel = MIN(MAX(Level, 0), 9);
}

- (void)reload
{
	[self.entries removeAllObjects];

	NSFileManager* Fm = NSFileManager.defaultManager;

	NSArray<NSURL*>* Contents =
	    [Fm contentsOfDirectoryAtURL:self.currentURL
	        includingPropertiesForKeys:@[ NSURLIsDirectoryKey, NSURLFileSizeKey, NSURLContentModificationDateKey ]
	                           options:NSDirectoryEnumerationSkipsHiddenFiles
	                             error:nil];

	for (NSURL* Url in Contents)
	{
		NSNumber* IsDir = nil;
		NSNumber* Size  = nil;
		NSDate* Mod     = nil;
		[Url getResourceValue:&IsDir forKey:NSURLIsDirectoryKey error:nil];
		[Url getResourceValue:&Size forKey:NSURLFileSizeKey error:nil];
		[Url getResourceValue:&Mod forKey:NSURLContentModificationDateKey error:nil];

		KittyFileEntry* Entry = [[KittyFileEntry alloc] init];
		Entry.url             = Url;
		Entry.name            = Url.lastPathComponent;
		Entry.isDirectory     = IsDir.boolValue;
		Entry.modified        = Mod ?: NSDate.distantPast;

		NSString* When = Mod ? [self.dateFormatter stringFromDate:Mod] : kNoDatePlaceholder;

		if (Entry.isDirectory)
		{
			NSUInteger Count = [Fm contentsOfDirectoryAtPath:Url.path error:nil].count;
			Entry.detail     = [NSString stringWithFormat:kDetailFolderFmt,
                                                      (unsigned long)Count,
                                                      Count == 1 ? @"" : kPluralSuffix,
                                                      When];
		}
		else
		{
			Entry.detail = [NSString stringWithFormat:kDetailFileFmt,
			                                          [self.byteFormatter stringFromByteCount:Size.longLongValue],
			                                          When];
		}

		[self.entries addObject:Entry];
	}

	// Newest first — the dump you just made is the one you want.
	[self.entries sortUsingComparator:^NSComparisonResult(KittyFileEntry* A, KittyFileEntry* B) {
	  return [B.modified compare:A.modified];
	}];

	[self pruneMissingSelections];

	[self.tableView reloadData];
	[self refreshChrome];
}

// A selection now outlives the folder it was made in, so it can also outlive the file
// itself. Without this the count would keep advertising entries that no longer exist
// and Share would hand the sheet a dead URL.
- (void)pruneMissingSelections
{
	NSFileManager* Fm = NSFileManager.defaultManager;

	NSMutableArray<NSString*>* Gone = [NSMutableArray array];
	for (NSString* Key in self.selection)
	{
		if (![Fm fileExistsAtPath:self.selection[Key].url.path])
			[Gone addObject:Key];
	}

	[self.selection removeObjectsForKeys:Gone];
}

static NSString* SelectionKey(NSURL* Url)
{
	return Url.URLByStandardizingPath.path ?: Url.path;
}

- (void)refreshChrome
{
	const BOOL bAtRoot = [self.currentURL.path isEqualToString:self.rootURL.path];
	const BOOL bEmpty  = self.entries.count == 0;

	self.backButton.hidden = bAtRoot;

	if (self.selection.count > 0)
		self.titleLabel.text = [NSString stringWithFormat:kTitleSelectedFmt, (unsigned long)self.selection.count];
	else
		self.titleLabel.text = bAtRoot ? kTitleRoot : self.currentURL.lastPathComponent;

	// Show the path relative to the container so the meaningful tail survives.
	NSString* Shown = self.currentURL.path;
	NSString* Home  = NSHomeDirectory();
	if ([Shown hasPrefix:Home])
		Shown = [Shown substringFromIndex:Home.length];
	self.breadcrumbLabel.text = Shown;

	self.emptyLabel.hidden = !bEmpty;
	if (bEmpty)
		self.emptyLabel.text = bAtRoot ? kEmptyRoot : kEmptyFolder;

	const BOOL bHasSelection  = self.selection.count > 0;
	self.shareButton.enabled  = bHasSelection;
	self.deleteButton.enabled = bHasSelection;
}

- (void)navigateTo:(NSURL*)Url
{
	// Sandbox: never allow a path outside the dump root.
	if (![Url.path hasPrefix:self.rootURL.path])
		return;

	self.currentURL = Url;
	[self reload];
	[self.tableView setContentOffset:CGPointZero animated:NO];
}

- (void)goBack
{
	if ([self.currentURL.path isEqualToString:self.rootURL.path])
		return;

	[self navigateTo:self.currentURL.URLByDeletingLastPathComponent];
}

#pragma mark - Selection

- (void)toggleSelectionForEntry:(KittyFileEntry*)Entry
{
	NSString* Key = SelectionKey(Entry.url);
	if (self.selection[Key])
		[self.selection removeObjectForKey:Key];
	else
		self.selection[Key] = Entry;

	// Resolved now rather than captured when the cell was configured: the listing can
	// be reloaded between the two, which would leave a captured index path pointing at
	// a different row, or past the end of a shorter listing.
	const NSUInteger Row = [self.entries indexOfObjectIdenticalTo:Entry];
	if (Row != NSNotFound)
	{
		[self.tableView reloadRowsAtIndexPaths:@[ [NSIndexPath indexPathForRow:(NSInteger)Row inSection:0] ]
		                      withRowAnimation:UITableViewRowAnimationNone];
	}

	[self refreshChrome];
}

- (NSArray<KittyFileEntry*>*)selectedEntries
{
	// Built from the selection itself, not from the visible folder, so entries picked
	// elsewhere are still shared and deleted. Sorted for a stable confirmation prompt.
	return [self.selection.allValues sortedArrayUsingComparator:^NSComparisonResult(KittyFileEntry* A, KittyFileEntry* B) {
	  return [A.url.path compare:B.url.path];
	}];
}

#pragma mark - UITableView

- (NSInteger)tableView:(UITableView*)TableView numberOfRowsInSection:(NSInteger)Section
{
	return (NSInteger)self.entries.count;
}

- (UITableViewCell*)tableView:(UITableView*)TableView cellForRowAtIndexPath:(NSIndexPath*)IndexPath
{
	KittyFileCell* Cell   = [TableView dequeueReusableCellWithIdentifier:kCellReuseId forIndexPath:IndexPath];
	KittyFileEntry* Entry = self.entries[(NSUInteger)IndexPath.row];

	[Cell applyEntry:Entry checked:self.selection[SelectionKey(Entry.url)] != nil];

	__weak KittyFileBrowser* WeakSelf = self;
	Cell.onToggleCheck                = ^{
      [WeakSelf toggleSelectionForEntry:Entry];
	};
	if (Entry.isDirectory)
	{
		Cell.onNavigate = ^{
		  [WeakSelf navigateTo:Entry.url];
		};
	}
	else
	{
		Cell.onNavigate = nil;
	}

	return Cell;
}

- (void)tableView:(UITableView*)TableView didSelectRowAtIndexPath:(NSIndexPath*)IndexPath
{
	[TableView deselectRowAtIndexPath:IndexPath animated:YES];

	KittyFileEntry* Entry = self.entries[(NSUInteger)IndexPath.row];

	// Selection is the primary action for every row, folders included - reaching for
	// the small checkbox just to pick a folder was the wrong default. Descending is
	// the chevron's job.
	[self toggleSelectionForEntry:Entry];
}

#pragma mark - Share

- (void)shareSelection
{
	NSArray<KittyFileEntry*>* Selected = [self selectedEntries];
	if (Selected.count == 0)
		return;

	BOOL bNeedsZip = NO;
	for (KittyFileEntry* Entry in Selected)
		bNeedsZip |= Entry.isDirectory;

	if (!bNeedsZip)
	{
		NSMutableArray<NSURL*>* Urls = [NSMutableArray array];
		for (KittyFileEntry* Entry in Selected)
			[Urls addObject:Entry.url];

		[self presentShareForURLs:Urls temporary:@[]];
		return;
	}

	// A directory URL is refused or mangled by most share targets, so folders are
	// zipped first. That can be slow for a large dump, hence the background hop.
	// Zips land in a timestamped folder inside the browser root so the user can
	// see any leftover files if the cleanup after sharing is missed.
	NSString* RootPath       = self.rootURL.path;
	const NSInteger ZipLevel = self.zipLevel;
	KittyAlertView* Waiting  = Alert::showWaiting(kPreparingTitle, kPreparingBody);
	self.shareButton.enabled = NO;

	dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
	  NSMutableArray<NSURL*>* Urls      = [NSMutableArray array];
	  NSMutableArray<NSURL*>* Temporary = [NSMutableArray array];
	  NSMutableArray<NSString*>* Failed = [NSMutableArray array];

	  // Create a temp folder in the root path; fall back to NSTemporaryDirectory() if that fails.
	  NSString* TempName   = [NSString stringWithFormat:@"_kitty_share_%lld", (long long)[[NSDate date] timeIntervalSince1970]];
	  NSString* TempFolder = [RootPath stringByAppendingPathComponent:TempName];
	  BOOL bTempInRoot     = [NSFileManager.defaultManager createDirectoryAtPath:TempFolder
                                                 withIntermediateDirectories:NO
                                                                  attributes:nil
                                                                       error:nil];
	  NSString* ZipBase    = bTempInRoot ? TempFolder : NSTemporaryDirectory();
	  if (bTempInRoot)
		  [Temporary addObject:[NSURL fileURLWithPath:TempFolder]];

	  for (KittyFileEntry* Entry in Selected)
	  {
		  if (!Entry.isDirectory)
		  {
			  [Urls addObject:Entry.url];
			  continue;
		  }

		  // Named after the path relative to the root, not the bare folder name: a
		  // selection can now span folders, so two different "SDK" directories under
		  // different parents would otherwise produce the same zip path - the second
		  // overwriting the first, and both share entries pointing at one file.
		  NSString* Relative = Entry.url.path;
		  if ([Relative hasPrefix:RootPath])
			  Relative = [Relative substringFromIndex:RootPath.length];

		  Relative = [Relative stringByTrimmingCharactersInSet:[NSCharacterSet characterSetWithCharactersInString:@"/"]];

		  NSString* ZipName = [Relative stringByReplacingOccurrencesOfString:@"/" withString:@"_"];
		  if (ZipName.length == 0)
			  ZipName = Entry.name;

		  NSString* ZipPath = [ZipBase stringByAppendingPathComponent:[ZipName stringByAppendingPathExtension:kZipExtension]];

		  [NSFileManager.defaultManager removeItemAtPath:ZipPath error:nil];

		  const bool bOk = Utils::Zip::CreateZipWithDirectory(Entry.url.path.UTF8String, (int)ZipLevel, ZipPath.UTF8String);
		  if (bOk)
		  {
			  NSURL* ZipUrl = [NSURL fileURLWithPath:ZipPath];
			  [Urls addObject:ZipUrl];
			  if (!bTempInRoot)
				  [Temporary addObject:ZipUrl]; // fallback: track individual files
		  }
		  else
		  {
			  [Failed addObject:Entry.name];
		  }
	  }

	  UIUtils::DispatchSyncMain(^{
		Alert::dismiss(Waiting);
		self.shareButton.enabled = self.selection.count > 0;

		if (Urls.count == 0)
		{
			Alert::showError(kShareFailedTitle, kShareFailedBody, nil);
			return;
		}

		if (Failed.count > 0)
		{
			NSString* Msg = [NSString stringWithFormat:kPartialBodyFmt, [Failed componentsJoinedByString:kListSeparator]];
			Alert::showError(kPartialTitle, Msg, ^{
			  [self presentShareForURLs:Urls temporary:Temporary];
			});
			return;
		}

		[self presentShareForURLs:Urls temporary:Temporary];
	  });
	});
}

- (void)presentShareForURLs:(NSArray<NSURL*>*)Urls temporary:(NSArray<NSURL*>*)Temporary
{
	NSMutableArray<KittyShareItem*>* Items = [NSMutableArray arrayWithCapacity:Urls.count];
	for (NSURL* Url in Urls)
		[Items addObject:[[KittyShareItem alloc] initWithURL:Url]];

	UIActivityViewController* Activity =
	    [[UIActivityViewController alloc] initWithActivityItems:Items
	                                      applicationActivities:nil];

	Activity.completionWithItemsHandler = ^(UIActivityType, BOOL, NSArray*, NSError*) {
	  for (NSURL* Temp in Temporary)
		  [NSFileManager.defaultManager removeItemAtURL:Temp error:nil];
	};

	// Without an anchor this is a hard crash on iPad.
	UIPopoverPresentationController* Popover = Activity.popoverPresentationController;
	if (Popover)
	{
		Popover.sourceView               = self.shareButton;
		Popover.sourceRect               = self.shareButton.bounds;
		Popover.permittedArrowDirections = UIPopoverArrowDirectionAny;
	}

	dispatch_async(dispatch_get_main_queue(), ^{
	  [UIUtils::GetTopViewController() presentViewController:Activity animated:YES completion:nil];
	});
}

#pragma mark - Delete

- (void)deleteSelection
{
	NSArray<KittyFileEntry*>* Selected = [self selectedEntries];
	if (Selected.count == 0)
		return;

	NSString* What = Selected.count == 1
	                     ? [NSString stringWithFormat:kDeleteOneFmt, Selected.firstObject.name]
	                     : [NSString stringWithFormat:kDeleteManyFmt, (unsigned long)Selected.count];

	// A selection survives navigation, so this can destroy files that are not on
	// screen. Say how many rather than letting the count quietly cover them.
	NSString* Here       = self.currentURL.URLByStandardizingPath.path;
	NSUInteger Elsewhere = 0;
	for (KittyFileEntry* Entry in Selected)
	{
		if (![Entry.url.URLByDeletingLastPathComponent.URLByStandardizingPath.path isEqualToString:Here])
			Elsewhere++;
	}

	if (Elsewhere > 0)
		What = [What stringByAppendingFormat:kDeleteElsewhereFmt, (unsigned long)Elsewhere];

	Alert::showNoOrYes(kDeleteTitle, [NSString stringWithFormat:kDeleteConfirmFmt, What], ^{}, ^{
	  NSMutableArray<NSString*>* Failed = [NSMutableArray array];

	  for (KittyFileEntry* Entry in Selected)
	  {
		  if (![NSFileManager.defaultManager removeItemAtURL:Entry.url error:nil])
			  [Failed addObject:Entry.name];
	  }

	  [self.selection removeAllObjects];
	  [self reload];

	  // One alert for the batch rather than one per file.
	  if (Failed.count > 0)
	  {
		  NSString* Msg = [NSString stringWithFormat:kDeleteFailedFmt, [Failed componentsJoinedByString:kListSeparator]];
		  Alert::showError(kDeleteFailedTitle, Msg, nil);
	  }
	});
}

#pragma mark - Presentation

- (void)showInView:(UIView*)ParentView forPath:(NSString*)RootPath
{
	if (self.isVisible || !ParentView)
		return;

	if (RootPath.length == 0)
	{
		// fileURLWithPath: raises on an empty string, so refuse rather than crash.
		Alert::showError(kNoPathTitle, kNoPathBody, nil);
		return;
	}

	self.isVisible  = YES;
	self.rootURL    = [NSURL fileURLWithPath:RootPath isDirectory:YES];
	self.currentURL = self.rootURL;

	[self.selection removeAllObjects];

	self.frame            = ParentView.bounds;
	self.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
	[ParentView addSubview:self];

	[self reload];

	self.backdrop.alpha = 0.0;
	self.card.alpha     = 0.0;
	self.card.transform = CGAffineTransformMakeScale(Motion::ShowFromScale, Motion::ShowFromScale);

	[UIView animateWithDuration:Motion::ShowDuration
	                      delay:0.0
	     usingSpringWithDamping:Motion::ShowSpringDamping
	      initialSpringVelocity:Motion::ShowSpringVelocity
	                    options:UIViewAnimationOptionCurveEaseOut
	                 animations:^{
		               self.backdrop.alpha = 1.0;
		               self.card.alpha     = 1.0;
		               self.card.transform = CGAffineTransformIdentity;
	                 }
	                 completion:nil];
}

- (void)hide
{
	if (!self.isVisible)
		return;

	self.isVisible = NO;

	[UIView animateWithDuration:Motion::HideDuration
	    animations:^{
		  self.backdrop.alpha = 0.0;
		  self.card.alpha     = 0.0;
		  self.card.transform = CGAffineTransformMakeScale(Motion::HideToScale, Motion::HideToScale);
	    }
	    completion:^(BOOL) {
		  self.card.transform = CGAffineTransformIdentity;
		  [self removeFromSuperview];
	    }];
}

@end
