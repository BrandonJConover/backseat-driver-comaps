package app.organicmaps.sdk.backseat;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.UiThread;

/// Backseat Driver hooks that aren't about rewording lines.
@androidx.annotation.Keep
public final class Backseat
{
  private Backseat() {}

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

  @NonNull
  private static native String nativeDriverSaid(@NonNull String text);
}
