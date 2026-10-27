#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef void (^MWMBackseatRewriteCompletion)(NSString * text);
typedef void (^MWMBackseatRewriteHandler)(NSString * instructions, NSString * prompt,
                                          MWMBackseatRewriteCompletion completion);
/// Lets the on-device model turn what the driver said into an app action (it calls +runTool:...).
/// `done` says whether it picked one; if not, the keyword parser gets the words. Any thread.
typedef void (^MWMBackseatToolHandler)(NSString * spoken, void (^done)(BOOL used));

/// Hands Backseat Driver's roasts to an on-device language model for rewording. The handler
/// runs off the main thread and may answer late or never; the canned line covers for it.
@interface MWMBackseatRewriter : NSObject

+ (void)installWithHandler:(MWMBackseatRewriteHandler)handler;
+ (void)setAvailable:(BOOL)available;
/// Something the driver said out loud (speech recognition), e.g. "I see a Texas plate". Main thread.
+ (void)driverSaid:(NSString *)text;
/// Lets the GPS see whether other audio (music, podcasts) is playing.
+ (void)installMusicCheck;

+ (void)installToolHandler:(MWMBackseatToolHandler)handler;
+ (void)setToolsAvailable:(BOOL)available;
/// The app actions the model can pick from, snake_case ("cancel_route"), and what each does.
@property(class, nonatomic, readonly) NSArray<NSString *> * toolNames;
@property(class, nonatomic, readonly) NSString * toolGuide;
/// Each app action as "what it does", for the "what can you do?" sheet.
@property(class, nonatomic, readonly) NSArray<NSString *> * commandDescriptions;
/// Carries out the action the model picked. Main thread.
+ (void)runTool:(NSString *)name argument:(NSString *)argument spoken:(NSString *)spoken;
/// Tells the navigator which voices there are, for "what voice options do you have?".
+ (void)shareVoices;

/// Who's in the car, for the roasts. Pronouns are "they", "he" or "she". Setting any of these
/// reloads the navigator's personality at once.
@property(class, nonatomic, copy) NSString * partner;
@property(class, nonatomic, copy) NSString * partnerPronouns;
@property(class, nonatomic, copy) NSString * driverPronouns;
@property(class, nonatomic) BOOL carHasAutopilot;
/// What to call the driver; empty until first-run setup.
@property(class, nonatomic, copy) NSString * driverName;

/// The spoken first-run setup, in order: intro, name (asked until it sticks), wrong name, where to.
typedef NS_ENUM(NSInteger, MWMBackseatSetupStep) {
  MWMBackseatSetupStepIntro,
  MWMBackseatSetupStepName,
  MWMBackseatSetupStepWrongName,
  MWMBackseatSetupStepWhereTo,
  MWMBackseatSetupStepPronouns,
  MWMBackseatSetupStepPassenger
};
/// What the GPS says at `step`; `voiceName` is the voice saying it ("Echo").
+ (NSString *)setupLine:(MWMBackseatSetupStep)step voiceName:(NSString *)voiceName;
/// The driver's answer at `step`. NO means ask again (the name wasn't understood).
+ (BOOL)setupHeard:(MWMBackseatSetupStep)step answer:(NSString *)answer;
/// "Where are we going?" (or a guess: "It's Tuesday morning. Work, I assume?") when the app opens.
@property(class, nonatomic, readonly) NSString * askWhereTo;
/// What to say when nobody answered `askWhereTo`; empty for nothing.
@property(class, nonatomic, readonly) NSString * noAnswerToWhereTo;
/// What the GPS has worked out about the driver, as label / value pairs ("Works at", "Acme Corp").
@property(class, nonatomic, readonly) NSArray<NSArray<NSString *> *> * learnedFacts;

@end

NS_ASSUME_NONNULL_END
