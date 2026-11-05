package app.organicmaps.sdk.backseat;

import android.content.Context;
import android.media.AudioManager;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.UiThread;

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

  private static native void nativeSetUserPronunciations(@NonNull String lines);

  @NonNull
  private static native String nativeSpokenText(@NonNull String text);

  @NonNull
  private static native String nativePronunciationLexicon();

  @NonNull
  private static native String nativeDriverSaid(@NonNull String text);
}
