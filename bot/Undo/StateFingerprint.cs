using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using WindBot.Game;
using WindBot.Game.AI;
using YGOSharp.Network;

namespace WindBot.Undo
{
    // Diagnostic only: reconstruction always executes callbacks and validates the
    // complete transcript. This digest is never a serialized restore mechanism.
    internal sealed class StateFingerprint
    {
        private readonly List<object> seen = new List<object>();
        private readonly BinaryWriter writer;
        private StateFingerprint(BinaryWriter writer) { this.writer = writer; }
        internal static byte[] Compute(object state, TapeEntry[] tape)
        {
            byte[] bytes = ReplayRandom.Encode(w =>
            {
                new StateFingerprint(w).Write(state);
                WorkerWire.WriteTape(w, tape); // includes initialization and random/external cursors
            });
            using (var sha = SHA256.Create()) return sha.ComputeHash(bytes);
        }
        private void Write(object value)
        {
            if (value == null) { writer.Write("null"); return; }
            Type type = value.GetType();
            writer.Write(type.FullName);
            if (value is GameClient || value is BinaryClient || value is Dialogs || value is Type) return;
            if (type.IsPrimitive || type.IsEnum || value is string || value is decimal)
            {
                writer.Write(Convert.ToString(value, CultureInfo.InvariantCulture));
                return;
            }
            if (!type.IsValueType)
            {
                int index = seen.FindIndex(item => ReferenceEquals(item, value));
                writer.Write(index);
                if (index >= 0) return;
                seen.Add(value);
            }
            var callback = value as Delegate;
            if (callback != null)
            {
                foreach (Delegate item in callback.GetInvocationList())
                {
                    writer.Write(item.Method.DeclaringType.FullName + "." + item.Method.Name);
                    Write(item.Target);
                }
                writer.Write("end-delegate");
                return;
            }
            var enumerable = value as IEnumerable;
            if (enumerable != null)
            {
                foreach (object item in enumerable) Write(item);
                writer.Write("end-sequence");
                return;
            }
            for (Type current = type; current != null; current = current.BaseType)
            {
                foreach (var field in current.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly).OrderBy(f => f.Name, StringComparer.Ordinal))
                {
                    writer.Write(current.FullName + "." + field.Name);
                    Write(field.GetValue(value));
                }
            }
            writer.Write("end-object");
        }
    }
}