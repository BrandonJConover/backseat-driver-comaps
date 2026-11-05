import org.gradle.api.initialization.resolve.RepositoriesMode
import java.net.URI

pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.PREFER_SETTINGS)
    repositories {
        google()
        mavenCentral()
        maven { url = URI("https://www.jitpack.io") } // MPAndroidChart
    }
}
rootProject.name = "CoMaps"
include(":app")
include(":sdk")
// Backseat Driver: the Kokoro voice model, delivered by Play as an install-time asset pack.
include(":kokoro_pack")
