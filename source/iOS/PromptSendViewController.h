#import <UIKit/UIKit.h>

@class PromptSendViewController;

@protocol PromptSendViewControllerDelegate <NSObject>
- (void)promptSendViewController:(PromptSendViewController *)controller
                  didSubmitPrompt:(NSString *)prompt;
- (NSArray *)allowedModelsForPromptSendViewController:
    (PromptSendViewController *)controller;
- (NSArray *)assistantSetsForPromptSendViewController:
    (PromptSendViewController *)controller;
- (NSString *)selectedAssistantSetIdentifierForPromptSendViewController:
    (PromptSendViewController *)controller;
- (BOOL)promptSendViewController:(PromptSendViewController *)controller
  setSelectedAssistantSetIdentifier:(NSString *)assistantSetIdentifier;
- (NSString *)selectedModelIdentifierForPromptSendViewController:
    (PromptSendViewController *)controller;
- (BOOL)promptSendViewController:(PromptSendViewController *)controller
        setSelectedModelIdentifier:(NSString *)modelIdentifier;
- (BOOL)promptSendViewController:(PromptSendViewController *)controller
                  setWebProvider:(NSString *)webProvider;
- (BOOL)promptSendViewController:(PromptSendViewController *)controller
                  setBashEnabled:(BOOL)enabled;
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
- (void)setWebProvider:(NSString *)webProvider;
- (void)setBashEnabled:(BOOL)enabled;
- (void)reloadOptionsMenu;
- (BOOL)canSendCurrentPrompt;
- (void)performSend:(id)sender;

@end
