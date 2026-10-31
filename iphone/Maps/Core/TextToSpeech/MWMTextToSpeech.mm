#import <AVFoundation/AVFoundation.h>
#import "MWMBackseatRewriter.h"
#import "MWMRouter.h"
#import "MWMTextToSpeech+CPP.h"
#import "SwiftBridge.h"
#import "TTSTester.h"

#include "LocaleTranslator.h"

#include <CoreApi/Framework.h>

#include "platform/languages.hpp"

using namespace locale_translator;
using namespace routing;

namespace
{
NSString * const kUserDefaultsTTSLanguageBcp47 = @"UserDefaultsTTSLanguageBcp47";
NSString * const kIsTTSEnabled = @"UserDefaultsNeedToEnableTTS";
NSString * const kNavigationSoundMode = @"UserDefaultsNavigationSoundMode";
NSString * const kIsStreetNamesTTSEnabled = @"UserDefaultsNeedToEnableStreetNamesTTS";
NSString * const kDefaultLanguage = @"en-US";

std::vector<std::pair<std::string, std::string>> availableLanguages()
{
  NSArray<AVSpeechSynthesisVoice *> * voices = [AVSpeechSynthesisVoice speechVoices];
  std::vector<std::pair<std::string, std::string>> native;
  for (AVSpeechSynthesisVoice * v in voices)
    native.emplace_back(make_pair(bcp47ToTwineLanguage(v.language), v.language.UTF8String));

  using namespace routing::turns::sound;
  std::vector<std::pair<std::string, std::string>> result;
  for (auto const & [twineRouting, _] : kLanguageList)
  {
    for (auto const & [twineVoice, bcp47Voice] : native)
    {
      if (twineVoice == twineRouting)
      {
        auto pair = std::make_pair(bcp47Voice, tts::translateLocale(bcp47Voice));
        if (std::find(result.begin(), result.end(), pair) == result.end())
          result.emplace_back(std::move(pair));
      }
    }
  }
  return result;
}

using Observer = id<MWMTextToSpeechObserver>;
using Observers = NSHashTable<Observer>;
}  // namespace

@interface MWMTextToSpeech ()<AVSpeechSynthesizerDelegate>
{
  std::vector<std::pair<std::string, std::string>> _availableLanguages;
}

@property(nonatomic) AVSpeechSynthesizer * speechSynthesizer;
@property(nonatomic) AVSpeechSynthesisVoice * speechVoice;
@property(nonatomic) AVAudioPlayer * audioPlayer;

@property(nonatomic) Observers * observers;

- (BOOL)allowsSpokenNotifications;

@end

@implementation MWMTextToSpeech

+ (MWMTextToSpeech *)tts {
  static dispatch_once_t onceToken;
  static MWMTextToSpeech * tts = nil;
  dispatch_once(&onceToken, ^{
    tts = [[self alloc] initTTS];
  });
  return tts;
}

+ (void)applicationDidBecomeActive {
  auto tts = [self tts];
  tts.speechSynthesizer = nil;
  tts.speechVoice = nil;
}

