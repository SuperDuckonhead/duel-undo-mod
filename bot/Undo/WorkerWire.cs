using System;
using System.IO;

namespace WindBot.Undo
{
    // Private W1 test transport over redirected standard streams; not the W2 host protocol.
    internal static class WorkerWire
    {
        internal const int MaximumFrame = 64 * 1024 * 1024;
        internal static void WriteBytes(BinaryWriter w, byte[] data) { w.Write(data.Length); w.Write(data); }
        internal static byte[] ReadBytes(BinaryReader r)
        {
            int length = r.ReadInt32();
            if (length < 0 || length > MaximumFrame) throw new InvalidOperationException("Invalid worker frame size");
            byte[] bytes = r.ReadBytes(length);
            if (bytes.Length != length) throw new EndOfStreamException();
            return bytes;
        }
        internal static void WriteTape(BinaryWriter w, TapeEntry[] entries)
        {
            w.Write(entries.Length);
            foreach (var e in entries) { w.Write((int)e.Kind); w.Write(e.Call); WriteBytes(w, e.Input); WriteBytes(w, e.Output); }
        }
        internal static TapeEntry[] ReadTape(BinaryReader r)
        {
            int length = r.ReadInt32();
            if (length < 0 || length > 1000000) throw new InvalidOperationException("Invalid tape length");
            var entries = new TapeEntry[length];
            for (int i = 0; i < length; i++) entries[i] = new TapeEntry { Kind = (TapeKind)r.ReadInt32(), Call = r.ReadString(), Input = ReadBytes(r), Output = ReadBytes(r) };
            return entries;
        }
    }
}