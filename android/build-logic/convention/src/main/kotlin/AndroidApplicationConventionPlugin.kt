import com.android.build.api.dsl.ApplicationExtension
import com.s3wear.buildlogic.JAVA_VERSION
import com.s3wear.buildlogic.configureKotlinAndroid
import com.s3wear.buildlogic.configureQuality
import com.s3wear.buildlogic.libs
import com.s3wear.buildlogic.versionInt
import org.gradle.api.Plugin
import org.gradle.api.Project
import org.gradle.kotlin.dsl.configure

class AndroidApplicationConventionPlugin : Plugin<Project> {
    override fun apply(target: Project) {
        with(target) {
            pluginManager.apply("com.android.application")

            extensions.configure<ApplicationExtension> {
                compileSdk = libs.versionInt("compileSdk")
                defaultConfig {
                    minSdk = libs.versionInt("minSdk")
                    targetSdk = libs.versionInt("targetSdk")
                }
                compileOptions {
                    sourceCompatibility = JAVA_VERSION
                    targetCompatibility = JAVA_VERSION
                }
                buildTypes {
                    getByName("release") {
                        isMinifyEnabled = true
                        isShrinkResources = true
                        proguardFiles(
                            getDefaultProguardFile("proguard-android-optimize.txt"),
                            "proguard-rules.pro",
                        )
                    }
                }
            }
            configureKotlinAndroid()
            configureQuality()
        }
    }
}
