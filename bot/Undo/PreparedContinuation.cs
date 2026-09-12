using System;
using System.Collections.Generic;
using System.IO;
using YGOSharp.OCGWrapper.Enums;

namespace WindBot.Undo
{
    // Exactly one birth followed by recipient-filtered STOC packets. Historical
    // selections for another participant must never be supplied as AI prompts.
    internal sealed class PreparedContinuation
    {
        internal byte[] Birth;
        internal readonly List<byte[]> Messages = new List<byte[]>();
        internal static PreparedContinuation Decode(byte[] input)
        {
            if (input == null || input.Length > 4 * 1024 * 1024) throw new InvalidOperationException("Invalid continuation size");
            using (var stream = new MemoryStream(input)) using (var r = new BinaryReader(stream))
            {
                if (r.ReadUInt32() != TestStatePatch.Version) throw new InvalidOperationException("Unsupported continuation version");
                uint count = r.ReadUInt32();
                if (count == 0 || count > 4096) throw new InvalidOperationException("Invalid continuation count");
                var result = new PreparedContinuation();
                for (uint i = 0; i < count; ++i)
                {
                    byte kind = r.ReadByte(); int size = r.ReadInt32();
                    if (size < 2 || size > 65535 || (i == 0 && size != 16)) throw new InvalidOperationException("Invalid continuation event size");
                    byte[] bytes = r.ReadBytes(size);
                    if (bytes.Length != size) throw new InvalidOperationException("Truncated continuation event");
                    if (i == 0)
                    {
                        if (kind != 1) throw new InvalidOperationException("Continuation must start with birth");
                        TestStatePatch.Decode(bytes); result.Birth = bytes;
                    }
                    else
                    {
                        if (kind != 0 || bytes.Length < 2 || bytes.Length > 65535 || bytes[0] != 1)
                            throw new InvalidOperationException("Invalid or duplicate continuation event");
                        var message = (GameMessage)bytes[1];
                        bool prompt = (bytes[1] >= 10 && bytes[1] <= 26) || bytes[1] == 132 || (bytes[1] >= 140 && bytes[1] <= 143);
                        int seat = message == GameMessage.SelectSum ? 3 : 2;
                        if (prompt && (bytes.Length <= seat || bytes[seat] != result.Birth[8]))
                            throw new InvalidOperationException("Continuation contains another participant's selection");
                        result.Messages.Add(bytes);
                    }
                }
                if (stream.Position != stream.Length) throw new InvalidOperationException("Trailing continuation bytes");
                return result;
            }
        }
        internal void Apply(ReplaySession candidate)
        {
            candidate.ApplyTestStatePatch(Birth);
            foreach (byte[] message in Messages) candidate.Dispatch(message);
            if (!candidate.IsConnected) throw new InvalidOperationException("Prepared continuation closed client");
        }
    }
}
