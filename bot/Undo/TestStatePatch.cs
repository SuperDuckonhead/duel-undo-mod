using System;
using System.IO;
using WindBot.Game;
using YGOSharp.OCGWrapper.Enums;

namespace WindBot.Undo
{
    // Recipient input only. The ordinal belongs to this prepared continuation;
    // it is never a card identity and never reaches an executor.
    public sealed class TestStatePatch
    {
        public const uint Version = 1;
        internal byte Recipient, Owner, Controller, Sequence;
        internal ushort ExpectedCount;

        internal static TestStatePatch Decode(byte[] bytes)
        {
            if (bytes == null || bytes.Length != 16) throw new InvalidOperationException("Invalid TestStatePatch size");
            using (var reader = new BinaryReader(new MemoryStream(bytes)))
            {
                if (reader.ReadUInt32() != Version) throw new InvalidOperationException("Unsupported TestStatePatch version");
                if (reader.ReadUInt32() != 0) throw new InvalidOperationException("Out-of-order TestStatePatch birth");
                var patch = new TestStatePatch { Recipient = reader.ReadByte(), Owner = reader.ReadByte(), Controller = reader.ReadByte() };
                byte location = reader.ReadByte(); patch.Sequence = reader.ReadByte(); byte position = reader.ReadByte();
                patch.ExpectedCount = reader.ReadUInt16();
                if (patch.Recipient > 1 || patch.Owner > 1 || patch.Controller != patch.Owner ||
                    location != (byte)CardLocation.Deck || position != (byte)CardPosition.FaceDownDefence ||
                    patch.ExpectedCount > 126 || patch.Sequence > patch.ExpectedCount)
                    throw new InvalidOperationException("Invalid TestStatePatch source");
                return patch;
            }
        }

        internal void Apply(Duel duel)
        {
            if (Recipient != (duel.IsFirst ? 0 : 1)) throw new InvalidOperationException("TestStatePatch recipient mismatch");
            int player = duel.GetLocalPlayer(Controller);
            ClientField field = duel.Fields[player];
            if (field.Deck == null || field.Deck.Count != ExpectedCount)
                throw new InvalidOperationException("TestStatePatch source count mismatch");
            var card = new ClientCard(0, CardLocation.Deck, Sequence, (int)CardPosition.FaceDownDefence);
            card.Owner = duel.GetLocalPlayer(Owner); card.Controller = player;
            card.UncountedDeckSource = true;
            field.Deck.Insert(Sequence, card);
            for (int i = Sequence; i < field.Deck.Count; ++i) field.Deck[i].Sequence = i;
        }
    }
}
