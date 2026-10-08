plugins {
    alias(libs.plugins.s3w.jvm.library)
    alias(libs.plugins.wire)
}

// Kotlin models generated at build time from the monorepo's protocol/proto
// (the single source of truth); nothing generated is committed here.
wire {
    sourcePath {
        srcDir(rootProject.file("../protocol/proto").path)
    }
    kotlin {}
}

dependencies {
    api(libs.wire.runtime)

    testImplementation(libs.wire.moshi.adapter)
    testImplementation(libs.moshi)
}

tasks.test {
    val vectors = rootProject.file("../protocol/testvectors")
    inputs.dir(vectors).withPathSensitivity(PathSensitivity.RELATIVE)
    systemProperty("s3w.testvectors", vectors.absolutePath)
}