- (instancetype)initTTS {
  self = [super init];
  if (self) {
    _availableLanguages = availableLanguages();
    _observers = [Observers weakObjectsHashTable];

    NSString * saved = [[self class] savedLanguage];
    NSString * preferedLanguageBcp47;
    if (saved.length)
      preferedLanguageBcp47 = saved;
    else
      preferedLanguageBcp47 = [AVSpeechSynthesisVoice currentLanguageCode];

    std::pair<std::string, std::string> const lan =
        std::make_pair(preferedLanguageBcp47.UTF8String,
                       tts::translateLocale(preferedLanguageBcp47.UTF8String));

    if (find(_availableLanguages.begin(), _availableLanguages.end(), lan) !=
        _availableLanguages.end())
      [self setNotificationsLocale:preferedLanguageBcp47];
    else
      [self setNotificationsLocale:kDefaultLanguage];

    NSError * err = nil;
    if (![[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayback
                                          mode:AVAudioSessionModeVoicePrompt
                                          options:AVAudioSessionCategoryOptionInterruptSpokenAudioAndMixWithOthers |
                                                  AVAudioSessionCategoryOptionDuckOthers
                                                error:&err]) {
      LOG(LWARNING, ("Couldn't configure audio session: ", [err localizedDescription]));
    }

    NSDictionary * dictionary = @{
      kNavigationSoundMode : @(MWMNavigationSoundModeVoiceGuidance),
      kIsStreetNamesTTSEnabled : @NO
    };
    [NSUserDefaults.standardUserDefaults registerDefaults:dictionary];

    [MWMRouter enableTurnNotifications:self.allowsTurnInstructions];

    // Backseat Driver: the Kokoro voice, with the system voice as fallback.
    __weak MWMTextToSpeech * weakSelf = self;
    BackseatKokoroVoice.shared.systemIsSpeaking = ^BOOL {
      return weakSelf.speechSynthesizer.isSpeaking;
    };
    [BackseatKokoroVoice.shared start];
  }
  return self;
}

- (void)dealloc {
  [[AVAudioSession sharedInstance] setActive:NO
                                 withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation
                                       error:nil];
  self.speechSynthesizer.delegate = nil;
}
- (std::vector<std::pair<std::string, std::string>>)availableLanguages { return _availableLanguages; }
- (std::pair<std::string, std::string>)standardLanguage {
  return std::make_pair(kDefaultLanguage.UTF8String, tts::translateLocale(kDefaultLanguage.UTF8String));
}
- (void)setNotificationsLocale:(NSString *)locale {
  NSUserDefaults * ud = NSUserDefaults.standardUserDefaults;
  [ud setObject:locale forKey:kUserDefaultsTTSLanguageBcp47];
  [self createVoice:locale];
}
- (AVSpeechSynthesisVoice *)voice {
  [self createVoice:[[self class] savedLanguage]];
  return self.speechVoice;
}
- (BOOL)isValid { return _speechSynthesizer != nil && _speechVoice != nil; }
+ (BOOL)isTTSEnabled { return [NSUserDefaults.standardUserDefaults boolForKey:kIsTTSEnabled]; }
+ (void)setTTSEnabled:(BOOL)enabled {
  if ([self isTTSEnabled] == enabled)
    return;
  auto tts = [self tts];
  NSUserDefaults * ud = NSUserDefaults.standardUserDefaults;
  [ud setBool:enabled forKey:kIsTTSEnabled];

  [MWMRouter enableTurnNotifications:tts.allowsTurnInstructions];
  [tts onTTSStatusUpdated];
}
+ (BOOL)isStreetNamesTTSEnabled { return [NSUserDefaults.standardUserDefaults boolForKey:kIsStreetNamesTTSEnabled]; }
+ (void)setStreetNamesTTSEnabled:(BOOL)enabled {
  if ([self isStreetNamesTTSEnabled] == enabled)
    return;
  NSUserDefaults * ud = NSUserDefaults.standardUserDefaults;
  [ud setBool:enabled forKey:kIsStreetNamesTTSEnabled];
  [ud synchronize];
}

- (void)setNavigationSoundMode:(MWMNavigationSoundMode)navigationSoundMode {
  if (self.navigationSoundMode == navigationSoundMode)
    return;

  if (navigationSoundMode != MWMNavigationSoundModeMuted && [[self class] isTTSEnabled] && ![self isValid])
    [self createVoice:[[self class] savedLanguage]];

  [NSUserDefaults.standardUserDefaults setInteger:navigationSoundMode forKey:kNavigationSoundMode];
  [MWMRouter enableTurnNotifications:self.allowsTurnInstructions];
  dispatch_async(dispatch_get_main_queue(), ^{
    [self onTTSStatusUpdated];
  });
}

- (MWMNavigationSoundMode)navigationSoundMode {
  NSInteger const mode = [NSUserDefaults.standardUserDefaults integerForKey:kNavigationSoundMode];
  if (mode < MWMNavigationSoundModeVoiceGuidance || mode > MWMNavigationSoundModeMuted)
    return MWMNavigationSoundModeVoiceGuidance;
  return (MWMNavigationSoundMode)mode;
}

- (BOOL)allowsTurnInstructions {
  return [[self class] isTTSEnabled] &&
         self.navigationSoundMode == MWMNavigationSoundModeVoiceGuidance;
}

- (BOOL)allowsSpeedCameraWarnings {
  return [[self class] isTTSEnabled] &&
         self.navigationSoundMode != MWMNavigationSoundModeMuted;
}

- (BOOL)allowsSpokenNotifications {
  return [[self class] isTTSEnabled] &&
         self.navigationSoundMode != MWMNavigationSoundModeMuted;
}

- (void)cycleNavigationSoundMode {
  switch (self.navigationSoundMode) {
    case MWMNavigationSoundModeVoiceGuidance:
      self.navigationSoundMode = MWMNavigationSoundModeSpeedCameraWarningsOnly;
      break;
    case MWMNavigationSoundModeSpeedCameraWarningsOnly:
      self.navigationSoundMode = MWMNavigationSoundModeMuted;
      break;
    case MWMNavigationSoundModeMuted:
      self.navigationSoundMode = MWMNavigationSoundModeVoiceGuidance;
      break;
  }
}

+ (NSDictionary<NSString *, NSString *> *)availableLanguages
{
  NSMutableDictionary<NSString *, NSString *> *availableLanguages = [[NSMutableDictionary alloc] init];
  auto const & v = [[self tts] availableLanguages];
  for (auto i: v) {
    [availableLanguages setObject:@(i.second.c_str()) forKey:@(i.first.c_str())];
  }
  return availableLanguages;
}

+ (NSString *)selectedLanguage {
  if ([self savedLanguage] != nil) {
    return [self savedLanguage];
  }

  NSString * preferedLanguageBcp47 = [AVSpeechSynthesisVoice currentLanguageCode];

  std::pair<std::string, std::string> const lan =
  std::make_pair(preferedLanguageBcp47.UTF8String, tts::translateLocale(preferedLanguageBcp47.UTF8String));

  std::vector<std::pair<std::string, std::string>> const availableLanguages = [[self tts] availableLanguages];
  if (find(availableLanguages.begin(), availableLanguages.end(), lan) !=
      availableLanguages.end()) {
    return preferedLanguageBcp47;
  }

  return kDefaultLanguage;
}

+ (NSString *)savedLanguage {
  return [NSUserDefaults.standardUserDefaults stringForKey:kUserDefaultsTTSLanguageBcp47];
}

+ (NSInteger)speedCameraMode
{
  SpeedCameraManagerMode mode = GetFramework().GetRoutingManager().GetSpeedCamManager().GetMode();
  if (mode == SpeedCameraManagerMode::Auto) {
    return 2;
  } else if (mode == SpeedCameraManagerMode::Always) {
    return 1;
  }

  return 0;
}

+ (void)setSpeedCameraMode:(NSInteger)speedCameraMode
{
  SpeedCameraManagerMode mode = SpeedCameraManagerMode::Never;
  if (speedCameraMode == 2) {
    mode = SpeedCameraManagerMode::Auto;
  } else if (speedCameraMode == 1) {
    mode = SpeedCameraManagerMode::Always;
  }
  GetFramework().GetRoutingManager().GetSpeedCamManager().SetMode(mode);
}

- (void)createVoice:(NSString *)locale {
  if (!self.speechSynthesizer) {
    self.speechSynthesizer = [[AVSpeechSynthesizer alloc] init];
    self.speechSynthesizer.delegate = self;
  }

  NSMutableArray<NSString *> * candidateLocales = [@[ kDefaultLanguage, @"en-GB" ] mutableCopy];

  if (locale)
    [candidateLocales insertObject:locale atIndex:0];
  else
    LOG(LWARNING, ("locale is nil. Trying default locale."));

  AVSpeechSynthesisVoice * voice = nil;
  for (NSString * loc in candidateLocales) {
    if (@available(iOS 16.0, *)) {
      for (AVSpeechSynthesisVoice * aVoice in [AVSpeechSynthesisVoice speechVoices]) {
        if (voice == nil && aVoice.language == loc && aVoice.quality == AVSpeechSynthesisVoiceQualityPremium) {
          voice = aVoice;
        }
      }
    }
    for (AVSpeechSynthesisVoice * aVoice in [AVSpeechSynthesisVoice speechVoices]) {
      if (voice == nil && aVoice.language == loc && aVoice.quality == AVSpeechSynthesisVoiceQualityEnhanced) {
        voice = aVoice;
      }
    }
    if (voice == nil) {
      voice = [AVSpeechSynthesisVoice voiceWithLanguage:loc];
    }
    if (voice) {
      break;
    }
  }

  self.speechVoice = voice;
  if (voice) {
    std::string const twineLang = bcp47ToTwineLanguage(voice.language);
    if (twineLang.empty())
      LOG(LERROR, ("Cannot convert UI locale or default locale to twine language. MWMTextToSpeech "
                   "is invalid."));
    else
      [MWMRouter setTurnNotificationsLocale:@(twineLang.c_str())];
  } else {
    LOG(LWARNING,
        ("The UI language and English are not available for TTS. MWMTextToSpeech is invalid."));
  }
}

+ (void)playTest
{
  TTSTester * ttsTester = [[TTSTester alloc] init];
  [ttsTester playRandomTestString];
}


- (void)speakOneString:(NSString *)textToSpeak {
  // Backseat: one utterance per sentence with a breath between them; run together, the system
  // voice barely pauses and sounds rushed.
  NSString * text = [textToSpeak stringByReplacingOccurrencesOfString:@" || " withString:@". "];
  NSMutableArray<NSString *> * sentences = [NSMutableArray array];
  [text enumerateSubstringsInRange:NSMakeRange(0, text.length)
                           options:NSStringEnumerationBySentences
                        usingBlock:^(NSString * sentence, NSRange, NSRange, BOOL *) {
    NSString * trimmed = [sentence stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    if (trimmed.length > 0)
      [sentences addObject:trimmed];
  }];
  if (sentences.count == 0)
    [sentences addObject:text];
  for (NSString * sentence in sentences)
  {
    AVSpeechUtterance * utterance = [AVSpeechUtterance speechUtteranceWithString:sentence];
    utterance.voice = self.speechVoice;
    utterance.rate = AVSpeechUtteranceDefaultSpeechRate;
    utterance.preUtteranceDelay = 0.1;
    utterance.postUtteranceDelay = 0.4;
    [self.speechSynthesizer speakUtterance:utterance];
  }
}

- (void)playRouteNotifications:(NSArray<NSString *> *)routeNotifications {
  auto stopSession = ^{
    if (self.speechSynthesizer.isSpeaking || BackseatKokoroVoice.shared.isBusy || BackseatListener.shared.isListening)
      return;
    [[AVAudioSession sharedInstance]
          setActive:NO
        withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation
              error:nil];
  };

  if (![MWMRouter isOnRoute] || !self.allowsSpokenNotifications) {
    stopSession();
    return;
  }

  if (![self isValid])
    [self createVoice:[[self class] savedLanguage]];

  if (![self isValid]) {
    stopSession();
    return;
  }

  if (routeNotifications.count == 0) {
    stopSession();
    return;
  } else {
    NSError * err = nil;
    if (![[AVAudioSession sharedInstance] setActive:YES error:&err]) {
      LOG(LWARNING, ("Couldn't activate audio session: ", [err localizedDescription]));
      return;
    }

    // Backseat Driver: Kokoro says the lines when it's ready. Navigation lines never wait on it:
    // if Kokoro can't start one quickly, the system voice says it.
    auto & backseat = GetFramework().GetRoutingManager().GetBackseat();
    auto const & voices = backseat.LastLineVoices();
    BOOL const hinted = self.allowsTurnInstructions && voices.size() == routeNotifications.count;
    BackseatKokoroVoice * kokoro = BackseatKokoroVoice.shared;
    // A fake phone call that will put someone on just started: get their voice ready.
    auto const callerVoice = backseat.TakeCallerVoiceToWarm();
    if (!callerVoice.empty())
      [kokoro prepareVoice:@(callerVoice.c_str())];
    for (NSUInteger i = 0; i < routeNotifications.count; ++i)
    {
      // While Kokoro loads, roasts wait for it and turn instructions go to the system voice.
      NSString * notification = routeNotifications[i];
      // Someone else talking (the caller on the phone): only ever Kokoro, never the system voice.
      if (hinted && !voices[i].voice.empty())
      {
        [kokoro speak:notification voice:@(voices[i].voice.c_str()) speed:static_cast<float>(voices[i].speed)];
        continue;
      }
      BOOL const nav = hinted ? voices[i].nav : YES;
      float const speed = hinted ? static_cast<float>(voices[i].speed) : 1.0f;
      double const maxWaitMs = hinted ? voices[i].maxWaitMs : 0;
      __weak MWMTextToSpeech * weakSelf = self;
      [kokoro speak:notification nav:nav speed:speed maxWaitMs:maxWaitMs fallback:^{
        [weakSelf speakOneString:notification];
      }];
    }
    // "Are you actually intending to walk there?": Yes / No on screen too.
    BOOL const askWalk = backseat.TakeModeQuestion();
    // "No": "I'm going to need you to click the driving button."
    BOOL const highlightDriving = !backseat.TakeTravelModeSwitch().empty();
    dispatch_async(dispatch_get_main_queue(), ^{
      if (askWalk)
        [MWMBackseatRewriter showWalkQuestion];
      [MWMBackseatRewriter closeWalkQuestionIfAnswered];
      if (highlightDriving)
        [MWMBackseatRewriter highlightDrivingButton];
    });
    // 20 Questions: the driver answers out loud, so listen once the GPS has said its piece.
    for (NSUInteger i = 0; hinted && i < voices.size(); ++i)
    {
      if (voices[i].listen)
      {
        [BackseatListener.shared listenForGameAnswer];
        break;
      }
    }
  }
}

- (void)playWarningSound {
  if (!GetFramework().GetRoutingManager().GetSpeedCamManager().ShouldPlayBeepSignal())
    return;

  if (self.allowsSpeedCameraWarnings)
    [self.audioPlayer play];
}

- (AVAudioPlayer *)audioPlayer {
  if (!_audioPlayer) {
    if (auto url = [[NSBundle mainBundle] URLForResource:@"Alert 5" withExtension:@"m4a"]) {
      NSError * error = nil;
      _audioPlayer = [[AVAudioPlayer alloc] initWithContentsOfURL:url error:&error];
      CHECK(!error, (error.localizedDescription.UTF8String));
    } else {
      CHECK(false, ("Speed warning file not found"));
    }
  }

  return _audioPlayer;
}

- (void)play:(NSString *)text {
  if (![self isValid])
    [self createVoice:[[self class] savedLanguage]];
  
  [self speakOneString:text];
}

#pragma mark - MWMNavigationDashboardObserver

- (void)onTTSStatusUpdated {
  for (Observer observer in self.observers)
    [observer onTTSStatusUpdated];
}

#pragma mark - Add/Remove Observers

+ (void)addObserver:(id<MWMTextToSpeechObserver>)observer {
  [[self tts].observers addObject:observer];
}

+ (void)removeObserver:(id<MWMTextToSpeechObserver>)observer {
  [[self tts].observers removeObject:observer];
}

@end

namespace tts
{
std::string translateLocale(std::string const & localeString)
{
  NSString * nsLocaleString = [NSString stringWithUTF8String: localeString.c_str()];
  NSLocale * locale = [[NSLocale alloc] initWithLocaleIdentifier: nsLocaleString];
  NSString * localizedName = [locale localizedStringForLocaleIdentifier:nsLocaleString];
  localizedName = [localizedName capitalizedString];
  return std::string(localizedName.UTF8String);
}
}  // namespace tts
