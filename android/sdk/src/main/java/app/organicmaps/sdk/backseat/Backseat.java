package app.organicmaps.sdk.backseat;

import androidx.annotation.NonNull;
import androidx.annotation.UiThread;

/// Backseat Driver hooks that aren't about rewording lines.
@androidx.annotation.Keep
public final class Backseat
{
  private Backseat() {}

  /// Something the driver said out loud, as text from speech recognition, e.g. "I see a Texas plate".
  @UiThread
  public static void driverSaid(@NonNull String text)
  {
    nativeDriverSaid(text);
  }

  private static native void nativeDriverSaid(@NonNull String text);
}
