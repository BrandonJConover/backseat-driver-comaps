#import <CoreLocation/CoreLocation.h>
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef void (^MWMBackseatRewriteCompletion)(NSString * text);
typedef void (^MWMBackseatRewriteHandler)(NSString * instructions, NSString * prompt,
                                          MWMBackseatRewriteCompletion completion);
/// Lets the on-device model turn what the driver said into an app action (it calls +runTool:...),
/// or just answer. `done` says whether it ran an action; if not, the keyword parser gets the words,
/// and `chat` (the model's answer) is said when the keywords meant nothing either. Any thread.
typedef void (^MWMBackseatToolHandler)(NSString * spoken, void (^done)(BOOL used, NSString * chat));

/// Hands Backseat Driver's roasts to an on-device language model for rewording. The handler
/// runs off the main thread and may answer late or never; the canned line covers for it.
@interface MWMBackseatRewriter : NSObject

+ (void)installWithHandler:(MWMBackseatRewriteHandler)handler;
+ (void)setAvailable:(BOOL)available;
/// Something the driver said out loud (speech recognition), e.g. "I see a Texas plate". Main thread.
+ (void)driverSaid:(NSString *)text;
/// Adds a stop on the way, keeping the route's destination.
+ (void)addStop:(CLLocationCoordinate2D)coordinate title:(NSString *)title;
/// The GPS is saying `line`, so the mic may pick it up.
+ (void)noteSpoken:(NSString *)line;
/// The line the GPS said lately that `heard` is an echo of, if any.
+ (nullable NSString *)echoOf:(NSString *)heard;
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
/// NO when what was said doesn't support the tool: nothing ran.
+ (BOOL)runTool:(NSString *)name argument:(NSString *)argument spoken:(NSString *)spoken;
/// Tells the navigator which voices there are, for "what voice options do you have?".
+ (void)shareVoices;
/// How far the next turn is while following a route, in meters; -1 without a route.
@property(class, nonatomic, readonly) double metersToNextTurn;
/// The speed limit here while following a route, m/s: -1 unknown, 0 unlimited.
@property(class, nonatomic, readonly) double speedLimitMps;

/// Who's in the car, for the roasts. Pronouns are "they", "he" or "she". Setting any of these
/// reloads the navigator's personality at once.
@property(class, nonatomic, copy) NSString * partner;
@property(class, nonatomic, copy) NSString * partnerPronouns;
@property(class, nonatomic, copy) NSString * driverPronouns;
@property(class, nonatomic) BOOL carHasAutopilot;
/// What to call the driver; empty until first-run setup.
@property(class, nonatomic, copy) NSString * driverName;
/// The driver's age as typed ("34"); empty if never given.
@property(class, nonatomic, copy) NSString * driverAge;
/// Call when the profile section of Settings appears, and endProfileEdit when it goes away. A hand
/// edit of name, pronouns or age comes back as a line to speak ("Changing it up, I see. Okay,
/// Brandon... I mean, Brenda."); empty for none.
+ (void)beginProfileEdit;
+ (NSString *)endProfileEdit;

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
/// 20 Questions or the plate game is on, so what's heard is an answer, not a request.
@property(class, nonatomic, readonly) BOOL inGame;
/// A line for the voice preview, with `voice` filled in.
+ (NSString *)voicePreviewLine:(NSString *)key voice:(NSString *)voice;
/// The silly phrase that picks this voice ("banana phone").
+ (NSString *)voicePhrase:(NSString *)voiceID;
/// How often `text` says a voice's name, tolerating the recognizer's close misses.
+ (NSInteger)countVoiceName:(NSString *)name in:(NSString *)text;
/// Places driven to before, most driven first, for the "Where to?" list. Each has "label" ("Home",
/// "Starbucks"), "destination", "lat", "lon", "trips" and "minutes" (usual drive time, 0 if unknown).
+ (NSArray<NSDictionary<NSString *, id> *> *)pastRoutes:(NSInteger)max;
/// The GPS's permanent memory of the driver. Remembering a topic again replaces it, except "note",
/// which piles up. memories: oldest first, each with "topic", "fact" and "learnedAt" (NSDate).
+ (BOOL)remember:(NSString *)fact topic:(NSString *)topic;
+ (BOOL)forget:(NSString *)topic;
@property(class, nonatomic, readonly) NSArray<NSDictionary<NSString *, id> *> * memories;
/// Instructions for the on-device model when the driver is just talking: the jaded persona, the
/// rules (do_app_action, remember_fact) and everything in memory.
+ (NSString *)chatInstructions:(NSString *)voiceName;
/// A "for your file" question to ask at a quiet moment, or empty. Listen once, then fileAnswer.
@property(class, nonatomic, readonly) NSString * fileQuestion;
+ (NSString *)fileAnswer:(NSString *)spoken;
/// The do-nothing Niceness slider: call when the driver lets go of it, speak the laugh, then animate
/// the slider back to the mean end ("I guess I could tone it back a bit").
+ (NSString *)nicenessMoved;
/// The driver panned the map away from the car mid-trip. A line to say, or empty (rate-limited).
+ (NSString *)mapBrowsed;
/// The first character typed into the search field. A line to say, or empty.
+ (NSString *)typingDestination;
/// A rough stretch of road (several hard jolts). A line to say, or empty.
+ (NSString *)roughRoad;
/// Spoken once the slider is back at the mean end: "Don't touch my slider."
+ (NSString *)nicenessSlidBack;
/// After the arrival line, at most once every 20 hours: the one-to-ten rating question, or empty.
/// Listen once, then say surveyAnswer: (an empty answer gets "That wasn't a number...").
@property(class, nonatomic, readonly) NSString * arrivalSurvey;
+ (NSString *)surveyAnswer:(NSString *)spoken;

