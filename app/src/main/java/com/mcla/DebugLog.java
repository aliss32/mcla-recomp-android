package com.mcla;

import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Date;
import java.util.Locale;

/**
 * "Play (Debug Log)": the game process records its own logcat - the runtime,
 * the Vulkan driver, SDL and the crash buffer - into
 * files/debug_logs/debug-DATE.log, so a player can attach one file to an
 * issue. Apps can read only their own logs, which is exactly what is wanted.
 *
 * <p>logcat runs as a child of the game process and would outlive a crash,
 * so a leftover writer is ended before the next game start and whenever the
 * launcher comes back; the file is size-capped either way.</p>
 */
final class DebugLog {

    private static final String TAG = "MclaDebugLog";
    static final String DIRECTORY = "debug_logs";
    private static final int KEEP = 5;
    /** logcat -r is in kilobytes; with -n 1 at most twice this on disk. */
    private static final String ROTATE_KB = "8192";

    private DebugLog() {}

    static File directory(File dataRoot) {
        return new File(dataRoot, DIRECTORY);
    }

    /** Starts recording; returns the log file, or null if logcat could not start. */
    static File start(File dataRoot) {
        stopLeftovers(dataRoot);
        File dir = directory(dataRoot);
        if (!dir.isDirectory() && !dir.mkdirs()) {
            Log.e(TAG, "cannot create " + dir);
            return null;
        }
        prune(dir);
        String stamp = new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(new Date());
        File file = new File(dir, "debug-" + stamp + ".log");
        try {
            new ProcessBuilder("logcat", "-v", "threadtime", "-b", "main,system,crash", "-T", "500",
                    "-f", file.getAbsolutePath(), "-r", ROTATE_KB, "-n", "1")
                    .redirectErrorStream(true)
                    .start();
            Log.i(TAG, "recording the game log to " + file);
            return file;
        } catch (IOException e) {
            Log.e(TAG, "logcat could not start", e);
            return null;
        }
    }

    /** Ends logcat writers into debug_logs left over from an earlier session. */
    static void stopLeftovers(File dataRoot) {
        String marker = new File(dataRoot, DIRECTORY).getAbsolutePath();
        File[] processes = new File("/proc").listFiles();
        if (processes == null) return;
        for (File process : processes) {
            String name = process.getName();
            if (name.isEmpty() || !Character.isDigit(name.charAt(0))) continue;
            String commandLine = read(new File(process, "cmdline"));
            if (!commandLine.startsWith("logcat") || !commandLine.contains(marker)) continue;
            try {
                android.os.Process.sendSignal(Integer.parseInt(name), android.os.Process.SIGNAL_KILL);
                Log.i(TAG, "ended leftover log writer " + name);
            } catch (RuntimeException ignored) {
            }
        }
    }

    private static void prune(File dir) {
        File[] files = dir.listFiles((d, n) -> n.startsWith("debug-"));
        if (files == null || files.length < KEEP) return;
        Arrays.sort(files);
        for (int i = 0; i <= files.length - KEEP; ++i) {
            //noinspection ResultOfMethodCallIgnored
            files[i].delete();
        }
    }

    private static String read(File file) {
        try (InputStream in = new FileInputStream(file)) {
            byte[] buffer = new byte[4096];
            int length = in.read(buffer);
            if (length <= 0) return "";
            return new String(buffer, 0, length, StandardCharsets.UTF_8).replace('\0', ' ');
        } catch (IOException e) {
            return "";
        }
    }
}
