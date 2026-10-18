/* Test-only Objective-C runtime bridge for Tiger and modern runtimes. */
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
static void XPScrollProbeExchangeMethods(Method first, Method second)
{
#if defined(__OBJC2__) && __OBJC2__
  method_exchangeImplementations(first, second);
#else
  IMP previous = first->method_imp;
  first->method_imp = second->method_imp;
  second->method_imp = previous;
#endif
}