/// The mileage log. Off by default; nothing is recorded until mileageOn is set.
@property(class, nonatomic) BOOL mileageOn;
/// Talk about drives the driver didn't start (default on).
@property(class, nonatomic) BOOL mileageTalk;
/// Home is the main place of business, so trips from home to work places are business (default off).
@property(class, nonatomic) BOOL homeIsWorkplace;
/// Every location fix, in time order. `activity`: 0 unknown, 1 stationary, 2 walking, 3 running,
/// 4 cycling, 5 automotive. Returns a line to say, or empty.
+ (NSString *)driveSample:(double)lat
                      lon:(double)lon
                    speed:(double)speedMps
                 accuracy:(double)accuracyM
                 activity:(NSInteger)activity
                     time:(double)timeMs;
/// The OS says we arrived (CLVisit) or the app is going away. Returns a line to say, or empty.
+ (NSString *)mileageFinish;
/// "businessMiles", "personalMiles", "commuteMiles", "unknownMiles", "businessDrives",
/// "personalDrives", "unknownDrives", "ratePerMile", "estimatedDeduction", "unknown" and "flagged"
/// (arrays of mileageLog indexes).
+ (NSDictionary<NSString *, id> *)mileageSummary:(NSInteger)year;
/// Every drive: "index", "startedAt" (NSDate), "fromName", "toName", "miles", "bucket" ("Business",
/// "Personal", "Unknown"), "why", "outOfState", "farFromHome", "newPlace", "purpose".
@property(class, nonatomic, readonly) NSArray<NSDictionary<NSString *, id> *> * mileageLog;
+ (NSString *)mileageCsv:(NSInteger)year;
+ (NSInteger)similarDrives:(NSInteger)index;
/// `bucket`: 0 Unknown, 1 Business, 2 Personal. Returns a line to say.
+ (NSString *)reassignDrive:(NSInteger)index bucket:(NSInteger)bucket similar:(BOOL)similar purpose:(NSString *)purpose;
/// `role`: 0 Home, 1 Office, 2 Client, 3 Personal.
+ (void)tagPlace:(NSString *)name lat:(double)lat lon:(double)lon role:(NSInteger)role;
/// "name", "lat", "lon", "role".
@property(class, nonatomic, readonly) NSArray<NSDictionary<NSString *, id> *> * taggedPlaces;
/// In December or January, once a year, the year in miles; empty otherwise.
@property(class, nonatomic, readonly) NSString * mileageYearEnd;
/// Right after the mileage log is turned on the first time: "Whose business are we tracking?"
/// Empty after that.
+ (NSString *)mileageTurnedOn;
/// The GPS's own name, the voice the driver picked ("Echo"), for the fake phone calls.
+ (void)setGpsName:(NSString *)name;
/// The last trip's report card, for the arrival card; nil until a trip reaches its destination in
/// this app run. Keys: at (epoch seconds), letter, quip, destination, instructionsScore,
/// drivingScore, onTime, late, missed, wrongWay, reroutes, speedingSeconds, maxOverLimit,
/// hardBrakes, hardAccels, expectedMin, actualMin, and previousLetter when there was a trip before.
@property(class, nonatomic, readonly, nullable) NSDictionary<NSString *, id> * lastTripReport;
/// How wound up the GPS is this trip, 0-100 (0 with no trip).
@property(class, nonatomic, readonly) NSInteger frustration;
/// The last mileage line asked a question; listen once and pass the answer to +mileageAnswer:.
@property(class, nonatomic, readonly) BOOL mileageListening;
/// Records the answer and returns what to say, or empty if it wasn't understood.
+ (NSString *)mileageAnswer:(NSString *)spoken;
/// Whose business the log tracks: "" the driver's own, "wife", or a name.
@property(class, nonatomic) NSString * businessOwner;
/// The GPS's opinion of a drawn route, once it's built. Empty otherwise. Once.
+ (NSString *)takeSketchVerdict;
/// After the drawing is erased: "Oh, now you want my route."
+ (NSString *)sketchCleared;
/// Kokoro's comeback after the system voice stood in: play a cough, then speak this.
@property(class, nonatomic, readonly) NSString * voiceRecovered;
/// Voice search: what the GPS says about the top results (names with their streets, nearest
/// first). `pick` is the result to route to straight away (only one found); `none` means there's
/// nothing to choose; otherwise listen and pass the answer to +pickSearchResult:.
+ (NSString *)speakSearchResults:(NSString *)query
                           names:(NSArray<NSString *> *)names
                       distances:(NSArray<NSNumber *> *)distances
                            pick:(NSInteger * _Nullable)pick
                            none:(BOOL * _Nullable)none
                         confirm:(BOOL * _Nullable)confirm;
