package com.s3wear.buildlogic

import org.gradle.api.JavaVersion
import org.gradle.api.Project
import org.gradle.kotlin.dsl.configure
import org.jetbrains.kotlin.gradle.dsl.JvmTarget
import org.jetbrains.kotlin.gradle.dsl.KotlinAndroidProjectExtension

val JAVA_VERSION: JavaVersion = JavaVersion.VERSION_17

/** Kotlin settings shared by every Android module (AGP 9 built-in Kotlin). */
internal fun Project.configureKotlinAndroid() {
    extensions.configure<KotlinAndroidProjectExtension> {
        compilerOptions {
            jvmTarget.set(JvmTarget.fromTarget(JAVA_VERSION.toString()))
            allWarningsAsErrors.set(true)
        }
    }
}
