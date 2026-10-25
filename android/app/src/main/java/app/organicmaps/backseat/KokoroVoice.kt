package app.organicmaps.backseat

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTrack
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import androidx.annotation.RequiresApi
import app.organicmaps.sdk.sound.NeuralVoice
import app.organicmaps.sdk.sound.TtsPlayer
import app.organicmaps.sdk.util.log.Logger
import com.k2fsa.sherpa.onnx.OfflineTts
import com.k2fsa.sherpa.onnx.OfflineTtsConfig
import com.k2fsa.sherpa.onnx.OfflineTtsKokoroModelConfig
import com.k2fsa.sherpa.onnx.OfflineTtsModelConfig
import java.io.File
import java.util.concurrent.PriorityBlockingQueue
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference

/**
 * Backseat Driver's Kokoro voice on Android, via sherpa-onnx.
 *
 * One thread synthesizes, another plays. Roasts queue up and are dropped once stale. Navigation lines
 * go first and never wait: one that isn't ready to play within [NAV_DEADLINE_MS] is handed back to
 * the system voice, and its Kokoro audio is kept for the next time the same line comes up (countdown
 * lines repeat a lot). A roast being synthesized stops at its next sentence when a navigation line
 * arrives.
 */
object KokoroVoice : NeuralVoice
{
  private const val TAG = "KokoroVoice"
  private const val NAV_DEADLINE_MS = 1200L
  private const val ROAST_MAX_AGE_MS = 15_000L
  /** af_heart in sherpa-onnx's Kokoro v1.0 voice table. */
  private const val VOICE_ID = 3
  private const val CACHE_LINES = 32
  /** Longer clips aren't cached: they're one-off roasts, and they cost memory. */
  private const val CACHE_MAX_SECONDS = 6

  private enum class State { WAITING, PLAYING, FELL_BACK }

  private class Job(
    val text: String,
    val nav: Boolean,
    val speed: Float,
    val volume: Float,
    val fallback: Runnable,
    val generation: Int,
  ) : Comparable<Job>
  {
    val seq = sequence.incrementAndGet()
    val queuedAt = SystemClock.elapsedRealtime()
    val state = AtomicReference(State.WAITING)
    lateinit var deadline: Runnable

    // Navigation lines first, then oldest first.
    override fun compareTo(other: Job): Int =
      if (nav != other.nav) (if (nav) -1 else 1) else seq.compareTo(other.seq)
  }

  private val sequence = AtomicLong()
  private val generation = AtomicInteger()
  private val pendingNav = AtomicInteger()
  private val jobs = PriorityBlockingQueue<Job>()
  private val mainHandler = Handler(Looper.getMainLooper())
  private val cache = object : LinkedHashMap<String, Clip>(CACHE_LINES, 0.75f, true)
  {
    override fun removeEldestEntry(eldest: MutableMap.MutableEntry<String, Clip>) = size > CACHE_LINES
  }

  @Volatile private var tts: OfflineTts? = null
  @Volatile private var installed = false
  @Volatile private var player: ClipPlayer? = null

  /** Plugs the voice into [TtsPlayer] and loads (or first downloads) the model in the background. */
  @JvmStatic
  fun install(context: Context)
  {
    if (installed || Build.VERSION.SDK_INT < Build.VERSION_CODES.N)
      return
    installed = true
    val app = context.applicationContext
    player = ClipPlayer(app)
    TtsPlayer.INSTANCE.setNeuralVoice(this)
    KokoroModel.ensure(app) { start(app) }
  }

  override fun isReady() = tts != null

  override fun speak(text: String, nav: Boolean, speed: Float, volume: Float, fallback: Runnable)
  {
    val player = player
    if (tts == null || player == null)
    {
      fallback.run()
      return
    }
    val cached = synchronized(cache) { cache[key(text, speed)] }
    if (cached != null)
    {
      player.play(cached, volume, nav)
      return
    }
    val job = Job(text, nav, speed, volume, fallback, generation.get())
    if (nav)
    {
      pendingNav.incrementAndGet()
      job.deadline = Runnable {
        if (job.generation == generation.get() && job.state.compareAndSet(State.WAITING, State.FELL_BACK))
        {
          Logger.d(TAG, "Kokoro too slow for a navigation line, using the system voice")
          // The system voice must not talk over a roast.
          player.interruptRoast()
          job.fallback.run()
        }
      }
      mainHandler.postDelayed(job.deadline, NAV_DEADLINE_MS)
    }
    jobs.add(job)
  }

