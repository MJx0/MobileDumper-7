#import "KittyConsoleOverlay.h"

@interface ConsoleLogEntry : NSObject
@property(nonatomic, strong) NSString* message;
@property(nonatomic, strong) NSDate* timestamp;
@property(nonatomic, assign) ConsoleLogLevel level;
@end

@implementation ConsoleLogEntry
@end

@interface ConsoleLogCell : UITableViewCell
@property(nonatomic, strong) UILabel* badgeLabel;
@property(nonatomic, strong) UILabel* timestampLabel;
@property(nonatomic, strong) UILabel* messageLabel;
@end

@implementation ConsoleLogCell

- (instancetype)initWithStyle:(UITableViewCellStyle)style reuseIdentifier:(NSString*)reuseIdentifier
{
	self = [super initWithStyle:style reuseIdentifier:reuseIdentifier];
	if (self)
	{
		self.backgroundColor = [UIColor clearColor];
		self.selectionStyle  = UITableViewCellSelectionStyleNone;

		_badgeLabel                                           = [[UILabel alloc] init];
		_badgeLabel.translatesAutoresizingMaskIntoConstraints = NO;
		_badgeLabel.font                                      = [UIFont systemFontOfSize:9 weight:UIFontWeightBold];
		_badgeLabel.textColor                                 = [UIColor whiteColor];
		_badgeLabel.textAlignment                             = NSTextAlignmentCenter;
		_badgeLabel.layer.cornerRadius                        = 8;
		_badgeLabel.clipsToBounds                             = YES;
		[self.contentView addSubview:_badgeLabel];

		_timestampLabel                                           = [[UILabel alloc] init];
		_timestampLabel.translatesAutoresizingMaskIntoConstraints = NO;
		_timestampLabel.font                                      = [UIFont fontWithName:@"Menlo" size:10];
		_timestampLabel.textColor                                 = [UIColor colorWithWhite:0.5 alpha:1.0];
		[self.contentView addSubview:_timestampLabel];

		_messageLabel                                           = [[UILabel alloc] init];
		_messageLabel.translatesAutoresizingMaskIntoConstraints = NO;
		_messageLabel.font                                      = [UIFont fontWithName:@"Menlo" size:11.5];
		_messageLabel.textColor                                 = [UIColor whiteColor];
		_messageLabel.numberOfLines                             = 0;
		[self.contentView addSubview:_messageLabel];

		[NSLayoutConstraint activateConstraints:@[
			[_badgeLabel.leadingAnchor constraintEqualToAnchor:self.contentView.leadingAnchor
			                                          constant:12],
			[_badgeLabel.topAnchor constraintEqualToAnchor:self.contentView.topAnchor
			                                      constant:8],
			[_badgeLabel.widthAnchor constraintEqualToConstant:50],
			[_badgeLabel.heightAnchor constraintEqualToConstant:16],

			[_timestampLabel.leadingAnchor constraintEqualToAnchor:_badgeLabel.trailingAnchor
			                                              constant:8],
			[_timestampLabel.centerYAnchor constraintEqualToAnchor:_badgeLabel.centerYAnchor],
			[_timestampLabel.trailingAnchor constraintEqualToAnchor:self.contentView.trailingAnchor
			                                               constant:-12],

			[_messageLabel.leadingAnchor constraintEqualToAnchor:self.contentView.leadingAnchor
			                                            constant:12],
			[_messageLabel.topAnchor constraintEqualToAnchor:_badgeLabel.bottomAnchor
			                                        constant:6],
			[_messageLabel.trailingAnchor constraintEqualToAnchor:self.contentView.trailingAnchor
			                                             constant:-12],
			[_messageLabel.bottomAnchor constraintEqualToAnchor:self.contentView.bottomAnchor
			                                           constant:-8]
		]];
	}
	return self;
}

@end

#pragma mark - Main Console Implementation

@interface KittyConsoleOverlay () <UITableViewDelegate, UITableViewDataSource>

