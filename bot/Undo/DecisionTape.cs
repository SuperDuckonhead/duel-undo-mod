using System;
using System.Collections.Generic;
using System.Linq;

namespace WindBot.Undo
{
    public enum TapeKind { Message, Decision, Random, Clock, External }

    public sealed class TapeEntry
    {
        public TapeKind Kind;
        public string Call;
        public byte[] Input;
        public byte[] Output;
    }

    public sealed class DecisionTape
    {
        private readonly List<TapeEntry> recorded = new List<TapeEntry>();
        private TapeEntry[] expected;
        private int cursor;
        private bool failed;
        public int Cursor { get { return expected == null ? recorded.Count : cursor; } }

        internal static TapeEntry Clone(TapeEntry entry)
        {
            if (entry == null || entry.Call == null || entry.Input == null || entry.Output == null || !Enum.IsDefined(typeof(TapeKind), entry.Kind))
                throw new InvalidOperationException("Invalid AI event");
            return new TapeEntry { Kind = entry.Kind, Call = entry.Call, Input = (byte[])entry.Input.Clone(), Output = (byte[])entry.Output.Clone() };
        }

        public void Observe(TapeEntry entry)
        {
            if (failed) throw new InvalidOperationException("AI tape has failed");
            TapeEntry copy = Clone(entry);
            if (expected != null)
            {
                if (cursor >= expected.Length || expected[cursor].Kind != copy.Kind || expected[cursor].Call != copy.Call ||
                    !expected[cursor].Input.SequenceEqual(copy.Input) || !expected[cursor].Output.SequenceEqual(copy.Output))
                {
                    failed = true;
                    throw new InvalidOperationException("AI event diverged at " + cursor + " (" + copy.Kind + ":" + copy.Call + ")");
                }
                cursor++;
            }
            recorded.Add(copy);
        }

        public void BeginReplay(TapeEntry[] entries)
        {
            if (entries == null) throw new ArgumentNullException("entries");
            if (recorded.Count != 0 || expected != null || failed) throw new InvalidOperationException("Replay needs a fresh tape");
            expected = entries.Select(Clone).ToArray();
            cursor = 0;
        }

        public void RequireEnd()
        {
            if (failed || (expected != null && cursor != expected.Length))
                throw new InvalidOperationException("Missing AI events or failed tape");
        }

        internal void ContinueRecording()
        {
            RequireEnd();
            expected = null;
        }

        public TapeEntry[] Snapshot() { return recorded.Select(Clone).ToArray(); }
    }
}