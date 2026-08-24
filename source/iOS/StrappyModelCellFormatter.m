#import "StrappyModelCellFormatter.h"

static NSString *StrappyModelCellStringForRow(NSDictionary *row,
                                              NSString *key)
{
  NSString *value;

  value = [row objectForKey:key];
  return [value isKindOfClass:[NSString class]] ? value : @"";
}

static NSString *StrappyModelCellNumberString(NSDictionary *row,
                                              NSString *key)
{
  NSNumber *value;
  unsigned long long count;

  value = [row objectForKey:key];
  if (![value isKindOfClass:[NSNumber class]] || ([value longLongValue] <= 0LL)) {
    return @"";
  }
  count = [value unsignedLongLongValue];
  return [NSString stringWithFormat:@"%lluk", (count + 500ULL) / 1000ULL];
}

static NSString *StrappyModelCellPricingString(NSDictionary *row,
                                               NSString *key)
{
  static NSNumberFormatter *formatter = nil;
  NSString *value;
  double dollarsPerMillion;
  NSString *formatted;

  value = StrappyModelCellStringForRow(row, key);
  if ([value length] == 0U) {
    return @"";
  }

  if (formatter == nil) {
    NSLocale *locale;

    formatter = [[NSNumberFormatter alloc] init];
    [formatter setFormatterBehavior:NSNumberFormatterBehavior10_4];
    [formatter setNumberStyle:NSNumberFormatterCurrencyStyle];
    locale = [[NSLocale alloc] initWithLocaleIdentifier:@"en_US_POSIX"];
    [formatter setLocale:locale];
    [formatter setCurrencyCode:@"USD"];
    [formatter setCurrencySymbol:@"$"];
    [formatter setMinimumFractionDigits:0U];
    [formatter setMaximumFractionDigits:6U];
  }

  dollarsPerMillion = [value doubleValue] * 1000000.0;
  formatted =
    [formatter stringFromNumber:[NSNumber numberWithDouble:dollarsPerMillion]];
  return (formatted != nil) ? formatted : @"";
}

NSString *StrappyModelCellDetailText(NSDictionary *model)
{
  NSMutableArray *details;
  NSString *context;
  NSString *accountName;
  NSString *billingKind;
  NSString *promptPrice;
  NSString *completionPrice;

  details = [NSMutableArray array];
  accountName = StrappyModelCellStringForRow(model, @"provider_name");
  if ([accountName length] > 0U) {
    [details addObject:accountName];
  }
  billingKind = StrappyModelCellStringForRow(model, @"billing_kind");
  if ([billingKind isEqualToString:@"chatgpt_plan"]) {
    [details addObject:NSLocalizedString(@"ChatGPT plan", nil)];
  }
  context = StrappyModelCellNumberString(model, @"context_length");
  if ([context length] > 0U) {
    [details addObject:[NSString stringWithFormat:
      NSLocalizedString(@"Context: %@", nil), context]];
  }
  promptPrice = StrappyModelCellPricingString(model, @"pricing_prompt");
  completionPrice =
    StrappyModelCellPricingString(model, @"pricing_completion");
  if ([promptPrice length] > 0U) {
    [details addObject:[NSString stringWithFormat:
      NSLocalizedString(@"In: %@", nil), promptPrice]];
  }
  if ([completionPrice length] > 0U) {
    [details addObject:[NSString stringWithFormat:
      NSLocalizedString(@"Out: %@", nil), completionPrice]];
  }
  return [details componentsJoinedByString:@", "];
}
