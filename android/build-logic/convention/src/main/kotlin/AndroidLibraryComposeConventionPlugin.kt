import com.android.build.api.dsl.LibraryExtension
import com.s3wear.buildlogic.configureComposeDependencies
import org.gradle.api.Plugin
import org.gradle.api.Project
import org.gradle.kotlin.dsl.configure

class AndroidLibraryComposeConventionPlugin : Plugin<Project> {
    override fun apply(target: Project) {
        with(target) {
            pluginManager.apply("s3w.android.library")
            extensions.configure<LibraryExtension> {
                buildFeatures.compose = true
            }
            configureComposeDependencies()
        }
    }
}
