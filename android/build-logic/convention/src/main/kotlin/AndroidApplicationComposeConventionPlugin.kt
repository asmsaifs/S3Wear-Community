import com.android.build.api.dsl.ApplicationExtension
import com.s3wear.buildlogic.configureComposeDependencies
import org.gradle.api.Plugin
import org.gradle.api.Project
import org.gradle.kotlin.dsl.configure

class AndroidApplicationComposeConventionPlugin : Plugin<Project> {
    override fun apply(target: Project) {
        with(target) {
            pluginManager.apply("s3w.android.application")
            extensions.configure<ApplicationExtension> {
                buildFeatures.compose = true
            }
            configureComposeDependencies()
        }
    }
}
