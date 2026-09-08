#pragma once

#import <UIKit/UIKit.h>

#include <functional>

@interface DraggableButton : UIButton

@property(nonatomic, assign) std::function<void(DraggableButton*)> onClick;
@property(nonatomic, assign) std::function<void(CGPoint)> onDragEnd;
@property(nonatomic, assign) BOOL clampToSuperview;
@property(nonatomic, assign) BOOL snapToEdge;
@property(nonatomic, assign) BOOL enablePulse;

@end
