using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Security.Cryptography;
using YGOSharp.OCGWrapper;

namespace WindBot.Undo
{
    public sealed class FrozenCard
    {
        public uint Code, Alias, Type, Level, Attribute, Race, LScale, RScale, LinkMarker, RuleCode, Ot;
        public int Attack, Defense;
        public ushort[] Setcodes = new ushort[16];
        public string Name = "", Text = "";
        public string[] Descriptions = Enumerable.Repeat("", 16).ToArray();
    }
    public static class FrozenCardView
    {
        public static byte[] Encode(byte[] engine, byte[] resources, IEnumerable<FrozenCard> cards)
        {
            if (engine.Length != 32 || resources.Length != 32) throw new InvalidOperationException("Invalid resource binding");
            var sorted = cards.OrderBy(c => c.Code).ToArray();
            byte[] bytes = ReplayRandom.Encode(w =>
            {
                w.Write(1); w.Write(engine); w.Write(resources); w.Write(sorted.Length);
                foreach (var c in sorted)
                {
                    w.Write(c.Code); w.Write(c.Alias);
                    if (c.Setcodes.Length != 16 || c.Descriptions.Length != 16) throw new InvalidOperationException("Invalid normalized card");
                    foreach (ushort set in c.Setcodes) w.Write(set);
                    w.Write(c.Type); w.Write(c.Level); w.Write(c.Attribute); w.Write(c.Race); w.Write(c.Attack); w.Write(c.Defense);
                    w.Write(c.LScale); w.Write(c.RScale); w.Write(c.LinkMarker); w.Write(c.RuleCode); w.Write(c.Ot);
                    WriteText(w, c.Name); WriteText(w, c.Text); foreach (string desc in c.Descriptions) WriteText(w, desc);
                }
            });
            using (var sha = SHA256.Create()) return bytes.Concat(sha.ComputeHash(bytes)).ToArray();
        }
        internal static void WriteText(BinaryWriter w, string text) { WorkerWire.WriteBytes(w, Encoding.UTF8.GetBytes(text)); }
        internal static string ReadText(BinaryReader r) { return new UTF8Encoding(false, true).GetString(WorkerWire.ReadBytes(r)); }
        public static FrozenCard[] Decode(byte[] bytes, byte[] engine, byte[] resources)
        {
            if (bytes == null || bytes.Length < 104 || bytes.Length > 32 * 1024 * 1024 || engine.Length != 32 || resources.Length != 32)
                throw new InvalidOperationException("Invalid frozen card view");
            byte[] payload = bytes.Take(bytes.Length - 32).ToArray();
            using (var sha = SHA256.Create())
                if (!sha.ComputeHash(payload).SequenceEqual(bytes.Skip(payload.Length))) throw new InvalidOperationException("Frozen card bridge digest mismatch");
            using (var stream = new MemoryStream(payload)) using (var r = new BinaryReader(stream))
            {
                if (r.ReadInt32() != 1 || !r.ReadBytes(32).SequenceEqual(engine) || !r.ReadBytes(32).SequenceEqual(resources))
                    throw new InvalidOperationException("Engine/core resource view mismatch");
                int count = r.ReadInt32(); if (count < 0 || count > 200000) throw new InvalidOperationException("Invalid card count");
                var result = new List<FrozenCard>(); uint previous = 0;
                for (int i = 0; i < count; ++i)
                {
                    var c = new FrozenCard { Code = r.ReadUInt32(), Alias = r.ReadUInt32() };
                    if (c.Code <= previous) throw new InvalidOperationException("Cards are not uniquely ordered"); previous = c.Code;
                    for (int j = 0; j < 16; j++) c.Setcodes[j] = r.ReadUInt16();
                    c.Type = r.ReadUInt32(); c.Level = r.ReadUInt32(); c.Attribute = r.ReadUInt32(); c.Race = r.ReadUInt32();
                    c.Attack = r.ReadInt32(); c.Defense = r.ReadInt32(); c.LScale = r.ReadUInt32(); c.RScale = r.ReadUInt32(); c.LinkMarker = r.ReadUInt32(); c.RuleCode = r.ReadUInt32(); c.Ot = r.ReadUInt32();
                    c.Name = ReadText(r); c.Text = ReadText(r); for (int j = 0; j < 16; j++) c.Descriptions[j] = ReadText(r);
                    result.Add(c);
                }
                if (stream.Position != stream.Length) throw new InvalidOperationException("Trailing card view data");
                return result.ToArray();
            }
        }
    }
}