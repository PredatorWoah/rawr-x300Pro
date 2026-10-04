package com.rawr.camera.renderer

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import com.rawr.camera.ui.icons.Icons
import com.rawr.camera.ui.icons.automirrored.rounded.ArrowBack
import com.rawr.camera.ui.icons.rounded.Image
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SecondaryTabRow
import androidx.compose.material3.Tab
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import com.rawr.camera.settings.architecture.NavigateBack
import com.rawr.camera.settings.architecture.OpenSection
import com.rawr.camera.settings.architecture.SettingsDispatch
import com.rawr.camera.settings.model.ChoiceSelectorKind
import com.rawr.camera.settings.model.SettingsDestination
import com.rawr.camera.settings.model.SettingsSection
import com.rawr.camera.settings.model.SettingsUiState
import com.rawr.camera.settings.ui.ChoiceSelectorScreen
import com.rawr.camera.settings.ui.DevelopSettings
import com.rawr.camera.settings.ui.FilmSimDetailScreen
import com.rawr.camera.settings.ui.FilmSimSubPageScreen
import com.rawr.camera.settings.ui.LocalSettingsScrollSession
import com.rawr.camera.settings.ui.SettingsScrollMemory
import com.rawr.camera.settings.ui.SettingsScrollSession
import com.rawr.camera.settings.ui.SettingsSectionContent

private enum class RendererTab(val title: String, val section: SettingsSection) {
    Tone("Tone & color", SettingsSection.ImageTone),
    Develop("Develop", SettingsSection.Image),
    Film("Film Sim", SettingsSection.FilmSim),
    Jpeg("JPEG", SettingsSection.Jpeg)
}

private fun rendererTabFor(destination: SettingsDestination): RendererTab = when (destination) {
    is SettingsDestination.Section -> when (destination.section) {
        SettingsSection.FilmSim -> RendererTab.Film
        SettingsSection.Jpeg -> RendererTab.Jpeg
        SettingsSection.Image, SettingsSection.Demosaic, SettingsSection.Defringe,
        SettingsSection.HighlightReconstruction -> RendererTab.Develop
        else -> RendererTab.Tone
    }
    is SettingsDestination.FilmSimSubPage, is SettingsDestination.FilmSimDetailPage -> RendererTab.Film
    is SettingsDestination.ChoiceSelector -> when (destination.kind) {
        ChoiceSelectorKind.OutputColorSpace, ChoiceSelectorKind.TransferFunction, ChoiceSelectorKind.JpegChromaSubsampling -> RendererTab.Jpeg
        ChoiceSelectorKind.FilmOutputSpace -> RendererTab.Film
        else -> RendererTab.Tone
    }
    SettingsDestination.Home, is SettingsDestination.LensEditor -> RendererTab.Tone
}

internal fun isRendererTabRoot(destination: SettingsDestination): Boolean = when (destination) {
    is SettingsDestination.Section -> destination.section in setOf(
        SettingsSection.ImageTone, SettingsSection.Image, SettingsSection.Demosaic,
        SettingsSection.Defringe, SettingsSection.HighlightReconstruction,
        SettingsSection.FilmSim, SettingsSection.Jpeg
    )
    else -> false
}

internal fun rendererDestinationTitle(destination: SettingsDestination): String = when (destination) {
    SettingsDestination.Home -> "Settings"
    is SettingsDestination.Section -> destination.section.title
    is SettingsDestination.ChoiceSelector -> destination.kind.name
    is SettingsDestination.FilmSimSubPage -> destination.section.name
    is SettingsDestination.FilmSimDetailPage -> destination.detail.name
    is SettingsDestination.LensEditor -> "Lens"
}

private fun rendererDestinationKey(destination: SettingsDestination): String = when (destination) {
    SettingsDestination.Home -> "home"
    is SettingsDestination.Section -> "section:${destination.section.name}"
    is SettingsDestination.ChoiceSelector -> "selector:${destination.kind.name}"
    is SettingsDestination.FilmSimSubPage -> "filmsim:${destination.section.name}"
    is SettingsDestination.FilmSimDetailPage -> "filmdetail:${destination.detail.name}"
    is SettingsDestination.LensEditor -> "lens:${destination.lensName.orEmpty()}"
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun RendererSectionTabs(settings: SettingsUiState, busy: Boolean, dispatch: SettingsDispatch) {
    val current = rendererTabFor(settings.presentation.destination)
    val tabs = RendererTab.entries
    SecondaryTabRow(
        selectedTabIndex = tabs.indexOf(current),
        modifier = Modifier.fillMaxWidth().testTag("renderer_tabs")
    ) {
        tabs.forEach { tab ->
            Tab(
                selected = tab == current,
                enabled = !busy,
                onClick = { dispatch.invoke(OpenSection(tab.section)) },
                text = { Text(tab.title) },
                modifier = Modifier.testTag("renderer_tab_${tab.name}")
            )
        }
    }
}

@Composable
internal fun RendererTabContent(
    settings: SettingsUiState,
    dispatch: SettingsDispatch,
    busy: Boolean,
    onImportLut: (String?) -> Unit,
    modifier: Modifier = Modifier
) {
    val scrollMemory = remember { SettingsScrollMemory() }
    val destination = settings.presentation.destination
    CompositionLocalProvider(
        LocalSettingsScrollSession provides SettingsScrollSession(rendererDestinationKey(destination), scrollMemory)
    ) {
        Column(modifier.fillMaxSize()) {
            if (!isRendererTabRoot(destination)) {
                Row(
                    Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 4.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    IconButton(onClick = { if (!busy) dispatch.invoke(NavigateBack) }) {
                        Icon(Icons.AutoMirrored.Rounded.ArrowBack, contentDescription = "Back to ${rendererTabFor(destination).title}")
                    }
                    Text(rendererDestinationTitle(destination), style = MaterialTheme.typography.titleSmall)
                }
            }
            Box(Modifier.fillMaxWidth().weight(1f).testTag("renderer_tab_content")) {
                when (destination) {
                    is SettingsDestination.Section -> when (destination.section) {
                        SettingsSection.Image, SettingsSection.Demosaic, SettingsSection.Defringe,
                        SettingsSection.HighlightReconstruction ->
                            DevelopSettings(settings, dispatch)
                        SettingsSection.ImageTone, SettingsSection.FilmSim, SettingsSection.Jpeg, SettingsSection.LutProfile ->
                            SettingsSectionContent(settings, destination.section, dispatch, onImportLut = onImportLut)
                        else -> SettingsSectionContent(settings, SettingsSection.ImageTone, dispatch, onImportLut = onImportLut)
                    }
                    is SettingsDestination.ChoiceSelector -> ChoiceSelectorScreen(settings, destination.kind, dispatch)
                    is SettingsDestination.FilmSimSubPage -> FilmSimSubPageScreen(settings, destination.section, dispatch)
                    is SettingsDestination.FilmSimDetailPage -> FilmSimDetailScreen(settings, destination.detail, dispatch)
                    SettingsDestination.Home, is SettingsDestination.LensEditor ->
                        SettingsSectionContent(settings, SettingsSection.ImageTone, dispatch, onImportLut = onImportLut)
                }
            }
        }
    }
}
