package app.organicmaps.sdk.sound;

import androidx.annotation.NonNull;

/**
 * Backseat Driver: an on-device neural voice that {@link TtsPlayer} prefers over the system voice.
 * Implementations synthesize off the main thread and must never hold up a navigation line: when one
 * can't be started quickly they run {@code fallback}, which says it with the system voice instead.
 */
public interface NeuralVoice
{
  /** True once the model is loaded and lines can be spoken. */
  boolean isReady();

  /**
   * @param nav      a navigation line: say it now or hand it to {@code fallback}; other lines may wait
   *                 their turn or be dropped when stale
   * @param speed    speech rate, 1 = normal
   * @param volume   0..1
   * @param fallback says the line with the system voice; may be called on any thread
   */
  void speak(@NonNull String text, boolean nav, float speed, float volume, @NonNull Runnable fallback);

  /** Stops playback and forgets queued lines. */
  void stop();
}
