package com.s3wear.buildlogic

import org.gradle.api.Project
import org.gradle.api.artifacts.VersionCatalog
import org.gradle.api.artifacts.VersionCatalogsExtension
import org.gradle.kotlin.dsl.getByType

val Project.libs: VersionCatalog
    get() = extensions.getByType<VersionCatalogsExtension>().named("libs")

fun VersionCatalog.versionInt(alias: String): Int = findVersion(alias).get().requiredVersion.toInt()

fun VersionCatalog.library(alias: String) = findLibrary(alias).get()
