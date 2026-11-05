// Backseat Driver: the Kokoro voice (sherpa-onnx's Kokoro v1.0 int8 files) as a Play install-time
// asset pack. Play installs it with the app, so the app never downloads a model, and the base stays
// under Play's size limit. Sideloaded debug APKs get the same files in the base (app/build.gradle.kts).
//
// prepareKokoroAssets fetches sherpa-onnx's pinned release archive once into the Gradle cache (a
// build-time download; the app itself never downloads), keeps only the files KokoroModel needs,
// and, when kokoro_weights.tsv names a fine-tune, swaps in that fine-tune's model.int8.onnx from
// the local build-deps folder (model files never go on GitHub). Output: src/main/assets/kokoro/
// (generated, not checked in).

import java.net.URI
import org.gradle.kotlin.dsl.support.serviceOf
import java.security.MessageDigest

plugins {
    alias(libs.plugins.android.asset.pack)
}

assetPack {
    packName.set("kokoro_pack")
    dynamicDelivery {
        deliveryType.set("install-time")
    }
}

val sherpaKokoroName = "kokoro-int8-multi-lang-v1_0"
val sherpaKokoroUrl = "https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/$sherpaKokoroName.tar.bz2"
// Re-uploaded by k2-fsa on 2025-10-26; same model.int8.onnx and voices.bin as before.
val sherpaKokoroSha256 = "72c347230107ef8a31098ec4d468a605244beef5f1ed00db97efa13518800653"
val weightsTsv = rootDir.resolve("../kokoro_weights.tsv")
val buildDepsDir: String = providers.environmentVariable("BOOKLYRE_BUILD_DEPS_DIR").orNull
    ?: File(System.getProperty("user.home"), ".cache/booklyre/build-deps").path

val prepareKokoroAssets by tasks.registering {
    val archive = File(gradle.gradleUserHomeDir, "caches/backseat-kokoro/$sherpaKokoroName.tar.bz2")
    val outDir = layout.projectDirectory.dir("src/main/assets/kokoro").asFile
    val name = sherpaKokoroName
    val url = sherpaKokoroUrl
    val sha = sherpaKokoroSha256
    val tsv = weightsTsv
    val depsDir = buildDepsDir
    val archives = project.serviceOf<org.gradle.api.file.ArchiveOperations>()
    val files = project.serviceOf<org.gradle.api.file.FileSystemOperations>()
    inputs.property("archive", sha)
    inputs.file(weightsTsv)
    outputs.dir(outDir)
    doLast {
        fun sha256(file: File): String {
            val digest = MessageDigest.getInstance("SHA-256")
            file.inputStream().use { input ->
                val buffer = ByteArray(1 shl 16)
                while (true) {
                    val n = input.read(buffer)
                    if (n < 0) break
                    digest.update(buffer, 0, n)
                }
            }
            return digest.digest().joinToString("") { "%02x".format(it) }
        }
        if (!archive.isFile || sha256(archive) != sha) {
            archive.parentFile.mkdirs()
            val part = File(archive.path + ".part")
            logger.lifecycle("Downloading $url")
            URI(url).toURL().openStream().use { input -> part.outputStream().use { input.copyTo(it) } }
            check(sha256(part) == sha) { "$name.tar.bz2 failed its SHA-256 check" }
            part.renameTo(archive)
        }
        val target = File(outDir, name)
        outDir.deleteRecursively()
        // The same files KokoroModel keeps: graph, voices, tokens, US lexicon, English espeak-ng data.
        files.copy {
            from(archives.tarTree(archives.bzip2(archive)))
            include(
                "$name/model.int8.onnx", "$name/voices.bin", "$name/tokens.txt", "$name/lexicon-us-en.txt",
                "$name/espeak-ng-data/phontab", "$name/espeak-ng-data/phonindex", "$name/espeak-ng-data/phondata",
                "$name/espeak-ng-data/intonations", "$name/espeak-ng-data/en_dict", "$name/espeak-ng-data/lang/gmw/en*",
            )
            eachFile { path = path.removePrefix("$name/") }
            includeEmptyDirs = false
            into(target)
        }
        check(File(target, "model.int8.onnx").isFile && File(target, "voices.bin").isFile) { "$name archive is missing model files" }

        val rows = tsv.readLines().filter { it.isNotBlank() && !it.startsWith("#") }.map { it.split("\t") }
        val id = rows.firstOrNull { it[0] == "id" }?.getOrNull(1) ?: "stock"
        if (id != "stock") {
            val pin = rows.firstOrNull { it[0] == "sherpa/model.int8.onnx" }
                ?: error("kokoro_weights.tsv ($id) does not pin sherpa/model.int8.onnx")
            val source = File(depsDir, "$id/sherpa/model.int8.onnx")
            check(source.isFile) {
                "Kokoro weights $id not found at $source. Copy project files kokoro-finetune/ship/$id/ " +
                    "there or set BOOKLYRE_BUILD_DEPS_DIR (model files are never on GitHub)."
            }
            check(source.length() == pin[1].toLong() && sha256(source) == pin[2]) { "$source failed its SHA-256 check" }
            source.copyTo(File(target, "model.int8.onnx"), overwrite = true)
            logger.lifecycle("Bundling Kokoro weights $id")
        }
        // KokoroModel re-copies the files out of the APK when this changes (an update with new weights).
        File(target, "WEIGHTS_ID").writeText("$id:$sha\n")
    }
}
// Every packaging task of the pack needs the files first.
tasks.matching { it.name != prepareKokoroAssets.name && it.name != "clean" }.configureEach { dependsOn(prepareKokoroAssets) }