@property(nonatomic, strong) UIVisualEffectView* blurContainer;
@property(nonatomic, strong) UILabel* logCountLabel;
@property(nonatomic, strong) UITableView* tableView;
@property(nonatomic, strong) NSMutableArray<ConsoleLogEntry*>* logs;
@property(nonatomic, strong) NSDateFormatter* dateFormatter;
@property(nonatomic, weak) UIButton* transcriptButton;
@property(nonatomic, assign) BOOL isVisible;
// Set while hidden: entries land in self.logs but the table is not touched,
// so it must be reloaded wholesale the next time the console is shown.
@property(nonatomic, assign) BOOL needsReload;

@property(nonatomic, strong) NSMutableArray<ConsoleLogEntry*>* pendingLogs;
@property(nonatomic, strong) dispatch_queue_t queue;
@property(nonatomic, assign) BOOL isThrottling;

@end

@implementation KittyConsoleOverlay

+ (instancetype)sharedConsole
{
	static KittyConsoleOverlay* sharedInstance = nil;
	static dispatch_once_t onceToken;
	dispatch_once(&onceToken, ^{
	  CGRect screenBounds = [UIScreen mainScreen].bounds;
	  CGRect initialFrame = CGRectMake(0, -screenBounds.size.height, screenBounds.size.width, screenBounds.size.height * 0.65);
	  sharedInstance      = [[self alloc] initWithFrame:initialFrame];
	});
	return sharedInstance;
}

- (instancetype)initWithFrame:(CGRect)frame
{
	self = [super initWithFrame:frame];
	if (self)
	{
		_logs         = [NSMutableArray array];
		_pendingLogs  = [NSMutableArray array];
		_isVisible    = NO;
		_isThrottling = NO;
		_queue        = dispatch_queue_create("com.console.throttler", DISPATCH_QUEUE_SERIAL);

		_dateFormatter = [[NSDateFormatter alloc] init];
		[_dateFormatter setDateFormat:@"HH:mm:ss.SSS"];

		[self setupUI];
		[self setupGestures];
	}
	return self;
}

