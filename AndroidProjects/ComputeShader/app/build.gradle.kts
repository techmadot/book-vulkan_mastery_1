plugins {
    id("com.android.application")
}

android {
    namespace = "net.techbito.computeshader"
    compileSdk = 34

    defaultConfig {
        applicationId = "net.techbito.computeshader"
        minSdk = 33
        targetSdk = 34
        versionCode = 1
        versionName = "1.0"
        ndk {
            abiFilters.add("arm64-v8a")
            abiFilters.add("x86_64")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }
    buildFeatures {
        prefab = true
    }
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
}
tasks.register<Copy>("copyResToAssets") {
    description = "リソースデータをコピーします"
    from(File("../../../ComputeShader/res")) // コピー元のディレクトリ
    into(File("src/main/assets/res")) // コピー先のassetsディレクトリ
}
tasks.register<Copy>("copyValidationLayer") {
    description = "validation layerをコピーします"
    from(File("../../../AndroidProjects/validation_layer")) // コピー元のディレクトリ
    into(File("src/main/jniLibs")) // コピー先
}

// ビルド前タスクを設定
tasks.named("preBuild") {
    dependsOn("copyResToAssets")
    dependsOn("copyValidationLayer")
}
dependencies {

    implementation("androidx.appcompat:appcompat:1.6.1")
    implementation("com.google.android.material:material:1.11.0")
    implementation("androidx.games:games-activity:1.2.2")

}