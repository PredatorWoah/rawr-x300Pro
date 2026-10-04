package com.rawr.camera.video

import android.content.Context
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import org.json.JSONArray
import org.json.JSONObject
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Reads only container/track/timestamp metadata on device; never exports camera or mic samples. */
internal object VideoFileInspector {
    fun inspect(context: Context, uri: Uri): JSONObject {
        val report = JSONObject().put("uri", uri.toString())
        val extractor = MediaExtractor()
        try {
            extractor.setDataSource(context, uri, null)
            val tracks = JSONArray()
            for (track in 0 until extractor.trackCount) {
                val format = extractor.getTrackFormat(track)
                val detail = JSONObject()
                    .put("mime", format.getString(MediaFormat.KEY_MIME))
                    .put("format", format.toString())
                if (format.containsKey(MediaFormat.KEY_WIDTH))
                    detail.put("width", format.getInteger(MediaFormat.KEY_WIDTH))
                        .put("height", format.getInteger(MediaFormat.KEY_HEIGHT))
                if (format.containsKey(MediaFormat.KEY_SAMPLE_RATE))
                    detail.put("sampleRate", format.getInteger(MediaFormat.KEY_SAMPLE_RATE))
                if (format.containsKey(MediaFormat.KEY_CHANNEL_COUNT))
                    detail.put("channelCount", format.getInteger(MediaFormat.KEY_CHANNEL_COUNT))
                extractor.selectTrack(track)
                var first = -1L
                var last = -1L
                var samples = 0L
                var backwards = 0L
                var gapsOver100ms = 0L
                var maxGapUs = 0L
                var minDeltaUs = Long.MAX_VALUE
                val firstTimes = JSONArray()
                while (extractor.sampleTrackIndex >= 0 && samples < 1_000_000L) {
                    val pts = extractor.sampleTime
                    if (samples < 8) firstTimes.put(pts)
                    if (first < 0) first = pts
                    if (last >= 0) {
                        if (pts < last) backwards++
                        val gap = pts - last
                        if (gap > 100_000L) gapsOver100ms++
                        maxGapUs = maxOf(maxGapUs, gap)
                        if (gap > 0) minDeltaUs = minOf(minDeltaUs, gap)
                    }
                    last = pts
                    samples++
                    if (!extractor.advance()) break
                }
                detail.put("samples", samples).put("firstPtsUs", first).put("lastPtsUs", last)
                    .put("backwards", backwards).put("gapsOver100ms", gapsOver100ms)
                    .put("maxGapUs", maxGapUs).put("minDeltaUs", if (minDeltaUs == Long.MAX_VALUE) -1L else minDeltaUs)
                    .put("maxInstantFps", if (minDeltaUs == Long.MAX_VALUE || minDeltaUs <= 0) -1.0 else 1_000_000.0 / minDeltaUs)
                    .put("firstSampleTimesUs", firstTimes)
                if (last > first && first >= 0) {
                    val midpoint = first + (last - first) / 2
                    extractor.seekTo(midpoint, MediaExtractor.SEEK_TO_PREVIOUS_SYNC)
                    detail.put("midpointSeekTargetUs", midpoint)
                        .put("midpointSeekResultUs", extractor.sampleTime)
                }
                tracks.put(detail)
                extractor.unselectTrack(track)
                extractor.seekTo(0, MediaExtractor.SEEK_TO_CLOSEST_SYNC)
            }
            report.put("tracks", tracks)
        } finally {
            extractor.release()
        }
        context.contentResolver.openFileDescriptor(uri, "r")?.use { descriptor ->
            java.io.FileInputStream(descriptor.fileDescriptor).channel.use { channel ->
                val boxes = JSONArray()
                val header = ByteBuffer.allocate(16).order(ByteOrder.BIG_ENDIAN)
                val size = channel.size()
                var position = 0L
                while (position + 8 <= size && boxes.length() < 100_000) {
                    header.clear()
                    header.limit(8)
                    channel.position(position)
                    if (channel.read(header) != 8) break
                    header.flip()
                    var length = header.int.toLong() and 0xffff_ffffL
                    val typeBytes = ByteArray(4)
                    header.get(typeBytes)
                    val type = String(typeBytes, Charsets.US_ASCII)
                    var headerSize = 8L
                    if (length == 1L) {
                        header.clear()
                        header.limit(8)
                        if (channel.read(header) != 8) break
                        header.flip()
                        length = header.long
                        headerSize = 16L
                    } else if (length == 0L) length = size - position
                    if (length < headerSize || position + length > size) break
                    boxes.put(type)
                    position += length
                }
                report.put("bytes", size).put("topLevelBoxes", boxes)
                    .put("parsedBytes", position).put("completeBoxBoundary", position == size)
            }
        }
        return report
    }
}
