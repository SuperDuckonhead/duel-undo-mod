using System.Collections.Generic;
using System.Data;
using Mono.Data.Sqlite;
using System;
using System.IO;
using System.Linq;

namespace YGOSharp.OCGWrapper
{
    public static class NamedCardsManager
    {
        private static IDictionary<int, NamedCard> _cards;

        public static void Init(string databaseFullPath, bool readOnly = false)
        {
            try
            {
                if (!File.Exists(databaseFullPath))
                {
                    throw new Exception("Could not find the cards database.");
                }

                _cards = new Dictionary<int, NamedCard>();

                var settings = new SqliteConnectionStringBuilder { DataSource = databaseFullPath, ReadOnly = readOnly };
                using (SqliteConnection connection = new SqliteConnection(settings.ConnectionString))
                {
                    connection.Open();

                    using (IDbCommand command = new SqliteCommand(
                        "SELECT datas.id, ot, alias, setcode, type, level, race, attribute, atk, def, texts.name, texts.desc"
                        + " FROM datas INNER JOIN texts ON datas.id = texts.id",
                        connection))
                    {
                        using (IDataReader reader = command.ExecuteReader())
                        {
                            while (reader.Read())
                            {
                                LoadCard(reader);
                            }
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                throw new Exception("Could not initialize the cards database. Check the inner exception for more details.", ex);
            }
        }

        internal static void InitFrozen(WindBot.Undo.FrozenCard[] cards)
        {
            _cards = new Dictionary<int, NamedCard>();
            foreach (var card in cards) _cards.Add((int)card.Code, new NamedCard(card));
        }

        internal static NamedCard GetCard(int id)
        {
            if (_cards.ContainsKey(id))
                return _cards[id];
            return null;
        }

        public static IList<NamedCard> GetAllCards()
        {
            return _cards.Values.ToList();
        }

        private static void LoadCard(IDataRecord reader)
        {
            NamedCard card = new NamedCard(reader);
            _cards.Add(card.Id, card);
        }
    }
}