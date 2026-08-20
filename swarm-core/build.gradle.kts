plugins {
    kotlin("multiplatform") version "2.4.10"
}

kotlin {
    val hostOs = System.getProperty("os.name")
    val isMingwX64 = hostOs.startsWith("Windows")
    val isMac = hostOs == "Mac OS X"
    val isLinux = hostOs == "Linux"

    if (isMac) {
        macosArm64()
        macosX64()
    } else if (isLinux) {
        linuxX64()
        linuxArm64()
    } else if (isMingwX64) {
        mingwX64()
    }

    targets.withType<org.jetbrains.kotlin.gradle.plugin.mpp.KotlinNativeTarget> {
        binaries {
            executable("basalt-swarm") {
                entryPoint = "org.basaltmc.swarm.main"
            }
        }
    }

    sourceSets {
        commonMain.dependencies {
            implementation("io.ktor:ktor-network:3.0.0")
            implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.9.0")
        }
    }
}
