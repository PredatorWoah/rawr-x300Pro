package com.rawr.camera.renderer

import com.rawr.camera.settings.architecture.OpenSection
import com.rawr.camera.settings.architecture.PersistentSettingsController
import com.rawr.camera.settings.architecture.SettingsController
import com.rawr.camera.settings.model.SettingsCatalog
import com.rawr.camera.settings.model.SettingsSection
import com.rawr.camera.settings.model.SettingsValues
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

/** Independent recipe state. Staged Develop edits never change global user preferences. */
internal class RendererEditorSession(
    initial: SettingsValues,
    private val onDraftChanged: (SettingsValues) -> Unit
) {
    private val mutableApplied = MutableStateFlow(initial)
    val applied = mutableApplied.asStateFlow()
    private val mutableDirty = MutableStateFlow(false)
    val dirty = mutableDirty.asStateFlow()
    private val mutableController = MutableStateFlow<SettingsController>(createController(initial))
    val controller = mutableController.asStateFlow()

    private fun createController(initial: SettingsValues): SettingsController = PersistentSettingsController(
        SettingsCatalog.initialState().copy(values = initial),
        onValuesChanged = { edited ->
            if (mutableDirty.value) {
                val live = RendererDevelopStaging.appliedPreview(mutableApplied.value, edited)
                if (live != mutableApplied.value) publish(live)
            } else if (RendererDevelopStaging.isStagedChange(mutableApplied.value, edited)) {
                mutableDirty.value = true
            } else publish(edited)
        }
    ).apply { dispatch(OpenSection(SettingsSection.ImageTone)) }

    private fun publish(values: SettingsValues) {
        mutableApplied.value = values
        onDraftChanged(values)
    }

    fun apply() {
        mutableDirty.value = false
        publish(controller.value.state.value.values)
    }

    fun discard() {
        mutableDirty.value = false
        mutableController.value = createController(mutableApplied.value)
    }
}
