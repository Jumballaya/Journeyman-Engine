// macOS: whether the user's appearance (System Settings > Appearance) is light.
#import <Foundation/Foundation.h>

namespace theme {

bool systemPrefersLight() {
  @autoreleasepool {
    // Set to "Dark" in dark mode, absent in light mode.
    NSString* style = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleInterfaceStyle"];
    return ![style isEqualToString:@"Dark"];
  }
}

}  // namespace theme
