#import <UIKit/UIKit.h>
#import "StrappySession.h"

@class PromptSendViewController;

@protocol PromptSendViewControllerDelegate <NSObject>
- (void)promptSendViewController:(PromptSendViewController *)controller
                  didSubmitPrompt:(NSString *)prompt;
- (NSArray *)allowedModelsForPromptSendViewController:
    (PromptSendViewController *)controller;
- (NSArray *)assistantSetsForPromptSendViewController:
    (PromptSendViewController *)controller;
- (BOOL)promptSendViewController:(PromptSendViewController *)controller
            updateSessionOptions:(StrappySessionOptions *)options
                   changedFields:(StrappySessionOptionMask)changedFields;
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
- (void)setStudyLocked:(BOOL)studyLocked;
- (void)setSending:(BOOL)sending;
- (void)setCancellationRequested:(BOOL)requested;
- (void)setSessionOptions:(StrappySessionOptions *)options;
- (void)reloadOptionsMenu;
- (BOOL)canSendCurrentPrompt;
- (void)performSend:(id)sender;

@end
