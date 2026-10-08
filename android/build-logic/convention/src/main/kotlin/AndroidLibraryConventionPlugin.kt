import com.android.build.api.dsl.LibraryExtension
import com.s3wear.buildlogic.JAVA_VERSION
import com.s3wear.buildlogic.configureKotlinAndroid
import com.s3wear.buildlogic.configureQuality
import com.s3wear.buildlogic.libs
import com.s3wear.buildlogic.versionInt
import org.gradle.api.Plugin
import org.gradle.api.Project
import org.gradle.kotlin.dsl.configure

class AndroidLibraryConventionPlugin : Plugin<Project> {
    override fun apply(target: Project) {
        with(target) {
            pluginManager.apply("com.android.library")

            extensions.configure<LibraryExtension> {
                compileSdk = libs.versionInt("compileSdk")
                defaultConfig {
                    minSdk = libs.versionInt("minSdk")
                }
                compileOptions {
                    sourceCompatibility = JAVA_VERSION
                    targetCompatibility = JAVA_VERSION
                }
            }
            configureKotlinAndroid()
            configureQuality()
        }
    }
}
