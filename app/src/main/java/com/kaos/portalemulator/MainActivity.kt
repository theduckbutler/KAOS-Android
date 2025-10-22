package com.kaos.portalemulator

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.util.Log
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.LinearLayoutManager
import com.kaos.portalemulator.databinding.ActivityMainBinding
import java.io.File

class MainActivity : AppCompatActivity() {
    private lateinit var binding: ActivityMainBinding
    private lateinit var fileAdapter: DumpFileAdapter
    private var selectedDirectory: File? = null
    private val slots = mutableListOf<SlotState>()
    private var currentSlotIndex = 0
    private var gadgetActive = false
    private var daemonProcess: Process? = null
    private var daemonReady = false
    private var allReady = false

    private external fun nativeInit(): Int
    private external fun nativeSetSlotFile(slot: Int, path: String): Int
    private external fun nativeLoadSlot(slot: Int): Int
    private external fun nativeUnloadSlot(slot: Int): Int

    companion object {
        private const val TAG = "MainActivity"
        init {
            System.loadLibrary("portal_emulator")
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        for (i in 0 until 2) {
            slots.add(SlotState(i, null, false))
        }

        setupUI()
        checkPermissions()
        checkRootAccess()
        updateGadgetStatus()

        // Start capturing logcat to /sdcard/portal_debug.log for debugging
        try {
            logToFile()
            Log.i(TAG, "Started logcat capture to /sdcard/portal_debug.log")
        } catch (e: Exception) {
            Log.w(TAG, "Failed to start log capture: ${e.message}")
        }
    }

    private fun logToFile() {
        try {
            val logFile = File("/sdcard/portal_debug.log")

            // Optional: clear the buffer first
            Runtime.getRuntime().exec("logcat -c")

            // Start piping everything to the file
            Runtime.getRuntime().exec("logcat -f ${logFile.absolutePath} *:V")

        } catch (e: Exception) {
            e.printStackTrace()
        }

    }
    private fun setupUI() {
        fileAdapter = DumpFileAdapter { file ->
            assignFileToCurrentSlot(file)
        }
        binding.fileRecyclerView.apply {
            layoutManager = LinearLayoutManager(this@MainActivity)
            adapter = fileAdapter
        }

        binding.btnSelectDirectory.setOnClickListener {
            showPathInputDialog()
        }

        binding.btnSlot1.setOnClickListener { selectSlot(0) }
        binding.btnSlot2.setOnClickListener { selectSlot(1) }

        binding.btnLoadSlot.setOnClickListener { loadCurrentSlot() }
        binding.btnUnloadSlot.setOnClickListener { unloadCurrentSlot() }
        /// binding.btnSendSense.setOnClickListener { sendSense() }

        binding.btnStartGadget.setOnClickListener { startGadget() }
        binding.btnStopGadget.setOnClickListener { stopGadget() }
        binding.btnCleanup.setOnClickListener { performCleanup() }

        updateSlotDisplay()
    }

    private fun checkPermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                AlertDialog.Builder(this)
                    .setTitle("Storage Permission Required")
                    .setMessage("This app needs all files access to read NFC dumps")
                    .setPositiveButton("Grant") { _, _ ->
                        val intent = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION)
                        intent.data = Uri.parse("package:$packageName")
                        startActivity(intent)
                    }
                    .setNegativeButton("Cancel", null)
                    .show()
            }
        } else {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.READ_EXTERNAL_STORAGE)
                != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(arrayOf(Manifest.permission.READ_EXTERNAL_STORAGE), 100)
            }
        }
    }

    private fun checkRootAccess() {
        Thread {
            try {
                val process = Runtime.getRuntime().exec(arrayOf("su", "-c", "id"))
                val exitCode = process.waitFor()
                runOnUiThread {
                    if (exitCode == 0) {
                        binding.tvRootStatus.text = "Root: Available"
                        binding.tvRootStatus.setTextColor(getColor(android.R.color.holo_green_dark))
                    } else {
                        showRootError()
                    }
                }
            } catch (e: Exception) {
                runOnUiThread { showRootError() }
            }
        }.start()
    }

    private fun showRootError() {
        binding.tvRootStatus.text = "Root: NOT AVAILABLE"
        binding.tvRootStatus.setTextColor(getColor(android.R.color.holo_red_dark))
        AlertDialog.Builder(this)
            .setTitle("Root Required")
            .setMessage("This app requires root access. Please root your device first.")
            .setPositiveButton("OK", null)
            .show()
    }

    private fun showPathInputDialog() {
        val editText = android.widget.EditText(this).apply {
            hint = "/sdcard/skylanders"
            setText("/sdcard/skylanders")
        }

        AlertDialog.Builder(this)
            .setTitle("Enter Directory Path")
            .setMessage("Enter the full path to your NFC dump files:")
            .setView(editText)
            .setPositiveButton("OK") { _, _ ->
                val path = editText.text.toString().trim()
                if (path.isNotEmpty()) {
                    loadDirectory(File(path))
                }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun loadDirectory(dir: File) {
        if (!dir.exists() || !dir.isDirectory) {
            Toast.makeText(this, "Invalid directory: ${dir.absolutePath}", Toast.LENGTH_LONG).show()
            return
        }

        selectedDirectory = dir
        val files = dir.listFiles { file ->
            file.extension.lowercase() in listOf("bin", "dmp", "dump", "sky")
        }?.toList() ?: emptyList()

        fileAdapter.updateFiles(files)
        binding.tvDirectoryPath.text = "Directory: ${dir.absolutePath}"
        Toast.makeText(this, "Found ${files.size} dump files", Toast.LENGTH_SHORT).show()
    }

    private fun selectSlot(index: Int) {
        currentSlotIndex = index
        updateSlotDisplay()
    }

    private fun assignFileToCurrentSlot(file: File) {
        slots[currentSlotIndex].file = file
        updateSlotDisplay()

        val result = nativeSetSlotFile(currentSlotIndex, file.absolutePath)
        if (result == 0) {
            Toast.makeText(this, "File assigned to slot ${currentSlotIndex + 1}", Toast.LENGTH_SHORT).show()
        } else {
            Toast.makeText(this, "Failed to assign file", Toast.LENGTH_SHORT).show()
        }
    }

    private fun loadCurrentSlot() {
        if (slots[currentSlotIndex].file == null) {
            Toast.makeText(this, "No file assigned to slot", Toast.LENGTH_SHORT).show()
            return
        }

        val result = nativeLoadSlot(currentSlotIndex)
        if (result == 0) {
            slots[currentSlotIndex].loaded = true
            updateSlotDisplay()
            Toast.makeText(this, "Slot ${currentSlotIndex + 1} loaded", Toast.LENGTH_SHORT).show()
        } else {
            Toast.makeText(this, "Failed to load slot", Toast.LENGTH_SHORT).show()
        }
    }

    private fun unloadCurrentSlot() {
        val result = nativeUnloadSlot(currentSlotIndex)
        if (result == 0) {
            slots[currentSlotIndex].loaded = false
            updateSlotDisplay()
            Toast.makeText(this, "Slot ${currentSlotIndex + 1} unloaded", Toast.LENGTH_SHORT).show()
        } else {
            Toast.makeText(this, "Failed to unload slot", Toast.LENGTH_SHORT).show()
        }
    }

    private fun startGadget() {
        if (gadgetActive) {
            Toast.makeText(this, "Gadget already active", Toast.LENGTH_SHORT).show()
            return
        }

        binding.btnStartGadget.isEnabled = false
        binding.btnStartGadget.text = "Starting..."

        Thread {
            try {
                logToFile()

                // ========================================
                // STEP 1: Clean up any existing state
                // ========================================
                Log.d(TAG, "Step 1: Cleaning up any existing gadget")
                runRootScriptWithLogging(getGadgetCleanupScript())
                Thread.sleep(500)

                // ========================================
                // STEP 2: Verify UDC availability
                // ========================================
                Log.d(TAG, "Step 2: Checking for UDC")
                val (udcCheck, udcOutput) = runRootScriptWithOutput("ls /sys/class/udc/")
                if (udcCheck != 0 || udcOutput.trim().isEmpty()) {
                    runOnUiThread {
                        showError("No UDC found. Device may not support USB gadget mode.\nOutput: $udcOutput")
                        resetStartButton()
                    }
                    return@Thread
                }
                val udcName = udcOutput.trim().lines().first()
                Log.d(TAG, "UDC available: $udcName")

                // ========================================
                // STEP 3: Disable Android USB completely
                // ========================================
                Log.d(TAG, "Step 3: Disabling Android USB")
                val (disableResult, disableOutput) = runRootScriptWithOutput("""
                    # Stop Android USB services
                    stop adbd
                    
                    # Disable current USB config
                    setprop sys.usb.config none
                    setprop sys.usb.state none
                    
                    # Unbind ALL gadgets from UDC
                    UDC=$(ls /sys/class/udc | head -n1)
                    echo "Forcefully unbinding UDC: ${'$'}UDC"
                    
                    for gadget in /config/usb_gadget/*/UDC; do
                        if [ -f "${'$'}gadget" ]; then
                            CURRENT=$(cat "${'$'}gadget" 2>/dev/null)
                            if [ -n "${'$'}CURRENT" ]; then
                                echo "Unbinding gadget: ${'$'}gadget"
                                echo "" > "${'$'}gadget" 2>&1
                            fi
                        fi
                    done
                    
                    sleep 1
                    echo "USB disabled"
                """.trimIndent())

                if (disableResult != 0) {
                    Log.w(TAG, "USB disable had warnings: $disableOutput")
                }
                Log.d(TAG, "USB disable output: $disableOutput")
                Thread.sleep(1000)

                // ========================================
                // STEP 4: Setup gadget ConfigFS structure
                // ========================================
                Log.d(TAG, "Step 4: Setting up gadget structure")
                val (setupResult, setupOutput) = runRootScriptWithOutput(getGadgetSetupScript())
                if (setupResult != 0) {
                    runOnUiThread {
                        showError("Failed to setup gadget:\n$setupOutput")
                        resetStartButton()
                    }
                    return@Thread
                }
                Log.d(TAG, "Gadget structure created: $setupOutput")
                Thread.sleep(500)

                // ========================================
                // STEP 5: Mount FunctionFS AND fix permissions
                // ========================================
                Log.d(TAG, "Step 5: Mounting FunctionFS")
                val (mountResult, mountOutput) = runRootScriptWithOutput("""
                    # Create mount point
                    mkdir -p /dev/usb-ffs/portal0
                    
                    # Check if already mounted
                    if mount | grep -q "portal0"; then
                        echo "Already mounted, unmounting first..."
                        umount /dev/usb-ffs/portal0 2>/dev/null || true
                        sleep 0.5
                    fi
                    
                    # Mount FunctionFS
                    mount -t functionfs portal0 /dev/usb-ffs/portal0
                    
                    if [ ${'$'}? -ne 0 ]; then
                        echo "Failed to mount FunctionFS"
                        exit 1
                    fi
                    
                    echo "FunctionFS mounted successfully"
                    
                    # CRITICAL: Fix ep0 permissions immediately
                    for i in 1 2 3 4 5; do
                        if [ -e /dev/usb-ffs/portal0/ep0 ]; then
                            echo "ep0 appeared, setting permissions..."
                            chmod 666 /dev/usb-ffs/portal0/ep0
                            chown system:system /dev/usb-ffs/portal0/ep0 2>/dev/null || true
                            break
                        fi
                        echo "Waiting for ep0 to appear (attempt ${'$'}i)..."
                        sleep 0.2
                    done
                    
                    # Verify ep0 exists
                    if [ ! -e /dev/usb-ffs/portal0/ep0 ]; then
                        echo "ERROR: ep0 did not appear after mount"
                        exit 1
                    fi
                    
                    # Set SELinux permissive
                    setenforce 0 2>/dev/null || echo "Could not set SELinux permissive"
                    
                    echo "Mount and permissions complete"
                    ls -la /dev/usb-ffs/portal0/
                    exit 0
                """.trimIndent())

                if (mountResult != 0) {
                    runOnUiThread {
                        showError("Failed to mount FunctionFS:\n$mountOutput")
                        resetStartButton()
                    }
                    return@Thread
                }
                Log.d(TAG, "FunctionFS mounted: $mountOutput")
                Thread.sleep(300)

                // ========================================
                // STEP 6: Extract and start daemon executable
                // ========================================
                Log.d(TAG, "Step 6: Extracting portal daemon")

                val daemonDest = File(filesDir, "portal_daemon")

                try {
                    // Extract from assets based on ABI
                    val abi = Build.SUPPORTED_ABIS[0] // e.g., "arm64-v8a"
                    val assetPath = "$abi/portal_daemon"

                    Log.d(TAG, "Extracting daemon for ABI: $abi from assets: $assetPath")

                    assets.open(assetPath).use { input ->
                        daemonDest.outputStream().use { output ->
                            input.copyTo(output)
                        }
                    }

                    // Make executable
                    Runtime.getRuntime().exec(arrayOf("chmod", "755", daemonDest.absolutePath)).waitFor()

                    Log.d(TAG, "Daemon extracted to: ${daemonDest.absolutePath}")

                } catch (e: Exception) {
                    runOnUiThread {
                        showError("Failed to extract daemon: ${e.message}")
                        resetStartButton()
                    }
                    return@Thread
                }

                // Start daemon with su
                daemonProcess = Runtime.getRuntime().exec(arrayOf(
                    "su", "-c",
                    "nice -n -20 ${daemonDest.absolutePath} 2>/data/local/tmp/portal_daemon_err.log"
                ))

                // Monitor daemon output
                val reader = daemonProcess!!.inputStream.bufferedReader()
                daemonReady = false
                allReady = false

                Thread {
                    try {
                        reader.forEachLine { line ->
                            Log.d(TAG, "Daemon: $line")
                            if (line == "READY") {
                                daemonReady = true
                                Log.d(TAG, "✓ Daemon reports ep0 ready")
                            }
                            if (line == "ALL_READY") {
                                allReady = true
                                Log.d(TAG, "✓ Daemon reports all endpoints ready")
                            }
                            if (line.contains("FATAL") || line.contains("ERROR")) {
                                Log.e(TAG, "Daemon error: $line")
                            }
                        }
                        Log.e(TAG, "Daemon process ended unexpectedly!")
                    } catch (e: Exception) {
                        Log.e(TAG, "Daemon output error: ${e.message}")
                    }
                }.start()

                // ========================================
                // STEP 7: Wait for daemon ep0 ready
                // ========================================
                Log.d(TAG, "Step 7: Waiting for daemon to open ep0...")
                for (i in 0 until 15) {
                    if (daemonReady) {
                        Log.d(TAG, "✓ ep0 ready after ${i + 1} seconds")
                        break
                    }
                    Thread.sleep(1000)
                }

                if (!daemonReady) {
                    runOnUiThread {
                        showError("Timeout: Daemon failed to initialize ep0")
                        resetStartButton()
                    }
                    daemonProcess?.destroy()
                    cleanupFunctionFS()
                    return@Thread
                }

                // ========================================
                // STEP 8: Bind to UDC (creates ep1/ep2)
                // ========================================
                Log.d(TAG, "Step 8: Binding to UDC to create data endpoints...")
                val bindScript = """
                    UDC=$(ls /sys/class/udc | head -n1)
                    if [ -z "${'$'}UDC" ]; then
                        echo "ERROR: No UDC found"
                        exit 1
                    fi
                    
                    echo "Using UDC: ${'$'}UDC"
                    
                    # Verify not already bound
                    CURRENT=$(cat /config/usb_gadget/kaos_portal/UDC 2>/dev/null)
                    if [ -n "${'$'}CURRENT" ]; then
                        echo "WARNING: Already bound to ${'$'}CURRENT, unbinding..."
                        echo "" > /config/usb_gadget/kaos_portal/UDC 2>&1
                        sleep 0.5
                    fi
                    
                    # Bind to UDC
                    echo "Binding to ${'$'}UDC..."
                    if echo "${'$'}UDC" > /config/usb_gadget/kaos_portal/UDC 2>&1; then
                        echo "SUCCESS: Bound to UDC"
                        sleep 1
                        
                        # Verify endpoints created
                        if [ -e /dev/usb-ffs/portal0/ep1 ] && [ -e /dev/usb-ffs/portal0/ep2 ]; then
                            echo "Data endpoints created successfully"
                            exit 0
                        else
                            echo "ERROR: Data endpoints not created"
                            exit 1
                        fi
                    else
                        echo "ERROR: Bind failed"
                        dmesg | tail -20
                        exit 1
                    fi
                """.trimIndent()

                val (bindResult, bindOutput) = runRootScriptWithOutput(bindScript)
                Log.d(TAG, "Bind result (exit=$bindResult):\n$bindOutput")

                if (bindResult != 0 || !bindOutput.contains("SUCCESS")) {
                    runOnUiThread {
                        showError("Failed to bind to UDC:\n$bindOutput")
                        resetStartButton()
                    }
                    daemonProcess?.destroy()
                    cleanupFunctionFS()
                    return@Thread
                }

                // ========================================
                // STEP 9: Fix data endpoint permissions
                // ========================================
                Log.d(TAG, "Step 9: Setting permissions for data endpoints...")
                val (permResult, permOutput) = runRootScriptWithOutput("""
                    for i in 1 2 3 4 5; do
                        if [ -e /dev/usb-ffs/portal0/ep1 ] && [ -e /dev/usb-ffs/portal0/ep2 ]; then
                            chmod 666 /dev/usb-ffs/portal0/ep1 2>&1
                            chmod 666 /dev/usb-ffs/portal0/ep2 2>&1
                            chown system:system /dev/usb-ffs/portal0/ep1 2>/dev/null || true
                            chown system:system /dev/usb-ffs/portal0/ep2 2>/dev/null || true
                            echo "Permissions set"
                            break
                        fi
                        sleep 0.3
                    done
                    
                    ls -la /dev/usb-ffs/portal0/
                """.trimIndent())
                Log.d(TAG, "Data endpoint permissions: $permOutput")
                Thread.sleep(500)

                // ========================================
                // STEP 10: Wait for daemon to open data endpoints
                // ========================================
                Log.d(TAG, "Step 10: Waiting for daemon to open data endpoints...")
                for (i in 0 until 10) {
                    if (allReady) {
                        Log.d(TAG, "✓ All endpoints ready after ${i + 1} seconds")
                        break
                    }
                    Thread.sleep(1000)
                }

                // ========================================
                // FINAL: Update UI
                // ========================================
                runOnUiThread {
                    if (allReady) {
                        gadgetActive = true
                        updateGadgetStatus()
                        binding.btnStartGadget.isEnabled = false  // ADD THIS
                        binding.btnStartGadget.text = "Start Gadget"
                        Toast.makeText(this, "✓ Gadget started! Connect USB to host.", Toast.LENGTH_LONG).show()
                    } else {
                        showError("Timeout: Data endpoints failed to initialize")
                        daemonProcess?.destroy()
                        cleanupFunctionFS()
                        resetStartButton()
                    }
                }

            } catch (e: Exception) {
                e.printStackTrace()
                Log.e(TAG, "Exception during gadget start", e)
                runOnUiThread {
                    showError("Exception: ${e.message}\n${e.stackTraceToString().take(200)}")
                    resetStartButton()
                }
                try {
                    unbindUdc()
                    daemonProcess?.destroy()
                    cleanupFunctionFS()
                } catch (cleanupEx: Exception) {
                    Log.e(TAG, "Error during cleanup", cleanupEx)
                }
            }
        }.start()
    }

    private fun showError(message: String) {
        Log.e(TAG, "Error: $message")
        Toast.makeText(this, message, Toast.LENGTH_LONG).show()
        binding.btnStartGadget.isEnabled = true
        binding.btnStartGadget.text = "Start Gadget"
    }

    private fun stopGadget() {
        Log.e(TAG, "stopGadget() called! Stack trace:")
        Log.e(TAG, Thread.currentThread().stackTrace.joinToString("\n") { it.toString() })

        if (!gadgetActive) {
            Toast.makeText(this, "Gadget not active", Toast.LENGTH_SHORT).show()
            return
        }

        binding.btnStopGadget.isEnabled = false
        binding.btnStopGadget.text = "Stopping..."

        Thread {
            try {
                Log.d(TAG, "=== STOPPING GADGET ===")

                // ========================================
                // STEP 1: Unbind from UDC first
                // ========================================
                Log.d(TAG, "Step 1: Unbinding from UDC...")
                unbindUdc()
                Thread.sleep(500)

                // ========================================
                // STEP 2: Stop native emulator thread
                // ========================================
                Log.d(TAG, "Stopping daemon...")
                daemonProcess?.destroy()
                daemonProcess = null

                // ========================================
                // STEP 3: Cleanup native state
                // ========================================
                Log.d(TAG, "Step 3: Cleaning up native state...")
                daemonProcess?.destroy()
                Thread.sleep(300)

                // ========================================
                // STEP 4: Unmount FunctionFS
                // ========================================
                Log.d(TAG, "Step 4: Unmounting FunctionFS...")
                cleanupFunctionFS()
                Thread.sleep(300)

                // ========================================
                // STEP 5: Re-enable Android USB (optional)
                // ========================================
                Log.d(TAG, "Step 5: Re-enabling Android USB...")
                val (reEnableResult, reEnableOutput) = runRootScriptWithOutput("""
                # Restart ADB if desired
                # start adbd
                
                # Or restore Android's default USB configuration
                # setprop sys.usb.config adb
                
                echo "Cleanup complete"
            """.trimIndent())
                Log.d(TAG, "Re-enable output: $reEnableOutput")

                // ========================================
                // FINAL: Update UI
                // ========================================
                runOnUiThread {
                    gadgetActive = false
                    updateGadgetStatus()
                    binding.btnStopGadget.isEnabled = true
                    binding.btnStopGadget.text = "Stop Gadget"
                    Toast.makeText(this, "✓ Gadget stopped successfully", Toast.LENGTH_SHORT).show()
                    Log.d(TAG, "=== GADGET STOPPED ===")
                }

            } catch (e: Exception) {
                e.printStackTrace()
                Log.e(TAG, "Exception during gadget stop", e)
                runOnUiThread {
                    gadgetActive = false
                    updateGadgetStatus()
                    binding.btnStopGadget.isEnabled = true
                    binding.btnStopGadget.text = "Stop Gadget"
                    Toast.makeText(
                        this,
                        "Error stopping gadget: ${e.message}",
                        Toast.LENGTH_LONG
                    ).show()
                }
            }
        }.start()
    }

    private fun resetStartButton() {
        binding.btnStartGadget.isEnabled = true
        binding.btnStartGadget.text = "Start Gadget"
    }

    private fun unbindUdc() {
        val (result, output) = runRootScriptWithOutput("""
            UDC_PATH=/config/usb_gadget/kaos_portal/UDC
            if [ -f "${'$'}UDC_PATH" ]; then
                CURRENT=$(cat "${'$'}UDC_PATH" 2>/dev/null)
                if [ -n "${'$'}CURRENT" ]; then
                    echo "Unbinding from UDC: ${'$'}CURRENT"
                    echo "" > "${'$'}UDC_PATH" 2>&1
                    sleep 0.5
                    echo "Unbound successfully"
                else
                    echo "Already unbound"
                fi
            else
                echo "UDC file not found"
            fi
        """.trimIndent())
        Log.d(TAG, "Unbind UDC: $output")
    }

    private fun performCleanup() {
        AlertDialog.Builder(this)
            .setTitle("Cleanup USB Gadget")
            .setMessage("This will restore normal USB functionality. Continue?")
            .setPositiveButton("Yes") { _, _ ->
                Thread {
                    daemonProcess?.destroy()
                    val (cleanupResult, cleanupOutput) = runRootScriptWithOutput(getGadgetCleanupScript())

                    Log.d(TAG, "Cleanup result: $cleanupOutput")

                    runOnUiThread {
                        gadgetActive = false
                        updateGadgetStatus()
                        val msg = if (cleanupResult == 0) {
                            "Cleanup successful. You may need to replug USB or reboot."
                        } else {
                            "Cleanup completed with warnings:\n$cleanupOutput"
                        }
                        AlertDialog.Builder(this)
                            .setTitle("Cleanup Complete")
                            .setMessage(msg)
                            .setPositiveButton("OK", null)
                            .show()
                    }
                }.start()
            }
            .setNegativeButton("No", null)
            .show()
    }

    private fun updateSlotDisplay() {
        binding.btnSlot1.isSelected = (currentSlotIndex == 0)
        binding.btnSlot2.isSelected = (currentSlotIndex == 1)

        val slot = slots[currentSlotIndex]
        binding.tvCurrentSlot.text = "Slot ${currentSlotIndex + 1}"
        binding.tvSlotFile.text = slot.file?.name ?: "No file assigned"
        binding.tvSlotStatus.text = if (slot.loaded) "Loaded" else "Not loaded"
        binding.tvSlotStatus.setTextColor(
            getColor(if (slot.loaded) android.R.color.holo_green_dark else android.R.color.darker_gray)
        )
    }

    private fun updateGadgetStatus() {
        binding.tvGadgetStatus.text = if (gadgetActive) "Gadget: ACTIVE" else "Gadget: Inactive"
        binding.tvGadgetStatus.setTextColor(
            getColor(if (gadgetActive) android.R.color.holo_green_dark else android.R.color.darker_gray)
        )
        binding.btnStartGadget.isEnabled = !gadgetActive  // Start enabled only when inactive
        binding.btnStopGadget.isEnabled = gadgetActive     // Stop enabled only when active
    }

    private fun runRootScriptWithLogging(script: String): Int {
        val (exitCode, output) = runRootScriptWithOutput(script)
        if (exitCode != 0 || output.isNotEmpty()) {
            Log.d(TAG, "Script output (exit=$exitCode): $output")
        }
        return exitCode
    }

    private fun runRootScriptWithOutput(script: String): Pair<Int, String> {
        return try {
            val process = Runtime.getRuntime().exec(arrayOf("su", "-c", script))
            val output = process.inputStream.bufferedReader().readText()
            val error = process.errorStream.bufferedReader().readText()
            val exitCode = process.waitFor()

            val combined = buildString {
                if (output.isNotEmpty()) append(output)
                if (error.isNotEmpty()) {
                    if (isNotEmpty()) append("\n")
                    append("ERR: $error")
                }
            }

            Pair(exitCode, combined)
        } catch (e: Exception) {
            Pair(-1, "Exception: ${e.message}")
        }
    }

    private fun getGadgetSetupScript(): String {
        return """
            set -e
            
            GADGET_DIR="/config/usb_gadget/kaos_portal"
            
            if [ -d "${'$'}GADGET_DIR" ]; then
                echo "Cleaning up existing gadget..."
                if [ -f "${'$'}GADGET_DIR/UDC" ]; then
                    echo "" > "${'$'}GADGET_DIR/UDC" 2>/dev/null || true
                fi
                rm -rf "${'$'}GADGET_DIR/configs/c.1/ffs.portal0" 2>/dev/null || true
                rmdir "${'$'}GADGET_DIR/functions/ffs.portal0" 2>/dev/null || true
                rmdir "${'$'}GADGET_DIR/configs/c.1/strings/0x409" 2>/dev/null || true
                rmdir "${'$'}GADGET_DIR/configs/c.1" 2>/dev/null || true
                rmdir "${'$'}GADGET_DIR/strings/0x409" 2>/dev/null || true
                rmdir "${'$'}GADGET_DIR" 2>/dev/null || true
            fi
            
            echo "Creating gadget directory..."
            mkdir -p "${'$'}GADGET_DIR"
            cd "${'$'}GADGET_DIR"
            
            # Device descriptor
            echo 0x1430 > idVendor
            echo 0x0150 > idProduct
            echo 0x0200 > bcdUSB
            echo 0x0100 > bcdDevice
            echo 0x00 > bDeviceClass
            echo 0x00 > bDeviceSubClass
            echo 0x00 > bDeviceProtocol
            echo 64 > bMaxPacketSize0
            
            # Device strings - THESE ARE WHAT WINDOWS SEES
            mkdir -p strings/0x409
            echo "Activision" > strings/0x409/manufacturer
            echo "Spyro Porta" > strings/0x409/product
            echo "99B3f9C9E6" > strings/0x409/serialnumber
            
            # Configuration
            mkdir -p configs/c.1/strings/0x409
            echo "Portal Config" > configs/c.1/strings/0x409/configuration
            echo 250 > configs/c.1/MaxPower
            echo 0x80 > configs/c.1/bmAttributes
            
            # FunctionFS function
            mkdir -p functions/ffs.portal0
            
            # Link function
            ln -s "${'$'}GADGET_DIR/functions/ffs.portal0" "${'$'}GADGET_DIR/configs/c.1/ffs.portal0"
            
            # Verify
            if [ -L "${'$'}GADGET_DIR/configs/c.1/ffs.portal0" ]; then
                echo "✓ Setup complete"
            else
                echo "✗ Setup failed"
                exit 1
            fi
        """.trimIndent()
    }

    private fun getGadgetCleanupScript(): String {
        return """
            set -x
            
            GADGET=/config/usb_gadget/kaos_portal
            
            # Unbind from UDC
            if [ -f "${'$'}GADGET/UDC" ]; then
                echo "" > "${'$'}GADGET/UDC" 2>/dev/null || true
                sleep 0.5
            fi
            
            # Unmount FunctionFS
            umount /dev/usb-ffs/portal0 2>/dev/null || true
            rmdir /dev/usb-ffs/portal0 2>/dev/null || true
            
            # Remove symlink
            rm -f "${'$'}GADGET/configs/c.1/ffs.portal0" 2>/dev/null || true
            
            # Remove function
            rmdir "${'$'}GADGET/functions/ffs.portal0" 2>/dev/null || true
            
            # Remove config
            rmdir "${'$'}GADGET/configs/c.1/strings/0x409" 2>/dev/null || true
            rmdir "${'$'}GADGET/configs/c.1" 2>/dev/null || true
            
            # Remove device strings
            rmdir "${'$'}GADGET/strings/0x409" 2>/dev/null || true
            
            # Remove gadget
            rmdir "${'$'}GADGET" 2>/dev/null || true
            
            # Restore Android USB
            setprop sys.usb.config mtp,adb
            start adbd
            
            echo "Cleanup complete"
        """.trimIndent()
    }

    private fun setupFunctionFS(): Boolean {
        try {
            Log.i(TAG, "Setting up FunctionFS for portal0...")

            // Create mount point
            val createDir = Runtime.getRuntime().exec(arrayOf(
                "su", "-c", "mkdir -p /dev/usb-ffs/portal0"
            ))
            createDir.waitFor()

            // Check if already mounted
            val checkMount = Runtime.getRuntime().exec(arrayOf(
                "su", "-c", "mount | grep portal0"
            ))
            checkMount.waitFor()

            val mountOutput = checkMount.inputStream.bufferedReader().readText()
            if (mountOutput.contains("portal0")) {
                Log.i(TAG, "FunctionFS already mounted for portal0")
                return true
            }

            // Mount functionfs
            val mount = Runtime.getRuntime().exec(arrayOf(
                "su", "-c", "mount -t functionfs portal0 /dev/usb-ffs/portal0"
            ))
            val mountResult = mount.waitFor()

            if (mountResult != 0) {
                val error = mount.errorStream.bufferedReader().readText()
                Log.e(TAG, "Failed to mount functionfs: $error")
                return false
            }

            Log.i(TAG, "FunctionFS mounted successfully")

            // Verify mount
            val verify = Runtime.getRuntime().exec(arrayOf(
                "su", "-c", "ls -la /dev/usb-ffs/portal0"
            ))
            verify.waitFor()
            val verifyOutput = verify.inputStream.bufferedReader().readText()
            Log.i(TAG, "Mount verification: $verifyOutput")

            return true

        } catch (e: Exception) {
            Log.e(TAG, "Exception setting up FunctionFS: ${e.message}")
            return false
        }
    }

    private fun cleanupFunctionFS() {
        val (result, output) = runRootScriptWithOutput("""
            # Check if mounted
            if mount | grep -q "portal0"; then
                echo "Unmounting FunctionFS..."
                umount /dev/usb-ffs/portal0 2>&1
                
                if [ ${'$'}? -eq 0 ]; then
                    echo "Unmounted successfully"
                else
                    echo "Unmount failed or already unmounted"
                fi
            else
                echo "FunctionFS not mounted"
            fi
            
            # Remove directory if it exists
            if [ -d /dev/usb-ffs/portal0 ]; then
                rmdir /dev/usb-ffs/portal0 2>&1 || echo "Directory cleanup done"
            fi
            
            # Verify
            if [ -d /dev/usb-ffs/portal0 ]; then
                echo "WARNING: Directory still exists"
                ls -la /dev/usb-ffs/portal0/ 2>&1 || true
            else
                echo "Cleanup complete"
            fi
        """.trimIndent())
        Log.d(TAG, "FunctionFS cleanup: $output")
    }

    override fun onDestroy() {
        super.onDestroy()
        if (gadgetActive) {
            daemonProcess?.destroy()
        }
    }
}

data class SlotState(
    val index: Int,
    var file: File?,
    var loaded: Boolean
)