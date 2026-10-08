package com.s3wear.buildlogic

import io.gitlab.arturbosch.detekt.extensions.DetektExtension
import org.gradle.api.Project
import org.gradle.kotlin.dsl.configure
import org.jlleitschuh.gradle.ktlint.KtlintExtension

/** ktlint + detekt for every module; both hook into `check`. */
internal fun Project.configureQuality() {
    pluginManager.apply("org.jlleitschuh.gradle.ktlint")
    pluginManager.apply("io.gitlab.arturbosch.detekt")

    extensions.configure<KtlintExtension> {
        version.set(libs.findVersion("ktlint").get().requiredVersion)
        android.set(true)
    }

    extensions.configure<DetektExtension> {
        buildUponDefaultConfig = true
        config.setFrom(rootProject.file("config/detekt/detekt.yml"))
    }
}
