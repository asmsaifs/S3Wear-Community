pluginManagement {
    includeBuild("build-logic")
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "S3WearCompanion"

include(":app")
include(":core:designsystem")
include(":core:ble")
include(":core:protocol")
include(":core:data")
include(":core:domain")
include(":core:common")
include(":core:testing")
