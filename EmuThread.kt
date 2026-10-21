package com.ndsemu.app

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack

/**
 * Runs the emulator one frame at a time. Pacing comes from the blocking
 * AudioTrack write: the core produces audio at the DS rate, so the loop
 * naturally runs at real-time speed (~59.8 fps).
 */
class EmuThread(private val core: NativeCore, private val view: EmulatorView) : Thread("emulator") {

    @Volatile
    var running = true

    override fun run() {
        val rate = 32768
        val minBuf = AudioTrack.getMinBufferSize(rate, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT)
        val track = AudioTrack.Builder()
            .setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build()
            )
            .setAudioFormat(
                AudioFormat.Builder()
                    .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                    .setSampleRate(rate)
                    .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                    .build()
            )
            .setTransferMode(AudioTrack.MODE_STREAM)
            .setBufferSizeInBytes(maxOf(minBuf, rate / 10 * 4))  // ~100 ms
            .build()
        track.play()

        val audio = ShortArray(4096)
        val frame = core.frameBuffer()
        try {
            while (running) {
                core.runFrame()
                view.present(frame)
                val n = core.takeAudio(audio)
                // Blocking write throttles us; fall back to a timer if audio is unavailable.
                val written = if (n > 0) track.write(audio, 0, n) else 0
                if (written <= 0) sleep(16)
            }
        } finally {
            track.stop()
            track.release()
        }
    }

    fun stopAndJoin() {
        running = false
        join()
    }
}
