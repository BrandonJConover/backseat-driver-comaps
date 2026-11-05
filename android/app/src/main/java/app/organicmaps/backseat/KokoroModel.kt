package app.organicmaps.backseat

import android.content.Context
import android.content.res.AssetManager
import app.organicmaps.sdk.util.log.Logger
import java.io.File
import java.io.FileNotFoundException
import java.io.IOException
import java.util.concurrent.atomic.AtomicBoolean

/**
 * The Kokoro model files for sherpa-onnx: Kokoro v1.0 (int8, or a fine-tune of it), English
 * lexicon, and the English part of espeak-ng's data for words the lexicon doesn't know (street
 * names, numbers).
 *
 * They ship with the app (android/kokoro_pack: a Play install-time asset pack, or the base APK for
 * sideloaded builds); the app never downloads a model. sherpa-onnx reads real files, so the first
 * launch after an install or an update with new weights copies them out of the APK once (about
 * 150 MB on disk).
 */
object KokoroModel
{
  private const val TAG = "KokoroModel"
  private const val NAME = "kokoro-int8-multi-lang-v1_0"
  private const val ASSET_DIR = "kokoro/$NAME"
  /** In the assets: "<weights id>:<archive sha256>". The copy is redone when it changes. */
  private const val WEIGHTS_ID = "WEIGHTS_ID"
  private const val COMPLETE_MARKER = ".complete"

  private val copying = AtomicBoolean(false)

  fun dir(context: Context) = File(File(context.filesDir, "kokoro"), NAME)

  /** True when the files on disk are the ones this build bundles. */
  fun isInstalled(context: Context): Boolean
  {
    val marker = File(dir(context), COMPLETE_MARKER)
    return marker.exists() && marker.readText() == bundledWeights(context.assets)
  }

  /**
   * Calls [onReady] (on a background thread) once the files are on disk: right away if they already
   * are, otherwise after copying them out of the app.
   */
  fun ensure(context: Context, onReady: () -> Unit)
  {
    val app = context.applicationContext
    if (isInstalled(app))
    {
      onReady()
      return
    }
    if (!copying.compareAndSet(false, true))
      return
    Thread({
      try
      {
        install(app)
        onReady()
      }
      catch (e: Exception)
      {
        Logger.e(TAG, "Could not unpack the bundled Kokoro voice; the system voice stays on", e)
      }
      finally
      {
        copying.set(false)
      }
    }, "KokoroUnpack").start()
  }

  private fun bundledWeights(assets: AssetManager): String? = try
  {
    assets.open("$ASSET_DIR/$WEIGHTS_ID").bufferedReader().use { it.readText().trim() }
  }
  catch (e: FileNotFoundException)
  {
    null
  }

  @Throws(IOException::class)
  private fun install(context: Context)
  {
    val assets = context.assets
    val weights = bundledWeights(assets) ?: throw IOException("This build has no bundled Kokoro voice")
    val target = dir(context)
    val staging = File(target.parentFile, "$NAME.partial")
    staging.deleteRecursively()
    staging.mkdirs()
    Logger.i(TAG, "Unpacking the bundled Kokoro voice ($weights)")
    copyTree(assets, ASSET_DIR, staging)
    if (!File(staging, "model.int8.onnx").exists() || !File(staging, "voices.bin").exists())
    {
      staging.deleteRecursively()
      throw IOException("The bundled Kokoro voice is missing model files")
    }
    File(staging, COMPLETE_MARKER).writeText(weights)
    target.deleteRecursively()
    if (!staging.renameTo(target))
      throw IOException("Could not move the model into place")
    Logger.i(TAG, "Kokoro voice installed in $target")
  }

  /** Copies an asset folder; `list` returns an empty array for a file. */
  private fun copyTree(assets: AssetManager, path: String, into: File)
  {
    val children = assets.list(path).orEmpty()
    if (children.isEmpty())
    {
      into.parentFile?.mkdirs()
      assets.open(path).use { input -> into.outputStream().use { input.copyTo(it, 1 shl 16) } }
      return
    }
    into.mkdirs()
    for (child in children)
      copyTree(assets, "$path/$child", File(into, child))
  }
}
