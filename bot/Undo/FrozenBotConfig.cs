using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Xml;

namespace WindBot.Undo
{
    public sealed class FrozenBotConfig
    {
        public string Source;
        public byte[] Content, Sha256;
        internal FrozenBotConfig Copy()
        {
            if (string.IsNullOrWhiteSpace(Source) || Source.Length > 1024 || Source.IndexOf('\0') >= 0 || Content == null || Content.Length > 1024 * 1024 || Sha256 == null || Sha256.Length != 32)
                throw new InvalidOperationException("Invalid frozen configuration binding");
            using (var sha = SHA256.Create())
                if (!sha.ComputeHash(Content).SequenceEqual(Sha256)) throw new InvalidOperationException("Frozen configuration hash changed: " + Source);
            return new FrozenBotConfig { Source = Source, Content = (byte[])Content.Clone(), Sha256 = (byte[])Sha256.Clone() };
        }
        internal void Write(BinaryWriter writer)
        {
            var value = Copy(); FrozenCardView.WriteText(writer, value.Source);
            WorkerWire.WriteBytes(writer, value.Content); writer.Write(value.Sha256);
        }
        internal static FrozenBotConfig Read(BinaryReader reader)
        {
            return new FrozenBotConfig { Source = FrozenCardView.ReadText(reader), Content = WorkerWire.ReadBytes(reader), Sha256 = reader.ReadBytes(32) }.Copy();
        }
        internal Dictionary<string, string> Fields()
        {
            using (var reader = new StreamReader(new MemoryStream(Content), System.Text.Encoding.UTF8, true))
                return Config.ReadFields(reader);
        }
        internal Dictionary<string, string> ApplicationFields()
        {
            var document = new XmlDocument { XmlResolver = null };
            using (var reader = XmlReader.Create(new MemoryStream(Content), new XmlReaderSettings { DtdProcessing = DtdProcessing.Prohibit, XmlResolver = null })) document.Load(reader);
            if (document.DocumentElement == null || document.DocumentElement.Name != "configuration") throw new InvalidOperationException("Invalid frozen application configuration");
            var sections = document.DocumentElement.SelectNodes("appSettings");
            if (sections.Count > 1) throw new InvalidOperationException("Duplicate frozen appSettings section");
            var result = new Dictionary<string, string>();
            if (sections.Count == 0) return result;
            var section = sections[0];
            if (section.Attributes.Count != 0) throw new InvalidOperationException("External appSettings must be flattened by host capture");
            foreach (XmlNode node in section.ChildNodes)
            {
                if (node.NodeType == XmlNodeType.Comment || node.NodeType == XmlNodeType.Whitespace) continue;
                if (node.Name == "clear" && node.Attributes.Count == 0) { result.Clear(); continue; }
                if (node.Name == "remove" && node.Attributes.Count == 1 && node.Attributes["key"] != null) { result.Remove(node.Attributes["key"].Value.ToUpper()); continue; }
                if (node.Name != "add" || node.Attributes.Count != 2 || node.Attributes["key"] == null || node.Attributes["value"] == null)
                    throw new InvalidOperationException("Invalid frozen appSettings entry");
                string key = node.Attributes["key"].Value.ToUpper();
                if (result.ContainsKey(key)) throw new InvalidOperationException("Invalid application configuration: duplicate key '" + key + "'");
                result.Add(key, node.Attributes["value"].Value);
            }
            return result;
        }
    }
}