using System;
using System.IO;

namespace WindBot.Undo
{
    // Each override calls only inner: no double accounting through another override.
    public sealed class ReplayRandom : Random
    {
        private readonly Random inner;
        private readonly DecisionTape tape;
        public ReplayRandom(int seed, DecisionTape tape)
        {
            if (tape == null) throw new ArgumentNullException("tape");
            inner = new Random(seed);
            this.tape = tape;
        }
        private void Record(string call, byte[] input, byte[] output)
        {
            tape.Observe(new TapeEntry { Kind = TapeKind.Random, Call = call, Input = input, Output = output });
        }
        internal static byte[] Encode(Action<BinaryWriter> write)
        {
            using (var stream = new MemoryStream())
            using (var writer = new BinaryWriter(stream))
            {
                write(writer);
                writer.Flush();
                return stream.ToArray();
            }
        }
        public override int Next()
        {
            int value = inner.Next();
            Record("Next", new byte[0], Encode(w => w.Write(value)));
            return value;
        }
        public override int Next(int maxValue)
        {
            int value = inner.Next(maxValue);
            Record("Next(max)", Encode(w => w.Write(maxValue)), Encode(w => w.Write(value)));
            return value;
        }
        public override int Next(int minValue, int maxValue)
        {
            int value = inner.Next(minValue, maxValue);
            Record("Next(min,max)", Encode(w => { w.Write(minValue); w.Write(maxValue); }), Encode(w => w.Write(value)));
            return value;
        }
        public override double NextDouble()
        {
            double value = inner.NextDouble();
            Record("NextDouble", new byte[0], Encode(w => w.Write(value)));
            return value;
        }
        public override void NextBytes(byte[] buffer)
        {
            inner.NextBytes(buffer);
            Record("NextBytes", Encode(w => w.Write(buffer.Length)), buffer);
        }
        protected override double Sample()
        {
            double value = inner.NextDouble();
            Record("Sample", new byte[0], Encode(w => w.Write(value)));
            return value;
        }
    }
}