- (void)setupUI
{
	self.layer.shadowColor   = [UIColor blackColor].CGColor;
	self.layer.shadowOpacity = 0.5;
	self.layer.shadowRadius  = 20.0;
	self.layer.shadowOffset  = CGSizeMake(0, 10);

	UIBlurEffect* blurEffect                                     = [UIBlurEffect effectWithStyle:UIBlurEffectStyleDark];
	self.blurContainer                                           = [[UIVisualEffectView alloc] initWithEffect:blurEffect];
	self.blurContainer.translatesAutoresizingMaskIntoConstraints = NO;
	self.blurContainer.layer.cornerRadius                        = 22.0;
	self.blurContainer.layer.maskedCorners                       = kCALayerMinXMaxYCorner | kCALayerMaxXMaxYCorner;
	self.blurContainer.clipsToBounds                             = YES;
	[self addSubview:self.blurContainer];

	UIView* headerView                                   = [[UIView alloc] init];
	headerView.translatesAutoresizingMaskIntoConstraints = NO;
	[self.blurContainer.contentView addSubview:headerView];

	UILabel* titleLabel                                  = [[UILabel alloc] init];
	titleLabel.translatesAutoresizingMaskIntoConstraints = NO;
	titleLabel.text                                      = @"Console";
	titleLabel.textColor                                 = [UIColor colorWithWhite:0.95 alpha:1.0];
	titleLabel.font                                      = [UIFont systemFontOfSize:13 weight:UIFontWeightBlack];
	[headerView addSubview:titleLabel];

	_logCountLabel                                           = [[UILabel alloc] init];
	_logCountLabel.translatesAutoresizingMaskIntoConstraints = NO;
	_logCountLabel.font                                      = [UIFont systemFontOfSize:10 weight:UIFontWeightMedium];
	_logCountLabel.textColor                                 = [UIColor colorWithWhite:0.55 alpha:1.0];
	_logCountLabel.text                                      = @"0 entries";
	[headerView addSubview:_logCountLabel];

	UIButton* transcriptButton                                 = [UIButton buttonWithType:UIButtonTypeSystem];
	transcriptButton.translatesAutoresizingMaskIntoConstraints = NO;
	[transcriptButton setTitle:@"Copy" forState:UIControlStateNormal];
	[transcriptButton setTitleColor:[UIColor systemBlueColor] forState:UIControlStateNormal];
	transcriptButton.titleLabel.font = [UIFont systemFontOfSize:14 weight:UIFontWeightBold];
	[transcriptButton addTarget:self action:@selector(copyLogs) forControlEvents:UIControlEventTouchUpInside];
	[headerView addSubview:transcriptButton];
	_transcriptButton = transcriptButton;

	UIButton* clearButton                                 = [UIButton buttonWithType:UIButtonTypeSystem];
	clearButton.translatesAutoresizingMaskIntoConstraints = NO;
	[clearButton setTitle:@"Clear" forState:UIControlStateNormal];
	[clearButton setTitleColor:[UIColor systemRedColor] forState:UIControlStateNormal];
	clearButton.titleLabel.font = [UIFont systemFontOfSize:14 weight:UIFontWeightBold];
	[clearButton addTarget:self action:@selector(clearLogs) forControlEvents:UIControlEventTouchUpInside];
	[headerView addSubview:clearButton];

	UIButton* closeButton                                 = [UIButton buttonWithType:UIButtonTypeSystem];
	closeButton.translatesAutoresizingMaskIntoConstraints = NO;
	[closeButton setTitle:@"Hide" forState:UIControlStateNormal];
	[closeButton setTitleColor:[UIColor systemBlueColor] forState:UIControlStateNormal];
	closeButton.titleLabel.font = [UIFont systemFontOfSize:14 weight:UIFontWeightBold];
	[closeButton addTarget:self action:@selector(hide) forControlEvents:UIControlEventTouchUpInside];
	[headerView addSubview:closeButton];

	UIView* separator                                   = [[UIView alloc] init];
	separator.translatesAutoresizingMaskIntoConstraints = NO;
	separator.backgroundColor                           = [UIColor colorWithWhite:1.0 alpha:0.10];
	[self.blurContainer.contentView addSubview:separator];

	self.tableView                                           = [[UITableView alloc] initWithFrame:CGRectZero style:UITableViewStylePlain];
	self.tableView.translatesAutoresizingMaskIntoConstraints = NO;
	self.tableView.backgroundColor                           = [UIColor clearColor];
	self.tableView.separatorStyle                            = UITableViewCellSeparatorStyleSingleLine;
	self.tableView.separatorColor                            = [UIColor colorWithWhite:1.0 alpha:0.07];
	self.tableView.separatorInset                            = UIEdgeInsetsMake(0, 74, 0, 0);
	self.tableView.delegate                                  = self;
	self.tableView.dataSource                                = self;
	self.tableView.indicatorStyle                            = UIScrollViewIndicatorStyleWhite;
	self.tableView.estimatedRowHeight                        = 60.0;
	self.tableView.rowHeight                                 = UITableViewAutomaticDimension;
	self.tableView.alwaysBounceVertical                      = YES;
	[self.tableView registerClass:[ConsoleLogCell class] forCellReuseIdentifier:@"ConsoleCell"];
	[self.blurContainer.contentView addSubview:self.tableView];

	[NSLayoutConstraint activateConstraints:@[
		[self.blurContainer.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
		[self.blurContainer.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
		[self.blurContainer.topAnchor constraintEqualToAnchor:self.topAnchor],
		[self.blurContainer.bottomAnchor constraintEqualToAnchor:self.bottomAnchor],

		[headerView.topAnchor constraintEqualToAnchor:self.safeAreaLayoutGuide.topAnchor],
		[headerView.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
		[headerView.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
		[headerView.heightAnchor constraintEqualToConstant:44],

		[titleLabel.leadingAnchor constraintEqualToAnchor:headerView.leadingAnchor
		                                         constant:16],
		[titleLabel.centerYAnchor constraintEqualToAnchor:headerView.centerYAnchor],

		[_logCountLabel.leadingAnchor constraintEqualToAnchor:titleLabel.trailingAnchor
		                                             constant:8],
		[_logCountLabel.centerYAnchor constraintEqualToAnchor:headerView.centerYAnchor],

		[closeButton.trailingAnchor constraintEqualToAnchor:headerView.trailingAnchor
		                                           constant:-16],
		[closeButton.centerYAnchor constraintEqualToAnchor:headerView.centerYAnchor],

		[clearButton.trailingAnchor constraintEqualToAnchor:closeButton.leadingAnchor
		                                           constant:-20],
		[clearButton.centerYAnchor constraintEqualToAnchor:headerView.centerYAnchor],

		[transcriptButton.trailingAnchor constraintEqualToAnchor:clearButton.leadingAnchor
		                                                constant:-20],
		[transcriptButton.centerYAnchor constraintEqualToAnchor:headerView.centerYAnchor],

		[separator.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
		[separator.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
		[separator.topAnchor constraintEqualToAnchor:headerView.bottomAnchor],
		[separator.heightAnchor constraintEqualToConstant:0.5],

		[self.tableView.topAnchor constraintEqualToAnchor:separator.bottomAnchor],
		[self.tableView.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
		[self.tableView.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
		[self.tableView.bottomAnchor constraintEqualToAnchor:self.bottomAnchor]
	]];
}

- (void)setupGestures
{
	UIPanGestureRecognizer* pan = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(handlePan:)];
	[self addGestureRecognizer:pan];
}

- (void)layoutSubviews
{
	[super layoutSubviews];
	UIBezierPath* Path    = [UIBezierPath bezierPathWithRoundedRect:self.bounds
	                                              byRoundingCorners:UIRectCornerBottomLeft | UIRectCornerBottomRight
	                                                    cornerRadii:CGSizeMake(22, 22)];
	self.layer.shadowPath = Path.CGPath;
}

#pragma mark - Log Batching

- (void)addLog:(NSString*)message level:(ConsoleLogLevel)level
{
	dispatch_async(self.queue, ^{
	  ConsoleLogEntry* entry = [[ConsoleLogEntry alloc] init];
	  entry.message          = message;
	  entry.timestamp        = [NSDate date];
	  entry.level            = level;

	  [self.pendingLogs addObject:entry];

	  if (!self.isThrottling)
	  {
		  self.isThrottling = YES;
		  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.1 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
			[self flushLogs];
		  });
	  }
	});
}

- (void)flushLogs
{
	dispatch_async(self.queue, ^{
	  NSArray* logsToAppend = [self.pendingLogs copy];
	  [self.pendingLogs removeAllObjects];
	  self.isThrottling = NO;

	  dispatch_async(dispatch_get_main_queue(), ^{
		NSInteger OldCount  = (NSInteger)self.logs.count;
		NSInteger BatchSize = (NSInteger)logsToAppend.count;
		[self.logs addObjectsFromArray:logsToAppend];

		NSInteger Removed = 0;
		if ((NSInteger)self.logs.count > 1000)
		{
			Removed = (NSInteger)self.logs.count - 1000;
			[self.logs removeObjectsInRange:NSMakeRange(0, (NSUInteger)Removed)];
		}
		NSInteger NewCount = (NSInteger)self.logs.count;

		// While hidden the backlog is still kept and capped, but touching the table
		// (and animating a scroll) every 100ms costs as much as it does on screen.
		if (!self.isVisible)
		{
			self.needsReload = YES;
			return;
		}

		if (BatchSize > 1000 || Removed > OldCount)
		{
			[self.tableView reloadData];
		}
		else
		{
			[self.tableView beginUpdates];
			if (Removed > 0)
			{
				NSMutableArray* Del = [NSMutableArray arrayWithCapacity:(NSUInteger)Removed];
				for (NSInteger i = 0; i < Removed; i++)
					[Del addObject:[NSIndexPath indexPathForRow:i inSection:0]];
				[self.tableView deleteRowsAtIndexPaths:Del withRowAnimation:UITableViewRowAnimationNone];
			}
			NSMutableArray* Ins = [NSMutableArray arrayWithCapacity:(NSUInteger)BatchSize];
			for (NSInteger i = NewCount - BatchSize; i < NewCount; i++)
				[Ins addObject:[NSIndexPath indexPathForRow:i inSection:0]];
			[self.tableView insertRowsAtIndexPaths:Ins withRowAnimation:UITableViewRowAnimationFade];
			[self.tableView endUpdates];
		}

		self.logCountLabel.text = [NSString stringWithFormat:@"%lu entries", (unsigned long)NewCount];
		[self scrollToBottomAnimated:YES];
	  });
	});
}

static NSString* ConsoleLevelName(ConsoleLogLevel Level)
{
	switch (Level)
	{
	case ConsoleLogLevelDebug:
		return @"DEBUG";
	case ConsoleLogLevelInfo:
		return @"INFO";
	case ConsoleLogLevelWarning:
		return @"WARN";
	case ConsoleLogLevelError:
		return @"ERROR";
	}
	return @"INFO";
}

- (void)copyLogs
{
	// self.logs is only ever mutated on the main queue and this runs from a button,
	// so the snapshot needs no further synchronisation.
	if (self.logs.count == 0)
		return;

	NSMutableString* Text = [NSMutableString stringWithCapacity:self.logs.count * 96];
	for (ConsoleLogEntry* Entry in self.logs)
	{
		// Padded explicitly rather than with a %-5@ width flag, which is not dependable
		// for %@ conversions, so the levels stay aligned in the pasted text.
		NSString* Level = [ConsoleLevelName(Entry.level) stringByPaddingToLength:5
		                                                              withString:@" "
		                                                         startingAtIndex:0];

		[Text appendFormat:@"%@  %@  %@\n",
		                   [self.dateFormatter stringFromDate:Entry.timestamp],
		                   Level,
		                   Entry.message];
	}

	UIPasteboard.generalPasteboard.string = Text;

	UIImpactFeedbackGenerator* Haptic = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
	[Haptic impactOccurred];

	// Confirm in place: the console covers the screen, so an alert would sit on top
	// of the very thing the user is copying.
	UIButton* Button = self.transcriptButton;
	if (!Button)
		return;

	[Button setTitle:@"Copied" forState:UIControlStateNormal];
	Button.enabled = NO;

	__weak KittyConsoleOverlay* WeakSelf = self;
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.2 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
	  UIButton* Later = WeakSelf.transcriptButton;
	  [Later setTitle:@"Copy" forState:UIControlStateNormal];
	  Later.enabled = YES;
	});
}

- (void)clearLogs
{
	dispatch_async(self.queue, ^{
	  [self.pendingLogs removeAllObjects];

	  dispatch_async(dispatch_get_main_queue(), ^{
		[self.logs removeAllObjects];
		[UIView transitionWithView:self.tableView
			              duration:0.2
			               options:UIViewAnimationOptionTransitionCrossDissolve
			            animations:^{ [self.tableView reloadData]; }
			            completion:nil];
		self.logCountLabel.text = @"0 entries";
	  });
	});
}

#pragma mark - Animations

- (void)showInView:(UIView*)ParentView
{
	if (self.isVisible)
		return;
	self.isVisible = YES;

	if (self.needsReload)
	{
		self.needsReload = NO;
		[self.tableView reloadData];
		self.logCountLabel.text = [NSString stringWithFormat:@"%lu entries", (unsigned long)self.logs.count];
	}

	if (!self.superview && ParentView)
		[ParentView addSubview:self];

	CGRect screenBounds = [UIScreen mainScreen].bounds;
	[UIView animateWithDuration:0.5
	    delay:0.0
	    usingSpringWithDamping:0.8
	    initialSpringVelocity:0.3
	    options:UIViewAnimationOptionCurveEaseInOut
	    animations:^{
		  self.frame = CGRectMake(0, 0, screenBounds.size.width, screenBounds.size.height * 0.65);
	    }
	    completion:^(BOOL) {
		  [self scrollToBottomAnimated:NO];
	    }];
}

- (void)hide
{
	if (!self.isVisible)
		return;
	self.isVisible = NO;

	CGRect screenBounds = [UIScreen mainScreen].bounds;
	[UIView animateWithDuration:0.3
	    delay:0.0
	    options:UIViewAnimationOptionCurveEaseInOut
	    animations:^{
		  self.frame = CGRectMake(0, -self.frame.size.height, screenBounds.size.width, self.frame.size.height);
	    }
	    completion:^(BOOL) {
		  [self removeFromSuperview];
	    }];
}

- (void)handlePan:(UIPanGestureRecognizer*)gesture
{
	CGPoint translation   = [gesture translationInView:self];
	CGRect screenBounds   = [UIScreen mainScreen].bounds;
	CGFloat consoleHeight = screenBounds.size.height * 0.65;

	if (gesture.state == UIGestureRecognizerStateChanged)
	{
		if (translation.y < 0)
			self.frame = CGRectMake(0, translation.y, self.frame.size.width, consoleHeight);
	}
	else if (gesture.state == UIGestureRecognizerStateEnded || gesture.state == UIGestureRecognizerStateCancelled)
	{
		CGPoint velocity = [gesture velocityInView:self];
		if (translation.y < -150 || velocity.y < -500)
		{
			[self hide];
		}
		else
		{
			[UIView animateWithDuration:0.3
			                 animations:^{
				               self.frame = CGRectMake(0, 0, self.frame.size.width, consoleHeight);
			                 }];
		}
	}
}

- (void)scrollToBottomAnimated:(BOOL)animated
{
	if (self.logs.count > 0)
	{
		NSIndexPath* indexPath = [NSIndexPath indexPathForRow:self.logs.count - 1 inSection:0];
		[self.tableView scrollToRowAtIndexPath:indexPath atScrollPosition:UITableViewScrollPositionBottom animated:animated];
	}
}

#pragma mark - UITableView DataSource

- (NSInteger)tableView:(UITableView*)tableView numberOfRowsInSection:(NSInteger)section
{
	return self.logs.count;
}

- (UITableViewCell*)tableView:(UITableView*)tableView cellForRowAtIndexPath:(NSIndexPath*)indexPath
{
	ConsoleLogCell* cell = [tableView dequeueReusableCellWithIdentifier:@"ConsoleCell" forIndexPath:indexPath];

	ConsoleLogEntry* entry = self.logs[indexPath.row];

	NSString* badgeText = @"DEBUG";
	UIColor* badgeBg    = [UIColor colorWithWhite:1.0 alpha:0.15];
	UIColor* textColor  = [UIColor colorWithWhite:0.9 alpha:1.0];

	switch (entry.level)
	{
	case ConsoleLogLevelError:
		badgeText = @"ERROR";
		badgeBg   = [UIColor colorWithRed:0.92 green:0.26 blue:0.21 alpha:1.0];
		textColor = [UIColor colorWithRed:1.0 green:0.45 blue:0.45 alpha:1.0];
		break;
	case ConsoleLogLevelWarning:
		badgeText = @"WARNING";
		badgeBg   = [UIColor colorWithRed:1.0 green:0.60 blue:0.0 alpha:1.0];
		textColor = [UIColor colorWithRed:1.0 green:0.85 blue:0.4 alpha:1.0];
		break;
	case ConsoleLogLevelInfo:
		badgeText = @"INFO";
		badgeBg   = [UIColor colorWithRed:0.18 green:0.49 blue:0.96 alpha:1.0];
		textColor = [UIColor colorWithRed:0.65 green:0.85 blue:1.0 alpha:1.0];
		break;
	case ConsoleLogLevelDebug:
		badgeText = @"DEBUG";
		badgeBg   = [UIColor colorWithWhite:1.0 alpha:0.12];
		textColor = [UIColor colorWithWhite:0.85 alpha:1.0];
		break;
	}

	cell.badgeLabel.text            = badgeText;
	cell.badgeLabel.backgroundColor = badgeBg;
	cell.timestampLabel.text        = [self.dateFormatter stringFromDate:entry.timestamp];
	cell.messageLabel.text          = entry.message;
	cell.messageLabel.textColor     = textColor;

	return cell;
}

@end