  override fun stop()
  {
    generation.incrementAndGet()
    jobs.clear()
    pendingNav.set(0)
    player?.stop()
  }

  private fun key(text: String, speed: Float) = "$speed|$text"

  // stop() resets the count, so never go below zero.
  private fun navDone() = pendingNav.updateAndGet { maxOf(0, it - 1) }

  private fun start(context: Context)
  {
    Thread({
      val engine = try
      {
        load(KokoroModel.dir(context))
      }
      catch (e: Throwable)
      {
        // Includes a missing or broken native library: the system voice just stays on.
        Logger.e(TAG, "Could not load Kokoro", e)
        return@Thread
      }
      tts = engine
      Logger.i(TAG, "Kokoro voice ready")
      while (true)
      {
        val job = try { jobs.take() } catch (e: InterruptedException) { return@Thread }
        try
        {
          synthesize(engine, job)
        }
        catch (e: Throwable)
        {
          Logger.e(TAG, "Kokoro failed on a line", e)
          if (!job.nav)
            job.fallback.run()
          else if (job.state.compareAndSet(State.WAITING, State.FELL_BACK))
          {
            mainHandler.removeCallbacks(job.deadline)
            job.fallback.run()
          }
        }
      }
    }, "KokoroSynth").start()
  }

  private fun load(dir: File): OfflineTts
  {
    val path = dir.absolutePath
    val config = OfflineTtsConfig(
      model = OfflineTtsModelConfig(
        kokoro = OfflineTtsKokoroModelConfig(
          model = "$path/model.int8.onnx",
          voices = "$path/voices.bin",
          tokens = "$path/tokens.txt",
          lexicon = "$path/lexicon-us-en.txt",
          dataDir = "$path/espeak-ng-data",
          lang = "en-us",
        ),
        numThreads = Runtime.getRuntime().availableProcessors().coerceIn(1, 4),
        provider = "cpu",
      ),
      maxNumSentences = 1,
    )
    val engine = OfflineTts(null, config)
    // The first generation is slow while onnxruntime warms up; pay for it now, not on a turn.
    engine.generate("Okay.", VOICE_ID, 1.0f)
    return engine
  }

  private fun synthesize(engine: OfflineTts, job: Job)
  {
    val player = player ?: return
    if (job.generation != generation.get())
    {
      if (job.nav)
        navDone()
      return
    }
    if (!job.nav && SystemClock.elapsedRealtime() - job.queuedAt > ROAST_MAX_AGE_MS)
      return

    var aborted = false
    val audio = engine.generateWithCallback(job.text, VOICE_ID, job.speed) {
      // Returning 0 stops synthesis: a navigation line is waiting.
      if (!job.nav && pendingNav.get() > 0) { aborted = true; 0 } else 1
    }
    if (job.nav)
      navDone()
    if (aborted)
    {
      // Try the roast again after the navigation line, unless it goes stale first.
      jobs.add(job)
      return
    }

    val clip = Clip.of(audio.samples, audio.sampleRate)
    if (job.nav || clip.seconds <= CACHE_MAX_SECONDS)
      synchronized(cache) { cache[key(job.text, job.speed)] = clip }

    if (job.nav)
    {
      mainHandler.removeCallbacks(job.deadline)
      // Too late: the system voice already said it. The clip stays cached for next time.
      if (!job.state.compareAndSet(State.WAITING, State.PLAYING))
        return
    }
    player.play(clip, job.volume, job.nav)
  }

  class Clip(val pcm: ShortArray, val sampleRate: Int)
  {
    val seconds get() = pcm.size / sampleRate.toFloat()

    companion object
    {
      fun of(samples: FloatArray, sampleRate: Int): Clip
      {
        val pcm = ShortArray(samples.size) { i -> (samples[i].coerceIn(-1f, 1f) * Short.MAX_VALUE).toInt().toShort() }
        return Clip(pcm, sampleRate)
      }
    }
  }

  /** Plays clips one at a time: navigation clips first, and they cut off a roast that's playing. */
  @RequiresApi(Build.VERSION_CODES.N)
  private class ClipPlayer(context: Context)
  {
    private class Item(val clip: Clip, val volume: Float, val nav: Boolean)
    {
      val queuedAt = SystemClock.elapsedRealtime()
      @Volatile var interrupted = false
    }

