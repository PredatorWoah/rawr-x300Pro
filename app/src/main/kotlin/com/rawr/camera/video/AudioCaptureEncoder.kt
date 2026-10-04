package com.rawr.camera.video

import android.media.AudioFormat
import android.media.AudioRecord
import android.media.AudioTimestamp
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.media.MediaRecorder
import android.os.SystemClock
import java.nio.ByteBuffer
import kotlin.math.max

/** Mic capture plus AAC encoding in Camera2's REALTIME/boot clock domain. */
internal class AudioCaptureEncoder @androidx.annotation.RequiresPermission(android.Manifest.permission.RECORD_AUDIO) constructor(
    private val muxer: FragmentedAvMuxer,
    private val onFailure: (Throwable) -> Unit,
    requestedChannels: Int = 2,
    requestedBitrate: Int = 192_000
) : AutoCloseable {
    private val sampleRate = 48_000
    private val actualChannels: Int
    private val actualBitrate: Int
    val audioFallback: Boolean
    private val bytesPerFrame: Int
    private val bufferBytes = 4_096
    private val record: AudioRecord
    private val codec: MediaCodec
    private val worker: Thread
    @Volatile private var running = false
    private var totalFrames = 0L
    private var anchorNs: Long? = null

    init {
        require(requestedChannels == 1 || requestedChannels == 2)
        require(requestedBitrate in 64_000..512_000)
        // Stereo-first with single mono fallback at 128k.
        val stereoOk = requestedChannels == 2 &&
            AudioRecord.getMinBufferSize(sampleRate, AudioFormat.CHANNEL_IN_STEREO,
                AudioFormat.ENCODING_PCM_16BIT) > 0
        actualChannels = if (stereoOk) 2 else 1
        audioFallback = actualChannels != requestedChannels
        actualBitrate = if (audioFallback) minOf(requestedBitrate, 128_000) else requestedBitrate
        bytesPerFrame = 2 * actualChannels
        val channelMask = if (actualChannels == 2) AudioFormat.CHANNEL_IN_STEREO else AudioFormat.CHANNEL_IN_MONO
        val minimum = AudioRecord.getMinBufferSize(sampleRate, channelMask,
            AudioFormat.ENCODING_PCM_16BIT)
        check(minimum > 0) { "48 kHz PCM16 microphone unsupported (channels=$actualChannels)" }
        record = AudioRecord.Builder()
            .setAudioSource(MediaRecorder.AudioSource.MIC)
            .setAudioFormat(AudioFormat.Builder()
                .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                .setSampleRate(sampleRate)
                .setChannelMask(channelMask).build())
            .setBufferSizeInBytes(max(minimum * 4, bufferBytes * 4)).build()
        if (record.state != AudioRecord.STATE_INITIALIZED) {
            record.release()
            error("Microphone initialization failed")
        }
        codec = try {
            MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_AUDIO_AAC)
        } catch (error: Throwable) {
            record.release()
            throw error
        }
        val format = MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_AAC, sampleRate, actualChannels).apply {
            setInteger(MediaFormat.KEY_AAC_PROFILE, MediaCodecInfo.CodecProfileLevel.AACObjectLC)
            setInteger(MediaFormat.KEY_BIT_RATE, actualBitrate)
            setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, bufferBytes)
        }
        try {
            codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
        } catch (error: Throwable) {
            codec.release()
            record.release()
            throw error
        }
        worker = Thread(::captureLoop, "RawrAudioCapture")
    }

    fun start() {
        check(!running) { "Audio already started" }
        codec.start()
        record.startRecording()
        check(record.recordingState == AudioRecord.RECORDSTATE_RECORDING) { "Microphone did not start" }
        running = true
        worker.start()
    }

    private fun captureLoop() {
        val pcm = ByteBuffer.allocateDirect(bufferBytes)
        val timestamp = AudioTimestamp()
        val info = MediaCodec.BufferInfo()
        try {
            while (running) {
                pcm.clear()
                val bytes = record.read(pcm, bufferBytes, AudioRecord.READ_BLOCKING)
                if (bytes <= 0) {
                    if (running) check(bytes == 0) { "AudioRecord.read failed: $bytes" }
                    continue
                }
                val count = bytes - bytes % bytesPerFrame
                val framesBefore = totalFrames
                val framesRead = count / bytesPerFrame
                totalFrames += framesRead
                val measuredFirstNs = if (record.getTimestamp(timestamp, AudioTimestamp.TIMEBASE_BOOTTIME) ==
                    AudioRecord.SUCCESS) {
                    timestamp.nanoTime + (framesBefore - timestamp.framePosition) * 1_000_000_000L / sampleRate
                } else {
                    SystemClock.elapsedRealtimeNanos() - framesRead * 1_000_000_000L / sampleRate
                }
                val measuredAnchor = measuredFirstNs - framesBefore * 1_000_000_000L / sampleRate
                anchorNs = anchorNs?.let { it + (measuredAnchor - it).coerceIn(-1_000_000L, 1_000_000L) }
                    ?: measuredAnchor
                val ptsUs = (requireNotNull(anchorNs) + framesBefore * 1_000_000_000L / sampleRate) / 1_000L
                var consumed = 0
                while (consumed < count && running) {
                    val index = codec.dequeueInputBuffer(10_000)
                    if (index < 0) {
                        drain(info, false)
                        continue
                    }
                    val input = requireNotNull(codec.getInputBuffer(index))
                    input.clear()
                    val part = minOf(input.remaining(), count - consumed)
                    val slice = pcm.duplicate()
                    slice.position(consumed)
                    slice.limit(consumed + part)
                    input.put(slice)
                    codec.queueInputBuffer(index, 0, part,
                        ptsUs + (consumed / bytesPerFrame) * 1_000_000L / sampleRate, 0)
                    consumed += part
                    drain(info, false)
                }
            }
            queueEos(info)
            drain(info, true)
        } catch (error: Throwable) {
            onFailure(error)
        } finally {
            runCatching { record.stop() }
            record.release()
            runCatching { codec.stop() }
            codec.release()
        }
    }

    private fun queueEos(info: MediaCodec.BufferInfo) {
        val endUs = ((anchorNs ?: SystemClock.elapsedRealtimeNanos()) +
            totalFrames * 1_000_000_000L / sampleRate) / 1_000L
        while (true) {
            val index = codec.dequeueInputBuffer(10_000)
            if (index >= 0) {
                codec.queueInputBuffer(index, 0, 0, endUs, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                return
            }
            drain(info, false)
        }
    }

    private fun drain(info: MediaCodec.BufferInfo, untilEos: Boolean) {
        val deadline = SystemClock.elapsedRealtimeNanos() + if (untilEos) 3_000_000_000L else 0L
        do {
            when (val index = codec.dequeueOutputBuffer(info, if (untilEos) 10_000 else 0)) {
                MediaCodec.INFO_OUTPUT_FORMAT_CHANGED ->
                    muxer.setFormat(FragmentedAvMuxer.Track.Audio, codec.outputFormat)
                in 0..Int.MAX_VALUE -> {
                    val eos = info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0
                    codec.getOutputBuffer(index)?.let { muxer.write(FragmentedAvMuxer.Track.Audio, it, info) }
                    codec.releaseOutputBuffer(index, false)
                    if (eos) return
                }
                else -> if (!untilEos) return
            }
        } while (!untilEos || SystemClock.elapsedRealtimeNanos() < deadline)
        if (untilEos) error("AAC encoder did not emit EOS in three seconds")
    }

    override fun close() {
        if (!running) return
        running = false
        runCatching { record.stop() }
        worker.join(5_000)
        check(!worker.isAlive) { "Audio capture worker did not stop" }
    }
}
