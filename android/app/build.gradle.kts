plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "com.egegurtunca.smartsafety"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.egegurtunca.smartsafety"
        minSdk = 26

        // targetSdk 33 bilincli bir secim: uygulama sideload ediliyor,
        // Play Store'a girmiyor. 34+ foregroundServiceType zorunlulugu ve
        // Android 15'in dataSync servislerine koydugu gunluk 6 saat siniri
        // 7/24 izleyen bir guvenlik uygulamasini bozardi.
        targetSdk = 33

        versionCode = 1
        versionName = "1.0"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }

    kotlinOptions {
        jvmTarget = "11"
    }

    buildFeatures {
        compose = true
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.15.0")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.8.7")
    implementation("androidx.activity:activity-compose:1.9.3")

    implementation(platform("androidx.compose:compose-bom:2024.12.01"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-core")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.9.0")
}
