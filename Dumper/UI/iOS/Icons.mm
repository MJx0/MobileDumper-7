#import "Icons.h"

UIImage* Icons::Paw()
{
	static UIImage* CachedImage = nil;
	static dispatch_once_t Token;
	dispatch_once(&Token, ^{
	  // SF Symbols (iOS 13+): pawprint.fill renders a crisp, professional paw icon.
	  if (@available(iOS 13.0, *))
	  {
		  UIImageSymbolConfiguration* Cfg =
			  [UIImageSymbolConfiguration configurationWithPointSize:22
			                                                  weight:UIImageSymbolWeightMedium];
		  UIImage* Sym = [[UIImage systemImageNamed:@"pawprint.fill" withConfiguration:Cfg]
			  imageWithTintColor:UIColor.whiteColor
			       renderingMode:UIImageRenderingModeAlwaysOriginal];
		  if (Sym)
		  {
			  CachedImage = Sym;
			  return;
		  }
	  }

	  // Fallback: hand-drawn paw — four symmetric oval toes arc across the top,
	  // a rounded main pad with subtle inner toe-divider marks sits below.
	  CGFloat Size                      = 28.0;
	  UIGraphicsImageRenderer* Renderer = [[UIGraphicsImageRenderer alloc] initWithSize:CGSizeMake(Size, Size)];
	  CachedImage                       = [Renderer imageWithActions:^(UIGraphicsImageRendererContext*) {
		[UIColor.whiteColor setFill];

		// Four toes: evenly spaced across 10–90% of width, two rows of heights.
		//   outer toes sit slightly lower; inner toes are highest.
		struct
		{
			CGFloat Cx;
			CGFloat Cy;
			CGFloat Rx;
			CGFloat Ry;
		} Toes[4] = {
			{0.18f, 0.30f, 0.09f, 0.11f},
			{0.38f, 0.18f, 0.10f, 0.12f},
			{0.62f, 0.18f, 0.10f, 0.12f},
			{0.82f, 0.30f, 0.09f, 0.11f},
		};
		for (int I = 0; I < 4; I++)
		{
			CGFloat Cx = Toes[I].Cx * Size, Cy = Toes[I].Cy * Size;
			CGFloat Rx = Toes[I].Rx * Size, Ry = Toes[I].Ry * Size;
			[[UIBezierPath bezierPathWithOvalInRect:CGRectMake(Cx - Rx, Cy - Ry, Rx * 2, Ry * 2)] fill];
		}

		// Main pad: wide oval in the lower half.
		CGFloat PadCx = 0.50f * Size, PadCy = 0.68f * Size;
		CGFloat PadRx = 0.30f * Size, PadRy = 0.22f * Size;
		UIBezierPath* Pad = [UIBezierPath bezierPathWithOvalInRect:
			                                  CGRectMake(PadCx - PadRx, PadCy - PadRy, PadRx * 2, PadRy * 2)];
		[Pad fill];

		// Three subtle inner-toe marks drawn as small filled ellipses at 40% opacity.
		[[UIColor colorWithWhite:0.0 alpha:0.4] setFill];
		struct
		{
			CGFloat Cx;
			CGFloat Cy;
		} InnerToes[3] = {
			{0.35f, 0.64f},
			{0.50f, 0.58f},
			{0.65f, 0.64f},
		};
		for (int I = 0; I < 3; I++)
		{
			CGFloat Cx = InnerToes[I].Cx * Size, Cy = InnerToes[I].Cy * Size;
			[[UIBezierPath bezierPathWithOvalInRect:CGRectMake(Cx - 0.05f * Size,
				                                               Cy - 0.06f * Size,
				                                               0.10f * Size,
				                                               0.12f * Size)] fill];
		}
	  }];
	});
	return CachedImage;
}

