package com.ndsemu.app

import android.net.Uri
import android.os.Bundle
import android.view.KeyEvent
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import java.io.File
import kotlin.concurrent.thread

class MainActivity : AppCompatActivity() {

    private val core = NativeCore()
    private lateinit var emulatorView: EmulatorView
    private lateinit var titleView: TextView
    private var emuThread: EmuThread? = null
    private var romLoaded = false

    // On-screen buttons and a physical gamepad can be used at the same time.
    private var touchKeys = 0
    private var padKeys = 0

    private val romPicker =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) loadRom(uri)
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        emulatorView = findViewById(R.id.emulator)
        titleView = findViewById(R.id.title)
        findViewById<Button>(R.id.openRom).setOnClickListener { romPicker.launch(arrayOf("*/*")) }

        emulatorView.onTouch = { down, x, y -> core.setTouch(down, x, y) }
        findViewById<ButtonsView>(R.id.buttons).onKeys = {
            touchKeys = it
            core.setKeys(touchKeys or padKeys)
        }
    }

    private fun loadRom(uri: Uri) {
        stopEmulation()
        thread(name = "rom-loader") {
            // Copy to app storage so the native side can read the file by path
            // (large ROMs would not fit comfortably in the Java heap).
            val file = File(cacheDir, "game.nds")
            val ok = try {
                contentResolver.openInputStream(uri)?.use { input ->
                    file.outputStream().use { input.copyTo(it) }
                } != null && core.loadRom(file.absolutePath)
            } catch (e: Exception) {
                false
            }
            runOnUiThread {
                romLoaded = ok
                if (ok) {
                    titleView.text = core.gameTitle()
                    startEmulation()
                } else {
                    Toast.makeText(this, R.string.rom_failed, Toast.LENGTH_LONG).show()
                }
            }
        }
    }

    private fun startEmulation() {
        if (!romLoaded || emuThread != null) return
        emuThread = EmuThread(core, emulatorView).also { it.start() }
    }

    private fun stopEmulation() {
        emuThread?.stopAndJoin()
        emuThread = null
    }

    override fun onResume() {
        super.onResume()
        startEmulation()
    }

    override fun onPause() {
        stopEmulation()
        super.onPause()
    }

    override fun onDestroy() {
        stopEmulation()
        core.destroy()
        super.onDestroy()
    }

    // --- physical gamepad / keyboard ---

    private fun keyFor(code: Int): Int = when (code) {
        KeyEvent.KEYCODE_BUTTON_B -> NativeCore.KEY_A  // east button = DS A
        KeyEvent.KEYCODE_BUTTON_A -> NativeCore.KEY_B
        KeyEvent.KEYCODE_BUTTON_Y -> NativeCore.KEY_X
        KeyEvent.KEYCODE_BUTTON_X -> NativeCore.KEY_Y
        KeyEvent.KEYCODE_BUTTON_L1 -> NativeCore.KEY_L
        KeyEvent.KEYCODE_BUTTON_R1 -> NativeCore.KEY_R
        KeyEvent.KEYCODE_BUTTON_START -> NativeCore.KEY_START
        KeyEvent.KEYCODE_BUTTON_SELECT -> NativeCore.KEY_SELECT
        KeyEvent.KEYCODE_DPAD_UP -> NativeCore.KEY_UP
        KeyEvent.KEYCODE_DPAD_DOWN -> NativeCore.KEY_DOWN
        KeyEvent.KEYCODE_DPAD_LEFT -> NativeCore.KEY_LEFT
        KeyEvent.KEYCODE_DPAD_RIGHT -> NativeCore.KEY_RIGHT
        else -> 0
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        val k = keyFor(keyCode)
        if (k == 0) return super.onKeyDown(keyCode, event)
        padKeys = padKeys or k
        core.setKeys(touchKeys or padKeys)
        return true
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        val k = keyFor(keyCode)
        if (k == 0) return super.onKeyUp(keyCode, event)
        padKeys = padKeys and k.inv()
        core.setKeys(touchKeys or padKeys)
        return true
    }
}
