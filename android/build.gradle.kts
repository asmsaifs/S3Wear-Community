// Puts every plugin on the build classpath at one version; modules apply
// them through the s3w.* convention plugins in build-logic/.
plugins {
    alias(libs.plugins.android.application) apply false
    alias(libs.plugins.android.library) apply false
    // Not applied (AGP 9 has built-in Kotlin); pins the Kotlin Gradle plugin version.
    alias(libs.plugins.kotlin.android) apply false
    alias(libs.plugins.kotlin.jvm) apply false
    alias(libs.plugins.kotlin.compose) apply false
    alias(libs.plugins.ksp) apply false
    alias(libs.plugins.hilt) apply false
    alias(libs.plugins.ktlint) apply false
    alias(libs.plugins.detekt) apply false
    alias(libs.plugins.wire) apply false
}