UIImage* Icons::Bug()
{
	static UIImage* CachedImage = nil;
	static dispatch_once_t Token;
	dispatch_once(&Token, ^{
	  // SF Symbols (iOS 13+): ant.fill is a clean, recognisable insect icon.
	  if (@available(iOS 13.0, *))
	  {
		  UIImageSymbolConfiguration* Cfg =
			  [UIImageSymbolConfiguration configurationWithPointSize:22
			                                                  weight:UIImageSymbolWeightMedium];
		  UIImage* Sym = [[UIImage systemImageNamed:@"ant.fill" withConfiguration:Cfg]
			  imageWithTintColor:UIColor.whiteColor
			       renderingMode:UIImageRenderingModeAlwaysOriginal];
		  if (Sym)
		  {
			  CachedImage = Sym;
			  return;
		  }
	  }

	  // Fallback: hand-drawn bug with three body segments, curved antennae,
	  // three pairs of bent legs, and a small highlight on the thorax.
	  CGFloat Size                      = 28.0;
	  UIGraphicsImageRenderer* Renderer = [[UIGraphicsImageRenderer alloc] initWithSize:CGSizeMake(Size, Size)];
	  CachedImage                       = [Renderer imageWithActions:^(UIGraphicsImageRendererContext* Ctx) {
		CGContextRef Cg = Ctx.CGContext;
		CGContextSetLineCap(Cg, kCGLineCapRound);
		CGContextSetLineJoin(Cg, kCGLineJoinRound);
		[UIColor.whiteColor setFill];
		[UIColor.whiteColor setStroke];

		CGFloat LineW = Size * 0.07f;
		CGContextSetLineWidth(Cg, LineW);

		// ── Body: head (top) → thorax (mid) → abdomen (bottom) ────────────
		// Head
		CGFloat HeadR  = Size * 0.12f;
		CGFloat HeadCx = Size * 0.50f, HeadCy = Size * 0.20f;
		[[UIBezierPath bezierPathWithOvalInRect:
			               CGRectMake(HeadCx - HeadR, HeadCy - HeadR, HeadR * 2, HeadR * 2)] fill];

		// Thorax — small oval linking head to abdomen
		CGFloat ThRx = Size * 0.11f, ThRy = Size * 0.10f;
		CGFloat ThCx = HeadCx, ThCy = HeadCy + HeadR + ThRy * 0.6f;
		[[UIBezierPath bezierPathWithOvalInRect:
			               CGRectMake(ThCx - ThRx, ThCy - ThRy, ThRx * 2, ThRy * 2)] fill];

		// Abdomen — largest oval
		CGFloat AbRx = Size * 0.18f, AbRy = Size * 0.22f;
		CGFloat AbCx = HeadCx, AbCy = ThCy + ThRy + AbRy * 0.7f;
		[[UIBezierPath bezierPathWithOvalInRect:
			               CGRectMake(AbCx - AbRx, AbCy - AbRy, AbRx * 2, AbRy * 2)] fill];

		// Abdomen highlight — subtle oval at 25% opacity
		[[UIColor colorWithWhite:0.0 alpha:0.25] setFill];
		[[UIBezierPath bezierPathWithOvalInRect:
			               CGRectMake(AbCx - AbRx * 0.45f, AbCy - AbRy * 0.5f, AbRx * 0.9f, AbRy * 0.55f)] fill];
		[UIColor.whiteColor setFill];

		// ── Antennae: curved paths from head top-left / top-right ─────────
		UIBezierPath* AntL = [UIBezierPath bezierPath];
		[AntL moveToPoint:CGPointMake(HeadCx - HeadR * 0.6f, HeadCy - HeadR * 0.8f)];
		[AntL addCurveToPoint:CGPointMake(Size * 0.12f, Size * 0.03f)
			    controlPoint1:CGPointMake(Size * 0.28f, Size * 0.08f)
			    controlPoint2:CGPointMake(Size * 0.18f, Size * 0.04f)];
		[AntL setLineWidth:LineW];
		[AntL stroke];

		UIBezierPath* AntR = [UIBezierPath bezierPath];
		[AntR moveToPoint:CGPointMake(HeadCx + HeadR * 0.6f, HeadCy - HeadR * 0.8f)];
		[AntR addCurveToPoint:CGPointMake(Size * 0.88f, Size * 0.03f)
			    controlPoint1:CGPointMake(Size * 0.72f, Size * 0.08f)
			    controlPoint2:CGPointMake(Size * 0.82f, Size * 0.04f)];
		[AntR setLineWidth:LineW];
		[AntR stroke];

		// ── Three pairs of bent legs (at thorax and upper/lower abdomen) ───
		CGFloat LegAttachY[3] = {ThCy, AbCy - AbRy * 0.35f, AbCy + AbRy * 0.15f};
		CGFloat LegAttachDx   = ThRx;

		for (int I = 0; I < 3; I++)
		{
			CGFloat AttachY  = LegAttachY[I];
			CGFloat AttachDx = (I == 0) ? LegAttachDx : AbRx * 0.85f;
			CGFloat KneeOff  = Size * 0.13f;
			CGFloat TipOff   = Size * 0.11f;
			CGFloat KneeY    = AttachY + Size * 0.04f;

			// Left leg
			UIBezierPath* LegL = [UIBezierPath bezierPath];
			[LegL moveToPoint:CGPointMake(AbCx - AttachDx, AttachY)];
			[LegL addLineToPoint:CGPointMake(AbCx - AttachDx - KneeOff, KneeY)];
			[LegL addLineToPoint:CGPointMake(AbCx - AttachDx - KneeOff - TipOff, KneeY + Size * 0.07f)];
			[LegL setLineWidth:LineW];
			[LegL stroke];

			// Right leg
			UIBezierPath* LegR = [UIBezierPath bezierPath];
			[LegR moveToPoint:CGPointMake(AbCx + AttachDx, AttachY)];
			[LegR addLineToPoint:CGPointMake(AbCx + AttachDx + KneeOff, KneeY)];
			[LegR addLineToPoint:CGPointMake(AbCx + AttachDx + KneeOff + TipOff, KneeY + Size * 0.07f)];
			[LegR setLineWidth:LineW];
			[LegR stroke];
		}
	  }];
	});
	return CachedImage;
}


