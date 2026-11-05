using System;
using System.Collections.Generic;

namespace PlanetEditor
{
    /// <summary>新建星球的模板：内置几种常见天体，也可以把当前星球存成自己的模板。</summary>
    internal static class PackTemplates
    {
        public const string FileName = "WorldCreater_Templates.txt";

        // key, 显示名, 来源星球（空表示特殊构造）
        private static readonly string[][] Builtin = new string[][]
        {
            new[] { "earth", "类地行星", "Earth" },
            new[] { "rocky", "岩石行星", "Mars" },
            new[] { "gas", "气态巨行星", "Jupiter" },
            new[] { "ice", "冰卫星", "Europa" },
            new[] { "asteroid", "小行星", "Phobos" },
            new[] { "bary", "双星质心", "" },
        };

        /// <summary>列出全部模板：内置在前，自定义在后。</summary>
        public static List<string> List(IFolder pack)
        {
            var list = new List<string>();
            for (int i = 0; i < Builtin.Length; i++)
                list.Add(Builtin[i][0] + "\t" + Builtin[i][1] + "\t" + Builtin[i][2]);
            foreach (string line in ReadCustom(pack))
            {
                int p = line.IndexOf('=');
                if (p <= 0) continue;
                list.Add(line.Substring(0, p).Trim() + "\t" + line.Substring(0, p).Trim() + "\t" + line.Substring(p + 1).Trim());
            }
            return list;
        }

        /// <summary>按 key 找模板的来源星球名，找不到返回 null。</summary>
        public static string Resolve(IFolder pack, string key)
        {
            if (string.IsNullOrEmpty(key)) return null;
            string k = key.Trim().ToLowerInvariant();
            for (int i = 0; i < Builtin.Length; i++)
            {
                if (Builtin[i][0] == k) return Builtin[i][2];
            }
            foreach (string line in ReadCustom(pack))
            {
                int p = line.IndexOf('=');
                if (p <= 0) continue;
                if (line.Substring(0, p).Trim().ToLowerInvariant() == k)
                    return line.Substring(p + 1).Trim();
            }
            return null;
        }

        /// <summary>把当前星球存成自定义模板。</summary>
        public static bool Save(IFolder pack, string name, string codeName)
        {
            if (pack == null || string.IsNullOrEmpty(name) || string.IsNullOrEmpty(codeName)) return false;
            try
            {
                List<string> lines = ReadCustom(pack);
                for (int i = 0; i < lines.Count; i++)
                {
                    int p = lines[i].IndexOf('=');
                    if (p <= 0) continue;
                    if (lines[i].Substring(0, p).Trim().Equals(name.Trim(), StringComparison.OrdinalIgnoreCase))
                    {
                        lines[i] = name.Trim() + "=" + codeName;
                        WriteCustom(pack, lines);
                        return true;
                    }
                }
                lines.Add(name.Trim() + "=" + codeName);
                WriteCustom(pack, lines);
                return true;
            }
            catch (Exception e) { Log.Error("save template: " + e.Message); return false; }
        }

        /// <summary>读自定义模板列表。</summary>
        private static List<string> ReadCustom(IFolder pack)
        {
            var list = new List<string>();
            if (pack == null) return list;
            try
            {
                IFile f = pack.GetFile(FileName);
                if (!f.Exists()) return list;
                string[] lines = f.ReadText().Split('\n');
                for (int i = 0; i < lines.Length; i++)
                {
                    string line = lines[i].Trim();
                    if (line.Length == 0 || line[0] == '#') continue;
                    list.Add(line);
                }
            }
            catch (Exception e) { Log.Error("read templates: " + e.Message); }
            return list;
        }

        /// <summary>写自定义模板列表。</summary>
        private static void WriteCustom(IFolder pack, List<string> lines)
        {
            var sb = new System.Text.StringBuilder();
            sb.Append("# WorldCreater 自定义模板，每行 名字=来源星球\n");
            for (int i = 0; i < lines.Count; i++) sb.Append(lines[i]).Append('\n');
            pack.GetFile(FileName).WriteText(sb.ToString());
        }
    }
}
