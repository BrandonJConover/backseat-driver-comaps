package app.organicmaps.sdk.backseat;

import android.app.ActivityManager;
import android.content.Context;
import android.database.ContentObserver;
import android.media.AudioManager;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.provider.Settings;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.UiThread;
import app.organicmaps.sdk.sound.TtsPlayer;

/// Backseat Driver hooks that aren't about rewording lines.
@androidx.annotation.Keep
public final class Backseat
{
  private Backseat() {}

  @Nullable
  private static AudioManager sAudio;

  /// Lets the GPS see whether music is playing ("Could we please turn on some music?").
  /// Call once the framework is up.
  @UiThread
  public static void init(@NonNull Context context)
  {
    sAudio = (AudioManager) context.getApplicationContext().getSystemService(Context.AUDIO_SERVICE);
    nativeEnableMusicCheck();
    watchVolume(context);
  }

  // "I won't be silenced!": turning the media volume down gets a comeback, said at the volume it
  // was before the presses, and then the phone goes back to the level the driver picked.

  /// Presses this close together are one turn-down.
  private static final long BURST_MS = 800;
  /// Never wait longer than this for the line to finish before handing the volume back.
  private static final long GIVE_BACK_MAX_MS = 15_000;
  /// Synthesis takes a moment; don't take "not speaking yet" for "done".
  private static final long GIVE_BACK_MIN_MS = 1_500;

  private static final Handler sMain = new Handler(Looper.getMainLooper());
  private static boolean sWatching;
  private static int sLastVolume = -1;
  /// The level before the first press of the current burst, or -1 when there's no burst.
  private static int sBurstFrom = -1;
  /// A level this class set itself; the change it causes isn't the driver's.
  private static int sSelfSet = -1;
  /// Where to put the volume back after the line; -1 when not in the middle of the bit.
  private static int sGiveBackTo = -1;

  private static void watchVolume(@NonNull Context context)
  {
    if (sWatching || sAudio == null)
      return;
    sWatching = true;
    sLastVolume = sAudio.getStreamVolume(AudioManager.STREAM_MUSIC);
    context.getApplicationContext().getContentResolver().registerContentObserver(
        Settings.System.CONTENT_URI, true, new ContentObserver(sMain) {
          @Override
          public void onChange(boolean selfChange)
          {
            onVolumeMaybeChanged();
          }
        });
  }

  private static final Runnable sBurstOver = Backseat::onBurstOver;

  private static void onVolumeMaybeChanged()
  {
    final int now = sAudio.getStreamVolume(AudioManager.STREAM_MUSIC);
    final int before = sLastVolume;
    if (now == before)
      return;
    sLastVolume = now;
    if (now == sSelfSet)
    {
      sSelfSet = -1;
      return;
    }
    // The driver touched the volume mid-bit: their level wins, and it stays.
    sGiveBackTo = -1;
    if (now < before && sBurstFrom < 0)
      sBurstFrom = before;
    if (sBurstFrom >= 0)
    {
      sMain.removeCallbacks(sBurstOver);
      sMain.postDelayed(sBurstOver, BURST_MS);
    }
  }

  private static void onBurstOver()
  {
    final int from = sBurstFrom;
    sBurstFrom = -1;
    final int to = sAudio.getStreamVolume(AudioManager.STREAM_MUSIC);
    if (from < 0 || to >= from || !TtsPlayer.isEnabled())
      return;
    final double max = Math.max(1, sAudio.getStreamMaxVolume(AudioManager.STREAM_MUSIC));
    final String line = nativeVolumeDown(from / max, to / max, inForeground());
    if (line.isEmpty())
      return;
    // Back up to where it was, with the system slider showing it jump.
    setVolume(from, AudioManager.FLAG_SHOW_UI);
    sGiveBackTo = to;
    TtsPlayer.INSTANCE.playTurnNotifications(new String[] {line}, new boolean[] {false}, new float[] {1.0f});
    final long started = SystemClock.elapsedRealtime();
    sMain.postDelayed(new Runnable() {
      @Override
      public void run()
      {
        if (sGiveBackTo < 0)
          return;
        if (TtsPlayer.INSTANCE.anyVoiceSpeaking() && SystemClock.elapsedRealtime() - started < GIVE_BACK_MAX_MS)
        {
          sMain.postDelayed(this, 300);
          return;
        }
        // Fine. The driver's level.
        setVolume(sGiveBackTo, 0);
        sGiveBackTo = -1;
      }
    }, GIVE_BACK_MIN_MS);
  }

