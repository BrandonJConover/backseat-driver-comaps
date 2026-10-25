package app.organicmaps.backseat

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.os.Build
import app.organicmaps.sdk.util.log.Logger
import org.apache.commons.compress.archivers.tar.TarArchiveInputStream
import org.apache.commons.compress.compressors.bzip2.BZip2CompressorInputStream
import java.io.BufferedInputStream
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.URL
import java.security.DigestInputStream
import java.security.MessageDigest
import java.util.concurrent.atomic.AtomicBoolean

/**
 * The Kokoro model files for sherpa-onnx: Kokoro v1.0 (int8), English lexicon, and the English part of
 * espeak-ng's data for words the lexicon doesn't know (street names, numbers).
 *
 * They are too big for the APK, so the first launch on an unmetered network downloads the release
 * archive once (about 130 MB) and keeps only the files below (about 150 MB on disk).
 */
object KokoroModel
{
  private const val TAG = "KokoroModel"
  private const val NAME = "kokoro-int8-multi-lang-v1_0"
  private const val URL_STRING =
    "https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/$NAME.tar.bz2"
  /** SHA-256 of the archive above, checked before the files are used. */
  private const val SHA256 = "4c3052abaa60943a341f193888cf6abd68787dae6ab8ae5c925a706caa247e4e"
  private const val COMPLETE_MARKER = ".complete"

  private val KEEP_FILES = setOf(
    "model.int8.onnx", "voices.bin", "tokens.txt", "lexicon-us-en.txt",
    "espeak-ng-data/phontab", "espeak-ng-data/phonindex", "espeak-ng-data/phondata",
    "espeak-ng-data/intonations", "espeak-ng-data/en_dict",
  )
  private const val KEEP_PREFIX = "espeak-ng-data/lang/gmw/en"

  private val downloading = AtomicBoolean(false)
  @Volatile private var networkCallback: ConnectivityManager.NetworkCallback? = null

  fun dir(context: Context) = File(File(context.filesDir, "kokoro"), NAME)

  fun isInstalled(context: Context) = File(dir(context), COMPLETE_MARKER).exists()

  /**
   * Calls [onReady] (on a background thread) once the files are on disk: right away if they already
   * are, otherwise after a download that waits for an unmetered network.
   */
  fun ensure(context: Context, onReady: () -> Unit)
  {
    val app = context.applicationContext
    if (isInstalled(app))
    {
      onReady()
      return
    }
    // The sherpa-onnx voice needs Android 7+; older phones keep the system voice.
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.N)
      return
    val cm = app.getSystemService(ConnectivityManager::class.java) ?: return
    if (!cm.isActiveNetworkMetered && cm.activeNetwork != null)
    {
      startDownload(app, onReady)
      return
    }
    if (networkCallback != null)
      return
    Logger.i(TAG, "Waiting for Wi-Fi to download the Kokoro voice")
    val callback = object : ConnectivityManager.NetworkCallback()
    {
      override fun onAvailable(network: Network)
      {
        cm.unregisterNetworkCallback(this)
        networkCallback = null
        startDownload(app, onReady)
      }
    }
    networkCallback = callback
    val request = NetworkRequest.Builder()
      .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
      .addCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED)
      .build()
    cm.registerNetworkCallback(request, callback)
  }

  private fun startDownload(context: Context, onReady: () -> Unit)
  {
    if (!downloading.compareAndSet(false, true))
      return
    Thread({
      try
      {
        download(context)
        onReady()
      }
      catch (e: Exception)
      {
        Logger.e(TAG, "Kokoro download failed; the system voice stays on", e)
      }
      finally
      {
        downloading.set(false)
      }
    }, "KokoroDownload").start()
  }

  @Throws(IOException::class)
  private fun download(context: Context)
  {
    val target = dir(context)
    val staging = File(target.parentFile, "$NAME.partial")
    staging.deleteRecursively()
    staging.mkdirs()
    Logger.i(TAG, "Downloading $URL_STRING")

    val digest = MessageDigest.getInstance("SHA-256")
    var connection = URL(URL_STRING).openConnection() as HttpURLConnection
    // GitHub redirects release downloads to its CDN.
    connection.instanceFollowRedirects = true
    connection.connectTimeout = 30_000
    connection.readTimeout = 60_000
    try
    {
      if (connection.responseCode != HttpURLConnection.HTTP_OK)
        throw IOException("HTTP ${connection.responseCode}")
      DigestInputStream(BufferedInputStream(connection.inputStream, 1 shl 16), digest).use { hashed ->
        extract(hashed, staging)
        // Read to the end so the hash covers the whole archive.
        val sink = ByteArray(1 shl 16)
        while (hashed.read(sink) >= 0) {}
      }
    }
    finally
    {
      connection.disconnect()
    }

    val actual = digest.digest().joinToString("") { "%02x".format(it) }
    if (actual != SHA256)
    {
      staging.deleteRecursively()
      throw IOException("Checksum mismatch: $actual")
    }
    if (!File(staging, "model.int8.onnx").exists() || !File(staging, "voices.bin").exists())
    {
      staging.deleteRecursively()
      throw IOException("Archive is missing model files")
    }
    File(staging, COMPLETE_MARKER).writeText(SHA256)
    target.deleteRecursively()
    if (!staging.renameTo(target))
      throw IOException("Could not move the model into place")
    Logger.i(TAG, "Kokoro voice installed in $target")
  }

  private fun extract(archive: InputStream, into: File)
  {
    // The archive's own streams must not close `archive`: its tail still has to be hashed.
    val tar = TarArchiveInputStream(BZip2CompressorInputStream(NonClosing(archive)))
    while (true)
    {
      val entry = tar.nextEntry ?: break
      if (entry.isDirectory)
        continue
      val name = entry.name.removePrefix("./").removePrefix("$NAME/")
      // Only exact, known names are written, so nothing in the archive can escape `into`.
      if (name !in KEEP_FILES && !(name.startsWith(KEEP_PREFIX) && !name.contains("..")))
        continue
      val out = File(into, name)
      out.parentFile?.mkdirs()
      out.outputStream().use { tar.copyTo(it, 1 shl 16) }
    }
  }

  private class NonClosing(input: InputStream) : java.io.FilterInputStream(input)
  {
    override fun close() {}
  }
}