    private val lock = java.lang.Object()
    private val navQueue = ArrayDeque<Item>()
    private val roastQueue = ArrayDeque<Item>()
    private var current: Item? = null
    private val audioManager = context.getSystemService(AudioManager::class.java)
    private val attributes = AudioAttributes.Builder()
      .setUsage(AudioAttributes.USAGE_ASSISTANCE_NAVIGATION_GUIDANCE)
      .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
      .build()
    private val focusRequest: AudioFocusRequest? =
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
        AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK).setAudioAttributes(attributes).build()
      else null
    private var hasFocus = false

    init
    {
      Thread(::loop, "KokoroPlayer").start()
    }

    fun play(clip: Clip, volume: Float, nav: Boolean)
    {
      synchronized(lock) {
        if (nav)
        {
          navQueue.addLast(Item(clip, volume, true))
          current?.let { if (!it.nav) it.interrupted = true }
        }
        else
          roastQueue.addLast(Item(clip, volume, false))
        lock.notifyAll()
      }
    }

    fun interruptRoast()
    {
      synchronized(lock) {
        current?.let { if (!it.nav) it.interrupted = true }
        lock.notifyAll()
      }
    }

    fun stop()
    {
      synchronized(lock) {
        navQueue.clear()
        roastQueue.clear()
        current?.interrupted = true
        lock.notifyAll()
      }
    }

    private fun next(): Item
    {
      synchronized(lock) {
        while (true)
        {
          navQueue.removeFirstOrNull()?.let { return it }
          val roast = roastQueue.removeFirstOrNull()
          if (roast != null && SystemClock.elapsedRealtime() - roast.queuedAt <= ROAST_MAX_AGE_MS)
            return roast
          if (roast == null)
          {
            if (hasFocus)
              abandonFocus()
            lock.wait()
          }
        }
      }
    }

    @Suppress("DEPRECATION")
    private fun requestFocus()
    {
      if (hasFocus || audioManager == null)
        return
      hasFocus = if (focusRequest != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
        audioManager.requestAudioFocus(focusRequest) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED
      else
        audioManager.requestAudioFocus(null, AudioManager.STREAM_MUSIC, AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK) ==
          AudioManager.AUDIOFOCUS_REQUEST_GRANTED
    }

    @Suppress("DEPRECATION")
    private fun abandonFocus()
    {
      if (audioManager == null)
        return
      if (focusRequest != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
        audioManager.abandonAudioFocusRequest(focusRequest)
      else
        audioManager.abandonAudioFocus(null)
      hasFocus = false
    }

    private fun loop()
    {
      while (true)
      {
        val item = try { next() } catch (e: InterruptedException) { return }
        // Don't talk over the system voice (a navigation line it took over).
        val waitUntil = SystemClock.elapsedRealtime() + 8_000
        while (TtsPlayer.INSTANCE.isSpeaking() && SystemClock.elapsedRealtime() < waitUntil && !item.interrupted)
          Thread.sleep(50)
        if (item.interrupted)
          continue
        synchronized(lock) { current = item }
        try
        {
          playClip(item)
        }
        catch (e: Exception)
        {
          Logger.e(TAG, "Kokoro playback failed", e)
        }
        finally
        {
          synchronized(lock) { current = null }
        }
      }
    }

    private fun playClip(item: Item)
    {
      val clip = item.clip
      val track = AudioTrack.Builder()
        .setAudioAttributes(attributes)
        .setAudioFormat(
          AudioFormat.Builder()
            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
            .setSampleRate(clip.sampleRate)
            .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
            .build()
        )
        .setTransferMode(AudioTrack.MODE_STATIC)
        .setBufferSizeInBytes(clip.pcm.size * 2)
        .build()
      try
      {
        track.write(clip.pcm, 0, clip.pcm.size)
        track.setVolume(item.volume)
        synchronized(lock) { requestFocus() }
        track.play()
        val endBy = SystemClock.elapsedRealtime() + (clip.seconds * 1000).toLong() + 1000
        synchronized(lock) {
          while (!item.interrupted && track.playbackHeadPosition < clip.pcm.size && SystemClock.elapsedRealtime() < endBy)
            lock.wait(20)
        }
        track.stop()
      }
      finally
      {
        track.release()
      }
    }
  }
}
