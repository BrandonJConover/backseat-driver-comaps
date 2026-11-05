package app.organicmaps.backseat

import android.content.Context
import android.text.InputType
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView
import app.organicmaps.sdk.backseat.Backseat
import com.google.android.material.dialog.MaterialAlertDialogBuilder

/**
 * The driver's own pronunciations (Settings > Voice instructions > Pronunciations): a word and how
 * to say it, for every line the voice says. Saved by [Backseat.setUserPronunciations]; the core
 * applies them on top of its own (libs/backseat/pronunciation.cpp).
 */
object PronunciationsDialog
{
  private data class Entry(val word: String, val said: String)

  @JvmStatic
  fun show(context: Context)
  {
    val entries = load(context)
    val builder = MaterialAlertDialogBuilder(context)
      .setTitle("Pronunciations")
      .setPositiveButton("Add a word") { _, _ -> showAdd(context) }
      .setNegativeButton("Done", null)
    if (entries.isEmpty())
      builder.setMessage("No words yet. Add one I say wrong, and I'll say it your way from now on.")
    else
      builder.setItems(entries.map { "${it.word}  →  ${it.said}" }.toTypedArray()) { _, which ->
        showEntry(context, entries[which])
      }
    builder.show()
  }

  private fun showEntry(context: Context, entry: Entry)
  {
    MaterialAlertDialogBuilder(context)
      .setTitle(entry.word)
      .setMessage("Said like: ${entry.said}")
      .setPositiveButton("Hear it") { _, _ ->
        hear(entry.word)
        showEntry(context, entry)
      }
      .setNeutralButton("Delete") { _, _ ->
        save(context, load(context).filterNot { it.word.equals(entry.word, ignoreCase = true) })
        show(context)
      }
      .setNegativeButton("Back") { _, _ -> show(context) }
      .show()
  }

  private fun showAdd(context: Context)
  {
    val pad = (context.resources.displayMetrics.density * 20).toInt()
    val word = EditText(context).apply {
      hint = "Word or name, e.g. Quasi"
      inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_CAP_WORDS
      isSingleLine = true
    }
    val said = EditText(context).apply {
      hint = "Say it like, e.g. KWAH-zee"
      inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
      isSingleLine = true
    }
    val help = TextView(context).apply {
      text = "Write it how it sounds: plain words (\"wine sap\"), or syllables with dashes and the loud " +
        "one in capitals (\"KWAH-zee\"). It applies to everything I say. Adding a word again replaces it."
      setPadding(0, pad / 2, 0, 0)
    }
    val layout = LinearLayout(context).apply {
      orientation = LinearLayout.VERTICAL
      setPadding(pad, pad / 2, pad, 0)
      addView(word)
      addView(said)
      addView(help)
    }
    MaterialAlertDialogBuilder(context)
      .setTitle("Add a word")
      .setView(layout)
      .setPositiveButton("Add and hear it") { _, _ ->
        val w = word.text.toString().trim().replace('\t', ' ').replace('\n', ' ')
        val s = said.text.toString().trim().replace('\t', ' ').replace('\n', ' ')
        if (w.isNotEmpty() && s.isNotEmpty())
        {
          save(context, load(context).filterNot { it.word.equals(w, ignoreCase = true) } + Entry(w, s))
          hear(w)
        }
        show(context)
      }
      .setNegativeButton("Cancel") { _, _ -> show(context) }
      .show()
  }

  private fun load(context: Context): List<Entry> =
    Backseat.userPronunciations(context).lineSequence().mapNotNull { line ->
      val parts = line.split('\t')
      if (parts.size == 2 && parts[0].isNotBlank() && parts[1].isNotBlank()) Entry(parts[0], parts[1]) else null
    }.toList()

  private fun save(context: Context, entries: List<Entry>)
  {
    Backseat.setUserPronunciations(context, entries.joinToString("") { "${it.word}\t${it.said}\n" })
    KokoroVoice.pronunciationsChanged()
  }

  /** Says `word` in a short line, the way every other line will say it now. */
  private fun hear(word: String)
  {
    KokoroVoice.stop()
    KokoroVoice.speak("$word. Did I get it right?", false, 1.0f, 1.0f) {}
  }
}
