package com.s3wear.core.designsystem.theme

import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.LocalContext

private val DarkBrandColors =
    darkColorScheme(
        primary = Accent,
        onPrimary = White,
        background = Black,
        onBackground = White,
        surface = Surface,
        onSurface = White,
        surfaceVariant = SurfaceHi,
        onSurfaceVariant = TextDim,
        error = Danger
    )

private val LightBrandColors =
    lightColorScheme(
        primary = AccentDark,
        onPrimary = White,
        background = White,
        onBackground = Black,
        surface = White,
        onSurface = Black,
        surfaceVariant = LightSurface,
        onSurfaceVariant = LightOnSurfaceDim,
        error = DangerDark
    )

/**
 * App theme: Material 3 dynamic colour on Android 12+, S3Wear brand colours otherwise.
 */
@Composable
fun S3WearTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    dynamicColor: Boolean = true,
    content: @Composable () -> Unit
) {
    val colorScheme: ColorScheme =
        when {
            dynamicColor && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S -> {
                val context = LocalContext.current
                if (darkTheme) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)
            }

            darkTheme -> DarkBrandColors

            else -> LightBrandColors
        }

    MaterialTheme(
        colorScheme = colorScheme,
        typography = S3WearTypography,
        content = content
    )
}
