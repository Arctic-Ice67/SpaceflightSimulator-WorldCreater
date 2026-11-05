using System;
using System.Collections.Generic;

namespace PlanetEditor
{
    /// <summary>包的作者、版本、描述，存放在包根目录的 WorldCreater_Meta.txt。</summary>
    internal static class PackMeta
    {
        public const string FileName = "WorldCreater_Meta.txt";
        public const string DefaultAuthor = "World Creater";
        public const string DefaultVersion = "1.0";
        public const string DefaultDescription = "Created in game by World Creater";

        public static string Author = DefaultAuthor;
        public static string Version = DefaultVersion;
        public static string Description = DefaultDescription;

        /// <summary>从包里读元数据，文件不在就用默认值。</summary>
        public static void Load(IFolder pack)
        {
            Author = DefaultAuthor;
            Version = DefaultVersion;
            Description = DefaultDescription;
            if (pack == null) return;
            try
            {
                IFile f = pack.GetFile(FileName);
                if (!f.Exists()) return;
                string[] lines = f.ReadText().Split('\n');
                for (int i = 0; i < lines.Length; i++)
                {
                    string line = lines[i].Trim();
                    if (line.Length == 0 || line[0] == '#') continue;
                    int p = line.IndexOf('=');
                    if (p <= 0) continue;
                    string key = line.Substring(0, p).Trim().ToLowerInvariant();
                    string val = line.Substring(p + 1).Trim();
                    if (key == "author") Author = val;
                    else if (key == "version") Version = val;
                    else if (key == "description") Description = val;
                }
            }
            catch (Exception e) { Log.Error("meta load: " + e.Message); }
        }

        /// <summary>把当前元数据写回包里。</summary>
        public static void Save(IFolder pack)
        {
            if (pack == null) return;
            try
            {
                string text = "# WorldCreater 包信息\n"
                    + "author=" + Author + "\n"
                    + "version=" + Version + "\n"
                    + "description=" + Description + "\n";
                pack.GetFile(FileName).WriteText(text);
            }
            catch (Exception e) { Log.Error("meta save: " + e.Message); }
        }

        /// <summary>改一项元数据并立刻存盘，返回是否成功。</summary>
        public static bool Set(IFolder pack, string key, string value)
        {
            if (pack == null) return false;
            string k = key == null ? "" : key.Trim().ToLowerInvariant();
            if (k == "author") Author = value;
            else if (k == "version") Version = value;
            else if (k == "description") Description = value;
            else return false;
            Save(pack);
            return true;
        }

        /// <summary>取一项元数据。</summary>
        public static string Get(string key)
        {
            string k = key == null ? "" : key.Trim().ToLowerInvariant();
            if (k == "author") return Author;
            if (k == "version") return Version;
            if (k == "description") return Description;
            return "";
        }

        /// <summary>打包成 key=value 多行，给界面显示用。</summary>
        public static List<string> Lines()
        {
            var list = new List<string>();
            list.Add("author=" + Author);
            list.Add("version=" + Version);
            list.Add("description=" + Description);
            return list;
        }
    }
}
