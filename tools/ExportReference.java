// Export the analyzed reference with Ghidra's C/C++ exporter.
//@category TOCA
import java.io.File;
import java.util.zip.CRC32;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.exporter.CppExporter;

public class ExportReference extends GhidraScript {
    @Override
    public void run() throws Exception {
        var files = currentProgram.getMemory().getAllFileBytes();
        if (files.size() != 1 || files.get(0).getSize() != 790016) {
            throw new IllegalStateException("Expected the single imported TOCA 1.1a executable.");
        }
        CRC32 checksum = new CRC32();
        byte[] buffer = new byte[65536];
        var source = files.get(0);
        for (long offset = 0; offset < source.getSize();) {
            monitor.checkCancelled();
            int count = source.getOriginalBytes(offset, buffer, 0,
                (int) Math.min(buffer.length, source.getSize() - offset));
            if (count <= 0) {
                throw new IllegalStateException("Incomplete imported executable bytes.");
            }
            checksum.update(buffer, 0, count);
            offset += count;
        }
        if (checksum.getValue() != 0x8ce56d41L) {
            throw new IllegalStateException("The Ghidra program is not the TOCA 1.1a reference.");
        }
        File output = new File(getScriptArgs()[0]);
        CppExporter exporter = new CppExporter(null, true, true, true, true, true, null);
        if (!exporter.export(output, currentProgram, null, monitor)) {
            throw new IllegalStateException("C export failed: " + exporter.getMessageLog());
        }
        println("Exported: " + output);
        println("Recognized functions: " + currentProgram.getFunctionManager().getFunctionCount());
    }
}