  private static void setVolume(int level, int flags)
  {
    if (sAudio.getStreamVolume(AudioManager.STREAM_MUSIC) == level)
      return;
    try
    {
      sSelfSet = level;
      sAudio.setStreamVolume(AudioManager.STREAM_MUSIC, level, flags);
    }
    catch (SecurityException e)
    {
      // Do Not Disturb can refuse it; the line still plays at the lower level.
      sSelfSet = -1;
    }
  }

  private static boolean inForeground()
  {
    final ActivityManager.RunningAppProcessInfo info = new ActivityManager.RunningAppProcessInfo();
    ActivityManager.getMyMemoryState(info);
    return info.importance == ActivityManager.RunningAppProcessInfo.IMPORTANCE_FOREGROUND;
  }

  // Called from the core.
  @SuppressWarnings("unused")
  private static boolean isMusicActive()
  {
    return sAudio != null && sAudio.isMusicActive();
  }

  /// A destination the driver asked for out loud ("take me to work").
  public static final class Command
  {
    /// Where to route, when the GPS knows (home or work); null means search for {@link #name}.
    @Nullable
    public final double[] latLon;
    @NonNull
    public final String name;

    Command(@Nullable double[] latLon, @NonNull String name)
    {
      this.latLon = latLon;
      this.name = name;
    }
  }

  /// Something the driver said out loud, as text from speech recognition, e.g. "I see a Texas plate"
  /// or "take me to work". Returns the destination when it was a request to go somewhere.
  @UiThread
  @Nullable
  public static Command driverSaid(@NonNull String text)
  {
    final String heard = nativeDriverSaid(text);
    final String[] parts = heard.split("\t", -1);
    try
    {
      if (parts.length == 4 && parts[0].equals("route"))
        return new Command(new double[] {Double.parseDouble(parts[1]), Double.parseDouble(parts[2])}, parts[3]);
    }
    catch (NumberFormatException ignored)
    {
      return null;
    }
    if (parts.length == 2 && parts[0].equals("search"))
      return new Command(null, parts[1]);
    return null;
  }

  /// A line as Kokoro should read it (libs/backseat/pronunciation.hpp, shared with iOS): streets,
  /// numbers and times written out, "…" and dashes as pauses, and words spelled alike swapped for
  /// the sense the sentence needs ("Where do you live?"). Only for the voice; captions keep `text`.
  /// Any thread.
  @NonNull
  public static String spokenText(@NonNull String text)
  {
    return nativeSpokenText(text);
  }

  /// Kokoro's pronunciation dictionary, one "word\tphonemes" line per word (Misaki IPA).
  @NonNull
  public static String pronunciationLexicon()
  {
    return nativePronunciationLexicon();
  }

  private static final String PREFS = "backseat_pronunciations";
  private static final String KEY_LINES = "lines";
  private static boolean sPronunciationsLoaded;

  /// The driver's own pronunciations, one "word\tsay it as" line each, in the order added; they win
  /// over the built-in ones. Saved on the phone.
  @NonNull
  public static synchronized String userPronunciations(@NonNull Context context)
  {
    return context.getApplicationContext().getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY_LINES, "");
  }

  /// Saves the driver's pronunciations and hands them to the voice's rules at once. The Kokoro
  /// voice then needs to reload its dictionary (KokoroVoice.pronunciationsChanged).
  public static synchronized void setUserPronunciations(@NonNull Context context, @NonNull String lines)
  {
    context.getApplicationContext()
        .getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        .edit()
        .putString(KEY_LINES, lines)
        .apply();
    nativeSetUserPronunciations(lines);
    sPronunciationsLoaded = true;
  }

  /// Hands the saved pronunciations to the voice's rules, once. Call before the voice loads.
  public static synchronized void loadUserPronunciations(@NonNull Context context)
  {
    if (sPronunciationsLoaded)
      return;
    nativeSetUserPronunciations(userPronunciations(context));
    sPronunciationsLoaded = true;
  }

  private static native void nativeEnableMusicCheck();

  @NonNull
  private static native String nativeVolumeDown(double before, double after, boolean foreground);

  private static native void nativeSetUserPronunciations(@NonNull String lines);

  @NonNull
  private static native String nativeSpokenText(@NonNull String text);

  @NonNull
  private static native String nativePronunciationLexicon();

  @NonNull
  private static native String nativeDriverSaid(@NonNull String text);
}
