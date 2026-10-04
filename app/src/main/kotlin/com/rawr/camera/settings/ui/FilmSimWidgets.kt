package com.rawr.camera.settings.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.rawr.camera.settings.model.FilmPreset

@Composable
internal fun FilmOptionLabel(text: String) {
    Text(
        text.uppercase(),
        style = MaterialTheme.typography.labelSmall,
        color = MaterialTheme.colorScheme.primary,
        fontWeight = FontWeight.SemiBold,
        modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp)
    )
}

/** 2-4 short options, inline. Replaces radio rows for tiny discrete lists. */
@Composable
internal fun FilmSegmentedRow(
    title: String,
    options: List<String>,
    selected: Int,
    onSelect: (Int) -> Unit
) {
    FilmOptionLabel(title)
    SingleChoiceSegmentedButtonRow(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp)
    ) {
        options.forEachIndexed { index, label ->
            SegmentedButton(
                selected = index == selected,
                onClick = { onSelect(index) },
                shape = SegmentedButtonDefaults.itemShape(index = index, count = options.size),
                label = { Text(label, maxLines = 1) }
            )
        }
    }
}

/** Short names (papers, formats, preset chips). Wraps or scrolls by caller choice. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun FilmChipFlowRow(
    options: List<String>,
    selected: Int,
    onSelect: (Int) -> Unit
) {
    FlowRow(
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp)
    ) {
        options.forEachIndexed { index, name ->
            FilterChip(
                selected = index == selected,
                onClick = { onSelect(index) },
                label = { Text(name) }
            )
        }
    }
}

/** Film stocks as a 2-column chip grid with Negative/Reversal headers. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun FilmStockGrid(
    options: List<String>,
    reversal: Set<Int>,
    selected: Int,
    onSelect: (Int) -> Unit
) {
    FilmStockGridPart("Color negative", options, (options.indices).filter { it !in reversal }, selected, onSelect)
    FilmStockGridPart("Reversal", options, (options.indices).filter { it in reversal }, selected, onSelect)
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun FilmStockGridPart(
    header: String,
    options: List<String>,
    indices: List<Int>,
    selected: Int,
    onSelect: (Int) -> Unit
) {
    if (indices.isEmpty()) return
    FilmOptionLabel(header)
    Column(modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp)) {
        indices.chunked(2).forEach { pair ->
            Row(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                modifier = Modifier.fillMaxWidth()
            ) {
                pair.forEach { index ->
                    FilterChip(
                        selected = index == selected,
                        onClick = { onSelect(index) },
                        label = { Text(options[index], maxLines = 1) },
                        modifier = Modifier.weight(1f)
                    )
                }
                if (pair.size == 1) {
                    androidx.compose.foundation.layout.Spacer(modifier = Modifier.weight(1f))
                }
            }
        }
    }
}

/** One-tap preset chips, grouped Factory / Yours. Same action as the preset list rows. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun FilmSimPresetChips(
    factory: List<FilmPreset>,
    user: List<FilmPreset>,
    selectedId: String?,
    onSelect: (String) -> Unit
) {
    Column(modifier = Modifier.padding(vertical = 8.dp)) {
        FilmOptionLabel("Factory")
        FlowRow(
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp)
        ) {
            factory.forEach { preset ->
                FilterChip(
                    selected = selectedId == preset.id,
                    onClick = { onSelect(preset.id) },
                    label = { Text(preset.name, maxLines = 1) }
                )
            }
        }
        if (user.isNotEmpty()) {
            FilmOptionLabel("Yours")
            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp)
            ) {
                user.forEach { preset ->
                    FilterChip(
                        selected = selectedId == preset.id,
                        onClick = { onSelect(preset.id) },
                        label = { Text(preset.name, maxLines = 1) }
                    )
                }
            }
        }
    }
}
/** Collapsible card for advanced slider groups. State survives rotation. */
@Composable
internal fun CollapsibleSettingsCard(
    title: String,
    summary: String? = null,
    initiallyExpanded: Boolean = false,
    content: @Composable ColumnScope.() -> Unit
) {
    var expanded by rememberSaveable { mutableStateOf(initiallyExpanded) }
    Column {
        SettingsRow(
            title = title,
            value = summary,
            trailing = { Text(if (expanded) "−" else "+", style = MaterialTheme.typography.titleMedium) },
            onClick = { expanded = !expanded }
        )
        if (expanded) {
            content()
        }
    }
}