// ── File browser icons ────────────────────────────────────────────────────────

namespace
{
	// Shared builder for the browser's glyphs: SF Symbol when available, falling
	// back to a drawn shape so the UI never renders a blank cell on older systems.
	UIImage* MakeSymbol(NSString* SymbolName, CGFloat PointSize, void (^Fallback)(CGFloat Size))
	{
		if (@available(iOS 13.0, *))
		{
			UIImageSymbolConfiguration* Cfg =
			    [UIImageSymbolConfiguration configurationWithPointSize:PointSize
			                                                    weight:UIImageSymbolWeightMedium];

			UIImage* Sym = [[UIImage systemImageNamed:SymbolName withConfiguration:Cfg]
			    imageWithRenderingMode:UIImageRenderingModeAlwaysTemplate];

			if (Sym)
				return Sym;
		}

		if (!Fallback)
			return nil;

		const CGFloat Size                = PointSize + 6.0;
		UIGraphicsImageRenderer* Renderer = [[UIGraphicsImageRenderer alloc] initWithSize:CGSizeMake(Size, Size)];

		UIImage* Drawn = [Renderer imageWithActions:^(UIGraphicsImageRendererContext*) {
		  [UIColor.whiteColor setFill];
		  [UIColor.whiteColor setStroke];
		  Fallback(Size);
		}];

		return [Drawn imageWithRenderingMode:UIImageRenderingModeAlwaysTemplate];
	}
}

UIImage* Icons::Folder()
{
	static UIImage* CachedImage = nil;
	static dispatch_once_t Token;
	dispatch_once(&Token, ^{
	  CachedImage = MakeSymbol(@"folder.fill", 20.0, ^(CGFloat Size) {
		// Tab across the top-left, then the body below it.
		UIBezierPath* Tab = [UIBezierPath bezierPathWithRoundedRect:CGRectMake(0.08f * Size, 0.22f * Size, 0.38f * Size, 0.14f * Size)
			                                           cornerRadius:0.04f * Size];
		[Tab fill];

		UIBezierPath* Body = [UIBezierPath bezierPathWithRoundedRect:CGRectMake(0.08f * Size, 0.30f * Size, 0.84f * Size, 0.48f * Size)
			                                            cornerRadius:0.07f * Size];
		[Body fill];
	  });
	});
	return CachedImage;
}

