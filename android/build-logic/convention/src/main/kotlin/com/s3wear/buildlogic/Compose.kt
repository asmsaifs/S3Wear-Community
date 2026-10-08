package com.s3wear.buildlogic

import org.gradle.api.Project
import org.gradle.kotlin.dsl.dependencies

/** Compose compiler plugin + BOM-aligned UI dependencies. */
internal fun Project.configureComposeDependencies() {
    pluginManager.apply("org.jetbrains.kotlin.plugin.compose")

    dependencies {
        val bom = platform(libs.library("androidx-compose-bom"))
        add("implementation", bom)
        add("implementation", libs.library("androidx-compose-ui"))
        add("implementation", libs.library("androidx-compose-ui-graphics"))
        add("implementation", libs.library("androidx-compose-ui-tooling-preview"))
        add("implementation", libs.library("androidx-compose-material3"))
        add("debugImplementation", libs.library("androidx-compose-ui-tooling"))
        add("debugImplementation", libs.library("androidx-compose-ui-test-manifest"))
        add("androidTestImplementation", bom)
    }
}
