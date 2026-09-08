#import "DraggableButton.h"

#define kDragPressScale      0.92f
#define kDragPressDuration   0.1
#define kDragReleaseDuration 0.4
#define kDragSpringDamping   0.5
#define kDragSpringVelocity  1.5
#define kDragThreshold       5.0

@interface DraggableButton ()
{
	CGPoint _initialCenter;
	BOOL _didDrag;
	BOOL _suppressNextTap;
}
@property(nonatomic, strong) UIPanGestureRecognizer* panGesture;
@property(nonatomic, strong) CALayer* pulseRing;
@end

@implementation DraggableButton

- (instancetype)initWithFrame:(CGRect)frame
{
	self = [super initWithFrame:frame];
	if (self)
	{
		[self initializeDraggableBehavior];
	}
	return self;
}

- (instancetype)initWithCoder:(NSCoder*)coder
{
	self = [super initWithCoder:coder];
	if (self)
	{
		[self initializeDraggableBehavior];
	}
	return self;
}

- (void)initializeDraggableBehavior
{
	self.clampToSuperview = YES;
	self.snapToEdge       = YES;

	[self addTarget:self action:@selector(buttonTapped) forControlEvents:UIControlEventTouchUpInside];
	[self addTarget:self action:@selector(animatePress) forControlEvents:UIControlEventTouchDown | UIControlEventTouchDragEnter];
	[self addTarget:self action:@selector(animateRelease) forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside | UIControlEventTouchCancel | UIControlEventTouchDragExit];

	self.panGesture = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(handlePan:)];
	// cancelsTouchesInView = NO allows UIControlEvent touches to coexist with the pan, so press
	// animations play correctly at the start of a drag before UIGestureRecognizerStateBegan fires.
	self.panGesture.cancelsTouchesInView = NO;
	[self addGestureRecognizer:self.panGesture];
}

#pragma mark - Click Animations

- (void)animatePress
{
	[UIView animateWithDuration:kDragPressDuration
	                      delay:0
	                    options:UIViewAnimationOptionBeginFromCurrentState
	                 animations:^{
		               self.transform = CGAffineTransformMakeScale(kDragPressScale, kDragPressScale);
	                 }
	                 completion:nil];
}

- (void)animateRelease
{
	[UIView animateWithDuration:kDragReleaseDuration
	                      delay:0
	     usingSpringWithDamping:kDragSpringDamping
	      initialSpringVelocity:kDragSpringVelocity
	                    options:UIViewAnimationOptionBeginFromCurrentState | UIViewAnimationOptionAllowUserInteraction
	                 animations:^{
		               self.transform = CGAffineTransformIdentity;
	                 }
	                 completion:nil];
}

#pragma mark - Gesture Handling

- (void)handlePan:(UIPanGestureRecognizer*)gesture
{
	UIView* superview = self.superview;
	if (!superview)
		return;

	CGPoint translation = [gesture translationInView:superview];

	if (gesture.state == UIGestureRecognizerStateBegan)
	{
		_initialCenter = self.center;
		_didDrag       = NO;
	}
	else if (gesture.state == UIGestureRecognizerStateChanged)
	{
		if (fabs(translation.x) > kDragThreshold || fabs(translation.y) > kDragThreshold)
			_didDrag = YES;

		CGPoint newCenter = CGPointMake(_initialCenter.x + translation.x, _initialCenter.y + translation.y);

		if (self.clampToSuperview)
		{
			CGFloat halfWidth  = self.bounds.size.width / 2.0;
			CGFloat halfHeight = self.bounds.size.height / 2.0;
			newCenter.x        = fmax(halfWidth, fmin(superview.bounds.size.width - halfWidth, newCenter.x));
			newCenter.y        = fmax(halfHeight, fmin(superview.bounds.size.height - halfHeight, newCenter.y));
		}

		self.center = newCenter;
	}
	else if (gesture.state == UIGestureRecognizerStateEnded || gesture.state == UIGestureRecognizerStateCancelled)
	{
		BOOL WasDrag     = _didDrag;
		_didDrag         = NO;
		_suppressNextTap = WasDrag;

		[self animateRelease];

		if (self.snapToEdge && self.clampToSuperview && superview)
		{
			CGFloat halfWidth    = self.bounds.size.width / 2.0;
			CGFloat targetX      = self.center.x < superview.bounds.size.width / 2.0
			                           ? halfWidth
			                           : superview.bounds.size.width - halfWidth;
			CGFloat targetY      = self.center.y;
			CGPoint targetCenter = CGPointMake(targetX, targetY);

			[UIView animateWithDuration:kDragReleaseDuration
			    delay:0
			    usingSpringWithDamping:kDragSpringDamping
			    initialSpringVelocity:kDragSpringVelocity
			    options:UIViewAnimationOptionAllowUserInteraction
			    animations:^{
				  self.center = targetCenter;
			    }
			    completion:^(BOOL) {
				  if (WasDrag && self.onDragEnd)
					  self.onDragEnd(self.center);
			    }];
		}
		else if (WasDrag && self.onDragEnd)
		{
			self.onDragEnd(self.center);
		}
	}
}

#pragma mark - Pulse

- (void)layoutSubviews
{
	[super layoutSubviews];
	// The ring was sized once in the setter and never followed the button afterwards.
	self.pulseRing.frame        = self.bounds;
	self.pulseRing.cornerRadius = self.layer.cornerRadius;
}

- (void)setEnablePulse:(BOOL)enablePulse
{
	_enablePulse = enablePulse;

	// Both directions matter: NO used to leave the ring animating forever, and a
	// second YES used to stack another ring and another infinite animation on top.
	if (self.pulseRing)
	{
		[self.pulseRing removeAnimationForKey:@"Pulse"];
		[self.pulseRing removeFromSuperlayer];
		self.pulseRing = nil;
	}

	if (!enablePulse)
		return;

	CALayer* PulseRing        = [CALayer layer];
	PulseRing.frame           = self.bounds;
	PulseRing.cornerRadius    = self.layer.cornerRadius;
	PulseRing.backgroundColor = self.backgroundColor.CGColor;
	PulseRing.opacity         = 0;
	[self.layer insertSublayer:PulseRing atIndex:0];

	CABasicAnimation* Scale = [CABasicAnimation animationWithKeyPath:@"transform.scale"];
	Scale.fromValue         = @1.0;
	Scale.toValue           = @1.65;

	CABasicAnimation* Opacity = [CABasicAnimation animationWithKeyPath:@"opacity"];
	Opacity.fromValue         = @0.55f;
	Opacity.toValue           = @0.0f;

	CAAnimationGroup* Group = [CAAnimationGroup animation];
	Group.animations        = @[ Scale, Opacity ];
	Group.duration          = 1.8;
	Group.repeatCount       = HUGE_VALF;
	Group.timingFunction    = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseOut];
	[PulseRing addAnimation:Group forKey:@"Pulse"];
	self.pulseRing = PulseRing;
}

#pragma mark - Actions

- (void)buttonTapped
{
	if (_suppressNextTap)
	{
		_suppressNextTap = NO;
		return;
	}
	if (self.onClick)
		self.onClick(self);
}

@end