UIImage* Icons::Archive()
{
	static UIImage* CachedImage = nil;
	static dispatch_once_t Token;
	dispatch_once(&Token, ^{
	  CachedImage = MakeSymbol(@"doc.zipper", 20.0, ^(CGFloat Size) {
		UIBezierPath* Page = [UIBezierPath bezierPathWithRoundedRect:CGRectMake(0.16f * Size, 0.12f * Size, 0.68f * Size, 0.76f * Size)
			                                            cornerRadius:0.08f * Size];
		[Page fill];

		// Zipper teeth: alternating notches down the centre.
		[[UIColor colorWithWhite:0.0 alpha:0.45] setFill];
		for (int I = 0; I < 5; I++)
		{
			CGFloat Y = (0.24f + 0.12f * I) * Size;
			CGFloat X = (I % 2 == 0) ? 0.44f * Size : 0.50f * Size;
			[[UIBezierPath bezierPathWithRect:CGRectMake(X, Y, 0.07f * Size, 0.07f * Size)] fill];
		}
	  });
	});
	return CachedImage;
}

UIImage* Icons::Doc()
{
	static UIImage* CachedImage = nil;
	static dispatch_once_t Token;
	dispatch_once(&Token, ^{
	  CachedImage = MakeSymbol(@"doc.fill", 20.0, ^(CGFloat Size) {
		UIBezierPath* Page = [UIBezierPath bezierPathWithRoundedRect:CGRectMake(0.18f * Size, 0.12f * Size, 0.64f * Size, 0.76f * Size)
			                                            cornerRadius:0.08f * Size];
		[Page fill];
	  });
	});
	return CachedImage;
}

UIImage* Icons::Share()
{
	static UIImage* CachedImage = nil;
	static dispatch_once_t Token;
	dispatch_once(&Token, ^{
	  CachedImage = MakeSymbol(@"square.and.arrow.up", 20.0, ^(CGFloat Size) {
		// Tray open at the top, with an arrow rising out of it.
		UIBezierPath* Tray = [UIBezierPath bezierPath];
		[Tray moveToPoint:CGPointMake(0.22f * Size, 0.44f * Size)];
		[Tray addLineToPoint:CGPointMake(0.22f * Size, 0.88f * Size)];
		[Tray addLineToPoint:CGPointMake(0.78f * Size, 0.88f * Size)];
		[Tray addLineToPoint:CGPointMake(0.78f * Size, 0.44f * Size)];
		Tray.lineWidth = 0.09f * Size;
		[Tray stroke];

		UIBezierPath* Arrow = [UIBezierPath bezierPath];
		[Arrow moveToPoint:CGPointMake(0.50f * Size, 0.62f * Size)];
		[Arrow addLineToPoint:CGPointMake(0.50f * Size, 0.14f * Size)];
		[Arrow moveToPoint:CGPointMake(0.32f * Size, 0.30f * Size)];
		[Arrow addLineToPoint:CGPointMake(0.50f * Size, 0.12f * Size)];
		[Arrow addLineToPoint:CGPointMake(0.68f * Size, 0.30f * Size)];
		Arrow.lineWidth    = 0.09f * Size;
		Arrow.lineCapStyle = kCGLineCapRound;
		[Arrow stroke];
	  });
	});
	return CachedImage;
}

