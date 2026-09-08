#include "IProfile.h"

#import <Foundation/Foundation.h>

std::string IProfile::GetGameIdentifier() const
{
	NSBundle* MainBundle = [NSBundle mainBundle];
	if (MainBundle && MainBundle.bundleIdentifier.length > 0)
		return [MainBundle.bundleIdentifier UTF8String];

	return [[[NSProcessInfo processInfo] processName] UTF8String];
}

std::string IProfile::GetGameVersion() const
{
	NSBundle* MainBundle = [NSBundle mainBundle];
	NSString* Version    = MainBundle ? [MainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"] : nil;
	if (MainBundle && Version.length > 0)
		return [Version UTF8String];

	return "";
}