/// "The second one": the result picked (-1 when not understood) and the line to say.
+ (NSString *)pickSearchResult:(NSString *)answer index:(NSInteger *)index confirm:(BOOL *)confirm;
/// The answer to "Latheron Place? Say yes." Empty when nobody answered. Route only when confirmed.
+ (NSString *)confirmSearchResult:(NSString *)answer confirmed:(BOOL *)confirmed;
/// Builds a route to a place by voice.
+ (void)routeTo:(CLLocationCoordinate2D)coordinate title:(NSString *)title;
/// What the GPS has worked out about the driver, as label / value pairs ("Works at", "Acme Corp").
@property(class, nonatomic, readonly) NSArray<NSArray<NSString *> *> * learnedFacts;

/// First launch (Brandon's script): the disclaimer, paragraph by paragraph, as written for the screen.
+ (NSArray<NSString *> *)disclaimerParagraphs:(BOOL)creditCoMaps;
/// A disclaimer paragraph as the voice reads it ("him slash her slash the officer").
+ (NSString *)spokenDisclaimer:(NSString *)paragraph;
/// What the GPS says to a disclaimer answer (0 yes, 1 no, 2 skip) at `round` (0 the first ask,
/// 1 after a no), in order; `done` says whether the menu is over.
+ (NSArray<NSString *> *)termsAnswer:(NSInteger)answer round:(NSInteger)round done:(BOOL *)done;
/// Any line from the GPS's script ("onboard.monologuing"), with {placeholders} filled from `vars`.
+ (NSString *)line:(NSString *)key vars:(NSDictionary<NSString *, NSString *> *)vars;
/// Getting to know you: question 0 gender, 1 married, 2 home. The question, its answer choices
/// ("man", "woman", "skip"...), and the GPS's comment on an answer, which also saves it.
+ (NSString *)aboutYouQuestion:(NSInteger)question;
+ (NSArray<NSString *> *)aboutYouChoices:(NSInteger)question;
+ (NSString *)aboutYouAnswered:(NSInteger)question answer:(NSString *)answer;
/// What the driver answered before, "" if never.
+ (NSString *)aboutYouAnswer:(NSInteger)question;
/// Each voice's audition for the voice line-up ("Hi, I'm Echo. ...say Purple Monkey."), in order.
+ (NSArray<NSString *> *)voiceAuditions:(NSArray<NSString *> *)voiceIDs;
/// Whether `typed` is the code phrase, ignoring case, spaces and punctuation.
+ (BOOL)typed:(NSString *)typed matchesPhrase:(NSString *)phrase;

@end

NS_ASSUME_NONNULL_END
