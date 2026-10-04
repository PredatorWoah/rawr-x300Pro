package com.rawr.camera.video

import android.media.MediaCodec
import android.media.MediaFormat
import androidx.media3.common.C
import androidx.media3.common.ColorInfo
import androidx.media3.common.Format
import androidx.media3.muxer.BufferInfo
import androidx.media3.muxer.FragmentedMp4Muxer
import java.io.File
import java.io.FileOutputStream
import java.nio.ByteBuffer
import java.nio.channels.WritableByteChannel
import java.util.PriorityQueue

/** Serializes compressed HEVC/AAC samples into short, recoverable MP4 fragments. */
@androidx.annotation.OptIn(androidx.media3.common.util.UnstableApi::class)
internal class FragmentedAvMuxer(private val stream: FileOutputStream, private val originNs: Long,
                                  private val rotationDegrees: Int = 0) : AutoCloseable {
    constructor(file: File, originNs: Long) : this(FileOutputStream(file), originNs, 0)
    enum class Track { Video, Audio }

    private data class Sample(val track: Track, val presentationTimeUs: Long, val flags: Int, val bytes: ByteArray)

    private val channel = object : WritableByteChannel {
        private var open = true
        override fun write(source: ByteBuffer): Int {
            var written = 0
            while (source.hasRemaining()) {
                val count = stream.channel.write(source)
                check(count > 0) { "Fragmented MP4 output stalled" }
                written += count
            }
            return written
        }
        override fun isOpen(): Boolean = open
        override fun close() {
            if (!open) return
            try {
                stream.fd.sync()
            } finally {
                open = false
                stream.close()
            }
        }
    }
    private val muxer = FragmentedMp4Muxer.Builder(channel)
        .setFragmentDurationMs(2_000)
        .setSampleCopyingEnabled(false)
        .build()
    private val pending = PriorityQueue<Sample>(compareBy<Sample> { it.presentationTimeUs }.thenBy { it.track.ordinal })
    private val formats = mutableMapOf<Track, MediaFormat>()
    private val ids = mutableMapOf<Track, Int>()
    private val latestPtsUs = mutableMapOf<Track, Long>()
    private val firstPtsUs = mutableMapOf<Track, Long>()
    private var closed = false
    private var written = 0L
    private var audioTrimmed = 0L

    @Synchronized
    fun setFormat(track: Track, format: MediaFormat) {
        check(!closed) { "Muxer closed" }
        check(track !in formats) { "Duplicate format for $track" }
        formats[track] = format
        if (formats.size == 2) {
            ids[Track.Video] = muxer.addTrack(toMuxFormat(Track.Video, requireNotNull(formats[Track.Video])))
            ids[Track.Audio] = muxer.addTrack(toMuxFormat(Track.Audio, requireNotNull(formats[Track.Audio])))
            flushReady(force = false)
        }
    }

    /** Copies only compressed bytes before returning the codec's output buffer. */
    @Synchronized
    fun write(track: Track, source: ByteBuffer, info: MediaCodec.BufferInfo, presentationTimeUs: Long = info.presentationTimeUs) {
        check(!closed) { "Muxer closed" }
        if (info.size <= 0 || info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG != 0) return
        val pts = (presentationTimeUs - originNs / 1_000L).coerceAtLeast(0)
        val firstVideo = firstPtsUs[Track.Video]
        // Media3's fragmented writer emits trun durations without a per-track
        // start offset. Align the first AAC packet to the first video frame so
        // separate track timelines cannot erase a several-hundred-ms startup
        // delay between microphone and Vulkan encoder Surface.
        if (track == Track.Audio && firstVideo != null && pts < firstVideo) {
            audioTrimmed++
            return
        }
        if (track == Track.Video && firstVideo == null) {
            val before = pending.size
            pending.removeIf { it.track == Track.Audio && it.presentationTimeUs < pts }
            audioTrimmed += before - pending.size
            firstPtsUs.remove(Track.Audio)
            pending.filter { it.track == Track.Audio }.minOfOrNull { it.presentationTimeUs }?.let {
                firstPtsUs[Track.Audio] = it
            }
        }
        val view = source.duplicate()
        view.position(info.offset)
        view.limit(info.offset + info.size)
        val bytes = ByteArray(info.size)
        view.get(bytes)
        val flags = if (info.flags and MediaCodec.BUFFER_FLAG_KEY_FRAME != 0) C.BUFFER_FLAG_KEY_FRAME else 0
        pending.add(Sample(track, pts, flags, bytes))
        firstPtsUs.putIfAbsent(track, pts)
        latestPtsUs[track] = maxOf(latestPtsUs[track] ?: 0, pts)
        // A failed or stalled track must not cause unbounded compressed-data
        // growth. The recorder treats this as a fatal error and closes cleanly.
        check(pending.size <= 256) { "Encoded sample interleave queue exceeded 256 samples" }
        flushReady(force = false)
    }

    @Synchronized
    fun sampleCount(): Long = written

    @Synchronized
    fun timingStats(): Map<String, Long> = mapOf(
        "firstVideoUs" to (firstPtsUs[Track.Video] ?: -1L),
        "lastVideoUs" to (latestPtsUs[Track.Video] ?: -1L),
        "firstAudioUs" to (firstPtsUs[Track.Audio] ?: -1L),
        "lastAudioUs" to (latestPtsUs[Track.Audio] ?: -1L),
        "trimmedAudioPackets" to audioTrimmed,
        "writtenSamples" to written)

    private fun flushReady(force: Boolean) {
        if (ids.size != 2) return
        while (pending.isNotEmpty()) {
            val sample = requireNotNull(pending.peek())
            if (!force) {
                val other = if (sample.track == Track.Video) Track.Audio else Track.Video
                val otherLatest = latestPtsUs[other] ?: break
                if (otherLatest + 500_000L < sample.presentationTimeUs) break
            }
            pending.remove()
            muxer.writeSampleData(requireNotNull(ids[sample.track]), ByteBuffer.wrap(sample.bytes),
                BufferInfo(sample.presentationTimeUs, sample.bytes.size, sample.flags))
            written++
        }
    }

    @Synchronized
    override fun close() {
        if (closed) return
        closed = true
        try {
            check(ids.size == 2) { "Both encoded tracks must have a format before finalization" }
            flushReady(force = true)
            muxer.close()
        } finally {
            channel.close()
        }
    }

    private fun toMuxFormat(track: Track, media: MediaFormat): Format {
        val builder = Format.Builder().setSampleMimeType(requireNotNull(media.getString(MediaFormat.KEY_MIME)))
        if (track == Track.Video) {
            builder.setWidth(media.getInteger(MediaFormat.KEY_WIDTH))
                .setHeight(media.getInteger(MediaFormat.KEY_HEIGHT))
                .setRotationDegrees(rotationDegrees)
            if (media.containsKey(MediaFormat.KEY_COLOR_STANDARD)) {
                builder.setColorInfo(ColorInfo.Builder()
                    .setColorSpace(media.getInteger(MediaFormat.KEY_COLOR_STANDARD))
                    .setColorRange(media.getInteger(MediaFormat.KEY_COLOR_RANGE))
                    .setColorTransfer(media.getInteger(MediaFormat.KEY_COLOR_TRANSFER))
                    .build())
            }
        } else {
            builder.setSampleRate(media.getInteger(MediaFormat.KEY_SAMPLE_RATE))
                .setChannelCount(media.getInteger(MediaFormat.KEY_CHANNEL_COUNT))
        }
        val csd = buildList {
            for (index in 0..2) {
                val name = "csd-$index"
                if (!media.containsKey(name)) continue
                val data = requireNotNull(media.getByteBuffer(name)).duplicate()
                val bytes = ByteArray(data.remaining())
                data.get(bytes)
                add(bytes)
            }
        }
        if (csd.isNotEmpty()) builder.setInitializationData(csd)
        return builder.build()
    }
}
