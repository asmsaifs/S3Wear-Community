plugins {
    alias(libs.plugins.s3w.android.application.compose)
    alias(libs.plugins.s3w.android.hilt)
}

android {
    namespace = "com.s3wear.companion"

    defaultConfig {
        applicationId = "com.s3wear.companion"
        versionCode = 1
        versionName = "0.1.0"
    }
}

dependencies {
    implementation(project(":core:designsystem"))
    implementation(project(":core:common"))

    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.lifecycle.runtime.ktx)
}