UIImage* Icons::Trash()
{
	static UIImage* CachedImage = nil;
	static dispatch_once_t Token;
	dispatch_once(&Token, ^{
	  CachedImage = MakeSymbol(@"trash.fill", 20.0, ^(CGFloat Size) {
		UIBezierPath* Lid = [UIBezierPath bezierPathWithRoundedRect:CGRectMake(0.16f * Size, 0.20f * Size, 0.68f * Size, 0.10f * Size)
			                                           cornerRadius:0.04f * Size];
		[Lid fill];

		UIBezierPath* Can = [UIBezierPath bezierPathWithRoundedRect:CGRectMake(0.24f * Size, 0.32f * Size, 0.52f * Size, 0.54f * Size)
			                                           cornerRadius:0.07f * Size];
		[Can fill];
	  });
	});
	return CachedImage;
}

UIImage* Icons::ChevronLeft()
{
	static UIImage* CachedImage = nil;
	static dispatch_once_t Token;
	dispatch_once(&Token, ^{
	  CachedImage = MakeSymbol(@"chevron.left", 17.0, ^(CGFloat Size) {
		UIBezierPath* Path = [UIBezierPath bezierPath];
		[Path moveToPoint:CGPointMake(0.62f * Size, 0.18f * Size)];
		[Path addLineToPoint:CGPointMake(0.34f * Size, 0.50f * Size)];
		[Path addLineToPoint:CGPointMake(0.62f * Size, 0.82f * Size)];
		Path.lineWidth     = 0.11f * Size;
		Path.lineCapStyle  = kCGLineCapRound;
		Path.lineJoinStyle = kCGLineJoinRound;
		[Path stroke];
	  });
	});
	return CachedImage;
}

UIImage* Icons::ChevronRight()
{
	static UIImage* CachedImage = nil;
	static dispatch_once_t Token;
	dispatch_once(&Token, ^{
	  CachedImage = MakeSymbol(@"chevron.right", 13.0, ^(CGFloat Size) {
		UIBezierPath* Path = [UIBezierPath bezierPath];
		[Path moveToPoint:CGPointMake(0.38f * Size, 0.18f * Size)];
		[Path addLineToPoint:CGPointMake(0.66f * Size, 0.50f * Size)];
		[Path addLineToPoint:CGPointMake(0.38f * Size, 0.82f * Size)];
		Path.lineWidth     = 0.11f * Size;
		Path.lineCapStyle  = kCGLineCapRound;
		Path.lineJoinStyle = kCGLineJoinRound;
		[Path stroke];
	  });
	});
	return CachedImage;
}

UIImage* Icons::Close()
{
	static UIImage* CachedImage = nil;
	static dispatch_once_t Token;
	dispatch_once(&Token, ^{
	  CachedImage = MakeSymbol(@"xmark", 15.0, ^(CGFloat Size) {
		UIBezierPath* Path = [UIBezierPath bezierPath];
		[Path moveToPoint:CGPointMake(0.26f * Size, 0.26f * Size)];
		[Path addLineToPoint:CGPointMake(0.74f * Size, 0.74f * Size)];
		[Path moveToPoint:CGPointMake(0.74f * Size, 0.26f * Size)];
		[Path addLineToPoint:CGPointMake(0.26f * Size, 0.74f * Size)];
		Path.lineWidth    = 0.11f * Size;
		Path.lineCapStyle = kCGLineCapRound;
		[Path stroke];
	  });
	});
	return CachedImage;
}

UIImage* Icons::Check()
{
	static UIImage* CachedImage = nil;
	static dispatch_once_t Token;
	dispatch_once(&Token, ^{
	  CachedImage = MakeSymbol(@"checkmark", 13.0, ^(CGFloat Size) {
		UIBezierPath* Path = [UIBezierPath bezierPath];
		[Path moveToPoint:CGPointMake(0.24f * Size, 0.52f * Size)];
		[Path addLineToPoint:CGPointMake(0.43f * Size, 0.71f * Size)];
		[Path addLineToPoint:CGPointMake(0.77f * Size, 0.31f * Size)];
		Path.lineWidth     = 0.13f * Size;
		Path.lineCapStyle  = kCGLineCapRound;
		Path.lineJoinStyle = kCGLineJoinRound;
		[Path stroke];
	  });
	});
	return CachedImage;
}
