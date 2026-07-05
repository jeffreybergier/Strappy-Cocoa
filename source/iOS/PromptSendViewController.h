#import <UIKit/UIKit.h>

@class PromptSendViewController;

@protocol PromptSendViewControllerDelegate <NSObject>
- (void)promptSendViewController:(PromptSendViewController *)controller
                  didSubmitPrompt:(NSString *)prompt;
- (NSArray *)allowedModelsForPromptSendViewController:
    (PromptSendViewController *)controller;
- (NSString *)selectedModelIdentifierForPromptSendViewController:
    (PromptSendViewController *)controller;
- (BOOL)promptSendViewController:(PromptSendViewController *)controller
        setSelectedModelIdentifier:(NSString *)modelIdentifier;
- (BOOL)promptSendViewController:(PromptSendViewController *)controller
              setStreamingEnabled:(BOOL)enabled;
- (void)promptSendViewControllerDidCancelPrompt:
    (PromptSendViewController *)controller;
- (void)promptSendViewControllerDidChangeHeight:
    (PromptSendViewController *)controller;
@end

@interface PromptSendViewController : UIView

@property (nonatomic, assign) id<PromptSendViewControllerDelegate> delegate;

- (CGFloat)preferredHeight;
- (void)setComposing:(BOOL)composing;
- (void)setEnabled:(BOOL)enabled;
- (void)setSending:(BOOL)sending;
- (void)setCancellationRequested:(BOOL)requested;
- (void)setStreamingEnabled:(BOOL)enabled;
- (void)reloadOptionsMenu;
- (BOOL)canSendCurrentPrompt;
- (void)performSend:(id)sender;

@end
