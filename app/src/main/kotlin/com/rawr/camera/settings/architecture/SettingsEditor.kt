package com.rawr.camera.settings.architecture

import com.rawr.camera.settings.model.SettingsValues
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.emitAll
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.launch

/** One optimistic snapshot for global settings; editor drafts deliberately use local controllers. */
class SettingsEditor(
    defaults: SettingsValues,
    persisted: Flow<SettingsValues>,
    private val save: suspend (SettingsValues) -> Unit,
    scope: CoroutineScope,
    private val onChanged: (SettingsValues) -> Unit = {}
) {
    private val mutableState = MutableStateFlow(defaults)
    val state = mutableState.asStateFlow()
    private val ready = CompletableDeferred<Unit>()
    /** Backend consumers wait for durable initialization instead of applying provisional defaults. */
    val values: Flow<SettingsValues> = flow { ready.await(); emitAll(state) }
    private val writes = Channel<SettingsValues>(Channel.CONFLATED)
    private val pending = mutableListOf<(SettingsValues) -> SettingsValues>()
    private var initialized = false
    private val mutableSaveFailure = MutableStateFlow<Throwable?>(null)
    val saveFailure = mutableSaveFailure.asStateFlow()

    init {
        scope.launch {
            try {
                val loaded = persisted.first()
                synchronized(this@SettingsEditor) {
                    val next = pending.fold(loaded) { values, edit -> edit(values) }
                    onChanged(next)
                    mutableState.value = next
                    initialized = true
                    if (pending.isNotEmpty()) writes.trySend(next)
                    pending.clear()
                    ready.complete(Unit)
                }
            } catch (e: Exception) {
                ready.completeExceptionally(e)
                mutableSaveFailure.value = e
            }
        }
        scope.launch {
            for (values in writes) {
                try {
                    save(values)
                    mutableSaveFailure.value = null
                } catch (e: kotlinx.coroutines.CancellationException) {
                    throw e
                } catch (e: Exception) {
                    mutableSaveFailure.value = e
                }
            }
        }
    }

    /** Transforms the latest snapshot atomically, so separate screens cannot overwrite each other. */
    @Synchronized
    fun update(edit: (SettingsValues) -> SettingsValues): SettingsValues {
        if (!initialized) pending += edit
        val before = mutableState.value
        val next = edit(before)
        if (next != before) {
            if (initialized) onChanged(next)
            mutableState.value = next
            if (initialized) writes.trySend(next)
        }
        return next
    }
}
